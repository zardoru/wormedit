#include "sys/compose_input.h"

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

auto dispatch_compose_navigation_actions(const wormedit::editor_navigation_action_batch_t& actions,
                                         wormedit::document_history_t& history,
                                         wormedit::editor_interaction_state_t& state)
    -> std::string {
    using namespace wormedit;
    auto status = std::string{};
    for (std::size_t index = 0; index < actions.count; ++index) {
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

auto dispatch_compose_keyboard_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    wormedit::editor_interaction_result_t& result, std::string_view& accepted)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto status = std::string{};
    do {
        switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS: {
            const auto navigation = compose_navigation_action(input.navigation_action);
            if (!navigation) {
                return "Compose navigation input used an unknown action.";
            }
            status = dispatch_compose_navigation_actions(
                navigation_input.press(*navigation, static_cast<std::uint64_t>(SDL_GetTicks())),
                history, state);
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE: {
            const auto navigation = compose_navigation_action(input.navigation_action);
            if (!navigation) {
                return "Compose navigation release used an unknown action.";
            }
            navigation_input.release(*navigation);
            status = "Compose navigation key released.";
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL:
            navigation_input.cancel();
            status = "Compose navigation canceled.";
            continue;
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
        case WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS:
            if (input.lane_id == 0) {
                return "Lane-key input did not identify a chart lane.";
            }
            if (!state.selected_notes.empty()) {
                result = dispatch_editor_action(history, state,
                                                assign_selected_notes_to_lane_action_t{
                                                    lane_id_t{input.lane_id}, input.successive});
                accepted = input.successive ? "Next selected note assigned to lane."
                                            : "Selected notes assigned to lane.";
            } else {
                result = dispatch_editor_action(
                    history, state,
                    begin_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id}});
                accepted =
                    "Lane hold started; move the edit cursor and release the key to finish.";
            }
            break;
        case WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE:
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
                        history, state,
                        place_virtual_bms_tap_action_t{pending.canvas_row, pending.lane});
                    accepted = "Virtual BMS tap placed from the lane key.";
                } else {
                    result = dispatch_editor_action(
                        history, state,
                        place_virtual_bms_hold_action_t{pending.canvas_row, *end_row,
                                                        pending.lane});
                    accepted = "Virtual BMS hold placed from the lane key.";
                }
            } else if ((state.pending_hold.has_value() &&
                        state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) ||
                       !state.additional_keyboard_holds.empty()) {
                result = dispatch_editor_action(history, state,
                                                release_lane_key_hold_gesture_action_t{
                                                    lane_id_t{input.lane_id}});
                accepted = result.document_changed ? "Lane hold committed." : "Lane hold released.";
            } else {
                result.accepted = true;
                accepted = "Lane key released.";
            }
            break;
        default:
            return {};
        }
    } while (false);
    if (input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS ||
        input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE ||
        input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL)
        return compose_input_family_result_t::continue_batch(std::move(status));
    return compose_input_family_result_t::continue_with_result();
}

