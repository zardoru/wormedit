#include "sys/application_command_dispatch.h"
#include "sys/application_edit_inputs.h"
#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_measure_commands.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_quantize_request.h"
#include "sys/compose_semantic_layer_commands.h"

#include "sys/bookmark_commands.h"
#include <wormedit/core/chart_clipboard.h>
#include "sys/compose_input.h"
#include "sys/compose_sound_selection.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_view.h"
#include "sys/external_player.h"
#include "sys/transport_player_actions.h"
#include "ui/frontend.h"

#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wormedit::sys {
void reset_application_project_edit_state(application_command_state_t& current) {
    current.bookmarks.reset_project();
    current.project = {};
}

namespace {

void remember_snap_denominator(application_command_state_t& state, wormedit::chart_id_t chart,
                               std::uint64_t denominator) {
    if (chart.value != 0 && denominator != 0) {
        state.project.compose_view.requested_snap_denominators.insert_or_assign(chart.value, denominator);
    }
}

// One shared grid: the Quantize target follows the snap, keeping its other settings.
void follow_snap_with_quantize_grid(application_command_state_t& application,
                                    const wormedit::document_history_t& history,
                                    const wormedit::editor_interaction_state_t& state) {
    auto& editor = application.project.quantize.quantize_editor;
    if (!editor || !editor->stamp.is_identity_current(history.document, state.chart) ||
        editor->request.divisions == state.active_snap_denominator)
        return;
    editor->request.divisions = state.active_snap_denominator;
    editor->preset_index.reset();
}

auto clear_selection_or_cancel_preview(wormedit::document_history_t& history,
                                       wormedit::editor_interaction_state_t* state,
                                       application_command_state_t& application_state,
                                       notification_sink_t& sink)
    -> wormedit::command_invocation_result_t {
    if (application_state.project.clipboard.paste_session) {
        application_state.project.clipboard.paste_session.reset();
        sink.post(notification_severity_t::hint, notification_source_t::edit,
                  "Paste preview cancelled.");
        return {.accepted = true};
    }
    if (application_state.services.transport_inline_active &&
        application_state.services.transport_inline_active() &&
        application_state.services.transport_cancel_restore) {
        static_cast<void>(application_state.services.transport_cancel_restore());
        return {.accepted = true};
    }
    if (!state) {
        return reject_application_command(sink,
            "Clear Selection requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit);
    }
    (void)wormedit::dispatch_editor_action(history, *state,
                                           wormedit::cancel_editor_gesture_action_t{});
    application_state.project.compose_gesture.note_paint_mode_active = false;
    application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
    application_state.project.semantic_layer.semantic_layer_solo_active = false;
    return accept_editor_command(
        wormedit::dispatch_editor_action(history, *state,
                                         wormedit::select_editor_notes_action_t{}),
        sink, "Selection, pending Compose gesture, and note-paint mode cleared.",
        notification_severity_t::success, notification_source_t::edit);
}

auto post_command_message(notification_sink_t& sink, notification_severity_t severity,
                          notification_source_t source, std::string_view message)
    -> void {
    sink.post_feedback(severity, source, message);
}

} // namespace

auto reject_application_command(
    notification_sink_t& sink, std::string message, notification_severity_t severity,
    notification_source_t source,
    wormedit::diagnostic_code_t code)
    -> wormedit::command_invocation_result_t {
    post_command_message(sink, severity, source, message);
    return {
        .diagnostics = {{.severity = wormedit::diagnostic_severity_t::error,
                         .code = code,
                         .message = std::move(message)}},
    };
}

auto accept_editor_command(const wormedit::editor_interaction_result_t& result,
                           notification_sink_t& sink, std::string_view accepted,
                           notification_severity_t accepted_severity,
                           notification_source_t source) -> wormedit::command_invocation_result_t {
    if (result.accepted) {
        post_command_message(sink, accepted_severity, source, std::string{accepted});
    } else if (!result.diagnostics.empty()) {
        const auto& diagnostic = result.diagnostics.front();
        post_command_message(sink, to_notification_severity(diagnostic.severity), source,
                             diagnostic.message);
    } else {
        post_command_message(sink, notification_severity_t::warning, source,
                             "Compose action was rejected.");
    }
    return {.accepted = result.accepted, .diagnostics = result.diagnostics};
}

struct application_command_context_t {
    const command_dispatch_inputs_t& environment;
    wormedit::sys::application_command_t command;

