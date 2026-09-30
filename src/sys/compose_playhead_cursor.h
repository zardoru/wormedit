#pragma once

#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>

namespace wormedit::sys {

[[nodiscard]] inline auto playhead_cursor_edit_blocked(const editor_interaction_state_t& state)
    -> bool {
    return state.pending_hold || !state.additional_keyboard_holds.empty() ||
           state.pending_virtual_lane_key ||
           state.pending_note_duration_gesture || state.pending_note_move_gesture ||
           state.pending_note_paint_gesture || state.pending_semantic_layer_paint_gesture ||
           state.pending_editor_area_selection || state.pending_space_range_selection;
}

// While playing, the playhead stays anchored at the edge margin measured from the
// chart-start side (#000); when paused it only scrolls once it leaves the comfort band.
[[nodiscard]] inline auto compose_playhead_scroll_target(
    const editor_interaction_state_t& state, const chart_document_t& chart,
    long double absolute_row, double previous_scroll, bool anchored) -> std::optional<double> {
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
    const auto target = anchored                     ? playhead_y - comfort_top
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
                                                   double absolute_row, bool anchored) -> bool {
    if (!std::isfinite(absolute_row) || absolute_row < 0.0 ||
        playhead_cursor_edit_blocked(state))
        return false;
    const auto chart = std::ranges::find(history.document.charts, state.chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end() || state.active_snap_denominator == 0 ||
        chart->divisions_per_standard_measure % state.active_snap_denominator != 0)
        return false;
    const auto built = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    if (!index || index->total_rows == 0) return false;
    const auto previous_scroll = state.viewport_scroll_pixels;
    const auto step = chart->divisions_per_standard_measure / state.active_snap_denominator;
    const auto bounded_row = std::min(static_cast<long double>(absolute_row),
                                      static_cast<long double>(index->total_rows - 1));
    const auto row = static_cast<std::uint64_t>(std::floor(bounded_row / step)) * step;
    const auto after = std::ranges::upper_bound(index->entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index->entries.begin()) return false;
    const auto measure = std::prev(after);
    const local_position_t position{measure->id, row - measure->absolute_begin_row};
    if ((state.cursor != position || state.virtual_bms_cursor_row) &&
        !dispatch_editor_action(
            history, state, jump_edit_cursor_action_t{position, history.document.revision}))
        return false;
    const auto target_scroll = compose_playhead_scroll_target(
        state, *chart, bounded_row, previous_scroll, anchored);
    if (!target_scroll) return false;
    if (std::abs(*target_scroll - state.viewport_scroll_pixels) < 0.001) return true;
    return static_cast<bool>(dispatch_editor_action(
        history, state, scroll_editor_view_action_t{
                            .vertical_pixels = *target_scroll - state.viewport_scroll_pixels}));
}

} // namespace wormedit::sys
