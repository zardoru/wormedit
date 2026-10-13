#include "sys/compose_input.h"
#include "sys/application_edit_inputs.h"
#include "sys/compose_input_internal.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_navigation.h"
#include "sys/compose_navigation_input.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_playhead_skip.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/compose_sound_selection.h"
#include <wormedit/core/chart_clipboard.h>
#include <wormedit/core/interaction_gestures.h>

#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/span_manipulation.h>
#include <wormedit/audio/decoder.h>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/kiai_spans.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/workspace.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <numeric>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace wormedit::sys {
using namespace compose_input_detail;
using namespace compose_navigation_input_detail;

namespace compose_input_detail {

auto validate_bmson_marker_positions(
    const wormedit::chart_document_t& chart,
    std::span<const wormedit::bmson_sound_marker_insertion_t> markers) -> std::string {
    using namespace wormedit;
    const auto measures = build_measure_index(chart);
    const auto* measure_index = std::get_if<measure_index_t>(&measures);
    if (!measure_index) {
        return "BMSON marker target has invalid chart geometry.";
    }
    for (std::size_t index = 0; index < markers.size(); ++index) {
        if (!absolute_row(*measure_index, markers[index].position)) {
            return "BMSON marker target is outside exact chart geometry.";
        }
        const auto repeated = std::ranges::any_of(markers.first(index), [&](const auto& prior) {
            return prior.position == markers[index].position;
        });
        if (repeated) {
            return "BMSON razor segment visits the same snapped row twice.";
        }
    }
    return {};
}

auto find_bmson_marker_channel(const wormedit::chart_document_t& chart,
                              wormedit::object_id_t reference)
    -> std::pair<const wormedit::bgm_event_t*, const wormedit::sound_channel_t*> {
    using namespace wormedit;
    const bgm_event_t* semantic{};
    for (const auto& measure : chart.measures) {
        const auto event = std::ranges::find(measure.bgm_events, reference, &bgm_event_t::id);
        if (event != measure.bgm_events.end()) {
            semantic = &*event;
            break;
        }
    }
    if (!semantic || !semantic->sound_channel_event) {
        return {semantic, nullptr};
    }
    const auto channel = std::ranges::find_if(chart.sound_channels, [&](const auto& candidate) {
        return std::ranges::any_of(candidate.events, [&](const auto& event) {
            return event.id == *semantic->sound_channel_event;
        });
    });
    return {semantic, channel == chart.sound_channels.end() ? nullptr : &*channel};
}

auto validate_bmson_marker_channel(
    const wormedit::sound_channel_t& channel,
    std::span<const wormedit::bmson_sound_marker_insertion_t> markers) -> std::string {
    for (const auto& marker : markers) {
        const auto occupied = std::ranges::any_of(channel.events, [&](const auto& event) {
            return event.position == marker.position;
        });
        if (occupied) {
            return "A marker or playable event already occupies this sound-channel pulse.";
        }
    }
    return {};
}

auto validate_bmson_marker_draft(
    const application_command_state_t::bmson_marker_draft_t* draft,
    std::span<const wormedit::bmson_sound_marker_insertion_t> markers) -> std::string {
    if (markers.size() > 256 ||
        (draft && (draft->markers.size() > 256 ||
                   markers.size() > 256 - draft->markers.size()))) {
        return "A BMSON marker batch can contain at most 256 pulses.";
    }
    if (!draft) {
        return {};
    }
    for (const auto& marker : markers) {
        const auto repeated = std::ranges::any_of(draft->markers, [&](const auto& prior) {
            return prior.position == marker.position;
        });
        if (repeated) {
            return "That BMSON pulse is already staged.";
        }
    }
    return {};
}

} // namespace compose_input_detail

auto stage_bmson_bgm_markers(const wormedit::document_history_t& history,
                             const wormedit::editor_interaction_state_t& state,
                             application_command_state_t& application_state,
                             wormedit::object_id_t reference,
                             std::span<const wormedit::bmson_sound_marker_insertion_t> markers,
                             wormedit::notification_severity_t* severity) -> std::string {
    if (severity) *severity = wormedit::notification_severity_t::warning;
    using namespace wormedit;
    if (markers.empty()) {
        if (severity) *severity = notification_severity_t::hint;
        return "BMSON razor remains on the current snapped row.";
    }
    if (state.selected_objects.size() != 1 ||
        state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        state.selected_objects.front().id != reference) {
        return "Select one BMSON BGM marker to choose its sound channel.";
    }
    const auto chart = std::ranges::find(history.document.charts, state.chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end() ||
        chart->source_family != format_family_t::bmson) {
        return "The selected chart is not a BMSON chart.";
    }
    if (const auto validation = validate_bmson_marker_positions(*chart, markers);
        !validation.empty()) {
        return validation;
    }
    const auto [semantic, channel] = find_bmson_marker_channel(*chart, reference);
    if (!semantic || !semantic->sound_channel_event) {
        return "The selected BGM marker has no retained BMSON sound event.";
    }
    if (!channel) {
        return "The selected BMSON sound channel is unavailable.";
    }
    if (const auto validation = validate_bmson_marker_channel(*channel, markers);
        !validation.empty()) {
        return validation;
    }
    auto& draft = application_state.project.compose_gesture.bmson_marker_draft;
    if (draft && (draft->chart != state.chart ||
                  draft->revision != history.document.revision)) {
        draft.reset();
    }
    if (draft && draft->reference != reference) {
        return "Commit or cancel the current BMSON marker batch before choosing another channel.";
    }
    if (const auto validation = validate_bmson_marker_draft(draft ? &*draft : nullptr, markers);
        !validation.empty()) {
        return validation;
    }
    if (!draft) {
        draft = application_command_state_t::bmson_marker_draft_t{
            .chart = state.chart, .revision = history.document.revision,
            .reference = reference, .markers = {}};
    }
    draft->markers.insert(draft->markers.end(), markers.begin(), markers.end());
    if (severity) *severity = notification_severity_t::hint;
    return markers.size() == 1 ? "BMSON marker staged at the exact snapped row."
                               : "BMSON razor segment staged at exact snapped rows.";
}

auto stage_bmson_bgm_marker(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state,
                            application_command_state_t& application_state,
                            wormedit::object_id_t reference, wormedit::local_position_t position,
                            bool restart, wormedit::notification_severity_t* severity) -> std::string {
    const std::array markers{wormedit::bmson_sound_marker_insertion_t{position, restart}};
    return stage_bmson_bgm_markers(history, state, application_state, reference, markers, severity);
}


auto find_bmson_marker_source(const wormedit::chart_document_t& chart,
                               wormedit::object_id_t bgm_event)
    -> std::optional<bmson_marker_source_t> {
    using namespace wormedit;
    if (chart.source_family != format_family_t::bmson) {
        return std::nullopt;
    }
    for (const auto& measure : chart.measures) {
        const auto semantic = std::ranges::find(measure.bgm_events, bgm_event,
                                                &bgm_event_t::id);
        if (semantic == measure.bgm_events.end() || !semantic->sound_channel_event) {
            continue;
        }
        for (const auto& channel : chart.sound_channels) {
            const auto source = std::ranges::find(channel.events,
                *semantic->sound_channel_event, &sound_channel_event_t::id);
            if (source != channel.events.end() && !source->playable &&
                source->position == semantic->position) {
                return bmson_marker_source_t{
                    .source_lane = semantic->source_lane,
                    .position = semantic->position,
                    .source_event = source->id,
                    .channel = &channel};
            }
        }
    }
    return std::nullopt;
}

auto selected_bmson_waveform_source(compose_scene_cache_t& cache,
                                    const wormedit::chart_document_t& chart, std::uint64_t revision,
                                    wormedit::object_id_t bgm_event)
    -> std::optional<compose_scene_cache_t::channel_waveform_selection_t> {
    if (cache.waveform.channel_waveform_selection && cache.waveform.channel_waveform_selection->chart == chart.id &&
        cache.waveform.channel_waveform_selection->revision == revision &&
        cache.waveform.channel_waveform_selection->bgm_event == bgm_event)
        return cache.waveform.channel_waveform_selection;
    cache.waveform.channel_waveform_selection.reset();
    const auto marker = find_bmson_marker_source(chart, bgm_event);
    if (marker) {
        cache.waveform.channel_waveform_selection = compose_scene_cache_t::channel_waveform_selection_t{
            .chart = chart.id,
            .revision = revision,
            .bgm_event = bgm_event,
            .channel = marker->channel->id,
            .source_lane = marker->source_lane,
            .asset = marker->channel->source_clip.source};
    }
    return cache.waveform.channel_waveform_selection;
}

namespace compose_input_detail {

// A placement that created a note auditions it while the click-audition preference is on.
void audition_created_note(bool enabled, const wormedit::document_history_t& history,
                           const wormedit::editor_interaction_state_t& state,
                           wormedit::editor_interaction_result_t& result) {
    if (!enabled || !result.accepted || !result.document_changed || !result.created_note) return;
    result.audition = wormedit::editor_object_audition_request_t{
        .document_revision = history.document.revision,
        .chart = state.chart,
        .object = {wormedit::editor_selection_object_kind_t::note, *result.created_note}};
}


auto dispatch_compose_keyboard_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted, const wormedit::notification_sink_t* notifications,
    compose_scene_cache_t& scene_cache)
    -> compose_input_family_result_t {
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND:
        result = wormedit::arm_note_kind(history, state,
            static_cast<wormedit::note_kind_t>(input.note_kind));
        accepted = "Note kind armed.";
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP:
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS:
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE:
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL:
        return dispatch_compose_navigation_input(input, history, state, navigation_input,
                                                 application, notifications);
    case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD:
    case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD:
    case WORM_UI_COMPOSE_INPUT_DELETE_SELECTION:
        return dispatch_compose_cursor_input(input, history, state, application, notifications);
    case WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS:
    {
        auto pressed = dispatch_compose_lane_key_press(input, history, state, application, scene_cache, result, accepted);
        audition_created_note(application.preferences.editor_preferences.audition_on_click, history, state, result);
        return pressed;
    }
    case WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE:
    {
        auto released = dispatch_compose_lane_key_release(input, history, workspace, state, result,
                                                          accepted);
        audition_created_note(application.preferences.editor_preferences.audition_on_click, history, state, result);
        return released;
    }
    default:
        return {};
    }
}

auto dispatch_compose_navigation_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application,
    const wormedit::notification_sink_t* notifications) -> compose_input_family_result_t {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP: {
        const auto navigation = compose_navigation_action(input.navigation_action);
        if (!navigation || navigation_measure_count(*navigation) == 0)
            return "Compose measure navigation step used an unknown action.";
        editor_navigation_action_batch_t batch;
        batch.actions[batch.count++] = *navigation;
        auto dispatched = compose_input_family_result_t::continue_batch(
            dispatch_compose_navigation_actions(batch, history, state, &application,
                                                notifications));
        dispatched.notification_posted = notifications != nullptr;
        return dispatched;
    }
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS: {
        const auto navigation = compose_navigation_action(input.navigation_action);
        if (!navigation) {
            return "Compose navigation input used an unknown action.";
        }
        const auto status = dispatch_compose_navigation_actions(
            navigation_input.press(*navigation, static_cast<std::uint64_t>(SDL_GetTicks())),
            history, state, &application, notifications);
        auto dispatched = compose_input_family_result_t::continue_batch(status);
        dispatched.notification_posted = notifications != nullptr;
        return dispatched;
    }
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE: {
        const auto navigation = compose_navigation_action(input.navigation_action);
        if (!navigation) {
            return "Compose navigation release used an unknown action.";
        }
        navigation_input.release(*navigation);
        return compose_input_family_result_t::continue_batch("Compose navigation key released.");
    }
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL:
        navigation_input.cancel();
        return compose_input_family_result_t::continue_batch("Compose navigation canceled.");
    default:
        return {};
    }
}

auto dispatch_compose_cursor_input(const WormUiComposeInputAction& input,
                                   wormedit::document_history_t& history,
                                   wormedit::editor_interaction_state_t& state,
                                   application_command_state_t& application,
                                   const wormedit::notification_sink_t* notifications)
    -> compose_input_family_result_t {
    using namespace wormedit;
    if (input.kind != WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD &&
        input.kind != WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD) return {};
    editor_navigation_action_batch_t batch;
    batch.actions[batch.count++] = input.kind == WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD
        ? editor_navigation_action_t::cursor_forward : editor_navigation_action_t::cursor_backward;
    return compose_input_family_result_t::continue_batch(
        dispatch_compose_navigation_actions(batch, history, state, &application, notifications));
}

