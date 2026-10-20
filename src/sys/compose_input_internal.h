#pragma once
#include "sys/application_edit_inputs.h"
#include "sys/compose_input.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_navigation.h"
#include "sys/compose_navigation_input.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_playhead_skip.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/compose_sound_selection.h"
#include <wormedit/core/chart_clipboard.h>

#include <wormedit/audio/decoder.h>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/kiai_spans.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/span_manipulation.h>
#include <wormedit/core/sv_recipe.h>
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
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace wormedit::sys {
namespace compose_input_detail {}
using namespace compose_input_detail;
using namespace compose_navigation_input_detail;
} // namespace wormedit::sys

namespace wormedit::sys::compose_input_detail {
auto validate_bmson_marker_positions(
    const wormedit::chart_indices_t& indices,
    std::span<const wormedit::bmson_sound_marker_insertion_t> markers) -> std::string;

auto find_bmson_marker_channel(const wormedit::chart_document_t& chart,
                               wormedit::object_id_t reference)
    -> std::pair<const wormedit::bgm_event_t*, const wormedit::sound_channel_t*>;

auto validate_bmson_marker_channel(
    const wormedit::sound_channel_t& channel,
    std::span<const wormedit::bmson_sound_marker_insertion_t> markers) -> std::string;

auto validate_bmson_marker_draft(const application_command_state_t::bmson_marker_draft_t* draft,
                                 std::span<const wormedit::bmson_sound_marker_insertion_t> markers)
    -> std::string;

} // namespace wormedit::sys::compose_input_detail

namespace wormedit::sys {
auto propose_bmson_bgm_markers(const wormedit::document_history_t& history,
                             const wormedit::editor_interaction_state_t& state,
                             application_command_state_t& application_state,
                             wormedit::object_id_t reference,
                             std::span<const wormedit::bmson_sound_marker_insertion_t> markers,
                             wormedit::notification_severity_t* severity) -> std::string;

struct bmson_marker_source_t {
    wormedit::object_id_t source_lane;
    wormedit::local_position_t position;
    wormedit::object_id_t source_event;
    const wormedit::sound_channel_t* channel{};
};

auto find_bmson_marker_source(const wormedit::chart_document_t& chart,
                              wormedit::object_id_t bgm_event)
    -> std::optional<bmson_marker_source_t>;

} // namespace wormedit::sys

namespace wormedit::sys::compose_input_detail {
template <typename PositionAtComposeY>
auto dispatch_bmson_razor_input(const WormUiComposeInputAction& input,
                                const wormedit::document_history_t& history,
                                const wormedit::editor_interaction_state_t& state,
                                application_command_state_t& application_state, bool gesture_active,
                                PositionAtComposeY&& position_at_compose_y,
                                wormedit::notification_severity_t& severity) -> std::string {
    severity = wormedit::notification_severity_t::warning;
    using namespace wormedit;
    if (gesture_active) {
        return "Finish or cancel the active Compose gesture before using the BMSON razor.";
    }
    const auto* chart = history.document.find_chart(state.chart);
    const auto current_view = compose_view_for_state(history, state);
    if (chart == nullptr || !current_view || !std::isfinite(input.y_pixels)) {
        return "The BMSON chart field is no longer available.";
    }
    auto& stroke = application_state.project.compose_gesture.bmson_razor_stroke;
    const object_id_t reference{input.object_id};
    if (input.razor_stroke_start) {
        stroke.reset();
    } else if (!stroke || stroke->stamp.chart != state.chart || stroke->payload.reference != reference) {
        stroke.reset();
        return "Begin a new BMSON razor stroke on the selected sound channel.";
    } else if (!stroke->stamp.document.is_current(history.document,
                                                 document_identity_policy_t::revision_only)) {
        stroke.reset();
        return stale_action_message("BMSON razor stroke");
    }
    const auto denominator =
        input.razor_stroke_start ? edit_snap_denominator(*chart, state) : stroke->payload.snap_denominator;
    const auto raw = position_at_compose_y(*chart, *current_view, input.y_pixels);
    const auto position =
        raw ? resolve_resource_placement_position(*chart, *raw, denominator) : std::nullopt;
    if (!position) {
        return "BMSON razor could not snap to a valid chart row.";
    }
    const auto restart = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART;
    if (input.razor_stroke_start) {
        const auto& draft = application_state.project.compose_gesture.bmson_marker_draft;
        const auto already_staged =
            draft && draft->stamp.is_current(history.document, state.chart,
                wormedit::document_identity_policy_t::revision_only) &&
            draft->payload.reference == reference &&
            draft->payload.has_position(*position);
        const auto status = already_staged
                                ? std::string{"BMSON razor stroke started on a staged pulse."}
                                : propose_bmson_bgm_marker(history, state, application_state,
                                                         reference, *position, restart, &severity);
        severity = staged_input_severity(already_staged, severity);
        const auto& staged = application_state.project.compose_gesture.bmson_marker_draft;
        auto proposed = application_command_state_t::bmson_razor_stroke_t::propose(
            history, state.chart, staged ? &*staged : nullptr, reference, denominator, *position);
        stroke = std::move(proposed.value);
        return status;
    }
    const auto path =
        interpolate_bmson_razor_path(*chart, stroke->payload.last_position, *position, denominator);
    if (!path) {
        severity = to_notification_severity(path.diagnostics.front().severity);
        return path.diagnostics.front().message;
    }
    std::vector<bmson_sound_marker_insertion_t> pending;
    pending.reserve(path.positions.size());
    for (const auto candidate : path.positions) {
        const auto& draft = application_state.project.compose_gesture.bmson_marker_draft;
        if (draft && draft->payload.has_position(candidate)) {
            continue;
        }
        pending.push_back({.position = candidate, .restart = restart});
    }
    if (pending.empty()) {
        auto revised = stroke->revise(*position);
        stroke = std::move(revised.value);
        severity = notification_severity_t::hint;
        return "BMSON razor remains on staged grid cells.";
    }
    const auto before = application_state.project.compose_gesture.bmson_marker_draft
                            ? application_state.project.compose_gesture.bmson_marker_draft->payload.markers.size()
                            : 0;
    const auto status =
        propose_bmson_bgm_markers(history, state, application_state, reference, pending, &severity);
    if (application_state.project.compose_gesture.bmson_marker_draft &&
        application_state.project.compose_gesture.bmson_marker_draft->payload.markers.size() == before + pending.size()) {
        auto revised = stroke->revise(*position);
        stroke = std::move(revised.value);
    }
    return status;
}

enum class compose_input_route_t { unhandled, continue_batch, stop_batch, editor_result };

struct compose_input_family_result_t {
    compose_input_route_t route{compose_input_route_t::unhandled};
    compose_action_outcome_t outcome;

