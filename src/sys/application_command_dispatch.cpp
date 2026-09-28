#include "sys/application_command_dispatch.h"

#include "sys/compose_input.h"
#include "ui/frontend.h"

#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wormedit::sys {
namespace {

void remember_snap_denominator(application_command_state_t& state, wormedit::chart_id_t chart,
                               std::uint64_t denominator) {
    if (chart.value != 0 && denominator != 0) {
        state.requested_snap_denominators.insert_or_assign(chart.value, denominator);
    }
}

} // namespace

auto reject_application_command(
    std::string& status, std::string message,
    wormedit::diagnostic_code_t code)
    -> wormedit::command_invocation_result_t {
    status = message;
    return {
        .diagnostics = {{.severity = wormedit::diagnostic_severity_t::error,
                         .code = code,
                         .message = std::move(message)}},
    };
}

auto accept_editor_command(const wormedit::editor_interaction_result_t& result, std::string& status,
                           std::string_view accepted) -> wormedit::command_invocation_result_t {
    status = compose_result_status(result, accepted);
    return {.accepted = result.accepted, .diagnostics = result.diagnostics};
}

auto invoke_application_command(
    wormedit::sys::application_command_t command, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_state,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    application_command_state_t& application_state, std::string& status)
    -> wormedit::command_invocation_result_t {
    using namespace wormedit;
    using command_kind_t = wormedit::sys::application_command_kind_t;
    const auto kind = command.kind;
    const auto require_compose = [&]() -> editor_interaction_state_t* {
        return workspace.active_tab == workspace_tab_t::compose && workspace.selected_chart &&
                       compose_state && compose_state->chart == *workspace.selected_chart
                   ? &*compose_state
                   : nullptr;
    };
    const auto rebind_after_edit = [&]() -> command_invocation_result_t {
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return reject_application_command(
                status,
                rebound.diagnostics.empty() ? "Command applied, but workspace rebinding failed."
                                            : rebound.diagnostics.front().message,
                diagnostic_code_t::invalid_id);
        }
        return {.accepted = true};
    };

    if (kind == command_kind_t::history_undo || kind == command_kind_t::history_redo) {
        if (compose_state) {
            (void)dispatch_editor_action(history, *compose_state, cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(history, *compose_state, select_editor_notes_action_t{});
        }
        const bool undo = kind == command_kind_t::history_undo;
        const bool changed = undo ? undo_document_command(history) : redo_document_command(history);
        status = changed ? (undo ? "Undo applied." : "Redo applied.")
                         : (undo ? "Nothing to undo." : "Nothing to redo.");
        if (!changed) {
            return {.accepted = true};
        }
        timing_stage.reset();
        return rebind_after_edit();
    }

    if (kind == command_kind_t::transport_play_pause) {
        if (!application_state.transport_play_pause) {
            return reject_application_command(status, "Chart preview is unavailable.");
        }
        status = application_state.transport_play_pause();
        return {.accepted = true};
    }

    if (kind == command_kind_t::transport_stop) {
        if (!application_state.transport_stop || !application_state.transport_active ||
            !application_state.transport_active()) {
            return reject_application_command(status, "No audio preview is playing.");
        }
        application_state.transport_stop();
        status = "Audio preview stopped.";
        return {.accepted = true};
    }

    if (kind == command_kind_t::edit_delete) {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(status, "Delete requires the active Compose chart.");
        }
        const auto result = dispatch_editor_action(history, *state,
                                                   delete_editor_selection_action_t{});
        auto invocation_result = accept_editor_command(
            result, status,
            result.document_changed ? "Selection deleted." : "No object at the cursor.");
        if (result.document_changed) {
            const auto rebound = rebind_after_edit();
            if (!rebound) {
                return rebound;
            }
        }
        return invocation_result;
    }

    if (kind == command_kind_t::selection_clear) {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(status,
                                              "Clear Selection requires the active Compose chart.");
        }
        (void)dispatch_editor_action(history, *state, cancel_editor_gesture_action_t{});
        application_state.note_paint_mode_active = false;
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        return accept_editor_command(
            dispatch_editor_action(history, *state, select_editor_notes_action_t{}), status,
            "Selection, pending Compose gesture, and note-paint mode cleared.");
    }

    if (kind == command_kind_t::selection_select_all || kind == command_kind_t::selection_invert) {
        auto* state = require_compose();
        const auto* chart = selected_chart(history, workspace);
        if (!state || !chart) {
            return reject_application_command(
                status, "Selection command requires the active Compose chart.");
        }
        std::unordered_set<std::uint64_t> previously_selected;
        if (kind == command_kind_t::selection_invert) {
            previously_selected.reserve(state->selected_notes.size());
            for (const auto note : state->selected_notes) {
                previously_selected.insert(note.value);
            }
        }
        std::vector<object_id_t> notes;
        for (const auto& measure : chart->measures) {
            for (const auto& note : measure.notes) {
                if (kind == command_kind_t::selection_select_all || !previously_selected.contains(note.id.value)) {
                    notes.push_back(note.id);
                }
            }
        }
        return accept_editor_command(
            dispatch_editor_action(history, *state, select_editor_notes_action_t{std::move(notes)}),
            status,
            kind == command_kind_t::selection_select_all ? "All playable notes selected."
                                         : "Playable-note selection inverted.");
    }

    if (kind == command_kind_t::navigation_previous_snap || kind == command_kind_t::navigation_next_snap) {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(status,
                                              "Snap navigation requires the active Compose chart.");
        }
        const auto direction = kind == command_kind_t::navigation_previous_snap ? interaction_direction_t::backward
                                                                : interaction_direction_t::forward;
        const auto result =
            dispatch_editor_action(history, *state, cycle_edit_snap_action_t{direction});
        if (result.accepted) {
            remember_snap_denominator(application_state, state->chart,
                                      state->active_snap_denominator);
        }
        return accept_editor_command(result, status,
                                     direction == interaction_direction_t::backward
                                         ? "Previous snap selected."
                                         : "Next snap selected.");
    }

    if (kind == command_kind_t::navigation_previous_marker || kind == command_kind_t::navigation_next_marker) {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(
                status, "Bookmark navigation requires the active Compose chart.");
        }
        const auto direction = kind == command_kind_t::navigation_previous_marker
                                   ? marker_navigation_direction_t::previous
                                   : marker_navigation_direction_t::next;
        const auto marker = navigate_markers(history.document, history.document.revision,
                                             state->chart, state->cursor, direction);
        if (marker.status == marker_navigation_status_t::no_result) {
            status = direction == marker_navigation_direction_t::previous
                         ? "No previous beat-anchored bookmark or section boundary."
                         : "No next beat-anchored bookmark or section boundary.";
            return {.accepted = true};
        }
        if (!marker || !marker.item->beat_position) {
            const auto message = marker.diagnostics.empty()
                                     ? "Bookmark navigation could not resolve an exact beat row."
                                     : marker.diagnostics.front().message;
            return reject_application_command(status, message, diagnostic_code_t::invalid_id);
        }
        return accept_editor_command(
            dispatch_editor_action(
                history, *state,
                jump_edit_cursor_action_t{*marker.item->beat_position, marker.document_revision}),
            status,
            direction == marker_navigation_direction_t::previous
                ? "Moved to the previous bookmark or section boundary."
                : "Moved to the next bookmark or section boundary.");
    }

    std::optional<workspace_tab_t> tab;
    switch (kind) {
    case command_kind_t::workspace_open_setup: tab = workspace_tab_t::setup; break;
    case command_kind_t::workspace_open_metadata: tab = workspace_tab_t::metadata; break;
    case command_kind_t::workspace_open_timing: tab = workspace_tab_t::timing; break;
    case command_kind_t::workspace_open_compose: tab = workspace_tab_t::compose; break;
    case command_kind_t::workspace_open_storyboard: tab = workspace_tab_t::storyboard; break;
    case command_kind_t::workspace_open_audio_slices: tab = workspace_tab_t::audio_slices; break;
    case command_kind_t::workspace_open_compare_validate:
        tab = workspace_tab_t::compare_validate; break;
    default: break;
    }
    if (tab) {
        const auto applied =
            apply_workspace_action(workspace, history.document, history.document.revision,
                                   workspace_switch_tab_action_t{*tab});
        status = applied ? "Workspace tab changed."
                         : (applied.diagnostics.empty() ? "Workspace tab change was rejected."
                                                        : applied.diagnostics.front().message);
        return {.accepted = applied.applied, .diagnostics = applied.diagnostics};
    }

    if (kind == command_kind_t::timing_toggle_lock) {
        if (workspace.active_tab != workspace_tab_t::timing) {
            return reject_application_command(status,
                                              "Timing Lock requires the active Timing tab.");
        }
        application_state.timing_locked = !application_state.timing_locked;
        if (application_state.timing_locked && timing_stage) {
            cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
        }
        status =
            application_state.timing_locked ? "Timing edits locked." : "Timing edits unlocked.";
        return {.accepted = true};
    }

    if (kind == command_kind_t::semantic_layer_select_slot) {
        auto* state = require_compose();
        const auto* chart = selected_chart(history, workspace);
        if (!state || !chart) {
            return reject_application_command(status,
                                              "Layer selection requires the active Compose chart.");
        }
        const auto slot = command.semantic_layer_slot.value_or(0);
        if (slot == 0) {
            return reject_application_command(status, "That semantic-layer slot is unavailable.");
        }
        if (application_state.semantic_layer_palette_chart != chart->id) {
            application_state.semantic_layer_palette_chart = chart->id;
            application_state.semantic_layer_palette_page = 0;
        }
        const auto palette =
            make_semantic_layer_palette_window(*chart, application_state.semantic_layer_target,
                                               application_state.semantic_layer_palette_page, false,
                                               WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
        if (!palette || slot > palette->layer_indices.size()) {
            return reject_application_command(status, "That semantic-layer slot is unavailable.");
        }
        const auto* layer = &chart->semantic_layers[palette->layer_indices[slot - 1]];
        application_state.semantic_layer_target = layer->id;
        status = "Semantic-layer target: " + layer->name;
        return {.accepted = true};
    }

    if (kind == command_kind_t::semantic_layer_clear_target) {
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        status = "Semantic-layer target cleared.";
        return {.accepted = true};
    }

    if (kind == command_kind_t::semantic_layer_apply || kind == command_kind_t::semantic_layer_remove ||
        kind == command_kind_t::semantic_layer_toggle) {
        auto* state = require_compose();
        if (!state || !application_state.semantic_layer_target || state->selected_notes.empty()) {
            return reject_application_command(
                status, "Choose a semantic layer and select playable notes first.");
        }
        semantic_layer_edit_action_t action;
        if (kind == command_kind_t::semantic_layer_apply) {
            action = assign_notes_to_semantic_layer_action_t{
                state->chart, *application_state.semantic_layer_target, state->selected_notes};
        } else if (kind == command_kind_t::semantic_layer_remove) {
            action = remove_notes_from_semantic_layer_action_t{
                state->chart, *application_state.semantic_layer_target, state->selected_notes};
        } else {
            action = toggle_semantic_layer_membership_action_t{
                state->chart, *application_state.semantic_layer_target, state->selected_notes};
        }
        const auto applied = apply_semantic_layer_edit(history, history.document.revision, action);
        if (applied.applied) {
            const auto rebound = rebind_after_edit();
            if (!rebound) {
                return rebound;
            }
        }
        status = applied.applied
                     ? "Semantic-layer membership updated."
                     : (applied.diagnostics.empty() ? "Semantic-layer membership was unchanged."
                                                    : applied.diagnostics.front().message);
        return {.accepted = applied.applied || applied.diagnostics.empty(),
                .diagnostics = applied.diagnostics};
    }

    return reject_application_command(status, "Command is not implemented by the application.");
}


} // namespace wormedit::sys
