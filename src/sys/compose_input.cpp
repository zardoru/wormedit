#include "sys/compose_input.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_playhead_skip.h"
#include "sys/compose_scroll_geometry.h"

#include <wormedit/audio/decoder.h>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/workspace.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
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

auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string {
    if (result.accepted) {
        return std::string(accepted);
    }
    return result.diagnostics.empty() ? "Compose action was rejected."
                                      : result.diagnostics.front().message;
}

auto compose_view_for_state(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state)
    -> std::optional<wormedit::editor_interaction_view_model_t> {
    const auto inspected = wormedit::inspect_editor_interaction(history, state);
    if (const auto* view =
            std::get_if<wormedit::editor_interaction_view_model_t>(&inspected)) {
        return *view;
    }
    return std::nullopt;
}

auto compose_navigation_action(WormUiComposeNavigationAction action)
    -> std::optional<wormedit::editor_navigation_action_t> {
    using namespace wormedit;
    switch (action) {
    case WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD:
        return editor_navigation_action_t::cursor_backward;
    case WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD:
        return editor_navigation_action_t::cursor_forward;
    case WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD:
        return editor_navigation_action_t::page_backward;
    case WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD:
        return editor_navigation_action_t::page_forward;
    }
    return std::nullopt;
}

namespace {

// While the preview plays, navigation skips the playhead instead of moving the edit cursor.
auto skip_playhead_for_navigation(wormedit::editor_navigation_action_t action,
                                  const wormedit::document_history_t& history,
                                  const wormedit::editor_interaction_state_t& state,
                                  application_command_state_t& application) -> std::string {
    using namespace wormedit;
    const auto found =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (found == history.document.charts.end())
        return "The selected difficulty is no longer available.";
    const auto forward = action == editor_navigation_action_t::cursor_forward ||
                         action == editor_navigation_action_t::page_forward;
    const auto paged = action == editor_navigation_action_t::page_backward ||
                       action == editor_navigation_action_t::page_forward;
    return skip_playing_playhead(application, *found, state.active_snap_denominator,
                                 paged ? playhead_skip_unit_t::measure : playhead_skip_unit_t::snap,
                                 forward ? 1.0 : -1.0)
        .status;
}

} // namespace

auto dispatch_compose_navigation_actions(const wormedit::editor_navigation_action_batch_t& actions,
                                         wormedit::document_history_t& history,
                                         wormedit::editor_interaction_state_t& state,
                                         application_command_state_t* application)
    -> std::string {
    using namespace wormedit;
    auto status = std::string{};
    for (std::size_t index = 0; index < actions.count; ++index) {
        if (application && is_preview_playing(*application)) {
            status = skip_playhead_for_navigation(actions[index], history, state, *application);
            continue;
        }
        editor_interaction_result_t result;
        std::string_view accepted;
        switch (actions[index]) {
        case editor_navigation_action_t::cursor_backward:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
            accepted = "Edit cursor moved backward.";
            break;
        case editor_navigation_action_t::cursor_forward:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
            accepted = "Edit cursor moved forward.";
            break;
        case editor_navigation_action_t::page_backward:
            result = dispatch_editor_action(
                history, state, page_edit_cursor_action_t{interaction_direction_t::backward, 1});
            accepted = "Edit cursor paged backward by one measure.";
            break;
        case editor_navigation_action_t::page_forward:
            result = dispatch_editor_action(
                history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 1});
            accepted = "Edit cursor paged forward by one measure.";
            break;
        }
        status = compose_result_status(result, accepted);
        if (!result.accepted)
            break;
    }
    return status;
}

namespace {

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

} // namespace

auto stage_bmson_bgm_markers(const wormedit::document_history_t& history,
                             const wormedit::editor_interaction_state_t& state,
                             application_command_state_t& application_state,
                             wormedit::object_id_t reference,
                             std::span<const wormedit::bmson_sound_marker_insertion_t> markers)
    -> std::string {
    using namespace wormedit;
    if (markers.empty()) {
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
    auto& draft = application_state.bmson_marker_draft;
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
    return markers.size() == 1 ? "BMSON marker staged at the exact snapped row."
                               : "BMSON razor segment staged at exact snapped rows.";
}

auto stage_bmson_bgm_marker(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state,
                            application_command_state_t& application_state,
                            wormedit::object_id_t reference, wormedit::local_position_t position,
                            bool restart) -> std::string {
    const std::array markers{wormedit::bmson_sound_marker_insertion_t{position, restart}};
    return stage_bmson_bgm_markers(history, state, application_state, reference, markers);
}

struct bmson_marker_source_t {
    wormedit::object_id_t source_lane;
    wormedit::local_position_t position;
    wormedit::object_id_t source_event;
    const wormedit::sound_channel_t* channel{};
};

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
    if (cache.channel_waveform_selection && cache.channel_waveform_selection->chart == chart.id &&
        cache.channel_waveform_selection->revision == revision &&
        cache.channel_waveform_selection->bgm_event == bgm_event)
        return cache.channel_waveform_selection;
    cache.channel_waveform_selection.reset();
    const auto marker = find_bmson_marker_source(chart, bgm_event);
    if (marker) {
        cache.channel_waveform_selection = compose_scene_cache_t::channel_waveform_selection_t{
            .chart = chart.id,
            .revision = revision,
            .bgm_event = bgm_event,
            .channel = marker->channel->id,
            .source_lane = marker->source_lane,
            .asset = marker->channel->source_clip.source};
    }
    return cache.channel_waveform_selection;
}

namespace {

template <typename PositionAtComposeY>
auto dispatch_bmson_razor_input(
    const WormUiComposeInputAction& input, const wormedit::document_history_t& history,
    const wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, bool gesture_active,
    PositionAtComposeY&& position_at_compose_y) -> std::string {
    using namespace wormedit;
    if (gesture_active) {
        return "Finish or cancel the active Compose gesture before using the BMSON razor.";
    }
    const auto chart = std::ranges::find(history.document.charts, state.chart,
                                         &chart_document_t::id);
    const auto current_view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !current_view ||
        !std::isfinite(input.y_pixels)) {
        return "The BMSON chart field is no longer available.";
    }
    auto& stroke = application_state.bmson_razor_stroke;
    const object_id_t reference{input.object_id};
    if (input.razor_stroke_start) {
        stroke.reset();
    } else if (!stroke || stroke->chart != state.chart ||
               stroke->revision != history.document.revision ||
               stroke->reference != reference) {
        stroke.reset();
        return "Begin a new BMSON razor stroke on the selected sound channel.";
    }
    const auto denominator = input.razor_stroke_start
        ? state.active_snap_denominator : stroke->snap_denominator;
    const auto raw = position_at_compose_y(*chart, *current_view, input.y_pixels);
    const auto position = raw ? resolve_resource_placement_position(
        *chart, *raw, denominator) : std::nullopt;
    if (!position) {
        return "BMSON razor could not snap to a valid chart row.";
    }
    const auto restart = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART;
    if (input.razor_stroke_start) {
        const auto& draft = application_state.bmson_marker_draft;
        const auto already_staged = draft && draft->chart == state.chart &&
            draft->revision == history.document.revision && draft->reference == reference &&
            std::ranges::any_of(draft->markers, [&](const auto& marker) {
                return marker.position == *position;
            });
        const auto status = already_staged
            ? std::string{"BMSON razor stroke started on a staged pulse."}
            : stage_bmson_bgm_marker(history, state, application_state,
                                     reference, *position, restart);
        const auto& staged = application_state.bmson_marker_draft;
        if (staged && staged->chart == state.chart &&
            staged->revision == history.document.revision &&
            staged->reference == reference &&
            std::ranges::any_of(staged->markers, [&](const auto& marker) {
                return marker.position == *position;
            })) {
            stroke = application_command_state_t::bmson_razor_stroke_t{
                .chart = state.chart, .revision = history.document.revision,
                .reference = reference, .snap_denominator = denominator,
                .last_position = *position};
        }
        return status;
    }
    const auto path = interpolate_bmson_razor_path(
        *chart, stroke->last_position, *position, denominator);
    if (!path) {
        return path.diagnostics.front().message;
    }
    std::vector<bmson_sound_marker_insertion_t> pending;
    pending.reserve(path.positions.size());
    for (const auto candidate : path.positions) {
        const auto& draft = application_state.bmson_marker_draft;
        if (draft && std::ranges::any_of(draft->markers, [&](const auto& marker) {
                return marker.position == candidate;
            })) {
            continue;
        }
        pending.push_back({.position = candidate, .restart = restart});
    }
    if (pending.empty()) {
        stroke->last_position = *position;
        return "BMSON razor remains on staged grid cells.";
    }
    const auto before = application_state.bmson_marker_draft
        ? application_state.bmson_marker_draft->markers.size() : 0;
    const auto status = stage_bmson_bgm_markers(
        history, state, application_state, reference, pending);
    if (application_state.bmson_marker_draft &&
        application_state.bmson_marker_draft->markers.size() == before + pending.size()) {
        stroke->last_position = *position;
    }
    return status;
}

struct compose_input_family_result_t {
    bool handled{};
    bool stop_batch{};
    bool process_result{};
    std::string status;

    compose_input_family_result_t() = default;
    compose_input_family_result_t(const char* message)
        : compose_input_family_result_t(std::string{message}) {}
    compose_input_family_result_t(std::string message)
        : handled(true), stop_batch(true), status(std::move(message)) {}

    static auto continue_batch(std::string message) -> compose_input_family_result_t {
        auto result = compose_input_family_result_t{};
        result.handled = true;
        result.status = std::move(message);
        return result;
    }
    static auto continue_with_result() -> compose_input_family_result_t {
        auto result = compose_input_family_result_t{};
        result.handled = true;
        result.process_result = true;
        return result;
    }
};

auto dispatch_compose_navigation_input(const WormUiComposeInputAction&,
                                       wormedit::document_history_t&,
                                       wormedit::editor_interaction_state_t&,
                                       wormedit::editor_navigation_input_controller_t&,
                                       application_command_state_t&)
    -> compose_input_family_result_t;
auto dispatch_compose_cursor_input(const WormUiComposeInputAction&, wormedit::document_history_t&,
                                   wormedit::editor_interaction_state_t&,
                                   wormedit::editor_interaction_result_t&, std::string_view&)
    -> compose_input_family_result_t;
auto dispatch_compose_lane_key_press(const WormUiComposeInputAction&, wormedit::document_history_t&,
                                     wormedit::editor_interaction_state_t&,
                                     wormedit::editor_interaction_result_t&, std::string_view&)
    -> compose_input_family_result_t;
auto dispatch_compose_lane_key_release(const WormUiComposeInputAction&,
                                       wormedit::document_history_t&,
                                       wormedit::workspace_controller_t&,
                                       wormedit::editor_interaction_state_t&,
                                       wormedit::editor_interaction_result_t&, std::string_view&)
    -> compose_input_family_result_t;

auto dispatch_compose_keyboard_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted) -> compose_input_family_result_t {
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
                                                 application);
    case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD:
    case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD:
    case WORM_UI_COMPOSE_INPUT_DELETE_SELECTION:
        return dispatch_compose_cursor_input(input, history, state, result, accepted);
    case WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS:
        return dispatch_compose_lane_key_press(input, history, state, result, accepted);
    case WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE:
        return dispatch_compose_lane_key_release(input, history, workspace, state, result,
                                                 accepted);
    default:
        return {};
    }
}

