#include <wormedit/audio/waveform_job.h>

#include <algorithm>

namespace wormedit::audio {
namespace {

constexpr std::size_t maximum_waveform_jobs = 8;

} // namespace

waveform_job_t::waveform_job_t(decoded_source_registry_t& sources, decoded_block_cache_t& blocks,
                               waveform_cache_t& waveforms, task_scheduler_t& scheduler,
                               std::size_t maximum_in_flight_jobs)
    : async_job_t(scheduler, std::min(maximum_in_flight_jobs, maximum_waveform_jobs),
                  invalid_request_message_,
                  "waveform analysis jobs have not reached cancellation checkpoints",
                  "waveform analysis worker returned no typed result"),
      sources_(sources), blocks_(blocks), waveforms_(waveforms) {}

auto waveform_job_t::analyze(const waveform_request_t& request, std::stop_token stop)
    -> work_outcome_t {
    return waveform_analysis_result_t::from(
        request.sources.analyze_waveform(request.asset, request.blocks, request.waveforms,
                                         request.frames_per_bin, request.options, stop));
}

auto waveform_job_t::request(asset_id_t asset, std::uint32_t frames_per_bin,
                             waveform_analysis_options_t options) -> async_job_start_outcome_t {
    if (!asset || frames_per_bin == 0) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request, invalid_request_message_});
    }
    return async_job_t::request(
        [this, asset, frames_per_bin, options] {
            return waveform_request_t{.sources = sources_,
                                      .blocks = blocks_,
                                      .waveforms = waveforms_,
                                      .asset = asset,
                                      .frames_per_bin = frames_per_bin,
                                      .options = options};
        },
        asset, analyze);
}

} // namespace wormedit::audio
