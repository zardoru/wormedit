#pragma once

#include <wormedit/core/grid.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/interaction_cursor_pose.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace wormedit::sys {

// While playing, the playhead stays anchored at the edge margin measured from the
// chart-start side (#000); when paused it only scrolls once it leaves the comfort band.
[[nodiscard]] inline auto compose_playhead_scroll_target(
    const editor_interaction_state_t& state, const chart_document_t& chart,
    long double absolute_row, double previous_scroll, bool anchored,
    double following_anchor_fraction = editor_cursor_edge_margin_fraction) -> std::optional<double> {
    if (!std::isfinite(absolute_row) || !std::isfinite(previous_scroll) ||
        !std::isfinite(state.viewport_height_pixels) ||
        !std::isfinite(state.pixels_per_standard_measure) ||
        state.viewport_height_pixels <= 0.0 || chart.divisions_per_standard_measure == 0)
        return std::nullopt;
    const auto pixels_per_row = static_cast<long double>(state.pixels_per_standard_measure) /
                                chart.divisions_per_standard_measure;
    const auto playhead_y = absolute_row * pixels_per_row;
    const auto visible_y = playhead_y - previous_scroll;
    const auto comfort_top = state.viewport_height_pixels * editor_cursor_edge_margin_fraction;
    const auto comfort_bottom = state.viewport_height_pixels - comfort_top;
    if (!std::isfinite(following_anchor_fraction) || following_anchor_fraction < 0.0 ||
        following_anchor_fraction > 1.0) return std::nullopt;
    const auto following_anchor = state.viewport_height_pixels * following_anchor_fraction;
    const auto fixed_anchor = state.fixed_cursor_screen_y_pixels
        ? (state.scroll_direction == editor_field_scroll_direction_t::down
            ? state.viewport_height_pixels - *state.fixed_cursor_screen_y_pixels
            : *state.fixed_cursor_screen_y_pixels) : following_anchor;
    const auto target = state.fixed_cursor_screen_y_pixels ? playhead_y - fixed_anchor
                      : anchored                     ? playhead_y - following_anchor
                      : visible_y < comfort_top    ? playhead_y - comfort_top
                      : visible_y > comfort_bottom ? playhead_y - comfort_bottom
                                                   : static_cast<long double>(previous_scroll);
    if (!std::isfinite(target) ||
        target < -static_cast<long double>(std::numeric_limits<double>::max()) ||
        target > static_cast<long double>(std::numeric_limits<double>::max()))
        return std::nullopt;
    return static_cast<double>(target);
}

// Keep Compose's snapped edit cursor on the currently audible chart row.
[[nodiscard]] inline auto follow_compose_playhead(document_history_t& history,
                                                   editor_interaction_state_t& state,
                                                   double absolute_row, bool anchored,
                                                   editor_measure_geometry_t geometry = {},
                                                   double following_anchor_fraction = editor_cursor_edge_margin_fraction) -> bool {
    if (!std::isfinite(absolute_row) || absolute_row < 0.0 ||
        state.is_pending())
        return false;
    const auto chart = history.document.find_chart(state.chart);
    if (chart == nullptr || !snap_step(*chart, active_snap_grid(state), 0))
        return false;
    if (geometry.index && (geometry.chart != chart->id ||
                           geometry.document_revision != history.document.revision)) return false;
    if (!geometry.index) {
        const auto& measures = history.indices(chart->id)->measures;
        if (!measures) return false;
        geometry = {chart->id, history.document.revision, &*measures};
    }
    const auto* index = geometry.index;
    if (!index || index->total_rows == 0) return false;
    const auto previous_scroll = state.viewport_scroll_pixels;
    const auto bounded_row = std::min(static_cast<long double>(absolute_row),
                                      static_cast<long double>(index->total_rows - 1));
    auto snapped = snap_canvas_row(*chart, *index, bounded_row,
                                   active_snap_grid(state), index->total_rows);
    if (!snapped) return false;
    if (*snapped > bounded_row)
        snapped = adjacent_snap_canvas_row(*chart, *index, bounded_row,
            active_snap_grid(state), index->total_rows, false);
    if (!snapped) return false;
    const auto row = *snapped;
    const auto after = std::ranges::upper_bound(index->entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index->entries.begin()) return false;
    const auto measure = std::prev(after);
    const local_position_t position{measure->id, row - measure->absolute_begin_row};
    const auto target_scroll = compose_playhead_scroll_target(
        state, *chart, bounded_row, previous_scroll, anchored, following_anchor_fraction);
    if (!target_scroll) return false;
    return static_cast<bool>(follow_editor_playhead(history, state, geometry, position,
                                                   *target_scroll, anchored ? following_anchor_fraction
                                                       : state.viewport_minimum_margin_fraction,
                                                   static_cast<double>(bounded_row - row)));
}

} // namespace wormedit::sys