    [[nodiscard]] auto is_handled() const -> bool {
        return route != compose_input_route_t::unhandled;
    }
    [[nodiscard]] auto is_stopping_batch() const -> bool {
        return route == compose_input_route_t::stop_batch;
    }
    [[nodiscard]] auto has_editor_result() const -> bool {
        return route == compose_input_route_t::editor_result;
    }
    [[nodiscard]] static auto from(std::string message) -> compose_input_family_result_t {
        return {.route = compose_input_route_t::stop_batch,
                .outcome = compose_action_outcome_t::blocked(std::move(message),
                                                             notification_source_t::input)};
    }
    [[nodiscard]] static auto from(compose_action_outcome_t outcome,
                                   compose_input_route_t route = compose_input_route_t::stop_batch)
        -> compose_input_family_result_t {
        return {.route = route, .outcome = std::move(outcome)};
    }
    [[nodiscard]] static auto
    continue_batch(std::string message,
                   notification_severity_t severity = notification_severity_t::hint,
                   notification_source_t source = notification_source_t::input)
        -> compose_input_family_result_t {
        auto feedback = stop_with_feedback(std::move(message), severity, source);
        feedback.route = compose_input_route_t::continue_batch;
        return feedback;
    }
    [[nodiscard]] static auto stop_with_feedback(std::string message,
                                                 notification_severity_t severity,
                                                 notification_source_t source)
        -> compose_input_family_result_t {
        return from(compose_action_outcome_t::from(
            std::move(message), severity,
            severity == notification_severity_t::hint ? notification_source_t::input : source));
    }
    [[nodiscard]] static auto continue_with_result() -> compose_input_family_result_t {
        return {.route = compose_input_route_t::editor_result};
    }
};

inline auto input_diagnostic_feedback(const wormedit::diagnostic_list_t& diagnostics,
                               std::string_view fallback,
                               wormedit::notification_severity_t fallback_severity,
                               wormedit::notification_source_t source)
    -> compose_input_family_result_t {
    if (diagnostics.empty())
        return compose_input_family_result_t::stop_with_feedback(std::string{fallback},
                                                                 fallback_severity, source);
    return compose_input_family_result_t::stop_with_feedback(
        diagnostics.front().message, to_notification_severity(diagnostics.front().severity),
        source);
}

auto dispatch_compose_navigation_input(const WormUiComposeInputAction&,
                                       wormedit::document_history_t&,
                                       wormedit::editor_interaction_state_t&,
                                       wormedit::editor_navigation_input_controller_t&,
                                       application_command_state_t&,
                                       const wormedit::notification_sink_t*)
    -> compose_input_family_result_t;

auto dispatch_compose_cursor_input(const WormUiComposeInputAction&, wormedit::document_history_t&,
                                   wormedit::editor_interaction_state_t&,
                                   application_command_state_t&,
                                   const wormedit::notification_sink_t*)
    -> compose_input_family_result_t;

void audition_created_note(bool enabled, const wormedit::document_history_t& history,
                           const wormedit::editor_interaction_state_t& state,
                           wormedit::editor_interaction_result_t& result);

auto dispatch_compose_lane_key_press(const WormUiComposeInputAction&, wormedit::document_history_t&,
                                     wormedit::editor_interaction_state_t&,
                                     application_command_state_t&, compose_scene_cache_t&,
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
    std::string_view& accepted, const wormedit::notification_sink_t* notifications,
    compose_scene_cache_t& scene_cache) -> compose_input_family_result_t;

auto dispatch_compose_navigation_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application, const wormedit::notification_sink_t* notifications)
    -> compose_input_family_result_t;

auto dispatch_compose_cursor_input(const WormUiComposeInputAction& input,
                                   wormedit::document_history_t& history,
                                   wormedit::editor_interaction_state_t& state,
                                   application_command_state_t& application,
                                   const wormedit::notification_sink_t* notifications)
    -> compose_input_family_result_t;

auto note_paint_sound(const WormUiComposeInputAction& input)
    -> std::optional<wormedit::resource_id_t>;

void advance_sound_after_paint_commit(const wormedit::editor_interaction_result_t& result,
                                      std::optional<wormedit::resource_id_t> sound,
                                      bool notes_created);

auto retained_bmson_source_geometry(const wormedit::document_history_t& history,
                                    const compose_scene_cache_t& cache,
                                    const wormedit::chart_document_t& chart,
                                    wormedit::asset_id_t asset)
    -> std::optional<wormedit::bmson_source_geometry_t>;

auto compose_stem_geometry(const wormedit::document_history_t& history,
                           const wormedit::editor_interaction_state_t& state,
                           const compose_scene_cache_t& cache,
                           std::optional<wormedit::resource_id_t> sound)
    -> std::optional<wormedit::bmson_source_geometry_t>;

auto dispatch_compose_lane_key_press(const WormUiComposeInputAction& input,
                                     wormedit::document_history_t& history,
                                     wormedit::editor_interaction_state_t& state,
                                     application_command_state_t& application,
                                     compose_scene_cache_t& cache,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> compose_input_family_result_t;

auto lane_key_sound(const wormedit::editor_interaction_state_t& state, std::uint64_t lane)
    -> std::optional<wormedit::resource_id_t>;

auto dispatch_compose_lane_key_release(const WormUiComposeInputAction& input,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t& workspace,
                                       wormedit::editor_interaction_state_t& state,
                                       wormedit::editor_interaction_result_t& result,
                                       std::string_view& accepted) -> compose_input_family_result_t;

struct bmson_compose_context_t {
    const compose_input_inputs_t& environment;
    const WormUiWorkspaceAction& action;
    wormedit::editor_interaction_state_t& state;
    bool gesture_active;
};

auto begin_bmson_bgm_move(const WormUiComposeInputAction& input, bmson_compose_context_t& context)
    -> compose_input_family_result_t;

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
        const auto rounded = snap_absolute_row(chart, snapped, snap_denominator);
        if (!rounded)
            return std::nullopt;
        snapped = *rounded;
    }
    const auto measure = canvas->measure_containing(snapped);
    return measure && !measure->authored ? std::optional{snapped} : std::nullopt;
}

template <typename PositionAtComposeY, typename VirtualBmsRowAt>
auto update_bmson_bgm_move(const WormUiComposeInputAction& input, bmson_compose_context_t& context,
                           PositionAtComposeY&& position_at_compose_y,
                           VirtualBmsRowAt&& virtual_bms_row_at) -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, action, state, gesture_active] = context;
    auto& history = environment.document.history;
    if (!state.gesture<wormedit::pending_note_move_gesture_t>() ||
        state.gesture<wormedit::pending_note_move_gesture_t>()->reference_note != object_id_t{input.object_id})
        return compose_input_family_result_t::from("BGM event drag expired before release.");
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    auto position = chart != nullptr && view ? position_at_compose_y(*chart, *view, input.y_pixels)
                                             : std::nullopt;
    std::optional<std::uint64_t> virtual_row;
    if (state.y_lock)
        position = state.gesture<wormedit::pending_note_move_gesture_t>()->preview_position;
    else if (!position && chart != nullptr && view)
        virtual_row =
            virtual_bms_move_target(*chart, *view, input.y_pixels,
                                    edit_snap_denominator(*chart, state), virtual_bms_row_at);
    if (!position && !virtual_row) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "Selected-object move fell outside chart geometry.");
    }
    const auto target = position.value_or(state.gesture<wormedit::pending_note_move_gesture_t>()->preview_position);
    const auto lane = input.lane_id ? std::optional{object_id_t{input.lane_id}} : std::nullopt;
    const auto releasing = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE;
    const auto result = dispatch_editor_action(
        history, state,
        update_note_move_gesture_action_t{target,
                                          {},
                                          false,
                                          lane,
                                          virtual_row,
                                          releasing ? proposal_operation_t::apply
                                                    : proposal_operation_t::revise});
    if (!result.accepted)
        return compose_input_family_result_t::from(
            result.diagnostics.empty() ? "Selected-object move could not be applied."
                                       : result.diagnostics.front().message);
    auto feedback =
        releasing ? compose_input_family_result_t::stop_with_feedback(
                        result.document_changed ? "Selected objects moved to the snapped row."
                                                : "BGM event remained at its original position.",
                        result.document_changed ? notification_severity_t::success
                                                : notification_severity_t::info,
                        notification_source_t::edit)
                  : compose_input_family_result_t::continue_batch(
                        "Selected-object move preview updated.");
    feedback.outcome.mutated = result.document_changed;
    feedback.outcome.rebind_failure = "Objects moved, but workspace rebinding failed.";
    feedback.outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    feedback.outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::document_only;
    feedback.outcome.rebind_failure_severity = notification_severity_t::warning;
    feedback.outcome.rebind_failure_source = notification_source_t::input;
    return feedback;
}

auto revise_bmson_draft_marker(const WormUiComposeInputAction& input,
                             bmson_compose_context_t& context) -> compose_input_family_result_t;

