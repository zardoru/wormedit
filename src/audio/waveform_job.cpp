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
                  [](const auto& worker) { return worker.graph.outcome(worker.task).has_value(); });
}

auto waveform_job_t::begin(asset_id_t asset, std::uint32_t frames_per_bin,
                             waveform_analysis_options_t options) -> async_job_start_outcome_t {
    if (!asset || frames_per_bin == 0 || maximum_in_flight_jobs_ == 0) {
        return std::unexpected(error_t<async_job_start_status_t>{waveform_job_start_status_t::invalid_request, "waveform analysis request is incomplete"});
    }
    discard_terminal_jobs();
    if (workers_.size() >= maximum_in_flight_jobs_) {
        return std::unexpected(error_t<async_job_start_status_t>{waveform_job_start_status_t::capacity_exceeded, "waveform analysis jobs have not reached cancellation checkpoints"});
    }

    auto generation = next_generation_ + 1U;
    if (generation == 0)
        generation = 1;
    task_graph_t graph{1};
    const auto task = graph.create_task({}, [this, asset, frames_per_bin, options](
                                             std::span<const task_result_t>, std::stop_token stop) {
        auto analysis =
            sources_.analyze_waveform(asset, blocks_, waveforms_, frames_per_bin, options, stop);
        auto payload = std::make_shared<const waveform_analysis_result_t>(
            waveform_analysis_result_t::from(std::move(analysis)));
        return task_result_t::success(task_value_t::from(std::move(payload)));
    });
    if (!task) {
        return std::unexpected(error_t<async_job_start_status_t>{waveform_job_start_status_t::scheduler_rejected, task.error().message});
    }
    auto submitted = scheduler_.begin(std::move(graph));
    if (!submitted) {
        const auto status = submitted.error().code == task_error_code_t::capacity_exceeded
                                ? async_job_start_status_t::capacity_exceeded
                                : async_job_start_status_t::scheduler_rejected;
        return std::unexpected(error_t<async_job_start_status_t>{status, submitted.error().message});
    }

    for (const auto& worker : workers_)
        worker.graph.cancel();
    next_generation_ = generation;
    current_generation_ = generation;
    workers_.push_back({.generation = generation,
                        .asset = asset,
                        .graph = std::move(submitted.value()),
                        .task = *task});
    return generation;
}

auto waveform_job_t::poll_completion() -> std::optional<waveform_job_completion_outcome_t> {
    std::optional<waveform_job_completion_outcome_t> completion;
    for (auto worker = workers_.begin(); worker != workers_.end();) {
        auto result = worker->graph.outcome(worker->task);
        if (!result) {
            ++worker;
            continue;
        }
        if (worker->generation == current_generation_) {
            completion = async_job_completion_value_t<asset_id_t, waveform_analysis_result_t>::from(
                worker->generation, worker->asset, std::move(*result),
                "waveform analysis worker returned no typed result");
            current_generation_ = 0;
        }
        worker = workers_.erase(worker);
    }
    return completion;
}

auto waveform_job_t::wait_completion() -> std::optional<waveform_job_completion_outcome_t> {
    const auto current = std::ranges::find(workers_, current_generation_, &worker_t::generation);
    if (current == workers_.end() || !current->graph.wait()) {
        return std::nullopt;
    }
    return poll_completion();
}

void waveform_job_t::cancel() noexcept {
    current_generation_ = 0;
    for (const auto& worker : workers_)
        worker.graph.cancel();
}

auto waveform_job_t::is_busy() const noexcept -> bool {
    return current_generation_ != 0;
}

} // namespace wormedit::audio
