#include <wormedit/visual/video_job.h>

#include <algorithm>
#include <ranges>
#include <utility>

namespace wormedit::visual {
namespace {

constexpr std::size_t hard_maximum_video_jobs = 4;

auto video_source_equal(const resolved_visual_asset_t& left, const resolved_visual_asset_t& right)
    -> bool {
    return left.resource == right.resource && left.asset == right.asset &&
           left.kind == right.kind && left.path == right.path &&
           left.declared_width == right.declared_width &&
           left.declared_height == right.declared_height;
}

auto is_video_key_current(const video_frame_cache_entry_t& entry, song_id_t project,
              std::uint64_t document_revision, const resolved_visual_asset_t& source,
              const video_preview_clock_t& clock) -> bool {
    return entry.project == project && entry.document_revision == document_revision &&
           video_source_equal(entry.source, source) &&
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
        return is_video_key_current(entry, project, document_revision, source, clock);
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
        return is_video_key_current(entry, project, document_revision, source, clock);
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
                  [](const auto& worker) { return worker.graph.outcome(worker.task).has_value(); });
}

auto video_frame_job_t::decode_uncached_frame(const decode_request_t& request,
                                               std::stop_token stop)
    -> video_frame_decode_result_t {
    std::unique_lock session_lock(session_mutex_);
    if (!session_ || session_project_ != request.project ||
        session_document_revision_ != request.document_revision ||
        !video_source_equal(session_->source(), request.source)) {
        session_.reset();
        session_project_ = {};
        session_document_revision_ = 0;
        auto opened = open_video_decoder_session(request.source, stop);
        if (!opened) {
            return std::unexpected(std::move(opened.error()));
        }
        session_ = std::move(*opened);
        session_project_ = request.project;
        session_document_revision_ = request.document_revision;
    }
    auto decoded = session_->decode(request.clock, request.limits, stop);
    if (!decoded) {
        // Interrupted demux/codec calls can leave a session unusable.
        session_.reset();
        session_project_ = {};
        session_document_revision_ = 0;
    }
    return decoded;
}

auto video_frame_job_t::run_decode(const decode_request_t& request, std::stop_token stop)
    -> task_result_t {
    video_job_context_t context{.identity = {request.project, request.document_revision},
                                .source = request.source, .clock = request.clock};
    auto status = video_frame_status_t::decode_failed;
    std::shared_ptr<const rgba_video_frame_t> result_frame;
    std::string message;
    if (stop.stop_requested()) {
        return task_result_t::failure(task_error_code_t::cancelled,
                                      "video frame request was superseded");
    }
    if (auto cached = cache_.find(request.project, request.document_revision, request.source,
                                  request.clock)) {
        status = video_frame_status_t::ready;
        result_frame = std::move(cached);
    } else {
        auto decoded = decode_uncached_frame(request, stop);
        if (decoded) {
            status = video_frame_status_t::ready;
            auto frame = std::make_shared<const rgba_video_frame_t>(std::move(*decoded));
            result_frame = cache_.insert(request.project, request.document_revision,
                                         request.source, request.clock, std::move(frame));
        } else {
            status = decoded.error().code;
            message = std::move(decoded.error().message);
        }
    }
    if (stop.stop_requested()) {
        return task_result_t::failure(task_error_code_t::cancelled,
                                      "video frame request was superseded");
    }
    return task_result_t::success(
        task_value_t::from(std::make_shared<const video_job_outcome_t>(
            video_job_frame_t::from(std::move(context), status, std::move(result_frame),
                                    std::move(message)))));
}


auto video_frame_job_t::begin(song_id_t project, std::uint64_t document_revision,
                                resolved_visual_asset_t source, video_preview_clock_t clock,
                                video_frame_decode_limits_t limits) -> async_job_start_outcome_t {
    if (!project || document_revision == 0 || !source.resource || !source.asset ||
        source.kind != asset_kind_t::video || source.resource != clock.resource ||
        clock.media_time_denominator == 0 || maximum_in_flight_jobs_ == 0) {
        return std::unexpected(error_t<async_job_start_status_t>{video_job_start_status_t::invalid_request, "video frame request has invalid identity or timing"});
    }
    discard_terminal_jobs();
    if (workers_.size() >= maximum_in_flight_jobs_) {
        return std::unexpected(error_t<async_job_start_status_t>{video_job_start_status_t::capacity_exceeded, "video frame jobs have not reached cancellation checkpoints"});
    }

    auto generation = next_generation_ + 1U;
    if (generation == 0) {
        generation = 1;
    }
    task_graph_t graph{1};
    const auto task = graph.add_task(
        {}, [this, decode_request = decode_request_t{project, document_revision, std::move(source),
                                                      clock, limits}](std::span<const task_result_t>,
                                                                      std::stop_token stop) {
            return run_decode(decode_request, stop);
        });
    if (!task) {
        return std::unexpected(error_t<async_job_start_status_t>{video_job_start_status_t::scheduler_rejected, task.error ? task.error->message : "video frame task could not be created"});
    }
    auto submitted = scheduler_.submit(std::move(graph));
    if (!submitted) {
        const auto status = submitted.error &&
                                    submitted.error->code == task_error_code_t::capacity_exceeded
                                ? async_job_start_status_t::capacity_exceeded
                                : async_job_start_status_t::scheduler_rejected;
        return std::unexpected(error_t<async_job_start_status_t>{status, submitted.error ? submitted.error->message
                                           : "video frame task could not be submitted"});
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

auto video_frame_job_t::poll_completion() -> std::optional<video_job_completion_outcome_t> {
    std::optional<video_job_completion_outcome_t> completion;
    for (auto worker = workers_.begin(); worker != workers_.end();) {
        auto result = worker->graph.outcome(worker->task);
        if (!result) {
            ++worker;
            continue;
        }
        if (worker->generation == current_generation_) {
            completion = async_job_completion_value_t<async_job_project_identity_t, video_job_outcome_t>::from(
                worker->generation, async_job_project_identity_t{worker->project, worker->document_revision}, std::move(*result),
                "video frame worker returned no typed result");
            current_generation_ = 0;
        }
        worker = workers_.erase(worker);
    }
    return completion;
}

auto video_frame_job_t::wait_completion() -> std::optional<video_job_completion_outcome_t> {
    const auto current = std::ranges::find(workers_, current_generation_, &worker_t::generation);
    if (current == workers_.end() || !current->graph.wait()) {
        return std::nullopt;
    }
    return poll_completion();
}

void video_frame_job_t::cancel() noexcept {
    current_generation_ = 0;
    for (const auto& worker : workers_) {
        worker.graph.cancel();
    }
}

auto video_frame_job_t::is_busy() const noexcept -> bool {
    return current_generation_ != 0;
}

} // namespace wormedit::visual