auto toggle_bmson_restart(const WormUiComposeInputAction& input, bmson_compose_context_t& context)
    -> compose_input_family_result_t;

auto retained_bmson_source_geometry(const bmson_compose_context_t& context,
                                    const wormedit::chart_document_t& chart,
                                    wormedit::asset_id_t asset)
    -> std::optional<wormedit::bmson_source_geometry_t>;

auto bmson_stem_drop_matches(const wormedit::song_document_t& document,
                             const wormedit::chart_document_t& chart, std::uint64_t resource,
                             wormedit::object_id_t channel) -> bool;

auto apply_bmson_draw_point(const WormUiComposeInputAction& input, bmson_compose_context_t& context,
                            const wormedit::chart_document_t& chart,
                            wormedit::local_position_t position)
    -> wormedit::document_command_result_t;

template <typename PositionAtComposeY>
auto insert_bmson_stem_point(const WormUiComposeInputAction& input,
                             bmson_compose_context_t& context,
                             PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, action, state, gesture_active] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    if (gesture_active || !application_state.project.compose_gesture.note_paint_mode_active ||
        !wormedit::ui::compose_notes_editable(compose_mode_of(application_state)))
        return compose_input_family_result_t::from(
            "Draw requires an editable chart without an active gesture.");
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (chart == nullptr || !view || !std::isfinite(input.y_pixels))
        return compose_input_family_result_t::from("The BMSON chart field is no longer available.");
    const auto raw = position_at_compose_y(*chart, *view, input.y_pixels);
    const auto position = raw ? resolve_resource_placement_position(
                                    *chart, *raw, edit_snap_denominator(*chart, state))
                              : std::nullopt;
    if (!position)
        return compose_input_family_result_t::from(
            "The point could not snap to a valid chart row.");
    const auto applied = apply_bmson_draw_point(input, context, *chart, *position);
    if (!applied.diagnostics.empty())
        return compose_input_family_result_t::stop_with_feedback(
            applied.diagnostics.front().message,
            to_notification_severity(applied.diagnostics.front().severity),
            notification_source_t::edit);
    if (!applied.applied)
        return compose_input_family_result_t::from("No BMSON point was added.");
    auto outcome = compose_action_outcome_t::from(
        "BMSON point added.", notification_severity_t::success, notification_source_t::edit, true);
    outcome.rebind_failure = "BMSON point added, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::document_only;
    return compose_input_family_result_t::from(std::move(outcome));
}

template <typename PositionAtComposeY, typename VirtualBmsRowAt>
auto dispatch_bmson_compose_input(const WormUiWorkspaceAction& ui_action,
                                  const WormUiComposeInputAction& input,
                                  const compose_input_inputs_t& environment,
                                  wormedit::editor_interaction_state_t& state, bool gesture_active,
                                  PositionAtComposeY&& position_at_compose_y,
                                  VirtualBmsRowAt&& virtual_bms_row_at)
    -> compose_input_family_result_t {
    auto& history = environment.document.history;
    auto& application_state = environment.commands;

    auto context = bmson_compose_context_t{environment, ui_action, state, gesture_active};
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN:
        return begin_bmson_bgm_move(input, context);
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE:
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE:
        return update_bmson_bgm_move(input, context, position_at_compose_y, virtual_bms_row_at);
    case WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL:
        if (context.state.gesture<wormedit::pending_note_move_gesture_t>())
            (void)wormedit::dispatch_editor_action(context.environment.document.history, context.state,
                                                   wormedit::cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::stop_with_feedback(
            "BMSON BGM marker drag canceled.", wormedit::notification_severity_t::hint,
            wormedit::notification_source_t::input);
    case WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE:
    case WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART: {
        auto severity = wormedit::notification_severity_t::warning;
        auto message = dispatch_bmson_razor_input(input, history, state, application_state,
                                                  gesture_active, position_at_compose_y, severity);
        return compose_input_family_result_t::stop_with_feedback(
            std::move(message), severity, wormedit::notification_source_t::input);
    }
    case WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT: {
        const auto& draft = application_state.project.compose_gesture.bmson_marker_draft;
        if (!draft || input.object_id == 0 || input.object_id > draft->payload.markers.size())
            return compose_input_family_result_t::from(
                "The BMSON region audition request expired.");
        if (!draft->stamp.is_current(history.document, state.chart,
                wormedit::document_identity_policy_t::revision_only))
            return compose_input_family_result_t::from(
                wormedit::stale_action_message("BMSON region audition"));
        auto result = compose_input_family_result_t::continue_batch(
            "Preparing staged BMSON region audition.");
        result.outcome.audition = bmson_draft_audition_request_t{
            .chart = draft->stamp.chart, .revision = draft->stamp.document.revision,
            .reference = draft->payload.reference, .markers = draft->payload.markers,
            .marker_index = static_cast<std::size_t>(input.object_id - 1)};
        return result;
    }
    case WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE:
    case WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM:
    case WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART:
    case WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT:
        return revise_bmson_draft_marker(input, context);
    case WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE:
        return toggle_bmson_restart(input, context);
    case WORM_UI_COMPOSE_INPUT_BMSON_EMPTY_SELECT:
        if (application_state.project.compose_gesture.note_paint_mode_active)
            return {};
        (void)wormedit::dispatch_editor_action(history, state,
                                               wormedit::select_editor_objects_action_t{});
        return compose_input_family_result_t::continue_batch("Selection cleared.");
    case WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW:
    case WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE:
    case WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART:
        return insert_bmson_stem_point(input, context, position_at_compose_y);
    default:
        return {};
    }
}

void update_compose_viewport_pointer_input(const WormUiWorkspaceAction& ui_action,
                                           wormedit::viewport_input_controller_t& viewport_input,
                                           compose_scene_cache_t& scene_cache,
                                           double bgm_pane_width, double bgm_pane_left_pixels,
                                           double viewport_width_pixels);

void update_compose_pointer_hover(const WormUiWorkspaceAction& ui_action, bool gesture_active,
                                  const wormedit::chart_document_t* chart,
                                  const wormedit::editor_interaction_view_model_t* interaction_view,
                                  std::uint64_t revision, double field_left_pixels,
                                  double field_width_pixels, compose_scene_cache_t& scene_cache,
                                  const wormedit::editor_preferences_t& preferences);

