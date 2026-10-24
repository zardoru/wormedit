#include "sys/compose/views/compose_interaction_view.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/app/application_edit_inputs.h"
#include "sys/compose/actions/compose_actions.h"
#include "sys/compose/actions/compose_area_actions.h"
#include "sys/compose/actions/compose_measure_commands.h"
#include "sys/compose/actions/compose_note_to_bgm.h"
#include "sys/compose/actions/compose_quantize.h"
#include "sys/compose/actions/compose_quantize_request.h"
#include "sys/compose/compose_semantic_layer_commands.h"

#include "sys/bookmarks/bookmark_commands.h"
#include "sys/compose/input/compose_input.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "sys/compose/compose_sound_selection.h"
#include "sys/compose/views/compose_view.h"
#include "sys/preferences/external_player.h"
#include "sys/audio/transport_player_actions.h"
#include "ui/frontend.h"
#include <wormedit/core/chart_clipboard.h>

#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <limits>
#include <string>
#include <array>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wormedit::sys {
void reset_application_project_edit_state(application_command_state_t& current) {
    current.bookmarks.reset_project();
    current.project = {};
}

auto application_command_disabled_by_timing_focus(application_command_kind_t kind) noexcept
    -> bool {
    using kind_t = application_command_kind_t;
    switch (kind) {
    case kind_t::audio_paint_hitsound:
    case kind_t::edit_tool_draw:
    case kind_t::edit_tool_layer:
    case kind_t::edit_toggle_quantize_preview:
    case kind_t::edit_apply_quantize:
    case kind_t::view_open_layers:
    case kind_t::semantic_layer_create:
    case kind_t::view_toggle_sounds:
        return true;
    default:
        return false;
    }
}

void apply_timing_focus_command_policy(command_registry_t& registry,
                                       bool compose_notes_editable) {
    if (compose_notes_editable) return;
    for (const auto& command : registry.commands) {
        const auto parsed = parse_application_command(command.definition.id.value);
        if (!parsed || !application_command_disabled_by_timing_focus(parsed->kind)) continue;
        (void)update_command_state(registry, command.definition.id, false,
                                   command.definition.checked);
    }
}

namespace {

auto timing_focus_command_refusal(application_command_t command,
                                  const application_command_state_t& state)
    -> std::optional<std::string_view> {
    if (wormedit::ui::compose_notes_editable(compose_mode_of(state)) ||
        !application_command_disabled_by_timing_focus(command.kind))
        return std::nullopt;
    return "Timing focus is on: press F4 to edit notes.";
}

void remember_snap_denominator(application_command_state_t& state, wormedit::chart_id_t chart,
                               std::uint64_t denominator) {
    if (chart.value != 0 && denominator != 0) {
        state.project.compose_view.requested_snap_denominators.insert_or_assign(chart.value,
                                                                                denominator);
    }
}

// One shared grid: the Quantize target follows the snap, keeping its other settings.
void follow_snap_with_quantize_grid(application_command_state_t& application,
                                    const wormedit::document_history_t& history,
                                    const wormedit::editor_interaction_state_t& state) {
    auto& editor = application.project.quantize.quantize_editor;
    if (!editor || !editor->stamp.is_identity_current(history.document, state.chart) ||
        (editor->request.divisions == state.active_snap_denominator &&
         editor->request.space == state.active_snap_space))
        return;
    editor->request.divisions = state.active_snap_denominator;
    editor->request.space = state.active_snap_space;
    editor->preset_index.reset();
}

} // namespace

auto unavailable_application_command_message(const command_registry_t& registry,
    const std::optional<command_id_t>& id, const application_command_state_t& state) -> std::string {
    const auto* command = id ? find_command(registry, *id) : nullptr;
    if (!command) return "Command is currently unavailable.";
    if (const auto parsed = parse_application_command(command->definition.id.value)) {
        if (const auto reason = timing_focus_command_refusal(*parsed, state))
            return std::string{*reason};
    }
    return "Command is currently unavailable: " + command->definition.display_name;
}

auto reject_application_command(notification_sink_t& sink, std::string message,
                                notification_severity_t severity, notification_source_t source,
                                diagnostic_code_t code) -> dispatch_outcome_t {
    return finish_application_command(
        sink, compose_action_outcome_t::from(std::move(message), severity, source, code));
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
};

using command_result_t = compose_action_outcome_t;
using command_context_t = application_command_context_t;
using command_kind_t = wormedit::sys::application_command_kind_t;

