#pragma once

#include <wormedit/core/operation_state.h>
#include <wormedit/core/result.h>
#include <wormedit/core/timing.h>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace wormedit {

enum class video_preview_clock_phase_t : std::uint8_t { inactive };
static_assert(sizeof(operation_state_t<video_preview_clock_phase_t>) <= 2 * sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<operation_state_t<video_preview_clock_phase_t>>);

// Exact, decoder-neutral position in the video activated by the current BGA
// layer. The reduced media-time fraction is suitable for conversion to a
// decoder's time base without first passing through floating point.
//
// Evaluates a video activation against the shared transport clock. A new BGA
// video event always restarts media time at zero; seek and offline rendering
// therefore produce the same timestamp without retaining decoder state.
struct video_preview_clock_t {
    [[nodiscard]] static auto from(
        const std::optional<preview_visual_event_t>& active_video,
        std::uint64_t sample_frame, std::uint32_t sample_rate) noexcept
        -> result_t<video_preview_clock_t, operation_state_t<video_preview_clock_phase_t>>;

    object_id_t event;
    resource_id_t resource;
    std::uint64_t activation_sample_frame{};
    std::uint64_t elapsed_sample_frames{};
    std::uint64_t media_time_numerator{};
    std::uint32_t media_time_denominator{1};

    auto operator==(const video_preview_clock_t&) const -> bool = default;
};

static_assert(sizeof(result_t<video_preview_clock_t, operation_state_t<video_preview_clock_phase_t>>) <=
              sizeof(video_preview_clock_t) + alignof(video_preview_clock_t));


} // namespace wormedit