auto dispatch_compose_navigation_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application) -> compose_input_family_result_t {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP: {
        const auto navigation = compose_navigation_action(input.navigation_action);
        if (!navigation || (*navigation != editor_navigation_action_t::page_backward &&
                            *navigation != editor_navigation_action_t::page_forward))
            return "Compose measure navigation step used an unknown action.";
        editor_navigation_action_batch_t batch;
        batch.actions[batch.count++] = *navigation;
        return compose_input_family_result_t::continue_batch(
            dispatch_compose_navigation_actions(batch, history, state, &application));
    }
    case WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS: {
        const auto navigation = compose_navigation_action(input.navigation_action);
        if (!navigation) {
            return "Compose navigation input used an unknown action.";
        }
        const auto status = dispatch_compose_navigation_actions(
            navigation_input.press(*navigation, static_cast<std::uint64_t>(SDL_GetTicks())),
            history, state, &application);
        return compose_input_family_result_t::continue_batch(status);
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
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> compose_input_family_result_t {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD:
        result = dispatch_editor_action(
            history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
        accepted = "Edit cursor moved backward.";
        break;
    case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD:
        result = dispatch_editor_action(
            history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
        accepted = "Edit cursor moved forward.";
        break;
    case WORM_UI_COMPOSE_INPUT_DELETE_SELECTION:
        result = dispatch_editor_action(history, state, delete_editor_selection_action_t{});
        accepted = result.document_changed ? "Selection deleted." : "No object at the cursor.";
        break;
    default:
        return {};
    }
    return compose_input_family_result_t::continue_with_result();
}

auto dispatch_compose_lane_key_press(const WormUiComposeInputAction& input,
                                     wormedit::document_history_t& history,
                                     wormedit::editor_interaction_state_t& state,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> compose_input_family_result_t {
    using namespace wormedit;
    if (input.lane_id == 0) {
        return "Lane-key input did not identify a chart lane.";
    }
    if (!state.selected_notes.empty()) {
        result = dispatch_editor_action(
            history, state,
            assign_selected_notes_to_lane_action_t{lane_id_t{input.lane_id}, input.successive});
        accepted = input.successive ? "Next selected note assigned to lane."
                                    : "Selected notes assigned to lane.";
    } else {
        result = dispatch_editor_action(
            history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id}});
        accepted = "Lane hold started; move the edit cursor and release the key to finish.";
    }
    return compose_input_family_result_t::continue_with_result();
}

auto dispatch_compose_lane_key_release(const WormUiComposeInputAction& input,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t& workspace,
                                       wormedit::editor_interaction_state_t& state,
                                       wormedit::editor_interaction_result_t& result,
                                       std::string_view& accepted)
    -> compose_input_family_result_t {
    using namespace wormedit;
    if (state.pending_virtual_lane_key) {
        const auto pending = *state.pending_virtual_lane_key;
        state.pending_virtual_lane_key.reset();
        if (pending.document_revision != history.document.revision ||
            pending.lane != lane_id_t{input.lane_id}) {
            return "Virtual BMS lane key was canceled after the chart or key changed.";
        }
        auto end_row = state.virtual_bms_cursor_row;
        if (!end_row) {
            const auto* chart = selected_chart(history, workspace);
            if (chart) {
                const auto built = build_measure_index(*chart);
                if (const auto* index = std::get_if<measure_index_t>(&built)) {
                    end_row = absolute_row(*index, state.cursor);
                }
            }
        }
        if (!end_row) {
            return "Virtual BMS lane key ended outside chart geometry.";
        }
        if (*end_row == pending.canvas_row) {
            result = dispatch_editor_action(
                history, state, place_virtual_bms_tap_action_t{pending.canvas_row, pending.lane});
            accepted = "Virtual BMS tap placed from the lane key.";
        } else {
            result = dispatch_editor_action(
                history, state,
                place_virtual_bms_hold_action_t{pending.canvas_row, *end_row, pending.lane});
            accepted = "Virtual BMS hold placed from the lane key.";
        }
    } else if ((state.pending_hold.has_value() &&
                state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) ||
               !state.additional_keyboard_holds.empty()) {
        result = dispatch_editor_action(
            history, state, release_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id}});
        accepted = result.document_changed ? "Lane hold committed." : "Lane hold released.";
    } else {
        result.accepted = true;
        accepted = "Lane key released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

struct bmson_compose_context_t {
    const WormUiWorkspaceAction& action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    wormedit::editor_interaction_state_t& state;
    application_command_state_t& application_state;
    compose_scene_cache_t& scene_cache;
    bool gesture_active;
};

auto begin_bmson_bgm_move(const WormUiComposeInputAction& input, bmson_compose_context_t& context)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [action, history, workspace, state, application_state, scene_cache, gesture_active] =
        context;
    if (gesture_active)
        return "Finish or cancel the active Compose gesture before moving a BGM event.";
    const object_id_t event{input.object_id};
    if (state.selected_objects.size() != 1 ||
        state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        state.selected_objects.front().id != event)
        return "Select one BGM event before dragging it.";
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end())
        return "The selected chart is no longer available.";
    const auto source = find_bmson_marker_source(*chart, event);
    const bgm_event_t* ordinary{};
    if (!source) {
        for (const auto& measure : chart->measures) {
            const auto found = std::ranges::find(measure.bgm_events, event, &bgm_event_t::id);
            if (found != measure.bgm_events.end()) {
                ordinary = &*found;
                break;
            }
        }
        if (!ordinary || ordinary->sound_channel_event)
            return "The selected BGM event cannot use this move gesture.";
    }
    application_state.bmson_marker_move_preview =
        application_command_state_t::bmson_marker_move_preview_t{
            .chart = state.chart,
            .revision = history.document.revision,
            .event = event,
            .source_lane = source ? source->source_lane : ordinary->source_lane,
            .linked_bmson = source.has_value(),
            .snap_denominator = state.active_snap_denominator,
            .target = source ? source->position : ordinary->position,
            .valid = true};
    return compose_input_family_result_t::continue_batch("BGM event drag started.");
}

template <typename VirtualBmsRowAt>
auto virtual_bms_move_target(const wormedit::chart_document_t& chart,
                             const wormedit::editor_interaction_view_model_t& view, double y_pixels,
                             std::uint64_t snap_denominator, VirtualBmsRowAt&& virtual_bms_row_at)
    -> std::optional<std::uint64_t> {
    using namespace wormedit;
    const auto raw_row = virtual_bms_row_at(chart, view, y_pixels);
    if (!raw_row)
        return std::nullopt;
    const auto built_canvas = build_virtual_canvas(chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built_canvas);
    if (!canvas ||
        (snap_denominator != 0 && chart.divisions_per_standard_measure % snap_denominator != 0))
        return std::nullopt;
    auto snapped = *raw_row;
    if (snap_denominator != 0) {
        const auto step = chart.divisions_per_standard_measure / snap_denominator;
        const auto remainder = snapped % step;
        snapped -= remainder;
        if (remainder > step - remainder) {
            if (step > std::numeric_limits<std::uint64_t>::max() - snapped)
                return std::nullopt;
            snapped += step;
        }
    }
    const auto measure = canvas->measure_containing(snapped);
    return measure && !measure->authored ? std::optional{snapped} : std::nullopt;
}

auto commit_bmson_bgm_move(const WormUiComposeInputAction& input, bmson_compose_context_t& context,
                           const application_command_state_t::bmson_marker_move_preview_t& preview)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [action, history, workspace, state, application_state, scene_cache, gesture_active] =
        context;
    const auto moved =
        preview.linked_bmson
            ? move_bmson_bgm_marker(history, action.compose_input_document_revision, state.chart,
                                    object_id_t{input.object_id}, preview.target)
        : preview.virtual_canvas_row
            ? apply_virtual_bms_bgm_event_move(history, action.compose_input_document_revision,
                                               {.chart = state.chart,
                                                .event = object_id_t{input.object_id},
                                                .canvas_row = *preview.virtual_canvas_row,
                                                .source_lane = preview.source_lane})
            : apply_bgm_event_move(history, action.compose_input_document_revision,
                                   {.chart = state.chart,
                                    .event = object_id_t{input.object_id},
                                    .position = preview.target,
                                    .source_lane = preview.source_lane});
    if (!moved.diagnostics.empty())
        return moved.diagnostics.front().message;
    if (!moved.applied)
        return "BGM event remained at its original position.";
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BGM event moved, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "BGM event moved to the snapped row.";
}

template <typename PositionAtComposeY, typename VirtualBmsRowAt>
auto update_bmson_bgm_move(const WormUiComposeInputAction& input, bmson_compose_context_t& context,
                           PositionAtComposeY&& position_at_compose_y,
                           VirtualBmsRowAt&& virtual_bms_row_at) -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [action, history, workspace, state, application_state, scene_cache, gesture_active] =
        context;
    auto& preview = application_state.bmson_marker_move_preview;
    const object_id_t event{input.object_id};
    if (!preview || preview->chart != state.chart ||
        preview->revision != history.document.revision || preview->event != event) {
        preview.reset();
        return "BGM event drag expired before release.";
    }
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !view || !std::isfinite(input.y_pixels)) {
        preview.reset();
        return "BGM event drag lost its chart field.";
    }
    const auto raw = position_at_compose_y(*chart, *view, input.y_pixels);
    const auto target =
        raw ? resolve_resource_placement_position(*chart, *raw, preview->snap_denominator)
            : std::nullopt;
    preview->virtual_canvas_row.reset();
    if (!target && !preview->linked_bmson)
        preview->virtual_canvas_row = virtual_bms_move_target(
            *chart, *view, input.y_pixels, preview->snap_denominator, virtual_bms_row_at);
    if (!target && !preview->virtual_canvas_row) {
        preview->valid = false;
        if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE) {
            preview.reset();
            return "BGM event drag cannot snap to this chart row.";
        }
        return compose_input_family_result_t::continue_batch(
            "BGM event drag cannot snap to this chart row.");
    }
    if (target)
        preview->target = *target;
    if (preview->linked_bmson) {
        const auto source = find_bmson_marker_source(*chart, event);
        if (!source) {
            preview.reset();
            return "BMSON BGM marker source event is unavailable.";
        }
        preview->valid = std::ranges::none_of(source->channel->events, [&](const auto& candidate) {
            return candidate.id != source->source_event && candidate.position == *target;
        });
    } else {
        preview->source_lane = object_id_t{input.lane_id};
        preview->valid = std::ranges::any_of(chart->bgm_source_lanes, [&](const auto& lane) {
            return lane.id == preview->source_lane;
        });
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE)
        return compose_input_family_result_t::continue_batch(
            preview->valid ? "BGM event drag preview updated." : "BGM move target is unavailable.");
    const auto captured = *preview;
    preview.reset();
    return captured.valid ? commit_bmson_bgm_move(input, context, captured)
                          : compose_input_family_result_t{"BGM move target is unavailable."};
}

auto edit_bmson_draft_marker(const WormUiComposeInputAction& input,
                             bmson_compose_context_t& context) -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [action, history, workspace, state, application_state, scene_cache, gesture_active] =
        context;
    if (gesture_active)
        return "Finish or cancel the active Compose gesture before editing a staged BMSON region.";
    auto& draft = application_state.bmson_marker_draft;
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
                "Staged BMSON region already has that assignment.");
        proposed[index].playable_lane = lane;
    }
    auto temporary = make_document_history(history.document, 1);
    const auto checked = insert_bmson_sound_markers(temporary, temporary.document.revision,
                                                    draft->chart, draft->reference, proposed);
    if (!checked)
        return checked.diagnostics.empty() ? "Staged BMSON region edit could not be validated."
                                           : checked.diagnostics.front().message;
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
    auto& [action, history, workspace, state, application_state, scene_cache, gesture_active] =
        context;
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
        return applied.diagnostics.front().message;
    if (!applied.applied)
        return "BMSON restart state did not change.";
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound)
        return rebound.diagnostics.empty()
                   ? "BMSON restart changed, but workspace rebinding failed."
                   : rebound.diagnostics.front().message;
    return "BMSON slice restart toggled.";
}

template <typename PositionAtComposeY, typename VirtualBmsRowAt>
auto dispatch_bmson_compose_input(
    const WormUiWorkspaceAction& ui_action, const WormUiComposeInputAction& input,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::editor_interaction_state_t& state, application_command_state_t& application_state,
    compose_scene_cache_t& scene_cache, bool gesture_active,
    PositionAtComposeY&& position_at_compose_y, VirtualBmsRowAt&& virtual_bms_row_at)
    -> compose_input_family_result_t {
    auto context = bmson_compose_context_t{ui_action,         history,     workspace,     state,
                                           application_state, scene_cache, gesture_active};
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN:
        return begin_bmson_bgm_move(input, context);
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE:
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE:
        return update_bmson_bgm_move(input, context, position_at_compose_y, virtual_bms_row_at);
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL:
        application_state.bmson_marker_move_preview.reset();
        return "BMSON BGM marker drag canceled.";
    case WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE:
    case WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART:
        return dispatch_bmson_razor_input(input, history, state, application_state, gesture_active,
                                          position_at_compose_y);
    case WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT: {
        const auto& draft = application_state.bmson_marker_draft;
        if (!draft || draft->chart != state.chart || draft->revision != history.document.revision ||
            input.object_id == 0 || input.object_id > draft->markers.size())
            return "The BMSON region audition request expired.";
        scene_cache.draft_audition_request = bmson_draft_audition_request_t{
            draft->chart, draft->revision, draft->reference, draft->markers,
            static_cast<std::size_t>(input.object_id - 1)};
        return compose_input_family_result_t::continue_batch(
            "Preparing staged BMSON region audition.");
    }
    case WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE:
    case WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM:
    case WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART:
    case WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT:
        return edit_bmson_draft_marker(input, context);
    case WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE:
        return toggle_bmson_restart(input, context);
    default:
        return {};
    }
}