template <typename PositionAtComposeY, typename VirtualBmsRowAt>
auto dispatch_bmson_compose_input(
    const WormUiWorkspaceAction& ui_action, const WormUiComposeInputAction& input,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    bool gesture_active, PositionAtComposeY&& position_at_compose_y,
    VirtualBmsRowAt&& virtual_bms_row_at) -> compose_input_family_result_t {
    using namespace wormedit;
    auto status = std::string{};
    do {
        switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN: {
            if (gesture_active) {
                return "Finish or cancel the active Compose gesture before moving a BGM event.";
            }
            const object_id_t event{input.object_id};
            if (state.selected_objects.size() != 1 ||
                state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
                state.selected_objects.front().id != event) {
                return "Select one BGM event before dragging it.";
            }
            const auto chart = std::ranges::find(history.document.charts, state.chart,
                                                 &chart_document_t::id);
            if (chart == history.document.charts.end()) {
                return "The selected chart is no longer available.";
            }
            const auto source = find_bmson_marker_source(*chart, event);
            const bgm_event_t* ordinary{};
            if (!source) {
                for (const auto& measure : chart->measures) {
                    const auto found = std::ranges::find(measure.bgm_events, event,
                                                         &bgm_event_t::id);
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
                    .chart = state.chart, .revision = history.document.revision,
                    .event = event,
                    .source_lane = source ? source->source_lane : ordinary->source_lane,
                    .linked_bmson = source.has_value(),
                    .snap_denominator = state.active_snap_denominator,
                    .target = source ? source->position : ordinary->position,
                    .valid = true};
            status = "BGM event drag started.";
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE:
        case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE: {
            auto& preview = application_state.bmson_marker_move_preview;
            const object_id_t event{input.object_id};
            if (!preview || preview->chart != state.chart ||
                preview->revision != history.document.revision || preview->event != event) {
                preview.reset();
                return "BGM event drag expired before release.";
            }
            const auto chart = std::ranges::find(history.document.charts, state.chart,
                                                 &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view ||
                !std::isfinite(input.y_pixels)) {
                preview.reset();
                return "BGM event drag lost its chart field.";
            }
            const auto raw = position_at_compose_y(*chart, *current_view, input.y_pixels);
            const auto target = raw ? resolve_resource_placement_position(
                *chart, *raw, preview->snap_denominator) : std::nullopt;
            preview->virtual_canvas_row.reset();
            if (!target && !preview->linked_bmson) {
                if (const auto raw_canvas_row =
                        virtual_bms_row_at(*chart, *current_view, input.y_pixels)) {
                    const auto built_canvas = build_virtual_canvas(*chart);
                    const auto* canvas = std::get_if<virtual_canvas_t>(&built_canvas);
                    if (canvas && (preview->snap_denominator == 0 ||
                                   chart->divisions_per_standard_measure %
                                           preview->snap_denominator == 0)) {
                        auto snapped = *raw_canvas_row;
                        bool snapped_valid = true;
                        if (preview->snap_denominator != 0) {
                            const auto step = chart->divisions_per_standard_measure /
                                              preview->snap_denominator;
                            const auto remainder = snapped % step;
                            snapped -= remainder;
                            if (remainder > step - remainder) {
                                snapped_valid = step <=
                                    std::numeric_limits<std::uint64_t>::max() - snapped;
                                if (snapped_valid)
                                    snapped += step;
                            }
                        }
                        const auto destination = snapped_valid
                            ? canvas->measure_containing(snapped)
                            : std::nullopt;
                        if (destination && !destination->authored)
                            preview->virtual_canvas_row = snapped;
                    }
                }
            }
            if (!target && !preview->virtual_canvas_row) {
                preview->valid = false;
                if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE) {
                    preview.reset();
                    return "BGM event drag cannot snap to this chart row.";
                }
                status = "BGM event drag cannot snap to this chart row.";
                continue;
            }
            if (target)
                preview->target = *target;
            if (preview->linked_bmson) {
                const auto source = find_bmson_marker_source(*chart, event);
                if (!source) {
                    preview.reset();
                    return "BMSON BGM marker source event is unavailable.";
                }
                preview->valid = std::ranges::none_of(source->channel->events,
                    [&](const auto& candidate) {
                        return candidate.id != source->source_event &&
                               candidate.position == *target;
                    });
            } else {
                preview->source_lane = object_id_t{input.lane_id};
                preview->valid = std::ranges::any_of(chart->bgm_source_lanes,
                    [&](const auto& lane) { return lane.id == preview->source_lane; });
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE) {
                status = preview->valid ? "BGM event drag preview updated."
                                        : "BGM move target is unavailable.";
                continue;
            }
            const auto destination = preview->target;
            const auto virtual_destination = preview->virtual_canvas_row;
            const auto destination_lane = preview->source_lane;
            const auto linked_bmson = preview->linked_bmson;
            const auto valid = preview->valid;
            preview.reset();
            if (!valid) {
                return "BGM move target is unavailable.";
            }
            const auto moved = linked_bmson
                ? move_bmson_bgm_marker(history, ui_action.compose_input_document_revision,
                                         state.chart, event, destination)
                : virtual_destination
                    ? apply_virtual_bms_bgm_event_move(
                          history, ui_action.compose_input_document_revision,
                          {.chart = state.chart, .event = event,
                           .canvas_row = *virtual_destination,
                           .source_lane = destination_lane})
                    : apply_bgm_event_move(history, ui_action.compose_input_document_revision,
                                           {.chart = state.chart, .event = event,
                                            .position = destination,
                                            .source_lane = destination_lane});
            if (!moved.diagnostics.empty()) {
                return moved.diagnostics.front().message;
            }
            if (!moved.applied) {
                return "BGM event remained at its original position.";
            }
            const auto rebound = rebind_workspace(workspace, history);
            if (!rebound) {
                return rebound.diagnostics.empty()
                           ? "BGM event moved, but workspace rebinding failed."
                           : rebound.diagnostics.front().message;
            }
            return "BGM event moved to the snapped row.";
        }
        case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL:
            application_state.bmson_marker_move_preview.reset();
            return "BMSON BGM marker drag canceled.";
        case WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE:
        case WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART:
            return dispatch_bmson_razor_input(
                input, history, state, application_state, gesture_active,
                position_at_compose_y);
        case WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT: {
            const auto& draft = application_state.bmson_marker_draft;
            if (!draft || draft->chart != state.chart ||
                draft->revision != history.document.revision ||
                input.object_id == 0 || input.object_id > draft->markers.size()) {
                return "The BMSON region audition request expired.";
            }
            scene_cache.draft_audition_request = bmson_draft_audition_request_t{
                draft->chart, draft->revision, draft->reference, draft->markers,
                static_cast<std::size_t>(input.object_id - 1)};
            status = "Preparing staged BMSON region audition.";
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE:
        case WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM:
        case WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART:
        case WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT: {
            if (gesture_active) {
                return "Finish or cancel the active Compose gesture before editing a staged BMSON "
                       "region.";
            }
            auto& draft = application_state.bmson_marker_draft;
            if (!draft || draft->chart != state.chart ||
                draft->revision != history.document.revision || input.object_id == 0 ||
                input.object_id > draft->markers.size()) {
                return "The staged BMSON region edit expired.";
            }
            auto proposed = draft->markers;
            const auto selected_index = static_cast<std::size_t>(input.object_id - 1);
            if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT) {
                proposed.erase(proposed.begin() + selected_index);
                if (proposed.empty()) {
                    draft.reset();
                    status = "The last staged BMSON region was removed.";
                    continue;
                }
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART) {
                proposed[selected_index].restart = !proposed[selected_index].restart;
            } else {
                const auto lane =
                    input.kind == WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE
                        ? std::optional<lane_id_t>{state.active_lane}
                        : std::nullopt;
                if (proposed[selected_index].playable_lane == lane) {
                    status = "Staged BMSON region already has that assignment.";
                    continue;
                }
                proposed[selected_index].playable_lane = lane;
            }
            auto temporary = make_document_history(history.document, 1);
            const auto checked = insert_bmson_sound_markers(
                temporary, temporary.document.revision, draft->chart, draft->reference, proposed);
            if (!checked) {
                return checked.diagnostics.empty()
                           ? "Staged BMSON region edit could not be validated."
                           : checked.diagnostics.front().message;
            }
            draft->markers = std::move(proposed);
            status = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT
                         ? "Staged BMSON region removed."
                     : input.kind == WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART
                         ? "Staged BMSON region continuation/restart toggled."
                     : input.kind == WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE
                         ? "Staged BMSON region assigned to the active key lane."
                         : "Staged BMSON region assigned to BGM.";
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE: {
            if (gesture_active) {
                return "Finish or cancel the active Compose gesture before editing a BMSON reset.";
            }
            const auto chart = std::ranges::find(history.document.charts, state.chart,
                                                 &chart_document_t::id);
            if (chart == history.document.charts.end()) {
                return "The selected BMSON chart is no longer available.";
            }
            std::optional<object_id_t> source_event;
            for (const auto& measure : chart->measures) {
                const auto event = std::ranges::find(measure.bgm_events,
                                                     object_id_t{input.object_id},
                                                     &bgm_event_t::id);
                if (event != measure.bgm_events.end()) {
                    source_event = event->sound_channel_event;
                    break;
                }
            }
            if (!source_event) {
                return "This BGM tile has no BMSON sound-channel slice to reset.";
            }
            const auto applied = toggle_bmson_sound_channel_restart(
                history, ui_action.compose_input_document_revision, state.chart, *source_event);
            if (!applied.diagnostics.empty()) {
                return applied.diagnostics.front().message;
            }
            if (!applied.applied) {
                return "BMSON restart state did not change.";
            }
            const auto rebound = rebind_workspace(workspace, history);
            if (!rebound) {
                return rebound.diagnostics.empty()
                           ? "BMSON restart changed, but workspace rebinding failed."
                           : rebound.diagnostics.front().message;
            }
            return "BMSON slice restart toggled.";
        }
        default:
            return {};
        }
    } while (false);
    return compose_input_family_result_t::continue_batch(std::move(status));
}

void update_compose_viewport_pointer_input(
    const WormUiWorkspaceAction& ui_action,
    wormedit::viewport_input_controller_t& viewport_input, compose_scene_cache_t& scene_cache,
    double bgm_pane_width, double field_left_pixels, double field_width_pixels) {
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

void update_compose_pointer_hover(
    const WormUiWorkspaceAction& ui_action, bool gesture_active,
    const wormedit::chart_document_t* chart,
    const wormedit::editor_interaction_view_model_t* interaction_view,
    std::uint64_t revision, double field_left_pixels, double field_width_pixels,
    compose_scene_cache_t& scene_cache) {
    using namespace wormedit;
    if (!ui_action.compose_has_pointer_hover || gesture_active ||
        !chart || !interaction_view) {
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
                 .y_pixels = ui_action.compose_pointer_hover_y_pixels,
                 .field_left_pixels = field_left_pixels,
                 .field_width_pixels = field_width_pixels,
                 .hit_radius_pixels = 8.0,
                 .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
        }
    }
}

template <typename HitTargetAt, typename PositionAtComposeY>
auto dispatch_compose_gesture_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state,
    wormedit::editor_interaction_result_t& result, std::string_view& accepted,
    HitTargetAt&& hit_target_at, PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    using namespace wormedit;
    switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target) {
                return "Alt/Option-drag must begin over an editable lane.";
            }
            if (target->note && target->note_kind &&
                (*target->note_kind == note_kind_t::tap ||
                 *target->note_kind == note_kind_t::hold)) {
                state.active_lane = target->lane;
                state.cursor = target->position;
                state.virtual_bms_cursor_row.reset();
                result = dispatch_editor_action(
                    history, state, begin_note_duration_gesture_action_t{*target->note});
                accepted = "Note resize started; drag to change duration and release to finish.";
            } else if (target->note) {
                return "Alt/Option-drag resizes tap and hold notes; this note kind is not "
                       "resizable.";
            } else {
                result = dispatch_editor_action(history, state,
                                                begin_alt_pointer_hold_gesture_action_t{
                                                    .lane = target->lane,
                                                    .position = target->position,
                                                    .alt_or_option_down = true,
                                                });
                accepted = "Pointer hold started; release to finish.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE:
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            const auto y = std::clamp(input.y_pixels, 0.0,
                                      std::max(0.0, current_view->viewport_height_pixels - 0.001));
            const auto position = position_at_compose_y(*chart, *current_view, y);
            if (!position) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "Pointer hold input fell outside chart geometry and was canceled.";
            }
            if (state.pending_note_duration_gesture) {
                if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE) {
                    result = dispatch_editor_action(
                        history, state, update_note_duration_gesture_action_t{*position});
                    accepted = "Note resize preview updated.";
                } else {
                    result = dispatch_editor_action(
                        history, state, release_note_duration_gesture_action_t{*position});
                    accepted = result.document_changed ? "Note duration committed."
                                                       : "Note resize released.";
                }
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE) {
                result = dispatch_editor_action(history, state,
                                                update_alt_pointer_hold_gesture_action_t{
                                                    .position = *position,
                                                    .alt_or_option_down = true,
                                                    .pointer_captured = true,
                                                });
                accepted = "Pointer hold preview updated.";
            } else {
                result = dispatch_editor_action(history, state,
                                                release_alt_pointer_hold_gesture_action_t{
                                                    .position = *position,
                                                    .alt_or_option_down = true,
                                                    .pointer_captured = true,
                                                });
                accepted =
                    result.document_changed ? "Pointer hold committed." : "Pointer hold released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN:
        case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE:
        case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE: {
            if (!application_state.semantic_layer_paint_mode_active ||
                !application_state.semantic_layer_target) {
                if (state.pending_semantic_layer_paint_gesture) {
                    result =
                        dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    accepted =
                        "Semantic-layer paint stroke canceled because paint mode is inactive.";
                } else {
                    result.accepted = true;
                    accepted = "Semantic-layer paint input ignored because paint mode is inactive.";
                }
                break;
            }
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
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
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target || !target->note) {
                if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN) {
                    return "Begin semantic-layer painting over a playable note.";
                }
                if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE) {
                    result.accepted = true;
                    accepted = "Semantic-layer paint moved between playable notes.";
                    break;
                }
                result =
                    dispatch_editor_action(history, state,
                                           end_semantic_layer_paint_gesture_action_t{
                                               .note = object_id_t{}, .pointer_captured = true});
                accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                                   : "Semantic-layer paint stroke released.";
                break;
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN) {
                result = dispatch_editor_action(
                    history, state,
                    begin_semantic_layer_paint_gesture_action_t{
                        .layer = layer->id, .note = *target->note, .pointer_captured = true});
                accepted = "Semantic-layer paint stroke started.";
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE) {
                result =
                    dispatch_editor_action(history, state,
                                           update_semantic_layer_paint_gesture_action_t{
                                               .note = *target->note, .pointer_captured = true});
                accepted = "Semantic-layer paint preview updated.";
            } else {
                result =
                    dispatch_editor_action(history, state,
                                           end_semantic_layer_paint_gesture_action_t{
                                               .note = *target->note, .pointer_captured = true});
                accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                                   : "Semantic-layer paint stroke released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
        case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
        case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE: {
            if (!application_state.note_paint_mode_active) {
                if (state.pending_note_paint_gesture) {
                    result =
                        dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    accepted = "Note-paint stroke canceled because paint mode is inactive.";
                } else {
                    result.accepted = true;
                    accepted = "Note-paint input ignored because paint mode is inactive.";
                }
                break;
            }
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target) {
                if (input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
                    result =
                        dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    accepted = "Note-paint stroke canceled outside editable chart geometry.";
                    break;
                }
                return "Begin note painting over an editable chart lane.";
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
                result = dispatch_editor_action(
                    history, state,
                    begin_note_paint_gesture_action_t{.lane = target->lane,
                                                      .position = target->position,
                                                      .pointer_captured = true});
                accepted = "Note-paint stroke started.";
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE) {
                result = dispatch_editor_action(
                    history, state,
                    update_note_paint_gesture_action_t{.lane = target->lane,
                                                       .position = target->position,
                                                       .pointer_captured = true});
                accepted = "Note-paint preview updated.";
            } else {
                result = dispatch_editor_action(
                    history, state,
                    end_note_paint_gesture_action_t{.lane = target->lane,
                                                    .position = target->position,
                                                    .pointer_captured = true});
                accepted = result.document_changed ? "Note-paint stroke committed."
                                                   : "Note-paint stroke released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN:
        case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE:
        case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN) {
                const auto target =
                    hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
                if (!target || !target->note ||
                    std::ranges::find(state.selected_notes, *target->note) ==
                        state.selected_notes.end()) {
                    return "Begin a note move by dragging a selected playable note.";
                }
                state.active_lane = target->lane;
                state.cursor = target->position;
                state.virtual_bms_cursor_row.reset();
                result = dispatch_editor_action(history, state,
                                                begin_note_move_gesture_action_t{*target->note});
                accepted = "Selected-note move started.";
            } else {
                const auto y =
                    std::clamp(input.y_pixels, 0.0,
                               std::max(0.0, current_view->viewport_height_pixels - 0.001));
                const auto position = position_at_compose_y(*chart, *current_view, y);
                const auto target =
                    hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
                if (!position || !target) {
                    (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    return "Note move input fell outside chart lane geometry and was canceled.";
                }
                if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE) {
                    result = dispatch_editor_action(
                        history, state, update_note_move_gesture_action_t{*position, target->lane});
                    accepted = "Selected-note move preview updated.";
                } else {
                    result = dispatch_editor_action(
                        history, state,
                        release_note_move_gesture_action_t{*position, target->lane});
                    accepted = result.document_changed ? "Selected notes moved."
                                                       : "Selected-note move released.";
                }
            }
            break;
        }
    default:
        return {};
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename SelectionMode, typename InputSourceY>
auto dispatch_compose_selection_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state,
    wormedit::editor_interaction_result_t& result, std::string_view& accepted,
    SelectionMode&& selection_mode, InputSourceY&& input_source_y,
    double field_left_pixels, double field_width_pixels)
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
                return "Compose Space selection used an unknown selection mode.";
            }
            result = dispatch_editor_action(history, state,
                                            space_range_selection_action_t{
                                                .mode = *selection_mode(input.selection_mode),
                                            });
            accepted = state.pending_space_range_selection
                           ? "Space range selection started; move the cursor and press Space again."
                           : "Space range selection committed.";
            break;
    default:
        return {};
    }
    return compose_input_family_result_t::continue_with_result();
}

