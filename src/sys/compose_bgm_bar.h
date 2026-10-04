#pragma once

#include "ui/frontend.h"

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit::sys {

// The BGM bar shows the song audio between two fractions of its length. Zoom and pan move
// that window; the whole song is the default. This state is a per-session view setting.
struct bgm_bar_range_t {
    double begin{0.0};
    double end{1.0};
    auto operator==(const bgm_bar_range_t&) const -> bool = default;
};

// Explicit pan temporarily takes precedence over centering. It remains valid only while the
// field's visible span and its chart/audio context stay unchanged.
struct bgm_bar_pan_override_t {
    bgm_bar_range_t range;
    double visible_begin_fraction{};
    double visible_end_fraction{};
    song_id_t project;
    chart_id_t chart;
    std::uint64_t document_revision{};
    asset_id_t asset;
};

// Snapshot of the displayed BGM window for one active overview drag.
struct bgm_bar_window_capture_t {
    bgm_bar_range_t range;
    song_id_t project;
    chart_id_t chart;
    std::uint64_t document_revision{};
    asset_id_t asset;
    std::uint64_t total_frames{};
    std::uint32_t sample_rate{};
};

// Narrowest window, as a fraction of the song: about 5.5 seconds at 48 kHz, which still has a
// source bin behind every projected row.
inline constexpr std::uint64_t bgm_bar_minimum_span_frames = 262'144;
inline constexpr double bgm_bar_zoom_step = 1.25;

[[nodiscard]] auto bgm_bar_minimum_span(std::uint64_t total_frames) -> double;

// Fits an arbitrary window into [0, 1], keeping its span within [minimum_span, 1].
[[nodiscard]] auto bgm_bar_fit(bgm_bar_range_t range, double minimum_span) -> bgm_bar_range_t;

// Keeps the current span centered on a song fraction, shifting only when a song edge clamps it.
[[nodiscard]] auto bgm_bar_center_on(bgm_bar_range_t range, double center,
                                    double minimum_span) -> bgm_bar_range_t;
[[nodiscard]] auto bgm_bar_center_on_visible_span(bgm_bar_range_t range, double visible_begin,
                                                  double visible_end, double minimum_span)
    -> bgm_bar_range_t;
[[nodiscard]] auto bgm_bar_pan_override_matches(const bgm_bar_pan_override_t& override,
    double visible_begin, double visible_end, song_id_t project, chart_id_t chart,
    std::uint64_t revision, asset_id_t asset) -> bool;
[[nodiscard]] auto bgm_bar_window_capture_matches(const bgm_bar_window_capture_t& capture,
    song_id_t project, chart_id_t chart, std::uint64_t revision, asset_id_t asset,
    std::uint64_t total_frames, std::uint32_t sample_rate) -> bool;

// Positive `steps` zoom in. The audio fraction under `anchor` stays under the pointer.
[[nodiscard]] auto bgm_bar_zoom(bgm_bar_range_t range, double anchor, double steps,
                                double minimum_span) -> bgm_bar_range_t;

// Zooms to a new span centered on the current field's visible audio midpoint.
[[nodiscard]] auto bgm_bar_zoom_centered(bgm_bar_range_t range, double center, double steps,
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
struct bgm_bar_input_context_t {
    std::uint64_t total_frames{};
    double visible_begin_fraction{};
    double visible_end_fraction{};
    song_id_t project;
    chart_id_t chart;
    std::uint64_t document_revision{};
    asset_id_t asset;
};

auto consume_bgm_bar_inputs(WormUiWorkspaceAction& action, bgm_bar_range_t& range,
                            std::optional<bgm_bar_pan_override_t>& pan_override,
                            const bgm_bar_input_context_t& context) -> bool;

} // namespace wormedit::sys