struct compose_input_context_t {
    const compose_input_inputs_t& environment;
    WormUiWorkspaceAction& ui_action;
    wormedit::editor_interaction_state_t& state;
    const wormedit::notification_sink_t* notifications{};
    const wormedit::chart_document_t* input_chart{};
    bool gesture_active{};
    std::size_t bgm_lane_count{};
    double bgm_pane_width{};
    double bgm_pane_left_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};

    auto is_input_current() const -> bool {
        const wormedit::chart_stamp_t stamp{
            .document = {environment.document.history.document.id,
                         ui_action.compose_input_document_revision},
            .chart = wormedit::chart_id_t{ui_action.compose_input_chart_id}};
        return stamp.is_current(environment.document.history.document, state.chart) &&
               environment.document.workspace.active_tab == wormedit::workspace_tab_t::compose &&
               environment.document.workspace.selected_chart &&
               environment.document.workspace.selected_chart->value ==
                   ui_action.compose_input_chart_id;
    }

    auto is_timing_input_current(const WormUiComposeInputAction& input) const -> bool {
        const wormedit::chart_stamp_t stamp{
            .document = {.project = environment.document.history.document.id,
                         .revision = input.timing_document_revision},
            .chart = wormedit::chart_id_t{input.timing_chart_id}};
        return stamp.is_current(environment.document.history.document, state.chart);
    }

    auto chart() const -> const wormedit::chart_document_t* {
        const auto* found = environment.document.history.document.find_chart(state.chart);
        return found;
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
        return compose_source_y(
            y, state.viewport_height_pixels,
            input_chart &&
                compose_field_downscroll(
                    *input_chart, environment.commands.preferences.editor_preferences));
    }
    auto hit_target_at(const wormedit::chart_document_t& chart,
                       const wormedit::editor_interaction_view_model_t& view, double x_pixels,
                       double y_pixels) -> std::optional<wormedit::editor_hit_target_t> {
        return hit_target_unrestricted(chart, view, x_pixels, y_pixels);
    }
    auto note_move_hit_target_at(WormUiComposeInputKind kind,
                                 const wormedit::chart_document_t& chart,
                                 const wormedit::editor_interaction_view_model_t& view,
                                 double x_pixels, double y_pixels)
        -> std::optional<wormedit::editor_hit_target_t> {
        const auto target = hit_target_at(chart, view, x_pixels, y_pixels);
        if (target || (kind != WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE &&
                       kind != WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE))
            return target;
        if (!state.gesture<wormedit::pending_note_move_gesture_t>() || !std::isfinite(x_pixels) ||
            x_pixels < field_left_pixels || x_pixels >= field_left_pixels + field_width_pixels)
            return std::nullopt;
        const auto position = position_at_compose_y(chart, view, y_pixels);
        // A valid row with no target lane keeps the original lanes, allowing red vertical
        // proposals through empty layouts. A pointer outside the field/chart still cancels.
        return position ? std::optional{wormedit::editor_hit_target_t{.position = *position}}
                        : std::nullopt;
    }
    auto hit_target_unrestricted(const wormedit::chart_document_t& chart,
                                 const wormedit::editor_interaction_view_model_t& view,
                                 double x_pixels, double y_pixels)
        -> std::optional<wormedit::editor_hit_target_t> {
        const auto* index = environment.controls.cache.hit_test_index_for(
            chart, environment.document.history.document.revision);
        if (!index) return std::nullopt;
        if (environment.commands.project.compose_gesture.note_paint_mode_active)
            return resolve_compose_paint_cell(
                chart, *index, view, x_pixels,
                y_pixels, field_left_pixels, field_width_pixels,
                compose_field_downscroll(
                    chart, environment.commands.preferences.editor_preferences),
                wormedit::edit_snap_denominator(chart, state));
        return wormedit::hit_test_editor(
            chart, *index, view,
            {.x_pixels = x_pixels,
             .y_pixels = compose_source_y(
                 y_pixels, view.viewport_height_pixels,
                 compose_field_downscroll(
                     chart, environment.commands.preferences.editor_preferences)),
             .field_left_pixels = field_left_pixels,
             .field_width_pixels = field_width_pixels,
             .note_head_height_pixels =
                 environment.controls.cache.frame.preview_scene_request.note_head_height_pixels,
             .split_stage_gutter_pixels = environment.controls.cache.frame.preview_scene_request
                                              .split_stage_gutter_pixels});
    }
    auto position_at_compose_y(const wormedit::chart_document_t& chart,
                               const wormedit::editor_interaction_view_model_t& view,
                               double y_pixels) const -> std::optional<wormedit::local_position_t> {
        return position_at_compose_y_unrestricted(chart, view, y_pixels);
    }
    auto position_at_compose_y_unrestricted(const wormedit::chart_document_t& chart,
                                            const wormedit::editor_interaction_view_model_t& view,
                                            double y_pixels) const
        -> std::optional<wormedit::local_position_t> {
        return wormedit::editor_position_at_y_pixels(
            chart, view,
            compose_source_y(
                y_pixels, view.viewport_height_pixels,
                compose_field_downscroll(
                    chart, environment.commands.preferences.editor_preferences)));
    }
    auto virtual_bms_row_at(const wormedit::chart_document_t& chart,
                            const wormedit::editor_interaction_view_model_t& view,
                            double y_pixels) const -> std::optional<std::uint64_t> {
        using namespace wormedit;
        if (!has_chart_virtual_tail(chart) || !std::isfinite(y_pixels) || y_pixels < 0.0 ||
            y_pixels >= view.viewport_height_pixels)
            return std::nullopt;
        const auto built = build_virtual_canvas(chart);
        const auto* canvas = std::get_if<virtual_canvas_t>(&built);
        if (!canvas)
            return std::nullopt;
        const auto source_y = compose_source_y(
            y_pixels, view.viewport_height_pixels,
            compose_field_downscroll(chart,
                                     environment.commands.preferences.editor_preferences));
        const auto row = (static_cast<long double>(view.viewport_scroll_pixels) + source_y) *
                         chart.divisions_per_standard_measure / view.pixels_per_standard_measure;
        if (!std::isfinite(row) || row < 0.0L ||
            row >= static_cast<long double>(canvas->total_rows))
            return std::nullopt;
        const auto canvas_row =
            nearest_row(row, canvas->total_rows, row_clamp_policy_t::reject_outside);
        if (!canvas_row)
            return std::nullopt;
        const auto measure = canvas->measure_containing(*canvas_row);
        return measure && !measure->authored ? canvas_row : std::nullopt;
    }
    auto virtual_bms_hit_at(const wormedit::chart_document_t& chart,
                            const wormedit::editor_interaction_view_model_t& view, double x_pixels,
                            double y_pixels)
        -> std::optional<std::pair<std::uint64_t, wormedit::lane_id_t>> {
        if (!environment.controls.cache.scene.index.value())
            return std::nullopt;
        const auto row = virtual_bms_row_at(chart, view, y_pixels);
        if (!row)
            return std::nullopt;
        auto request = environment.controls.cache.frame.preview_scene_request;
        request.include_virtual_bms_tail = true;
        const auto composed = wormedit::editor_field_scene_view_result_t::from(
            chart, *environment.controls.cache.scene.index.value(), view, request);
        if (!composed.value || !composed.ok())
            return std::nullopt;
        for (const auto& lane : composed.value->lanes) {
            if (x_pixels >= lane.bounds.left && x_pixels < lane.bounds.right &&
                y_pixels >= lane.bounds.top && y_pixels < lane.bounds.bottom)
                return std::pair{*row, lane.lane};
        }
        return std::nullopt;
    }
};

struct compose_gesture_context_t {
    const compose_input_inputs_t& environment;
    wormedit::editor_interaction_state_t& state;
    wormedit::editor_interaction_result_t& result;
    std::string_view& accepted;
};