    auto compose() const -> wormedit::editor_interaction_state_t* {
        return environment.document.workspace.active_tab == wormedit::workspace_tab_t::compose &&
                       environment.document.workspace.selected_chart &&
                       environment.editor.retained &&
                       environment.editor.retained->chart ==
                           *environment.document.workspace.selected_chart
                   ? &*environment.editor.retained
                   : nullptr;
    }
    auto rebind() -> wormedit::command_invocation_result_t {
        auto outcome = compose_action_outcome_t::from({}, notification_severity_t::success,
                                                      notification_source_t::document, true);
        outcome.delivery = compose_feedback_delivery_t::none;
        outcome.rebind_failure =
            "Command applied, but environment.document.workspace rebinding failed.";
        outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::document_only;
        outcome.is_rebind_severity_diagnostic = false;
        auto finished = finish_compose_action(
            environment.document, environment.document.notifications, std::move(outcome));
        if (!finished.accepted)
            return {.diagnostics = {{.severity = diagnostic_severity_t::error,
                                     .code = diagnostic_code_t::invalid_id,
                                     .message = std::move(finished.message)}}};
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
        if (context.environment.editor.retained) {
            (void)dispatch_editor_action(context.environment.document.history,
                                         *context.environment.editor.retained,
                                         cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(context.environment.document.history,
                                         *context.environment.editor.retained,
                                         select_editor_notes_action_t{});
        }
        const bool undo = kind == command_kind_t::history_undo;
        const auto changed = undo ? undo_document_command(context.environment.document.history)
                                  : redo_document_command(context.environment.document.history);
        post_command_message(context.environment.document.notifications,
                             changed ? notification_severity_t::success
                                     : notification_severity_t::info,
                             notification_source_t::edit,
                             changed ? (undo ? "Undo applied." : "Redo applied.")
                                     : (undo ? "Nothing to undo." : "Nothing to redo."));
        if (!changed) return command_result_t{.accepted = true};
        context.environment.editor.timing_stage.reset();
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
    if (!context.environment.editor.commands.services.external_player)
        return reject_application_command(
            context.environment.document.notifications, "External player is unavailable.",
            notification_severity_t::warning, notification_source_t::audio);
    static_cast<void>(context.environment.editor.commands.services.external_player(action));
    return command_result_t{.accepted = true};
}

auto metronome_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::transport_toggle_metronome) return std::nullopt;
    if (!toggle_transport_metronome(context.environment.editor.commands))
        return reject_application_command(
            context.environment.document.notifications, "Metronome is unavailable.",
            notification_severity_t::warning, notification_source_t::audio);
    post_command_message(
        context.environment.document.notifications, notification_severity_t::success,
        notification_source_t::audio,
        context.environment.editor.commands.metronome_settings().enabled
            ? "Metronome enabled."
            : "Metronome disabled.");
    return command_result_t{.accepted = true};
}

auto cursor_measure_operation(command_kind_t kind) -> std::optional<cursor_measure_command_t> {
    switch (kind) {
    case command_kind_t::measures_insert_before: return cursor_measure_command_t::insert_before;
    case command_kind_t::measures_insert_after: return cursor_measure_command_t::insert_after;
    case command_kind_t::measures_remove_empty: return cursor_measure_command_t::remove_empty;
    default: return std::nullopt;
    }
}

auto measure_command(command_context_t& context) -> std::optional<command_result_t> {
    const auto operation = cursor_measure_operation(context.command.kind);
    if (!operation && context.command.kind != command_kind_t::measures_split_at_cursor) return std::nullopt;
    auto* state = context.compose();
    if (!state)
        return reject_application_command(context.environment.document.notifications,
                                          "Measure editing requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    if (const auto error = compose_measure_edit_rejection(
            *state, context.environment.editor.commands.project.clipboard.paste_session.has_value()))
        return reject_application_command(context.environment.document.notifications, *error,
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    auto severity = notification_severity_t::warning;
    const auto message =
        operation ? apply_cursor_measure_command(context.environment.document.history, *state,
                                                 *operation, &severity)
                  : apply_cursor_measure_split(context.environment.document.history, *state, &severity);
    post_command_message(context.environment.document.notifications, severity,
                         notification_source_t::edit, message);
    if (severity == notification_severity_t::warning) return command_result_t{};
    return context.rebind();
}

auto loop_selection_command(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const bool toggle = context.command.kind == command_kind_t::transport_toggle_loop_selection;
    if (!toggle && context.command.kind != command_kind_t::transport_loop_selection) return std::nullopt;
    const bool disable =
        toggle &&
        context.environment.editor.commands.services.transport_loop_selection_active &&
        context.environment.editor.commands.services.transport_loop_selection_active();
    const auto* state = context.compose();
    const auto chart =
        std::ranges::find(context.environment.document.history.document.charts,
                          state ? state->chart : chart_id_t{}, &chart_document_t::id);
    if (!state || chart == context.environment.document.history.document.charts.end() ||
        (!disable && !state->retained_selection_range) ||
        editor_interaction_has_pending_gesture(*state) ||
        context.environment.editor.commands.project.clipboard.paste_session ||
        !context.environment.editor.commands.services.transport_set_loop_selection)
        return reject_application_command(
            context.environment.document.notifications, "Select a committed area to loop playback.",
            notification_severity_t::warning, notification_source_t::audio);
    if (disable) {
        if (!context.environment.editor.commands.services.transport_set_loop_selection(
                state->chart, context.environment.document.history.document.revision, 0, 0, false))
            return reject_application_command(context.environment.document.notifications,
                                              "Loop selection is unavailable or expired.",
                                              notification_severity_t::warning,
                                              notification_source_t::audio);
        post_command_message(context.environment.document.notifications,
                             notification_severity_t::success, notification_source_t::audio,
                             "Loop selection disabled.");
        return command_result_t{.accepted = true};
    }
    const auto indexed = build_measure_index(*chart);
    const auto* measures = std::get_if<measure_index_t>(&indexed);
    const auto begin = measures ? absolute_row(*measures, state->retained_selection_range->begin) : std::nullopt;
    const auto end = measures ? absolute_row(*measures, state->retained_selection_range->end) : std::nullopt;
    if (!begin || !end || *begin >= *end ||
        !context.environment.editor.commands.services.transport_set_loop_selection(
            state->chart, context.environment.document.history.document.revision, *begin, *end, true))
        return reject_application_command(
            context.environment.document.notifications, "Loop selection is unavailable or expired.",
            notification_severity_t::warning, notification_source_t::audio);
    post_command_message(context.environment.document.notifications,
                         notification_severity_t::success, notification_source_t::audio,
                         "Loop selection enabled.");
    return command_result_t{.accepted = true};
}

auto transport_command(command_context_t& context) -> std::optional<command_result_t> {
    if (const auto result = metronome_command(context)) return result;
    const auto kind = context.command.kind;
    auto& app = context.environment.editor.commands;
    if (kind == command_kind_t::transport_rate_slower || kind == command_kind_t::transport_rate_faster) {
        const auto prior_rate = app.preferences.transport_rate_index;
        const auto stepped =
            step_transport_rate_status(app, kind == command_kind_t::transport_rate_faster);
        if (!stepped)
            return reject_application_command(context.environment.document.notifications,
                                              "Playback output rejected the rate change.",
                                              notification_severity_t::warning,
                                              notification_source_t::audio);
        post_command_message(context.environment.document.notifications,
                             prior_rate == app.preferences.transport_rate_index
                                 ? notification_severity_t::info
                                 : notification_severity_t::success,
                             notification_source_t::audio, *stepped);
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_play_stop) {
        const bool hard_stop = app.services.transport_preview_playing && app.services.transport_preview_playing();
        const auto result = play_stop_transport(app);
        if (!result)
            return reject_application_command(
                context.environment.document.notifications, "Chart preview is unavailable.",
                notification_severity_t::warning, notification_source_t::audio);
        if (hard_stop)
            post_command_message(context.environment.document.notifications,
                                 notification_severity_t::hint, notification_source_t::audio,
                                 "Audio preview stopped.");
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_play_pause) {
        if (!app.services.transport_play_pause)
            return reject_application_command(
                context.environment.document.notifications, "Chart preview is unavailable.",
                notification_severity_t::warning, notification_source_t::audio);
        static_cast<void>(app.services.transport_play_pause());
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_play_from_start) {
        if (!app.services.transport_play_from_start)
            return reject_application_command(
                context.environment.document.notifications, "Chart preview is unavailable.",
                notification_severity_t::warning, notification_source_t::audio);
        static_cast<void>(app.services.transport_play_from_start());
        return command_result_t{.accepted = true};
    }
    if (kind == command_kind_t::transport_stop) {
        if (!app.services.transport_stop || !app.services.transport_active || !app.services.transport_active())
            return reject_application_command(
                context.environment.document.notifications, "No audio preview is playing.",
                notification_severity_t::hint, notification_source_t::audio);
        app.services.transport_stop();
        post_command_message(context.environment.document.notifications,
                             notification_severity_t::hint, notification_source_t::audio,
                             "Audio preview stopped.");
        return command_result_t{.accepted = true};
    }
    return std::nullopt;
}

auto note_to_bgm_command(command_context_t& context) -> std::optional<command_result_t> {
    const auto kind = context.command.kind;
    if (kind != command_kind_t::edit_move_to_bgm && kind != command_kind_t::edit_move_next_to_bgm)
        return std::nullopt;
    auto* state = context.compose();
    const auto found =
        std::ranges::find(context.environment.document.history.document.charts,
                          state ? state->chart : chart_id_t{}, &chart_document_t::id);
    const auto* chart =
        found == context.environment.document.history.document.charts.end() ? nullptr : &*found;
    if (!state || !chart ||
        !compose_move_to_bgm_available(*chart, *state,
                                       context.environment.editor.commands))
        return reject_application_command(
            context.environment.document.notifications,
            "Move to BGM requires selected notes on an editable BMS chart.",
            notification_severity_t::hint, notification_source_t::edit);
    const auto moved = move_editor_selection_to_bgm(
        context.environment.document.history, *state,
        std::nullopt, kind == command_kind_t::edit_move_next_to_bgm);
    if (!moved.payload.handled)
        return reject_application_command(context.environment.document.notifications,
                                          "Select playable notes before moving them to BGM.",
                                          notification_severity_t::hint,
                                          notification_source_t::edit);
    if (!moved.command.applied) {
        const auto message = moved.command.diagnostics.empty() ? "The notes could not be moved to BGM."
                                                               : moved.command.diagnostics.front().message;
        return reject_application_command(context.environment.document.notifications, message,
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    }
    const auto rebound = context.rebind();
    if (!rebound.accepted) return rebound;
    post_command_message(context.environment.document.notifications, notification_severity_t::hint,
                         notification_source_t::edit, "Selected notes moved to BGM.");
    return command_result_t{.accepted = true};
}

auto clipboard_commands(command_context_t& context) -> std::optional<command_result_t> {
    const auto kind = context.command.kind;
    if (kind == command_kind_t::edit_paste) {
        const auto* state = context.compose();
        if (!state)
            return reject_application_command(context.environment.document.notifications,
                                              "Paste requires the active Compose chart.",
                                              notification_severity_t::warning,
                                              notification_source_t::edit);
        std::string message;
        const bool accepted =
            begin_compose_paste(context.environment.editor.commands,
                                context.environment.document.history.document, *state, message);
        post_command_message(context.environment.document.notifications,
                             accepted ? notification_severity_t::hint
                                      : notification_severity_t::warning,
                             notification_source_t::edit, std::move(message));
        return command_result_t{.accepted = accepted};
    }
    if (kind != command_kind_t::edit_copy && kind != command_kind_t::edit_cut)
        return std::nullopt;
    auto* state = context.compose();
    if (!state)
        return reject_application_command(context.environment.document.notifications,
                                          "Clipboard requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    const wormedit::selection_delete_options_t options{
        .kiai_head =
            wormedit::object_id_t{
                context.environment.editor.commands.project.timing_recipe.kiai_selected_head},
        .recipe =
            wormedit::recipe_id_t{
                context.environment.editor.commands.project.timing_recipe.recipe_selected},
        .timing_locked = context.environment.editor.commands.project.timing_recipe.timing_locked};
    if (options.recipe)
        return reject_application_command(context.environment.document.notifications,
                                          "SV recipes cannot be copied to the chart clipboard.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    auto copied =
        kind == command_kind_t::edit_cut
            ? wormedit::create_cut_chart_clipboard(context.environment.document.history, *state,
                                            options)
            : wormedit::chart_clipboard_t::from(context.environment.document.history.document, *state,
                                             context.environment.document.history.document.revision,
                                             options.kiai_head);
    if (!copied)
        return reject_application_command(context.environment.document.notifications,
                                          std::move(copied.error()), notification_severity_t::warning,
                                          notification_source_t::edit);
    if (kind == command_kind_t::edit_cut)
        context.environment.editor.commands.project.timing_recipe.kiai_selected_head = 0;
    context.environment.editor.commands.project.clipboard.chart_clipboard = std::move(*copied);
    post_command_message(context.environment.document.notifications,
                         notification_severity_t::success, notification_source_t::edit,
                         kind == command_kind_t::edit_cut ? "Selection cut." : "Selection copied.");
    if (kind == command_kind_t::edit_cut) return context.rebind();
    return command_result_t{.accepted = true};
}

auto edit_commands(command_context_t& context) -> std::optional<command_result_t> {
    if (const auto result = clipboard_commands(context)) return result;
    if (context.command.kind != command_kind_t::edit_delete) return std::nullopt;
    auto* state = context.compose();
    if (!state)
        return reject_application_command(
            context.environment.document.notifications, "Delete requires the active Compose chart.",
            notification_severity_t::warning, notification_source_t::edit);
    const auto result = wormedit::delete_selection_all(
        context.environment.document.history, *state,
        {.kiai_head =
             wormedit::object_id_t{
                 context.environment.editor.commands.project.timing_recipe.kiai_selected_head},
         .recipe =
             wormedit::recipe_id_t{
                 context.environment.editor.commands.project.timing_recipe.recipe_selected},
         .timing_locked = context.environment.editor.commands.project.timing_recipe.timing_locked});
    if (result.document_changed) {
        context.environment.editor.commands.project.timing_recipe.kiai_selected_head = 0;
        context.environment.editor.commands.project.timing_recipe.recipe_selected = 0;
    }
    auto accepted = accept_editor_command(
        result, context.environment.document.notifications,
        result.document_changed ? "Selection deleted." : "No object at the cursor.",
        result.document_changed ? notification_severity_t::success : notification_severity_t::info,
        notification_source_t::edit);
    if (result.document_changed) {
        const auto rebound = context.rebind();
        if (!rebound.accepted) return rebound;
    }
    return accepted;
}

auto same_sound_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!state || !chart)
        return reject_application_command(
            context.environment.document.notifications, "Hover a note or select a sound first",
            notification_severity_t::hint, notification_source_t::edit);
    auto request =
        compose_same_sound_request(context.environment.document.history.document, *chart, *state,
                                   context.environment.editor.commands, false,
                                   editor_area_selection_mode_t::add);
    if (const auto* error = std::get_if<std::string>(&request))
        return reject_application_command(context.environment.document.notifications, *error,
                                          notification_severity_t::hint,
                                          notification_source_t::edit);
    return accept_editor_command(
        dispatch_editor_action(context.environment.document.history, *state,
                               std::get<select_editor_objects_action_t>(std::move(request))),
        context.environment.document.notifications, "Matching sounds added to selection.",
        notification_severity_t::success, notification_source_t::edit);
}

auto selection_commands(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (kind == command_kind_t::selection_select_same_sound_chart) return same_sound_command(context);
    if (kind == command_kind_t::selection_clear)
        return clear_selection_or_cancel_preview(context.environment.document.history, state,
                                                 context.environment.editor.commands,
                                                 context.environment.document.notifications);
    if (kind != command_kind_t::selection_select_all && kind != command_kind_t::selection_invert &&
        kind != command_kind_t::selection_select_all_objects)
        return std::nullopt;
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!state || !chart)
        return reject_application_command(context.environment.document.notifications,
                                          "Selection command requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    const auto all_objects = kind == command_kind_t::selection_select_all_objects;
    const auto invert = kind == command_kind_t::selection_invert;
    return accept_editor_command(
        select_editor_scope(context.environment.document.history, *state,
                            all_objects ? editor_selection_scope_t{true, true, true, true}
                                        : editor_selection_scope_t{},
                            invert),
        context.environment.document.notifications,
        all_objects ? "All selectable chart objects selected."
        : invert    ? "Playable-note selection inverted."
                    : "All playable notes selected.",
        notification_severity_t::success, notification_source_t::edit);
}

// Audio-time markers need the chart's timing index. A failed build is reported
// once and navigation falls back to beat-anchored markers only.
auto marker_timing_index(const wormedit::chart_document_t& chart,
                         notification_sink_t& sink) -> std::optional<wormedit::timing_index_t> {
    if (!wormedit::sys::has_audio_marker_navigation(chart)) return std::nullopt;
    auto built = wormedit::build_timing_index(chart);
    if (built) return std::move(built.index);
    post_command_message(sink, notification_severity_t::warning, notification_source_t::input,
        "Audio-time bookmarks are skipped because the chart timing could not be built.");
    return std::nullopt;
}

auto navigate_active_markers(command_context_t& context,
                             const wormedit::editor_interaction_state_t& state,
                             wormedit::marker_navigation_direction_t direction)
    -> wormedit::marker_navigation_outcome_t {
    using namespace wormedit;
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    const auto timing = chart
                            ? marker_timing_index(*chart, context.environment.document.notifications)
                            : std::nullopt;
    return marker_navigation_match_t::from(context.environment.document.history.document,
                            context.environment.document.history.document.revision, state.chart,
                            state.cursor, direction, {}, {}, timing ? &*timing : nullptr);
}

void post_marker_warnings(notification_sink_t& sink,
                          const wormedit::diagnostic_list_t& diagnostics) {
    for (const auto& diagnostic : diagnostics)
        post_command_message(sink, to_notification_severity(diagnostic.severity),
                             notification_source_t::input, diagnostic.message);
}

auto navigation_commands(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (kind == command_kind_t::navigation_previous_snap ||
        kind == command_kind_t::navigation_next_snap) {
        if (!state)
            return reject_application_command(context.environment.document.notifications,
                                              "Snap navigation requires the active Compose chart.",
                                              notification_severity_t::warning,
                                              notification_source_t::input);
        const auto direction = kind == command_kind_t::navigation_previous_snap
            ? interaction_direction_t::backward : interaction_direction_t::forward;
        const auto result = dispatch_editor_action(context.environment.document.history, *state,
                                                   cycle_edit_snap_action_t{direction});
        if (result.accepted) {
            remember_snap_denominator(context.environment.editor.commands,
                                      state->chart, state->active_snap_denominator);
            follow_snap_with_quantize_grid(context.environment.editor.commands,
                                           context.environment.document.history, *state);
        }
        return accept_editor_command(result, context.environment.document.notifications,
                                     direction == interaction_direction_t::backward
                                         ? "Previous snap selected."
                                         : "Next snap selected.",
                                     notification_severity_t::hint, notification_source_t::input);
    }
    if (kind != command_kind_t::navigation_previous_marker &&
        kind != command_kind_t::navigation_next_marker) return std::nullopt;
    if (!state)
        return reject_application_command(context.environment.document.notifications,
                                          "Bookmark navigation requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    const auto direction = kind == command_kind_t::navigation_previous_marker
        ? marker_navigation_direction_t::previous : marker_navigation_direction_t::next;
    const auto marker = navigate_active_markers(context, *state, direction);
    if (marker.value.status == marker_navigation_status_t::no_result) {
        post_command_message(context.environment.document.notifications,
                             notification_severity_t::info, notification_source_t::input,
                             direction == marker_navigation_direction_t::previous
                                 ? "No previous beat-anchored bookmark or section boundary."
                                 : "No next beat-anchored bookmark or section boundary.");
        return command_result_t{.accepted = true};
    }
    if (marker.value.status != marker_navigation_status_t::found || !marker.value.item ||
        !marker.value.item->beat_position)
        return reject_application_command(
            context.environment.document.notifications,
            marker.diagnostics.empty() ? "Bookmark navigation could not resolve an exact beat row."
                                       : marker.diagnostics.front().message,
            marker.diagnostics.empty()
                ? notification_severity_t::error
                : to_notification_severity(marker.diagnostics.front().severity),
            notification_source_t::input, diagnostic_code_t::invalid_id);
    post_marker_warnings(context.environment.document.notifications, marker.diagnostics);
    return accept_editor_command(
        dispatch_editor_action(
            context.environment.document.history, *state,
            jump_edit_cursor_action_t{*marker.value.item->beat_position, marker.value.document_revision}),
        context.environment.document.notifications,
        direction == marker_navigation_direction_t::previous
            ? "Moved to the previous bookmark or section boundary."
            : "Moved to the next bookmark or section boundary.",
        notification_severity_t::hint, notification_source_t::input);
}

auto open_bookmark_chooser_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::navigation_go_to_bookmark) return std::nullopt;
    auto* interaction = context.compose();
    if (!interaction || !context.environment.editor.commands.bookmarks.has_chooser_backend())
        return reject_application_command(context.environment.document.notifications,
                                          "Bookmark chooser requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    if (editor_interaction_has_pending_gesture(*interaction))
        return reject_application_command(context.environment.document.notifications,
                                          "Finish or cancel the active Compose gesture first.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    const bool opened = context.environment.editor.commands.bookmarks.open_chooser(
        context.environment.document.history.document.id.value, interaction->chart.value,
        context.environment.document.history.document.revision);
    if (!opened)
        return reject_application_command(
            context.environment.document.notifications,
            "Bookmark chooser is unavailable while another editor owns input.",
            notification_severity_t::warning, notification_source_t::input);
    return command_result_t{.accepted = true};
}

auto open_sound_picker_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::navigation_go_to_sound) return std::nullopt;
    auto* interaction = context.compose();
    if (!interaction || !context.environment.editor.commands.services.open_sound_picker)
        return reject_application_command(context.environment.document.notifications,
                                          "Sound picker requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    if (editor_interaction_has_pending_gesture(*interaction) ||
        context.environment.editor.commands.project.clipboard.paste_session)
        return reject_application_command(context.environment.document.notifications,
                                          "Finish or cancel the active Compose gesture first.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    const bool opened = context.environment.editor.commands.services.open_sound_picker(
        context.environment.document.history.document.id.value, interaction->chart.value,
        context.environment.document.history.document.revision);
    if (!opened)
        return reject_application_command(
            context.environment.document.notifications,
            "Sound picker is unavailable while another editor owns input.",
            notification_severity_t::warning, notification_source_t::input);
    return command_result_t{.accepted = true};
}

