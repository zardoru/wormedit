#include "sys/application_command_dispatch.h"

#include "sys/compose_bookmark_commands.h"
#include "sys/compose_input.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_timing_delete.h"
#include "sys/external_player.h"
#include "sys/transport_player_actions.h"
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

auto clear_selection_or_cancel_preview(wormedit::document_history_t& history,
                                       wormedit::editor_interaction_state_t* state,
                                       application_command_state_t& application_state,
                                       std::string& status)
    -> wormedit::command_invocation_result_t {
    if (application_state.paste_session) {
        application_state.paste_session.reset();
        status = "Paste preview cancelled.";
        return {.accepted = true};
    }
    if (application_state.transport_inline_active &&
        application_state.transport_inline_active() &&
        application_state.transport_cancel_restore) {
        status = application_state.transport_cancel_restore();
        return {.accepted = true};
    }
    if (!state) {
        return reject_application_command(status,
                                          "Clear Selection requires the active Compose chart.");
    }
    (void)wormedit::dispatch_editor_action(history, *state,
                                           wormedit::cancel_editor_gesture_action_t{});
    application_state.note_paint_mode_active = false;
    application_state.semantic_layer_paint_mode_active = false;
    application_state.semantic_layer_solo_active = false;
    return accept_editor_command(
        wormedit::dispatch_editor_action(history, *state,
                                         wormedit::select_editor_notes_action_t{}),
        status, "Selection, pending Compose gesture, and note-paint mode cleared.");
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

struct application_command_context_t {
    wormedit::sys::application_command_t command;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    std::optional<wormedit::editor_interaction_state_t>& compose_state;
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage;
    application_command_state_t& application_state;
    std::string& status;

    auto compose() const -> wormedit::editor_interaction_state_t* {
        return workspace.active_tab == wormedit::workspace_tab_t::compose &&
                       workspace.selected_chart && compose_state &&
                       compose_state->chart == *workspace.selected_chart
                   ? &*compose_state : nullptr;
    }
    auto rebind() -> wormedit::command_invocation_result_t {
        const auto rebound = wormedit::rebind_workspace(workspace, history);
        if (!rebound)
            return reject_application_command(
                status, rebound.diagnostics.empty() ? "Command applied, but workspace rebinding failed."
                                                    : rebound.diagnostics.front().message,
                wormedit::diagnostic_code_t::invalid_id);
        return {.accepted = true};
    }
};

using command_result_t = wormedit::command_invocation_result_t;
using command_context_t = application_command_context_t;
using command_kind_t = wormedit::sys::application_command_kind_t;

auto history_command(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const auto kind = context.command.kind;
    if (kind == command_kind_t::history_undo || kind == command_kind_t::history_redo) {
        if (context.compose_state) {
            (void)dispatch_editor_action(context.history, *context.compose_state,
                                         cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(context.history, *context.compose_state,
                                         select_editor_notes_action_t{});
        }
        const bool undo = kind == command_kind_t::history_undo;
        const auto changed = undo ? undo_document_command(context.history)
                                  : redo_document_command(context.history);
        context.status = changed ? (undo ? "Undo applied." : "Redo applied.")
                                 : (undo ? "Nothing to undo." : "Nothing to redo.");
        if (!changed) return command_result_t{.accepted = true};
        context.timing_stage.reset();
        return context.rebind();
    }
    return std::nullopt;
}

auto external_player_command(command_context_t& context) -> std::optional<command_result_t> {
    external_player_action_t action;
    switch (context.command.kind) {
    case command_kind_t::transport_external_play_start: action = external_player_action_t::play_start; break;
    case command_kind_t::transport_external_play_here: action = external_player_action_t::play_here; break;
    case command_kind_t::transport_external_stop: action = external_player_action_t::stop; break;
    default: return std::nullopt;
    }
    if (!context.application_state.external_player)
        return reject_application_command(context.status, "External player is unavailable.");
    context.status = context.application_state.external_player(action);
    return command_result_t{.accepted = true};
}

auto metronome_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::transport_toggle_metronome) return std::nullopt;
    if (!toggle_transport_metronome(context.application_state))
        return reject_application_command(context.status, "Metronome is unavailable.");
    context.status = context.application_state.transport_metronome_enabled
                         ? "Metronome enabled." : "Metronome disabled.";
    return command_result_t{.accepted = true};
}

