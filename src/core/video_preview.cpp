#include <wormedit/core/video_preview.h>

#include <numeric>

namespace wormedit {

auto evaluate_video_preview_clock(const std::optional<preview_visual_event_t>& active_video,
                                  std::uint64_t sample_frame,
                                  std::uint32_t sample_rate) noexcept
    -> video_preview_clock_result_t {
    if (sample_rate == 0) {
        return {.status = video_preview_clock_status_t::invalid_sample_rate, .clock = std::nullopt};
    }
    if (!active_video) {
        return {.status = video_preview_clock_status_t::inactive, .clock = std::nullopt};
    }
    if (!active_video->object || !active_video->resource ||
        active_video->kind != visual_event_kind_t::video) {
        return {.status = video_preview_clock_status_t::invalid_event, .clock = std::nullopt};
    }
    if (active_video->sample_frame > sample_frame) {
        return {.status = video_preview_clock_status_t::event_in_future, .clock = std::nullopt};
    }

    const auto elapsed = sample_frame - active_video->sample_frame;
    const auto divisor = std::gcd(elapsed, static_cast<std::uint64_t>(sample_rate));
    return {
        .status = video_preview_clock_status_t::ready,
        .clock = video_preview_clock_t{
            .event = active_video->object,
            .resource = active_video->resource,
            .activation_sample_frame = active_video->sample_frame,
            .elapsed_sample_frames = elapsed,
            .media_time_numerator = elapsed / divisor,
            .media_time_denominator = static_cast<std::uint32_t>(sample_rate / divisor),
        },
    };
}

} // namespace wormedit