auto note_paint_sound(const WormUiComposeInputAction& input)
    -> std::optional<wormedit::resource_id_t> {
    if (input.resource_id == 0) return std::nullopt;
    return wormedit::resource_id_t{input.resource_id};
}

// A stroke or lane-key release advances once, only after creating notes with its sound.
void advance_sound_after_paint_commit(const wormedit::editor_interaction_result_t& result,
                                      std::optional<wormedit::resource_id_t> sound,
                                      bool notes_created) {
    if (!sound || !result.document_changed || !notes_created) return;
    worm_ui_frontend_resource_placed(sound->value, 0);
}

auto retained_bmson_source_geometry(const wormedit::document_history_t& history,
    const compose_scene_cache_t& cache, const wormedit::chart_document_t& chart,
    wormedit::asset_id_t asset) -> std::optional<wormedit::bmson_source_geometry_t> {
    const auto& index = cache.scene.stem_index;
    if (cache.scene.scene_document_identity != &history.document || cache.scene.scene_chart_identity != &chart ||
        !index.built || index.document != history.document.id || index.chart != chart.id ||
        index.revision != history.document.revision) return std::nullopt;
    const auto binding = std::ranges::find(index.bindings, asset,
        &wormedit::audio::decoded_source_binding_t::asset);
    if (binding == index.bindings.end() || !binding->source_sample_rate || !binding->source_frame_count)
        return std::nullopt;
    return wormedit::bmson_source_geometry_t{asset, binding->source_sample_rate, *binding->source_frame_count};
}

auto compose_stem_geometry(const wormedit::document_history_t& history,
    const wormedit::editor_interaction_state_t& state, const compose_scene_cache_t& cache,
    std::optional<wormedit::resource_id_t> sound) -> std::optional<wormedit::bmson_source_geometry_t> {
    using namespace wormedit;
    if (!sound) return std::nullopt;
    const auto chart = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto resource = std::ranges::find(history.document.resources, *sound, &resource_entry_t::id);
    if (chart == history.document.charts.end() || resource == history.document.resources.end())
        return std::nullopt;
    return retained_bmson_source_geometry(history, cache, *chart, resource->asset);
}

auto dispatch_compose_lane_key_press(const WormUiComposeInputAction& input,
                                     wormedit::document_history_t& history,
                                     wormedit::editor_interaction_state_t& state,
                                     application_command_state_t& application, compose_scene_cache_t& cache,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> compose_input_family_result_t {
    using namespace wormedit;
    if (input.lane_id == 0) {
        return "Lane-key input did not identify a chart lane.";
    }
    if (!state.selected_notes.empty() || std::ranges::any_of(state.selected_objects, [](const auto& object) {
            return object.kind == editor_selection_object_kind_t::note ||
                   object.kind == editor_selection_object_kind_t::bgm_event;
        })) {
        result = dispatch_editor_action(
            history, state,
            assign_selected_objects_to_lane_action_t{lane_id_t{input.lane_id}, input.successive});
        accepted = input.successive ? "Next selected object assigned to lane."
                                    : "Selected objects assigned to lane.";
    } else {
        result = dispatch_editor_action(
            history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id},
                                                               note_paint_sound(input),
                application.preferences.editor_preferences.move_collision_policy == move_collision_policy_t::displace_to_bgm,
                compose_stem_geometry(history, state, cache, note_paint_sound(input))});
        advance_sound_after_paint_commit(result, note_paint_sound(input), result.created_note.has_value());
        accepted = result.document_changed ? "Lane-key note updated." : "Lane hold started; move the edit cursor and release the key to finish.";
    }
    return compose_input_family_result_t::continue_with_result();
}

auto lane_key_sound(const wormedit::editor_interaction_state_t& state, std::uint64_t lane)
    -> std::optional<wormedit::resource_id_t> {
    if (state.pending_hold && (lane == 0 || state.pending_hold->lane.value == lane))
        return state.pending_hold->sound;
    const auto found = std::ranges::find(state.additional_keyboard_holds, wormedit::lane_id_t{lane},
                                        &wormedit::pending_hold_t::lane);
    return found == state.additional_keyboard_holds.end() ? std::nullopt : found->sound;
}

auto dispatch_compose_lane_key_release(const WormUiComposeInputAction& input,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t&,
                                       wormedit::editor_interaction_state_t& state,
                                       wormedit::editor_interaction_result_t& result,
                                       std::string_view& accepted)
    -> compose_input_family_result_t {
    using namespace wormedit;
    if ((state.pending_hold.has_value() &&
                state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) ||
               !state.additional_keyboard_holds.empty()) {
        const auto sound = lane_key_sound(state, input.lane_id);
        result = dispatch_editor_action(
            history, state, release_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id}});
        advance_sound_after_paint_commit(result, sound, result.created_note.has_value());
        accepted = result.document_changed ? "Lane hold committed." : "Lane hold released.";
    } else {
        result.accepted = true;
        accepted = "Lane key released.";
    }
    return compose_input_family_result_t::continue_with_result();
}


auto begin_bmson_bgm_move(const WormUiComposeInputAction& input, bmson_compose_context_t& context)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, action, state, gesture_active] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    if (gesture_active)
        return "Finish or cancel the active Compose gesture before moving a BGM event.";
    const auto result = dispatch_editor_action(history, state,
        begin_note_move_gesture_action_t{object_id_t{input.object_id},
            application_state.preferences.editor_preferences.move_collision_policy == move_collision_policy_t::displace_to_bgm});
    return result.accepted ? compose_input_family_result_t::continue_batch("Selected-object move started.")
        : compose_input_family_result_t{result.diagnostics.empty() ? "Selected-object move could not start."
                                                                   : result.diagnostics.front().message};
}


auto edit_bmson_draft_marker(const WormUiComposeInputAction& input,
                             bmson_compose_context_t& context) -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, action, state, gesture_active] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    if (gesture_active)
        return "Finish or cancel the active Compose gesture before editing a staged BMSON region.";
    auto& draft = application_state.project.compose_gesture.bmson_marker_draft;
    if (!draft || draft->chart != state.chart || draft->revision != history.document.revision ||
        input.object_id == 0 || input.object_id > draft->markers.size())
        return "The staged BMSON region edit expired.";
    auto proposed = draft->markers;
    const auto index = static_cast<std::size_t>(input.object_id - 1);
    if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT) {
        proposed.erase(proposed.begin() + index);
        if (proposed.empty()) {
            draft.reset();
            return compose_input_family_result_t::continue_batch(
                "The last staged BMSON region was removed.");
        }
    } else if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART) {
        proposed[index].restart = !proposed[index].restart;
    } else {
        const auto lane = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE
                              ? std::optional<lane_id_t>{state.active_lane}
                              : std::nullopt;
        if (proposed[index].playable_lane == lane)
            return compose_input_family_result_t::continue_batch(
                "Staged BMSON region already has that assignment.", notification_severity_t::info,
                notification_source_t::edit);
        proposed[index].playable_lane = lane;
    }
    auto temporary = make_document_history(history.document, 1);
    const auto checked = insert_bmson_sound_markers(temporary, temporary.document.revision,
                                                    draft->chart, draft->reference, proposed);
    if (!checked)
        return compose_input_family_result_t::stop_with_feedback(
            checked.diagnostics.empty() ? "Staged BMSON region edit could not be validated."
                                       : checked.diagnostics.front().message,
            checked.diagnostics.empty() ? notification_severity_t::warning
                : input_notification_severity(checked.diagnostics.front().severity),
            notification_source_t::edit);
    draft->markers = std::move(proposed);
    const auto message = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT
                             ? "Staged BMSON region removed."
                         : input.kind == WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART
                             ? "Staged BMSON region continuation/restart toggled."
                         : input.kind == WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE
                             ? "Staged BMSON region assigned to the active key lane."
                             : "Staged BMSON region assigned to BGM.";
    return compose_input_family_result_t::continue_batch(message);
}

auto toggle_bmson_restart(const WormUiComposeInputAction& input, bmson_compose_context_t& context)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, action, state, gesture_active] = context;
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    if (gesture_active)
        return "Finish or cancel the active Compose gesture before editing a BMSON reset.";
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end())
        return "The selected BMSON chart is no longer available.";
    std::optional<object_id_t> source_event;
    for (const auto& measure : chart->measures) {
        const auto event =
            std::ranges::find(measure.bgm_events, object_id_t{input.object_id}, &bgm_event_t::id);
        if (event != measure.bgm_events.end()) {
            source_event = event->sound_channel_event;
            break;
        }
    }
    if (!source_event)
        return "This BGM tile has no BMSON sound-channel slice to reset.";
    const auto applied = toggle_bmson_sound_channel_restart(
        history, action.compose_input_document_revision, state.chart, *source_event);
    if (!applied.diagnostics.empty())
        return compose_input_family_result_t::stop_with_feedback(
            applied.diagnostics.front().message,
            input_notification_severity(applied.diagnostics.front().severity),
            notification_source_t::edit);
    if (!applied.applied)
        return compose_input_family_result_t::stop_with_feedback(
            "BMSON restart state did not change.", notification_severity_t::info,
            notification_source_t::edit);
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound)
        return compose_input_family_result_t::stop_with_feedback(
            rebound.diagnostics.empty()
                ? "BMSON restart changed, but workspace rebinding failed."
                : rebound.diagnostics.front().message,
            notification_severity_t::error, notification_source_t::document);
    return compose_input_family_result_t::stop_with_feedback(
        "BMSON slice restart toggled.", notification_severity_t::success,
        notification_source_t::edit);
}

auto retained_bmson_source_geometry(const bmson_compose_context_t& context,
    const wormedit::chart_document_t& chart, wormedit::asset_id_t asset)
    -> std::optional<wormedit::bmson_source_geometry_t> {
    return retained_bmson_source_geometry(
        context.environment.document.history, context.environment.controls.cache, chart, asset);
}

auto bmson_stem_drop_matches(const wormedit::song_document_t& document,
    const wormedit::chart_document_t& chart, std::uint64_t resource, wormedit::object_id_t channel) -> bool {
    const auto bindings = wormedit::bmson_channel_resource_bindings(document, chart.id);
    const auto found = std::ranges::find(bindings, wormedit::resource_id_t{resource},
        &wormedit::bmson_channel_resource_binding_t::resource);
    return found != bindings.end() && !found->ambiguous && found->channel == channel;
}

auto apply_bmson_draw_point(const WormUiComposeInputAction& input,
    bmson_compose_context_t& context, const wormedit::chart_document_t& chart,
    wormedit::local_position_t position) -> wormedit::document_command_result_t {
    using namespace wormedit;
    const auto revision = context.action.compose_input_document_revision;
    if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW)
        return insert_bmson_packed_one_shot(context.environment.document.history, revision, chart.id,
                                            object_id_t{input.lane_id},
                                            resource_id_t{input.resource_id}, position);
    const auto lane = std::ranges::find(chart.bgm_source_lanes,
        object_id_t{input.lane_id}, &bgm_source_lane_t::id);
    if (lane == chart.bgm_source_lanes.end() || !lane->stem_channel)
        return {.revision = context.environment.document.history.document.revision,
                .diagnostics = {{.severity = diagnostic_severity_t::error,
                                 .code = diagnostic_code_t::invalid_id,
                                 .message = "The stem lane is no longer available."}}};
    const auto channel = std::ranges::find(chart.sound_channels, *lane->stem_channel,
        &sound_channel_t::id);
    if (channel == chart.sound_channels.end())
        return {.revision = context.environment.document.history.document.revision,
                .diagnostics = {{.severity = diagnostic_severity_t::error,
                                 .code = diagnostic_code_t::invalid_id,
                                 .message = "The stem channel is no longer available."}}};
    if (input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM &&
        !bmson_stem_drop_matches(context.environment.document.history.document, chart, input.resource_id,
                                 channel->id))
        return {.revision = context.environment.document.history.document.revision,
                .diagnostics = {{.severity = diagnostic_severity_t::error,
                                 .code = diagnostic_code_t::dangling_resource,
                                 .message = "Drop this stem source onto its own lane."}}};
    return wormedit::insert_bmson_stem_point(
        context.environment.document.history, revision, chart.id, channel->id, position,
        input.kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART,
        retained_bmson_source_geometry(context, chart, channel->source_clip.source));
}


