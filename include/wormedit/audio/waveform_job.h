#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/core/task_graph.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::audio {

enum class waveform_job_start_status_t {
    started,
    invalid_request,
    capacity_exceeded,
    scheduler_rejected,
};

struct waveform_job_start_result_t {
    waveform_job_start_status_t status{waveform_job_start_status_t::invalid_request};
    std::uint64_t generation{};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == waveform_job_start_status_t::started;
    }
};

struct waveform_job_completion_t {
    std::uint64_t generation{};
    asset_id_t asset;
    std::shared_ptr<const waveform_analysis_result_t> analysis;
    std::optional<task_error_t> task_error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return analysis != nullptr && !task_error.has_value();
    }
};

// Owns a bounded set of scheduler handles while exposing only the newest
// request to the UI thread. Superseded tasks are cooperatively cancelled and
// their terminal payloads are discarded. Referenced registry/caches/scheduler
// must outlive this coordinator.
class waveform_job_t {
  public:
    waveform_job_t(decoded_source_registry_t& sources, decoded_block_cache_t& blocks,
                   waveform_cache_t& waveforms, task_scheduler_t& scheduler,
                   std::size_t maximum_in_flight_jobs = 4);
    ~waveform_job_t();

    waveform_job_t(const waveform_job_t&) = delete;
    auto operator=(const waveform_job_t&) -> waveform_job_t& = delete;

    [[nodiscard]] auto request(asset_id_t asset, std::uint32_t frames_per_bin,
                               waveform_analysis_options_t options = {})
        -> waveform_job_start_result_t;
    [[nodiscard]] auto poll() -> std::optional<waveform_job_completion_t>;
    // Intended for tests and orderly shutdown paths, not the UI frame loop.
    [[nodiscard]] auto wait() -> std::optional<waveform_job_completion_t>;
    void cancel() noexcept;
    [[nodiscard]] auto busy() const noexcept -> bool;

  private:
    struct worker_t {
        std::uint64_t generation{};
        asset_id_t asset;
        task_graph_handle_t graph;
        task_id_t task;
    };

    void discard_terminal_jobs();

    decoded_source_registry_t& sources_;
    decoded_block_cache_t& blocks_;
    waveform_cache_t& waveforms_;
    task_scheduler_t& scheduler_;
    std::size_t maximum_in_flight_jobs_{};
    std::uint64_t next_generation_{};
    std::uint64_t current_generation_{};
    std::vector<worker_t> workers_;
};

} // namespace wormedit::audio