auto compose_quantize_command(command_context_t& context) -> std::optional<command_result_t> {
    const auto kind = context.command.kind;
    if (kind != command_kind_t::edit_toggle_quantize_preview &&
        kind != command_kind_t::edit_apply_quantize) return std::nullopt;
    auto* state = context.compose();
    if (!state || !can_apply_compose_quantize(context.environment.editor.commands, *state))
        return reject_application_command(
            context.environment.document.notifications, compose_quantize_idle_message,
            notification_severity_t::warning, notification_source_t::input);
    const auto toggle = kind == command_kind_t::edit_toggle_quantize_preview;
    auto outcome = invoke_compose_quantize_command(toggle, context.environment.document.history,
                                                   *state, context.environment.editor.commands);
    if (toggle)
        context.environment.document.notifications.post_transient_hint(
            notification_source_t::edit,
            context.environment.editor.commands.project.quantize.quantize_editor->preview_enabled
                ? "Quantize preview enabled."
                : "Quantize preview disabled.");
    if (toggle)
        return command_result_t{.accepted = outcome.accepted};
    auto finished =
        finish_compose_action(context.environment.document,
                              context.environment.document.notifications, std::move(outcome));
    if (!finished.accepted && finished.mutated)
        return command_result_t{.accepted = false,
                                .diagnostics = {{.severity = diagnostic_severity_t::error,
                                                 .code = diagnostic_code_t::invalid_id,
                                                 .message = std::move(finished.message)}}};
    return command_result_t{.accepted = finished.accepted};
}