void update_compose_viewport_pointer_input(const WormUiWorkspaceAction& ui_action,
                                           wormedit::viewport_input_controller_t& viewport_input,
                                           compose_scene_cache_t& scene_cache,
                                           double bgm_pane_width, double bgm_pane_left_pixels,
                                           double viewport_width_pixels) {
    using namespace wormedit;
    if (ui_action.compose_input_ownership != 0) {
        viewport_input.modal_opened();
        scene_cache.frame.viewport_autoscroll_active = false;
    } else if (!ui_action.shortcut_window_focused) {
        viewport_input.focus_lost();
        scene_cache.frame.viewport_autoscroll_active = false;
    } else {
        if (ui_action.compose_viewport_middle_click) {
            const auto middle_click_pane =
                bgm_pane_width > 0.0 &&
                        std::isfinite(ui_action.compose_viewport_middle_click_x_pixels) &&
                        ui_action.compose_viewport_middle_click_x_pixels >=
                            bgm_pane_left_pixels &&
                        ui_action.compose_viewport_middle_click_x_pixels <=
                            viewport_width_pixels
                    ? viewport_pane_t::bgm
                    : viewport_pane_t::chart;
            viewport_input.middle_click(middle_click_pane,
                                        ui_action.compose_viewport_middle_click_x_pixels,
                                        ui_action.compose_viewport_middle_click_y_pixels);
            scene_cache.frame.viewport_autoscroll_active = viewport_input.autoscroll_active();
            if (scene_cache.frame.viewport_autoscroll_active) {
                scene_cache.frame.viewport_autoscroll_anchor_x_pixels =
                    ui_action.compose_viewport_middle_click_x_pixels;
                scene_cache.frame.viewport_autoscroll_anchor_y_pixels =
                    ui_action.compose_viewport_middle_click_y_pixels;
            }
        }
        if (ui_action.compose_viewport_pointer_valid) {
            viewport_input.update_autoscroll_pointer(ui_action.compose_viewport_pointer_x_pixels,
                                                     ui_action.compose_viewport_pointer_y_pixels);
            scene_cache.frame.viewport_autoscroll_pointer_x_pixels =
                ui_action.compose_viewport_pointer_x_pixels;
            scene_cache.frame.viewport_autoscroll_pointer_y_pixels =
                ui_action.compose_viewport_pointer_y_pixels;
        }
    }
    if (ui_action.compose_input_count != 0 && !ui_action.compose_viewport_middle_click) {
        if (viewport_input.normal_edit_press()) {
            scene_cache.frame.viewport_autoscroll_active = false;
        }
    }
}

void update_compose_pointer_hover(const WormUiWorkspaceAction& ui_action, bool gesture_active,
                                  const wormedit::chart_document_t* chart,
                                  const wormedit::editor_interaction_view_model_t* interaction_view,
                                  std::uint64_t revision, double field_left_pixels,
                                  double field_width_pixels, compose_scene_cache_t& scene_cache,
                                  const wormedit::editor_preferences_t& preferences) {
    using namespace wormedit;
    scene_cache.frame.paint_pointer = ui_action.compose_has_pointer_hover && !gesture_active
        ? std::optional{std::pair{ui_action.compose_pointer_hover_x_pixels,
                                  ui_action.compose_pointer_hover_y_pixels}} : std::nullopt;
    if (!ui_action.compose_has_pointer_hover || gesture_active || !chart || !interaction_view) {
        scene_cache.frame.hover.reset();
    } else {
        if (!scene_cache.scene.hit_test_index) {
            auto built = build_editor_hit_test_index(*chart, revision);
            if (std::holds_alternative<editor_hit_test_index_t>(built)) {
                scene_cache.scene.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
            }
        }
        if (scene_cache.scene.hit_test_index) {
            scene_cache.frame.hover = hover_editor_object(
                *chart, *scene_cache.scene.hit_test_index, *interaction_view,
                {.x_pixels = ui_action.compose_pointer_hover_x_pixels,
                 .y_pixels = compose_source_y(ui_action.compose_pointer_hover_y_pixels,
                                              interaction_view->viewport_height_pixels,
                                              compose_field_downscroll(*chart, preferences)),
                 .field_left_pixels = field_left_pixels,
                 .field_width_pixels = field_width_pixels,
                 .hit_radius_pixels = 8.0,
                 .note_head_height_pixels = scene_cache.frame.preview_scene_request.note_head_height_pixels,
                 .split_stage_gutter_pixels = scene_cache.frame.preview_scene_request.split_stage_gutter_pixels,
                 .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
        }
    }
}


void arm_compose_note_layers(wormedit::editor_interaction_state_t& state,
                             const application_command_state_t& app,
                             const wormedit::chart_document_t& chart) {
    state.armed_note_layers.reset();
    if (app.project.semantic_layer.semantic_layer_target_none) {
        state.armed_note_layers = std::vector<wormedit::layer_id_t>{};
        return;
    }
    if (!app.project.semantic_layer.semantic_layer_target) return;
    const auto layer = std::ranges::find(chart.semantic_layers, *app.project.semantic_layer.semantic_layer_target,
                                        &wormedit::semantic_layer_t::id);
    if (layer != chart.semantic_layers.end() && !layer->locked)
        state.armed_note_layers = std::vector{layer->id};
}

auto semantic_layer_paint_armed(const application_command_state_t& app) -> bool {
    return app.project.compose_gesture.semantic_layer_paint_mode_active &&
           app.project.semantic_layer.semantic_layer_target.has_value() && !app.project.semantic_layer.semantic_layer_target_none;
}

auto semantic_layer_paint_target_editable(const application_command_state_t& app,
                                          const wormedit::semantic_layer_t* layer) -> bool {
    return !app.project.semantic_layer.semantic_layer_target_none && layer && layer->visible && !layer->locked;
}


auto apply_semantic_layer_paint_target(const WormUiComposeInputAction& input,
    compose_gesture_context_t& context, const semantic_layer_paint_target_t& target,
    const wormedit::semantic_layer_t* layer) -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, state, result, accepted] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    const auto target_id = target.note.value_or(target.bgm_event.value_or(object_id_t{}));
    if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN) {
        result = begin_compose_layer_paint(history, state,
            application_state.project.semantic_layer.semantic_layer_target_none,
            layer ? layer->id : layer_id_t{}, target_id, true, target.bgm_event,
            input.paint_intent == 2 ? semantic_layer_paint_mode_t::remove
                                    : semantic_layer_paint_mode_t::add);
        accepted = target.bgm_event ? "BGM semantic-layer paint stroke started."
                                    : "Semantic-layer paint stroke started.";
    } else if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE) {
        result = dispatch_editor_action(history, state,
            update_semantic_layer_paint_gesture_action_t{
                .note = target.note.value_or(object_id_t{}), .pointer_captured = true,
                .bgm_event = target.bgm_event});
        accepted = "Semantic-layer paint preview updated.";
    } else {
        result = finish_compose_layer_paint(history, state, target_id, target.bgm_event);
        accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                           : "Semantic-layer paint stroke released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

auto handle_empty_semantic_layer_paint_target(const WormUiComposeInputAction& input,
    compose_gesture_context_t& context) -> compose_input_family_result_t {
    using namespace wormedit;
    auto& history = context.environment.document.history;
    auto& state = context.state;
    auto& result = context.result;
    auto& accepted = context.accepted;
    if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN)
        return "Begin semantic-layer painting over a note or BGM event.";
    if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE) {
        result.accepted = true;
        accepted = "Semantic-layer paint moved between chart objects.";
    } else {
        result = finish_compose_layer_paint(history, state, {});
        accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                           : "Semantic-layer paint stroke released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

auto handle_inactive_semantic_layer_paint(compose_gesture_context_t& context)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& history = context.environment.document.history;
    auto& state = context.state;
    auto& result = context.result;
    auto& accepted = context.accepted;
    if (state.pending_semantic_layer_paint_gesture) {
        result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        accepted = "Semantic-layer paint stroke canceled because paint mode is inactive.";
    } else {
        result.accepted = true;
        accepted = "Semantic-layer paint input ignored because paint mode is inactive.";
    }
    return compose_input_family_result_t::continue_with_result();
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#endif

// Latest BPM change or timing anchor strictly before the beat row; that row is where the fit
// anchors.

auto focus_compose_note_label(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view)
        return "The selected chart field is no longer available.";
    if (!context.environment.controls.cache.scene.hit_test_index) {
        auto built =
            build_editor_hit_test_index(*chart, context.environment.document.history.document.revision);
        if (std::holds_alternative<editor_hit_test_index_t>(built))
            context.environment.controls.cache.scene.hit_test_index =
                std::get<editor_hit_test_index_t>(std::move(built));
    }
    if (!context.environment.controls.cache.scene.hit_test_index ||
        !context.environment.controls.cache.frame.interaction_view)
        return "The chart field hover index is unavailable.";
    const auto hover = hover_editor_object(
        *chart, *context.environment.controls.cache.scene.hit_test_index,
        *context.environment.controls.cache.frame.interaction_view,
        {.x_pixels = input.x_pixels,
         .y_pixels = context.input_source_y(input.y_pixels),
         .field_left_pixels = context.field_left_pixels,
         .field_width_pixels = context.field_width_pixels,
         .hit_radius_pixels = 8.0,
         .note_head_height_pixels = context.environment.controls.cache
                                        .frame.preview_scene_request.note_head_height_pixels,
         .split_stage_gutter_pixels = context.environment.controls.cache
                                          .frame.preview_scene_request.split_stage_gutter_pixels,
         .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
    if (!hover || hover->object.kind != editor_selection_object_kind_t::note)
        return "Double-click a note to edit its label.";
    result = dispatch_editor_action(context.environment.document.history, context.state,
                                    select_editor_notes_action_t{{hover->object.id}});
    if (!result)
        return compose_result_status(result, "Note label editor could not be focused.");
    context.environment.controls.cache.frame.focus_note_label_editor = true;
    accepted = "Note label editor focused.";
    return std::nullopt;
}

auto select_compose_bgm_event(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    if (input.object_id == 0)
        return "BGM event click did not identify an event.";
    const auto mode = context.selection_mode(input.selection_mode);
    if (!mode)
        return "BGM event click used an unknown selection mode.";
    result = dispatch_editor_action(
        context.environment.document.history, context.state,
        select_editor_objects_action_t{
            .objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{input.object_id}}},
            .mode = *mode,
            .request_audition = context.environment.commands.preferences.editor_preferences
                                    .audition_on_click});
    accepted = "BGM event selected.";
    return std::nullopt;
}


// One policy for timing pointers: clamp to the viewport, resolve a measure-local row,
// and reject failed snapping. Never silently publish an unsnapped edit.


// The Kiai rows a drag proposes, through the shared span geometry (span_manipulation.h). Edge 2
// creates an area between the press row and the pointer row.


// Length of a speed transition from its begin row to `end_row`, in the event's own clock: beats
// at four per standard measure, or the seconds the timing index spends between the two rows.

// A speed span's begin edge moves the event; its end edge sets the transition length. One undoable
// command per release.

// The recipe a recipe input names, if it still exists in the current chart.

// A recipe's begin edge, end edge, or whole span dragged: the previewed, snapped interval is
// committed as one history command through the core range drag (stale input and the timing lock
// reject there).

// Menu and key commands on one recipe: toggle enabled, materialize into events, or delete.


auto place_virtual_note_from_pointer(const WormUiComposeInputAction& input,
                                      compose_input_context_t& context,
                                      std::uint64_t row, wormedit::lane_id_t lane,
                                      wormedit::editor_interaction_result_t& result,
                                      std::string_view& accepted) -> void {
    using namespace wormedit;
    const auto pending = context.state.pending_hold;
    const auto sound = pending ? pending->sound : note_paint_sound(input);
    const auto geometry = compose_stem_geometry(context.environment.document.history, context.state,
                                                context.environment.controls.cache, sound);
    if (pending) {
        auto anchor = pending->anchor_canvas_row;
        if (!anchor) {
            const auto built = build_measure_index(*context.chart());
            if (const auto* index = std::get_if<measure_index_t>(&built))
                anchor = absolute_row(*index, pending->position);
        }
        if (!anchor) {
            result = dispatch_editor_action(context.environment.document.history, context.state,
                                            cancel_editor_gesture_action_t{});
            accepted = "Hold anchor is unavailable; gesture canceled.";
            return;
        }
        result = dispatch_editor_action(
            context.environment.document.history, context.state,
            place_virtual_bms_hold_action_t{*anchor, row, pending->lane, sound, geometry});
        accepted = "Hold gesture released.";
    } else {
        result = dispatch_editor_action(context.environment.document.history, context.state,
                                        place_virtual_bms_tap_action_t{row, lane, sound, geometry});
        accepted = context.state.pending_hold ? "Hold gesture started; click its end row."
                                              : "Note placed in virtual BMS measure.";
    }
    advance_sound_after_paint_commit(result, sound, result.created_note.has_value());
    audition_created_note(
        context.environment.commands.preferences.editor_preferences.audition_on_click,
        context.environment.document.history, context.state, result);
}

