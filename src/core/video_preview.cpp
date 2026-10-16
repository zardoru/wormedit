#include <wormedit/core/video_preview.h>

#include <numeric>

namespace wormedit {

auto video_preview_clock_t::from(const std::optional<preview_visual_event_t>& active_video,
                                  std::uint64_t sample_frame,
                                  std::uint32_t sample_rate) noexcept
    -> video_preview_clock_result_t {
    if (sample_rate == 0) {
        return std::unexpected(failure_code_t::invalid_request);
    }
    if (!active_video) {
        return std::unexpected(video_preview_clock_phase_t::inactive);
    }
    if (!active_video->object || !active_video->resource ||
        active_video->kind != visual_event_kind_t::video) {
        return std::unexpected(failure_code_t::invalid_data);
    }
    if (active_video->sample_frame > sample_frame) {
        return std::unexpected(failure_code_t::not_ready);
    }

    const auto elapsed = sample_frame - active_video->sample_frame;
    const auto divisor = std::gcd(elapsed, static_cast<std::uint64_t>(sample_rate));
    return video_preview_clock_t{
        .event = active_video->object,
        .resource = active_video->resource,
        .activation_sample_frame = active_video->sample_frame,
        .elapsed_sample_frames = elapsed,
        .media_time_numerator = elapsed / divisor,
        .media_time_denominator = static_cast<std::uint32_t>(sample_rate / divisor),
    };
}

} // namespace wormedit