auto project_export_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::document_export_output_group) return std::nullopt;
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart || !context.environment.editor.commands.services.open_project_export)
        return reject_application_command(context.environment.document.notifications,
                                          "Export requires a selected project difficulty.",
                                          notification_severity_t::warning,
                                          notification_source_t::export_);
    if ((context.environment.editor.retained &&
         editor_interaction_has_pending_gesture(
             *context.environment.editor.retained)) ||
        context.environment.editor.commands.project.clipboard.paste_session)
        return reject_application_command(
            context.environment.document.notifications,
            "Finish or cancel the active editor gesture before exporting.",
            notification_severity_t::warning, notification_source_t::export_);
    const bool opened = context.environment.editor.commands.services.open_project_export(
        context.environment.document.history.document.id.value, chart->id.value,
        context.environment.document.history.document.revision);
    if (!opened)
        return reject_application_command(context.environment.document.notifications,
                                          "Export is unavailable while another window owns input.",
                                          notification_severity_t::warning,
                                          notification_source_t::export_);
    return command_result_t{.accepted = true};
}

auto compose_auxiliary_command(command_context_t& context) -> std::optional<command_result_t> {
    if (auto result = project_export_command(context)) return result;
    if (auto result = compose_quantize_command(context)) return result;
    if (auto result = open_sound_picker_command(context)) return result;
    WormUiComposeDockCommand dock;
    switch (context.command.kind) {
    case command_kind_t::view_open_layers: dock = WORM_UI_COMPOSE_DOCK_OPEN_LAYERS; break;
    case command_kind_t::semantic_layer_create: dock = WORM_UI_COMPOSE_DOCK_CREATE_LAYER; break;
    case command_kind_t::view_toggle_sounds: dock = WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS; break;
    default: return std::nullopt;
    }
    auto* interaction = context.compose();
    if (!interaction || context.environment.editor.commands.project.compose_view.compose_timing_focus ||
        !context.environment.editor.commands.services.compose_dock_command)
        return reject_application_command(context.environment.document.notifications,
                                          "Dock command requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    if (editor_interaction_has_pending_gesture(*interaction) ||
        context.environment.editor.commands.project.clipboard.paste_session)
        return reject_application_command(context.environment.document.notifications,
                                          "Finish or cancel the active Compose gesture first.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    const bool accepted = context.environment.editor.commands.services.compose_dock_command(
        dock, context.environment.document.history.document.id.value, interaction->chart.value,
        context.environment.document.history.document.revision);
    if (!accepted)
        return reject_application_command(
            context.environment.document.notifications,
            "Dock command is unavailable while another editor owns input.",
            notification_severity_t::warning, notification_source_t::input);
    return command_result_t{.accepted = true};
}