auto transport_command(command_context_t& context) -> std::optional<command_result_t> {
    if (const auto result = metronome_command(context)) return result;
    const auto kind = context.command.kind;
    auto& app = context.application_state;
    if (kind == command_kind_t::transport_rate_slower || kind == command_kind_t::transport_rate_faster) {
        const auto stepped =
            step_transport_rate_status(app, kind == command_kind_t::transport_rate_faster);
        if (!stepped)
            return reject_application_command(context.status, "Playback output rejected the rate change.");
        context.status = *stepped;
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_play_stop) {
        const auto result = play_stop_transport(app);
        if (!result)
            return reject_application_command(context.status, "Chart preview is unavailable.");
        context.status = *result;
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_play_pause) {
        if (!app.transport_play_pause)
            return reject_application_command(context.status, "Chart preview is unavailable.");
        context.status = app.transport_play_pause();
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_play_from_start) {
        if (!app.transport_play_from_start)
            return reject_application_command(context.status, "Chart preview is unavailable.");
        context.status = app.transport_play_from_start();
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_stop) {
        if (!app.transport_stop || !app.transport_active || !app.transport_active())
            return reject_application_command(context.status, "No audio preview is playing.");
        app.transport_stop();
        context.status = "Audio preview stopped.";
        return command_result_t{.accepted = true};
    }
    return std::nullopt;
}

auto clipboard_commands(command_context_t& context) -> std::optional<command_result_t> {
    const auto kind = context.command.kind;
    if (kind == command_kind_t::edit_paste) {
        const auto* state = context.compose();
        if (!state) return reject_application_command(context.status,
            "Paste requires the active Compose chart.");
        const bool accepted = begin_compose_paste(context.application_state,
            context.history.document, *state, context.status);
        return command_result_t{.accepted = accepted};
    }
    if (kind != command_kind_t::edit_copy && kind != command_kind_t::edit_cut)
        return std::nullopt;
    auto* state = context.compose();
    if (!state)
        return reject_application_command(context.status, "Clipboard requires the active Compose chart.");
    auto copied = kind == command_kind_t::edit_cut
        ? wormedit::cut_chart_selection(context.history, *state, context.history.document.revision)
        : wormedit::copy_chart_selection(context.history.document, *state,
                                         context.history.document.revision);
    if (!copied) return reject_application_command(context.status, std::move(copied.error));
    context.application_state.chart_clipboard = std::move(copied.clipboard);
    context.status = kind == command_kind_t::edit_cut ? "Selection cut." : "Selection copied.";
    if (kind == command_kind_t::edit_cut) return context.rebind();
    return command_result_t{.accepted = true};
}

auto edit_commands(command_context_t& context) -> std::optional<command_result_t> {
    if (const auto result = clipboard_commands(context)) return result;
    if (context.command.kind != command_kind_t::edit_delete) return std::nullopt;
    auto* state = context.compose();
    if (!state)
        return reject_application_command(context.status, "Delete requires the active Compose chart.");
    // Timing markers (SV, BPM, stops, ...) are deleted through the timing controller.
    const auto timing = wormedit::sys::delete_selected_timing_markers(
        context.history, *state, context.application_state.timing_locked);
    if (timing.handled) {
        if (!timing.command.applied)
            return reject_application_command(
                context.status, timing.command.diagnostics.empty()
                                    ? "Timing markers were not deleted."
                                    : timing.command.diagnostics.front().message);
        context.status = "Timing markers deleted.";
        const auto rebound = context.rebind();
        if (!rebound.accepted) return rebound;
        return command_result_t{.accepted = true};
    }
    const auto result = wormedit::dispatch_editor_action(
        context.history, *state, wormedit::delete_editor_selection_action_t{});
    auto accepted = accept_editor_command(
        result, context.status,
        result.document_changed ? "Selection deleted." : "No object at the cursor.");
    if (result.document_changed) {
        const auto rebound = context.rebind();
        if (!rebound.accepted) return rebound;
    }
    return accepted;
}

