#pragma once

#include "sys/metadata_commands.h"

#include <wormedit/core/interaction.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <algorithm>

namespace wormedit::sys {

inline auto selected_compose_bgm_events(const editor_interaction_state_t& state)
    -> std::vector<object_id_t> {
    std::vector<object_id_t> selected;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event)
            selected.push_back(object.id);
    return selected;
}

// Context selection preserves an existing multi-note selection when its hit note is already in it.
inline auto select_compose_note_context(document_history_t& history,
    editor_interaction_state_t& state, std::uint64_t revision, object_id_t note)
    -> editor_interaction_result_t {
    if (!document_stamp_t{.project = history.document.id, .revision = revision}.is_current(history.document))
        return {.diagnostics = {{.severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_id,
            .message = stale_action_message("Note context")}}};
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
    const auto bgm_events = selected_compose_bgm_events(state);
    semantic_layer_edit_action_t edit = candidate_none
        ? semantic_layer_edit_action_t{clear_semantic_layer_membership_action_t{
              state.chart, state.selected_notes, bgm_events}}
        : semantic_layer_edit_action_t{replace_notes_semantic_layer_action_t{
              state.chart, candidate, state.selected_notes, false, bgm_events}};
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, revision, "Semantic layer assignment"))
        return std::move(*rejected);
    const auto result = apply_semantic_layer_edit(history, edit);
    if (result.applied || result.diagnostics.empty()) {
        target_none = candidate_none;
        target = candidate_none ? std::nullopt : std::optional{candidate};
    }
    return result;
}

inline auto apply_compose_selected_sound_layer_assignment(document_history_t& history,
    const editor_interaction_state_t& state, std::uint64_t revision, layer_id_t target)
    -> document_command_result_t {
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, revision, "Selected sound layer assignment"))
        return std::move(*rejected);
    return add_selected_sounds_to_semantic_layer(history,
        {state.chart, target, state.selected_notes, selected_compose_bgm_events(state)});
}

inline auto begin_compose_layer_paint(document_history_t& history,
    editor_interaction_state_t& state, bool target_none, layer_id_t target,
    object_id_t note, bool captured, std::optional<object_id_t> bgm_event = std::nullopt,
    std::optional<semantic_layer_paint_mode_t> mode = std::nullopt)
    -> editor_interaction_result_t {
    return dispatch_editor_action(history, state,
        begin_semantic_layer_paint_gesture_action_t{
            .layer = target, .note = note, .pointer_captured = captured,
            .replace_memberships = !mode.has_value(), .clear_memberships = target_none,
            .bgm_event = bgm_event, .mode = mode.value_or(semantic_layer_paint_mode_t::add)});
}

inline auto compose_layer_brush_target_editable(const song_document_t& document,
    chart_id_t chart_id, std::optional<layer_id_t> target) -> bool {
    if (!target) return false;
    const auto chart = document.find_chart(chart_id);
    if (chart == nullptr) return false;
    const auto layer = std::ranges::find(chart->semantic_layers, *target, &semantic_layer_t::id);
    return layer != chart->semantic_layers.end() && layer->visible && !layer->locked;
}

inline auto finish_compose_layer_paint(document_history_t& history,
    editor_interaction_state_t& state, object_id_t note,
    std::optional<object_id_t> bgm_event = std::nullopt) -> editor_interaction_result_t {
    auto result = dispatch_editor_action(history, state,
                                         update_semantic_layer_paint_gesture_action_t{
                                             .note = note,
                                             .pointer_captured = true,
                                             .bgm_event = bgm_event,
                                             .operation = wormedit::proposal_operation_t::apply});
    if (!result.accepted)
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
    return result;
}

} // namespace wormedit::sys