auto dispatch_compose_beat_fit_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    const wormedit::chart_document_t* input_chart) -> std::optional<std::string> {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN: {
        if (application_state.timing_locked || scene_cache.beat_fit_drag ||
            editor_interaction_has_pending_gesture(state) || !scene_cache.index ||
            !scene_cache.marker_timing_index || scene_cache.waveform_overview_sample_rate == 0 ||
            scene_cache.waveform_overview_total_frames == 0 || !input_chart || input.object_id == 0) {
            return "Beat fitting requires an unlocked waveform and idle chart gesture.";
        }
        const auto& measures = scene_cache.index->measures;
        const auto beat_row = input.object_id;
        const auto measure = std::ranges::upper_bound(
            measures.entries, beat_row, {}, &measure_index_entry_t::absolute_begin_row);
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
        const auto begun = begin_beat_interval_drag(
            history.document, input_chart->id, *previous_bpm, beat,
            scene_cache.waveform_overview_sample_rate, application_state.timing_locked);
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
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE: {
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
        const auto updated =
            update_beat_interval_drag(history.document, *scene_cache.beat_fit_drag, *frame,
                                      application_state.timing_locked);
        if (!updated) {
            if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
                scene_cache.beat_fit_drag.reset();
                scene_cache.beat_fit_asset.reset();
            }
            return updated.diagnostics.empty() ? "Beat fit target was rejected."
                                               : updated.diagnostics.front().message;
        }
        scene_cache.beat_fit_drag = updated.drag;
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
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL:
        scene_cache.beat_fit_drag.reset();
        scene_cache.beat_fit_asset.reset();
        return "Beat fit cancelled; timing was unchanged.";
    default:
        return std::nullopt;
    }
}

} // namespace