auto selection_commands(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (kind == command_kind_t::selection_clear)
        return clear_selection_or_cancel_preview(context.history, state,
                                                 context.application_state, context.status);
    if (kind != command_kind_t::selection_select_all && kind != command_kind_t::selection_invert)
        return std::nullopt;
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!state || !chart)
        return reject_application_command(context.status,
                                          "Selection command requires the active Compose chart.");
    std::unordered_set<std::uint64_t> selected;
    if (kind == command_kind_t::selection_invert) {
        selected.reserve(state->selected_notes.size());
        for (const auto note : state->selected_notes) selected.insert(note.value);
    }
    std::vector<object_id_t> notes;
    for (const auto& measure : chart->measures)
        for (const auto& note : measure.notes)
            if (kind == command_kind_t::selection_select_all || !selected.contains(note.id.value))
                notes.push_back(note.id);
    const bool all = kind == command_kind_t::selection_select_all;
    return accept_editor_command(
        dispatch_editor_action(context.history, *state,
                               select_editor_notes_action_t{std::move(notes)}),
        context.status, all ? "All playable notes selected." : "Playable-note selection inverted.");
}

auto navigation_commands(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (kind == command_kind_t::navigation_previous_snap ||
        kind == command_kind_t::navigation_next_snap) {
        if (!state)
            return reject_application_command(context.status,
                                               "Snap navigation requires the active Compose chart.");
        const auto direction = kind == command_kind_t::navigation_previous_snap
            ? interaction_direction_t::backward : interaction_direction_t::forward;
        const auto result = dispatch_editor_action(context.history, *state,
                                                   cycle_edit_snap_action_t{direction});
        if (result.accepted)
            remember_snap_denominator(context.application_state, state->chart,
                                      state->active_snap_denominator);
        return accept_editor_command(result, context.status,
            direction == interaction_direction_t::backward ? "Previous snap selected."
                                                             : "Next snap selected.");
    }
    if (kind != command_kind_t::navigation_previous_marker &&
        kind != command_kind_t::navigation_next_marker) return std::nullopt;
    if (!state)
        return reject_application_command(context.status,
            "Bookmark navigation requires the active Compose chart.");
    const auto direction = kind == command_kind_t::navigation_previous_marker
        ? marker_navigation_direction_t::previous : marker_navigation_direction_t::next;
    const auto marker = navigate_markers(context.history.document,
        context.history.document.revision, state->chart, state->cursor, direction);
    if (marker.status == marker_navigation_status_t::no_result) {
        context.status = direction == marker_navigation_direction_t::previous
            ? "No previous beat-anchored bookmark or section boundary."
            : "No next beat-anchored bookmark or section boundary.";
        return command_result_t{.accepted = true};
    }
    if (!marker || !marker.item->beat_position)
        return reject_application_command(context.status,
            marker.diagnostics.empty() ? "Bookmark navigation could not resolve an exact beat row."
                                       : marker.diagnostics.front().message,
            diagnostic_code_t::invalid_id);
    return accept_editor_command(dispatch_editor_action(context.history, *state,
        jump_edit_cursor_action_t{*marker.item->beat_position, marker.document_revision}),
        context.status, direction == marker_navigation_direction_t::previous
            ? "Moved to the previous bookmark or section boundary."
            : "Moved to the next bookmark or section boundary.");
}