template <typename HitTargetAt>
auto begin_alt_pointer_gesture(const WormUiComposeInputAction& input,
                               compose_gesture_context_t& context, HitTargetAt&& hit_target_at,
                               const compose_scene_cache_t& cache)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, state, result, accepted] = context;
    auto& history = environment.document.history;
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (chart == nullptr || !view) {
        return compose_input_family_result_t::from(
            "The selected chart field is no longer available.");
    }
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!target)
        return compose_input_family_result_t::from(
            "Alt/Option-drag must begin over an editable lane.");
    if (target->note && target->note_kind &&
        (*target->note_kind == note_kind_t::tap || *target->note_kind == note_kind_t::hold)) {
        state.active_lane = target->lane;
        place_cursor_on_picked_row(state, target->position);
        result = dispatch_editor_action(history, state,
                                        begin_note_duration_gesture_action_t{*target->note});
        accepted = "Note resize started; drag to change duration and release to finish.";
    } else if (target->note) {
        return compose_input_family_result_t::from(
            "Alt/Option-drag resizes tap and hold notes; this note kind is not resizable.");
    } else {
        result = dispatch_editor_action(history, state,
                                        begin_alt_pointer_hold_gesture_action_t{
                                            .lane = target->lane,
                                            .position = target->position,
                                            .alt_or_option_down = true,
                                            .sound = note_paint_sound(input),
                                            .source_geometry = compose_stem_geometry(
                                                history, state, cache, note_paint_sound(input))});
        accepted = "Pointer hold started; release to finish.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename PositionAtComposeY>
auto update_alt_pointer_gesture(const WormUiComposeInputAction& input,
                                compose_gesture_context_t& context,
                                PositionAtComposeY&& position_at_compose_y,
                                const compose_scene_cache_t& cache)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, state, result, accepted] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (chart == nullptr || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "The selected chart field is no longer available.");
    }
    const auto y =
        std::clamp(input.y_pixels, 0.0, std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto position = position_at_compose_y(*chart, *view, y);
    if (!position) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "Pointer hold input fell outside chart geometry and was canceled.");
    }
    const auto releasing = input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE;
    if (state.gesture<wormedit::pending_note_duration_gesture_t>()) {
        result = dispatch_editor_action(history, state,
                                        update_note_duration_gesture_action_t{
                                            *position, releasing ? proposal_operation_t::apply
                                                                 : proposal_operation_t::revise});
        accepted = releasing ? (result.document_changed ? "Note duration committed."
                                                        : "Note resize released.")
                             : "Note resize preview updated.";
    } else {
        const auto sound = state.hold() ? state.hold()->sound : std::nullopt;
        result = releasing ? dispatch_editor_action(history, state,
                                                    release_alt_pointer_hold_gesture_action_t{
                                                        .position = *position,
                                                        .alt_or_option_down = true,
                                                        .pointer_captured = true,
                                                        .source_geometry = compose_stem_geometry(
                                                            history, state, cache, sound)})
                           : dispatch_editor_action(history, state,
                                                    update_alt_pointer_hold_gesture_action_t{
                                                        .position = *position,
                                                        .alt_or_option_down = true,
                                                        .pointer_captured = true});
        if (releasing)
            advance_sound_after_paint_commit(result, sound, result.created_note.has_value());
        if (releasing)
            audition_created_note(application_state.preferences.editor_preferences.audition_on_click, history,
                                  state, result);
        accepted = releasing ? (result.document_changed ? "Pointer hold committed."
                                                        : "Pointer hold released.")
                             : "Pointer hold preview updated.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename HitTargetAt, typename PositionAtComposeY>
auto dispatch_alt_pointer_gesture(const WormUiComposeInputAction& input,
                                  compose_gesture_context_t& context, HitTargetAt&& hit_target_at,
                                  PositionAtComposeY&& position_at_compose_y,
                                  const compose_scene_cache_t& cache)
    -> compose_input_family_result_t {
    if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN)
        return begin_alt_pointer_gesture(input, context, hit_target_at, cache);
    return update_alt_pointer_gesture(input, context, position_at_compose_y, cache);
}

template <typename HitTargetAt>
auto dispatch_note_paint_gesture(const WormUiComposeInputAction& input,
                                 compose_gesture_context_t& context, HitTargetAt&& hit_target_at,
                                 const compose_scene_cache_t& cache)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, state, result, accepted] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    if (!application_state.project.compose_gesture.note_paint_mode_active) {
        if (state.gesture<wormedit::pending_note_paint_gesture_t>()) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            accepted = "Note-paint stroke canceled because paint mode is inactive.";
        } else {
            result.accepted = true;
            accepted = "Note-paint input ignored because paint mode is inactive.";
        }
        return compose_input_family_result_t::continue_with_result();
    }
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (chart == nullptr || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "The selected chart field is no longer available.");
    }
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!target) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN)
            return compose_input_family_result_t::from(
                "Begin note painting over an editable chart lane.");
        if (input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE ||
            !state.gesture<wormedit::pending_note_paint_gesture_t>()) {
            result.accepted = true;
            accepted = "Note-paint preview retained outside editable chart geometry.";
            return compose_input_family_result_t::continue_with_result();
        }
        const auto& pending = *state.gesture<wormedit::pending_note_paint_gesture_t>();
        const auto sound = pending.sound;
        result = dispatch_editor_action(
            history, state,
            end_note_paint_gesture_action_t{
                .lane = pending.last_lane,
                .position = pending.last_position,
                .source_geometry = compose_stem_geometry(history, state, cache, pending.sound)});
        advance_sound_after_paint_commit(result, sound, !state.selected_notes.empty());
        accepted = "Note-paint stroke released at its last editable cell.";
        return compose_input_family_result_t::continue_with_result();
    }
    const auto captured = true;
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
        result = dispatch_editor_action(
            history, state,
            begin_note_paint_gesture_action_t{
                .lane = target->lane,
                .position = target->position,
                .pointer_captured = captured,
                .sound = note_paint_sound(input),
                .intent = static_cast<note_paint_intent_t>(input.paint_intent),
                .source_geometry =
                    compose_stem_geometry(history, state, cache, note_paint_sound(input))});
        accepted = "Note-paint stroke started.";
    } else if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE) {
        result = dispatch_editor_action(
            history, state,
            update_note_paint_gesture_action_t{
                .lane = target->lane, .position = target->position, .pointer_captured = captured});
        accepted = "Note-paint preview updated.";
    } else {
        const auto stroke_sound = state.gesture<wormedit::pending_note_paint_gesture_t>()
                                      ? state.gesture<wormedit::pending_note_paint_gesture_t>()->sound
                                      : std::nullopt;
        result = dispatch_editor_action(
            history, state,
            end_note_paint_gesture_action_t{
                .lane = target->lane,
                .position = target->position,
                .pointer_captured = captured,
                .source_geometry = compose_stem_geometry(history, state, cache, stroke_sound)});
        advance_sound_after_paint_commit(result, stroke_sound, !state.selected_notes.empty());
        accepted = result.document_changed ? "Note-paint stroke committed."
                                           : "Note-paint stroke released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

void arm_compose_note_layers(wormedit::editor_interaction_state_t& state,
                             const application_command_state_t& app,
                             const wormedit::chart_document_t& chart);

auto semantic_layer_paint_armed(const application_command_state_t& app) -> bool;

auto semantic_layer_paint_target_editable(const application_command_state_t& app,
                                          const wormedit::semantic_layer_t* layer) -> bool;

struct semantic_layer_paint_target_t {
    std::optional<wormedit::object_id_t> note;
    std::optional<wormedit::object_id_t> bgm_event;
};

auto apply_semantic_layer_paint_target(const WormUiComposeInputAction& input,
                                       compose_gesture_context_t& context,
                                       const semantic_layer_paint_target_t& target,
                                       const wormedit::semantic_layer_t* layer)
    -> compose_input_family_result_t;

auto handle_empty_semantic_layer_paint_target(const WormUiComposeInputAction& input,
                                              compose_gesture_context_t& context)
    -> compose_input_family_result_t;

auto handle_inactive_semantic_layer_paint(compose_gesture_context_t& context)
    -> compose_input_family_result_t;

template <typename HitTargetAt, typename BgmEventAt>
auto dispatch_semantic_layer_paint_gesture(const WormUiComposeInputAction& input,
                                           compose_gesture_context_t& context,
                                           HitTargetAt&& hit_target_at, BgmEventAt&& bgm_event_at)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& history = context.environment.document.history;
    auto& state = context.state;
    auto& application_state = context.environment.commands;
    if (!semantic_layer_paint_armed(application_state))
        return handle_inactive_semantic_layer_paint(context);
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (chart == nullptr || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "The selected chart field is no longer available.");
    }
    const auto* layer = visible_semantic_layer_by_id(
        *chart, application_state.project.semantic_layer.semantic_layer_target.value_or(layer_id_t{}));
    if (!semantic_layer_paint_target_editable(application_state, layer)) {
        application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "Choose an unlocked visible semantic layer before painting chart membership.");
    }
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    const auto bgm_event = target && target->note ? std::optional<object_id_t>{}
                                                  : bgm_event_at(input.x_pixels, input.y_pixels);
    const semantic_layer_paint_target_t paint_target{
        target && target->note ? target->note : std::nullopt, bgm_event};
    if (paint_target.note || paint_target.bgm_event)
        return apply_semantic_layer_paint_target(input, context, paint_target, layer);
    return handle_empty_semantic_layer_paint_target(input, context);
}