auto run_bookmark_command(application_command_context_t& context, editor_interaction_state_t& state,
                          const chart_document_t& chart, bool previous)
    -> compose_action_outcome_t {
    if (context.command.kind == command_kind_t::markers_add_bookmark)
        return context.environment.editor.commands.bookmarks.add_beat_bookmark_here(
            context.environment.document.history, state, context.environment.editor.commands);
    const auto downscroll = compose_field_downscroll(
        chart, context.environment.editor.commands.preferences.editor_preferences);
    // Commands are bound to screen directions; a flipped field reverses the jump.
    const auto direction = compose_visual_navigation_direction(downscroll, previous);
    return context.environment.editor.commands.bookmarks.navigate_beat_bookmark(
        context.environment.document.history, state, context.environment.editor.commands,
        direction == interaction_direction_t::backward ? marker_navigation_direction_t::previous
                                                       : marker_navigation_direction_t::next);
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
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!state || !chart)
        return reject_application_command(context.environment.document.notifications,
                                          "Bookmark commands require the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::input);
    if (kind == command_kind_t::markers_add_bookmark &&
        (editor_interaction_has_pending_gesture(*state) ||
         context.environment.editor.commands.project.clipboard.paste_session))
        return reject_application_command(
            context.environment.document.notifications,
            "Finish or cancel the active Compose gesture before editing bookmarks.",
            notification_severity_t::warning, notification_source_t::edit);
    const auto previous_revision = context.environment.document.history.document.revision;
    const auto bookmark_chart = state->chart;
    auto outcome = run_bookmark_command(context, *state, *chart, previous);
    if (!outcome.accepted && !outcome.diagnostics.empty())
        outcome.delivery = compose_feedback_delivery_t::diagnostic_message;
    outcome = finish_compose_action(context.environment.document,
                                    context.environment.document.notifications, std::move(outcome));
    if (outcome.mutated && !outcome.accepted) {
        // Registered commands retain the old rebind rejection code and one diagnostic.
        outcome.diagnostics.resize(1);
        outcome.diagnostics.front() = {.severity = diagnostic_severity_t::error,
                                       .code = diagnostic_code_t::invalid_id,
                                       .message = std::move(outcome.message)};
    }
    if (outcome.mutated && outcome.accepted && kind == command_kind_t::markers_add_bookmark)
        static_cast<void>(context.environment.editor.commands.bookmarks.preserve_preview_revision(
            bookmark_chart, previous_revision,
            context.environment.document.history.document.revision));
    return command_result_t{.accepted = outcome.accepted,
                            .diagnostics = std::move(outcome.diagnostics)};
}

