#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/core/async_job.h>

#include <cstddef>
#include <cstdint>

namespace wormedit::audio {

using waveform_job_completion_outcome_t =
    async_job_completion_outcome_t<asset_id_t, waveform_analysis_result_t>;

struct waveform_request_t {
    decoded_source_registry_t& sources;
    decoded_block_cache_t& blocks;
    waveform_cache_t& waveforms;
    asset_id_t asset;
    std::uint32_t frames_per_bin{};
    waveform_analysis_options_t options;
};

// Owns a bounded set of scheduler handles while exposing only the newest
// request to the UI thread. Superseded tasks are cooperatively cancelled and
// their terminal payloads are discarded. Referenced registry/caches/scheduler
// must outlive this coordinator.
class waveform_job_t
    : public async_job_t<waveform_request_t, waveform_analysis_result_t, asset_id_t> {
  public:
    waveform_job_t(decoded_source_registry_t& sources, decoded_block_cache_t& blocks,
                   waveform_cache_t& waveforms, task_scheduler_t& scheduler,
                   std::size_t maximum_in_flight_jobs = 4);

    waveform_job_t(const waveform_job_t&) = delete;
    auto operator=(const waveform_job_t&) -> waveform_job_t& = delete;

    [[nodiscard]] auto request(asset_id_t asset, std::uint32_t frames_per_bin,
                               waveform_analysis_options_t options = {})
        -> async_job_start_outcome_t;

  private:
    static constexpr const char* invalid_request_message_ =
        "waveform analysis request is incomplete";

    [[nodiscard]] static auto analyze(const waveform_request_t& request, std::stop_token stop)
        -> work_outcome_t;

    decoded_source_registry_t& sources_;
    decoded_block_cache_t& blocks_;
    waveform_cache_t& waveforms_;
};

} // namespace wormedit::audio