void update_compose_viewport_pointer_input(const WormUiWorkspaceAction& ui_action,
                                           wormedit::viewport_input_controller_t& viewport_input,
                                           compose_scene_cache_t& scene_cache,
                                           double bgm_pane_width, double field_left_pixels,
                                           double field_width_pixels) {
    using namespace wormedit;
    if (!ui_action.shortcut_window_focused) {
        viewport_input.focus_lost();
        scene_cache.viewport_autoscroll_active = false;
    } else {
        if (ui_action.compose_viewport_middle_click) {
            const auto middle_click_pane =
                bgm_pane_width > 0.0 &&
                        std::isfinite(ui_action.compose_viewport_middle_click_x_pixels) &&
                        ui_action.compose_viewport_middle_click_x_pixels >=
                            field_left_pixels + field_width_pixels &&
                        ui_action.compose_viewport_middle_click_x_pixels <=
                            ui_action.compose_input_viewport_width_pixels
                    ? viewport_pane_t::bgm
                    : viewport_pane_t::chart;
            viewport_input.middle_click(middle_click_pane,
                                        ui_action.compose_viewport_middle_click_x_pixels,
                                        ui_action.compose_viewport_middle_click_y_pixels);
            scene_cache.viewport_autoscroll_active = viewport_input.autoscroll_active();
            if (scene_cache.viewport_autoscroll_active) {
                scene_cache.viewport_autoscroll_anchor_x_pixels =
                    ui_action.compose_viewport_middle_click_x_pixels;
                scene_cache.viewport_autoscroll_anchor_y_pixels =
                    ui_action.compose_viewport_middle_click_y_pixels;
            }
        }
        if (ui_action.compose_viewport_pointer_valid) {
            viewport_input.update_autoscroll_pointer(ui_action.compose_viewport_pointer_x_pixels,
                                                     ui_action.compose_viewport_pointer_y_pixels);
            scene_cache.viewport_autoscroll_pointer_x_pixels =
                ui_action.compose_viewport_pointer_x_pixels;
            scene_cache.viewport_autoscroll_pointer_y_pixels =
                ui_action.compose_viewport_pointer_y_pixels;
        }
    }
    if (ui_action.compose_input_count != 0 && !ui_action.compose_viewport_middle_click) {
        if (viewport_input.normal_edit_press()) {
            scene_cache.viewport_autoscroll_active = false;
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
    if (!ui_action.compose_has_pointer_hover || gesture_active || !chart || !interaction_view) {
        scene_cache.hover.reset();
    } else {
        if (!scene_cache.hit_test_index) {
            auto built = build_editor_hit_test_index(*chart, revision);
            if (std::holds_alternative<editor_hit_test_index_t>(built)) {
                scene_cache.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
            }
        }
        if (scene_cache.hit_test_index) {
            scene_cache.hover = hover_editor_object(
                *chart, *scene_cache.hit_test_index, *interaction_view,
                {.x_pixels = ui_action.compose_pointer_hover_x_pixels,
                 .y_pixels = compose_source_y(ui_action.compose_pointer_hover_y_pixels,
                                              interaction_view->viewport_height_pixels,
                                              compose_field_downscroll(*chart, preferences)),
                 .field_left_pixels = field_left_pixels,
                 .field_width_pixels = field_width_pixels,
                 .hit_radius_pixels = 8.0,
                 .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
        }
    }
}

struct compose_input_context_t {
    WormUiWorkspaceAction& ui_action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    wormedit::editor_interaction_state_t& state;
    wormedit::viewport_input_controller_t& viewport_input;
    wormedit::editor_navigation_input_controller_t& navigation_input;
    application_command_state_t& application_state;
    compose_scene_cache_t& scene_cache;
    const wormedit::chart_document_t* input_chart{};
    bool gesture_active{};
    std::size_t bgm_lane_count{};
    double bgm_pane_width{};
    double field_left_pixels{};
    double field_width_pixels{};

    auto input_is_current() const -> bool {
        return history.document.revision == ui_action.compose_input_document_revision &&
               workspace.active_tab == wormedit::workspace_tab_t::compose &&
               workspace.selected_chart &&
               workspace.selected_chart->value == ui_action.compose_input_chart_id &&
               state.chart.value == ui_action.compose_input_chart_id;
    }

    auto chart() const -> const wormedit::chart_document_t* {
        const auto found = std::ranges::find(history.document.charts, state.chart,
                                             &wormedit::chart_document_t::id);
        return found == history.document.charts.end() ? nullptr : &*found;
    }
    auto selection_mode(WormUiComposeSelectionMode mode) const
        -> std::optional<wormedit::editor_area_selection_mode_t> {
        using mode_t = wormedit::editor_area_selection_mode_t;
        switch (mode) {
        case WORM_UI_COMPOSE_SELECTION_REPLACE:
            return mode_t::replace;
        case WORM_UI_COMPOSE_SELECTION_ADD:
            return mode_t::add;
        case WORM_UI_COMPOSE_SELECTION_TOGGLE:
            return mode_t::toggle;
        }
        return std::nullopt;
    }
    auto input_source_y(double y) const -> double {
        return compose_source_y(y, state.viewport_height_pixels,
                                input_chart && compose_field_downscroll(*input_chart, application_state.editor_preferences));
    }
    // Notes cannot sit in a Stop, Delay or Warp: a pointer there targets nothing to place on.
    auto in_dead_span(const wormedit::chart_document_t& chart,
                      const wormedit::editor_interaction_view_model_t& view, double y_pixels) const
        -> bool {
        if (!scene_cache.dead_spans || chart.divisions_per_standard_measure == 0) return false;
        const auto pixels_per_row = view.pixels_per_standard_measure /
                                    static_cast<double>(chart.divisions_per_standard_measure);
        const auto bands = project_dead_span_bands(
            *scene_cache.dead_spans, pixels_per_row, view.viewport_scroll_pixels,
            view.viewport_height_pixels,
            compose_field_downscroll(chart, application_state.editor_preferences),
            dead_span_pause_band_pixels);
        return pointer_in_dead_span(bands, y_pixels);
    }
    auto hit_target_at(const wormedit::chart_document_t& chart,
                       const wormedit::editor_interaction_view_model_t& view, double x_pixels,
                       double y_pixels) -> std::optional<wormedit::editor_hit_target_t> {
        auto target = hit_target_unrestricted(chart, view, x_pixels, y_pixels);
        if (target && !target->note && in_dead_span(chart, view, y_pixels)) return std::nullopt;
        return target;
    }
    auto hit_target_unrestricted(const wormedit::chart_document_t& chart,
                                 const wormedit::editor_interaction_view_model_t& view,
                                 double x_pixels, double y_pixels)
        -> std::optional<wormedit::editor_hit_target_t> {
        if (!scene_cache.hit_test_index) {
            auto built = wormedit::build_editor_hit_test_index(chart, history.document.revision);
            if (!std::holds_alternative<wormedit::editor_hit_test_index_t>(built))
                return std::nullopt;
            scene_cache.hit_test_index =
                std::get<wormedit::editor_hit_test_index_t>(std::move(built));
        }
        return wormedit::hit_test_editor(
            chart, *scene_cache.hit_test_index, view,
            {.x_pixels = x_pixels,
             .y_pixels = compose_source_y(y_pixels, view.viewport_height_pixels,
                                          compose_field_downscroll(chart, application_state.editor_preferences)),
             .field_left_pixels = field_left_pixels,
             .field_width_pixels = field_width_pixels});
    }
    auto position_at_compose_y(const wormedit::chart_document_t& chart,
                               const wormedit::editor_interaction_view_model_t& view,
                               double y_pixels) const -> std::optional<wormedit::local_position_t> {
        if (in_dead_span(chart, view, y_pixels)) return std::nullopt;
        return position_at_compose_y_unrestricted(chart, view, y_pixels);
    }
    auto position_at_compose_y_unrestricted(const wormedit::chart_document_t& chart,
                                            const wormedit::editor_interaction_view_model_t& view,
                                            double y_pixels) const
        -> std::optional<wormedit::local_position_t> {
        return wormedit::editor_position_at_y_pixels(
            chart, view,
            compose_source_y(y_pixels, view.viewport_height_pixels,
                             compose_field_downscroll(chart, application_state.editor_preferences)));
    }
    auto virtual_bms_row_at(const wormedit::chart_document_t& chart,
                            const wormedit::editor_interaction_view_model_t& view,
                            double y_pixels) const -> std::optional<std::uint64_t> {
        using namespace wormedit;
        if (!chart_has_virtual_tail(chart) || !std::isfinite(y_pixels) ||
            y_pixels < 0.0 || y_pixels >= view.viewport_height_pixels)
            return std::nullopt;
        const auto built = build_virtual_canvas(chart);
        const auto* canvas = std::get_if<virtual_canvas_t>(&built);
        if (!canvas)
            return std::nullopt;
        const auto source_y = compose_source_y(y_pixels, view.viewport_height_pixels,
                                               compose_field_downscroll(chart, application_state.editor_preferences));
        const auto row = (static_cast<long double>(view.viewport_scroll_pixels) + source_y) *
                         chart.divisions_per_standard_measure / view.pixels_per_standard_measure;
        if (!std::isfinite(row) || row < 0.0L ||
            row >= static_cast<long double>(canvas->total_rows))
            return std::nullopt;
        const auto canvas_row = static_cast<std::uint64_t>(std::floor(row + 0.5L));
        const auto measure = canvas->measure_containing(canvas_row);
        return measure && !measure->authored ? std::optional{canvas_row} : std::nullopt;
    }
    auto virtual_bms_hit_at(const wormedit::chart_document_t& chart,
                            const wormedit::editor_interaction_view_model_t& view, double x_pixels,
                            double y_pixels)
        -> std::optional<std::pair<std::uint64_t, wormedit::lane_id_t>> {
        if (!scene_cache.index)
            return std::nullopt;
        const auto row = virtual_bms_row_at(chart, view, y_pixels);
        if (!row)
            return std::nullopt;
        auto request = scene_cache.preview_scene_request;
        request.include_virtual_bms_tail = true;
        const auto composed =
            wormedit::make_editor_field_scene(chart, *scene_cache.index, view, request);
        if (!composed.scene || composed.has_errors())
            return std::nullopt;
        for (const auto& lane : composed.scene->lanes) {
            if (x_pixels >= lane.bounds.left && x_pixels < lane.bounds.right &&
                y_pixels >= lane.bounds.top && y_pixels < lane.bounds.bottom)
                return std::pair{*row, lane.lane};
        }
        return std::nullopt;
    }
};

struct compose_gesture_context_t {
    wormedit::document_history_t& history;
    wormedit::editor_interaction_state_t& state;
    application_command_state_t& application_state;
    wormedit::editor_interaction_result_t& result;
    std::string_view& accepted;
};

template <typename HitTargetAt>
auto begin_alt_pointer_gesture(const WormUiComposeInputAction& input,
                               compose_gesture_context_t& context, HitTargetAt&& hit_target_at)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [history, state, application_state, result, accepted] = context;
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !view) {
        return "The selected chart field is no longer available.";
    }
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!target)
        return "Alt/Option-drag must begin over an editable lane.";
    if (target->note && target->note_kind &&
        (*target->note_kind == note_kind_t::tap || *target->note_kind == note_kind_t::hold)) {
        state.active_lane = target->lane;
        place_cursor_at(state, target->position);
        result = dispatch_editor_action(history, state,
                                        begin_note_duration_gesture_action_t{*target->note});
        accepted = "Note resize started; drag to change duration and release to finish.";
    } else if (target->note) {
        return "Alt/Option-drag resizes tap and hold notes; this note kind is not resizable.";
    } else {
        result = dispatch_editor_action(
            history, state,
            begin_alt_pointer_hold_gesture_action_t{
                .lane = target->lane, .position = target->position, .alt_or_option_down = true});
        accepted = "Pointer hold started; release to finish.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename PositionAtComposeY>
auto update_alt_pointer_gesture(const WormUiComposeInputAction& input,
                                compose_gesture_context_t& context,
                                PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [history, state, application_state, result, accepted] = context;
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "The selected chart field is no longer available.";
    }
    const auto y =
        std::clamp(input.y_pixels, 0.0, std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto position = position_at_compose_y(*chart, *view, y);
    if (!position) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Pointer hold input fell outside chart geometry and was canceled.";
    }
    const auto releasing = input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE;
    if (state.pending_note_duration_gesture) {
        result = releasing
                     ? dispatch_editor_action(history, state,
                                              release_note_duration_gesture_action_t{*position})
                     : dispatch_editor_action(history, state,
                                              update_note_duration_gesture_action_t{*position});
        accepted = releasing ? (result.document_changed ? "Note duration committed."
                                                        : "Note resize released.")
                             : "Note resize preview updated.";
    } else {
        result = releasing
                     ? dispatch_editor_action(
                           history, state,
                           release_alt_pointer_hold_gesture_action_t{.position = *position,
                                                                     .alt_or_option_down = true,
                                                                     .pointer_captured = true})
                     : dispatch_editor_action(
                           history, state,
                           update_alt_pointer_hold_gesture_action_t{.position = *position,
                                                                    .alt_or_option_down = true,
                                                                    .pointer_captured = true});
        accepted = releasing ? (result.document_changed ? "Pointer hold committed."
                                                        : "Pointer hold released.")
                             : "Pointer hold preview updated.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename HitTargetAt, typename PositionAtComposeY>
auto dispatch_alt_pointer_gesture(const WormUiComposeInputAction& input,
                                  compose_gesture_context_t& context, HitTargetAt&& hit_target_at,
                                  PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN)
        return begin_alt_pointer_gesture(input, context, hit_target_at);
    return update_alt_pointer_gesture(input, context, position_at_compose_y);
}

template <typename HitTargetAt>
auto dispatch_note_paint_gesture(const WormUiComposeInputAction& input,
                                 compose_gesture_context_t& context, HitTargetAt&& hit_target_at)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [history, state, application_state, result, accepted] = context;
    if (!application_state.note_paint_mode_active) {
        if (state.pending_note_paint_gesture) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            accepted = "Note-paint stroke canceled because paint mode is inactive.";
        } else {
            result.accepted = true;
            accepted = "Note-paint input ignored because paint mode is inactive.";
        }
        return compose_input_family_result_t::continue_with_result();
    }
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "The selected chart field is no longer available.";
    }
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!target) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN)
            return "Begin note painting over an editable chart lane.";
        result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        accepted = "Note-paint stroke canceled outside editable chart geometry.";
        return compose_input_family_result_t::continue_with_result();
    }
    const auto captured = true;
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
        result = dispatch_editor_action(
            history, state,
            begin_note_paint_gesture_action_t{
                .lane = target->lane, .position = target->position, .pointer_captured = captured});
        accepted = "Note-paint stroke started.";
    } else if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE) {
        result = dispatch_editor_action(
            history, state,
            update_note_paint_gesture_action_t{
                .lane = target->lane, .position = target->position, .pointer_captured = captured});
        accepted = "Note-paint preview updated.";
    } else {
        result = dispatch_editor_action(
            history, state,
            end_note_paint_gesture_action_t{
                .lane = target->lane, .position = target->position, .pointer_captured = captured});
        accepted = result.document_changed ? "Note-paint stroke committed."
                                           : "Note-paint stroke released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename HitTargetAt>
auto dispatch_semantic_layer_paint_gesture(const WormUiComposeInputAction& input,
                                           compose_gesture_context_t& context,
                                           HitTargetAt&& hit_target_at)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [history, state, application_state, result, accepted] = context;
    if (!application_state.semantic_layer_paint_mode_active ||
        !application_state.semantic_layer_target) {
        if (state.pending_semantic_layer_paint_gesture) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            accepted = "Semantic-layer paint stroke canceled because paint mode is inactive.";
        } else {
            result.accepted = true;
            accepted = "Semantic-layer paint input ignored because paint mode is inactive.";
        }
        return compose_input_family_result_t::continue_with_result();
    }
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "The selected chart field is no longer available.";
    }
    const auto* layer =
        visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
    if (!layer || layer->locked) {
        application_state.semantic_layer_paint_mode_active = false;
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Choose an unlocked visible semantic layer before painting note membership.";
    }
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    const auto beginning = input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN;
    const auto updating = input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE;
    if (!target || !target->note) {
        if (beginning)
            return "Begin semantic-layer painting over a playable note.";
        if (updating) {
            result.accepted = true;
            accepted = "Semantic-layer paint moved between playable notes.";
        } else {
            result = dispatch_editor_action(history, state,
                                            end_semantic_layer_paint_gesture_action_t{
                                                .note = object_id_t{}, .pointer_captured = true});
            accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                               : "Semantic-layer paint stroke released.";
        }
        return compose_input_family_result_t::continue_with_result();
    }
    if (beginning) {
        result = dispatch_editor_action(
            history, state,
            begin_semantic_layer_paint_gesture_action_t{
                .layer = layer->id, .note = *target->note, .pointer_captured = true});
        accepted = "Semantic-layer paint stroke started.";
    } else if (updating) {
        result = dispatch_editor_action(history, state,
                                        update_semantic_layer_paint_gesture_action_t{
                                            .note = *target->note, .pointer_captured = true});
        accepted = "Semantic-layer paint preview updated.";
    } else {
        result = dispatch_editor_action(history, state,
                                        end_semantic_layer_paint_gesture_action_t{
                                            .note = *target->note, .pointer_captured = true});
        accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                           : "Semantic-layer paint stroke released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename HitTargetAt, typename PositionAtComposeY>
auto dispatch_note_move_gesture(const WormUiComposeInputAction& input,
                                compose_gesture_context_t& context, HitTargetAt&& hit_target_at,
                                PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [history, state, application_state, result, accepted] = context;
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto view = compose_view_for_state(history, state);
    if (chart == history.document.charts.end() || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "The selected chart field is no longer available.";
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN) {
        const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
        if (!target || !target->note ||
            std::ranges::find(state.selected_notes, *target->note) == state.selected_notes.end())
            return "Begin a note move by dragging a selected playable note.";
        state.active_lane = target->lane;
        place_cursor_at(state, target->position);
        result =
            dispatch_editor_action(history, state, begin_note_move_gesture_action_t{*target->note});
        accepted = "Selected-note move started.";
        return compose_input_family_result_t::continue_with_result();
    }
    const auto y =
        std::clamp(input.y_pixels, 0.0, std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto position = position_at_compose_y(*chart, *view, y);
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!position || !target) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Note move input fell outside chart lane geometry and was canceled.";
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE) {
        result = dispatch_editor_action(history, state,
                                        update_note_move_gesture_action_t{*position, target->lane});
        accepted = "Selected-note move preview updated.";
    } else {
        result = dispatch_editor_action(
            history, state, release_note_move_gesture_action_t{*position, target->lane});
        accepted =
            result.document_changed ? "Selected notes moved." : "Selected-note move released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename HitTargetAt, typename PositionAtComposeY>
auto dispatch_compose_gesture_input(const WormUiComposeInputAction& input,
                                    wormedit::document_history_t& history,
                                    wormedit::editor_interaction_state_t& state,
                                    application_command_state_t& application_state,
                                    wormedit::editor_interaction_result_t& result,
                                    std::string_view& accepted, HitTargetAt&& hit_target_at,
                                    PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    auto context = compose_gesture_context_t{history, state, application_state, result, accepted};
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE:
        return dispatch_alt_pointer_gesture(input, context, hit_target_at, position_at_compose_y);
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE:
        return dispatch_semantic_layer_paint_gesture(input, context, hit_target_at);
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE:
        return dispatch_note_paint_gesture(input, context, hit_target_at);
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE:
        return dispatch_note_move_gesture(input, context, hit_target_at, position_at_compose_y);
    default:
        return {};
    }
}

template <typename SelectionMode, typename InputSourceY>
auto dispatch_compose_selection_input(const WormUiComposeInputAction& input,
                                      wormedit::document_history_t& history,
                                      wormedit::editor_interaction_state_t& state,
                                      wormedit::editor_interaction_result_t& result,
                                      std::string_view& accepted, SelectionMode&& selection_mode,
                                      InputSourceY&& input_source_y, double field_left_pixels,
                                      double field_width_pixels,
                                      const std::shared_ptr<const wormedit::editor_area_geometry_snapshot_t>& geometry)
    -> compose_input_family_result_t {
    using namespace wormedit;
    switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN:
            if (!selection_mode(input.selection_mode).has_value()) {
                return "Compose area selection used an unknown selection mode.";
            }
            result = dispatch_editor_action(history, state,
                                            begin_editor_area_selection_action_t{
                                                .x_pixels = input.x_pixels,
                                                .y_pixels = input_source_y(input.y_pixels),
                                                .field_left_pixels = field_left_pixels,
                                                .field_width_pixels = field_width_pixels,
                                                .mode = *selection_mode(input.selection_mode),
                                                .scope = geometry
                                                    ? editor_selection_scope_t{true, true, true, true}
                                                    : editor_selection_scope_t{},
                                                .canvas_pointer_capture = true,
                                                .object_geometry = geometry,
                                            });
            accepted = "Area selection started.";
            break;
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE:
            result = dispatch_editor_action(
                history, state,
                update_editor_area_selection_action_t{
                    input.x_pixels, input_source_y(input.y_pixels), true});
            accepted = "Area selection preview updated.";
            break;
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE:
            result = dispatch_editor_action(
                history, state,
                commit_editor_area_selection_action_t{
                    input.x_pixels, input_source_y(input.y_pixels), true});
            accepted = "Area selection committed.";
            break;
        case WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION:
            if (!selection_mode(input.selection_mode).has_value()) {
                return "Compose Shift range used an unknown selection mode.";
            }
            result = dispatch_editor_action(history, state,
                                            space_range_selection_action_t{
                                                .mode = *selection_mode(input.selection_mode),
                                            });
            accepted = state.pending_space_range_selection
                           ? "Shift range selection started; navigate and release Shift to commit."
                           : "Shift range selection committed.";
            break;
    default:
        return {};
    }
    return compose_input_family_result_t::continue_with_result();
}

