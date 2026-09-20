#include <wormedit/visual/video_job.h>

#include <algorithm>
#include <ranges>
#include <utility>

namespace wormedit::visual {
namespace {

constexpr std::size_t hard_maximum_video_jobs = 4;

auto same_source(const resolved_visual_asset_t& left, const resolved_visual_asset_t& right)
    -> bool {
    return left.resource == right.resource && left.asset == right.asset &&
           left.kind == right.kind && left.path == right.path &&
           left.declared_width == right.declared_width &&
           left.declared_height == right.declared_height;
}

auto same_key(const video_frame_cache_entry_t& entry, song_id_t project,
              std::uint64_t document_revision, const resolved_visual_asset_t& source,
              const video_preview_clock_t& clock) -> bool {
    return entry.project == project && entry.document_revision == document_revision &&
           same_source(entry.source, source) &&
           entry.media_time_numerator == clock.media_time_numerator &&
           entry.media_time_denominator == clock.media_time_denominator;
}

} // namespace

video_frame_cache_t::video_frame_cache_t(std::size_t maximum_rgba_bytes,
                                         std::size_t maximum_entries)
    : maximum_rgba_bytes_(maximum_rgba_bytes), maximum_entries_(maximum_entries) {
    entries_.reserve(maximum_entries_);
}

auto video_frame_cache_t::find(song_id_t project, std::uint64_t document_revision,
                               const resolved_visual_asset_t& source,
                               const video_preview_clock_t& clock)
    -> std::shared_ptr<const rgba_video_frame_t> {
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) {
        return same_key(entry, project, document_revision, source, clock);
    });
    if (found == entries_.end()) {
        return {};
    }
    auto entry = std::move(*found);
    entries_.erase(found);
    entries_.push_back(std::move(entry));
    return entries_.back().frame;
}

auto video_frame_cache_t::insert(song_id_t project, std::uint64_t document_revision,
                                 const resolved_visual_asset_t& source,
                                 const video_preview_clock_t& clock,
                                 std::shared_ptr<const rgba_video_frame_t> frame)
    -> std::shared_ptr<const rgba_video_frame_t> {
    if (!frame) {
        return {};
    }
    const auto bytes = frame->pixels.size();
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) {
        return same_key(entry, project, document_revision, source, clock);
    });
    if (found != entries_.end()) {
        auto entry = std::move(*found);
        entries_.erase(found);
        entries_.push_back(std::move(entry));
        return entries_.back().frame;
    }
    if (maximum_entries_ == 0 || bytes > maximum_rgba_bytes_) {
        return frame;
    }
    while (!entries_.empty() &&
           (entries_.size() >= maximum_entries_ || rgba_bytes_ > maximum_rgba_bytes_ - bytes)) {
        rgba_bytes_ -= entries_.front().frame->pixels.size();
        entries_.erase(entries_.begin());
    }
    entries_.push_back({.project = project,
                        .document_revision = document_revision,
                        .source = source,
                        .media_time_numerator = clock.media_time_numerator,
                        .media_time_denominator = clock.media_time_denominator,
                        .frame = frame});
    rgba_bytes_ += bytes;
    return frame;
}

void video_frame_cache_t::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    rgba_bytes_ = 0;
}

