#pragma once

#include <wormedit/core/interaction.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <algorithm>

namespace wormedit::sys {

// Context selection preserves an existing multi-note selection when its hit note is already in it.
inline auto select_compose_note_context(document_history_t& history,
    editor_interaction_state_t& state, std::uint64_t revision, object_id_t note)
    -> editor_interaction_result_t {
    if (revision != history.document.revision)
        return {.diagnostics = {{.severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_id,
            .message = "Note context changed or a Compose gesture is active."}}};
    if (std::ranges::find(state.selected_notes, note) != state.selected_notes.end())
        return {.accepted = true};
    return dispatch_editor_action(history, state,
        select_editor_objects_action_t{{{editor_selection_object_kind_t::note, note}},
            editor_area_selection_mode_t::replace, true});
}

// Bind a candidate target only after its membership edit succeeds or is a no-op.
inline auto apply_compose_layer_assignment(document_history_t& history,
    const editor_interaction_state_t& state, std::uint64_t revision,
    bool candidate_none, layer_id_t candidate, bool& target_none,
    std::optional<layer_id_t>& target) -> document_command_result_t {
    semantic_layer_edit_action_t edit = candidate_none
        ? semantic_layer_edit_action_t{clear_semantic_layer_membership_action_t{state.chart, state.selected_notes}}
        : semantic_layer_edit_action_t{replace_notes_semantic_layer_action_t{state.chart, candidate, state.selected_notes}};
    const auto result = apply_semantic_layer_edit(history, revision, edit);
    if (result.applied || result.diagnostics.empty()) {
        target_none = candidate_none;
        target = candidate_none ? std::nullopt : std::optional{candidate};
    }
    return result;
}

inline auto begin_compose_layer_paint(document_history_t& history,
    editor_interaction_state_t& state, bool target_none, layer_id_t target,
    object_id_t note, bool captured) -> editor_interaction_result_t {
    return dispatch_editor_action(history, state,
        begin_semantic_layer_paint_gesture_action_t{
            .layer = target, .note = note, .pointer_captured = captured,
            .replace_memberships = true, .clear_memberships = target_none});
}

inline auto finish_compose_layer_paint(document_history_t& history,
    editor_interaction_state_t& state, object_id_t note) -> editor_interaction_result_t {
    auto result = dispatch_editor_action(history, state,
        end_semantic_layer_paint_gesture_action_t{note, true});
    if (!result.accepted)
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
    return result;
}

} // namespace wormedit::sys