auto begin_compose_beat_fit(const WormUiComposeInputAction&, wormedit::document_history_t&,
                            wormedit::workspace_controller_t&,
                            wormedit::editor_interaction_state_t&, application_command_state_t&,
                            compose_scene_cache_t&, const wormedit::chart_document_t*)
    -> std::string;
auto update_compose_beat_fit(const WormUiComposeInputAction&, wormedit::document_history_t&,
                             wormedit::workspace_controller_t&,
                             wormedit::editor_interaction_state_t&, application_command_state_t&,
                             compose_scene_cache_t&) -> std::string;

auto dispatch_compose_beat_fit_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    const wormedit::chart_document_t* input_chart) -> std::optional<std::string> {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN:
        return begin_compose_beat_fit(input, history, workspace, state, application_state,
                                      scene_cache, input_chart);
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE: {
        auto source_input = input;
        source_input.y_pixels = compose_source_y(input.y_pixels, state.viewport_height_pixels,
                                                input_chart && compose_field_downscroll(*input_chart));
        return update_compose_beat_fit(source_input, history, workspace, state, application_state,
                                       scene_cache);
    }
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL:
        scene_cache.beat_fit_drag.reset();
        scene_cache.beat_fit_asset.reset();
        return "Beat fit cancelled; timing was unchanged.";
    default:
        return std::nullopt;
    }
}

auto dispatch_compose_offset_fit_input(
    const WormUiComposeInputAction& input, compose_input_context_t& context)
    -> std::optional<std::string> {
    using namespace wormedit;
    auto& cache = context.scene_cache;
    auto& action = context.ui_action;
    if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL) {
        cache.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return "Offset fit cancelled; timing was unchanged.";
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN) {
        if (context.application_state.audio_offset_edits_locked() || cache.offset_fit_drag ||
            !cache.marker_timing_index || !context.workspace.transport.audio_asset ||
            input.object_id == 0) {
            return "Offset fitting requires an unlocked waveform and idle chart gesture.";
        }
        cache.beat_fit_drag.reset();
        cache.beat_fit_asset.reset();
        const auto status = begin_compose_beat_fit(input, context.history, context.workspace,
                                                   context.state, context.application_state, cache,
                                                   context.input_chart);
        if (!cache.beat_fit_drag) return status;
        cache.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{
            .beat = *cache.beat_fit_drag,
            .timing = *cache.marker_timing_index,
            .asset = *context.workspace.transport.audio_asset,
            .target_y_pixels = input.y_pixels};
        cache.beat_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_BEGIN;
        action.audio_offset_drag_chart_id = context.state.chart.value;
        action.audio_offset_drag_document_revision = context.history.document.revision;
        action.audio_offset_drag_sample_rate = cache.offset_fit_drag->beat.sample_rate;
        action.audio_offset_drag_delta_frames = 0;
        return "Dragging beat to shift waveform offset.";
    }
    if (input.kind != WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE &&
        input.kind != WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE) return std::nullopt;
    if (!cache.offset_fit_drag ||
        cache.offset_fit_drag->beat.document_revision != context.history.document.revision ||
        cache.offset_fit_drag->asset != context.workspace.transport.audio_asset ||
        context.application_state.audio_offset_edits_locked()) {
        cache.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return "Offset fit expired after its chart or waveform changed.";
    }
    const auto& drag = *cache.offset_fit_drag;
    cache.offset_fit_drag->target_y_pixels = input.y_pixels;
    const auto target = sample_frame_at_chart_y(
        drag.timing, context.state.viewport_scroll_pixels, context.state.pixels_per_standard_measure,
        context.input_source_y(input.y_pixels), context.state.viewport_height_pixels, drag.beat.sample_rate);
    if (!target || *target >= cache.waveform_overview_total_frames) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE) {
            cache.offset_fit_drag.reset();
            action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        }
        return "Offset target is outside the source audio range.";
    }
    constexpr auto max_frame = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (*target > max_frame || drag.beat.original_sample_frame > max_frame) {
        cache.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return "Offset target exceeds the supported frame range.";
    }
    const auto delta = static_cast<std::int64_t>(*target) -
                       static_cast<std::int64_t>(drag.beat.original_sample_frame);
    action.audio_offset_drag_action = input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE
                                          ? WORM_UI_AUDIO_OFFSET_DRAG_COMMIT
                                          : WORM_UI_AUDIO_OFFSET_DRAG_UPDATE;
    action.audio_offset_drag_chart_id = drag.beat.chart.value;
    action.audio_offset_drag_document_revision = drag.beat.document_revision;
    action.audio_offset_drag_sample_rate = drag.beat.sample_rate;
    action.audio_offset_drag_delta_frames = delta;
    if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE) cache.offset_fit_drag.reset();
    return input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE
               ? "Offset fit committed."
               : "Previewing exact waveform offset.";
}