auto clear_selection_or_cancel_preview(command_context_t& context) -> compose_action_outcome_t {
    auto& history = context.environment.document.history;
    auto* state = context.compose();
    auto& application_state = context.environment.editor.commands;

    if (application_state.project.clipboard.paste_session) {
        application_state.project.clipboard.paste_session.reset();
        auto outcome = compose_action_outcome_t::from(
            "Paste preview cancelled.", notification_severity_t::hint, notification_source_t::edit,
            compose_message_lifetime_t::borrowed);
        outcome.delivery = compose_feedback_delivery_t::notification;
        return outcome;
    }
    if (application_state.services.transport_inline_active &&
        application_state.services.transport_inline_active() &&
        application_state.services.transport_cancel_restore) {
        static_cast<void>(application_state.services.transport_cancel_restore());
        return {.delivery = compose_feedback_delivery_t::none};
    }
    if (!state) {
        return compose_action_outcome_t::from(
            "Clear Selection requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    }
    if (state->is_pending())
        return compose_action_outcome_t::from(
            wormedit::dispatch_editor_action(history, *state, wormedit::cancel_editor_gesture_action_t{}),
            "Compose gesture cancelled; selection kept.",
            notification_severity_t::success, notification_source_t::edit);
    (void)wormedit::dispatch_editor_action(history, *state,
                                           wormedit::cancel_editor_gesture_action_t{});
    return compose_action_outcome_t::from(
        wormedit::dispatch_editor_action(history, *state, wormedit::select_editor_notes_action_t{}),
        "Selection and pending Compose gesture cleared.",
        notification_severity_t::success, notification_source_t::edit);
}

auto history_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    const auto kind = context.command.kind;
    const bool undo = kind == command_kind_t::history_undo;
    auto& history = context.environment.document.history;
    const auto applied = context.environment.editor.retained
        ? apply_editor_history(history, *context.environment.editor.retained, undo)
        : editor_history_result_t{.changed = undo ? undo_document_command(history) : redo_document_command(history),
                                 .document_changed = true};
    const auto changed = applied.changed;
    if (changed && context.environment.editor.retained)
        context.environment.document.workspace.selected_chart = context.environment.editor.retained->chart;
    auto outcome = compose_action_outcome_t::from(
        changed ? (undo ? "Undo applied." : "Redo applied.")
                : (undo ? "Nothing to undo." : "Nothing to redo."),
        changed ? notification_severity_t::success : notification_severity_t::info,
        notification_source_t::edit, compose_message_lifetime_t::borrowed);
    outcome.mutated = applied.document_changed && changed;
    if (!changed)
        return outcome;
    context.environment.editor.timing_stage.reset();
    return outcome;
}

auto external_player_command(command_context_t& context) -> command_result_t {
    external_player_action_t action;
    switch (context.command.kind) {
    case command_kind_t::transport_external_play_start:
        action = external_player_action_t::play_start;
        break;
    case command_kind_t::transport_external_play_here:
        action = external_player_action_t::play_here;
        break;
    case command_kind_t::transport_external_stop:
        action = external_player_action_t::stop;
        break;
    default:
        std::unreachable();
    }
    if (!context.environment.editor.commands.services.external_player)
        return compose_action_outcome_t::from(
            "External player is unavailable.", notification_severity_t::warning,
            notification_source_t::audio, diagnostic_code_t::unsupported_feature);
    static_cast<void>(context.environment.editor.commands.services.external_player(action));
    return command_result_t{.delivery = compose_feedback_delivery_t::none};
}

auto metronome_command(command_context_t& context) -> command_result_t {
    if (!toggle_transport_metronome(context.environment.editor.commands))
        return compose_action_outcome_t::from(
            "Metronome is unavailable.", notification_severity_t::warning,
            notification_source_t::audio, diagnostic_code_t::unsupported_feature);
    return compose_action_outcome_t::from(
        context.environment.editor.commands.metronome_settings().enabled ? "Metronome enabled."
                                                                         : "Metronome disabled.",
        notification_severity_t::success, notification_source_t::audio,
        compose_message_lifetime_t::borrowed);
}

auto cursor_measure_operation(command_kind_t kind) -> std::optional<cursor_measure_command_t> {
    switch (kind) {
    case command_kind_t::measures_insert_before:
        return cursor_measure_command_t::insert_before;
    case command_kind_t::measures_insert_after:
        return cursor_measure_command_t::insert_after;
    case command_kind_t::measures_remove_empty:
        return cursor_measure_command_t::remove_empty;
    default:
        return std::nullopt;
    }
}

auto measure_command(command_context_t& context) -> command_result_t {
    const auto operation = cursor_measure_operation(context.command.kind);
    auto* state = context.compose();
    if (!state)
        return compose_action_outcome_t::from(
            "Measure editing requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (const auto error = compose_measure_edit_rejection(
            *state,
            context.environment.editor.commands.project.clipboard.paste_session.has_value()))
        return compose_action_outcome_t::from(*error, notification_severity_t::warning,
                                              notification_source_t::edit,
                                              diagnostic_code_t::unsupported_feature);
    auto severity = notification_severity_t::warning;
    auto message = operation ? apply_cursor_measure_command(context.environment.document.history,
                                                            *state, *operation, &severity)
                             : apply_cursor_measure_split(context.environment.document.history,
                                                          *state, &severity);
    auto outcome =
        compose_action_outcome_t::from(std::move(message), severity, notification_source_t::edit);
    outcome.accepted = severity != notification_severity_t::warning;
    return outcome;
}

auto loop_selection_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    const bool toggle = context.command.kind == command_kind_t::transport_toggle_loop_selection;
    const bool disable =
        toggle && context.environment.editor.commands.services.transport_loop_selection_active &&
        context.environment.editor.commands.services.transport_loop_selection_active();
    const auto* state = context.compose();
    const auto* chart = context.environment.document.history.read_chart(
        state ? state->chart : chart_id_t{});
    if (!state || chart == nullptr || (!disable && !state->retained_selection_range) ||
        state->is_pending() ||
        context.environment.editor.commands.project.clipboard.paste_session ||
        !context.environment.editor.commands.services.transport_set_loop_selection)
        return compose_action_outcome_t::from(
            "Select a committed area to loop playback.", notification_severity_t::warning,
            notification_source_t::audio, diagnostic_code_t::unsupported_feature);
    if (disable) {
        if (!context.environment.editor.commands.services.transport_set_loop_selection(
                state->chart, context.environment.document.history.document.revision, 0, 0, false))
            return compose_action_outcome_t::from(
                "Loop selection is unavailable or expired.", notification_severity_t::warning,
                notification_source_t::audio, diagnostic_code_t::unsupported_feature);
        return compose_action_outcome_t::from(
            "Loop selection disabled.", notification_severity_t::success,
            notification_source_t::audio, compose_message_lifetime_t::borrowed);
    }
    const auto& indexed = context.environment.document.history.indices(chart->id)->measures;
    const auto* measures = indexed ? &*indexed : nullptr;
    const auto begin =
        measures ? (*measures).row_of( state->retained_selection_range->begin) : std::nullopt;
    const auto end =
        measures ? (*measures).row_of( state->retained_selection_range->end) : std::nullopt;
    if (!begin || !end || *begin >= *end ||
        !context.environment.editor.commands.services.transport_set_loop_selection(
            state->chart, context.environment.document.history.document.revision, *begin, *end,
            true))
        return compose_action_outcome_t::from(
            "Loop selection is unavailable or expired.", notification_severity_t::warning,
            notification_source_t::audio, diagnostic_code_t::unsupported_feature);
    return compose_action_outcome_t::from(
        "Loop selection enabled.", notification_severity_t::success, notification_source_t::audio,
        compose_message_lifetime_t::borrowed);
}

auto transport_command(command_context_t& context) -> command_result_t {
    const auto kind = context.command.kind;
    auto& app = context.environment.editor.commands;
    if (kind == command_kind_t::transport_rate_slower ||
        kind == command_kind_t::transport_rate_faster) {
        const auto prior_rate = app.preferences.transport_rate_index;
        auto stepped =
            step_transport_rate_status(app, kind == command_kind_t::transport_rate_faster);
        if (!stepped)
            return compose_action_outcome_t::from(
                "Playback output rejected the rate change.", notification_severity_t::warning,
                notification_source_t::audio, diagnostic_code_t::unsupported_feature);
        return compose_action_outcome_t::from(std::move(*stepped),
                                              prior_rate == app.preferences.transport_rate_index
                                                  ? notification_severity_t::info
                                                  : notification_severity_t::success,
                                              notification_source_t::audio);
    }
    if (kind == command_kind_t::transport_play_stop) {
        const bool hard_stop =
            app.services.transport_preview_playing && app.services.transport_preview_playing();
        const auto result = play_stop_transport(app);
        if (!result)
            return compose_action_outcome_t::from(
                "Chart preview is unavailable.", notification_severity_t::warning,
                notification_source_t::audio, diagnostic_code_t::unsupported_feature);
        if (hard_stop)
            return compose_action_outcome_t::from(
                "Audio preview stopped.", notification_severity_t::hint,
                notification_source_t::audio, compose_message_lifetime_t::borrowed);
        return command_result_t{.delivery = compose_feedback_delivery_t::none};
    }
    if (kind == command_kind_t::transport_play_pause) {
        if (!app.services.transport_play_pause)
            return compose_action_outcome_t::from(
                "Chart preview is unavailable.", notification_severity_t::warning,
                notification_source_t::audio, diagnostic_code_t::unsupported_feature);
        static_cast<void>(app.services.transport_play_pause());
        return command_result_t{.delivery = compose_feedback_delivery_t::none};
    }
    if (kind == command_kind_t::transport_play_from_start) {
        if (!app.services.transport_play_from_start)
            return compose_action_outcome_t::from(
                "Chart preview is unavailable.", notification_severity_t::warning,
                notification_source_t::audio, diagnostic_code_t::unsupported_feature);
        static_cast<void>(app.services.transport_play_from_start());
        return command_result_t{.delivery = compose_feedback_delivery_t::none};
    }
    if (kind == command_kind_t::transport_stop) {
        if (!app.services.transport_stop || !app.services.transport_active ||
            !app.services.transport_active())
            return compose_action_outcome_t::from(
                "No audio preview is playing.", notification_severity_t::hint,
                notification_source_t::audio, diagnostic_code_t::unsupported_feature);
        app.services.transport_stop();
        return compose_action_outcome_t::from(
            "Audio preview stopped.", notification_severity_t::hint, notification_source_t::audio,
            compose_message_lifetime_t::borrowed);
    }
    std::unreachable();
}