template <typename HitTargetAt, typename PositionAtComposeY>
auto dispatch_note_move_gesture(const WormUiComposeInputAction& input,
                                compose_gesture_context_t& context, HitTargetAt&& hit_target_at,
                                PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, state, result, accepted] = context;
    auto& history = environment.document.history;
    auto& application_state = environment.commands;
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (chart == nullptr || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "The selected chart field is no longer available.");
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN) {
        const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
        if (!target || !target->note ||
            std::ranges::find(state.selected_notes, *target->note) == state.selected_notes.end())
            return compose_input_family_result_t::from(
                "Begin a note move by dragging a selected playable note.");
        state.active_lane = target->lane;
        place_cursor_on_picked_row(state, target->position);
        result = dispatch_editor_action(
            history, state,
            begin_note_move_gesture_action_t{
                *target->note, application_state.preferences.editor_preferences.move_collision_policy ==
                                   move_collision_policy_t::displace_to_bgm});
        accepted = "Selected-note move started.";
        return compose_input_family_result_t::continue_with_result();
    }
    const auto y =
        input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE
            ? input.y_pixels
            : std::clamp(input.y_pixels, 0.0, std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto position = position_at_compose_y(*chart, *view, y);
    const auto target = hit_target_at(*chart, *view, input.x_pixels, input.y_pixels);
    if (!position || (!target && !input.note_move_to_bgm)) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "Note move input fell outside chart lane geometry and was canceled.");
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE) {
        result = dispatch_editor_action(
            history, state,
            update_note_move_gesture_action_t{.reference_position = *position,
                .reference_lane = target ? target->lane : lane_id_t{}, .to_bgm = input.note_move_to_bgm,
                .preferred_bgm_lane = input.lane_id ? std::optional{object_id_t{input.lane_id}} : std::nullopt});
        accepted = "Selected-note move preview updated.";
    } else {
        result = dispatch_editor_action(
            history, state,
            update_note_move_gesture_action_t{
                .reference_position = *position,
                .reference_lane = target ? target->lane : lane_id_t{},
                .to_bgm = input.note_move_to_bgm,
                .preferred_bgm_lane =
                    input.lane_id ? std::optional{object_id_t{input.lane_id}} : std::nullopt,
                .operation = wormedit::proposal_operation_t::apply});
        accepted =
            result.document_changed ? "Selected notes moved." : "Selected-note move released.";
    }
    return compose_input_family_result_t::continue_with_result();
}

template <typename PositionAtComposeY>
auto dispatch_bar_line_move_gesture(const WormUiComposeInputAction& input,
                                    compose_gesture_context_t& context,
                                    PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    using namespace wormedit;
    auto& [environment, state, result, accepted] = context;
    auto& history = environment.document.history;
    const auto* chart = history.document.find_chart(state.chart);
    const auto view = compose_view_for_state(history, state);
    if (!chart || !view) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "The selected chart field is no longer available.");
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_BEGIN) {
        const auto reference = measure_id_t{input.object_id};
        if (!reference || std::ranges::find(state.selected_objects,
                editor_selection_object_t{editor_selection_object_kind_t::bar_line,
                                          object_id_t{reference.value}}) == state.selected_objects.end())
            return compose_input_family_result_t::from(
                "Begin a bar-line move by dragging a selected measure-lane handle.");
        result = dispatch_editor_action(
            history, state, begin_bar_line_move_gesture_action_t{reference});
        accepted = "Selected-object move started.";
        return compose_input_family_result_t::continue_with_result();
    }
    const auto y = input.kind == WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE
        ? input.y_pixels
        : std::clamp(input.y_pixels, 0.0, std::max(0.0, view->viewport_height_pixels - 0.001));
    const auto position = position_at_compose_y(*chart, *view, y);
    if (!position) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return compose_input_family_result_t::from(
            "Bar-line move input left chart geometry and was canceled.");
    }
    result = dispatch_editor_action(
        history, state,
        update_bar_line_move_gesture_action_t{
            .reference_position = *position,
            .operation = input.kind == WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE
                ? proposal_operation_t::apply : proposal_operation_t::revise});
    accepted = input.kind == WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE
        ? (result.document_changed ? "Selected objects moved." : "Selected-object move released.")
        : "Selected-object move preview updated.";
    return compose_input_family_result_t::continue_with_result();
}

template <typename HitTargetAt, typename BgmEventAt, typename PositionAtComposeY>
auto dispatch_compose_gesture_input(const WormUiComposeInputAction& input,
                                    const compose_input_inputs_t& environment,
                                    wormedit::editor_interaction_state_t& state,
                                    wormedit::editor_interaction_result_t& result,
                                    std::string_view& accepted, HitTargetAt&& hit_target_at,
                                    BgmEventAt&& bgm_event_at,
                                    PositionAtComposeY&& position_at_compose_y)
    -> compose_input_family_result_t {
    const auto& cache = environment.controls.cache;

    auto context = compose_gesture_context_t{environment, state, result, accepted};
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE:
    case WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE:
        return dispatch_alt_pointer_gesture(input, context, hit_target_at, position_at_compose_y,
                                            cache);
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE:
        return dispatch_semantic_layer_paint_gesture(input, context, hit_target_at, bgm_event_at);
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE:
        return dispatch_note_paint_gesture(input, context, hit_target_at, cache);
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE:
        return dispatch_note_move_gesture(input, context, hit_target_at, position_at_compose_y);
    case WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_BEGIN:
    case WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_UPDATE:
    case WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE:
        return dispatch_bar_line_move_gesture(input, context, position_at_compose_y);
    default:
        return {};
    }
}

template <typename SelectionMode, typename InputSourceY>
auto dispatch_compose_selection_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted, SelectionMode&& selection_mode, InputSourceY&& input_source_y,
    double field_left_pixels, double field_width_pixels,
    const std::shared_ptr<const wormedit::editor_area_geometry_snapshot_t>& geometry)
    -> compose_input_family_result_t {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN:
        if (!selection_mode(input.selection_mode).has_value()) {
            return compose_input_family_result_t::from(
                "Compose area selection used an unknown selection mode.");
        }
        result = dispatch_editor_action(
            history, state,
            begin_editor_area_selection_action_t{
                .x_pixels = input.x_pixels,
                .y_pixels = input_source_y(input.y_pixels),
                .field_left_pixels = field_left_pixels,
                .field_width_pixels = field_width_pixels,
                .mode = *selection_mode(input.selection_mode),
                // Bar lines have projected rectangles only while the measure lane shows them.
                .scope = geometry ? editor_selection_scope_t::every_object()
                                  : editor_selection_scope_t{},
                .canvas_pointer_capture = true,
                .object_geometry = geometry,
            });
        accepted = "Area selection started.";
        break;
    case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE:
        result = dispatch_editor_action(history, state,
                                        update_editor_area_selection_action_t{
                                            input.x_pixels, input_source_y(input.y_pixels), true});
        accepted = "Area selection preview updated.";
        break;
    case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE:
        result = dispatch_editor_action(history, state,
                                        update_editor_area_selection_action_t{
                                            .x_pixels = input.x_pixels,
                                            .y_pixels = input_source_y(input.y_pixels),
                                            .pointer_captured = true,
                                            .operation = wormedit::proposal_operation_t::apply});
        accepted = "Area selection committed.";
        break;
    case WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION:
    case WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION:
        if (!selection_mode(input.selection_mode).has_value()) {
            return compose_input_family_result_t::from(
                "Compose Shift range used an unknown selection mode.");
        }
        result = dispatch_editor_action(
            history, state,
            space_range_selection_action_t{
                .mode = *selection_mode(input.selection_mode),
                .minimum_snap_width = input.kind == WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION,
            });
        accepted = state.gesture<wormedit::pending_space_range_selection_t>() ? "Range selection started."
                                                       : "Range selection committed.";
        break;
    default:
        return {};
    }
    return compose_input_family_result_t::continue_with_result();
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto fit_visible_source_frame(const wormedit::timing_index_t& timing,
                              const wormedit::editor_interaction_state_t& state,
                              const wormedit::audio::waveform_t& waveform, bool end)
    -> std::optional<std::uint64_t>;