auto begin_compose_beat_fit(const WormUiComposeInputAction& input,
                            wormedit::document_history_t& history,
                            wormedit::workspace_controller_t& workspace,
                            wormedit::editor_interaction_state_t& state,
                            application_command_state_t& application_state,
                            compose_scene_cache_t& scene_cache,
                            const wormedit::chart_document_t* input_chart) -> std::string {
    using namespace wormedit;
    if (application_state.timing_locked || scene_cache.beat_fit_drag ||
        editor_interaction_has_pending_gesture(state) || !scene_cache.index ||
        !scene_cache.marker_timing_index || scene_cache.waveform_overview_sample_rate == 0 ||
        scene_cache.waveform_overview_total_frames == 0 || !input_chart || input.object_id == 0) {
        return "Beat fitting requires an unlocked waveform and idle chart gesture.";
    }
    const auto& measures = scene_cache.index->measures;
    const auto beat_row = input.object_id;
    const auto measure = std::ranges::upper_bound(measures.entries, beat_row, {},
                                                  &measure_index_entry_t::absolute_begin_row);
    if (measure == measures.entries.begin()) {
        return "Selected beat is outside the chart.";
    }
    const auto& entry = *std::prev(measure);
    if (beat_row - entry.absolute_begin_row >= entry.row_capacity) {
        return "Selected beat is outside the chart.";
    }
    const auto beat = local_position_t{entry.id, beat_row - entry.absolute_begin_row};
    std::optional<object_id_t> previous_bpm;
    std::uint64_t previous_row{};
    for (const auto& bpm : input_chart->bpms) {
        const auto row = absolute_row(measures, bpm.position);
        if (row && *row < beat_row && (!previous_bpm || *row > previous_row)) {
            previous_bpm = bpm.id;
            previous_row = *row;
        }
    }
    if (!previous_bpm) {
        return "No earlier BPM change can anchor this beat fit.";
    }
    const auto begun = begin_beat_interval_drag(history.document, input_chart->id, *previous_bpm,
                                                beat, scene_cache.waveform_overview_sample_rate,
                                                application_state.timing_locked);
    if (!begun) {
        return begun.diagnostics.empty() ? "Beat fit could not start."
                                         : begun.diagnostics.front().message;
    }
    if (begun.drag->original_sample_frame >= scene_cache.waveform_overview_total_frames) {
        return "Selected beat lies after the source audio ends.";
    }
    scene_cache.beat_fit_drag = begun.drag;
    scene_cache.beat_fit_asset = workspace.transport.audio_asset;
    return "Dragging beat against the fixed waveform; release to apply.";
}

auto update_beat_fit_proposal(const WormUiComposeInputAction& input,
                             const wormedit::document_history_t& history,
                             const wormedit::beat_interval_drag_t& drag,
                             std::uint64_t target_frame,
                             bool timing_locked) -> wormedit::beat_interval_drag_result_t {
    using namespace wormedit;
    const auto frame_update = update_beat_interval_drag(history.document, drag,
                                                        target_frame, timing_locked);
    if (!frame_update || !input.beat_fit_ctrl_held) return frame_update;
    const auto snapped_bpm = quantize_bpm(frame_update.drag->proposed_bpm,
        {input.beat_fit_snap_step_numerator, input.beat_fit_snap_step_denominator});
    if (!snapped_bpm) return {};
    return update_beat_interval_drag_to_bpm(history.document, *frame_update.drag,
                                            *snapped_bpm, timing_locked);
}

auto update_compose_beat_fit(const WormUiComposeInputAction& input,
                             wormedit::document_history_t& history,
                             wormedit::workspace_controller_t& workspace,
                             wormedit::editor_interaction_state_t& state,
                             application_command_state_t& application_state,
                             compose_scene_cache_t& scene_cache) -> std::string {
    using namespace wormedit;
    if (!scene_cache.beat_fit_drag || !scene_cache.marker_timing_index ||
        scene_cache.beat_fit_drag->document_revision != history.document.revision) {
        scene_cache.beat_fit_drag.reset();
        scene_cache.beat_fit_asset.reset();
        return "Beat fit has no matching captured transaction.";
    }
    const auto frame = sample_frame_at_chart_y(
        *scene_cache.marker_timing_index, state.viewport_scroll_pixels,
        state.pixels_per_standard_measure, input.y_pixels, state.viewport_height_pixels,
        scene_cache.beat_fit_drag->sample_rate);
    if (!frame || *frame >= scene_cache.waveform_overview_total_frames) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
            scene_cache.beat_fit_drag.reset();
            scene_cache.beat_fit_asset.reset();
        }
        return "Beat target is outside the source audio range.";
    }
    const auto updated = update_beat_fit_proposal(
        input, history, *scene_cache.beat_fit_drag, *frame, application_state.timing_locked);
    if (updated && updated.drag->proposed_sample_frame >=
                       scene_cache.waveform_overview_total_frames) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
            scene_cache.beat_fit_drag.reset();
            scene_cache.beat_fit_asset.reset();
        }
        return "Snapped beat target is outside the source audio range.";
    }
    if (!updated) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
            scene_cache.beat_fit_drag.reset();
            scene_cache.beat_fit_asset.reset();
        }
        return updated.diagnostics.empty() ? "Beat fit target was rejected."
                                           : updated.diagnostics.front().message;
    }
    scene_cache.beat_fit_drag = updated.drag;
    scene_cache.beat_fit_drag->snap_active = input.beat_fit_ctrl_held;
    if (input.beat_fit_ctrl_held) {
        scene_cache.beat_fit_drag->snap_step = {input.beat_fit_snap_step_numerator,
                                                input.beat_fit_snap_step_denominator};
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE) {
        return "Previewing exact sample-frame beat fit.";
    }
    const auto committed = commit_beat_interval_drag(history, *scene_cache.beat_fit_drag,
                                                     application_state.timing_locked);
    scene_cache.beat_fit_drag.reset();
    scene_cache.beat_fit_asset.reset();
    if (!committed.applied) {
        return committed.diagnostics.empty() ? "Beat fit was unchanged."
                                             : committed.diagnostics.front().message;
    }
    const auto rebound = rebind_workspace(workspace, history);
    return rebound ? "Beat interval fitted to the selected audio frame."
                   : "Beat fit applied, but workspace rebinding failed.";
}

auto seek_compose_waveform(const WormUiComposeInputAction& input, compose_input_context_t& context,
                           wormedit::editor_interaction_result_t& result,
                           std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto& cache = context.scene_cache;
    const auto& state = context.state;
    if (!std::isfinite(input.audio_fraction) || input.audio_fraction < 0.0 ||
        input.audio_fraction > 1.0 || !cache.marker_timing_index ||
        cache.waveform_overview_total_frames == 0 || cache.waveform_overview_sample_rate == 0 ||
        !context.input_chart)
        return "Waveform overview seek is unavailable.";
    if (editor_interaction_has_pending_gesture(state))
        return "Finish or cancel the active gesture before navigating the waveform.";
    const auto& timing = *cache.marker_timing_index;
    const auto frame = static_cast<std::int64_t>(
        input.audio_fraction * static_cast<long double>(cache.waveform_overview_total_frames - 1));
    const auto musical =
        musical_position_at(timing, signed_rational_t{frame, cache.waveform_overview_sample_rate});
    long double row = 0.0L;
    if (musical && musical.absolute_row && musical.absolute_row->denominator != 0) {
        row = static_cast<long double>(musical.absolute_row->numerator) /
              musical.absolute_row->denominator;
    } else {
        const auto end = timing_at(timing, timing.total_rows);
        if (end && end.seconds) {
            const auto end_seconds =
                static_cast<long double>(end.seconds->numerator) / end.seconds->denominator;
            const auto audio_seconds =
                static_cast<long double>(frame) / cache.waveform_overview_sample_rate;
            row = audio_seconds >= end_seconds ? static_cast<long double>(timing.total_rows) : 0.0L;
        }
    }
    const auto target = static_cast<double>(row * state.pixels_per_standard_measure /
                                                timing.divisions_per_standard_measure -
                                            state.viewport_height_pixels * 0.5);
    result =
        dispatch_editor_action(context.history, context.state,
                               scroll_editor_view_action_t{target - state.viewport_scroll_pixels});
    accepted = "Compose waveform overview navigated.";
    return std::nullopt;
}

auto click_compose_waveform(const WormUiComposeInputAction& input, compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    if (input.overview_shift) return seek_compose_waveform(input, context, result, accepted);
    auto& cache = context.scene_cache;
    if (!cache.marker_timing_index || !cache.index || !context.input_chart ||
        editor_interaction_has_pending_gesture(context.state))
        return "Waveform overview seek is unavailable during an active gesture.";
    const auto end = context.application_state.transport_preview_end_frame
        ? context.application_state.transport_preview_end_frame() : std::nullopt;
    const auto target = compose_minimap_target(input.audio_fraction, cache.waveform_overview_total_frames,
        cache.waveform_overview_sample_rate, audio::output_sample_rate, *cache.marker_timing_index,
        context.state.active_snap_denominator, end);
    if (!target) return "Waveform overview seek is unavailable.";
    const auto status = navigate_compose_minimap_transport(context.application_state, target->frame,
                                                          false, input.overview_primary);
    result = move_compose_minimap_cursor(context.history, context.state, cache.index->measures,
                                         *cache.marker_timing_index, *target);
    if (!result.accepted) return compose_result_status(result, "Waveform cursor moved.");
    retain_compose_minimap_start(context.application_state, context.history, context.state,
                                 *target, *cache.marker_timing_index);
    accepted = "Compose waveform position moved.";
    if (!status.empty()) return status;
    return std::nullopt;
}

auto focus_compose_note_label(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.history, context.state);
    if (!chart || !view)
        return "The selected chart field is no longer available.";
    if (!context.scene_cache.hit_test_index) {
        auto built = build_editor_hit_test_index(*chart, context.history.document.revision);
        if (std::holds_alternative<editor_hit_test_index_t>(built))
            context.scene_cache.hit_test_index =
                std::get<editor_hit_test_index_t>(std::move(built));
    }
    if (!context.scene_cache.hit_test_index || !context.scene_cache.interaction_view)
        return "The chart field hover index is unavailable.";
    const auto hover = hover_editor_object(
        *chart, *context.scene_cache.hit_test_index, *context.scene_cache.interaction_view,
        {.x_pixels = input.x_pixels,
         .y_pixels = context.input_source_y(input.y_pixels),
         .field_left_pixels = context.field_left_pixels,
         .field_width_pixels = context.field_width_pixels,
         .hit_radius_pixels = 8.0,
         .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
    if (!hover || hover->object.kind != editor_selection_object_kind_t::note)
        return "Double-click a note to edit its label.";
    result = dispatch_editor_action(context.history, context.state,
                                    select_editor_notes_action_t{{hover->object.id}});
    if (!result)
        return compose_result_status(result, "Note label editor could not be focused.");
    context.scene_cache.focus_note_label_editor = true;
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
        context.history, context.state,
        select_editor_objects_action_t{
            .objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{input.object_id}}},
            .mode = *mode,
            .request_audition = true});
    accepted = "BGM event selected.";
    return std::nullopt;
}

struct compose_timing_identity_t {
    wormedit::timing_lane_kind_t lane;
    wormedit::editor_selection_object_kind_t selection;
};

auto compose_timing_identity(WormUiTimingLaneKind lane)
    -> std::optional<compose_timing_identity_t> {
    using lane_t = wormedit::timing_lane_kind_t;
    using selection_t = wormedit::editor_selection_object_kind_t;
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM: return compose_timing_identity_t{lane_t::bpm, selection_t::bpm_event};
    case WORM_UI_TIMING_LANE_SCROLL: return compose_timing_identity_t{lane_t::scroll, selection_t::scroll_event};
    case WORM_UI_TIMING_LANE_SPEED: return compose_timing_identity_t{lane_t::speed, selection_t::speed_event};
    case WORM_UI_TIMING_LANE_STOP: return compose_timing_identity_t{lane_t::stop, selection_t::stop_event};
    case WORM_UI_TIMING_LANE_DELAY: return compose_timing_identity_t{lane_t::delay, selection_t::delay_event};
    case WORM_UI_TIMING_LANE_WARP: return compose_timing_identity_t{lane_t::warp, selection_t::warp_event};
    case WORM_UI_TIMING_LANE_KIAI: return compose_timing_identity_t{lane_t::kiai, selection_t::kiai_event};
    case WORM_UI_TIMING_LANE_ANCHOR: return compose_timing_identity_t{lane_t::anchor, selection_t::timing_anchor};
    case WORM_UI_TIMING_LANE_COUNT: break;
    }
    return std::nullopt;
}