// Primary+Up/Down jump bookmarks in screen direction; Primary+B adds one here.
auto bookmark_commands(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const auto kind = context.command.kind;
    const bool previous = kind == command_kind_t::navigation_previous_bookmark;
    if (!previous && kind != command_kind_t::navigation_next_bookmark &&
        kind != command_kind_t::markers_add_bookmark)
        return std::nullopt;
    auto* state = context.compose();
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!state || !chart)
        return reject_application_command(context.status,
            "Bookmark commands require the active Compose chart.");
    if (kind == command_kind_t::markers_add_bookmark && editor_interaction_has_pending_gesture(*state))
        return reject_application_command(context.status,
            "Finish or cancel the active Compose gesture before editing bookmarks.");
    auto outcome = [&] {
        if (kind == command_kind_t::markers_add_bookmark)
            return add_beat_bookmark_here(context.history, *state, context.application_state);
        const auto downscroll =
            compose_field_downscroll(*chart, context.application_state.editor_preferences);
        // The commands are bound to screen directions: Up is "previous" unless the field is flipped.
        const auto direction = compose_visual_navigation_direction(downscroll, previous);
        return navigate_beat_bookmark(context.history, *state, context.application_state,
            direction == interaction_direction_t::backward
                ? marker_navigation_direction_t::previous : marker_navigation_direction_t::next);
    }();
    context.status = outcome.status;
    if (outcome.document_changed) {
        const auto rebound = context.rebind();
        if (!rebound.accepted) return rebound;
    }
    return command_result_t{.accepted = outcome.accepted, .diagnostics = outcome.diagnostics};
}

auto workspace_tab_command(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    std::optional<workspace_tab_t> tab;
    switch (context.command.kind) {
    case command_kind_t::workspace_open_setup: tab = workspace_tab_t::setup; break;
    case command_kind_t::workspace_open_metadata: tab = workspace_tab_t::metadata; break;
    case command_kind_t::workspace_open_timing:
        // The Timing screen is retired; its command opens Compose in timing focus.
        tab = workspace_tab_t::compose;
        context.application_state.compose_timing_focus = true;
        break;
    case command_kind_t::workspace_open_compose: tab = workspace_tab_t::compose; break;
    case command_kind_t::workspace_open_storyboard: tab = workspace_tab_t::storyboard; break;
    case command_kind_t::workspace_open_audio_slices: tab = workspace_tab_t::audio_slices; break;
    case command_kind_t::workspace_open_compare_validate: tab = workspace_tab_t::compare_validate; break;
    default: return std::nullopt;
    }
    const auto applied = apply_workspace_action(context.workspace, context.history.document,
        context.history.document.revision, workspace_switch_tab_action_t{*tab});
    context.status = applied ? "Workspace tab changed."
        : (applied.diagnostics.empty() ? "Workspace tab change was rejected."
                                       : applied.diagnostics.front().message);
    return command_result_t{.accepted = applied.applied, .diagnostics = applied.diagnostics};
}

auto timing_lock_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::timing_toggle_lock) return std::nullopt;
    using namespace wormedit;
    auto& app = context.application_state;
    if (!(context.workspace.active_tab == workspace_tab_t::compose && app.compose_timing_focus))
        return reject_application_command(context.status,
                                          "Timing Lock requires Compose with Timing focus.");
    app.timing_locked = !app.timing_locked;
    if (app.timing_locked && context.timing_stage) {
        cancel_timing_lane_pseudonote(*context.timing_stage);
        context.timing_stage.reset();
    }
    context.status = app.timing_locked ? "Timing edits locked." : "Timing edits unlocked.";
    return command_result_t{.accepted = true};
}

auto note_kind_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::edit_note_kind) return std::nullopt;
    auto* state = context.compose();
    if (!state || !context.command.note_kind)
        return reject_application_command(context.status, "Note kind requires the active Compose chart.");
    const auto result = wormedit::arm_note_kind(context.history, *state,
        static_cast<wormedit::note_kind_t>(*context.command.note_kind));
    return accept_editor_command(result, context.status, "Note kind armed.");
}

