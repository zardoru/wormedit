#pragma once

#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
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

// Keep Compose's snapped edit cursor on the currently audible chart row.
[[nodiscard]] inline auto follow_compose_playhead(document_history_t& history,
                                                   editor_interaction_state_t& state,
                                                   double absolute_row) -> bool {
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
    const auto step = chart->divisions_per_standard_measure / state.active_snap_denominator;
    const auto bounded_row = std::min(static_cast<long double>(absolute_row),
                                      static_cast<long double>(index->total_rows - 1));
    const auto row = static_cast<std::uint64_t>(std::floor(bounded_row / step)) * step;
    const auto after = std::ranges::upper_bound(index->entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index->entries.begin()) return false;
    const auto measure = std::prev(after);
    const local_position_t position{measure->id, row - measure->absolute_begin_row};
    if (state.cursor == position && !state.virtual_bms_cursor_row) return true;
    return static_cast<bool>(dispatch_editor_action(
        history, state, jump_edit_cursor_action_t{position, history.document.revision}));
}

} // namespace wormedit::sys
