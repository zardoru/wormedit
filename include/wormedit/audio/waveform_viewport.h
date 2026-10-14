#pragma once

#include <wormedit/audio/waveform.h>

#include <cstdint>

namespace wormedit::audio {

// Visible window of one source's waveform, in source frames. Shared by every
// view that scrolls and zooms a waveform (Sounds tab now, Compose stem cuts
// later). All operations are pure and keep the window inside the source.
struct waveform_viewport_t {
    std::uint64_t total_frames{};
    std::uint64_t begin_frame{};
    // Source frames covered by one pixel; may be below one when zoomed past
    // the sample level.
    double frames_per_pixel{1.0};
    std::uint32_t pixel_width{};

    auto operator==(const waveform_viewport_t&) const -> bool = default;
};

inline constexpr double minimum_waveform_frames_per_pixel = 1.0 / 16.0;

[[nodiscard]] auto is_waveform_viewport_valid(const waveform_viewport_t& viewport) noexcept -> bool;

// Exclusive last visible frame, clamped to total_frames.
[[nodiscard]] auto waveform_viewport_end_frame(const waveform_viewport_t& viewport) noexcept
    -> std::uint64_t;

// Whole source across pixel_width. Returns an invalid (zeroed) viewport when
// either argument is zero.
[[nodiscard]] auto fit_waveform_viewport(std::uint64_t total_frames,
                                         std::uint32_t pixel_width) noexcept
    -> waveform_viewport_t;

// Restores the invariants (zoom range, window inside the source).
[[nodiscard]] auto clamp_waveform_viewport(waveform_viewport_t viewport) noexcept
    -> waveform_viewport_t;

// factor > 1 zooms in, < 1 zooms out. The frame under anchor_pixel stays under
// it unless clamping prevents that. Invalid input returns the viewport as is.
[[nodiscard]] auto zoom_waveform_viewport(const waveform_viewport_t& viewport,
                                          double anchor_pixel, double factor) noexcept
    -> waveform_viewport_t;

// Positive pixels move the window toward the end of the source.
[[nodiscard]] auto pan_waveform_viewport(const waveform_viewport_t& viewport,
                                         double delta_pixels) noexcept -> waveform_viewport_t;

// Projects the visible window through the closest pyramid level. An invalid
// viewport or waveform yields a non-projected result.
[[nodiscard]] auto build_waveform_viewport_view(const waveform_t& waveform,
                                             const waveform_viewport_t& viewport)
    -> waveform_projection_result_t;

} // namespace wormedit::audio
