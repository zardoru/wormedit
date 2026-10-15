#include <wormedit/visual/image_job.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace wormedit::visual {
namespace {

constexpr std::size_t hard_maximum_sources_per_request = 32;
constexpr std::size_t hard_maximum_image_jobs = 8;

auto is_image_source_current(const image_cache_entry_t& entry, song_id_t project,
                 std::uint64_t document_revision, const resolved_visual_asset_t& source) -> bool {
    return entry.project == project && entry.document_revision == document_revision &&
           entry.source.resource == source.resource && entry.source.asset == source.asset &&
           entry.source.kind == source.kind && entry.source.path == source.path &&
           entry.source.declared_width == source.declared_width &&
           entry.source.declared_height == source.declared_height;
}

auto append_within_result_limit(image_job_result_t& result, const resolved_visual_asset_t& source,
                                std::shared_ptr<const rgba_image_t> image,
                                std::size_t& result_bytes, std::size_t maximum_result_bytes)
    -> void {
    const auto bytes = image->pixels.size();
    if (bytes > maximum_result_bytes || result_bytes > maximum_result_bytes - bytes) {
        result.items.push_back(image_job_item_value_t::from(
            source, image_status_t::byte_limit_exceeded, {},
            "decoded image batch exceeds the configured byte limit"));
        return;
    }
    result_bytes += bytes;
    result.items.push_back(image_job_item_value_t::from(
        source, image_status_t::ready, std::move(image)));
}

auto decode_images(image_cache_t& cache, song_id_t project, std::uint64_t document_revision,
                   std::span<const resolved_visual_asset_t> sources, image_job_options_t options,
                   std::stop_token stop) -> task_result_t {
    image_job_result_t result{
        .project = project, .document_revision = document_revision, .items = {}};
    result.items.reserve(sources.size());
    std::size_t result_bytes{};
    for (const auto& source : sources) {
        if (stop.stop_requested()) {
            return task_result_t::failure(task_error_code_t::cancelled,
                                          "image decode request was superseded");
        }
        if (auto cached = cache.find(project, document_revision, source)) {
            append_within_result_limit(result, source, std::move(cached), result_bytes,
                                       options.maximum_result_rgba_bytes);
            continue;
        }
        auto decoded = open_image_rgba(source, options.decode_limits);
        if (!decoded) {
            result.items.push_back(image_job_item_value_t::from(
                source, decoded.error().code, {}, std::move(decoded.error().message)));
            continue;
        }
        if (stop.stop_requested()) {
            return task_result_t::failure(task_error_code_t::cancelled,
                                          "image decode request was superseded");
        }
        auto image = std::make_shared<const rgba_image_t>(std::move(*decoded));
        append_within_result_limit(result, source, image, result_bytes,
                                   options.maximum_result_rgba_bytes);
        if (result.items.back()) {
            static_cast<void>(cache.insert(project, document_revision, source, std::move(image)));
        }
    }
    auto payload = std::make_shared<const image_job_result_t>(std::move(result));
    return task_result_t::success(task_value_t::from(std::move(payload)));
}

} // namespace

auto image_job_result_t::find(resource_id_t resource) const noexcept -> const image_job_item_value_t* {
    for (const auto& item : items) {
        const auto& source = item ? item->context : item.error().context;
        if (source.resource == resource)
            return item && item->payload ? &*item : nullptr;
    }
    return nullptr;
}

image_cache_t::image_cache_t(std::size_t maximum_rgba_bytes, std::size_t maximum_entries)
    : maximum_rgba_bytes_(maximum_rgba_bytes), maximum_entries_(maximum_entries) {
    entries_.reserve(maximum_entries_);
}

auto image_cache_t::find(song_id_t project, std::uint64_t document_revision,
                         const resolved_visual_asset_t& source)
    -> std::shared_ptr<const rgba_image_t> {
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) {
        return is_image_source_current(entry, project, document_revision, source);
    });
    if (found == entries_.end()) {
        return {};
    }
    auto entry = std::move(*found);
    entries_.erase(found);
    entries_.push_back(std::move(entry));
    return entries_.back().image;
}

auto image_cache_t::insert(song_id_t project, std::uint64_t document_revision,
                           const resolved_visual_asset_t& source,
                           std::shared_ptr<const rgba_image_t> image)
    -> std::shared_ptr<const rgba_image_t> {
    if (!image) {
        return {};
    }
    const auto bytes = image->pixels.size();
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) {
        return is_image_source_current(entry, project, document_revision, source);
    });
    if (found != entries_.end()) {
        auto entry = std::move(*found);
        entries_.erase(found);
        entries_.push_back(std::move(entry));
        return entries_.back().image;
    }
    if (maximum_entries_ == 0 || bytes > maximum_rgba_bytes_) {
        return image;
    }
    while (!entries_.empty() &&
           (entries_.size() >= maximum_entries_ || rgba_bytes_ > maximum_rgba_bytes_ - bytes)) {
        rgba_bytes_ -= entries_.front().image->pixels.size();
        entries_.erase(entries_.begin());
    }
    entries_.push_back({.project = project,
                        .document_revision = document_revision,
                        .source = source,
                        .image = image});
    rgba_bytes_ += bytes;
    return image;
}

