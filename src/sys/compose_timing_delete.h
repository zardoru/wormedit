#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/timing_lane_controller.h>

#include <optional>
#include <vector>

namespace wormedit::sys {

// The timing lane a selected object belongs to, or nullopt for notes, BGM and other objects.
[[nodiscard]] inline auto timing_lane_of_selection(editor_selection_object_kind_t kind)
    -> std::optional<timing_lane_kind_t> {
    using selection_t = editor_selection_object_kind_t;
    switch (kind) {
    case selection_t::bpm_event: return timing_lane_kind_t::bpm;
    case selection_t::scroll_event: return timing_lane_kind_t::scroll;
    case selection_t::speed_event: return timing_lane_kind_t::speed;
    case selection_t::stop_event: return timing_lane_kind_t::stop;
    case selection_t::delay_event: return timing_lane_kind_t::delay;
    case selection_t::warp_event: return timing_lane_kind_t::warp;
    case selection_t::kiai_event: return timing_lane_kind_t::kiai;
    case selection_t::timing_anchor: return timing_lane_kind_t::anchor;
    default: return std::nullopt;
    }
}

struct timing_selection_delete_t {
    bool handled{};
    document_command_result_t command;
};

// Deletes every selected timing marker as one undoable command (Delete key, menu and compose
// input all share this). `handled` is false when the selection holds no timing marker, so the
// caller falls through to the note/BGM/marker delete. A successful delete clears the selection.
[[nodiscard]] inline auto delete_selected_timing_markers(document_history_t& history,
                                                         editor_interaction_state_t& state,
                                                         bool timing_locked)
    -> timing_selection_delete_t {
    std::vector<timing_lane_event_ref_t> selected;
    for (const auto& object : state.selected_objects) {
        if (const auto lane = timing_lane_of_selection(object.kind))
            selected.push_back({*lane, object.id});
    }
    if (selected.empty()) return {};
    timing_selection_delete_t outcome{.handled = true};
    outcome.command = delete_timing_lane_event_group(
        history, state.chart, selected, history.document.revision, timing_locked);
    if (outcome.command.applied) state.selected_objects.clear();
    return outcome;
}

} // namespace wormedit::sys