auto dispatch_compose_input_action(const WormUiWorkspaceAction& ui_action,
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
        bgm_lane_count != 0,
        std::popcount(application_state.compose_timing_lane_visibility_mask));
    const auto bgm_pane_width = horizontal_layout.bgm_pane_width_pixels;
    const auto field_left_pixels = horizontal_layout.field_left_pixels;
    const auto field_width_pixels = horizontal_layout.playable_field_width_pixels;
    const auto input_source_y = [&](double y) {
        return compose_source_y(
            y, state.viewport_height_pixels,
            input_chart != history.document.charts.end() &&
                compose_field_downscroll(*input_chart));
    };

    update_compose_viewport_pointer_input(
        ui_action, viewport_input, scene_cache, bgm_pane_width, field_left_pixels,
        field_width_pixels);

    auto status = std::string{};
    const auto selection_mode = [](WormUiComposeSelectionMode mode)
        -> std::optional<wormedit::editor_area_selection_mode_t> {
        switch (mode) {
        case WORM_UI_COMPOSE_SELECTION_REPLACE:
            return wormedit::editor_area_selection_mode_t::replace;
        case WORM_UI_COMPOSE_SELECTION_ADD:
            return wormedit::editor_area_selection_mode_t::add;
        case WORM_UI_COMPOSE_SELECTION_TOGGLE:
            return wormedit::editor_area_selection_mode_t::toggle;
        }
        return std::nullopt;
    };
    const auto hit_target_at = [&](const chart_document_t& chart,
                                   const editor_interaction_view_model_t& view, double x_pixels,
                                   double y_pixels) -> std::optional<editor_hit_target_t> {
        if (!scene_cache.hit_test_index) {
            auto built = build_editor_hit_test_index(chart, history.document.revision);
            if (!std::holds_alternative<editor_hit_test_index_t>(built)) {
                return std::nullopt;
            }
            scene_cache.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
        }
        return hit_test_editor(chart, *scene_cache.hit_test_index, view,
                               {.x_pixels = x_pixels,
                                .y_pixels = compose_source_y(
                                    y_pixels, view.viewport_height_pixels,
                                    compose_field_downscroll(chart)),
                                .field_left_pixels = field_left_pixels,
                                .field_width_pixels = field_width_pixels});
    };
    const auto position_at_compose_y = [&](const chart_document_t& chart,
                                           const editor_interaction_view_model_t& view,
                                           double y_pixels) {
        return editor_position_at_y_pixels(
            chart, view, compose_source_y(y_pixels, view.viewport_height_pixels,
                                          compose_field_downscroll(chart)));
    };
    const auto virtual_bms_row_at =
        [&](const chart_document_t& chart, const editor_interaction_view_model_t& view,
            double y_pixels) -> std::optional<std::uint64_t> {
        if (chart.source_family != format_family_t::bms || !std::isfinite(y_pixels) ||
            y_pixels < 0.0 ||
            y_pixels >= view.viewport_height_pixels) {
            return std::nullopt;
        }
        const auto canvas = build_virtual_canvas(chart);
        const auto* virtual_canvas = std::get_if<virtual_canvas_t>(&canvas);
        if (!virtual_canvas) {
            return std::nullopt;
        }
        const auto source_y = compose_source_y(y_pixels, view.viewport_height_pixels,
                                               compose_field_downscroll(chart));
        const auto exact_row = (static_cast<long double>(view.viewport_scroll_pixels) +
                                source_y) *
                               chart.divisions_per_standard_measure /
                               view.pixels_per_standard_measure;
        if (!std::isfinite(exact_row) || exact_row < 0.0L ||
            exact_row >= static_cast<long double>(virtual_canvas->total_rows)) {
            return std::nullopt;
        }
        const auto canvas_row = static_cast<std::uint64_t>(std::floor(exact_row + 0.5L));
        const auto measure = virtual_canvas->measure_containing(canvas_row);
        if (!measure || measure->authored) {
            return std::nullopt;
        }
        return canvas_row;
    };
    const auto virtual_bms_hit_at =
        [&](const chart_document_t& chart, const editor_interaction_view_model_t& view,
            double x_pixels, double y_pixels)
        -> std::optional<std::pair<std::uint64_t, lane_id_t>> {
        if (!scene_cache.index) {
            return std::nullopt;
        }
        const auto canvas_row = virtual_bms_row_at(chart, view, y_pixels);
        if (!canvas_row) {
            return std::nullopt;
        }
        auto request = scene_cache.preview_scene_request;
        request.include_virtual_bms_tail = true;
        const auto composed = make_editor_field_scene(chart, *scene_cache.index, view, request);
        if (!composed.scene || composed.has_errors()) {
            return std::nullopt;
        }
        for (const auto& lane : composed.scene->lanes) {
            if (x_pixels >= lane.bounds.left && x_pixels < lane.bounds.right &&
                y_pixels >= lane.bounds.top && y_pixels < lane.bounds.bottom) {
                return std::pair{*canvas_row, lane.lane};
            }
        }
        return std::nullopt;
    };
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
    update_compose_pointer_hover(
        ui_action, gesture_active,
        chart == history.document.charts.end() ? nullptr : &*chart,
        scene_cache.interaction_view ? &*scene_cache.interaction_view : nullptr,
        history.document.revision, field_left_pixels, field_width_pixels, scene_cache);
    for (std::size_t index = 0; index < ui_action.compose_input_count; ++index) {
        if (history.document.revision != ui_action.compose_input_document_revision ||
            workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
            workspace.selected_chart->value != ui_action.compose_input_chart_id ||
            state.chart.value != ui_action.compose_input_chart_id) {
            return "Remaining Compose input was discarded after the document revision changed.";
        }

        const auto& input = ui_action.compose_inputs[index];
        editor_interaction_result_t result;
        std::string_view accepted = "Compose interaction applied.";
        const auto keyboard_dispatch = dispatch_compose_keyboard_input(
            input, history, workspace, state, navigation_input, result, accepted);
        if (keyboard_dispatch.handled) {
            if (keyboard_dispatch.stop_batch)
                return keyboard_dispatch.status;
            if (!keyboard_dispatch.process_result) {
                status = keyboard_dispatch.status;
                continue;
            }
        }
        const auto bmson_dispatch = dispatch_bmson_compose_input(
            ui_action, input, history, workspace, state, application_state, scene_cache,
            gesture_active, position_at_compose_y, virtual_bms_row_at);
        if (bmson_dispatch.handled) {
            status = bmson_dispatch.status;
            if (bmson_dispatch.stop_batch)
                return status;
            continue;
        }
        const auto gesture_dispatch = dispatch_compose_gesture_input(
            input, history, state, application_state, result, accepted,
            hit_target_at, position_at_compose_y);
        if (gesture_dispatch.handled && gesture_dispatch.stop_batch)
            return gesture_dispatch.status;
        const auto selection_dispatch = dispatch_compose_selection_input(
            input, history, state, result, accepted, selection_mode, input_source_y,
            field_left_pixels, field_width_pixels);
        if (selection_dispatch.handled && selection_dispatch.stop_batch)
            return selection_dispatch.status;
        if (!keyboard_dispatch.handled && !gesture_dispatch.handled &&
            !selection_dispatch.handled) {
        if (const auto beat_fit_status = dispatch_compose_beat_fit_input(
                input, history, workspace, state, application_state, scene_cache,
                input_chart == history.document.charts.end() ? nullptr : &*input_chart)) {
            return *beat_fit_status;
        }
        switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK: {
            if (!std::isfinite(input.audio_fraction) || input.audio_fraction < 0.0 ||
                input.audio_fraction > 1.0 || !scene_cache.marker_timing_index ||
                scene_cache.waveform_overview_total_frames == 0 ||
                scene_cache.waveform_overview_sample_rate == 0 ||
                input_chart == history.document.charts.end()) {
                return "Waveform overview seek is unavailable.";
            }
            if (editor_interaction_has_pending_gesture(state)) {
                return "Finish or cancel the active gesture before navigating the waveform.";
            }
            const auto& timing = *scene_cache.marker_timing_index;
            const auto frame = static_cast<std::int64_t>(
                input.audio_fraction *
                static_cast<long double>(scene_cache.waveform_overview_total_frames - 1));
            const auto seconds =
                signed_rational_t{frame, scene_cache.waveform_overview_sample_rate};
            const auto musical = musical_position_at(timing, seconds);
            long double row = 0.0L;
            if (musical && musical.absolute_row && musical.absolute_row->denominator != 0) {
                row = static_cast<long double>(musical.absolute_row->numerator) /
                      musical.absolute_row->denominator;
            } else {
                const auto chart_end = timing_at(timing, timing.total_rows);
                if (chart_end && chart_end.seconds) {
                    const auto end = static_cast<long double>(chart_end.seconds->numerator) /
                                     chart_end.seconds->denominator;
                    const auto audio =
                        static_cast<long double>(frame) / scene_cache.waveform_overview_sample_rate;
                    row = audio >= end ? static_cast<long double>(timing.total_rows) : 0.0L;
                }
            }
            const auto target = static_cast<double>(row * state.pixels_per_standard_measure /
                                                        timing.divisions_per_standard_measure -
                                                    state.viewport_height_pixels * 0.5);
            result = dispatch_editor_action(
                history, state, scroll_editor_view_action_t{target - state.viewport_scroll_pixels});
            accepted = "Compose waveform overview navigated.";
            break;
        }
        case WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            if (!scene_cache.hit_test_index) {
                auto built = build_editor_hit_test_index(*chart, history.document.revision);
                if (std::holds_alternative<editor_hit_test_index_t>(built)) {
                    scene_cache.hit_test_index =
                        std::get<editor_hit_test_index_t>(std::move(built));
                }
            }
            if (!scene_cache.hit_test_index || !scene_cache.interaction_view) {
                return "The chart field hover index is unavailable.";
            }
            const auto hover = hover_editor_object(
                *chart, *scene_cache.hit_test_index, *scene_cache.interaction_view,
                {.x_pixels = input.x_pixels,
                 .y_pixels = input_source_y(input.y_pixels),
                 .field_left_pixels = field_left_pixels,
                 .field_width_pixels = field_width_pixels,
                 .hit_radius_pixels = 8.0,
                 .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
            if (!hover || hover->object.kind != editor_selection_object_kind_t::note) {
                return "Double-click a note to edit its label.";
            }
            result = dispatch_editor_action(history, state,
                                            select_editor_notes_action_t{{hover->object.id}});
            if (!result) {
                return compose_result_status(result, "Note label editor could not be focused.");
            }
            scene_cache.focus_note_label_editor = true;
            accepted = "Note label editor focused.";
            break;
        }
        case WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK:
            if (input.object_id == 0) {
                return "BGM event click did not identify an event.";
            }
            if (!selection_mode(input.selection_mode).has_value()) {
                return "BGM event click used an unknown selection mode.";
            }
            result = dispatch_editor_action(
                history, state,
                select_editor_objects_action_t{
                    {{editor_selection_object_kind_t::bgm_event, object_id_t{input.object_id}}},
                    *selection_mode(input.selection_mode),
                    true});
            accepted = "BGM event selected.";
            break;
        case WORM_UI_COMPOSE_INPUT_POINTER_CLICK: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target) {
                if (const auto virtual_hit = virtual_bms_hit_at(
                        *chart, *current_view, input.x_pixels, input.y_pixels)) {
                    const auto built_canvas = build_virtual_canvas(*chart);
                    const auto* canvas = std::get_if<virtual_canvas_t>(&built_canvas);
                    const auto measure = canvas
                        ? canvas->measure_containing(virtual_hit->first)
                        : std::nullopt;
                    if (!measure)
                        return "The clicked BMS measure is unavailable.";
                    result = dispatch_editor_action(
                        history, state, select_bms_canvas_measure_action_t{measure->number});
                    state.active_lane = virtual_hit->second;
                    accepted = "BMS measure selected.";
                    break;
                }
                return "Click inside an editable lane to select a note or position.";
            }
            state.active_lane = target->lane;
            state.cursor = target->position;
            state.virtual_bms_cursor_row.reset();
            if (target->note) {
                result = dispatch_editor_action(
                    history, state,
                    select_editor_objects_action_t{
                        {{editor_selection_object_kind_t::note, *target->note}},
                        editor_area_selection_mode_t::replace,
                        true});
                accepted = "Note selected.";
            } else {
                result = dispatch_editor_action(
                    history, state,
                    select_editor_objects_action_t{{}, editor_area_selection_mode_t::replace,
                                                   false});
                accepted = "Edit cursor moved to the clicked row.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_MEASURE_SELECT: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view ||
                chart->source_family != format_family_t::bms ||
                !std::isfinite(input.y_pixels) || input.y_pixels < 0.0 ||
                input.y_pixels >= current_view->viewport_height_pixels ||
                input.x_pixels < 0.0 || input.x_pixels >= compose_measure_rail_width_pixels) {
                return "Click inside a BMS measure number to select its measure.";
            }
            const auto built = build_virtual_canvas(*chart);
            const auto* canvas = std::get_if<virtual_canvas_t>(&built);
            if (!canvas) {
                return "The BMS measure canvas is unavailable.";
            }
            const auto exact_row =
                (static_cast<long double>(current_view->viewport_scroll_pixels) +
                 compose_source_y(input.y_pixels, current_view->viewport_height_pixels,
                                  compose_field_downscroll(*chart))) *
                chart->divisions_per_standard_measure /
                current_view->pixels_per_standard_measure;
            if (!std::isfinite(exact_row) || exact_row < 0.0L ||
                exact_row >= static_cast<long double>(canvas->total_rows)) {
                return "The measure number is outside #000-#999.";
            }
            const auto row = std::min(
                static_cast<std::uint64_t>(std::floor(exact_row + 0.5L)),
                canvas->total_rows - 1);
            const auto measure = canvas->measure_containing(row);
            if (!measure) {
                return "The selected BMS measure is unavailable.";
            }
            result = dispatch_editor_action(
                history, state, select_bms_canvas_measure_action_t{measure->number});
            accepted = "BMS measure selected.";
            break;
        }
        case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE:
        case WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE: {
            if (gesture_active) {
                return "Finish or cancel the active Compose gesture before editing a resource.";
            }
            const auto replacing =
                input.kind == WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE;
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view ||
                input.object_id == 0) {
                return "The selected chart or armed resource is no longer available.";
            }
            const auto kind =
                input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                    ? resource_placement_kind_t::sample_note
                    : input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                          ? resource_placement_kind_t::bgm_event
                          : input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                                ? resource_placement_kind_t::bga_background
                                : static_cast<resource_placement_kind_t>(255);
            if (kind == static_cast<resource_placement_kind_t>(255)) {
                return "Resource pointer placement used an unknown role.";
            }
            const auto resource = std::ranges::find(history.document.resources,
                                                    resource_id_t{input.object_id},
                                                    &resource_entry_t::id);
            const auto required_role =
                kind == resource_placement_kind_t::sample_note
                    ? resource_role_t::sample
                    : kind == resource_placement_kind_t::bgm_event
                          ? resource_role_t::bgm
                          : resource_role_t::bga;
            if (resource == history.document.resources.end() ||
                std::ranges::find(resource->roles, required_role) ==
                    resource->roles.end()) {
                return "The armed resource cannot be used in this catalog lane.";
            }
            std::optional<local_position_t> raw_position;
            lane_id_t target_lane{};
            std::optional<object_id_t> note_target;
            if (kind == resource_placement_kind_t::sample_note) {
                const auto target =
                    hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
                if (!target) {
                    if (!replacing) {
                        if (const auto virtual_hit = virtual_bms_hit_at(
                                *chart, *current_view, input.x_pixels, input.y_pixels)) {
                            result = dispatch_editor_action(
                                history, state,
                                place_virtual_bms_tap_action_t{
                                    virtual_hit->first, virtual_hit->second,
                                    resource_id_t{input.object_id}});
                            if (result.document_changed) {
                                const auto next = next_eligible_placement_resource(
                                    history.document, resource_id_t{input.object_id},
                                    required_role);
                                worm_ui_frontend_resource_placed(
                                    input.object_id,
                                    next.value_or(resource_id_t{}).value);
                            }
                            accepted = "Sample placed in BMS measure.";
                            break;
                        }
                    }
                    return "Click inside an editable playable lane to place a Sample.";
                }
                raw_position = target->position;
                target_lane = target->lane;
                note_target = target->note;
            } else {
                raw_position = position_at_compose_y(*chart, *current_view, input.y_pixels);
            }
            if (!raw_position && !replacing &&
                kind != resource_placement_kind_t::sample_note) {
                if (const auto canvas_row =
                        virtual_bms_row_at(*chart, *current_view, input.y_pixels)) {
                    const auto applied = apply_virtual_bms_resource_placement(
                        history, ui_action.compose_input_document_revision,
                        {.chart = state.chart,
                         .resource = resource_id_t{input.object_id},
                         .canvas_row = *canvas_row,
                         .source_lane = kind == resource_placement_kind_t::bgm_event
                                            ? object_id_t{input.lane_id}
                                            : object_id_t{},
                         .kind = kind,
                         .snap_denominator = state.active_snap_denominator});
                    result.accepted = applied.applied;
                    result.document_changed = applied.applied;
                    result.diagnostics = applied.diagnostics;
                    if (applied.applied) {
                        const auto next = next_eligible_placement_resource(
                            history.document, resource_id_t{input.object_id}, required_role);
                        worm_ui_frontend_resource_placed(
                            input.object_id, next.value_or(resource_id_t{}).value);
                    }
                    accepted = kind == resource_placement_kind_t::bgm_event
                                   ? "BGM placed in BMS measure."
                                   : "BGA placed in BMS measure.";
                    break;
                }
            }
            const auto position =
                raw_position ? resolve_resource_placement_position(
                                   *chart, *raw_position, state.active_snap_denominator)
                             : std::nullopt;
            if (!position) {
                return "Pointer placement could not snap to a valid chart row.";
            }
            document_command_result_t applied;
            if (replacing) {
                std::optional<editor_selection_object_t> target;
                if (kind == resource_placement_kind_t::sample_note && note_target) {
                    target = {editor_selection_object_kind_t::note, *note_target};
                } else if (kind == resource_placement_kind_t::bgm_event &&
                           input.lane_id != 0) {
                    target = {editor_selection_object_kind_t::bgm_event,
                              object_id_t{input.lane_id}};
                } else if (kind == resource_placement_kind_t::bga_background) {
                    for (const auto& event : chart->bga_events) {
                        if (event.kind != visual_event_kind_t::background ||
                            event.position != *position) {
                            continue;
                        }
                        if (target) {
                            return "More than one BGA background occupies this row.";
                        }
                        target = {editor_selection_object_kind_t::bga_event, event.id};
                    }
                }
                if (!target) {
                    return "Click an existing object in the selected resource lane to replace it.";
                }
                applied = apply_resource_assignment(
                    history, ui_action.compose_input_document_revision,
                    {.chart = state.chart,
                     .resource = resource_id_t{input.object_id},
                     .objects = {*target}});
            } else {
                applied = apply_resource_placement(
                    history, ui_action.compose_input_document_revision,
                    {.chart = state.chart,
                     .resource = resource_id_t{input.object_id},
                     .position = *position,
                     .lane = target_lane,
                     .source_lane = kind == resource_placement_kind_t::bgm_event
                                        ? object_id_t{input.lane_id}
                                        : object_id_t{},
                     .kind = kind});
                if (applied.applied) {
                    const auto next = next_eligible_placement_resource(
                        history.document, resource_id_t{input.object_id}, required_role);
                    worm_ui_frontend_resource_placed(input.object_id,
                                                     next.value_or(resource_id_t{}).value);
                }
            }
            const auto unchanged = replacing && !applied.applied &&
                                   applied.diagnostics.empty();
            result.accepted = applied.applied || unchanged;
            result.document_changed = applied.applied;
            result.diagnostics = applied.diagnostics;
            if (result.accepted) {
                state.cursor = *position;
                state.virtual_bms_cursor_row.reset();
                if (target_lane) {
                    state.active_lane = target_lane;
                }
            }
            accepted = unchanged ? "Object already uses the armed resource."
                                 : replacing ? "Object resource replaced at the pointer."
                                             : "Resource placed at the pointer.";
            break;
        }
        case WORM_UI_COMPOSE_INPUT_UNDO:
        case WORM_UI_COMPOSE_INPUT_REDO: {
            (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(history, state, select_editor_notes_action_t{});
            const bool changed = input.kind == WORM_UI_COMPOSE_INPUT_UNDO
                                     ? undo_document_command(history)
                                     : redo_document_command(history);
            status = changed ? (input.kind == WORM_UI_COMPOSE_INPUT_UNDO ? "Undo applied."
                                                                         : "Redo applied.")
                             : (input.kind == WORM_UI_COMPOSE_INPUT_UNDO ? "Nothing to undo."
                                                                         : "Nothing to redo.");
            if (changed) {
                const auto rebound = rebind_workspace(workspace, history);
                if (!rebound) {
                    return rebound.diagnostics.empty()
                               ? "History changed, but workspace rebinding failed."
                               : rebound.diagnostics.front().message;
                }
                return status;
            }
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE:
            scene_cache.beat_fit_drag.reset();
            scene_cache.beat_fit_asset.reset();
            navigation_input.cancel();
            application_state.bmson_marker_move_preview.reset();
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            accepted = "Compose gesture canceled.";
            break;
        case WORM_UI_COMPOSE_INPUT_NONE:
        default:
            continue;
        }
        }
        status = compose_result_status(result, accepted);
        if (result.accepted && result.audition.has_value()) {
            // Last accepted sound click in the bounded input batch wins. The
            // runtime audio layer consumes this revision-bound request once it
            // owns decoded source bindings and an SDL output stream.
            scene_cache.audition_request = result.audition;
        }
        if (result.accepted && result.document_changed) {
            const auto rebound = rebind_workspace(workspace, history);
            if (!rebound) {
                return rebound.diagnostics.empty()
                           ? "Compose edit applied, but workspace rebinding failed."
                           : rebound.diagnostics.front().message;
            }
        }
    }
    if (ui_action.compose_note_label_focus_consumed) {
        scene_cache.focus_note_label_editor = false;
    }

    const auto apply_viewport_actions = [&](const viewport_action_batch_t& actions) {
        for (std::size_t action_index = 0; action_index < actions.count; ++action_index) {
            const auto& viewport_action = actions[action_index];
            if (const auto* scroll = std::get_if<viewport_scroll_action_t>(&viewport_action)) {
                if (scroll->pane == viewport_pane_t::bgm) {
                    if (scroll->delta_x_pixels == 0.0)
                        continue;
                    const auto maximum_scroll = std::max(
                        0.0, static_cast<double>(bgm_lane_count) * compose_bgm_lane_width_pixels -
                                 bgm_pane_width);
                    auto& retained_scroll =
                        application_state.bgm_horizontal_scroll_pixels[state.chart.value];
                    retained_scroll =
                        std::clamp(retained_scroll + scroll->delta_x_pixels, 0.0, maximum_scroll);
                    status = "Compose BGM pane scrolled.";
                    continue;
                }
                if (scroll->delta_y_pixels == 0.0) {
                    continue;
                }
                if (input_chart == history.document.charts.end()) {
                    status = "The selected difficulty is no longer available.";
                    continue;
                }
                const auto result = dispatch_editor_action(
                    history, state,
                    scroll_editor_view_action_t{
                        compose_wheel_scroll_delta(scroll->delta_y_pixels,
                                                   compose_field_downscroll(*input_chart)),
                        compose_cursor_screen_anchor(state.viewport_height_pixels,
                                                     compose_field_downscroll(*input_chart)),
                        compose_field_downscroll(*input_chart)
                            ? editor_field_scroll_direction_t::down
                            : editor_field_scroll_direction_t::up});
                status = compose_result_status(result, "Compose field scrolled.");
            } else if (const auto* zoom = std::get_if<viewport_zoom_action_t>(&viewport_action)) {
                const auto result = dispatch_editor_action(
                    history, state,
                    adjust_editor_zoom_action_t{.factor = zoom->factor,
                                                .anchor_screen_y_pixels = zoom->cursor_y_pixels});
                status = compose_result_status(result, "Compose field zoomed at the pointer.");
            }
        }
    };
    for (std::size_t index = 0; index < ui_action.compose_viewport_input_count; ++index) {
        if (history.document.revision != ui_action.compose_input_document_revision ||
            workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
            workspace.selected_chart->value != ui_action.compose_input_chart_id ||
            state.chart.value != ui_action.compose_input_chart_id) {
            return "Remaining Compose viewport input was discarded after the chart or document "
                   "changed.";
        }
        const auto& input = ui_action.compose_viewport_inputs[index];
        const auto input_pane =
            bgm_pane_width > 0.0 && std::isfinite(input.cursor_x_pixels) &&
                    input.cursor_x_pixels >= field_left_pixels + field_width_pixels &&
                    input.cursor_x_pixels <= ui_action.compose_input_viewport_width_pixels
                ? viewport_pane_t::bgm
                : viewport_pane_t::chart;
        apply_viewport_actions(viewport_input.handle_wheel({
            .pane = input_pane,
            .kind = input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                        ? viewport_wheel_kind_t::coarse_steps
                        : viewport_wheel_kind_t::smooth_pixels,
            .delta_x = input.delta_x,
            .delta_y = input.delta_y,
            .zoom_modifier = input.zoom_modifier,
            .cursor_x_pixels = input.cursor_x_pixels,
            .cursor_y_pixels = input.cursor_y_pixels,
        }));
    }
    if (std::isfinite(viewport_delta_seconds) && viewport_delta_seconds > 0.0 &&
        history.document.revision == ui_action.compose_input_document_revision &&
        workspace.active_tab == workspace_tab_t::compose && workspace.selected_chart &&
        workspace.selected_chart->value == ui_action.compose_input_chart_id &&
        state.chart.value == ui_action.compose_input_chart_id) {
        apply_viewport_actions(viewport_input.advance(viewport_delta_seconds));
    }
    scene_cache.viewport_autoscroll_active = viewport_input.autoscroll_active();
    if (!scene_cache.viewport_autoscroll_active) {
        scene_cache.viewport_autoscroll_anchor_x_pixels = 0.0;
        scene_cache.viewport_autoscroll_anchor_y_pixels = 0.0;
        scene_cache.viewport_autoscroll_pointer_x_pixels = 0.0;
        scene_cache.viewport_autoscroll_pointer_y_pixels = 0.0;
    }
    if (!status.empty()) {
        return status;
    }
    return ui_action.compose_input_count != 0 || ui_action.compose_viewport_input_count != 0
               ? "Compose input handled."
               : std::string{};
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