auto workspace_tab_command(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    std::optional<workspace_tab_t> tab;
    switch (context.command.kind) {
    case command_kind_t::timing_open_settings:
        context.environment.editor.commands.preferences.timing_settings_visible =
            !context.environment.editor.commands.preferences.timing_settings_visible;
        return command_result_t{.accepted = true};
    case command_kind_t::workspace_open_setup: tab = workspace_tab_t::setup; break;
    case command_kind_t::workspace_open_metadata: tab = workspace_tab_t::metadata; break;
    case command_kind_t::workspace_open_timing:
        // The Timing screen is retired; its command opens Compose in timing focus.
        tab = workspace_tab_t::compose;
        context.environment.editor.commands.project.compose_view.compose_timing_focus = true;
        break;
    case command_kind_t::workspace_open_compose: tab = workspace_tab_t::compose; break;
    case command_kind_t::workspace_open_storyboard: tab = workspace_tab_t::storyboard; break;
    case command_kind_t::workspace_open_audio_slices: tab = workspace_tab_t::audio_slices; break;
    case command_kind_t::workspace_open_compare_validate: tab = workspace_tab_t::compare_validate; break;
    default: return std::nullopt;
    }
    const auto applied = apply_workspace_action(context.environment.document.workspace,
                                                context.environment.document.history.document,
                                                workspace_switch_tab_action_t{*tab});
    post_command_message(context.environment.document.notifications,
                         applied.applied ? notification_severity_t::info
                         : applied.diagnostics.empty()
                             ? notification_severity_t::warning
                             : to_notification_severity(applied.diagnostics.front().severity),
                         notification_source_t::input,
                         applied.applied               ? "Workspace tab changed."
                         : applied.diagnostics.empty() ? "Workspace tab change was rejected."
                                                       : applied.diagnostics.front().message);
    return command_result_t{.accepted = applied.applied, .diagnostics = applied.diagnostics};
}

auto timing_lock_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::timing_toggle_lock) return std::nullopt;
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    if (!(context.environment.document.workspace.active_tab == workspace_tab_t::compose &&
          app.project.compose_view.compose_timing_focus))
        return reject_application_command(context.environment.document.notifications,
                                          "Timing Lock requires Compose with Timing focus.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    app.project.timing_recipe.timing_locked = !app.project.timing_recipe.timing_locked;
    if (app.project.timing_recipe.timing_locked && context.environment.editor.timing_stage) {
        cancel_timing_lane_pseudonote(*context.environment.editor.timing_stage);
        context.environment.editor.timing_stage.reset();
    }
    post_command_message(context.environment.document.notifications,
                         notification_severity_t::success, notification_source_t::edit,
                         app.project.timing_recipe.timing_locked ? "Timing edits locked." : "Timing edits unlocked.");
    return command_result_t{.accepted = true};
}

auto effective_scroll_multiplier_at(const wormedit::chart_document_t& chart,
                                    wormedit::local_position_t position)
    -> std::optional<wormedit::signed_rational_t> {
    using namespace wormedit;
    const auto measure_result = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&measure_result);
    const auto row = measures ? absolute_row(*measures, position) : std::nullopt;
    if (!row || *row > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const auto timing = build_timing_index(chart);
    if (!timing) return std::nullopt;
    const auto scroll = build_scroll_speed_index(chart, *timing.index);
    if (!scroll) return std::nullopt;
    const auto value = scroll_speed_at(*scroll.index, *timing.index,
                                       {static_cast<std::int64_t>(*row), 1});
    return value && value.state ? std::optional{value.state->scroll_multiplier} : std::nullopt;
}