auto compose_timing_lane_for_selection(wormedit::editor_selection_object_kind_t selection)
    -> std::optional<wormedit::timing_lane_kind_t> {
    for (unsigned ordinal = 0; ordinal < WORM_UI_TIMING_LANE_COUNT; ++ordinal) {
        const auto identity = compose_timing_identity(static_cast<WormUiTimingLaneKind>(ordinal));
        if (identity && identity->selection == selection) return identity->lane;
    }
    return std::nullopt;
}

auto select_compose_timing_marker(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto identity = compose_timing_identity(input.timing_lane);
    const auto mode = context.selection_mode(input.selection_mode);
    if (!identity || !mode || input.object_id == 0 ||
        input.timing_chart_id != context.state.chart.value ||
        input.timing_document_revision != context.history.document.revision)
        return "Timing marker selection is stale or invalid.";
    result = dispatch_editor_action(
        context.history, context.state,
        select_editor_objects_action_t{
            .objects = {{identity->selection, object_id_t{input.object_id}}},
            .mode = *mode});
    accepted = "Timing marker selected.";
    return std::nullopt;
}

auto move_compose_timing_selection(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto identity = compose_timing_identity(input.timing_lane);
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.history, context.state);
    if (!identity || !chart || !view || !std::isfinite(input.y_pixels) ||
        input.timing_chart_id != chart->id.value ||
        input.timing_document_revision != context.history.document.revision)
        return "Timing marker move is stale or invalid.";
    const auto clicked = editor_selection_object_t{identity->selection,
                                                    object_id_t{input.object_id}};
    if (std::ranges::find(context.state.selected_objects, clicked) ==
        context.state.selected_objects.end())
        return "Select the timing marker before dragging it.";
    const auto y = std::clamp(input.y_pixels, 0.0,
                              std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto position = context.position_at_compose_y_unrestricted(*chart, *view, y);
    const auto measures = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&measures);
    const auto target_row = position && index ? absolute_row(*index, *position) : std::nullopt;
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!target_row || input.timing_anchor_chart_row > maximum || *target_row > maximum)
        return "Timing marker move has no valid destination row.";
    const auto delta = static_cast<std::int64_t>(*target_row) -
                       static_cast<std::int64_t>(input.timing_anchor_chart_row);
    std::vector<timing_lane_event_ref_t> selected;
    selected.reserve(context.state.selected_objects.size());
    for (const auto& object : context.state.selected_objects) {
        const auto lane = compose_timing_lane_for_selection(object.kind);
        if (lane) selected.push_back({*lane, object.id});
    }
    if (selected.empty()) return "No timing markers are selected.";
    if (delta == 0) {
        result.accepted = true;
        accepted = "Timing markers stayed on their rows.";
        return std::nullopt;
    }
    const auto moved = move_timing_lane_event_group(
        context.history, chart->id, selected, delta, input.timing_document_revision,
        context.application_state.timing_locked);
    result.accepted = moved.applied;
    result.document_changed = moved.applied;
    result.diagnostics = moved.diagnostics;
    accepted = "Selected timing markers moved vertically.";
    return std::nullopt;
}