#endif

void capture_fit_onsets(compose_scene_cache_t& cache,
                        const wormedit::editor_interaction_state_t& state,
                        const wormedit::workspace_controller_t& workspace);

auto begin_compose_beat_fit(const WormUiComposeInputAction&, wormedit::document_history_t&,
                            wormedit::workspace_controller_t&,
                            wormedit::editor_interaction_state_t&, application_command_state_t&,
                            compose_scene_cache_t&, const wormedit::chart_document_t*)
    -> compose_input_family_result_t;

auto fit_onset_target(compose_scene_cache_t& cache, const WormUiComposeInputAction& input,
                      std::uint64_t frame) -> std::uint64_t;

auto revise_compose_beat_fit(const WormUiComposeInputAction&, wormedit::document_history_t&,
                             wormedit::workspace_controller_t&,
                             wormedit::editor_interaction_state_t&, application_command_state_t&,
                             compose_scene_cache_t&) -> compose_input_family_result_t;

auto dispatch_compose_beat_fit_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    const wormedit::chart_document_t* input_chart) -> std::optional<compose_input_family_result_t>;

auto dispatch_compose_offset_fit_input(const WormUiComposeInputAction& input,
                                       compose_input_context_t& context)
    -> std::optional<compose_input_family_result_t>;

auto previous_beat_fit_anchor(const wormedit::chart_document_t& chart,
                              const wormedit::measure_index_t& measures, std::uint64_t beat_row)
    -> std::optional<wormedit::local_position_t>;

auto begin_compose_beat_fit(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    const wormedit::chart_document_t* input_chart) -> compose_input_family_result_t;

auto revise_beat_fit_proposal(const WormUiComposeInputAction& input,
                              const wormedit::document_history_t& history,
                              const wormedit::proposal_t<wormedit::beat_interval_drag_t>& drag,
                              std::uint64_t target_frame, bool timing_locked)
    -> wormedit::beat_interval_drag_outcome_t;

auto revise_compose_beat_fit(const WormUiComposeInputAction& input,
                             wormedit::document_history_t& history,
                             wormedit::workspace_controller_t& workspace,
                             wormedit::editor_interaction_state_t& state,
                             application_command_state_t& application_state,
                             compose_scene_cache_t& scene_cache) -> compose_input_family_result_t;

auto seek_compose_waveform(const WormUiComposeInputAction& input, compose_input_context_t& context,
                           wormedit::editor_interaction_result_t& result,
                           std::string_view& accepted)
    -> std::optional<compose_input_family_result_t>;

auto click_compose_waveform(const WormUiComposeInputAction& input, compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted)
    -> std::optional<compose_input_family_result_t>;