auto video_frame_cache_t::size() const noexcept -> std::size_t {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

auto video_frame_cache_t::rgba_bytes() const noexcept -> std::size_t {
    std::lock_guard lock(mutex_);
    return rgba_bytes_;
}

video_frame_job_t::video_frame_job_t(video_frame_cache_t& cache, task_scheduler_t& scheduler,
                                     std::size_t maximum_in_flight_jobs)
    : cache_(cache), scheduler_(scheduler),
      maximum_in_flight_jobs_(std::min(maximum_in_flight_jobs, hard_maximum_video_jobs)) {
    workers_.reserve(maximum_in_flight_jobs_);
}

video_frame_job_t::~video_frame_job_t() {
    cancel();
    for (const auto& worker : workers_) {
        static_cast<void>(worker.graph.wait());
    }
}

void video_frame_job_t::discard_terminal_jobs() {
    std::erase_if(workers_,
                  [](const auto& worker) { return worker.graph.result(worker.task).has_value(); });
}

auto video_frame_job_t::request(song_id_t project, std::uint64_t document_revision,
                                resolved_visual_asset_t source, video_preview_clock_t clock,
                                video_frame_decode_limits_t limits) -> video_job_start_result_t {
    if (!project || document_revision == 0 || !source.resource || !source.asset ||
        source.kind != asset_kind_t::video || source.resource != clock.resource ||
        clock.media_time_denominator == 0 || maximum_in_flight_jobs_ == 0) {
        return {.status = video_job_start_status_t::invalid_request,
                .message = "video frame request has invalid identity or timing"};
    }
    discard_terminal_jobs();
    if (workers_.size() >= maximum_in_flight_jobs_) {
        return {.status = video_job_start_status_t::capacity_exceeded,
                .message = "video frame jobs have not reached cancellation checkpoints"};
    }

    auto generation = next_generation_ + 1U;
    if (generation == 0) {
        generation = 1;
    }
    task_graph_t graph{1};
    const auto task =
        graph.add_task({}, [this, project, document_revision, source = std::move(source), clock,
                            limits](std::span<const task_result_t>, std::stop_token stop) {
            video_job_result_t result{.project = project,
                                      .document_revision = document_revision,
                                      .source = source,
                                      .clock = clock,
                                      .status = video_frame_status_t::decode_failed,
                                      .frame = {},
                                      .message = {}};
            if (stop.stop_requested()) {
                return task_result_t::failure(task_error_code_t::cancelled,
                                              "video frame request was superseded");
            }
            if (auto cached = cache_.find(project, document_revision, source, clock)) {
                result.status = video_frame_status_t::ready;
                result.frame = std::move(cached);
            } else {
                auto decoded = decode_video_frame_rgba(source, clock, limits, stop);
                result.status = decoded.status;
                result.message = std::move(decoded.message);
                if (decoded) {
                    auto frame =
                        std::make_shared<const rgba_video_frame_t>(std::move(*decoded.frame));
                    result.frame =
                        cache_.insert(project, document_revision, source, clock, std::move(frame));
                }
            }
            if (stop.stop_requested()) {
                return task_result_t::failure(task_error_code_t::cancelled,
                                              "video frame request was superseded");
            }
            return task_result_t::success(
                make_task_value(std::make_shared<const video_job_result_t>(std::move(result))));
        });
    if (!task) {
        return {.status = video_job_start_status_t::scheduler_rejected,
                .message =
                    task.error ? task.error->message : "video frame task could not be created"};
    }
    auto submitted = scheduler_.submit(std::move(graph));
    if (!submitted) {
        return {.status = video_job_start_status_t::scheduler_rejected,
                .message = submitted.error ? submitted.error->message
                                           : "video frame task could not be submitted"};
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
    return {.status = video_job_start_status_t::started, .generation = generation, .message = {}};
}

auto video_frame_job_t::poll() -> std::optional<video_job_completion_t> {
    std::optional<video_job_completion_t> completion;
    for (auto worker = workers_.begin(); worker != workers_.end();) {
        auto result = worker->graph.result(worker->task);
        if (!result) {
            ++worker;
            continue;
        }
        if (worker->generation == current_generation_) {
            completion = {.generation = worker->generation,
                          .project = worker->project,
                          .document_revision = worker->document_revision,
                          .result = {},
                          .task_error = std::nullopt};
            if (auto frame = task_value_cast<video_job_result_t>(*result)) {
                completion->result = std::move(frame);
            } else {
                completion->task_error = result->error.value_or(task_error_t{
                    .code = task_error_code_t::task_failed,
                    .message = "video frame worker returned no typed result",
                });
            }
            current_generation_ = 0;
        }
        worker = workers_.erase(worker);
    }
    return completion;
}

auto video_frame_job_t::wait() -> std::optional<video_job_completion_t> {
    const auto current = std::ranges::find(workers_, current_generation_, &worker_t::generation);
    if (current == workers_.end() || !current->graph.wait()) {
        return std::nullopt;
    }
    return poll();
}

void video_frame_job_t::cancel() noexcept {
    current_generation_ = 0;
    for (const auto& worker : workers_) {
        worker.graph.cancel();
    }
}

auto video_frame_job_t::busy() const noexcept -> bool {
    return current_generation_ != 0;
}

} // namespace wormedit::visual