auto place_note_from_pointer(compose_input_context_t& context,
                              const auto& target,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> void {
    using namespace wormedit;
    result = dispatch_editor_action(context.history, context.state,
        context.state.pending_hold
            ? editor_ui_action_t{commit_hold_action_t{target.position}}
            : editor_ui_action_t{place_tap_action_t{target.position}});
    accepted = context.state.pending_hold ? "Hold gesture started; click its end row."
                                          : "Note placed.";
}

auto place_virtual_note_from_pointer(compose_input_context_t& context,
                                      std::uint64_t row, wormedit::lane_id_t lane,
                                      wormedit::editor_interaction_result_t& result,
                                      std::string_view& accepted) -> void {
    using namespace wormedit;
    const auto& pending = context.state.pending_virtual_lane_key;
    if (pending) {
        const auto anchor = pending->canvas_row;
        const auto hold_lane = pending->lane;
        context.state.pending_virtual_lane_key.reset();
        result = dispatch_editor_action(context.history, context.state,
            place_virtual_bms_hold_action_t{anchor, row, hold_lane});
        if (!result) context.state.pending_virtual_lane_key =
            editor_interaction_state_t::virtual_lane_key_t{anchor, hold_lane, context.history.document.revision};
        accepted = "Hold gesture committed.";
    } else if (context.state.armed_note_kind == note_kind_t::hold) {
        context.state.pending_virtual_lane_key =
            editor_interaction_state_t::virtual_lane_key_t{row, lane, context.history.document.revision};
        result.accepted = true;
        accepted = "Hold gesture started; click its end row.";
    } else {
        result = dispatch_editor_action(context.history, context.state,
            place_virtual_bms_tap_action_t{row, lane});
        accepted = "Note placed in virtual BMS measure.";
    }
}

auto finish_virtual_hold_at_authored_row(compose_input_context_t& context,
                                         const wormedit::chart_document_t& chart,
                                         wormedit::local_position_t position, wormedit::lane_id_t lane,
                                         wormedit::editor_interaction_result_t& result,
                                         std::string_view& accepted) -> bool {
    if (!context.state.pending_virtual_lane_key) return false;
    const auto built = wormedit::build_measure_index(chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    const auto row = index ? wormedit::absolute_row(*index, position) : std::nullopt;
    if (!row) return false;
    place_virtual_note_from_pointer(context, *row, lane, result, accepted);
    return true;
}

// A plain click only selects unless the paint tool is active (or a hold is awaiting its end
// click); the Select tool and Timing focus never place notes.
auto pointer_click_places_notes(const compose_input_context_t& context) -> bool {
    if (!wormedit::ui::compose_notes_editable(compose_mode_of(context.application_state)))
        return false;
    return context.application_state.note_paint_mode_active || context.state.pending_hold ||
           context.state.pending_virtual_lane_key;
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
        place_virtual_note_from_pointer(context, virtual_hit->first, virtual_hit->second, result, accepted);
        return std::nullopt;
    }
    const auto built = build_virtual_canvas(chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    const auto measure = canvas ? canvas->measure_containing(virtual_hit->first) : std::nullopt;
    if (!measure)
        return "The clicked BMS measure is unavailable.";
    auto candidate = context.state;
    result = dispatch_editor_action(context.history, candidate,
                                    select_bms_canvas_measure_action_t{measure->number});
    if (result.accepted) {
        restore_cursor_pose(context.state, capture_cursor_pose(candidate));
        context.state.active_lane = virtual_hit->second;
    }
    accepted = "BMS measure selected.";
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
    const auto view = compose_view_for_state(context.history, context.state);
    if (!chart || !view)
        return "The selected chart field is no longer available.";
    const auto target = context.hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!target)
        return select_or_place_virtual_pointer(input, context, *chart, *view, *mode, result, accepted);
    const auto places_notes = pointer_click_places_notes(context);
    if (*mode == editor_area_selection_mode_t::replace && places_notes &&
        finish_virtual_hold_at_authored_row(context, *chart, target->position, target->lane, result, accepted))
        return std::nullopt;
    context.state.active_lane = target->lane;
    place_cursor_at(context.state, target->position);
    const bool clicked_hold_end = context.state.pending_hold &&
                                  !context.state.pending_hold->gesture_source;
    if (*mode == editor_area_selection_mode_t::replace && places_notes &&
        (!target->note || clicked_hold_end)) {
        place_note_from_pointer(context, *target, result, accepted);
        return std::nullopt;
    }
    const auto selection =
        target->note
            ? select_editor_objects_action_t{{{editor_selection_object_kind_t::note,
                                               *target->note}},
                                             *mode,
                                             true}
            : select_editor_objects_action_t{{}, *mode, false};
    result = dispatch_editor_action(context.history, context.state, selection);
    accepted = target->note ? "Note selected." : "Edit cursor moved to the clicked row.";
    return std::nullopt;
}

auto select_compose_bms_measure(const WormUiComposeInputAction& input,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.history, context.state);
    if (!chart || !view || chart->source_family != format_family_t::bms ||
        !std::isfinite(input.y_pixels) || input.y_pixels < 0.0 ||
        input.y_pixels >= view->viewport_height_pixels || input.x_pixels < 0.0 ||
        input.x_pixels >= compose_measure_rail_width_pixels)
        return "Click inside a BMS measure number to select its measure.";
    const auto built = build_virtual_canvas(*chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    if (!canvas)
        return "The BMS measure canvas is unavailable.";
    const auto exact_row = (static_cast<long double>(view->viewport_scroll_pixels) +
                            compose_source_y(input.y_pixels, view->viewport_height_pixels,
                                             compose_field_downscroll(*chart, context.application_state.editor_preferences))) *
                           chart->divisions_per_standard_measure /
                           view->pixels_per_standard_measure;
    if (!std::isfinite(exact_row) || exact_row < 0.0L ||
        exact_row >= static_cast<long double>(canvas->total_rows))
        return "The measure number is outside #000-#999.";
    const auto row =
        std::min(static_cast<std::uint64_t>(std::floor(exact_row + 0.5L)), canvas->total_rows - 1);
    const auto measure = canvas->measure_containing(row);
    if (!measure)
        return "The selected BMS measure is unavailable.";
    result = dispatch_editor_action(context.history, context.state,
                                    select_bms_canvas_measure_action_t{measure->number});
    accepted = "BMS measure selected.";
    return std::nullopt;
}

struct resource_pointer_spec_t {
    wormedit::resource_id_t resource;
    wormedit::resource_placement_kind_t kind;
    wormedit::resource_role_t required_role;
    bool replacing;
};

auto resource_pointer_spec(const WormUiComposeInputAction& input, compose_input_context_t& context)
    -> std::variant<resource_pointer_spec_t, std::string> {
    using namespace wormedit;
    if (context.gesture_active)
        return "Finish or cancel the active Compose gesture before editing a resource.";
    if (!context.chart() || !compose_view_for_state(context.history, context.state) ||
        input.object_id == 0)
        return "The selected chart or armed resource is no longer available.";
    const auto kind = input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                          ? resource_placement_kind_t::sample_note
                      : input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                          ? resource_placement_kind_t::bgm_event
                      : input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                          ? resource_placement_kind_t::bga_background
                          : static_cast<resource_placement_kind_t>(255);
    if (kind == static_cast<resource_placement_kind_t>(255))
        return "Resource pointer placement used an unknown role.";
    const auto required = kind == resource_placement_kind_t::sample_note ? resource_role_t::sample
                          : kind == resource_placement_kind_t::bgm_event ? resource_role_t::bgm
                                                                         : resource_role_t::bga;
    const auto resource = std::ranges::find(context.history.document.resources,
                                            resource_id_t{input.object_id}, &resource_entry_t::id);
    if (resource == context.history.document.resources.end() ||
        std::ranges::find(resource->roles, required) == resource->roles.end())
        return "The armed resource cannot be used in this catalog lane.";
    return resource_pointer_spec_t{resource_id_t{input.object_id}, kind, required,
                                   input.kind == WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE};
}

struct resource_pointer_target_t {
    std::optional<wormedit::local_position_t> position;
    wormedit::lane_id_t lane{};
    std::optional<wormedit::object_id_t> note;
    bool handled_virtual{};
};

auto place_virtual_sample_resource(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view,
                                   const resource_pointer_spec_t& spec,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> bool {
    if (spec.replacing || spec.kind != wormedit::resource_placement_kind_t::sample_note)
        return false;
    const auto virtual_hit =
        context.virtual_bms_hit_at(chart, view, input.x_pixels, input.y_pixels);
    if (!virtual_hit)
        return false;
    result = wormedit::dispatch_editor_action(
        context.history, context.state,
        wormedit::place_virtual_bms_tap_action_t{virtual_hit->first, virtual_hit->second,
                                                 spec.resource});
    if (result.document_changed) {
        const auto next = wormedit::next_eligible_placement_resource(
            context.history.document, spec.resource, spec.required_role);
        worm_ui_frontend_resource_placed(input.object_id,
                                         next.value_or(wormedit::resource_id_t{}).value);
    }
    accepted = "Sample placed in BMS measure.";
    return true;
}

auto place_virtual_source_resource(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view,
                                   const resource_pointer_spec_t& spec,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> bool {
    using namespace wormedit;
    if (spec.replacing || spec.kind == resource_placement_kind_t::sample_note ||
        context.position_at_compose_y(chart, view, input.y_pixels))
        return false;
    const auto row = context.virtual_bms_row_at(chart, view, input.y_pixels);
    if (!row)
        return false;
    const auto applied = apply_virtual_bms_resource_placement(
        context.history, context.ui_action.compose_input_document_revision,
        {.chart = context.state.chart,
         .resource = spec.resource,
         .canvas_row = *row,
         .source_lane = spec.kind == resource_placement_kind_t::bgm_event
                            ? object_id_t{input.lane_id}
                            : object_id_t{},
         .kind = spec.kind,
         .snap_denominator = context.state.active_snap_denominator});
    result.accepted = applied.applied;
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    if (applied.applied) {
        const auto next = next_eligible_placement_resource(context.history.document, spec.resource,
                                                           spec.required_role);
        worm_ui_frontend_resource_placed(input.object_id, next.value_or(resource_id_t{}).value);
    }
    accepted = spec.kind == resource_placement_kind_t::bgm_event ? "BGM placed in BMS measure."
                                                                 : "BGA placed in BMS measure.";
    return true;
}

auto resolve_resource_pointer_target(
    const WormUiComposeInputAction& input, compose_input_context_t& context,
    const wormedit::chart_document_t& chart, const wormedit::editor_interaction_view_model_t& view,
    const resource_pointer_spec_t& spec, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted) -> std::variant<resource_pointer_target_t, std::string> {
    using namespace wormedit;
    resource_pointer_target_t target{};
    if (spec.kind == resource_placement_kind_t::sample_note) {
        const auto hit = context.hit_target_at(chart, view, input.x_pixels, input.y_pixels);
        if (!hit) {
            if (place_virtual_sample_resource(input, context, chart, view, spec, result, accepted))
                target.handled_virtual = true;
            if (target.handled_virtual)
                return target;
            return "Click inside an editable playable lane to place a Sample.";
        }
        target.position = hit->position;
        target.lane = hit->lane;
        target.note = hit->note;
        return target;
    }
    target.position = context.position_at_compose_y(chart, view, input.y_pixels);
    if (!target.position &&
        place_virtual_source_resource(input, context, chart, view, spec, result, accepted)) {
        target.handled_virtual = true;
        return target;
    }
    return target;
}

auto replacement_target_for_resource(const WormUiComposeInputAction& input,
                                     const wormedit::chart_document_t& chart,
                                     const resource_pointer_spec_t& spec,
                                     wormedit::local_position_t position,
                                     const resource_pointer_target_t& pointer)
    -> std::variant<wormedit::editor_selection_object_t, std::string> {
    using namespace wormedit;
    if (spec.kind == resource_placement_kind_t::sample_note && pointer.note)
        return editor_selection_object_t{editor_selection_object_kind_t::note, *pointer.note};
    if (spec.kind == resource_placement_kind_t::bgm_event && input.lane_id != 0)
        return editor_selection_object_t{editor_selection_object_kind_t::bgm_event,
                                         object_id_t{input.lane_id}};
    std::optional<editor_selection_object_t> target;
    if (spec.kind == resource_placement_kind_t::bga_background) {
        for (const auto& event : chart.bga_events) {
            if (event.kind != visual_event_kind_t::background || event.position != position)
                continue;
            if (target)
                return "More than one BGA background occupies this row.";
            target = editor_selection_object_t{editor_selection_object_kind_t::bga_event, event.id};
        }
    }
    if (!target)
        return "Click an existing object in the selected resource lane to replace it.";
    return *target;
}

auto dispatch_resource_pointer(const WormUiComposeInputAction& input,
                               compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> compose_input_family_result_t {
    using namespace wormedit;
    const auto spec_result = resource_pointer_spec(input, context);
    if (const auto* error = std::get_if<std::string>(&spec_result))
        return *error;
    const auto& spec = std::get<resource_pointer_spec_t>(spec_result);
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.history, context.state);
    if (!chart || !view)
        return "The selected chart or armed resource is no longer available.";
    const auto target_result =
        resolve_resource_pointer_target(input, context, *chart, *view, spec, result, accepted);
    if (const auto* error = std::get_if<std::string>(&target_result))
        return *error;
    const auto& pointer = std::get<resource_pointer_target_t>(target_result);
    if (!pointer.position) {
        return pointer.handled_virtual
                   ? compose_input_family_result_t::continue_with_result()
                   : compose_input_family_result_t{
                         "Pointer placement could not snap to a valid chart row."};
    }
    const auto position = resolve_resource_placement_position(
        *chart, *pointer.position, context.state.active_snap_denominator);
    if (!position)
        return "Pointer placement could not snap to a valid chart row.";
    document_command_result_t applied;
    if (spec.replacing) {
        const auto replacement =
            replacement_target_for_resource(input, *chart, spec, *position, pointer);
        if (const auto* error = std::get_if<std::string>(&replacement))
            return *error;
        applied = apply_resource_assignment(
            context.history, context.ui_action.compose_input_document_revision,
            {.chart = context.state.chart,
             .resource = spec.resource,
             .objects = {std::get<editor_selection_object_t>(replacement)}});
    } else {
        applied = apply_resource_placement(
            context.history, context.ui_action.compose_input_document_revision,
            {.chart = context.state.chart,
             .resource = spec.resource,
             .position = *position,
             .lane = pointer.lane,
             .source_lane = spec.kind == resource_placement_kind_t::bgm_event
                                ? object_id_t{input.lane_id}
                                : object_id_t{},
             .kind = spec.kind});
        if (applied.applied) {
            const auto next = next_eligible_placement_resource(context.history.document,
                                                               spec.resource, spec.required_role);
            worm_ui_frontend_resource_placed(input.object_id, next.value_or(resource_id_t{}).value);
        }
    }
    const auto unchanged = spec.replacing && !applied.applied && applied.diagnostics.empty();
    result.accepted = applied.applied || unchanged;
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    if (result.accepted) {
        place_cursor_at(context.state, *position);
        if (pointer.lane)
            context.state.active_lane = pointer.lane;
    }
    accepted = unchanged        ? "Object already uses the armed resource."
               : spec.replacing ? "Object resource replaced at the pointer."
                                : "Resource placed at the pointer.";
    return compose_input_family_result_t::continue_with_result();
}

auto dispatch_compose_undo_redo(const WormUiComposeInputAction& input,
                                compose_input_context_t& context) -> compose_input_family_result_t {
    using namespace wormedit;
    (void)dispatch_editor_action(context.history, context.state, cancel_editor_gesture_action_t{});
    (void)dispatch_editor_action(context.history, context.state, select_editor_notes_action_t{});
    const auto undo = input.kind == WORM_UI_COMPOSE_INPUT_UNDO;
    const auto changed =
        undo ? undo_document_command(context.history) : redo_document_command(context.history);
    const auto status = changed ? (undo ? "Undo applied." : "Redo applied.")
                                : (undo ? "Nothing to undo." : "Nothing to redo.");
    if (!changed)
        return compose_input_family_result_t::continue_batch(status);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "History changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return status;
}

auto cancel_compose_gesture(compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> compose_input_family_result_t {
    context.scene_cache.beat_fit_drag.reset();
    context.scene_cache.beat_fit_asset.reset();
    context.navigation_input.cancel();
    context.application_state.bmson_marker_move_preview.reset();
    result = wormedit::dispatch_editor_action(context.history, context.state,
                                              wormedit::cancel_editor_gesture_action_t{});
    accepted = "Compose gesture canceled.";
    return compose_input_family_result_t::continue_with_result();
}

auto dispatch_unhandled_compose_input(const WormUiComposeInputAction& input,
                                      compose_input_context_t& context,
                                      wormedit::editor_interaction_result_t& result,
                                      std::string_view& accepted) -> compose_input_family_result_t {
    if (const auto status = dispatch_compose_offset_fit_input(input, context))
        return compose_input_family_result_t{*status};
    if (const auto status = dispatch_compose_beat_fit_input(
            input, context.history, context.workspace, context.state, context.application_state,
            context.scene_cache, context.input_chart))
        return *status;
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
            return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK:
        if (const auto error = select_compose_bgm_event(input, context, result, accepted))
            return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT:
        if (const auto error = select_compose_timing_marker(input, context, result, accepted))
            return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE:
        if (const auto error = move_compose_timing_selection(input, context, result, accepted))
            return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_POINTER_CLICK:
        if (const auto error = select_compose_pointer_target(input, context, result, accepted))
            return *error;
        return compose_input_family_result_t::continue_with_result();
    case WORM_UI_COMPOSE_INPUT_MEASURE_SELECT:
        if (const auto error = select_compose_bms_measure(input, context, result, accepted))
            return *error;
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
        return true;
    default:
        return false;
    }
}

auto dispatch_compose_input_families(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> compose_input_family_result_t {
    if (compose_input_edits_notes(input.kind) &&
        !wormedit::ui::compose_notes_editable(compose_mode_of(context.application_state)))
        return compose_input_family_result_t::continue_batch(
            "Note editing is unavailable in Timing focus.");
    const auto keyboard =
        dispatch_compose_keyboard_input(input, context.history, context.workspace, context.state,
                                        context.navigation_input, context.application_state,
                                        result, accepted);
    if (keyboard.handled)
        return keyboard;
    const auto position = [&context](const auto& chart, const auto& view, double y) {
        return context.position_at_compose_y(chart, view, y);
    };
    const auto virtual_row = [&context](const auto& chart, const auto& view, double y) {
        return context.virtual_bms_row_at(chart, view, y);
    };
    const auto bmson =
        dispatch_bmson_compose_input(context.ui_action, input, context.history, context.workspace,
                                     context.state, context.application_state, context.scene_cache,
                                     context.gesture_active, position, virtual_row);
    if (bmson.handled)
        return bmson;
    const auto hit_target = [&context](const auto& chart, const auto& view, double x, double y) {
        return context.hit_target_at(chart, view, x, y);
    };
    const auto gesture = dispatch_compose_gesture_input(input, context.history, context.state,
                                                        context.application_state, result, accepted,
                                                        hit_target, position);
    if (gesture.handled)
        return gesture;
    const auto selection_mode = [&context](WormUiComposeSelectionMode mode) {
        return context.selection_mode(mode);
    };
    const auto source_y = [&context](double y) { return context.input_source_y(y); };
    const auto selection = dispatch_compose_selection_input(
        input, context.history, context.state, result, accepted, selection_mode, source_y,
        context.field_left_pixels, context.field_width_pixels, context.scene_cache.area_object_geometry);
    return selection;
}

auto dispatch_compose_paste_input(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context)
    -> std::optional<std::string> {
    auto& application = context.application_state;
    if (input.kind == WORM_UI_COMPOSE_INPUT_PASTE_CANCEL) {
        application.paste_session.reset();
        return "Paste preview cancelled.";
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_PASTE_COMMIT) {
        std::string status;
        if (commit_compose_paste(application, context.history, context.state, status))
            (void)wormedit::rebind_workspace(context.workspace, context.history);
        return status;
    }
    if (input.kind != WORM_UI_COMPOSE_INPUT_PASTE_UPDATE) return std::nullopt;
    if (!application.paste_session || !context.input_chart) return std::string{};
    const auto view = compose_view_for_state(context.history, context.state);
    if (!view) return "The paste field is unavailable.";
    const auto raw = context.position_at_compose_y(*context.input_chart, *view, input.y_pixels);
    const auto position = raw ? wormedit::resolve_resource_placement_position(
        *context.input_chart, *raw, context.state.active_snap_denominator) : std::nullopt;
    if (!position) {
        application.paste_session->anchor_available = false;
        application.paste_session->anchor_error = "The paste anchor is outside the chart.";
        return application.paste_session->anchor_error;
    }
    application.paste_session->anchor_available = true;
    application.paste_session->pointer_cursor = context.state.cursor;
    (void)update_compose_paste(application, context.history.document, context.state.chart, *position);
    return std::string{};
}

auto dispatch_compose_input_event(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> compose_input_family_result_t {
    if (const auto pasted = dispatch_compose_paste_input(input, context)) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_PASTE_UPDATE)
            return compose_input_family_result_t::continue_batch(*pasted);
        return compose_input_family_result_t{*pasted};
    }
    const auto family = dispatch_compose_input_families(input, context, result, accepted);
    return family.handled ? family
                          : dispatch_unhandled_compose_input(input, context, result, accepted);
}

auto dispatch_compose_input_batch(compose_input_context_t& context) -> std::string {
    using namespace wormedit;
    auto status = std::string{};
    for (std::size_t index = 0; index < context.ui_action.compose_input_count; ++index) {
        if (!context.input_is_current())
            return "Remaining Compose input was discarded after the document revision changed.";
        const auto& input = context.ui_action.compose_inputs[index];
        editor_interaction_result_t result;
        std::string_view accepted = "Compose interaction applied.";
        const auto dispatched = dispatch_compose_input_event(input, context, result, accepted);
        if (!dispatched.handled)
            continue;
        if (dispatched.stop_batch)
            return dispatched.status;
        if (!dispatched.process_result) {
            status = dispatched.status;
            continue;
        }
        status = compose_result_status(result, accepted);
        if (result.accepted && result.audition.has_value())
            context.scene_cache.audition_request = result.audition;
        if (result.accepted && result.document_changed) {
            const auto rebound = rebind_workspace(context.workspace, context.history);
            if (!rebound)
                return rebound.diagnostics.empty()
                           ? "Compose edit applied, but workspace rebinding failed."
                           : rebound.diagnostics.front().message;
        }
    }
    if (context.ui_action.compose_note_label_focus_consumed)
        context.scene_cache.focus_note_label_editor = false;
    return status;
}

void apply_compose_viewport_scroll(const wormedit::viewport_scroll_action_t& scroll,
                                   compose_input_context_t& context, std::string& status) {
    using namespace wormedit;
    if (scroll.pane == viewport_pane_t::bgm) {
        if (scroll.delta_x_pixels == 0.0)
            return;
        const auto maximum = std::max(0.0, static_cast<double>(context.bgm_lane_count) *
                                                   compose_bgm_lane_width_pixels -
                                               context.bgm_pane_width);
        auto& retained =
            context.application_state.bgm_horizontal_scroll_pixels[context.state.chart.value];
        retained = std::clamp(retained + scroll.delta_x_pixels, 0.0, maximum);
        status = "Compose BGM pane scrolled.";
        return;
    }
    if (scroll.delta_y_pixels == 0.0)
        return;
    if (!context.input_chart) {
        status = "The selected difficulty is no longer available.";
        return;
    }
    if (is_preview_playing(context.application_state)) {
        // Wheel notches skip by the active snap; scrolling up goes back in time on an upscroll.
        const auto downscroll = compose_field_downscroll(
            *context.input_chart, context.application_state.editor_preferences);
        const auto toward_later = (scroll.delta_y_pixels < 0.0) != downscroll;
        status = skip_playing_playhead(context.application_state, *context.input_chart,
                                       context.state.active_snap_denominator,
                                       playhead_skip_unit_t::snap, toward_later ? 1.0 : -1.0)
                     .status;
        return;
    }
    const auto downscroll = compose_field_downscroll(*context.input_chart, context.application_state.editor_preferences);
    const auto result = dispatch_editor_action(
        context.history, context.state,
        scroll_editor_view_action_t{
            compose_wheel_scroll_delta(scroll.delta_y_pixels, downscroll),
            compose_cursor_screen_anchor(context.state.viewport_height_pixels, downscroll),
            downscroll ? editor_field_scroll_direction_t::down
                       : editor_field_scroll_direction_t::up});
    status = compose_result_status(result, "Compose field scrolled.");
}

void apply_compose_viewport_actions(const wormedit::viewport_action_batch_t& actions,
                                    compose_input_context_t& context, std::string& status) {
    for (std::size_t index = 0; index < actions.count; ++index) {
        const auto& action = actions[index];
        if (const auto* scroll = std::get_if<wormedit::viewport_scroll_action_t>(&action)) {
            apply_compose_viewport_scroll(*scroll, context, status);
        } else if (const auto* zoom = std::get_if<wormedit::viewport_zoom_action_t>(&action)) {
            const auto result = wormedit::dispatch_editor_action(
                context.history, context.state,
                wormedit::adjust_editor_zoom_action_t{
                    .factor = zoom->factor, .anchor_screen_y_pixels = zoom->cursor_y_pixels});
            status = compose_result_status(result, "Compose field zoomed at the pointer.");
        }
    }
}

void apply_compose_pinch_zoom(const WormUiComposeViewportInput& input,
                              compose_input_context_t& context, std::string& status) {
    if (!std::isfinite(input.zoom_factor) || input.zoom_factor <= 0 ||
        !std::isfinite(input.cursor_y_pixels) || input.cursor_y_pixels < 0 ||
        input.cursor_y_pixels > context.state.viewport_height_pixels) {
        status = "Compose pinch zoom used an invalid scale or anchor.";
        return;
    }
    (void)context.viewport_input.normal_edit_press();
    const auto result = wormedit::dispatch_editor_action(
        context.history, context.state,
        wormedit::adjust_editor_zoom_action_t{
            .factor = input.zoom_factor, .anchor_screen_y_pixels = input.cursor_y_pixels});
    status = compose_result_status(result, "Compose field zoomed at the pinch focus.");
}

auto dispatch_compose_viewport_batch(compose_input_context_t& context,
                                     double viewport_delta_seconds, std::string status)
    -> std::string {
    using namespace wormedit;
    for (std::size_t index = 0; index < context.ui_action.compose_viewport_input_count; ++index) {
        if (!context.input_is_current())
            return "Remaining Compose viewport input was discarded after the chart or document "
                   "changed.";
        const auto& input = context.ui_action.compose_viewport_inputs[index];
        if (input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM) {
            apply_compose_pinch_zoom(input, context, status);
            continue;
        }
        const auto bgm_pane =
            context.bgm_pane_width > 0.0 && std::isfinite(input.cursor_x_pixels) &&
            input.cursor_x_pixels >= context.field_left_pixels + context.field_width_pixels &&
            input.cursor_x_pixels <= context.ui_action.compose_input_viewport_width_pixels;
        apply_compose_viewport_actions(
            context.viewport_input.handle_wheel(
                {.pane = bgm_pane ? viewport_pane_t::bgm : viewport_pane_t::chart,
                 .kind = input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                             ? viewport_wheel_kind_t::coarse_steps
                             : viewport_wheel_kind_t::smooth_pixels,
                 .delta_x = input.delta_x,
                 .delta_y = input.delta_y,
                 .zoom_modifier = input.zoom_modifier,
                 .cursor_x_pixels = input.cursor_x_pixels,
                 .cursor_y_pixels = input.cursor_y_pixels}),
            context, status);
    }
    if (std::isfinite(viewport_delta_seconds) && viewport_delta_seconds > 0.0 &&
        context.input_is_current())
        apply_compose_viewport_actions(context.viewport_input.advance(viewport_delta_seconds),
                                       context, status);
    return status;
}

void reset_compose_autoscroll_if_idle(compose_input_context_t& context) {
    context.scene_cache.viewport_autoscroll_active = context.viewport_input.autoscroll_active();
    if (context.scene_cache.viewport_autoscroll_active)
        return;
    context.scene_cache.viewport_autoscroll_anchor_x_pixels = 0.0;
    context.scene_cache.viewport_autoscroll_anchor_y_pixels = 0.0;
    context.scene_cache.viewport_autoscroll_pointer_x_pixels = 0.0;
    context.scene_cache.viewport_autoscroll_pointer_y_pixels = 0.0;
}

} // namespace

auto consume_minimap_preferences(WormUiWorkspaceAction& action,
                                 application_command_state_t& application) -> bool {
    if (action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) return false;
    bool changed = false;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < action.compose_input_count; ++index) {
        const auto& input = action.compose_inputs[index];
        if (input.kind == WORM_UI_COMPOSE_INPUT_SET_MINIMAP_PAUSED_BEHAVIOR) {
            application.minimap_click_moves_paused_playhead = input.overview_primary;
            changed = true;
        } else if (input.kind == WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE) {
            application.bgm_bar_hidden = !application.bgm_bar_hidden;
        } else action.compose_inputs[kept++] = input;
    }
    action.compose_input_count = kept;
    return changed;
}

auto dispatch_compose_input_action_impl(WormUiWorkspaceAction& ui_action,
                                   wormedit::document_history_t& history,
                                   wormedit::workspace_controller_t& workspace,
                                   wormedit::editor_interaction_state_t& state,
                                   wormedit::viewport_input_controller_t& viewport_input,
                                   wormedit::editor_navigation_input_controller_t& navigation_input,
                                   application_command_state_t& application_state,
                                   compose_scene_cache_t& scene_cache,
                                   double viewport_delta_seconds) -> std::string {
    using namespace wormedit;
    scene_cache.audition_request.reset();
    scene_cache.draft_audition_request.reset();
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != ui_action.compose_input_chart_id ||
        state.chart.value != ui_action.compose_input_chart_id ||
        history.document.revision != ui_action.compose_input_document_revision ||
        !std::isfinite(ui_action.compose_input_viewport_width_pixels) ||
        ui_action.compose_input_viewport_width_pixels <= 0.0) {
        return "Compose input ignored because the chart or document revision changed.";
    }
    if (ui_action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Compose input batch exceeded its capacity; the active gesture was canceled.";
    }
    if (ui_action.compose_viewport_input_count > WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Compose viewport input exceeded its capacity; the active gesture was canceled.";
    }
    const auto input_chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto bgm_lane_count = input_chart == history.document.charts.end()
                                    ? std::size_t{}
                                    : input_chart->bgm_source_lanes.size();
    const auto horizontal_layout = compose_horizontal_layout(
        ui_action.compose_input_viewport_width_pixels,
        input_chart == history.document.charts.end() ? 0 : maximum_layout_lane_count(*input_chart),
        bgm_lane_count != 0, std::popcount(effective_compose_timing_lane_mask(application_state)));
    const auto bgm_pane_width = horizontal_layout.bgm_pane_width_pixels;
    const auto field_left_pixels = horizontal_layout.field_left_pixels;
    const auto field_width_pixels = horizontal_layout.playable_field_width_pixels;
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto gesture_active = state.pending_hold.has_value() ||
                                state.pending_virtual_lane_key.has_value() ||
                                state.pending_note_duration_gesture.has_value() ||
                                state.pending_note_move_gesture.has_value() ||
                                state.pending_note_paint_gesture.has_value() ||
                                state.pending_semantic_layer_paint_gesture.has_value() ||
                                state.pending_editor_area_selection.has_value() ||
                                state.pending_space_range_selection.has_value();
    auto input_context = compose_input_context_t{
        .ui_action = ui_action,
        .history = history,
        .workspace = workspace,
        .state = state,
        .viewport_input = viewport_input,
        .navigation_input = navigation_input,
        .application_state = application_state,
        .scene_cache = scene_cache,
        .input_chart = input_chart == history.document.charts.end() ? nullptr : &*input_chart,
        .gesture_active = gesture_active,
        .bgm_lane_count = bgm_lane_count,
        .bgm_pane_width = bgm_pane_width,
        .field_left_pixels = field_left_pixels,
        .field_width_pixels = field_width_pixels};
    update_compose_viewport_pointer_input(ui_action, viewport_input, scene_cache, bgm_pane_width,
                                          field_left_pixels, field_width_pixels);
    update_compose_pointer_hover(
        ui_action, gesture_active, chart == history.document.charts.end() ? nullptr : &*chart,
        scene_cache.interaction_view ? &*scene_cache.interaction_view : nullptr,
        history.document.revision, field_left_pixels, field_width_pixels, scene_cache,
        application_state.editor_preferences);
    auto status = dispatch_compose_input_batch(input_context);
    if (ui_action.compose_note_label_focus_consumed) {
        scene_cache.focus_note_label_editor = false;
    }

    status = dispatch_compose_viewport_batch(input_context, viewport_delta_seconds,
                                            std::move(status));
    reset_compose_autoscroll_if_idle(input_context);
    if (!status.empty()) {
        return status;
    }
    return ui_action.compose_input_count != 0 || ui_action.compose_viewport_input_count != 0
               ? "Compose input handled."
               : std::string{};
}