auto timing_insert_command(command_context_t& context) -> std::optional<command_result_t> {
    using namespace wormedit;
    timing_lane_kind_t lane{};
    if (context.command.kind == command_kind_t::timing_insert_bpm)
        lane = timing_lane_kind_t::bpm;
    else if (context.command.kind == command_kind_t::timing_insert_sv)
        lane = timing_lane_kind_t::scroll;
    else
        return std::nullopt;

    if (context.environment.editor.commands.project.timing_recipe.timing_locked)
        return reject_application_command(
            context.environment.document.notifications, "Timing edits are locked.",
            notification_severity_t::warning, notification_source_t::edit);
    if (context.environment.editor.timing_stage)
        return reject_application_command(
            context.environment.document.notifications,
            "Confirm or cancel the open timing edit before starting another.",
            notification_severity_t::warning, notification_source_t::edit);
    auto* state = context.compose();
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!state || !chart)
        return reject_application_command(context.environment.document.notifications,
                                          "Timing insertion requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    if (state->virtual_bms_cursor_row)
        return reject_application_command(
            context.environment.document.notifications,
            "Timing insertion is unavailable at a virtual BMS cursor row.",
            notification_severity_t::warning, notification_source_t::edit);
    if (editor_interaction_has_pending_gesture(*state))
        return reject_application_command(
            context.environment.document.notifications,
            "Finish or cancel the active Compose gesture before inserting timing.",
            notification_severity_t::warning, notification_source_t::edit);

    timing_lane_value_t initial;
    if (lane == timing_lane_kind_t::bpm) {
        const auto bpm = effective_bpm_at(*chart, state->cursor);
        if (!bpm)
            return reject_application_command(
                context.environment.document.notifications,
                "The effective BPM could not be resolved at the cursor.",
                notification_severity_t::warning, notification_source_t::edit);
        initial = timing_bpm_value_t{*bpm};
    } else {
        const auto multiplier = effective_scroll_multiplier_at(*chart, state->cursor);
        if (!multiplier)
            return reject_application_command(
                context.environment.document.notifications,
                "The effective SV value could not be resolved at the cursor.",
                notification_severity_t::warning, notification_source_t::edit);
        initial = timing_scroll_value_t{*multiplier};
    }
    auto preview = begin_timing_lane_pseudonote(
        context.environment.document.history, chart->id, lane, state->cursor, std::move(initial),
        timing_lane_initial_value_source_t::effective_at_position,
        context.environment.editor.commands.project.timing_recipe.timing_locked);
    if (preview.status != timing_lane_pseudonote_status_t::editing) {
        const auto message = preview.diagnostics.empty()
                                 ? "Timing event could not be staged at the Compose cursor."
                                 : preview.diagnostics.front().message;
        return reject_application_command(
            context.environment.document.notifications, message,
            preview.diagnostics.empty()
                ? notification_severity_t::warning
                : to_notification_severity(preview.diagnostics.front().severity),
            notification_source_t::edit);
    }
    context.environment.editor.timing_stage = std::move(preview);
    post_command_message(context.environment.document.notifications, notification_severity_t::hint,
                         notification_source_t::edit, "Timing event staged; confirm to insert it.");
    return command_result_t{.accepted = true};
}