auto semantic_layer_slot_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!state || !chart)
        return reject_application_command(context.status, "Layer selection requires the active Compose chart.");
    const auto slot = context.command.semantic_layer_slot.value_or(0);
    if (!slot) return reject_application_command(context.status, "That semantic-layer slot is unavailable.");
    auto& app = context.application_state;
    if (app.semantic_layer_palette_chart != chart->id) {
        app.semantic_layer_palette_chart = chart->id;
        app.semantic_layer_palette_page = 0;
    }
    const auto palette = make_semantic_layer_palette_window(
        *chart, app.semantic_layer_target, app.semantic_layer_palette_page, false,
        WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
    if (!palette || slot > palette->layer_indices.size())
        return reject_application_command(context.status, "That semantic-layer slot is unavailable.");
    const auto* layer = &chart->semantic_layers[palette->layer_indices[slot - 1]];
    app.semantic_layer_target = layer->id;
    context.status = "Semantic-layer target: " + layer->name;
    return {.accepted = true};
}

auto semantic_layer_membership_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    auto& app = context.application_state;
    if (!state || !app.semantic_layer_target || state->selected_notes.empty())
        return reject_application_command(context.status,
            "Choose a semantic layer and select playable notes first.");
    semantic_layer_edit_action_t edit;
    const auto kind = context.command.kind;
    if (kind == command_kind_t::semantic_layer_apply)
        edit = assign_notes_to_semantic_layer_action_t{
            state->chart, *app.semantic_layer_target, state->selected_notes};
    else if (kind == command_kind_t::semantic_layer_remove)
        edit = remove_notes_from_semantic_layer_action_t{
            state->chart, *app.semantic_layer_target, state->selected_notes};
    else
        edit = toggle_semantic_layer_membership_action_t{
            state->chart, *app.semantic_layer_target, state->selected_notes};
    const auto applied = apply_semantic_layer_edit(context.history,
        context.history.document.revision, edit);
    if (applied.applied) {
        const auto rebound = context.rebind();
        if (!rebound.accepted) return rebound;
    }
    context.status = applied.applied ? "Semantic-layer membership updated."
        : (applied.diagnostics.empty() ? "Semantic-layer membership was unchanged."
                                       : applied.diagnostics.front().message);
    return {.accepted = applied.applied || applied.diagnostics.empty(),
            .diagnostics = applied.diagnostics};
}

auto invoke_application_command(
    wormedit::sys::application_command_t command, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_state,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    application_command_state_t& application_state, std::string& status)
    -> wormedit::command_invocation_result_t {
    command_context_t context{command, history, workspace, compose_state, timing_stage,
                              application_state, status};
    const auto kind = command.kind;
    if (auto result = history_command(context)) return *result;
    if (auto result = note_kind_command(context)) return *result;
    if (auto result = transport_command(context)) return *result;
    if (auto result = external_player_command(context)) return *result;
    if (auto result = edit_commands(context)) return *result;
    if (auto result = selection_commands(context)) return *result;
    if (auto result = navigation_commands(context)) return *result;
    if (auto result = bookmark_commands(context)) return *result;
    if (auto result = workspace_tab_command(context)) return *result;
    if (auto result = timing_lock_command(context)) return *result;
    if (kind == command_kind_t::semantic_layer_select_slot)
        return semantic_layer_slot_command(context);
    if (kind == command_kind_t::semantic_layer_clear_target) {
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        status = "Semantic-layer target cleared.";
        return {.accepted = true};
    }
    if (kind == command_kind_t::semantic_layer_apply ||
        kind == command_kind_t::semantic_layer_remove ||
        kind == command_kind_t::semantic_layer_toggle)
        return semantic_layer_membership_command(context);
    return reject_application_command(status, "Command is not implemented by the application.");
}


} // namespace wormedit::sys