auto focus_compose_note_label(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string>;

auto select_compose_bgm_event(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string>;

auto select_compose_timing_marker(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> std::optional<std::string>;

auto timing_pointer_row(compose_input_context_t& context, const wormedit::chart_document_t& chart,
                        const wormedit::editor_interaction_view_model_t& view,
                        const wormedit::measure_index_t& index, double y_pixels)
    -> std::optional<std::uint64_t>;

auto move_compose_timing_selection(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> std::optional<std::string>;

auto kiai_manipulated_rows(const wormedit::kiai_span_t* span, std::uint8_t edge,
                           std::uint64_t anchor_row, std::uint64_t pointer_row,
                           std::uint64_t total_rows)
    -> std::optional<std::pair<std::uint64_t, std::uint64_t>>;

auto edit_compose_kiai_span(const WormUiComposeInputAction& input, compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> std::optional<std::string>;

auto speed_transition_between(const wormedit::chart_document_t& chart,
                              const wormedit::timing_index_t* timing,
                              const wormedit::speed_event_t& speed, std::uint64_t begin_row,
                              std::uint64_t end_row) -> std::optional<wormedit::rational_t>;

auto edit_compose_speed_span(const WormUiComposeInputAction& input,
                             compose_input_context_t& context,
                             wormedit::editor_interaction_result_t& result,
                             std::string_view& accepted) -> std::optional<std::string>;

auto find_input_recipe(const wormedit::chart_document_t& chart, std::uint64_t id)
    -> const wormedit::sv_recipe_t*;

auto edit_compose_recipe_range(const WormUiComposeInputAction& input,
                               compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> std::optional<std::string>;

auto run_compose_recipe_command(std::uint64_t id, std::uint8_t command,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted) -> std::optional<std::string>;

auto place_note_from_pointer(const WormUiComposeInputAction& input,
                             compose_input_context_t& context, const auto& target,
                             wormedit::editor_interaction_result_t& result,
                             std::string_view& accepted) -> void {
    using namespace wormedit;
    // A pending hold keeps the sound captured at its first click; the end click only commits.
    const auto sound =
        context.state.hold() ? context.state.hold()->sound : note_paint_sound(input);
    result = dispatch_editor_action(
        context.environment.document.history, context.state,
        context.state.hold()
            ? editor_interaction_action_t{commit_hold_action_t{
                  target.position,
                  compose_stem_geometry(context.environment.document.history, context.state,
                                        context.environment.controls.cache, sound)}}
            : editor_interaction_action_t{place_tap_action_t{
                  target.position, sound,
                  compose_stem_geometry(context.environment.document.history, context.state,
                                        context.environment.controls.cache, sound)}});
    advance_sound_after_paint_commit(result, sound, result.created_note.has_value());
    audition_created_note(
        context.environment.commands.preferences.editor_preferences.audition_on_click,
        context.environment.document.history, context.state, result);
    accepted =
        context.state.hold() ? "Hold gesture started; click its end row." : "Note placed.";
}

auto place_virtual_note_from_pointer(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context, std::uint64_t row,
                                     wormedit::lane_id_t lane,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> void;

auto finish_virtual_hold_at_authored_row(const WormUiComposeInputAction& input,
                                         compose_input_context_t& context,
                                         const wormedit::chart_document_t& chart,
                                         wormedit::local_position_t position,
                                         wormedit::lane_id_t lane,
                                         wormedit::editor_interaction_result_t& result,
                                         std::string_view& accepted) -> bool;

auto pointer_click_places_notes(const compose_input_context_t& context) -> bool;

auto select_or_place_virtual_pointer(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context,
                                     const wormedit::chart_document_t& chart,
                                     const wormedit::editor_interaction_view_model_t& view,
                                     wormedit::editor_area_selection_mode_t mode,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> std::optional<std::string>;

auto select_compose_same_sound(const WormUiComposeInputAction& input,
                               compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> std::optional<std::string>;

auto select_compose_pointer_target(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> std::optional<std::string>;

auto select_compose_bga_event(const WormUiComposeInputAction& input,
                              compose_input_context_t& context,
                              wormedit::editor_interaction_result_t& result,
                              std::string_view& accepted) -> std::optional<std::string>;

auto move_compose_bga_event(const WormUiComposeInputAction& input, compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> std::optional<std::string>;

auto move_compose_bookmark(const WormUiComposeInputAction& input, compose_input_context_t& context,
                           wormedit::editor_interaction_result_t& result,
                           std::string_view& accepted) -> std::optional<std::string>;

auto select_compose_bms_measure(const WormUiComposeInputAction& input,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted) -> std::optional<std::string>;

struct resource_pointer_spec_t {
    wormedit::resource_id_t resource;
    wormedit::resource_placement_kind_t kind;
    wormedit::resource_role_t required_role;
    bool replacing;
};

auto resource_pointer_spec(const WormUiComposeInputAction& input, compose_input_context_t& context)
    -> std::variant<resource_pointer_spec_t, std::string>;

struct resource_pointer_target_t {
    std::optional<wormedit::local_position_t> position;
    wormedit::lane_id_t lane{};
    std::optional<wormedit::object_id_t> note;
    bool handled_virtual{};
};

auto resource_pointer_bga_kind(const WormUiComposeInputAction& input,
                               wormedit::resource_placement_kind_t kind)
    -> std::optional<wormedit::visual_event_kind_t>;

void advance_dock_after_placement(const wormedit::song_document_t& document,
                                  wormedit::resource_id_t placed,
                                  wormedit::resource_role_t required_role);

auto place_virtual_sample_resource(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view,
                                   const resource_pointer_spec_t& spec,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> bool;

auto place_virtual_source_resource(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view,
                                   const resource_pointer_spec_t& spec,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> bool;

auto resolve_resource_pointer_target(
    const WormUiComposeInputAction& input, compose_input_context_t& context,
    const wormedit::chart_document_t& chart, const wormedit::editor_interaction_view_model_t& view,
    const resource_pointer_spec_t& spec, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted) -> std::variant<resource_pointer_target_t, std::string>;

auto replacement_target_for_resource(const WormUiComposeInputAction& input,
                                     const wormedit::chart_document_t& chart,
                                     const resource_pointer_spec_t& spec,
                                     wormedit::local_position_t position,
                                     const resource_pointer_target_t& pointer)
    -> std::variant<wormedit::editor_selection_object_t, std::string>;

auto dispatch_resource_pointer(const WormUiComposeInputAction& input,
                               compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> compose_input_family_result_t;

auto dispatch_compose_undo_redo(const WormUiComposeInputAction& input,
                                compose_input_context_t& context) -> compose_input_family_result_t;

auto retire_interrupted_compose_input(wormedit::document_history_t& history,
                                      wormedit::editor_interaction_state_t& state,
                                      wormedit::viewport_input_controller_t& viewport,
                                      wormedit::editor_navigation_input_controller_t& navigation,
                                      application_command_state_t& application,
                                      compose_scene_cache_t& cache)
    -> wormedit::editor_interaction_result_t;

auto cancel_compose_gesture(compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> compose_input_family_result_t;

auto compose_input_rejection(std::string message,
                             const wormedit::editor_interaction_result_t& result)
    -> compose_input_family_result_t;

auto reveal_compose_bga_lane_input(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> compose_input_family_result_t;

auto dispatch_timing_lane_area_input(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted)
    -> std::optional<compose_input_family_result_t>;

auto dispatch_compose_bga_input(const WormUiComposeInputAction& input,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted)
    -> std::optional<compose_input_family_result_t>;

auto dispatch_unhandled_compose_input(const WormUiComposeInputAction& input,
                                      compose_input_context_t& context,
                                      wormedit::editor_interaction_result_t& result,
                                      std::string_view& accepted) -> compose_input_family_result_t;

auto compose_input_edits_notes(WormUiComposeInputKind kind) -> bool;

auto compose_input_notification_source(WormUiComposeInputKind kind)
    -> wormedit::notification_source_t;

auto input_result_severity(const WormUiComposeInputAction& input,
                           const wormedit::editor_interaction_result_t& result)
    -> wormedit::notification_severity_t;

auto dispatch_note_to_bgm_input(const WormUiComposeInputAction& input,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted)
    -> std::optional<compose_input_family_result_t>;

auto lane_key_places_inside_warp(const WormUiComposeInputAction& input,
                                 const compose_input_context_t& context) -> bool;

auto dispatch_compose_input_families(const WormUiComposeInputAction& input,
                                     compose_input_context_t& context,
                                     wormedit::editor_interaction_result_t& result,
                                     std::string_view& accepted) -> compose_input_family_result_t;

auto dispatch_compose_paste_input(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context)
    -> std::optional<compose_input_family_result_t>;

auto bga_draw_row(const WormUiComposeInputAction& input, const compose_input_context_t& context,
                  const wormedit::chart_document_t& chart, const wormedit::measure_index_t& index)
    -> std::optional<uint64_t>;

auto begin_bga_draw(const WormUiComposeInputAction& input, compose_input_context_t& context,
                    const wormedit::chart_document_t& chart) -> bool;

auto apply_bgm_draw_stroke(wormedit::document_command_transaction_t& transaction,
                           const application_command_state_t::bga_draw_stroke_proposal_t& stroke,
                           uint64_t lane) -> wormedit::document_command_result_t;

auto apply_bga_draw(const WormUiComposeInputAction& input, compose_input_context_t& context,
                     wormedit::editor_interaction_result_t& result,
                     wormedit::document_command_transaction_t& transaction) -> void;


auto bga_draw_stop(std::string_view text) -> compose_input_family_result_t;

auto dispatch_bga_draw_input(const WormUiComposeInputAction& input,
                             compose_input_context_t& context,
                             wormedit::editor_interaction_result_t& result,
                             std::string_view& accepted)
    -> std::optional<compose_input_family_result_t>;

auto select_lane_key_without_selection(const WormUiComposeInputAction& input,
                                       const compose_input_context_t& context) -> bool;

auto dispatch_compose_input_event(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> compose_input_family_result_t;

auto pointer_placement_keeps_cursor(const WormUiComposeInputAction& input,
                                    const wormedit::editor_interaction_result_t& result) -> bool;

void restore_pointer_placement_cursor(compose_input_context_t& context,
                                      const wormedit::cursor_pose_t& cursor,
                                      wormedit::editor_interaction_result_t& result);

struct compose_input_step_t {
    bool handled{};
    bool stop{};
    std::string status;
};

auto process_compose_input_result(const WormUiComposeInputAction& input,
                                  compose_input_context_t& context,
                                  compose_input_family_result_t&& dispatched,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view accepted) -> compose_input_step_t;

auto dispatch_compose_input_with_travel(const WormUiComposeInputAction& input,
                                        compose_input_context_t& context,
                                        wormedit::editor_interaction_result_t& result,
                                        std::string_view& accepted)
    -> compose_input_family_result_t;

void finish_interrupted_compose_travel(const WormUiComposeInputAction& input,
                                       compose_input_context_t& context);

auto dispatch_compose_input_batch(compose_input_context_t& context) -> std::string;

auto compose_bgm_scroll_content_width(const compose_input_context_t& context) -> double;

void apply_compose_viewport_scroll(const wormedit::viewport_scroll_action_t& scroll,
                                   compose_input_context_t& context, std::string& status);

void apply_compose_viewport_actions(const wormedit::viewport_action_batch_t& actions,
                                    compose_input_context_t& context, std::string& status);

void apply_compose_pinch_zoom(const WormUiComposeViewportInput& input,
                              compose_input_context_t& context, std::string& status);

auto dispatch_compose_viewport_batch(compose_input_context_t& context,
                                     double viewport_delta_seconds, std::string status)
    -> std::string;

void reset_compose_autoscroll_if_idle(compose_input_context_t& context);

} // namespace wormedit::sys::compose_input_detail

namespace wormedit::sys {
auto consume_minimap_preferences(WormUiWorkspaceAction& action,
                                 application_command_state_t& application) -> bool;

auto dispatch_compose_input_action_impl(WormUiWorkspaceAction& ui_action,
                                        const compose_input_inputs_t& environment,
                                        wormedit::editor_interaction_state_t& state,
                                        double viewport_delta_seconds,
                                        const wormedit::notification_sink_t* notifications)
    -> std::string;

} // namespace wormedit::sys

namespace wormedit::sys::compose_input_detail {
auto dispatch_bgm_mix_toggle(const WormUiComposeInputAction& input,
                             const WormUiWorkspaceAction& action,
                             const wormedit::document_history_t& history,
                             const wormedit::workspace_controller_t& workspace,
                             application_command_state_t& application, bool& accepted)
    -> std::string;

} // namespace wormedit::sys::compose_input_detail
