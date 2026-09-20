#include <wormedit/audio/waveform_job.h>

#include <algorithm>
#include <memory>
#include <ranges>
#include <span>
#include <utility>

namespace wormedit::audio {
namespace {

constexpr std::size_t maximum_waveform_jobs = 8;

} // namespace

waveform_job_t::waveform_job_t(decoded_source_registry_t& sources, decoded_block_cache_t& blocks,
                               waveform_cache_t& waveforms, task_scheduler_t& scheduler,
                               std::size_t maximum_in_flight_jobs)
    : sources_(sources), blocks_(blocks), waveforms_(waveforms), scheduler_(scheduler),
      maximum_in_flight_jobs_(std::min(maximum_in_flight_jobs, maximum_waveform_jobs)) {
    workers_.reserve(maximum_in_flight_jobs_);
}

waveform_job_t::~waveform_job_t() {
    cancel();
    for (const auto& worker : workers_) {
        static_cast<void>(worker.graph.wait());
    }
}

void waveform_job_t::discard_terminal_jobs() {
    std::erase_if(workers_,
                  [](const auto& worker) { return worker.graph.result(worker.task).has_value(); });
}

auto waveform_job_t::request(asset_id_t asset, std::uint32_t frames_per_bin,
                             waveform_analysis_options_t options) -> waveform_job_start_result_t {
    if (!asset || frames_per_bin == 0 || maximum_in_flight_jobs_ == 0) {
        return {.status = waveform_job_start_status_t::invalid_request,
                .message = "waveform analysis request is incomplete"};
    }
    discard_terminal_jobs();
    if (workers_.size() >= maximum_in_flight_jobs_) {
        return {.status = waveform_job_start_status_t::capacity_exceeded,
                .message = "waveform analysis jobs have not reached cancellation checkpoints"};
    }

    auto generation = next_generation_ + 1U;
    if (generation == 0)
        generation = 1;
    task_graph_t graph{1};
    const auto task = graph.add_task({}, [this, asset, frames_per_bin, options](
                                             std::span<const task_result_t>, std::stop_token stop) {
        auto analysis =
            sources_.analyze_waveform(asset, blocks_, waveforms_, frames_per_bin, options, stop);
        auto payload = std::make_shared<const waveform_analysis_result_t>(std::move(analysis));
        return task_result_t::success(make_task_value(std::move(payload)));
    });
    if (!task) {
        return {.status = waveform_job_start_status_t::scheduler_rejected,
                .message = task.error ? task.error->message
                                      : "waveform analysis task could not be created"};
    }
    auto submitted = scheduler_.submit(std::move(graph));
    if (!submitted) {
        return {.status = waveform_job_start_status_t::scheduler_rejected,
                .message = submitted.error ? submitted.error->message
                                           : "waveform analysis task could not be submitted"};
    }

    for (const auto& worker : workers_)
        worker.graph.cancel();
    next_generation_ = generation;
    current_generation_ = generation;
    workers_.push_back({.generation = generation,
                        .asset = asset,
                        .graph = std::move(submitted.graph),
                        .task = *task.id});
    return {.status = waveform_job_start_status_t::started, .generation = generation};
}

auto waveform_job_t::poll() -> std::optional<waveform_job_completion_t> {
    std::optional<waveform_job_completion_t> completion;
    for (auto worker = workers_.begin(); worker != workers_.end();) {
        auto result = worker->graph.result(worker->task);
        if (!result.has_value()) {
            ++worker;
            continue;
        }
        if (worker->generation == current_generation_) {
            completion = {.generation = worker->generation, .asset = worker->asset};
            if (auto analysis = task_value_cast<waveform_analysis_result_t>(*result)) {
                completion->analysis = std::move(analysis);
            } else {
                completion->task_error = result->error.value_or(task_error_t{
                    .code = task_error_code_t::task_failed,
                    .message = "waveform analysis worker returned no typed result",
                });
            }
            current_generation_ = 0;
        }
        worker = workers_.erase(worker);
    }
    return completion;
}

auto waveform_job_t::wait() -> std::optional<waveform_job_completion_t> {
    const auto current = std::ranges::find(workers_, current_generation_, &worker_t::generation);
    if (current == workers_.end() || !current->graph.wait()) {
        return std::nullopt;
    }
    return poll();
}

void waveform_job_t::cancel() noexcept {
    current_generation_ = 0;
    for (const auto& worker : workers_)
        worker.graph.cancel();
}

auto waveform_job_t::busy() const noexcept -> bool {
    return current_generation_ != 0;
}

} // namespace wormedit::audio