auto dispatch_compose_input_action(WormUiWorkspaceAction& ui_action,
                                   wormedit::document_history_t& history,
                                   wormedit::workspace_controller_t& workspace,
                                   wormedit::editor_interaction_state_t& state,
                                   wormedit::viewport_input_controller_t& viewport_input,
                                   wormedit::editor_navigation_input_controller_t& navigation_input,
                                   application_command_state_t& application_state,
                                   compose_scene_cache_t& scene_cache,
                                   double viewport_delta_seconds) -> std::string {
    // View-only BGM bar inputs never reach the editor dispatcher.
    (void)consume_bgm_bar_inputs(ui_action, application_state.bgm_bar_range,
                                 scene_cache.waveform_overview_total_frames);
    const bool preference_changed = consume_minimap_preferences(ui_action, application_state);
    if (preference_changed && ui_action.compose_input_count == 0)
        return "Minimap paused-click preference changed for this session.";
    const auto status = dispatch_compose_input_action_impl(ui_action, history, workspace, state, viewport_input,
        navigation_input, application_state, scene_cache, viewport_delta_seconds);
    (void)compose_minimap_start_frame(application_state, history, &state);
    return status;
}

auto selected_chart(const wormedit::document_history_t& history,
                    const wormedit::workspace_controller_t& workspace)
    -> const wormedit::chart_document_t* {
    if (!workspace.selected_chart) {
        return nullptr;
    }
    const auto found = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &wormedit::chart_document_t::id);
    return found == history.document.charts.end() ? nullptr : &*found;
}

auto visible_semantic_layer_by_id(const wormedit::chart_document_t& chart, wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t* {
    const auto found =
        std::ranges::find(chart.semantic_layers, id, &wormedit::semantic_layer_t::id);
    return found != chart.semantic_layers.end() && found->visible ? &*found : nullptr;
}


} // namespace wormedit::sys