auto finish_virtual_hold_at_authored_row(const WormUiComposeInputAction& input,
                                         compose_input_context_t& context,
                                         const wormedit::chart_document_t& chart,
                                         wormedit::local_position_t position, wormedit::lane_id_t lane,
                                         wormedit::editor_interaction_result_t& result,
                                         std::string_view& accepted) -> bool {
    if (!context.state.pending_hold || !context.state.pending_hold->anchor_canvas_row) return false;
    const auto built = wormedit::build_measure_index(chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    const auto row = index ? wormedit::absolute_row(*index, position) : std::nullopt;
    if (!row) return false;
    place_virtual_note_from_pointer(input, context, *row, lane, result, accepted);
    return true;
}

// A plain click only selects unless the paint tool is active (or a hold is awaiting its end
// click); the Select tool and Timing focus never place notes.
auto pointer_click_places_notes(const compose_input_context_t& context) -> bool {
    if (!wormedit::ui::compose_notes_editable(
            compose_mode_of(context.environment.commands)))
        return false;
    return context.environment.commands.project.compose_gesture.note_paint_mode_active ||
           context.state.pending_hold;
}

auto select_or_place_virtual_pointer(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  const wormedit::chart_document_t& chart,
                                  const wormedit::editor_interaction_view_model_t& view,
                                  wormedit::editor_area_selection_mode_t mode,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto virtual_hit =
        context.virtual_bms_hit_at(chart, view, input.x_pixels, input.y_pixels);
    if (!virtual_hit)
        return "Click inside an editable lane to select a note or position.";
    if (mode == editor_area_selection_mode_t::replace && pointer_click_places_notes(context)) {
        place_virtual_note_from_pointer(input, context, virtual_hit->first, virtual_hit->second,
                                        result, accepted);
        return std::nullopt;
    }
    const auto built = build_virtual_canvas(chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    const auto measure = canvas ? canvas->measure_containing(virtual_hit->first) : std::nullopt;
    if (!measure)
        return "The clicked BMS measure is unavailable.";
    auto candidate = context.state;
    result = dispatch_editor_action(context.environment.document.history, candidate,
                                    select_bms_canvas_measure_action_t{measure->number});
    if (result.accepted) {
        restore_cursor_pose(context.state, capture_cursor_pose(candidate));
        context.state.active_lane = virtual_hit->second;
    }
    accepted = "BMS measure selected.";
    return std::nullopt;
}

auto select_compose_same_sound(const WormUiComposeInputAction& input, compose_input_context_t& context,
    wormedit::editor_interaction_result_t& result, std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    if (!chart || !context.input_is_current()) return "Sound selection source is stale.";
    const auto mode = context.selection_mode(input.selection_mode);
    if (!mode) return "Sound selection mode is invalid.";
    auto request = compose_same_sound_request(
        context.environment.document.history.document, *chart, context.state,
        context.environment.commands,
        input.kind == WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE, *mode,
        input.resource_id ? std::nullopt : std::optional{object_id_t{input.object_id}},
        input.resource_id ? std::optional{resource_id_t{input.resource_id}} : std::nullopt);
    if (const auto* error = std::get_if<std::string>(&request)) return *error;
    result = dispatch_editor_action(context.environment.document.history, context.state,
                                    std::get<select_editor_objects_action_t>(std::move(request)));
    accepted = "Matching sounds selected.";
    return std::nullopt;
}

auto select_compose_pointer_target(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto mode = context.selection_mode(input.selection_mode);
    if (!mode) return "Compose pointer selection used an unknown selection mode.";
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view)
        return "The selected chart field is no longer available.";
    const auto target = context.hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!target)
        return select_or_place_virtual_pointer(input, context, *chart, *view, *mode, result, accepted);
    const auto places_notes = pointer_click_places_notes(context);
    if (*mode == editor_area_selection_mode_t::replace && places_notes &&
        finish_virtual_hold_at_authored_row(input, context, *chart, target->position, target->lane,
                                            result, accepted))
        return std::nullopt;
    context.state.active_lane = target->lane;
    // A click never moves the cursor: it keeps its fixed station and only keys and the
    // transport move it.
    const bool clicked_hold_end = context.state.pending_hold &&
                                  !context.state.pending_hold->gesture_source;
    if (*mode == editor_area_selection_mode_t::replace && places_notes &&
        (!target->note || clicked_hold_end)) {
        place_note_from_pointer(input, context, *target, result, accepted);
        return std::nullopt;
    }
    const auto selection =
        target->note
            ? select_editor_objects_action_t{{{editor_selection_object_kind_t::note,
                                               *target->note}},
                                             *mode,
                                             context.environment.commands
                                                 .preferences.editor_preferences.audition_on_click}
            : select_editor_objects_action_t{{}, *mode, false};
    result = dispatch_editor_action(context.environment.document.history, context.state, selection);
    accepted = target->note ? "Note selected." : "Selection cleared.";
    return std::nullopt;
}

auto select_compose_bga_event(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto mode = context.selection_mode(input.selection_mode);
    if (!chart || !mode || input.object_id == 0 || input.timing_chart_id != chart->id.value ||
        input.timing_document_revision != context.environment.document.history.document.revision)
        return "BGA event selection is stale or invalid.";
    const auto event = std::ranges::find(chart->bga_events, object_id_t{input.object_id},
                                         &bga_event_t::id);
    if (event == chart->bga_events.end() ||
        !editor_bga_lane_index(event->kind))
        return "That BGA event is no longer available in the event lane.";
    result = dispatch_editor_action(
        context.environment.document.history, context.state,
        select_editor_objects_action_t{{{editor_selection_object_kind_t::bga_event, event->id}},
                                       *mode});
    accepted = "BGA event selected.";
    return std::nullopt;
}

auto move_compose_bga_event(const WormUiComposeInputAction& input,
                            compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    const auto object = editor_selection_object_t{editor_selection_object_kind_t::bga_event,
                                                   object_id_t{input.object_id}};
    if (!chart || !view || input.object_id == 0 || input.timing_chart_id != chart->id.value ||
        input.timing_document_revision != context.environment.document.history.document.revision ||
        std::ranges::find(context.state.selected_objects, object) ==
            context.state.selected_objects.end() ||
        !std::isfinite(input.y_pixels))
        return "Select the current BGA event before moving it.";
    const auto y = std::clamp(input.y_pixels, 0.0,
                              std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto revision = context.ui_action.compose_input_document_revision;
    const auto snap = edit_snap_denominator(*chart, context.state);
    const auto position = context.position_at_compose_y_unrestricted(*chart, *view, y);
    const auto target_kind = input.bga_lane_kind_valid || input.bga_lane_kind != 0
        ? std::optional{static_cast<visual_event_kind_t>(input.bga_lane_kind)} : std::nullopt;
    if (target_kind && (!editor_bga_lane_index(*target_kind) || *target_kind == visual_event_kind_t::video))
        return "Unsupported BGA destination lane.";
    document_command_result_t moved;
    if (position) {
        moved = apply_bga_event_move(context.environment.document.history, revision,
                                     {.chart = chart->id,
                                      .event = object.id,
                                      .position = *position,
                                      .snap_denominator = snap,
                                      .bga_kind = target_kind});
    } else if (const auto row = context.virtual_bms_row_at(*chart, *view, y)) {
        moved = apply_virtual_bms_bga_event_move(context.environment.document.history, revision,
                                                 {.chart = chart->id,
                                                  .event = object.id,
                                                  .canvas_row = *row,
                                                  .snap_denominator = snap,
                                                  .bga_kind = target_kind});
    } else {
        return "BGA event move has no valid destination row.";
    }
    result.accepted = moved.applied;
    result.document_changed = moved.applied;
    result.diagnostics = moved.diagnostics;
    accepted = "BGA event moved.";
    return std::nullopt;
}

auto move_compose_bookmark(const WormUiComposeInputAction& input,
                           compose_input_context_t& context,
                           wormedit::editor_interaction_result_t& result,
                           std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view || input.object_id == 0 || input.timing_chart_id != chart->id.value ||
        input.timing_document_revision != context.environment.document.history.document.revision)
        return "The bookmark move is stale.";
    const auto found = std::ranges::find(chart->bookmarks, object_id_t{input.object_id},
                                         &bookmark_t::id);
    if (found == chart->bookmarks.end() || found->anchor != marker_anchor_t::beat || !found->position)
        return "That beat-anchored bookmark is no longer available.";
    const auto measures = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&measures);
    const auto row = index ? timing_pointer_row(context, *chart, *view, *index, input.y_pixels)
                           : std::nullopt;
    const auto position = row ? position_at_absolute_row(*index, *row) : std::nullopt;
    if (!position) return "Bookmark move has no valid destination row.";
    if (*position == *found->position) {
        result.accepted = true;
        accepted = "Bookmark stayed on its row.";
        return std::nullopt;
    }
    const auto moved = apply_marker_edit(
        context.environment.document.history, context.ui_action.compose_input_document_revision,
        marker_edit_action_t{update_bookmark_action_t{
            chart->id, found->id, found->label, marker_anchor_t::beat, *position, std::nullopt}});
    result.accepted = moved.applied;
    result.document_changed = moved.applied;
    result.diagnostics = moved.diagnostics;
    accepted = "Bookmark moved.";
    return std::nullopt;
}

auto select_compose_bms_measure(const WormUiComposeInputAction& input,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view || chart->source_family != format_family_t::bms ||
        !std::isfinite(input.y_pixels) || input.y_pixels < 0.0 ||
        input.y_pixels >= view->viewport_height_pixels || input.x_pixels < 0.0 ||
        input.x_pixels >= compose_measure_rail_width_pixels)
        return "Click inside a BMS measure number to select its measure.";
    const auto built = build_virtual_canvas(*chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    if (!canvas)
        return "The BMS measure canvas is unavailable.";
    const auto exact_row =
        (static_cast<long double>(view->viewport_scroll_pixels) +
         compose_source_y(
             input.y_pixels, view->viewport_height_pixels,
             compose_field_downscroll(
                 *chart, context.environment.commands.preferences.editor_preferences))) *
        chart->divisions_per_standard_measure / view->pixels_per_standard_measure;
    if (!std::isfinite(exact_row) || exact_row < 0.0L ||
        exact_row >= static_cast<long double>(canvas->total_rows))
        return "The measure number is outside #000-#999.";
    const auto row =
        std::min(static_cast<std::uint64_t>(std::floor(exact_row + 0.5L)), canvas->total_rows - 1);
    const auto measure = canvas->measure_containing(row);
    if (!measure)
        return "The selected BMS measure is unavailable.";
    result = dispatch_editor_action(context.environment.document.history, context.state,
                                    select_bms_canvas_measure_action_t{measure->number});
    accepted = "BMS measure selected.";
    return std::nullopt;
}


// Every placement path ends here: the Sounds/BGA dock advances to the next eligible resource.


auto dispatch_compose_undo_redo(const WormUiComposeInputAction& input,
                                compose_input_context_t& context) -> compose_input_family_result_t {
    using namespace wormedit;
    (void)dispatch_editor_action(context.environment.document.history, context.state,
                                 cancel_editor_gesture_action_t{});
    (void)dispatch_editor_action(context.environment.document.history, context.state,
                                 select_editor_notes_action_t{});
    const auto undo = input.kind == WORM_UI_COMPOSE_INPUT_UNDO;
    const auto changed = undo ? undo_document_command(context.environment.document.history)
                              : redo_document_command(context.environment.document.history);
    const auto status = changed ? (undo ? "Undo applied." : "Redo applied.")
                                : (undo ? "Nothing to undo." : "Nothing to redo.");
    if (!changed)
        return compose_input_family_result_t::continue_batch(
            status, notification_severity_t::info, notification_source_t::edit);
    const auto rebound =
        rebind_workspace(context.environment.document.workspace, context.environment.document.history);
    if (!rebound)
        return compose_input_family_result_t::stop_with_feedback(
            rebound.diagnostics.empty() ? "History changed, but workspace rebinding failed."
                                        : rebound.diagnostics.front().message,
            notification_severity_t::error, notification_source_t::document);
    return compose_input_family_result_t::stop_with_feedback(
        status, notification_severity_t::success, notification_source_t::edit);
}

auto retire_interrupted_compose_input(wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state, wormedit::viewport_input_controller_t& viewport,
    wormedit::editor_navigation_input_controller_t& navigation,
    application_command_state_t& application, compose_scene_cache_t& cache)
    -> wormedit::editor_interaction_result_t {
    viewport.focus_lost();
    navigation.focus_lost();
    cache.clear_beat_fit_capture();
    cache.timing.offset_fit_drag.reset();
    application.project.compose_gesture.bga_draw_stroke.reset();
    return wormedit::dispatch_editor_action(history, state, wormedit::cancel_editor_gesture_action_t{});
}