auto note_to_bgm_command(command_context_t& context) -> command_result_t {
    const auto kind = context.command.kind;
    auto* state = context.compose();
    const auto* found = context.environment.document.history.read_chart(
        state ? state->chart : chart_id_t{});
    const auto* chart = found == nullptr ? nullptr : &*found;
    if (!state || !chart ||
        !compose_move_to_bgm_available(*chart, *state, context.environment.editor.commands))
        return compose_action_outcome_t::from(
            "Move to BGM requires selected notes on an editable BMS/BMSON chart.",
            notification_severity_t::hint, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    const auto moved =
        move_editor_selection_to_bgm(context.environment.document.history, *state, std::nullopt,
                                     kind == command_kind_t::edit_move_next_to_bgm);
    if (!moved.payload.handled)
        return compose_action_outcome_t::from(
            "Select playable notes before moving them to BGM.", notification_severity_t::hint,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (!moved.command.applied) {
        const auto message = moved.command.diagnostics.empty()
                                 ? "The notes could not be moved to BGM."
                                 : moved.command.diagnostics.front().message;
        return compose_action_outcome_t::from(message, notification_severity_t::warning,
                                              notification_source_t::edit,
                                              diagnostic_code_t::unsupported_feature);
    }
    auto outcome = compose_action_outcome_t::from(
        "Selected notes moved to BGM.", notification_severity_t::hint, notification_source_t::edit,
        compose_message_lifetime_t::borrowed);
    outcome.mutated = true;
    return outcome;
}

auto clipboard_commands(command_context_t& context) -> command_result_t {
    const auto kind = context.command.kind;
    if (kind == command_kind_t::edit_paste) {
        const auto* state = context.compose();
        if (!state)
            return compose_action_outcome_t::from(
                "Paste requires the active Compose chart.", notification_severity_t::warning,
                notification_source_t::edit, diagnostic_code_t::unsupported_feature);
        std::string message;
        const bool accepted =
            begin_compose_paste(context.environment.editor.commands,
                                context.environment.document.history, *state, message);
        return compose_action_outcome_t::from(std::move(message),
                                              accepted ? notification_severity_t::hint
                                                       : notification_severity_t::warning,
                                              notification_source_t::edit, false, accepted);
    }
    auto* state = context.compose();
    if (!state)
        return compose_action_outcome_t::from(
            "Clipboard requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    const wormedit::selection_delete_options_t options{
        .kiai_head =
            wormedit::object_id_t{
                context.environment.editor.commands.project.timing_recipe.kiai_selected_head},
        .recipe =
            wormedit::recipe_id_t{
                context.environment.editor.commands.project.timing_recipe.recipe_selected},
        .timing_locked = context.environment.editor.commands.project.timing_recipe.timing_locked};
    if (options.recipe)
        return compose_action_outcome_t::from(
            "SV recipes cannot be copied to the chart clipboard.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    auto copied =
        kind == command_kind_t::edit_cut
            ? wormedit::create_cut_chart_clipboard(context.environment.document.history, *state,
                                                   options)
            : wormedit::chart_clipboard_t::from(
                  context.environment.document.history.document, *state,
                  context.environment.document.history.document.revision, options.kiai_head);
    if (!copied)
        return compose_action_outcome_t::from(
            std::move(copied.error()), notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (kind == command_kind_t::edit_cut)
        context.environment.editor.commands.project.timing_recipe.kiai_selected_head = 0;
    auto& commands = context.environment.editor.commands;
    commands.project.clipboard.chart_clipboard = std::move(*copied);
    commands.project.clipboard.system_text_at_copy =
        commands.services.system_clipboard_text ? commands.services.system_clipboard_text() : "";
    auto outcome = compose_action_outcome_t::from(
        kind == command_kind_t::edit_cut ? "Selection cut." : "Selection copied.",
        notification_severity_t::success, notification_source_t::edit,
        compose_message_lifetime_t::borrowed);
    return outcome;
}

auto edit_commands(command_context_t& context) -> command_result_t {
    auto* state = context.compose();
    if (!state)
        return compose_action_outcome_t::from(
            "Delete requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
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
    auto accepted = compose_action_outcome_t::from(
        result, result.document_changed ? "Selection deleted." : "No object at the cursor.",
        result.document_changed ? notification_severity_t::success : notification_severity_t::info,
        notification_source_t::edit);
    accepted.mutated = result.document_changed;
    return accepted;
}

auto same_sound_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!state || !chart)
        return compose_action_outcome_t::from(
            "Hover a note or select a sound first", notification_severity_t::hint,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    auto request = compose_same_sound_request(context.environment.document.history.document, *chart,
                                              *state, context.environment.editor.commands, false,
                                              editor_area_selection_mode_t::add);
    if (const auto* error = std::get_if<std::string>(&request))
        return compose_action_outcome_t::from(*error, notification_severity_t::hint,
                                              notification_source_t::edit,
                                              diagnostic_code_t::unsupported_feature);
    return compose_action_outcome_t::from(
        dispatch_editor_action(context.environment.document.history, *state,
                               std::get<select_editor_objects_action_t>(std::move(request))),
        "Matching sounds added to selection.", notification_severity_t::success,
        notification_source_t::edit);
}

// The scoped selection commands: which families each selects, and from where.
struct scoped_selection_command_t {
    wormedit::sys::application_command_kind_t kind;
    wormedit::editor_selection_scope_t scope;
    bool invert{};
    bool from_cursor{};
    std::string_view message;

    [[nodiscard]] static auto from(wormedit::sys::application_command_kind_t kind)
        -> const scoped_selection_command_t&;
};

// Select All reaches every kind except bar lines, which are selectable only through the measure lane.
constexpr wormedit::editor_selection_scope_t every_object_but_bar_lines{
    .notes = true, .bgm = true, .timing = true, .bga = true};
constexpr std::array scoped_selection_commands{
    scoped_selection_command_t{command_kind_t::selection_select_all, {}, false, false,
                               "All playable notes selected."},
    scoped_selection_command_t{command_kind_t::selection_select_all_objects, every_object_but_bar_lines, false, false,
                               "All selectable chart objects selected."},
    scoped_selection_command_t{command_kind_t::selection_invert, {}, true, false,
                               "Playable-note selection inverted."},
    scoped_selection_command_t{command_kind_t::selection_select_all_after_cursor, {}, false, true,
                               "Playable notes from the cursor onward selected."},
    scoped_selection_command_t{command_kind_t::selection_select_all_objects_after_cursor,
                               every_object_but_bar_lines, false, true, "Chart objects from the cursor onward selected."},
};

auto scoped_selection_command_t::from(wormedit::sys::application_command_kind_t kind)
    -> const scoped_selection_command_t& {
    const auto found = std::ranges::find(scoped_selection_commands, kind, &scoped_selection_command_t::kind);
    return found == scoped_selection_commands.end() ? scoped_selection_commands.front() : *found;
}

auto selection_commands(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    const auto kind = context.command.kind;
    auto* state = context.compose();
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!state || !chart)
        return compose_action_outcome_t::from(
            "Selection command requires the active Compose chart.",
            notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    const auto& selection = scoped_selection_command_t::from(kind);
    return compose_action_outcome_t::from(
        select_editor_scope(context.environment.document.history, *state, selection.scope,
                            selection.invert,
                            selection.from_cursor ? std::optional{state->cursor} : std::nullopt),
        std::string(selection.message), notification_severity_t::success, notification_source_t::edit);
}

// Audio-time markers need the chart's timing index. A failed build is reported
// once and navigation falls back to beat-anchored markers only.
auto marker_timing_index(const wormedit::chart_document_t& chart,
                         const wormedit::chart_indices_t& indices, notification_sink_t& sink)
    -> const wormedit::timing_index_t* {
    if (!wormedit::sys::has_audio_marker_navigation(chart)) return nullptr;
    if (indices.timing.value) return &*indices.timing.value;
    sink.post_feedback(
        notification_severity_t::warning, notification_source_t::input,
        "Audio-time bookmarks are skipped because the chart timing could not be built.");
    return nullptr;
}

auto navigate_active_markers(command_context_t& context,
                             const wormedit::editor_interaction_state_t& state,
                             wormedit::marker_navigation_direction_t direction)
    -> wormedit::marker_navigation_outcome_t {
    using namespace wormedit;
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    const auto timing =
        chart ? marker_timing_index(*chart, *context.environment.document.history.indices(chart->id),
                                    context.environment.document.notifications)
              : nullptr;
    return marker_navigation_match_t::from(context.environment.document.history.document,
                                           context.environment.document.history.document.revision,
                                           state.chart, state.cursor, direction, {}, {},
                                           timing);
}

void post_marker_warnings(notification_sink_t& sink,
                          const wormedit::diagnostic_list_t& diagnostics) {
    for (const auto& diagnostic : diagnostics)
        sink.post_feedback(to_notification_severity(diagnostic.severity),
                           notification_source_t::input, diagnostic.message);
}

auto navigation_commands(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (kind == command_kind_t::navigation_previous_snap ||
        kind == command_kind_t::navigation_next_snap) {
        if (!state)
            return compose_action_outcome_t::from(
                "Snap navigation requires the active Compose chart.",
                notification_severity_t::warning, notification_source_t::input,
                diagnostic_code_t::unsupported_feature);
        const auto direction = kind == command_kind_t::navigation_previous_snap
                                   ? interaction_direction_t::backward
                                   : interaction_direction_t::forward;
        const auto result = dispatch_editor_action(context.environment.document.history, *state,
                                                   cycle_edit_snap_action_t{direction});
        if (result.accepted) {
            remember_snap_denominator(context.environment.editor.commands, state->chart,
                                      state->active_snap_denominator);
            follow_snap_with_quantize_grid(context.environment.editor.commands,
                                           context.environment.document.history, *state);
        }
        return compose_action_outcome_t::from(
            result,
            direction == interaction_direction_t::backward ? "Previous snap selected."
                                                           : "Next snap selected.",
            notification_severity_t::hint, notification_source_t::input);
    }
    if (!state)
        return compose_action_outcome_t::from(
            "Bookmark navigation requires the active Compose chart.",
            notification_severity_t::warning, notification_source_t::input,
            diagnostic_code_t::unsupported_feature);
    const auto direction = kind == command_kind_t::navigation_previous_marker
                               ? marker_navigation_direction_t::previous
                               : marker_navigation_direction_t::next;
    const auto marker = navigate_active_markers(context, *state, direction);
    if (marker.value.status == marker_navigation_phase_t::no_result) {
        return compose_action_outcome_t::from(
            direction == marker_navigation_direction_t::previous
                ? "No previous beat-anchored bookmark or section boundary."
                : "No next beat-anchored bookmark or section boundary.",
            notification_severity_t::info, notification_source_t::input,
            compose_message_lifetime_t::borrowed);
    }
    if (marker.value.status != marker_navigation_phase_t::found || !marker.value.item ||
        !marker.value.item->beat_position)
        return compose_action_outcome_t::from(
            marker.diagnostics.empty() ? "Bookmark navigation could not resolve an exact beat row."
                                       : marker.diagnostics.front().message,
            marker.diagnostics.empty()
                ? notification_severity_t::error
                : to_notification_severity(marker.diagnostics.front().severity),
            notification_source_t::input, diagnostic_code_t::invalid_id);
    post_marker_warnings(context.environment.document.notifications, marker.diagnostics);
    return compose_action_outcome_t::from(
        dispatch_editor_action(context.environment.document.history, *state,
                               jump_edit_cursor_action_t{*marker.value.item->beat_position,
                                                         marker.value.document_revision}),
        direction == marker_navigation_direction_t::previous
            ? "Moved to the previous bookmark or section boundary."
            : "Moved to the next bookmark or section boundary.",
        notification_severity_t::hint, notification_source_t::input);
}

auto open_bookmark_chooser_command(command_context_t& context) -> command_result_t {
    auto* interaction = context.compose();
    if (!interaction || !context.environment.editor.commands.bookmarks.has_chooser_backend())
        return compose_action_outcome_t::from(
            "Bookmark chooser requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    if (interaction->is_pending())
        return compose_action_outcome_t::from(
            "Finish or cancel the active Compose gesture first.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    const bool opened = context.environment.editor.commands.bookmarks.open_chooser(
        context.environment.document.history.document.id.value, interaction->chart.value,
        context.environment.document.history.document.revision);
    if (!opened)
        return compose_action_outcome_t::from(
            "Bookmark chooser is unavailable while another editor owns input.",
            notification_severity_t::warning, notification_source_t::input,
            diagnostic_code_t::unsupported_feature);
    return command_result_t{.delivery = compose_feedback_delivery_t::none};
}

auto open_sound_picker_command(command_context_t& context) -> command_result_t {
    auto* interaction = context.compose();
    if (!interaction || !context.environment.editor.commands.services.open_sound_picker)
        return compose_action_outcome_t::from(
            "Sound picker requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    if (interaction->is_pending() ||
        context.environment.editor.commands.project.clipboard.paste_session)
        return compose_action_outcome_t::from(
            "Finish or cancel the active Compose gesture first.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    const bool opened = context.environment.editor.commands.services.open_sound_picker(
        context.environment.document.history.document.id.value, interaction->chart.value,
        context.environment.document.history.document.revision);
    if (!opened)
        return compose_action_outcome_t::from(
            "Sound picker is unavailable while another editor owns input.",
            notification_severity_t::warning, notification_source_t::input,
            diagnostic_code_t::unsupported_feature);
    return command_result_t{.delivery = compose_feedback_delivery_t::none};
}

auto compose_quantize_command(command_context_t& context) -> command_result_t {
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (!state || !can_apply_compose_quantize(context.environment.editor.commands, *state))
        return compose_action_outcome_t::from(
            compose_quantize_idle_message, notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    const auto toggle = kind == command_kind_t::edit_toggle_quantize_preview;
    auto outcome = compose_action_outcome_t{};
    if (toggle) {
        const auto enabled = toggle_compose_quantize_preview(
            context.environment.document.history, *state, context.environment.editor.commands);
        outcome = compose_action_outcome_t::from(
            enabled ? "Quantize preview enabled." : "Quantize preview disabled.",
            notification_severity_t::hint, notification_source_t::edit,
            compose_message_lifetime_t::borrowed);
        outcome.delivery = compose_feedback_delivery_t::transient_hint;
    } else {
        const auto request = compose_quantize_editor_for(
                                 context.environment.document.history, *state,
                                 context.environment.editor.commands)
                                 .request;
        outcome = apply_compose_quantize_selection(context.environment.document.history, *state,
                                                   context.environment.editor.commands, request);
    }
    outcome.are_command_diagnostics_retained = false;
    return outcome;
}

auto project_export_command(command_context_t& context) -> command_result_t {
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!chart || !context.environment.editor.commands.services.open_project_export)
        return compose_action_outcome_t::from(
            "Export requires a selected project difficulty.", notification_severity_t::warning,
            notification_source_t::export_, diagnostic_code_t::unsupported_feature);
    if ((context.environment.editor.retained &&
         context.environment.editor.retained->is_pending()) ||
        context.environment.editor.commands.project.clipboard.paste_session)
        return compose_action_outcome_t::from(
            "Finish or cancel the active editor gesture before exporting.",
            notification_severity_t::warning, notification_source_t::export_,
            diagnostic_code_t::unsupported_feature);
    const bool opened = context.environment.editor.commands.services.open_project_export(
        context.environment.document.history.document.id.value, chart->id.value,
        context.environment.document.history.document.revision);
    if (!opened)
        return compose_action_outcome_t::from(
            "Export is unavailable while another window owns input.",
            notification_severity_t::warning, notification_source_t::export_,
            diagnostic_code_t::unsupported_feature);
    return command_result_t{.delivery = compose_feedback_delivery_t::none};
}

auto compose_auxiliary_command(command_context_t& context) -> command_result_t {
    WormUiComposeDockCommand dock;
    switch (context.command.kind) {
    case command_kind_t::view_open_layers:
        dock = WORM_UI_COMPOSE_DOCK_OPEN_LAYERS;
        break;
    case command_kind_t::semantic_layer_create:
        dock = WORM_UI_COMPOSE_DOCK_CREATE_LAYER;
        break;
    case command_kind_t::view_toggle_sounds:
        dock = WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS;
        break;
    default:
        std::unreachable();
    }
    auto* interaction = context.compose();
    if (!interaction ||
        context.environment.editor.commands.project.compose_view.compose_timing_focus ||
        !context.environment.editor.commands.services.compose_dock_command)
        return compose_action_outcome_t::from(
            "Dock command requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    if (interaction->is_pending() ||
        context.environment.editor.commands.project.clipboard.paste_session)
        return compose_action_outcome_t::from(
            "Finish or cancel the active Compose gesture first.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    const bool accepted = context.environment.editor.commands.services.compose_dock_command(
        dock, context.environment.document.history.document.id.value, interaction->chart.value,
        context.environment.document.history.document.revision);
    if (!accepted)
        return compose_action_outcome_t::from(
            "Dock command is unavailable while another editor owns input.",
            notification_severity_t::warning, notification_source_t::input,
            diagnostic_code_t::unsupported_feature);
    return command_result_t{.delivery = compose_feedback_delivery_t::none};
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
auto bookmark_commands(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    const auto kind = context.command.kind;
    const bool previous = kind == command_kind_t::navigation_previous_bookmark;
    auto* state = context.compose();
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!state || !chart)
        return compose_action_outcome_t::from(
            "Bookmark commands require the active Compose chart.", notification_severity_t::warning,
            notification_source_t::input, diagnostic_code_t::unsupported_feature);
    if (kind == command_kind_t::markers_add_bookmark &&
        (state->is_pending() ||
         context.environment.editor.commands.project.clipboard.paste_session))
        return compose_action_outcome_t::from(
            "Finish or cancel the active Compose gesture before editing bookmarks.",
            notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    auto outcome = run_bookmark_command(context, *state, *chart, previous);
    if (!outcome.accepted && !outcome.diagnostics.empty())
        outcome.delivery = compose_feedback_delivery_t::diagnostic_message;
    return outcome;
}

auto workspace_tab_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    std::optional<workspace_tab_t> tab;
    switch (context.command.kind) {
    case command_kind_t::timing_open_settings:
        context.environment.editor.commands.preferences.timing_settings_visible =
            !context.environment.editor.commands.preferences.timing_settings_visible;
        return command_result_t{.delivery = compose_feedback_delivery_t::none};
    case command_kind_t::workspace_open_setup:
        tab = workspace_tab_t::setup;
        break;
    case command_kind_t::workspace_open_metadata:
        tab = workspace_tab_t::metadata;
        break;
    case command_kind_t::workspace_open_timing:
        // The Timing screen is retired; its command opens Compose in timing focus.
        tab = workspace_tab_t::compose;
        context.environment.editor.commands.project.compose_view.compose_timing_focus = true;
        break;
    case command_kind_t::workspace_open_compose:
        tab = workspace_tab_t::compose;
        context.environment.editor.commands.project.compose_view.compose_timing_focus = false;
        break;
    case command_kind_t::workspace_open_storyboard:
        tab = workspace_tab_t::storyboard;
        break;
    case command_kind_t::workspace_open_audio_slices:
        tab = workspace_tab_t::audio_slices;
        break;
    case command_kind_t::workspace_open_compare_validate:
        tab = workspace_tab_t::compare_validate;
        break;
    default:
        std::unreachable();
    }
    auto applied = apply_workspace_action(context.environment.document.workspace,
                                                   context.environment.document.history.document,
                                                   workspace_switch_tab_action_t{*tab});
    auto outcome = compose_action_outcome_t::from(
        document_command_result_t{.applied = applied.value,
                                  .diagnostics = std::move(applied.diagnostics)},
        compose_action_description_t{.success = "Workspace tab changed.",
                                     .unchanged = "Workspace tab change was rejected.",
                                     .source = notification_source_t::input,
                                     .diagnostic_policy =
                                         compose_diagnostic_feedback_policy_t::rejected_only,
                                     .unchanged_severity = notification_severity_t::warning,
                                     .is_unchanged_accepted = false},
        compose_message_lifetime_t::borrowed);
    outcome.severity = outcome.accepted ? notification_severity_t::info : outcome.severity;
    outcome.mutated = false;
    return outcome;
}

auto timing_lock_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    if (!(context.environment.document.workspace.active_tab == workspace_tab_t::compose &&
          app.project.compose_view.compose_timing_focus))
        return compose_action_outcome_t::from(
            "Timing Lock requires Compose with Timing focus.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    app.project.timing_recipe.timing_locked = !app.project.timing_recipe.timing_locked;
    if (app.project.timing_recipe.timing_locked && context.environment.editor.timing_stage) {
        cancel_proposal(*context.environment.editor.timing_stage);
        context.environment.editor.timing_stage.reset();
    }
    return compose_action_outcome_t::from(
        app.project.timing_recipe.timing_locked ? "Timing edits locked." : "Timing edits unlocked.",
        notification_severity_t::success, notification_source_t::edit,
        compose_message_lifetime_t::borrowed);
}

auto effective_scroll_multiplier_at(const wormedit::chart_document_t& chart,
                                    const wormedit::chart_indices_t& indices,
                                    wormedit::local_position_t position)
    -> std::optional<wormedit::signed_rational_t> {
    using namespace wormedit;
    const auto* measures = indices.measures ? &*indices.measures : nullptr;
    const auto row = measures ? (*measures).row_of( position) : std::nullopt;
    if (!row || *row > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const auto& timing = indices.timing;
    if (!timing.value)
        return std::nullopt;
    const auto scroll = scroll_speed_index_t::from(chart, *timing.value);
    if (!scroll.value)
        return std::nullopt;
    const auto value = scroll_speed_state_t::from(*scroll.value, *timing.value,
                                                  {static_cast<std::int64_t>(*row), 1});
    return value ? std::optional{value->value.scroll_multiplier} : std::nullopt;
}

auto timing_insert_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    timing_lane_kind_t lane{};
    if (context.command.kind == command_kind_t::timing_insert_bpm)
        lane = timing_lane_kind_t::bpm;
    else
        lane = timing_lane_kind_t::scroll;

    if (context.environment.editor.commands.project.timing_recipe.timing_locked)
        return compose_action_outcome_t::from(
            "Timing edits are locked.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (context.environment.editor.timing_stage)
        return compose_action_outcome_t::from(
            "Confirm or cancel the open timing edit before starting another.",
            notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    auto* state = context.compose();
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!state || !chart)
        return compose_action_outcome_t::from(
            "Timing insertion requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (state->virtual_bms_cursor_row)
        return compose_action_outcome_t::from(
            "Timing insertion is unavailable at a virtual BMS cursor row.",
            notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    if (state->is_pending())
        return compose_action_outcome_t::from(
            "Finish or cancel the active Compose gesture before inserting timing.",
            notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);

    timing_lane_value_t initial;
    if (lane == timing_lane_kind_t::bpm) {
        const auto bpm = effective_bpm_at(*chart, state->cursor);
        if (!bpm)
            return compose_action_outcome_t::from(
                "The effective BPM could not be resolved at the cursor.",
                notification_severity_t::warning, notification_source_t::edit,
                diagnostic_code_t::unsupported_feature);
        initial = timing_bpm_value_t{*bpm};
    } else {
        const auto multiplier = effective_scroll_multiplier_at(
            *chart, *context.environment.document.history.indices(chart->id), state->cursor);
        if (!multiplier)
            return compose_action_outcome_t::from(
                "The effective SV value could not be resolved at the cursor.",
                notification_severity_t::warning, notification_source_t::edit,
                diagnostic_code_t::unsupported_feature);
        initial = timing_scroll_value_t{*multiplier};
    }
    auto preview = timing_lane_pseudonote_preview_t::propose(
        context.environment.document.history, chart->id,
        wormedit::timing_lane_pseudonote_state_t{
            .kind = lane,
            .position = state->cursor,
            .value = std::move(initial),
            .initial_value_source = timing_lane_initial_value_source_t::effective_at_position},
        context.environment.editor.commands.project.timing_recipe.timing_locked);
    if (!preview.value) {
        const auto message = preview.diagnostics.empty()
                                 ? "Timing event could not be staged at the Compose cursor."
                                 : preview.diagnostics.front().message;
        return compose_action_outcome_t::from(
            message,
            preview.diagnostics.empty()
                ? notification_severity_t::warning
                : to_notification_severity(preview.diagnostics.front().severity),
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    }
    context.environment.editor.timing_stage = std::move(preview);
    return compose_action_outcome_t::from(
        "Timing event staged; confirm to insert it.", notification_severity_t::hint,
        notification_source_t::edit, compose_message_lifetime_t::borrowed);
}

auto select_tool_command(command_context_t& context) -> command_result_t {
    const auto kind = context.command.kind;
    auto* state = context.compose();
    if (!state)
        return compose_action_outcome_t::from(
            "Select requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (kind != command_kind_t::edit_tool_select &&
        !compose_view_for_state(context.environment.document.history, *state))
        return compose_action_outcome_t::from(
            "Draw and Layer require editable chart geometry.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (kind == command_kind_t::edit_tool_layer &&
        !compose_layer_brush_target_editable(
            context.environment.document.history.document, state->chart,
            context.environment.editor.commands.project.semantic_layer.semantic_layer_target))
        return compose_action_outcome_t::from(
            "Pick an unlocked visible layer in the Layers dock.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    if (kind == command_kind_t::audio_paint_hitsound &&
        !context.environment.editor.commands.project.compose_gesture.can_paint(
            context.environment.document.history.document, *state))
        return compose_action_outcome_t::blocked("Arm a Sample resource or choose osu! hitsounds before painting.");
    if (state->is_pending()) {
        const auto canceled =
            wormedit::dispatch_editor_action(context.environment.document.history, *state,
                                             wormedit::cancel_editor_gesture_action_t{});
        if (!canceled.accepted)
            return compose_action_outcome_t::from(canceled, "Gesture canceled.",
                                                  notification_severity_t::warning,
                                                  notification_source_t::edit);
    }
    context.environment.editor.commands.project.compose_gesture.bga_draw_stroke.reset();
    context.environment.editor.commands.project.compose_gesture.resource_paint_mode_active =
        kind == command_kind_t::audio_paint_hitsound;
    context.environment.editor.commands.project.compose_gesture.note_paint_mode_active =
        kind == command_kind_t::edit_tool_draw;
    context.environment.editor.commands.project.compose_gesture.semantic_layer_paint_mode_active =
        kind == command_kind_t::edit_tool_layer;
    return compose_action_outcome_t::from("Compose tool selected.", notification_severity_t::hint,
                                          notification_source_t::edit,
                                          compose_message_lifetime_t::borrowed);
}

auto hitsound_command(command_context_t& context) -> command_result_t {
    auto* state = context.compose();
    if (!state) return compose_action_outcome_t::blocked("Hitsounds require the active Compose chart.");
    if (context.command.kind == command_kind_t::hitsound_toggle_palette) {
        state->hitsound_palette_visible = !state->hitsound_palette_visible;
        if (state->hitsound_palette_visible && !state->armed_hitsample) state->armed_hitsample = wormedit::osu_hitsample_t{};
        return compose_action_outcome_t::from("Hitsound palette updated.", notification_severity_t::hint);
    }
    if (!context.command.hitsound) return compose_action_outcome_t::blocked("Invalid hitsound command.");
    return apply_compose_hitsound(context.environment.document.history, *state, *context.command.hitsound,
        context.environment.editor.commands.project.compose_gesture.resource_paint_mode_active);
}

auto note_kind_command(command_context_t& context) -> command_result_t {
    auto* state = context.compose();
    if (!state || !context.command.note_kind)
        return compose_action_outcome_t::from(
            "Note kind requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    const auto result =
        wormedit::arm_note_kind(context.environment.document.history, *state,
                                static_cast<wormedit::note_kind_t>(*context.command.note_kind));
    if (result.accepted) {
        context.environment.editor.commands.project.compose_gesture.resource_paint_mode_active = false;
        context.environment.editor.commands.project.compose_gesture.note_paint_mode_active = true;
        context.environment.editor.commands.project.compose_gesture
            .semantic_layer_paint_mode_active = false;
    }
    return compose_action_outcome_t::from(result, "Note paint tool armed.",
                                          notification_severity_t::hint,
                                          notification_source_t::edit);
}

auto semantic_layer_slot_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    const auto* chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!state || !chart)
        return compose_action_outcome_t::from(
            "Layer selection requires the active Compose chart.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    const auto slot = context.command.semantic_layer_slot.value_or(0);
    if (!slot)
        return compose_action_outcome_t::from(
            "That semantic-layer slot is unavailable.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    auto& app = context.environment.editor.commands;
    if (app.project.semantic_layer.semantic_layer_palette_chart != chart->id) {
        app.project.semantic_layer.semantic_layer_palette_chart = chart->id;
        app.project.semantic_layer.semantic_layer_palette_page = 0;
    }
    const auto palette = wormedit::semantic_layer_palette_window_t::from(
        *chart, app.project.semantic_layer.semantic_layer_target,
        app.project.semantic_layer.semantic_layer_palette_page, false,
        wormedit::ui::semantic_layer_shortcut_limit);
    if (!palette || slot > palette->layer_indices.size())
        return compose_action_outcome_t::from(
            "That semantic-layer slot is unavailable.", notification_severity_t::warning,
            notification_source_t::edit, diagnostic_code_t::unsupported_feature);
    const auto* layer = &chart->semantic_layers[palette->layer_indices[slot - 1]];
    app.project.semantic_layer.semantic_layer_target = layer->id;
    app.project.semantic_layer.semantic_layer_target_none = false;
    return compose_action_outcome_t::from("Semantic-layer target: " + layer->name,
                                          notification_severity_t::info,
                                          notification_source_t::edit);
}

auto semantic_layer_membership_command(command_context_t& context) -> command_result_t {
    using namespace wormedit;
    auto* state = context.compose();
    auto& app = context.environment.editor.commands;
    if (!state ||
        (!app.project.semantic_layer.semantic_layer_target &&
         !app.project.semantic_layer.semantic_layer_target_none) ||
        state->selected_notes.empty())
        return compose_action_outcome_t::from(
            "Choose a semantic layer and select playable notes first.",
            notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    semantic_layer_edit_action_t edit;
    const auto kind = context.command.kind;
    if (app.project.semantic_layer.semantic_layer_target_none &&
        kind != command_kind_t::semantic_layer_apply)
        return compose_action_outcome_t::from(
            "No layer only assigns", notification_severity_t::warning, notification_source_t::edit,
            diagnostic_code_t::unsupported_feature);
    if (app.project.semantic_layer.semantic_layer_target_none)
        edit = clear_semantic_layer_membership_action_t{.chart = state->chart, .notes = state->selected_notes};
    else if (kind == command_kind_t::semantic_layer_apply)
        edit = replace_notes_semantic_layer_action_t{.chart = state->chart,
            .layer = *app.project.semantic_layer.semantic_layer_target, .notes = state->selected_notes};
    else if (kind == command_kind_t::semantic_layer_remove)
        edit = semantic_layer_membership_edit_t{
            .chart = state->chart,
            .layer = *app.project.semantic_layer.semantic_layer_target,
            .notes = state->selected_notes,
            .operation = wormedit::membership_operation_t::remove};
    else
        edit = replace_notes_semantic_layer_action_t{.chart = state->chart,
            .layer = *app.project.semantic_layer.semantic_layer_target, .notes = state->selected_notes,
            .toggle = true};
    return compose_action_outcome_t::from(
        apply_semantic_layer_edit(context.environment.document.history, edit),
        compose_action_description_t{.success = "Semantic-layer membership updated.",
                                     .unchanged = "Semantic-layer membership was unchanged.",
                                     .source = notification_source_t::edit,
                                     .diagnostic_policy =
                                         compose_diagnostic_feedback_policy_t::rejected_only},
        compose_message_lifetime_t::borrowed);
}

auto clear_semantic_layer_command(command_context_t& context) -> compose_action_outcome_t {
    auto& application_state = context.environment.editor.commands;
    application_state.project.semantic_layer.semantic_layer_target.reset();
    application_state.project.semantic_layer.semantic_layer_target_none = false;
    application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
    application_state.project.semantic_layer.semantic_layer_solo_active = false;
    return compose_action_outcome_t::from(
        "Semantic-layer target cleared.", notification_severity_t::info,
        notification_source_t::edit, compose_message_lifetime_t::borrowed);
}

auto unsupported_application_command(command_context_t&) -> compose_action_outcome_t {
    return compose_action_outcome_t::from(
        "Command is not implemented by the application.", notification_severity_t::warning,
        notification_source_t::system, diagnostic_code_t::unsupported_feature);
}

struct application_command_route_t {
    compose_action_outcome_t (*operation)(command_context_t&);
    compose_feedback_order_t feedback_order{compose_feedback_order_t::after_rebind};
    bool refresh_on_accepted{};
    bool uses_producer_rebind_policy{};
    bool preserves_bookmark_preview{};
    auto operator==(const application_command_route_t&) const -> bool = default;

    static auto from(command_kind_t kind) -> application_command_route_t;
};

auto application_command_route_t::from(command_kind_t kind) -> application_command_route_t {
    switch (kind) {
    case command_kind_t::history_undo:
    case command_kind_t::history_redo:
        return {.operation = history_command,
                .feedback_order = compose_feedback_order_t::before_rebind};
    case command_kind_t::audio_paint_hitsound:
    case command_kind_t::edit_tool_select:
    case command_kind_t::edit_tool_draw:
    case command_kind_t::edit_tool_layer:
        return {.operation = select_tool_command};
    case command_kind_t::edit_note_kind:
        return {.operation = note_kind_command};
    case command_kind_t::hitsound_edit:
    case command_kind_t::hitsound_toggle_palette:
        return {.operation = hitsound_command};
    case command_kind_t::measures_insert_before:
    case command_kind_t::measures_insert_after:
    case command_kind_t::measures_remove_empty:
    case command_kind_t::measures_split_at_cursor:
        return {.operation = measure_command,
                .feedback_order = compose_feedback_order_t::before_rebind,
                .refresh_on_accepted = true};
    case command_kind_t::transport_loop_selection:
    case command_kind_t::transport_toggle_loop_selection:
        return {.operation = loop_selection_command};
    case command_kind_t::transport_toggle_metronome:
        return {.operation = metronome_command};
    case command_kind_t::transport_play_stop:
    case command_kind_t::transport_play_pause:
    case command_kind_t::transport_play_from_start:
    case command_kind_t::transport_stop:
    case command_kind_t::transport_rate_slower:
    case command_kind_t::transport_rate_faster:
        return {.operation = transport_command};
    case command_kind_t::transport_external_play_start:
    case command_kind_t::transport_external_play_here:
    case command_kind_t::transport_external_stop:
        return {.operation = external_player_command};
    case command_kind_t::edit_move_to_bgm:
    case command_kind_t::edit_move_next_to_bgm:
        return {.operation = note_to_bgm_command};
    case command_kind_t::edit_copy:
    case command_kind_t::edit_paste:
        return {.operation = clipboard_commands,
                .feedback_order = compose_feedback_order_t::before_rebind};
    case command_kind_t::edit_cut:
        return {.operation = clipboard_commands,
                .feedback_order = compose_feedback_order_t::before_rebind,
                .refresh_on_accepted = true};
    case command_kind_t::edit_delete:
        return {.operation = edit_commands,
                .feedback_order = compose_feedback_order_t::before_rebind};
    case command_kind_t::selection_clear:
        return {.operation = clear_selection_or_cancel_preview};
    case command_kind_t::selection_select_same_sound_chart:
        return {.operation = same_sound_command};
    case command_kind_t::selection_select_all:
    case command_kind_t::selection_select_all_objects:
    case command_kind_t::selection_invert:
    case command_kind_t::selection_select_all_after_cursor:
    case command_kind_t::selection_select_all_objects_after_cursor:
        return {.operation = selection_commands};
    case command_kind_t::navigation_previous_snap:
    case command_kind_t::navigation_next_snap:
    case command_kind_t::navigation_previous_marker:
    case command_kind_t::navigation_next_marker:
        return {.operation = navigation_commands};
    case command_kind_t::navigation_go_to_bookmark:
        return {.operation = open_bookmark_chooser_command};
    case command_kind_t::document_export_output_group:
        return {.operation = project_export_command};
    case command_kind_t::edit_toggle_quantize_preview:
    case command_kind_t::edit_apply_quantize:
        return {.operation = compose_quantize_command, .uses_producer_rebind_policy = true};
    case command_kind_t::navigation_go_to_sound:
        return {.operation = open_sound_picker_command};
    case command_kind_t::view_open_layers:
    case command_kind_t::semantic_layer_create:
    case command_kind_t::view_toggle_sounds:
        return {.operation = compose_auxiliary_command};
    case command_kind_t::navigation_previous_bookmark:
    case command_kind_t::navigation_next_bookmark:
    case command_kind_t::markers_add_bookmark:
        return {.operation = bookmark_commands,
                .uses_producer_rebind_policy = true,
                .preserves_bookmark_preview = true};
    case command_kind_t::workspace_open_setup:
    case command_kind_t::workspace_open_metadata:
    case command_kind_t::workspace_open_timing:
    case command_kind_t::workspace_open_compose:
    case command_kind_t::workspace_open_storyboard:
    case command_kind_t::workspace_open_audio_slices:
    case command_kind_t::workspace_open_compare_validate:
    case command_kind_t::timing_open_settings:
        return {.operation = workspace_tab_command};
    case command_kind_t::timing_insert_bpm:
    case command_kind_t::timing_insert_sv:
        return {.operation = timing_insert_command};
    case command_kind_t::timing_toggle_lock:
        return {.operation = timing_lock_command};
    case command_kind_t::semantic_layer_select_slot:
        return {.operation = semantic_layer_slot_command};
    case command_kind_t::semantic_layer_apply:
    case command_kind_t::semantic_layer_remove:
    case command_kind_t::semantic_layer_toggle:
        return {.operation = semantic_layer_membership_command};
    case command_kind_t::semantic_layer_clear_target:
        return {.operation = clear_semantic_layer_command};

    case command_kind_t::document_new_project:
    case command_kind_t::document_save_project:
    case command_kind_t::document_new_chart:
    case command_kind_t::view_notifications:
    case command_kind_t::workspace_options:
    case command_kind_t::workspace_lane_keys:
    case command_kind_t::timing_tap_assistant:
    case command_kind_t::timing_tap:
    case command_kind_t::audio_toggle_click_audition:
    case command_kind_t::audio_toggle_playback_audition:
    case command_kind_t::workspace_command_menu:
        break;
    }
    return {.operation = unsupported_application_command};
}

auto invoke_application_command(application_command_t command,
                                const command_dispatch_inputs_t& environment)
    -> dispatch_outcome_t {
    command_context_t context{environment, command};
    const auto route = application_command_route_t::from(command.kind);
    const auto previous_revision = environment.document.history.document.revision;
    const auto previous_chart =
        environment.document.workspace.selected_chart.value_or(chart_id_t{});
    const auto refusal = timing_focus_command_refusal(command, environment.editor.commands);
    auto outcome = refusal
        ? compose_action_outcome_t::from(std::string{*refusal}, notification_severity_t::warning,
              notification_source_t::input, diagnostic_code_t::unsupported_feature)
        : route.operation(context);
    outcome.feedback_order = route.feedback_order;
    if (route.refresh_on_accepted && outcome.accepted)
        outcome.is_workspace_refresh_required = true;
    if (!route.uses_producer_rebind_policy) {
        outcome.rebind_failure =
            "Command applied, but environment.document.workspace rebinding failed.";
        outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::document_only;
        outcome.is_rebind_severity_diagnostic = false;
    }
    const bool preserve_preview = route.preserves_bookmark_preview && outcome.mutated;
    auto finished = finish_application_command(environment.document.notifications,
                                               std::move(outcome), &environment.document);
    if (preserve_preview && finished.value)
        static_cast<void>(environment.editor.commands.bookmarks.preserve_preview_revision(
            previous_chart, previous_revision, environment.document.history.document.revision));
    return finished;
}

} // namespace wormedit::sys