void image_cache_t::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    rgba_bytes_ = 0;
}

auto image_cache_t::size() const noexcept -> std::size_t {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

auto image_cache_t::rgba_bytes() const noexcept -> std::size_t {
    std::lock_guard lock(mutex_);
    return rgba_bytes_;
}

image_job_t::image_job_t(image_cache_t& cache, task_scheduler_t& scheduler,
                         std::size_t maximum_sources_per_request,
                         std::size_t maximum_in_flight_jobs)
    : cache_(cache), scheduler_(scheduler),
      maximum_sources_per_request_(
          std::min(maximum_sources_per_request, hard_maximum_sources_per_request)),
      maximum_in_flight_jobs_(std::min(maximum_in_flight_jobs, hard_maximum_image_jobs)) {
    workers_.reserve(maximum_in_flight_jobs_);
}

image_job_t::~image_job_t() {
    cancel();
    for (const auto& worker : workers_) {
        static_cast<void>(worker.graph.wait());
    }
}

void image_job_t::discard_terminal_jobs() {
    std::erase_if(workers_,
                  [](const auto& worker) { return worker.graph.outcome(worker.task).has_value(); });
}


auto image_job_t::begin(song_id_t project, std::uint64_t document_revision,
                          std::span<const resolved_visual_asset_t> sources,
                          image_job_options_t options) -> async_job_start_outcome_t {
    if (!project || document_revision == 0 || sources.empty() ||
        maximum_sources_per_request_ == 0 || maximum_in_flight_jobs_ == 0 ||
        sources.size() > maximum_sources_per_request_ || options.maximum_result_rgba_bytes == 0) {
        return std::unexpected(error_t<async_job_start_status_t>{image_job_start_status_t::invalid_request, "image decode request is empty or exceeds its configured bounds"});
    }
    discard_terminal_jobs();
    if (workers_.size() >= maximum_in_flight_jobs_) {
        return std::unexpected(error_t<async_job_start_status_t>{image_job_start_status_t::capacity_exceeded, "image decode jobs have not reached cancellation checkpoints"});
    }

    auto generation = next_generation_ + 1U;
    if (generation == 0) {
        generation = 1;
    }
    std::vector<resolved_visual_asset_t> copied_sources{sources.begin(), sources.end()};
    task_graph_t graph{1};
    const auto task =
        graph.add_task({}, [this, project, document_revision, sources = std::move(copied_sources),
                            options](std::span<const task_result_t>, std::stop_token stop) {
            return decode_images(cache_, project, document_revision, sources, options, stop);
        });
    if (!task) {
        return std::unexpected(error_t<async_job_start_status_t>{image_job_start_status_t::scheduler_rejected, task.error ? task.error->message : "image decode task could not be created"});
    }
    auto submitted = scheduler_.submit(std::move(graph));
    if (!submitted) {
        const auto status = submitted.error &&
                                    submitted.error->code == task_error_code_t::capacity_exceeded
                                ? async_job_start_status_t::capacity_exceeded
                                : async_job_start_status_t::scheduler_rejected;
        return std::unexpected(error_t<async_job_start_status_t>{status, submitted.error ? submitted.error->message
                                           : "image decode task could not be submitted"});
    }

    for (const auto& worker : workers_) {
        worker.graph.cancel();
    }
    next_generation_ = generation;
    current_generation_ = generation;
    workers_.push_back({.generation = generation,
                        .project = project,
                        .document_revision = document_revision,
                        .graph = std::move(submitted.graph),
                        .task = *task.id});
    return generation;
}

auto image_job_t::poll_completion() -> std::optional<image_job_completion_outcome_t> {
    std::optional<image_job_completion_outcome_t> completion;
    for (auto worker = workers_.begin(); worker != workers_.end();) {
        auto result = worker->graph.outcome(worker->task);
        if (!result) {
            ++worker;
            continue;
        }
        if (worker->generation == current_generation_) {
            completion = async_job_completion_value_t<async_job_project_identity_t, image_job_result_t>::from(
                worker->generation, async_job_project_identity_t{worker->project, worker->document_revision}, std::move(*result),
                "image decode worker returned no typed result");
            current_generation_ = 0;
        }
        worker = workers_.erase(worker);
    }
    return completion;
}

auto image_job_t::wait_completion() -> std::optional<image_job_completion_outcome_t> {
    const auto current = std::ranges::find(workers_, current_generation_, &worker_t::generation);
    if (current == workers_.end() || !current->graph.wait()) {
        return std::nullopt;
    }
    return poll_completion();
}

void image_job_t::cancel() noexcept {
    current_generation_ = 0;
    for (const auto& worker : workers_) {
        worker.graph.cancel();
    }
}

auto image_job_t::is_busy() const noexcept -> bool {
    return current_generation_ != 0;
}

} // namespace wormedit::visual