auto select_tool_command(command_context_t& context) -> std::optional<command_result_t> {
    const auto kind = context.command.kind;
    if (kind != command_kind_t::edit_tool_select && kind != command_kind_t::edit_tool_draw &&
        kind != command_kind_t::edit_tool_layer) return std::nullopt;
    if (kind != command_kind_t::edit_tool_select &&
        !wormedit::ui::compose_notes_editable(
            compose_mode_of(context.environment.editor.commands)))
        return reject_application_command(context.environment.document.notifications,
                                          "Draw and Layer are unavailable in Timing focus.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    auto* state = context.compose();
    if (!state)
        return reject_application_command(
            context.environment.document.notifications, "Select requires the active Compose chart.",
            notification_severity_t::warning, notification_source_t::edit);
    if (kind != command_kind_t::edit_tool_select &&
        !std::holds_alternative<wormedit::editor_interaction_view_model_t>(
            wormedit::inspect_editor_interaction(context.environment.document.history, *state)))
        return reject_application_command(context.environment.document.notifications,
                                          "Draw and Layer require editable chart geometry.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    if (kind == command_kind_t::edit_tool_layer &&
        !compose_layer_brush_target_editable(
            context.environment.document.history.document, state->chart,
            context.environment.editor.commands.project.semantic_layer.semantic_layer_target))
        return reject_application_command(context.environment.document.notifications,
                                          "Pick an unlocked visible layer in the Layers dock.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    if (editor_interaction_has_pending_gesture(*state)) {
        const auto canceled = wormedit::dispatch_editor_action(
            context.environment.document.history, *state, wormedit::cancel_editor_gesture_action_t{});
        if (!canceled.accepted)
            return accept_editor_command(canceled, context.environment.document.notifications,
                                         "Gesture canceled.", notification_severity_t::warning,
                                         notification_source_t::edit);
    }
    context.environment.editor.commands.project.compose_gesture.bga_draw_stroke.reset();
    context.environment.editor.commands.project.compose_gesture.note_paint_mode_active =
        kind == command_kind_t::edit_tool_draw;
    context.environment.editor.commands.project.compose_gesture.semantic_layer_paint_mode_active =
        kind == command_kind_t::edit_tool_layer;
    post_command_message(context.environment.document.notifications, notification_severity_t::hint,
                         notification_source_t::edit, "Compose tool selected.");
    return command_result_t{.accepted = true};
}

auto note_kind_command(command_context_t& context) -> std::optional<command_result_t> {
    if (context.command.kind != command_kind_t::edit_note_kind) return std::nullopt;
    auto* state = context.compose();
    if (!state || !context.command.note_kind)
        return reject_application_command(context.environment.document.notifications,
                                          "Note kind requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    const auto result =
        wormedit::arm_note_kind(context.environment.document.history, *state,
                                static_cast<wormedit::note_kind_t>(*context.command.note_kind));
    if (result.accepted) {
        context.environment.editor.commands.project.compose_gesture.note_paint_mode_active = true;
        context.environment.editor.commands.project.compose_gesture.semantic_layer_paint_mode_active = false;
    }
    return accept_editor_command(result, context.environment.document.notifications,
                                 "Note paint tool armed.", notification_severity_t::hint,
                                 notification_source_t::edit);
}

auto semantic_layer_slot_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!state || !chart)
        return reject_application_command(context.environment.document.notifications,
                                          "Layer selection requires the active Compose chart.",
                                          notification_severity_t::warning,
                                          notification_source_t::edit);
    const auto slot = context.command.semantic_layer_slot.value_or(0);
    if (!slot)
        return reject_application_command(
            context.environment.document.notifications, "That semantic-layer slot is unavailable.",
            notification_severity_t::warning, notification_source_t::edit);
    auto& app = context.environment.editor.commands;
    if (app.project.semantic_layer.semantic_layer_palette_chart != chart->id) {
        app.project.semantic_layer.semantic_layer_palette_chart = chart->id;
        app.project.semantic_layer.semantic_layer_palette_page = 0;
    }
    const auto palette = make_semantic_layer_palette_window(
        *chart, app.project.semantic_layer.semantic_layer_target, app.project.semantic_layer.semantic_layer_palette_page, false,
        WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
    if (!palette || slot > palette->layer_indices.size())
        return reject_application_command(
            context.environment.document.notifications, "That semantic-layer slot is unavailable.",
            notification_severity_t::warning, notification_source_t::edit);
    const auto* layer = &chart->semantic_layers[palette->layer_indices[slot - 1]];
    app.project.semantic_layer.semantic_layer_target = layer->id;
    app.project.semantic_layer.semantic_layer_target_none = false;
    post_command_message(context.environment.document.notifications, notification_severity_t::info,
                         notification_source_t::edit, "Semantic-layer target: " + layer->name);
    return {.accepted = true};
}

auto semantic_layer_membership_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    auto& app = context.environment.editor.commands;
    if (!state || (!app.project.semantic_layer.semantic_layer_target && !app.project.semantic_layer.semantic_layer_target_none) || state->selected_notes.empty())
        return reject_application_command(
            context.environment.document.notifications,
            "Choose a semantic layer and select playable notes first.",
            notification_severity_t::warning, notification_source_t::edit);
    semantic_layer_edit_action_t edit;
    const auto kind = context.command.kind;
    if (app.project.semantic_layer.semantic_layer_target_none && kind != command_kind_t::semantic_layer_apply)
        return reject_application_command(context.environment.document.notifications,
                                          "No layer only assigns", notification_severity_t::warning,
                                          notification_source_t::edit);
    if (app.project.semantic_layer.semantic_layer_target_none)
        edit = clear_semantic_layer_membership_action_t{state->chart, state->selected_notes};
    else if (kind == command_kind_t::semantic_layer_apply)
        edit = replace_notes_semantic_layer_action_t{
            state->chart, *app.project.semantic_layer.semantic_layer_target, state->selected_notes};
    else if (kind == command_kind_t::semantic_layer_remove)
        edit =
            semantic_layer_membership_edit_t{.chart = state->chart,
                                             .layer = *app.project.semantic_layer.semantic_layer_target,
                                             .notes = state->selected_notes,
                                             .operation = wormedit::membership_operation_t::remove};
    else
        edit = replace_notes_semantic_layer_action_t{
            state->chart, *app.project.semantic_layer.semantic_layer_target, state->selected_notes, true};
    const auto applied = apply_semantic_layer_edit(context.environment.document.history, edit);
    if (applied.applied) {
        const auto rebound = context.rebind();
        if (!rebound.accepted) return rebound;
    }
    if (applied.applied || applied.diagnostics.empty())
        post_command_message(context.environment.document.notifications,
                             applied.applied ? notification_severity_t::success
                                             : notification_severity_t::info,
                             notification_source_t::edit,
                             applied.applied ? "Semantic-layer membership updated."
                                             : "Semantic-layer membership was unchanged.");
    else
        post_command_message(context.environment.document.notifications,
                             to_notification_severity(applied.diagnostics.front().severity),
                             notification_source_t::edit, applied.diagnostics.front().message);
    return {.accepted = applied.applied || applied.diagnostics.empty(),
            .diagnostics = applied.diagnostics};
}

auto invoke_application_command(application_command_t command,
                                const command_dispatch_inputs_t& environment)
    -> wormedit::command_invocation_result_t {
    auto& application_state = environment.editor.commands;
    auto& sink = environment.document.notifications;

    command_context_t context{environment, command};
    const auto kind = command.kind;
    if (auto result = history_command(context)) return *result;
    if (auto result = select_tool_command(context)) return *result;
    if (auto result = note_kind_command(context)) return *result;
    if (auto result = measure_command(context)) return *result;
    if (auto result = loop_selection_command(context)) return *result;
    if (auto result = transport_command(context)) return *result;
    if (auto result = external_player_command(context)) return *result;
    if (auto result = note_to_bgm_command(context)) return *result;
    if (auto result = edit_commands(context)) return *result;
    if (auto result = selection_commands(context)) return *result;
    if (auto result = navigation_commands(context)) return *result;
    if (auto result = open_bookmark_chooser_command(context)) return *result;
    if (auto result = compose_auxiliary_command(context)) return *result;
    if (auto result = bookmark_commands(context)) return *result;
    if (auto result = workspace_tab_command(context)) return *result;
    if (auto result = timing_insert_command(context)) return *result;
    if (auto result = timing_lock_command(context)) return *result;
    if (kind == command_kind_t::semantic_layer_select_slot)
        return semantic_layer_slot_command(context);
    if (kind == command_kind_t::semantic_layer_clear_target) {
        application_state.project.semantic_layer.semantic_layer_target.reset();
        application_state.project.semantic_layer.semantic_layer_target_none = false;
        application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
        application_state.project.semantic_layer.semantic_layer_solo_active = false;
        post_command_message(sink, notification_severity_t::info, notification_source_t::edit,
                             "Semantic-layer target cleared.");
        return {.accepted = true};
    }
    if (kind == command_kind_t::semantic_layer_apply ||
        kind == command_kind_t::semantic_layer_remove ||
        kind == command_kind_t::semantic_layer_toggle)
        return semantic_layer_membership_command(context);
    return reject_application_command(sink, "Command is not implemented by the application.",
        notification_severity_t::warning, notification_source_t::system);
}

} // namespace wormedit::sys
