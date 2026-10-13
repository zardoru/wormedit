#pragma once

#include <wormedit/core/timing.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <optional>

namespace wormedit {

enum class video_preview_clock_status_t : std::uint8_t {
    inactive,
    ready,
    invalid_sample_rate,
    invalid_event,
    event_in_future,
};

// Exact, decoder-neutral position in the video activated by the current BGA
// layer. The reduced media-time fraction is suitable for conversion to a
// decoder's time base without first passing through floating point.
struct video_preview_clock_t {
    [[nodiscard]] static auto from(
        const std::optional<preview_visual_event_t>& active_video,
        std::uint64_t sample_frame, std::uint32_t sample_rate) noexcept
        -> result_t<video_preview_clock_t, video_preview_clock_status_t>;

    object_id_t event;
    resource_id_t resource;
    std::uint64_t activation_sample_frame{};
    std::uint64_t elapsed_sample_frames{};
    std::uint64_t media_time_numerator{};
    std::uint32_t media_time_denominator{1};

    auto operator==(const video_preview_clock_t&) const -> bool = default;
};

using video_preview_clock_result_t =
    result_t<video_preview_clock_t, video_preview_clock_status_t>;

// Evaluates a video activation against the shared transport clock. A new BGA
// video event always restarts media time at zero; seek and offline rendering
// therefore produce the same timestamp without retaining decoder state.
[[nodiscard]] auto evaluate_video_preview_clock(
    const std::optional<preview_visual_event_t>& active_video, std::uint64_t sample_frame,
    std::uint32_t sample_rate) noexcept -> video_preview_clock_result_t;

} // namespace wormedit