auto cancel_compose_gesture(compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> compose_input_family_result_t {
    result = retire_interrupted_compose_input(context.environment.document.history, context.state,
                                              context.environment.controls.viewport,
                                              context.environment.controls.navigation,
                                              context.environment.commands,
                                              context.environment.controls.cache);
    if (context.ui_action.compose_input_ownership == 1)
        context.environment.controls.navigation.modal_opened();
    else if (context.ui_action.compose_input_ownership == 2)
        context.environment.controls.navigation.text_input_started();
    accepted = "Compose gesture canceled.";
    return compose_input_family_result_t::continue_with_result();
}

auto compose_input_rejection(std::string message,
    const wormedit::editor_interaction_result_t& result) -> compose_input_family_result_t {
    return compose_input_family_result_t::stop_with_feedback(std::move(message),
        result.diagnostics.empty() ? wormedit::notification_severity_t::warning
            : input_notification_severity(result.diagnostics.front().severity),
        wormedit::notification_source_t::input);
}

auto reveal_compose_bga_lane_input(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> compose_input_family_result_t {
    if (context.gesture_active)
        return compose_input_rejection("Finish or cancel the active gesture before adding a BGA lane.", result);
    const auto lane = wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(input.bga_lane_kind));
    if (!lane || input.bga_lane_kind == static_cast<std::uint8_t>(wormedit::visual_event_kind_t::video))
        return compose_input_rejection("Unsupported BGA lane kind.", result);
    context.state.bga_revealed_lane_mask |= static_cast<std::uint8_t>(1U << *lane);
    context.state.bga_lane_visible = true;
    result.accepted = true;
    accepted = "BGA lane shown.";
    return compose_input_family_result_t::continue_with_result();
}

// Kiai area and SV recipe inputs from the timing lanes. Returns nothing for any other input.
auto dispatch_timing_lane_area_input(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted)
    -> std::optional<compose_input_family_result_t> {
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT:
        context.environment.commands.project.timing_recipe.kiai_selected_head = input.object_id;
        result.accepted = true;
        accepted = input.object_id != 0 ? "Kiai area selected." : "Kiai selection cleared.";
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT:
        if (const auto error = edit_compose_kiai_span(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT:
        if (const auto error = edit_compose_speed_span(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_RECIPE_SELECT:
        if (context.environment.commands.project.timing_recipe.recipe_selected != input.object_id)
            context.environment.commands.project.timing_recipe.recipe_preview.reset();
        context.environment.commands.project.timing_recipe.recipe_selected = input.object_id;
        result.accepted = true;
        accepted = input.object_id != 0 ? "SV recipe selected." : "SV recipe selection cleared.";
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT:
        if (const auto error = edit_compose_recipe_range(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND:
        if (const auto error = run_compose_recipe_command(input.object_id, input.span_edge, context,
                                                          result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    default:
        return std::nullopt;
    }
}

auto dispatch_compose_bga_input(const WormUiComposeInputAction& input,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted)
    -> std::optional<compose_input_family_result_t> {
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK:
        if (const auto error = select_compose_bga_event(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE:
        if (const auto error = move_compose_bga_event(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL:
        return reveal_compose_bga_lane_input(input, context, result, accepted);
    default: return std::nullopt;
    }
}

auto dispatch_unhandled_compose_input(const WormUiComposeInputAction& input,
                                      compose_input_context_t& context,
                                      wormedit::editor_interaction_result_t& result,
                                      std::string_view& accepted) -> compose_input_family_result_t {
    if (const auto status = dispatch_compose_offset_fit_input(input, context))
        return *status;
    if (const auto status = dispatch_compose_beat_fit_input(
            input, context.environment.document.history, context.environment.document.workspace,
            context.state, context.environment.commands,
            context.environment.controls.cache, context.input_chart))
        return *status;
    if (const auto handled = dispatch_timing_lane_area_input(input, context, result, accepted))
        return *handled;
    if (const auto handled = dispatch_compose_bga_input(input, context, result, accepted))
        return *handled;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_CLICK:
        if (const auto error = click_compose_waveform(input, context, result, accepted)) return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK:
        if (const auto error = seek_compose_waveform(input, context, result, accepted))
            return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK:
        if (const auto error = focus_compose_note_label(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK:
        if (const auto error = select_compose_bgm_event(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT:
        if (const auto error = select_compose_timing_marker(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE:
        if (const auto error = move_compose_timing_selection(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE:
        if (const auto error = move_compose_bookmark(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE:
    case WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART:
        if (const auto error = select_compose_same_sound(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_POINTER_CLICK:
        if (const auto error = select_compose_pointer_target(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_MEASURE_SELECT:
        if (const auto error = select_compose_bms_measure(input, context, result, accepted))
            return compose_input_rejection(*error, result);
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE:
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE:
        return dispatch_resource_pointer(input, context, result, accepted);
    case WORM_UI_COMPOSE_INPUT_UNDO:
    case WORM_UI_COMPOSE_INPUT_REDO:
        return dispatch_compose_undo_redo(input, context);
    case WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE:
        return cancel_compose_gesture(context, result, accepted);
    case WORM_UI_COMPOSE_INPUT_NONE:
    default:
        return {};
    }
}

// Inputs that create, move, or reassign notes; Timing focus rejects them.
auto compose_input_edits_notes(WormUiComposeInputKind kind) -> bool {
    switch (kind) {
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE:
    case WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE:
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE:
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE:
    case WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW:
    case WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE:
    case WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART:
    case WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE:
        return true;
    default:
        return false;
    }
}

auto compose_input_notification_source(WormUiComposeInputKind kind)
    -> wormedit::notification_source_t {
    using namespace wormedit;
    if (compose_input_edits_notes(kind) || kind == WORM_UI_COMPOSE_INPUT_DELETE_SELECTION ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE ||
        kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART ||
        kind == WORM_UI_COMPOSE_INPUT_UNDO || kind == WORM_UI_COMPOSE_INPUT_REDO ||
        kind == WORM_UI_COMPOSE_INPUT_PASTE_COMMIT ||
        kind == WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT ||
        kind == WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT ||
        kind == WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT ||
        kind == WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND)
        return notification_source_t::edit;
    return notification_source_t::input;
}

auto input_feedback_source(WormUiComposeInputKind kind,
                           wormedit::notification_source_t explicit_source,
                           wormedit::notification_severity_t severity)
    -> wormedit::notification_source_t {
    if (severity == wormedit::notification_severity_t::hint)
        return wormedit::notification_source_t::input;
    return explicit_source == wormedit::notification_source_t::input
               ? compose_input_notification_source(kind)
               : explicit_source;
}

auto input_result_severity(const WormUiComposeInputAction& input,
                           const wormedit::editor_interaction_result_t& result)
    -> wormedit::notification_severity_t {
    using namespace wormedit;
    if (!result.accepted)
        return result.diagnostics.empty()
                   ? notification_severity_t::warning
                   : input_notification_severity(result.diagnostics.front().severity);
    if (input.kind == WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD ||
        input.kind == WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD ||
        input.kind == WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND)
        return notification_severity_t::hint;
    if (result.document_changed) return notification_severity_t::success;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_DELETE_SELECTION:
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE:
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE:
    case WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT:
    case WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT:
    case WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT:
    case WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND:
        return notification_severity_t::info;
    default: return notification_severity_t::hint;
    }
}

// Explicit note/BGM conversion and the drop of BGM events on the playfield.
auto dispatch_note_to_bgm_input(const WormUiComposeInputAction& input, compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result, std::string_view& accepted)
    -> std::optional<compose_input_family_result_t> {
    if (input.kind == WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE) {
        // A drop on the playfield assigns the selected objects through the shared core action.
        const auto dragged = input.object_id != 0 ? std::optional<wormedit::object_id_t>{wormedit::object_id_t{input.object_id}}
                                                  : std::nullopt;
        if (!context.input_is_current() || input.lane_id == 0) return std::nullopt;
        const auto outcome = wormedit::sys::move_selected_bgm_to_lane(
            context.environment.document.history, context.state, wormedit::lane_id_t{input.lane_id},
            dragged, input.successive);
        if (!outcome.payload.handled) return std::nullopt;
        result.accepted = outcome.command.applied;
        result.document_changed = outcome.command.applied;
        result.diagnostics = outcome.command.diagnostics;
        accepted = outcome.command.applied
            ? (input.successive ? "Next selected object assigned to lane."
                                : "BGM events moved to the lane.")
            : "The selected objects could not move to that lane.";
        return compose_input_family_result_t::continue_with_result();
    }
    return std::nullopt;
}

// A lane key at a cursor inside a Warp would place a playable head in skipped rows. Holds that
// started earlier may still end there, and a key on a selection only reassigns lanes.
auto lane_key_places_inside_warp(const WormUiComposeInputAction& input,
                                 const compose_input_context_t& context) -> bool {
    const auto& state = context.state;
    const auto press = input.kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS;
    if ((!press && input.kind != WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE) ||
        !context.environment.controls.cache.timing.dead_spans || !context.input_chart ||
        state.virtual_bms_cursor_row)
        return false;
    const auto selected = !state.selected_notes.empty() || !state.selected_objects.empty();
    const auto pending = state.pending_hold || !state.additional_keyboard_holds.empty();
    if (press ? selected : pending) return false;
    const auto built = wormedit::build_measure_index(*context.input_chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    const auto row = index ? wormedit::absolute_row(*index, state.cursor) : std::nullopt;
    return row && wormedit::head_blocked_by_warp(*context.input_chart, *index, *row, state.armed_note_kind);
}

auto dispatch_compose_input_families(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> compose_input_family_result_t {
    if (compose_input_edits_notes(input.kind) &&
        !wormedit::ui::compose_notes_editable(
            compose_mode_of(context.environment.commands)))
        return compose_input_family_result_t::continue_batch(
            "Note editing is unavailable in Timing focus.");
    if (lane_key_places_inside_warp(input, context))
        return compose_input_family_result_t::stop_with_feedback("Notes cannot be placed inside a Warp.",
            wormedit::notification_severity_t::warning, wormedit::notification_source_t::input);
    if (const auto to_bgm = dispatch_note_to_bgm_input(input, context, result, accepted))
        return *to_bgm;
    // Legacy compatibility input: no UI emits it; never build a second Delete request.
    if (input.kind == WORM_UI_COMPOSE_INPUT_DELETE_SELECTION) {
        result = wormedit::delete_selection_all(
            context.environment.document.history, context.state,
            context.environment.document.history.document.revision,
            {.kiai_head =
                 wormedit::object_id_t{
                     context.environment.commands.project.timing_recipe.kiai_selected_head},
             .recipe =
                 wormedit::recipe_id_t{
                     context.environment.commands.project.timing_recipe.recipe_selected},
             .timing_locked = context.environment.commands.project.timing_recipe.timing_locked});
        if (result.document_changed) {
            context.environment.commands.project.timing_recipe.kiai_selected_head = 0;
            context.environment.commands.project.timing_recipe.recipe_selected = 0;
        }
        accepted = result.document_changed ? "Selection deleted." : "No object at the cursor.";
        return compose_input_family_result_t::continue_with_result();
    }
    const auto keyboard = dispatch_compose_keyboard_input(
        input, context.environment.document.history, context.environment.document.workspace, context.state,
        context.environment.controls.navigation,
        context.environment.commands, result, accepted, context.notifications,
        context.environment.controls.cache);
    if (keyboard.handled)
        return keyboard;
    const auto position = [&context](const auto& chart, const auto& view, double y) {
        return context.position_at_compose_y(chart, view, y);
    };
    const auto virtual_row = [&context](const auto& chart, const auto& view, double y) {
        return context.virtual_bms_row_at(chart, view, y);
    };
    const auto bmson =
        dispatch_bmson_compose_input(context.ui_action, input, context.environment, context.state,
                                     context.gesture_active, position, virtual_row);
    if (bmson.handled)
        return bmson;
    const auto hit_target = [&context, &input](const auto& chart, const auto& view, double x, double y) {
        return context.note_move_hit_target_at(input.kind, chart, view, x, y);
    };
    const auto bgm_event_at = [&context](double x,
                                         double y) -> std::optional<wormedit::object_id_t> {
        const auto& scene = context.environment.controls.cache.scene.scene_window.scene;
        if (!scene)
            return std::nullopt;
        for (auto event = scene->bgm_events.rbegin(); event != scene->bgm_events.rend(); ++event) {
            const auto& bounds = event->bounds;
            if (x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom)
                return event->event;
        }
        return std::nullopt;
    };
    const auto gesture =
        dispatch_compose_gesture_input(input, context.environment, context.state, result, accepted,
                                       hit_target, bgm_event_at, position);
    if (gesture.handled)
        return gesture;
    const auto selection_mode = [&context](WormUiComposeSelectionMode mode) {
        return context.selection_mode(mode);
    };
    const auto source_y = [&context](double y) { return context.input_source_y(y); };
    const auto selection = dispatch_compose_selection_input(
        input, context.environment.document.history, context.state, result, accepted, selection_mode,
        source_y, context.field_left_pixels, context.field_width_pixels,
        context.environment.controls.cache.scene.area_object_geometry);
    return selection;
}

auto dispatch_compose_paste_input(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context)
    -> std::optional<compose_input_family_result_t> {
    auto& application = context.environment.commands;
    if (input.kind == WORM_UI_COMPOSE_INPUT_PASTE_CANCEL) {
        application.project.clipboard.paste_session.reset();
        return compose_input_family_result_t::stop_with_feedback("Paste preview cancelled.",
            wormedit::notification_severity_t::hint, wormedit::notification_source_t::input);
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_PASTE_COMMIT) {
        std::string status;
        auto severity = wormedit::notification_severity_t::warning;
        const auto committed = commit_compose_paste(application, context.environment.document.history,
                                                    context.state, status, &severity);
        post_input_feedback(context.notifications, severity, wormedit::notification_source_t::edit, status);
        if (committed) {
            const auto rebound = wormedit::rebind_workspace(context.environment.document.workspace,
                                                            context.environment.document.history);
            if (!rebound)
                post_input_feedback(context.notifications, wormedit::notification_severity_t::error,
                    wormedit::notification_source_t::document,
                    rebound.diagnostics.empty() ? "Compose edit applied, but workspace rebinding failed."
                                                : rebound.diagnostics.front().message);
        }
        auto feedback = compose_input_family_result_t::stop_with_feedback(
            std::move(status), severity, wormedit::notification_source_t::edit);
        feedback.notification_posted = context.notifications != nullptr;
        return feedback;
    }

    if (input.kind != WORM_UI_COMPOSE_INPUT_PASTE_UPDATE) return std::nullopt;
    if (!application.project.clipboard.paste_session || !context.input_chart)
        return compose_input_family_result_t::continue_batch({});
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!view)
        return compose_input_family_result_t::stop_with_feedback(
            "The paste field is unavailable.", wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::edit);
    const auto raw = context.position_at_compose_y(*context.input_chart, *view, input.y_pixels);
    const auto position = raw ? wormedit::resolve_resource_placement_position(
        *context.input_chart, *raw,
        edit_snap_denominator(*context.input_chart, context.state)) : std::nullopt;
    if (!position) {
        application.project.clipboard.paste_session->anchor_available = false;
        application.project.clipboard.paste_session->anchor_error = "The paste anchor is outside the chart.";
        return compose_input_family_result_t::stop_with_feedback(
            application.project.clipboard.paste_session->anchor_error, wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::edit);
    }
    application.project.clipboard.paste_session->anchor_available = true;
    application.project.clipboard.paste_session->pointer_cursor = context.state.cursor;
    (void)update_compose_paste(application, context.environment.document.history.document,
                               context.state.chart, *position);
    return compose_input_family_result_t::continue_batch({});
}

auto append_bga_draw_targets(application_command_state_t::bga_draw_stroke_t& stroke,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t& index,
    const wormedit::editor_interaction_state_t& state, uint64_t row, uint8_t kind) -> bool {
    const auto denominator = wormedit::edit_snap_denominator(chart, state);
    if (!denominator) return false;
    const auto [first, last] = std::minmax(stroke.last_row, row);
    auto current = first;
    for (;;) {
        if (stroke.visited.emplace(current, kind).second) {
            if (stroke.targets.size() >= 50'000) return false;
            const auto position = wormedit::position_at_absolute_row(index, current);
            if (!position) return false;
            stroke.targets.push_back({*position, static_cast<wormedit::visual_event_kind_t>(kind)});
        }
        if (current == last) break;
        const auto next = wormedit::adjacent_snap_canvas_row(chart, index, current,
            denominator, index.total_rows, true);
        if (!next || *next > last) return false;
        current = *next;
    }
    stroke.last_row = row;
    return true;
}

auto bga_draw_geometry(const compose_input_context_t& context, const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::measure_index_t, wormedit::diagnostic_list_t> {
    const auto& cache = context.environment.controls.cache;
    if (cache.scene.index && cache.scene.chart == chart.id &&
        cache.scene.revision == context.environment.document.history.document.revision &&
        cache.scene.scene_document_identity == &context.environment.document.history.document &&
        cache.scene.scene_chart_identity == &chart)
        return cache.scene.index->measures;
    return wormedit::build_measure_index(chart);
}

auto bga_draw_row(const WormUiComposeInputAction& input, const compose_input_context_t& context,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t& index)
    -> std::optional<uint64_t> {
    const auto inspected = wormedit::inspect_editor_interaction(
        context.environment.document.history, context.state,
        {chart.id, context.environment.document.history.document.revision, &index});
    const auto* view = std::get_if<wormedit::editor_interaction_view_model_t>(&inspected);
    if (!view) return std::nullopt;
    const auto raw = wormedit::editor_position_at_y_pixels(chart, index, *view,
        context.input_source_y(input.y_pixels));
    const auto position = raw ? wormedit::snap_position(chart, index, *raw,
        wormedit::edit_snap_denominator(chart, context.state)) : std::nullopt;
    return position ? wormedit::absolute_row(index, *position) : std::nullopt;
}

auto begin_bga_draw(const WormUiComposeInputAction& input, compose_input_context_t& context,
    const wormedit::chart_document_t& chart) -> bool {
    auto& pending = context.environment.commands.project.compose_gesture.bga_draw_stroke;
    pending.reset();
    if (input.paint_intent != 1 && input.paint_intent != 2) return false;
    const auto bgm = input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    if (input.paint_intent != 2 && !bgm) {
        const auto& resources = context.environment.document.history.document.resources;
        const auto resource = std::ranges::find(resources, wormedit::resource_id_t{input.resource_id},
            &wormedit::resource_entry_t::id);
        if (resource == resources.end() || std::ranges::find(resource->roles,
            wormedit::resource_role_t::bga) == resource->roles.end()) return false;
    }
    auto built = bga_draw_geometry(context, chart);
    auto* index = std::get_if<wormedit::measure_index_t>(&built);
    if (!index) return false;
    pending = application_command_state_t::bga_draw_stroke_t{
        .chart = chart.id,
        .revision = context.environment.document.history.document.revision,
        .resource = input.resource_id ? std::optional{wormedit::resource_id_t{input.resource_id}}
                                      : std::nullopt,
        .erase = input.paint_intent == 2,
        .geometry = std::move(*index)};
    return true;
}

auto apply_bgm_draw_stroke(compose_input_context_t& context,
    const application_command_state_t::bga_draw_stroke_t& stroke, uint64_t lane)
    -> wormedit::document_command_result_t {
    std::vector<wormedit::local_position_t> positions;
    positions.reserve(stroke.targets.size());
    for (const auto& target : stroke.targets) positions.push_back(target.position);
    return wormedit::apply_bgm_stroke(context.environment.document.history, stroke.revision,
                                      stroke.chart, wormedit::object_id_t{lane}, stroke.resource,
                                      positions, stroke.erase);
}

auto finish_bga_draw(const WormUiComposeInputAction& input, compose_input_context_t& context,
    wormedit::editor_interaction_result_t& result) -> void {
    auto& pending = context.environment.commands.project.compose_gesture.bga_draw_stroke;
    auto stroke = std::move(*pending);
    pending.reset();
    const auto bgm = input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    const auto applied = bgm ? apply_bgm_draw_stroke(context, stroke, input.lane_id)
                             : wormedit::apply_bga_stroke(
                                   context.environment.document.history, stroke.revision, stroke.chart,
                                   stroke.resource, stroke.targets, stroke.erase);
    result.accepted = applied.applied || applied.diagnostics.empty();
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    // BMSON strokes change retained sound channels, which the workspace binds.
    if (bgm && applied.applied) {
        const auto rebound = wormedit::rebind_workspace(context.environment.document.workspace,
                                                        context.environment.document.history);
        if (!rebound && !rebound.diagnostics.empty())
            result.diagnostics.push_back(rebound.diagnostics.front());
    }
    if (applied.applied && stroke.resource && !stroke.erase && !bgm)
        advance_dock_after_placement(context.environment.document.history.document, *stroke.resource,
                                     wormedit::resource_role_t::bga);
}

auto bga_draw_lane_valid(const WormUiComposeInputAction& input) -> bool {
    if (input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) return input.lane_id != 0;
    return input.bga_lane_kind_valid &&
        wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(input.bga_lane_kind));
}

auto bga_draw_stop(std::string_view text)
    -> compose_input_family_result_t {
    return compose_input_family_result_t::stop_with_feedback(std::string{text},
        wormedit::notification_severity_t::warning, wormedit::notification_source_t::edit);
}

auto dispatch_bga_draw_input(const WormUiComposeInputAction& input,
    compose_input_context_t& context, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted) -> std::optional<compose_input_family_result_t> {
    auto& pending = context.environment.commands.project.compose_gesture.bga_draw_stroke;
    if (input.kind == WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE) {
        pending.reset();
        return std::nullopt;
    }
    const auto stroke_input = input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN ||
        input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE ||
        input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
    const auto bgm = input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    if (!stroke_input || (input.resource_role != WORM_UI_COMPOSE_RESOURCE_ROLE_BGA && !bgm))
        return std::nullopt;
    const auto* chart = context.chart();
    const auto beginning = input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN;
    if (!chart || !context.environment.commands.project.compose_gesture.note_paint_mode_active ||
        !wormedit::ui::compose_notes_editable(
            compose_mode_of(context.environment.commands)) ||
        (beginning && !begin_bga_draw(input, context, *chart))) {
        pending.reset();
        return bga_draw_stop(bgm ? "BGM Draw source, chart or tool is unavailable."
                                          : "BGA Draw source, chart or tool is unavailable.");
    }
    if (!pending || pending->chart != context.state.chart ||
        pending->revision != context.environment.document.history.document.revision) {
        pending.reset();
        return bga_draw_stop(bgm ? "Begin a new BGM Draw stroke."
                                          : "Begin a new BGA Draw stroke.");
    }
    const auto row = bga_draw_row(input, context, *chart, pending->geometry);
    if (beginning && row) pending->last_row = *row;
    if (!row || !bga_draw_lane_valid(input) ||
        !append_bga_draw_targets(*pending, *chart, pending->geometry, context.state, *row, input.bga_lane_kind)) {
        pending.reset();
        return bga_draw_stop(bgm ? "BGM Draw requires a valid lane and at most 50,000 targets."
                                          : "BGA Draw requires a valid lane and at most 50,000 targets.");
    }
    result.accepted = true;
    accepted = bgm ? "BGM Draw stroke preview updated." : "BGA Draw stroke preview updated.";
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) {
        finish_bga_draw(input, context, result);
        accepted = bgm ? "BGM Draw stroke committed." : "BGA Draw stroke committed.";
    }
    return compose_input_family_result_t::continue_with_result();
}

auto select_lane_key_without_selection(const WormUiComposeInputAction& input,
                                      const compose_input_context_t& context) -> bool {
    if (input.kind != WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS ||
        context.environment.commands.project.compose_gesture.note_paint_mode_active)
        return false;
    const auto& state = context.state;
    return state.selected_notes.empty() &&
        std::ranges::none_of(state.selected_objects, [](const auto& object) {
            return object.kind == wormedit::editor_selection_object_kind_t::note ||
                   object.kind == wormedit::editor_selection_object_kind_t::bgm_event;
        });
}

auto dispatch_compose_input_event(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> compose_input_family_result_t {
    if (select_lane_key_without_selection(input, context))
        return compose_input_family_result_t::continue_batch({});
    if (const auto draw = dispatch_bga_draw_input(input, context, result, accepted)) return *draw;
    if (input.kind == WORM_UI_COMPOSE_INPUT_DRAW_SOURCE) {
        if (input.resource_role > 2 || (input.resource_role == 0 && input.resource_id != 0))
            return compose_input_family_result_t::stop_with_feedback("Unsupported Draw source.",
                wormedit::notification_severity_t::warning, wormedit::notification_source_t::input);
        context.environment.commands.project.compose_gesture.draw_source_kind = input.resource_role;
        context.environment.commands.project.compose_gesture.draw_source_id = input.resource_id;
        result.accepted = true;
        accepted = "Draw source selected.";
        return compose_input_family_result_t::continue_with_result();
    }
    if (const auto pasted = dispatch_compose_paste_input(input, context)) return *pasted;
    const auto family = dispatch_compose_input_families(input, context, result, accepted);
    return family.handled ? family
                          : dispatch_unhandled_compose_input(input, context, result, accepted);
}

auto pointer_placement_keeps_cursor(const WormUiComposeInputAction& input,
                                   const wormedit::editor_interaction_result_t& result) -> bool {
    if (!result.accepted) return false;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE:
        return true;
    case WORM_UI_COMPOSE_INPUT_POINTER_CLICK:
        return result.document_changed;
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE:
    case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE:
        return input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
    default: return false;
    }
}

void restore_pointer_placement_cursor(compose_input_context_t& context,
                                     const wormedit::cursor_pose_t& cursor,
                                     wormedit::editor_interaction_result_t& result) {
    wormedit::restore_cursor_pose(context.state, cursor);
    if (result.view) {
        const auto view = compose_view_for_state(context.environment.document.history, context.state);
        if (view) result.view = *view;
    }
}


auto process_compose_input_result(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  const compose_input_family_result_t& dispatched,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view accepted) -> compose_input_step_t {
    using namespace wormedit;
    if (!dispatched.handled) return {};
    if (dispatched.stop_batch) {
        if (!dispatched.notification_posted)
            post_input_feedback(context.notifications, dispatched.severity,
                                input_feedback_source(input.kind, dispatched.source, dispatched.severity),
                                dispatched.status);
        return {.handled = true, .stop = true, .status = dispatched.status};
    }
    if (!dispatched.process_result) {
        if (!dispatched.notification_posted)
            post_input_feedback(context.notifications, dispatched.severity,
                                input_feedback_source(input.kind, dispatched.source, dispatched.severity),
                                dispatched.status);
        return {.handled = true, .status = dispatched.status};
    }
    auto status = compose_result_status(result, accepted);
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE && result.accepted &&
        result.diagnostics.empty() && !result.document_changed)
        return {.handled = true, .status = std::move(status)};
    post_input_feedback(context.notifications, input_result_severity(input, result),
                        result.document_changed ? notification_source_t::edit
            : input_result_severity(input, result) == notification_severity_t::hint
                ? notification_source_t::input : compose_input_notification_source(input.kind), status);
    if (result.accepted && result.audition.has_value())
        context.environment.controls.cache.frame.audition_request = result.audition;
    if (result.accepted && result.document_changed) {
        const auto rebound =
            rebind_workspace(context.environment.document.workspace, context.environment.document.history);
        if (!rebound) {
            const auto message = rebound.diagnostics.empty()
                                     ? "Compose edit applied, but workspace rebinding failed."
                                     : rebound.diagnostics.front().message;
            post_input_feedback(context.notifications, notification_severity_t::error,
                                notification_source_t::document, message);
            return {.handled = true, .stop = true, .status = message};
        }
    }
    return {.handled = true, .status = std::move(status)};
}

auto dispatch_compose_input_with_travel(const WormUiComposeInputAction& input,
                                       compose_input_context_t& context,
                                       wormedit::editor_interaction_result_t& result,
                                       std::string_view& accepted) -> compose_input_family_result_t {
    auto& travel = context.environment.controls.cache.navigation_travel;
    const auto previous_scroll = context.state.viewport_scroll_pixels;
    const auto measure_step = input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP;
    const auto dispatched = dispatch_compose_input_event(input, context, result, accepted);
    if (is_preview_playing(context.environment.commands))
        travel.cancel();
    else if (measure_step && context.state.viewport_scroll_pixels != previous_scroll)
        travel.begin(context.state, context.environment.document.history.document.revision,
                     previous_scroll);
    return dispatched;
}

void finish_interrupted_compose_travel(const WormUiComposeInputAction& input,
                                       compose_input_context_t& context) {
    if (input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE) {
        context.environment.controls.cache.navigation_travel.cancel();
        return;
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP ||
        input.kind == WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION) return;
    // This input was validated against this revision; its own edit may have advanced history.
    context.environment.controls.cache.navigation_travel.finish(
        context.state, context.ui_action.compose_input_document_revision);
}

auto dispatch_compose_input_batch(compose_input_context_t& context) -> std::string {
    using namespace wormedit;
    auto status = std::string{};
    for (std::size_t index = 0; index < context.ui_action.compose_input_count; ++index) {
        if (!context.input_is_current()) {
            context.environment.commands.project.compose_gesture.bga_draw_stroke.reset();
            (void)dispatch_editor_action(context.environment.document.history, context.state,
                                         cancel_editor_gesture_action_t{});
            const auto message = "Remaining Compose input was discarded after the document revision changed.";
            post_input_feedback(context.notifications, notification_severity_t::warning,
                                notification_source_t::input, message);
            return message;
        }
        const auto& input = context.ui_action.compose_inputs[index];
        editor_interaction_result_t result;
        std::string_view accepted = "Compose interaction applied.";
        const auto cursor = capture_cursor_pose(context.state);
        const auto dispatched = dispatch_compose_input_with_travel(input, context, result, accepted);
        if (pointer_placement_keeps_cursor(input, result))
            restore_pointer_placement_cursor(context, cursor, result);
        finish_interrupted_compose_travel(input, context);
        auto step = process_compose_input_result(input, context, dispatched, result, accepted);
        if (!step.handled) continue;
        status = std::move(step.status);
        if (step.stop) return status;
    }
    if (context.ui_action.compose_note_label_focus_consumed)
        context.environment.controls.cache.frame.focus_note_label_editor = false;
    return status;
}

auto compose_bgm_scroll_content_width(const compose_input_context_t& context) -> double {
    const auto& widths = context.environment.controls.cache.scene.bgm_lane_widths_pixels;
    if (context.environment.controls.cache.scene.chart != context.state.chart || widths.empty())
        return static_cast<double>(context.bgm_lane_count) * compose_bgm_lane_width_pixels;
    double width{};
    for (const auto value : widths) width += value;
    return width;
}
void apply_compose_viewport_scroll(const wormedit::viewport_scroll_action_t& scroll,
                                   compose_input_context_t& context, std::string& status) {
    using namespace wormedit;
    if (scroll.pane == viewport_pane_t::bgm) {
        if (scroll.delta_x_pixels == 0.0)
            return;
        const auto maximum = std::max(0.0, compose_bgm_scroll_content_width(context) - context.bgm_pane_width);
        auto& retained = context.environment.commands
                             .project.compose_view.bgm_horizontal_scroll_pixels[context.state.chart.value];
        retained = std::clamp(retained + scroll.delta_x_pixels, 0.0, maximum);
        status = "Compose BGM pane scrolled.";
        post_input_feedback(context.notifications, notification_severity_t::hint,
                            notification_source_t::input, status);
        return;
    }
    if (scroll.delta_y_pixels == 0.0)
        return;
    if (!context.input_chart) {
        status = "The selected difficulty is no longer available.";
        post_input_feedback(context.notifications, notification_severity_t::warning,
                            notification_source_t::input, status);
        return;
    }
    const auto downscroll = compose_field_downscroll(
        *context.input_chart, context.environment.commands.preferences.editor_preferences);
    // Convert displacement to chart direction before routing it to the viewport or playhead.
    const auto chart_delta = compose_wheel_scroll_delta(scroll.delta_y_pixels, downscroll);
    if (is_preview_playing(context.environment.commands)) {
        editor_navigation_action_batch_t batch;
        batch.actions[batch.count++] = chart_delta > 0.0 ? editor_navigation_action_t::cursor_forward
                                                       : editor_navigation_action_t::cursor_backward;
        status = dispatch_compose_navigation_actions(
            batch, context.environment.document.history, context.state,
            &context.environment.commands, context.notifications);
        return;
    }
    const auto result = scroll_compose_navigation_view(
        context.environment.document.history, context.state,
        context.state.viewport_scroll_pixels + chart_delta,
        downscroll ? editor_field_scroll_direction_t::down : editor_field_scroll_direction_t::up);
    status = compose_result_status(result, "Compose field scrolled.");
    const auto severity = result.accepted
                              ? notification_severity_t::hint
                          : !result.diagnostics.empty()
                              ? input_notification_severity(result.diagnostics.front().severity)
                              : notification_severity_t::warning;
    post_input_feedback(context.notifications, severity, notification_source_t::input, status);
}

void apply_compose_viewport_actions(const wormedit::viewport_action_batch_t& actions,
                                    compose_input_context_t& context, std::string& status) {
    for (std::size_t index = 0; index < actions.count; ++index) {
        const auto& action = actions[index];
        if (const auto* scroll = std::get_if<wormedit::viewport_scroll_action_t>(&action)) {
            apply_compose_viewport_scroll(*scroll, context, status);
        } else if (const auto* zoom = std::get_if<wormedit::viewport_zoom_action_t>(&action)) {
            const auto result = wormedit::dispatch_editor_action(
                context.environment.document.history, context.state,
                wormedit::adjust_editor_zoom_action_t{
                    .factor = zoom->factor, .anchor_screen_y_pixels = zoom->cursor_y_pixels});
            status = compose_result_status(result, "Compose field zoomed at the pointer.");
            const auto severity = result.accepted
                                      ? notification_severity_t::hint
                                  : !result.diagnostics.empty()
                                      ? input_notification_severity(result.diagnostics.front().severity)
                                      : notification_severity_t::warning;
            post_input_feedback(context.notifications, severity, notification_source_t::input,
                                status);
        }
    }
}

void apply_compose_pinch_zoom(const WormUiComposeViewportInput& input,
                              compose_input_context_t& context, std::string& status) {
    if (!std::isfinite(input.zoom_factor) || input.zoom_factor <= 0 ||
        !std::isfinite(input.cursor_y_pixels) || input.cursor_y_pixels < 0 ||
        input.cursor_y_pixels > context.state.viewport_height_pixels) {
        status = "Compose pinch zoom used an invalid scale or anchor.";
        post_input_feedback(context.notifications, wormedit::notification_severity_t::warning,
                            wormedit::notification_source_t::input, status);
        return;
    }
    (void)context.environment.controls.viewport.normal_edit_press();
    const auto result = wormedit::dispatch_editor_action(
        context.environment.document.history, context.state,
        wormedit::adjust_editor_zoom_action_t{.factor = input.zoom_factor,
                                              .anchor_screen_y_pixels = input.cursor_y_pixels});
    status = compose_result_status(result, "Compose field zoomed at the pinch focus.");
    const auto severity = result.accepted
                              ? wormedit::notification_severity_t::hint
                          : !result.diagnostics.empty()
                              ? input_notification_severity(result.diagnostics.front().severity)
                              : wormedit::notification_severity_t::warning;
    post_input_feedback(context.notifications, severity, wormedit::notification_source_t::input,
                        status);
}

auto dispatch_compose_viewport_batch(compose_input_context_t& context,
                                     double viewport_delta_seconds, std::string status)
    -> std::string {
    using namespace wormedit;
    for (std::size_t index = 0; index < context.ui_action.compose_viewport_input_count; ++index) {
        if (!context.input_is_current()) {
            const auto message = "Remaining Compose viewport input was discarded after the chart or document changed.";
            post_input_feedback(context.notifications, notification_severity_t::warning,
                                notification_source_t::input, message);
            return "Remaining Compose viewport input was discarded after the chart or document "
                   "changed.";
        }
        const auto& input = context.ui_action.compose_viewport_inputs[index];
        if (input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM) {
            apply_compose_pinch_zoom(input, context, status);
            continue;
        }
        const auto bgm_pane =
            context.bgm_pane_width > 0.0 && std::isfinite(input.cursor_x_pixels) &&
            input.cursor_x_pixels >= context.bgm_pane_left_pixels &&
            input.cursor_x_pixels <= context.ui_action.compose_input_viewport_width_pixels;
        apply_compose_viewport_actions(
            context.environment.controls.viewport.handle_wheel(
                {.pane = bgm_pane ? viewport_pane_t::bgm : viewport_pane_t::chart,
                 .kind = input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                             ? viewport_wheel_kind_t::coarse_steps
                             : viewport_wheel_kind_t::smooth_pixels,
                 .delta_x = input.delta_x,
                 .delta_y = !bgm_pane && !input.zoom_modifier &&
                                    context.environment.commands
                                        .preferences.editor_preferences.invert_scroll_wheel
                                ? -input.delta_y
                                : input.delta_y,
                 .zoom_modifier = input.zoom_modifier,
                 .cursor_x_pixels = input.cursor_x_pixels,
                 .cursor_y_pixels = input.cursor_y_pixels}),
            context, status);
    }
    if (std::isfinite(viewport_delta_seconds) && viewport_delta_seconds > 0.0 &&
        context.input_is_current())
        apply_compose_viewport_actions(
            context.environment.controls.viewport.advance(viewport_delta_seconds), context,
            status);
    return status;
}

void reset_compose_autoscroll_if_idle(compose_input_context_t& context) {
    context.environment.controls.cache.frame.viewport_autoscroll_active =
        context.environment.controls.viewport.autoscroll_active();
    if (context.environment.controls.cache.frame.viewport_autoscroll_active)
        return;
    context.environment.controls.cache.frame.viewport_autoscroll_anchor_x_pixels = 0.0;
    context.environment.controls.cache.frame.viewport_autoscroll_anchor_y_pixels = 0.0;
    context.environment.controls.cache.frame.viewport_autoscroll_pointer_x_pixels = 0.0;
    context.environment.controls.cache.frame.viewport_autoscroll_pointer_y_pixels = 0.0;
}

} // namespace compose_input_detail

auto consume_minimap_preferences(WormUiWorkspaceAction& action,
                                 application_command_state_t& application) -> bool {
    if (action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) return false;
    bool changed = false;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < action.compose_input_count; ++index) {
        const auto& input = action.compose_inputs[index];
        if (input.kind == WORM_UI_COMPOSE_INPUT_SET_MINIMAP_PAUSED_BEHAVIOR) {
            application.preferences.minimap_click_moves_paused_playhead = input.overview_primary;
            changed = true;
        } else if (input.kind == WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE) {
            application.project.bgm.bgm_bar_hidden = input.overview_primary;
        } else action.compose_inputs[kept++] = input;
    }
    action.compose_input_count = kept;
    return changed;
}

auto dispatch_compose_input_action_impl(WormUiWorkspaceAction& ui_action,
                                        const compose_input_inputs_t& environment,
                                        wormedit::editor_interaction_state_t& state,
                                        double viewport_delta_seconds,
                                        const wormedit::notification_sink_t* notifications)
    -> std::string {
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    auto& viewport_input = environment.controls.viewport;
    auto& navigation_input = environment.controls.navigation;
    auto& application_state = environment.commands;
    auto& scene_cache = environment.controls.cache;

    using namespace wormedit;
    if (!editor_interaction_has_pending_gesture(state))
        state.scratch_on_right = application_state.scratch_on_right;
    scene_cache.frame.audition_request.reset();
    scene_cache.frame.draft_audition_request.reset();
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != ui_action.compose_input_chart_id ||
        state.chart.value != ui_action.compose_input_chart_id ||
        history.document.revision != ui_action.compose_input_document_revision ||
        !std::isfinite(ui_action.compose_input_viewport_width_pixels) ||
        ui_action.compose_input_viewport_width_pixels <= 0.0) {
        scene_cache.navigation_travel.cancel();
        if (workspace.active_tab != workspace_tab_t::compose ||
            editor_gestures_stale(state, history.document.revision))
            retire_interrupted_compose_input(history, state, viewport_input, navigation_input,
                                             application_state, scene_cache);
        const auto message = "Compose input ignored because the chart or document revision changed.";
        post_input_feedback(notifications, notification_severity_t::warning,
                            notification_source_t::input, message);
        return message;
    }
    if (ui_action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        scene_cache.navigation_travel.cancel();
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        const auto message = "Compose input batch exceeded its capacity; the active gesture was canceled.";
        post_input_feedback(notifications, notification_severity_t::warning,
                            notification_source_t::input, message);
        return message;
    }
    if (ui_action.compose_viewport_input_count > WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY) {
        scene_cache.navigation_travel.cancel();
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        const auto message = "Compose viewport input exceeded its capacity; the active gesture was canceled.";
        post_input_feedback(notifications, notification_severity_t::warning,
                            notification_source_t::input, message);
        return message;
    }
    const auto input_chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (input_chart != history.document.charts.end())
        arm_compose_note_layers(state, application_state, *input_chart);
    const auto bgm_lane_count = input_chart == history.document.charts.end()
                                    ? std::size_t{}
                                    : input_chart->bgm_source_lanes.size();
    const auto horizontal_layout = compose_horizontal_layout(
        compose_editor_layout_width(ui_action.compose_input_viewport_width_pixels,
                                    application_state.project.compose_view.compose_editor_right_pixels),
        input_chart == history.document.charts.end() ? 0 : maximum_layout_lane_count(*input_chart),
        bgm_lane_count != 0, state.bga_lane_visible && input_chart != history.document.charts.end()
            ? std::popcount(editor_bga_visible_lane_mask(*input_chart, state.bga_revealed_lane_mask)) : 0,
        effective_compose_timing_lane_mask(application_state));
    const auto bgm_pane_width = horizontal_layout.bgm_pane_width_pixels;
    const auto bgm_pane_left_pixels = horizontal_layout.bgm_pane_left_pixels;
    const auto field_left_pixels = horizontal_layout.field_left_pixels;
    const auto field_width_pixels = horizontal_layout.playable_field_width_pixels;
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto gesture_active = has_pending_editor_gesture(state);
    auto input_context = compose_input_context_t{
        .environment = environment,
        .ui_action = ui_action,
        .state = state,
        .notifications = notifications,
        .input_chart = input_chart == history.document.charts.end() ? nullptr : &*input_chart,
        .gesture_active = gesture_active,
        .bgm_lane_count = bgm_lane_count,
        .bgm_pane_width = bgm_pane_width,
        .bgm_pane_left_pixels = bgm_pane_left_pixels,
        .field_left_pixels = field_left_pixels,
        .field_width_pixels = field_width_pixels};
    update_compose_viewport_pointer_input(ui_action, viewport_input, scene_cache, bgm_pane_width,
                                          bgm_pane_left_pixels,
                                          ui_action.compose_input_viewport_width_pixels);
    update_compose_pointer_hover(
        ui_action, gesture_active, chart == history.document.charts.end() ? nullptr : &*chart,
        scene_cache.frame.interaction_view ? &*scene_cache.frame.interaction_view : nullptr,
        history.document.revision, field_left_pixels, field_width_pixels, scene_cache,
        application_state.preferences.editor_preferences);
    auto status = dispatch_compose_input_batch(input_context);
    if (ui_action.compose_note_label_focus_consumed) {
        scene_cache.frame.focus_note_label_editor = false;
    }

    status = dispatch_compose_viewport_batch(input_context, viewport_delta_seconds,
                                             std::move(status));
    scene_cache.navigation_travel.advance(state, history.document.revision,
        is_preview_playing(application_state), viewport_delta_seconds);
    reset_compose_autoscroll_if_idle(input_context);
    if (!status.empty()) {
        return status;
    }
    return ui_action.compose_input_count != 0 || ui_action.compose_viewport_input_count != 0
               ? "Compose input handled."
               : std::string{};
}

void refresh_compose_editor_width(const WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application) {
    if (workspace.active_tab != wormedit::workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision ||
        !std::isfinite(action.compose_input_viewport_width_pixels) || action.compose_input_viewport_width_pixels <= 0 ||
        !std::isfinite(action.compose_input_editor_right_pixels) || action.compose_input_editor_right_pixels <= 0) return;
    application.project.compose_view.compose_editor_right_pixels = compose_editor_layout_width(
        action.compose_input_viewport_width_pixels, action.compose_input_editor_right_pixels);
}

namespace compose_input_detail {
auto dispatch_bgm_mix_toggle(const WormUiComposeInputAction& input,
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, application_command_state_t& application,
    bool& accepted) -> std::string {
    const auto* chart = selected_chart(history, workspace);
    if (!chart || workspace.active_tab != wormedit::workspace_tab_t::compose ||
        action.compose_input_chart_id != chart->id.value ||
        action.compose_input_document_revision != history.document.revision)
        return "BGM mix input ignored because the chart or document revision changed.";
    if (std::ranges::find(chart->bgm_source_lanes, wormedit::object_id_t{input.lane_id},
        &wormedit::bgm_source_lane_t::id) == chart->bgm_source_lanes.end())
        return "The BGM lane is no longer available.";
    if (!application.services.transport_set_bgm_mix) return "BGM preview mixing is unavailable.";
    const auto found = application.project.bgm.bgm_lane_mix.find(chart->id.value);
    auto next = found == application.project.bgm.bgm_lane_mix.end() ? bgm_lane_mix_state_t{} : found->second;
    auto& set = input.kind == WORM_UI_COMPOSE_INPUT_BGM_MUTE ? next.muted : next.solo;
    const auto enabled = !set.erase(input.lane_id);
    if (enabled) set.insert(input.lane_id);
    const auto muted = compose_bgm_muted_lanes(*chart, next);
    if (!application.services.transport_set_bgm_mix(chart->id, muted))
        return "BGM preview mix could not be updated; lane controls were unchanged.";
    application.project.bgm.bgm_lane_mix.insert_or_assign(chart->id.value, std::move(next));
    accepted = true;
    if (input.kind == WORM_UI_COMPOSE_INPUT_BGM_MUTE)
        return enabled ? "BGM lane muted." : "BGM lane unmuted.";
    return enabled ? "BGM lane added to solo mix." : "BGM lane removed from solo mix.";
}
} // namespace compose_input_detail

auto dispatch_compose_bgm_mix_inputs(WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application, const wormedit::notification_sink_t* notifications)
    -> std::string {
    if (action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) return {};
    std::size_t kept{};
    std::string status;
    for (std::size_t index = 0; index < action.compose_input_count; ++index) {
        const auto& input = action.compose_inputs[index];
        if (input.kind != WORM_UI_COMPOSE_INPUT_BGM_MUTE && input.kind != WORM_UI_COMPOSE_INPUT_BGM_SOLO) {
            action.compose_inputs[kept++] = input;
            continue;
        }
        bool accepted{};
        status = dispatch_bgm_mix_toggle(input, action, history, workspace, application, accepted);
        post_input_feedback(notifications, accepted ? wormedit::notification_severity_t::hint
                                                   : wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::audio, status);
    }
    action.compose_input_count = kept;
    return status;
}

auto dispatch_compose_input_action(WormUiWorkspaceAction& ui_action,
                                   const compose_input_inputs_t& environment,
                                   wormedit::editor_interaction_state_t& state,
                                   double viewport_delta_seconds, bool post_notifications)
    -> std::string {
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    auto& application_state = environment.commands;
    auto& scene_cache = environment.controls.cache;
    const auto* notifications = post_notifications ? &environment.document.notifications : nullptr;

    const auto mix_status = dispatch_compose_bgm_mix_inputs(ui_action, history, workspace,
        application_state, notifications);
    if (!mix_status.empty() && ui_action.compose_input_count == 0) {
        refresh_compose_editor_width(ui_action, history, workspace, application_state);
        return mix_status;
    }
    // View-only BGM bar inputs never reach the editor dispatcher.
    const bgm_bar_input_context_t bgm_bar_context{
        .total_frames = scene_cache.waveform.waveform_overview_total_frames,
        .visible_begin_fraction = ui_action.minimap_visible_begin_fraction,
        .visible_end_fraction = ui_action.minimap_visible_end_fraction,
        .project = history.document.id,
        .chart = state.chart,
        .document_revision = history.document.revision,
        .asset = workspace.transport.audio_asset.value_or(asset_id_t{})};
    (void)consume_bgm_bar_inputs(ui_action, application_state.project.bgm.bgm_bar_range,
                                 application_state.project.bgm.bgm_bar_pan_override, bgm_bar_context);
    const bool preference_changed = consume_minimap_preferences(ui_action, application_state);
    if (preference_changed && ui_action.compose_input_count == 0) {
        const auto message = "Minimap paused-click preference changed for this session.";
        post_input_feedback(notifications, wormedit::notification_severity_t::hint,
                            wormedit::notification_source_t::input, message);
        return message;
    }
    const auto status = dispatch_compose_input_action_impl(ui_action, environment, state,
                                                           viewport_delta_seconds, notifications);
    refresh_compose_editor_width(ui_action, history, workspace, application_state);
    (void)compose_minimap_start_frame(application_state, history, &state);
    return status;
}

auto selected_chart(const wormedit::document_history_t& history,
                    const wormedit::workspace_controller_t& workspace)
    -> const wormedit::chart_document_t* {
    if (!workspace.selected_chart) {
        return nullptr;
    }
    return wormedit::find_chart(history.document, *workspace.selected_chart);
}

auto visible_semantic_layer_by_id(const wormedit::chart_document_t& chart, wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t* {
    const auto found =
        std::ranges::find(chart.semantic_layers, id, &wormedit::semantic_layer_t::id);
    return found != chart.semantic_layers.end() && found->visible ? &*found : nullptr;
}

} // namespace wormedit::sys
