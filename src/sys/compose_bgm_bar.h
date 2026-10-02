#pragma once

#include "ui/frontend.h"

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit::sys {

// The BGM bar shows the song audio between two fractions of its length. Zoom and pan move
// that window; the whole song is the default. This state is a per-session view setting.
struct bgm_bar_range_t {
    double begin{0.0};
    double end{1.0};
    auto operator==(const bgm_bar_range_t&) const -> bool = default;
};

// Narrowest window, as a fraction of the song: about 5.5 seconds at 48 kHz, which still has a
// source bin behind every projected row.
inline constexpr std::uint64_t bgm_bar_minimum_span_frames = 262'144;
inline constexpr double bgm_bar_zoom_step = 1.25;

[[nodiscard]] auto bgm_bar_minimum_span(std::uint64_t total_frames) -> double;

// Fits an arbitrary window into [0, 1], keeping its span within [minimum_span, 1].
[[nodiscard]] auto bgm_bar_fit(bgm_bar_range_t range, double minimum_span) -> bgm_bar_range_t;

// Positive `steps` zoom in. The audio fraction under `anchor` stays under the pointer.
[[nodiscard]] auto bgm_bar_zoom(bgm_bar_range_t range, double anchor, double steps,
                                double minimum_span) -> bgm_bar_range_t;

// Moves the window by a signed fraction of the song without changing its span.
[[nodiscard]] auto bgm_bar_pan(bgm_bar_range_t range, double delta_fraction) -> bgm_bar_range_t;

struct bgm_bar_mark_t {
    double audio_fraction{};
    std::uint32_t measure{};
};

// Measure starts inside the window, in order. A window holding more than `capacity` measures
// keeps every n-th so the result stays bounded; the frontend thins labels by pixel spacing.
[[nodiscard]] auto bgm_bar_measure_marks(const wormedit::measure_index_t& measures,
                                         const wormedit::timing_index_t& timing,
                                         std::uint32_t sample_rate, std::uint64_t total_frames,
                                         bgm_bar_range_t range, std::size_t capacity)
    -> std::vector<bgm_bar_mark_t>;

// Applies the BGM bar zoom, pan and reset inputs of one frame to `range` and removes them from
// the action's compose inputs. Returns true when any was consumed.
auto consume_bgm_bar_inputs(WormUiWorkspaceAction& action, bgm_bar_range_t& range,
                            std::uint64_t total_frames) -> bool;

} // namespace wormedit::sys
