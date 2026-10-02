#pragma once

#include <wormedit/core/interaction.h>
#include <wormedit/core/kiai_spans.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/document.h>

#include "sys/compose_timing_delete.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::sys {

// Kiai areas are selected through the application state (their opening event), not the
// interaction selection. A copy of the state with both boundary events added lets the clipboard
// treat the area like any other timing objects.
[[nodiscard]] inline auto with_kiai_span_selection(const document_history_t& history,
                                                   editor_interaction_state_t state,
                                                   std::uint64_t head)
    -> editor_interaction_state_t {
    if (head == 0) return state;
    const auto chart = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end()) return state;
    const auto indexed = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&indexed);
    if (!index) return state;
    for (const auto& span : kiai_spans_of(*chart, *index)) {
        if (span.head.value != head) continue;
        state.selected_objects.push_back({editor_selection_object_kind_t::kiai_event, span.head});
        if (!span.open_ended)
            state.selected_objects.push_back({editor_selection_object_kind_t::kiai_event, span.tail});
    }
    return state;
}

// Removes what a cut copied: the selected Kiai area, then the selected timing markers, then
// notes and BGM events. Returns an error message, empty on success.
[[nodiscard]] inline auto delete_cut_selection(document_history_t& history,
                                               editor_interaction_state_t& state,
                                               std::uint64_t& kiai_head, bool timing_locked)
    -> std::string {
    if (kiai_head != 0) {
        const auto edited = apply_marker_edit(
            history, history.document.revision,
            edit_kiai_span_action_t{.chart = state.chart, .edit = {.head = object_id_t{kiai_head}}});
        if (!edited.applied) return "The Kiai area could not be cut.";
        kiai_head = 0;
    }
    std::vector<editor_selection_object_t> rest;
    for (const auto& object : state.selected_objects)
        if (!timing_lane_of_selection(object.kind)) rest.push_back(object);
    const auto timing = delete_selected_timing_markers(history, state, timing_locked);
    if (timing.handled) {
        if (!timing.command.applied) return "The timing markers could not be cut.";
        state.selected_objects = rest;
    }
    if (state.selected_objects.empty() && state.selected_notes.empty()) return {};
    const auto deleted = dispatch_editor_action(history, state, delete_editor_selection_action_t{});
    return deleted ? std::string{} : std::string{"The selection could not be cut."};
}

} // namespace wormedit::sys
