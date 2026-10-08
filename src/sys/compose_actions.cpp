#include "sys/compose_actions.h"
#include "sys/compose_bookmark_commands.h"
#include "sys/bookmark_rename_commands.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/recipe_form.h"
#include <wormedit/core/sv_recipe.h>
#include "sys/compose_label_search.h"
#include "sys/compose_sound_actions.h"
#include "sys/compose_bga_actions.h"
#include "sys/compose_measure_commands.h"
#include "sys/compose_selection_transform.h"
#include "sys/compose_area_actions.h"
#include "sys/application_edit_helpers.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/transport_player_actions.h"
#include "ui/frontend.h"
#include "wormedit/audio.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bgm_columns.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_layout_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_decoder.h>
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#include <wormedit/visual/video_job.h>
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {

namespace {

struct compose_action_context_t {
    const WormUiWorkspaceAction& action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    wormedit::editor_interaction_state_t& state;
    double viewport_height;
    application_command_state_t& application_state;
    const std::filesystem::path& asset_base;
    wormedit::task_scheduler_t& resource_copy_scheduler;
    runtime::resource_copy_t& resource_copy_runtime;
    runtime::output_group_export_t& export_runtime;
    bool metadata_import_busy;
    wormedit::editor_field_scroll_direction_t scroll_direction;
    std::optional<notification_sink_t> notifications;
};

struct compose_feedback_t {
    std::string message;
    notification_severity_t severity;
    notification_source_t source;
};

auto compose_notification_severity(diagnostic_severity_t severity) -> notification_severity_t {
    switch (severity) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

void post_typed_compose_feedback(const notification_sink_t& notifications,
    notification_severity_t severity, notification_source_t source, std::string_view message) {
    if (severity == notification_severity_t::hint) notifications.post_transient_hint(source, message);
    else notifications.post(severity, source, message);
}

auto publish_compose_feedback(compose_action_context_t& context, std::string message,
                              notification_severity_t severity, notification_source_t source)
    -> std::string {
    if (context.notifications && !message.empty()) {
        if (severity == notification_severity_t::hint) context.notifications->post_transient_hint(source, message);
        else context.notifications->post(severity, source, message);
    }
    return message;
}

auto publish_compose_diagnostic(compose_action_context_t& context,
                                const diagnostic_t& diagnostic,
                                notification_source_t source = notification_source_t::edit)
    -> std::string {
    return publish_compose_feedback(context, diagnostic.message,
                                    compose_notification_severity(diagnostic.severity), source);
}

auto compose_action_success(compose_action_context_t& context, std::string message,
                            notification_source_t source = notification_source_t::edit)
    -> std::string {
    return publish_compose_feedback(context, std::move(message), notification_severity_t::success,
                                    source);
}

auto compose_action_info(compose_action_context_t& context, std::string message,
                         notification_source_t source = notification_source_t::input)
    -> std::string {
    return publish_compose_feedback(context, std::move(message), notification_severity_t::info,
                                    source);
}

auto compose_action_hint(compose_action_context_t& context, std::string message,
                         notification_source_t source = notification_source_t::input)
    -> std::string {
    return publish_compose_feedback(context, std::move(message), notification_severity_t::hint,
                                    source);
}

auto compose_action_warning(compose_action_context_t& context, std::string message,
                            notification_source_t source = notification_source_t::edit)
    -> std::string {
    return publish_compose_feedback(context, std::move(message), notification_severity_t::warning,
                                    source);
}

auto compose_action_error(compose_action_context_t& context, std::string message,
                          notification_source_t source = notification_source_t::document)
    -> std::string {
    return publish_compose_feedback(context, std::move(message), notification_severity_t::error,
                                    source);
}

auto compose_mutation_rebind_failure(
    compose_action_context_t& context, const workspace_action_result_t& rebound,
    std::string success, std::string failure,
    notification_source_t source = notification_source_t::edit,
    notification_severity_t severity = notification_severity_t::success) -> std::string {
    static_cast<void>(publish_compose_feedback(context, std::move(success), severity, source));
    if (!rebound.diagnostics.empty())
        return publish_compose_diagnostic(context, rebound.diagnostics.front(), notification_source_t::document);
    return compose_action_error(context, std::move(failure), notification_source_t::document);
}

auto compose_interaction_feedback(compose_action_context_t& context,
                                  const editor_interaction_result_t& result,
                                  std::string_view accepted,
                                  notification_source_t source = notification_source_t::input)
    -> std::string {
    if (result.accepted) {
        const auto status = compose_result_status(result, accepted);
        return result.document_changed ? compose_action_success(context, status, notification_source_t::edit)
                                       : compose_action_hint(context, status, source);
    }
    if (!result.diagnostics.empty())
        return publish_compose_diagnostic(context, result.diagnostics.front(), source);
    return compose_action_warning(context, compose_result_status(result, accepted), source);
}

auto compose_bookmark_navigation_feedback(compose_action_context_t& context,
                                          const bookmark_command_outcome_t& outcome) -> std::string {
    if (outcome.feedback_posted) return outcome.status;
    if (!outcome.diagnostics.empty())
        return publish_compose_feedback(context, outcome.status,
            compose_notification_severity(outcome.diagnostics.front().severity), notification_source_t::input);
    return outcome.accepted ? compose_action_hint(context, outcome.status)
                            : compose_action_warning(context, outcome.status, notification_source_t::input);
}

auto dispatch_compose_bookmark_action(compose_action_context_t& context) -> std::optional<std::string> {
    const auto& action = context.action;
    const bool rename = action.compose_action == WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK;
    if (!rename && action.compose_action != WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK) return std::nullopt;
    if (action.compose_input_project_id != context.history.document.id.value ||
        action.compose_input_chart_id != context.state.chart.value ||
        action.compose_input_document_revision != context.history.document.revision)
        return compose_action_warning(context,
                                      "Bookmark action is stale; reopen it in the active chart.");
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture first.");
    if (!rename) {
        const auto outcome = navigate_to_bookmark(
            context.history, context.state, context.application_state,
            action.compose_input_document_revision, context.state.chart,
            wormedit::object_id_t{action.compose_bookmark_id});
        return compose_bookmark_navigation_feedback(context, outcome);
    }
    const auto label_end = std::find(std::begin(action.compose_marker_label),
                                    std::end(action.compose_marker_label), '\0');
    if (label_end == std::end(action.compose_marker_label))
        return compose_action_warning(context, "Bookmark name is too long.");
    const auto outcome = rename_bookmark(context.history, action.compose_input_document_revision,
        context.state.chart, wormedit::object_id_t{action.compose_bookmark_id},
        std::string_view(action.compose_marker_label, label_end - action.compose_marker_label));
    if (outcome.document_changed) {
        const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
        if (!rebound)
            return compose_mutation_rebind_failure(context, rebound, outcome.status,
                "Bookmark renamed, but workspace rebinding failed.");
    }
    return publish_compose_feedback(context, outcome.status, outcome.severity, notification_source_t::edit);
}

std::optional<std::string> dispatch_compose_measure_navigation(compose_action_context_t& context) {
    using namespace wormedit;
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context,
            "Finish or cancel the active Compose gesture before going to a measure.");
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end())
        return compose_action_warning(context, "The selected difficulty is no longer available.");
    const auto number = context.action.compose_measure_number;
    editor_interaction_result_t result;
    if (chart_has_virtual_tail(*chart)) {
        // BMS measures are numbered from #000; other charts count from 1.
        const auto is_bms = chart->source_family == format_family_t::bms;
        if (!is_bms && number == 0)
            return compose_action_warning(context, "The requested measure is outside this chart.");
        const auto canvas_number = is_bms ? number : number - 1;
        if (canvas_number >= virtual_tail_measure_limit(*chart))
            return compose_action_warning(context, is_bms
                ? "The requested BMS measure is outside #000-#999."
                : "The requested measure is outside this chart.");
        // Reuse virtual-canvas navigation without changing the current selection.
        auto candidate = context.state;
        result = dispatch_editor_action(context.history, candidate,
                                         select_bms_canvas_measure_action_t{
                                             static_cast<std::size_t>(canvas_number)});
        if (result.accepted) {
            restore_cursor_pose(context.state, capture_cursor_pose(candidate));
            context.state.active_lane = candidate.active_lane;
        }
    } else {
        if (number == 0 || number > chart->measures.size())
            return compose_action_warning(context, "The requested measure is outside this chart.");
        result = dispatch_editor_action(context.history, context.state,
            jump_edit_cursor_action_t{{chart->measures[static_cast<std::size_t>(number - 1)].id, 0},
                                     context.history.document.revision});
    }
    return compose_interaction_feedback(context, result, "Compose cursor moved to the requested measure.");
}

std::optional<std::string> dispatch_compose_bgm_column_count(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context,
            "Finish or cancel the active Compose gesture before changing BGM columns.");
    const auto applied = wormedit::apply_bgm_column_count(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        context.action.compose_bgm_column_count);
    if (!applied.applied)
        return applied.diagnostics.empty()
            ? std::optional<std::string>{compose_action_info(context, "BGM column count unchanged.", notification_source_t::edit)}
            : std::optional<std::string>{publish_compose_diagnostic(context, applied.diagnostics.front())};
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "BGM column count set to " + std::to_string(context.action.compose_bgm_column_count) + ".", "BGM columns changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "BGM column count set to " +
        std::to_string(context.action.compose_bgm_column_count) + ".");
}

std::optional<std::string> dispatch_compose_lane_profile(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context,
            "Finish or cancel the active Compose gesture before changing the lane profile.");
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context,
        "The selected difficulty is no longer available.");
    const auto* segment = wormedit::active_lane_layout_segment(*chart, context.state.cursor);
    const auto choices = segment
        ? wormedit::lane_profile_choices(segment->layout.lanes.size())
        : wormedit::lane_profile_choices();
    if (context.action.compose_lane_profile_index >= choices.size())
        return compose_action_warning(context, "Choose a lane profile first.");
    const auto& target = choices[context.action.compose_lane_profile_index].layout;
    const auto position = segment ? context.state.cursor
        : wormedit::local_position_t{chart->measures.empty()
              ? context.state.cursor.measure : chart->measures.front().id, 0};
    const auto applied = wormedit::apply_lane_profile_switch(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        position, target);
    if (!applied.applied)
        return applied.diagnostics.empty()
            ? std::optional<std::string>{compose_action_info(context, "Lane profile unchanged.", notification_source_t::edit)}
            : std::optional<std::string>{publish_compose_diagnostic(context, applied.diagnostics.front())};
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "Lane profile set to " + target.display_name + ".", "Lane profile changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "Lane profile set to " + target.display_name + ".");
}

auto layout_profile_from_id(const wormedit::chart_document_t& chart,
                            wormedit::local_position_t position,
                            std::string_view profile_id)
    -> std::optional<wormedit::lane_layout_t> {
    for (const auto& profile : wormedit::built_in_bms_lane_profiles())
        if (profile.profile.layout.profile_id == profile_id) return profile.profile.layout;
    constexpr std::array<std::string_view, 3> step_types{"pump-single", "pump-double", "dance-solo"};
    for (const auto type : step_types) {
        auto profile = wormedit::make_stepmania_profile_for(type);
        if (profile && profile->layout.profile_id == profile_id) return profile->layout;
    }
    for (std::uint32_t keys = 1; keys <= 18; ++keys) {
        auto profile = wormedit::make_osu_mania_profile(keys);
        if (profile && profile->layout.profile_id == profile_id) return profile->layout;
    }
    if (profile_id == "bmson-generic-24keys") return wormedit::make_bmson_generic_24k_profile().layout;
    if (profile_id == "bmson-generic-48keys") return wormedit::make_bmson_generic_48k_profile().layout;
    if (const auto* active = wormedit::active_lane_layout_segment(chart, position);
        active && active->layout.profile_id == profile_id)
        return active->layout;
    const auto current = std::ranges::find_if(chart.lane_layouts, [&](const auto& segment) {
        return segment.layout.profile_id == profile_id;
    });
    if (current != chart.lane_layouts.end()) return current->layout;
    return std::nullopt;
}

auto layout_with_preserved_lane_ids(const wormedit::chart_document_t& chart,
                                    const wormedit::lane_layout_segment_t* reference,
                                    wormedit::lane_layout_t target)
    -> std::optional<wormedit::lane_layout_t> {
    if (target.lanes.empty()) return std::nullopt;
    std::vector<const wormedit::lane_definition_t*> old_lanes;
    if (reference) {
        old_lanes.reserve(reference->layout.lanes.size());
        for (const auto& lane : reference->layout.lanes) old_lanes.push_back(&lane);
        std::ranges::stable_sort(old_lanes, {}, [](const auto* lane) { return lane->display_order; });
    }
    std::ranges::stable_sort(target.lanes, {}, &wormedit::lane_definition_t::display_order);
    auto largest_id = std::uint64_t{};
    for (const auto& segment : chart.lane_layouts)
        for (const auto& lane : segment.layout.lanes)
            largest_id = std::max(largest_id, lane.id.value);
    for (std::size_t index = 0; index < target.lanes.size(); ++index) {
        if (index < old_lanes.size()) {
            target.lanes[index].id = old_lanes[index]->id;
            continue;
        }
        if (largest_id == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        target.lanes[index].id = wormedit::lane_id_t{++largest_id};
    }
    return target;
}

// Key bindings for a built-in BMS layout whose lane IDs were just assigned, for a chart that has
// none; the built-in lanes and the layout's lanes pair up by display order.
auto initial_input_profile(const wormedit::chart_document_t& chart, const wormedit::lane_layout_t& layout)
    -> std::optional<wormedit::input_profile_t> {
    if (!chart.input_profile.bindings.empty()) return std::nullopt;
    for (const auto& builtin : wormedit::built_in_bms_lane_profiles()) {
        if (builtin.profile.layout.profile_id != layout.profile_id) continue;
        auto source = builtin.profile.layout.lanes;
        auto target = layout.lanes;
        const auto by_order = [](const auto& left, const auto& right) { return left.display_order < right.display_order; };
        std::ranges::stable_sort(source, by_order);
        std::ranges::stable_sort(target, by_order);
        if (source.size() != target.size()) return std::nullopt;
        auto input = builtin.profile.input;
        for (auto& binding : input.bindings) {
            const auto found = std::ranges::find(source, binding.lane, &wormedit::lane_definition_t::id);
            if (found == source.end()) return std::nullopt;
            binding.lane = target[static_cast<std::size_t>(found - source.begin())].id;
        }
        return input;
    }
    return std::nullopt;
}

// A measure that so far exists only on the virtual canvas (an empty BMS, say) is created first.
// Returns the notification when that fails; `chart` is refreshed after the history changed.
std::optional<std::string> materialize_layout_measure(compose_action_context_t& context,
                                                      const wormedit::chart_document_t*& chart,
                                                      bool& materialized) {
    if (context.action.compose_measure_number <= chart->measures.size()) return std::nullopt;
    const auto created = wormedit::apply_measure_edit(
        context.history, context.action.compose_input_document_revision,
        wormedit::materialize_measures_action_t{
            chart->id, static_cast<std::size_t>(context.action.compose_measure_number)});
    if (!created.applied)
        return created.diagnostics.empty()
            ? std::optional<std::string>{compose_action_warning(context, "That measure is outside the chart.")}
            : std::optional<std::string>{publish_compose_diagnostic(context, created.diagnostics.front())};
    materialized = true;
    chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context, "The selected difficulty is no longer available.");
    return std::nullopt;
}

std::optional<std::string> dispatch_compose_lane_layout_profile(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context,
            "Finish or cancel the active Compose gesture before editing a lane layout.");
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return compose_action_warning(context, "Lane layout change ignored because the document changed.");
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart || chart->id.value != context.action.compose_input_chart_id ||
        context.action.compose_measure_number == 0)
        return compose_action_warning(context,
            "Lane layout change ignored because the selected difficulty changed.");
    auto materialized = false;
    if (auto refusal = materialize_layout_measure(context, chart, materialized)) return refusal;
    const auto measure_index = static_cast<std::size_t>(context.action.compose_measure_number - 1);
    const auto target_position = wormedit::local_position_t{chart->measures[measure_index].id, 0};
    const auto target = layout_profile_from_id(
        *chart, target_position, context.action.compose_lane_layout_profile_id);
    if (!target) return compose_action_warning(context, "Choose a lane layout profile first.");
    const auto exact_segment = std::ranges::find_if(chart->lane_layouts, [&](const auto& segment) {
        return segment.position.measure == target_position.measure && segment.position.row == 0;
    });
    const auto is_update = exact_segment != chart->lane_layouts.end();
    const auto* active = is_update
                             ? &*exact_segment
                             : wormedit::active_lane_layout_segment(*chart, target_position);
    if (!active && !chart->lane_layouts.empty()) active = &chart->lane_layouts.front();
    const auto layout = layout_with_preserved_lane_ids(*chart, active, *target);
    if (!layout) return compose_action_warning(context,
        "Lane layout change could not preserve lane identities.");
    wormedit::lane_layout_action_t edit = wormedit::insert_lane_layout_action_t{
        chart->id, target_position, *layout, initial_input_profile(*chart, *layout)};
    if (is_update)
        edit = wormedit::update_lane_layout_action_t{chart->id, exact_segment->id, *layout};
    const auto applied = wormedit::apply_lane_layout_edit(
        context.history, context.history.document.revision, edit);
    if (!applied.applied && materialized) static_cast<void>(wormedit::undo_document_command(context.history));
    if (!applied.applied)
        return applied.diagnostics.empty()
            ? std::optional<std::string>{compose_action_warning(context, "Lane layout change was rejected.")}
            : std::optional<std::string>{publish_compose_diagnostic(context, applied.diagnostics.front())};
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        is_update ? "Lane layout updated." : "Lane layout inserted.", "Lane layout changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, is_update ? "Lane layout updated." : "Lane layout inserted.");
}

std::optional<std::string> dispatch_compose_audio_offset(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context,
            "Finish or cancel the active Compose gesture before editing audio offset.");
    const auto applied = wormedit::apply_audio_offset_edit(context.history,
        context.action.compose_input_document_revision, context.state.chart,
        {context.action.compose_audio_offset_numerator, context.action.compose_audio_offset_denominator},
        context.application_state.audio_offset_edits_locked());
    if (!applied.applied)
        return applied.diagnostics.empty()
            ? std::optional<std::string>{compose_action_info(context, "Audio offset unchanged.", notification_source_t::edit)}
            : std::optional<std::string>{publish_compose_diagnostic(context, applied.diagnostics.front(), notification_source_t::edit)};
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "Audio offset changed.", "Audio offset changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "Audio offset changed.", notification_source_t::edit);
}

std::string select_compose_tool(compose_action_context_t& context) {
    using namespace wormedit;
    const auto& action = context.action;
    auto& application_state = context.application_state;
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_SELECT_TOOL &&
        !wormedit::ui::compose_notes_editable(compose_mode_of(application_state)))
        return compose_action_warning(context, "Draw and Layer are unavailable in Timing focus.");
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_LAYER_TOOL &&
        !compose_layer_brush_target_editable(context.history.document, context.state.chart,
                                            application_state.semantic_layer_target))
        return compose_action_warning(context, "Pick an unlocked visible layer in the Layers dock.");
    (void)dispatch_editor_action(context.history, context.state, cancel_editor_gesture_action_t{});
    application_state.bga_draw_stroke.reset();
    application_state.note_paint_mode_active = action.compose_action == WORM_UI_COMPOSE_ACTION_DRAW_TOOL;
    application_state.semantic_layer_paint_mode_active = action.compose_action == WORM_UI_COMPOSE_ACTION_LAYER_TOOL;
    return compose_action_info(context, "Compose tool selected.");
}

std::optional<std::string> dispatch_compose_view_action_group(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& state = context.state;
    auto& viewport_height = context.viewport_height;
    auto& application_state = context.application_state;
    auto& scroll_direction = context.scroll_direction;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ZOOM_IN:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.25, .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed in.";
        break;
    case WORM_UI_COMPOSE_ACTION_ZOOM_OUT:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.0 / 1.25,
                                        .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed out.";
        break;
    case WORM_UI_COMPOSE_ACTION_SCROLL_UP:
    case WORM_UI_COMPOSE_ACTION_SCROLL_DOWN: {
        const auto direction =
            action.compose_action == WORM_UI_COMPOSE_ACTION_SCROLL_UP ? -1.0 : 1.0;
        result = dispatch_editor_action(
            history, state,
            scroll_editor_view_action_t{direction * std::max(64.0, viewport_height * 0.75),
                                        compose_cursor_screen_anchor(
                                            viewport_height,
                                            scroll_direction == editor_field_scroll_direction_t::down),
                                        scroll_direction});
        status = direction < 0.0 ? "Compose field scrolled up." : "Compose field scrolled down.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_RESET_VIEW: {
        const auto factor = default_pixels_per_standard_measure / state.pixels_per_standard_measure;
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = factor, .anchor_screen_y_pixels = std::nullopt});
        if (result) {
            result = dispatch_editor_action(
                history, state, scroll_editor_view_action_t{-state.viewport_scroll_pixels});
        }
        status = "Compose field view reset.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_SELECT_TOOL:
    case WORM_UI_COMPOSE_ACTION_DRAW_TOOL:
    case WORM_UI_COMPOSE_ACTION_LAYER_TOOL:
        return select_compose_tool(context);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE:
        if (!wormedit::ui::compose_notes_editable(compose_mode_of(application_state)))
            return compose_action_warning(context, "Note painting is unavailable in Timing focus.");
        if (editor_interaction_has_pending_gesture(state)) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            if (!result.accepted) {
                return compose_interaction_feedback(context, result, "Compose gesture canceled.");
            }
        }
        application_state.note_paint_mode_active = !application_state.note_paint_mode_active;
        if (application_state.note_paint_mode_active) {
            application_state.semantic_layer_paint_mode_active = false;
        }
        status = application_state.note_paint_mode_active ? "Note-paint mode enabled."
                                                          : "Note-paint mode disabled.";
        return compose_action_info(context, status);
    case WORM_UI_COMPOSE_ACTION_RATE_SLOWER:
    case WORM_UI_COMPOSE_ACTION_RATE_FASTER: {
        const auto stepped = step_transport_rate_status(
            application_state, action.compose_action == WORM_UI_COMPOSE_ACTION_RATE_FASTER);
        return stepped ? compose_action_info(context, *stepped, notification_source_t::audio)
                       : compose_action_warning(context, "Playback output rejected the rate change.",
                                                notification_source_t::audio);
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS: {
        if (editor_interaction_has_pending_gesture(state)) {
            return compose_action_warning(context,
                "Finish or cancel the active Compose gesture before changing the Compose mode.");
        }
        application_state.compose_timing_focus = !application_state.compose_timing_focus;
        if (application_state.compose_timing_focus) {
            // Timing focus never edits notes, so leave any note tool.
            application_state.note_paint_mode_active = false;
            application_state.semantic_layer_paint_mode_active = false;
        }
        return compose_action_info(context, application_state.compose_timing_focus
            ? "Timing focus on." : "Timing focus off.");
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_EDIT_SNAP: {
        if (editor_interaction_has_pending_gesture(state)) {
            return compose_action_warning(context,
                "Finish or cancel the active Compose gesture before changing snapping.");
        }
        application_state.edit_snap_enabled = !application_state.edit_snap_enabled;
        return compose_action_info(context, application_state.edit_snap_enabled
            ? "Snap on." : "Snap off: edits use the finest row.");
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK: {
        application_state.y_lock = !application_state.y_lock;
        state.y_lock = application_state.y_lock;
        return compose_action_info(context, application_state.y_lock
            ? "Y-lock on: dragged notes keep their rows." : "Y-lock off.");
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY: {
        if (action.compose_timing_lane >= compose_timing_lane_count) {
            return compose_action_warning(context, "The requested timing lane does not exist.");
        }
        if (editor_interaction_has_pending_gesture(state)) {
            return compose_action_warning(context,
                "Finish or cancel the active Compose gesture before changing timing lanes.");
        }
        const auto bit = static_cast<std::uint8_t>(1U << action.compose_timing_lane);
        application_state.compose_timing_lane_visibility_mask ^= bit;
        return compose_action_info(context,
            (application_state.compose_timing_lane_visibility_mask & bit) != 0
                ? "Compose timing lane shown." : "Compose timing lane hidden.");
    }
    default:
        return std::nullopt;
    }
    return compose_interaction_feedback(context, result, status);
}

auto stage_output_music(const wormedit::song_document_t& snapshot,
                        const wormedit::output_group_t& group,
                        const std::filesystem::path& asset_base, std::stop_token stop)
    -> std::optional<std::string> {
    using namespace wormedit;
    if (group.family != format_family_t::osu_mania &&
        group.family != format_family_t::stepmania) return std::nullopt;
    const resource_entry_t* music{};
    for (const auto& resource : snapshot.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
            continue;
        if (music) return "The output has more than one music resource.";
        music = &resource;
    }
    if (!music) return std::nullopt;
    const auto asset = std::ranges::find(snapshot.audio_assets, music->asset, &audio_asset_t::id);
    if (asset == snapshot.audio_assets.end()) return "The music resource has no audio asset.";
    const auto copied = copy_resource_file_into_export(
        {.project_directory = asset_base,
         .source_path = asset->source_path,
         .relative_output_path = group.relative_output_path,
         .asset = asset->id}, stop);
    if (!copied) return "Could not stage music for export: " + copied.message;
    return std::nullopt;
}

void report_output_conversion_losses(const wormedit::conversion_result_t& converted,
                                     output_group_export_task_result_t& completed) {
    if (!completed.published) return;
    std::size_t count{};
    const wormedit::conversion_diagnostic_t* first{};
    for (const auto& diagnostic : converted.diagnostics) {
        if (!diagnostic.loss) continue;
        ++count;
        if (!first) first = &diagnostic;
        if (completed.detail.size() >= wormedit::notification_detail_max_bytes) continue;
        std::string detail = diagnostic.chart
            ? "Chart " + std::to_string(diagnostic.chart->value) + ": " : std::string{};
        if (!diagnostic.path.empty()) detail += diagnostic.path + ": ";
        detail += diagnostic.message + "\n";
        completed.detail += detail;
        wormedit::truncate_notification_text(completed.detail, wormedit::notification_detail_max_bytes);
    }
    if (!first) return;
    completed.message = "Output group published with " + std::to_string(count) +
        (count == 1 ? " conversion loss: " : " conversion losses: ") + first->message;
    wormedit::truncate_notification_text(completed.message, wormedit::notification_message_max_bytes);
    completed.severity = wormedit::notification_severity_t::warning;
}

auto sliced_bms_destination_error(const wormedit::song_document_t& snapshot,
    const wormedit::output_group_t& group, const wormedit::conversion_request_t& request,
    const wormedit::conversion_result_t& converted) -> std::optional<std::string> {
    if (group.family != wormedit::format_family_t::bms ||
        !wormedit::conversion_needs_slice_spans(snapshot, request) ||
        !group.relative_output_path.has_extension()) return std::nullopt;
    const auto requested = group.relative_output_path.extension();
    for (const auto& output : converted.outputs) {
        const auto inferred = std::filesystem::path(output.suggested_filename).extension();
        const bool incompatible = requested == ".pms" ? inferred != ".pms"
            : inferred == ".pms" || (requested == ".bme" && inferred != ".bme");
        if (incompatible)
            return "BMSON lane layout requires " + inferred.string() +
                " output; selected " + requested.string() + " filename is incompatible.";
    }
    return std::nullopt;
}

void complete_output_group_conversion(
    const wormedit::song_document_t& snapshot, const wormedit::output_group_t& group,
    wormedit::conversion_request_t& request, const std::filesystem::path& asset_base,
    std::stop_token stop, output_group_export_task_result_t& completed) {
    using namespace wormedit;
    const auto converted = convert_document(snapshot, request);
    if (!converted) {
        const auto error = std::ranges::find_if(converted.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == diagnostic_severity_t::error;
        });
        completed.message = error == converted.diagnostics.end()
                                ? "Output conversion failed."
                                : error->message;
        completed.severity = notification_severity_t::error;
        return;
    }
    if (stop.stop_requested()) {
        completed.message = "Export cancelled before publication.";
        completed.severity = notification_severity_t::info;
        return;
    }
    if (auto error = sliced_bms_destination_error(snapshot, group, request, converted)) {
        completed.message = *error;
        completed.severity = notification_severity_t::error;
        return;
    }
    conversion_publication_options_t publication;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (conversion_needs_slice_spans(snapshot, request)) {
        publication.stage_generated_files = [&](const auto& staging) {
            return render_conversion_slice_plans(snapshot, converted, {asset_base, staging, stop});
        };
    }
#endif
    const auto published = publish_conversion_group_atomic(asset_base, group, request, converted,
                                                           std::move(publication));
    completed.published = published.published;
    completed.message = published.published ? "Output group published from the selected revision."
                           : published.errors.empty() ? "Output group publication failed."
                                                      : published.errors.front();
    completed.severity = !published.published ? notification_severity_t::error
                          : !published.warnings.empty() ? notification_severity_t::warning
                                                        : notification_severity_t::success;
    if (published.published && !published.warnings.empty())
        completed.message += " " + published.warnings.front();
    if (published.published) {
        for (const auto& warning : published.warnings) completed.detail += warning + "\n";
        report_output_conversion_losses(converted, completed);
    }
}

void complete_sliced_output_group_conversion(
    const wormedit::song_document_t& snapshot, const wormedit::output_group_t& group,
    wormedit::conversion_request_t& request, const std::filesystem::path& asset_base,
    std::stop_token stop, output_group_export_task_result_t& completed) {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (auto error = wormedit::prepare_conversion_slice_sources(snapshot, request, asset_base, stop)) {
        completed.message = *error;
        completed.severity = stop.stop_requested() ? notification_severity_t::info
                                                   : notification_severity_t::error;
        return;
    }
    complete_output_group_conversion(snapshot, group, request, asset_base, stop, completed);
#else
    const auto converted = convert_document(snapshot, request);
    if (!converted || (converted.slice_plans.empty() && converted.asset_copies.empty() &&
                       converted.silent_files.empty())) {
        complete_output_group_conversion(snapshot, group, request, asset_base, stop, completed);
        return;
    }
    completed.message = "BMSON sliced-audio conversion requires the audio decoder build.";
    completed.severity = notification_severity_t::error;
#endif
}

task_result_t run_output_group_export_task(const wormedit::song_document_t& snapshot,
                                           const wormedit::output_group_t& group,
                                           wormedit::conversion_request_t& request,
                                           const std::filesystem::path& asset_base,
                                           std::stop_token stop) {
    using namespace wormedit;
    auto completed = std::make_shared<output_group_export_task_result_t>();
    if (stop.stop_requested()) {
        completed->message = "Export cancelled before conversion.";
        completed->severity = notification_severity_t::info;
    } else if (conversion_needs_slice_spans(snapshot, request)) {
        complete_sliced_output_group_conversion(snapshot, group, request, asset_base, stop, *completed);
    } else if (auto staging_error = stage_output_music(snapshot, group, asset_base, stop)) {
        completed->message = *staging_error;
        completed->severity = stop.stop_requested() ? notification_severity_t::info
                                                    : notification_severity_t::error;
    } else {
        const auto discovered = discover_staged_export_assets(snapshot, group, asset_base, stop);
        if (!discovered) {
            completed->message = discovered.errors.front();
            completed->severity = stop.stop_requested() ? notification_severity_t::info
                                                        : notification_severity_t::error;
        } else {
            request.staged_assets = discovered.mappings;
            complete_output_group_conversion(snapshot, group, request, asset_base, stop,
                                             *completed);
        }
    }
    return task_result_t::success(make_task_value(
        std::shared_ptr<const output_group_export_task_result_t>{std::move(completed)}));
}

struct imported_resource_metadata_t {
    std::optional<wormedit::audio_stream_info_t> audio;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> image;
};

auto import_resource_role(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_role_t> {
    using namespace wormedit;
    switch (action.compose_resource_role) {
    case WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE: return resource_role_t::sample;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGM: return resource_role_t::bgm;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGA: return resource_role_t::bga;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_MUSIC: return resource_role_t::music;
    default: return std::nullopt;
    }
}

auto inspect_imported_resource(const std::filesystem::path& path, wormedit::resource_role_t role)
    -> std::variant<imported_resource_metadata_t, std::string> {
    imported_resource_metadata_t metadata;
    if (role == wormedit::resource_role_t::bga) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        const auto decoded = wormedit::visual::load_image_rgba(
            {.kind = wormedit::asset_kind_t::image, .path = path});
        if (!decoded) return decoded.message.empty() ? "The image could not be decoded."
                                                      : decoded.message;
        metadata.image = std::pair{decoded.image->width, decoded.image->height};
#else
        return "Image import is unavailable in this build.";
#endif
    } else {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::audio::decoder_t decoder;
        std::string error;
        if (!decoder.open(path, &error))
            return error.empty() ? "The audio file could not be decoded." : error;
        const auto info = decoder.info();
        if (!info) return "The audio decoder did not report source geometry.";
        metadata.audio = {.sample_rate = info->source_sample_rate,
                          .channels = info->source_channel_count,
                          .frame_count = info->source_frame_count,
                          .seekable = info->seekable};
#else
        return "Audio import is unavailable in this build.";
#endif
    }
    return metadata;
}

std::optional<std::string> commit_primary_music(compose_action_context_t& context,
                                                const std::filesystem::path& path,
                                                const imported_resource_metadata_t& metadata) {
    using namespace wormedit;
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    auto name = path.filename().generic_string();
    if (name.size() > 255) name.clear();
    const auto applied = apply_primary_music_set(
        context.history, context.action.compose_input_document_revision,
        {.source_path = path, .name = std::move(name),
         .allocate_bms_identifier = chart != context.history.document.charts.end() &&
                                    chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio});
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front(), notification_source_t::import_);
    if (!applied.applied) return compose_action_info(context, "That file is already the song audio.", notification_source_t::import_);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "Song audio set to " + path.filename().string() + ".", "Song audio set, but workspace rebinding failed.", notification_source_t::import_);
    const auto music = std::ranges::find_if(context.history.document.resources, [](const auto& r) {
        return std::ranges::find(r.roles, resource_role_t::music) != r.roles.end();
    });
    if (music != context.history.document.resources.end())
        (void)apply_workspace_action(context.workspace, context.history.document,
            context.history.document.revision, workspace_select_audio_asset_action_t{music->asset});
    return compose_action_success(context, "Song audio set to " + path.filename().string() + ".", notification_source_t::import_);
}

std::optional<std::string> commit_resource_import(
    compose_action_context_t& context, wormedit::resource_role_t role,
    const std::filesystem::path& path, imported_resource_metadata_t metadata) {
    using namespace wormedit;
    if (role == resource_role_t::music) return commit_primary_music(context, path, metadata);
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    auto name = path.filename().generic_string();
    if (name.size() > 255) name.clear();
    const auto applied = apply_resource_asset_import(
        context.history, context.action.compose_input_document_revision,
        {.source_path = path, .role = role, .name = std::move(name),
         .allocate_bms_identifier = chart != context.history.document.charts.end() &&
                                    chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio, .image_geometry = metadata.image});
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front(), notification_source_t::import_);
    if (!applied.applied) return compose_action_info(context, "Resource import made no document change.", notification_source_t::import_);
    const auto imported = context.history.document.resources.back().asset;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "External file linked as a resource.", "Resource imported, but workspace rebinding failed.", notification_source_t::import_);
    if (role == resource_role_t::bga)
        (void)apply_workspace_action(context.workspace, context.history.document,
            context.history.document.revision, workspace_select_visual_asset_action_t{imported});
    else
        (void)apply_workspace_action(context.workspace, context.history.document,
            context.history.document.revision, workspace_select_audio_asset_action_t{imported});
    return compose_action_success(context, "External file linked as a resource.", notification_source_t::import_);
}

std::optional<std::string> import_resource_asset(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before importing a resource.", notification_source_t::import_);
    const auto role = import_resource_role(context.action);
    if (!role) return compose_action_warning(context, "Resource import used an unknown catalog role.", notification_source_t::import_);
    const std::filesystem::path path =
        std::filesystem::path{context.action.compose_import_path}.lexically_normal();
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
        return compose_action_warning(context, "Resource import requires an existing absolute file path.", notification_source_t::import_);
    auto metadata = inspect_imported_resource(path, *role);
    if (const auto* failure = std::get_if<std::string>(&metadata)) return compose_action_warning(context, *failure, notification_source_t::import_);
    return commit_resource_import(context, *role, path,
                                  std::get<imported_resource_metadata_t>(std::move(metadata)));
}

std::optional<std::string> dispatch_compose_import_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET)
        return std::nullopt;
    return import_resource_asset(context);
}

struct resource_copy_plan_t {
    wormedit::asset_id_t asset;
    std::filesystem::path source;
    std::filesystem::path output;
    bool export_copy{};
};

auto prepare_resource_copy(compose_action_context_t& context)
    -> std::variant<resource_copy_plan_t, compose_feedback_t> {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    if (context.resource_copy_runtime.busy() || context.export_runtime.busy() ||
        context.metadata_import_busy)
        return compose_feedback_t{"Wait for the current resource copy, import, or export to finish.", notification_severity_t::warning, notification_source_t::import_};
    if (context.asset_base.empty())
        return compose_feedback_t{"Open a saved native project before copying resource files into it.", notification_severity_t::warning, notification_source_t::import_};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_feedback_t{"Finish or cancel the active Compose gesture before copying a resource.", notification_severity_t::warning, notification_source_t::import_};
    const asset_id_t id{action.compose_resource_asset_id};
    const auto audio = std::ranges::find(history.document.audio_assets, id, &audio_asset_t::id);
    const auto visual = std::ranges::find(history.document.visual_assets, id, &visual_asset_t::id);
    if (!id || (audio == history.document.audio_assets.end() &&
                visual == history.document.visual_assets.end()))
        return compose_feedback_t{"The selected resource asset is no longer available.", notification_severity_t::warning, notification_source_t::import_};
    const auto& source = audio != history.document.audio_assets.end()
        ? audio->source_path : visual->source_path;
    const bool exporting = action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    if (!exporting && !source.is_absolute() && source.parent_path() == "assets" &&
        source.filename().string().starts_with(std::to_string(id.value) + "-"))
        return compose_feedback_t{"Resource source is already in the project assets directory.", notification_severity_t::info, notification_source_t::import_};
    std::filesystem::path output;
    if (exporting) {
        if (action.compose_output_group_index >= history.document.output_groups.size())
            return compose_feedback_t{"The selected output group is no longer available.", notification_severity_t::warning, notification_source_t::export_};
        const auto& group = history.document.output_groups[action.compose_output_group_index];
        if (std::ranges::find(group.charts, context.state.chart) == group.charts.end())
            return compose_feedback_t{"The selected output group does not include this chart.", notification_severity_t::warning, notification_source_t::export_};
        output = group.relative_output_path;
    }
    return resource_copy_plan_t{id, source, output, exporting};
}

std::optional<std::string> start_resource_copy(compose_action_context_t& context,
                                               resource_copy_plan_t plan) {
    using namespace wormedit;
    task_graph_t graph{1};
    const project_asset_copy_request_t request{
        .project_directory = context.asset_base, .source_path = plan.source, .asset = plan.asset};
    const auto added = graph.add_task({}, [request, plan](std::span<const task_result_t>,
                                                          std::stop_token stop) {
        auto copied = std::make_shared<const project_asset_copy_result_t>(
            plan.export_copy
                ? copy_resource_file_into_export(
                      {.project_directory = request.project_directory,
                       .source_path = request.source_path,
                       .relative_output_path = plan.output,
                       .asset = request.asset,
                       .maximum_bytes = request.maximum_bytes}, stop)
                : copy_resource_file_into_project(request, stop));
        return task_result_t::success(make_task_value(std::move(copied)));
    });
    if (!added) return compose_action_error(context,
        added.error ? added.error->message : "Resource copy could not start.",
        plan.export_copy ? notification_source_t::export_ : notification_source_t::import_);
    auto submitted = context.resource_copy_scheduler.submit(std::move(graph));
    if (!submitted) return compose_action_error(context,
        submitted.error ? submitted.error->message : "Resource copy could not start.",
        plan.export_copy ? notification_source_t::export_ : notification_source_t::import_);
    auto pending = pending_project_resource_copy_t{
        .graph = std::move(submitted.graph), .task = *added.id, .asset = plan.asset,
        .song = context.history.document.id, .revision = context.history.document.revision,
        .source_path = plan.source, .output_path = plan.output, .export_copy = plan.export_copy};
    if (!context.resource_copy_runtime.start(std::move(pending))) {
        pending.graph.cancel();
        return compose_action_warning(context,
            "Wait for the current resource copy, import, or export to finish.",
            plan.export_copy ? notification_source_t::export_ : notification_source_t::import_);
    }
    return plan.export_copy ? "Export copy started in the background."
                            : "Resource copy started in the background.";
}

std::optional<std::string> dispatch_compose_copy_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT &&
        context.action.compose_action != WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT)
        return std::nullopt;
    auto plan = prepare_resource_copy(context);
    if (const auto* error = std::get_if<compose_feedback_t>(&plan))
        return publish_compose_feedback(context, error->message, error->severity, error->source);
    return start_resource_copy(context, std::get<resource_copy_plan_t>(std::move(plan)));
}

struct output_export_plan_t {
    wormedit::output_group_t group;
    wormedit::conversion_request_t request;
};

auto prepare_output_export(compose_action_context_t& context)
    -> std::variant<output_export_plan_t, compose_feedback_t> {
    using namespace wormedit;
    const auto& doc = context.history.document;
    if (context.asset_base.empty())
        return compose_feedback_t{"Open a saved native project before exporting an output group.", notification_severity_t::warning, notification_source_t::export_};
    if (context.resource_copy_runtime.busy() || context.export_runtime.busy() ||
        context.metadata_import_busy)
        return compose_feedback_t{"Wait for the current resource copy, import, or export to finish.", notification_severity_t::warning, notification_source_t::export_};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_feedback_t{"Finish or cancel the active Compose gesture before exporting.", notification_severity_t::warning, notification_source_t::export_};
    if (context.action.compose_output_group_index >= doc.output_groups.size())
        return compose_feedback_t{"The selected output group is no longer available.", notification_severity_t::warning, notification_source_t::export_};
    auto group = doc.output_groups[context.action.compose_output_group_index];
    if (group.charts.empty() ||
        std::ranges::find(group.charts, context.state.chart) == group.charts.end())
        return compose_feedback_t{"The selected output group has no matching charts.", notification_severity_t::warning, notification_source_t::export_};
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project) request.charts = group.charts;
    request.overwrite = context.action.compose_export_overwrite;
    request.loss_policy = loss_policy_t::warn;
    if (group.family == format_family_t::stepmania) {
        request.stepmania = stepmania_export_policy_t{
            .file_profile = group.relative_output_path.extension() == ".sm" ||
                                    group.relative_output_path == "sm"
                ? stepmania_profile_t::sm : stepmania_profile_t::ssc};
    } else if (group.family == format_family_t::bms) {
        const auto extension = group.relative_output_path.extension().string();
        if ((extension == ".bme" || extension == ".bml" || extension == ".pms") &&
            !conversion_needs_slice_spans(doc, request))
            request.target_profile = extension.substr(1);
    }
    return output_export_plan_t{std::move(group), std::move(request)};
}

std::optional<std::string> start_output_export(compose_action_context_t& context,
                                                output_export_plan_t plan) {
    using namespace wormedit;
    auto snapshot = context.history.document;
    task_graph_t graph{1};
    const auto added = graph.add_task({}, [snapshot = std::move(snapshot), group = std::move(plan.group),
        request = std::move(plan.request), base = context.asset_base](
        std::span<const task_result_t>, std::stop_token stop) mutable {
        return run_output_group_export_task(snapshot, group, request, base, stop);
    });
    if (!added) return compose_action_error(context,
        added.error ? added.error->message : "Export could not start.", notification_source_t::export_);
    auto submitted = context.export_runtime.scheduler.submit(std::move(graph));
    if (!submitted) return compose_action_error(context,
        submitted.error ? submitted.error->message : "Export could not start.", notification_source_t::export_);
    auto pending = pending_output_group_export_t{
        .graph = std::move(submitted.graph), .task = *added.id,
        .song = context.history.document.id, .revision = context.history.document.revision};
    if (!context.export_runtime.start(std::move(pending))) {
        pending.graph.cancel();
        return compose_action_warning(context,
            "Wait for the current resource copy, import, or export to finish.", notification_source_t::export_);
    }
    return "Output group conversion started in the background.";
}

std::optional<std::string> dispatch_compose_export_output_group_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP)
        return std::nullopt;
    auto plan = prepare_output_export(context);
    if (const auto* error = std::get_if<compose_feedback_t>(&plan))
        return publish_compose_feedback(context, error->message, error->severity, error->source);
    return start_output_export(context, std::get<output_export_plan_t>(std::move(plan)));
}

auto compose_placement_kind(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_placement_kind_t> {
    using namespace wormedit;
    switch (action.compose_resource_role) {
    case WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE: return resource_placement_kind_t::sample_note;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGM: return resource_placement_kind_t::bgm_event;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGA: return resource_placement_kind_t::bga_background;
    default: return std::nullopt;
    }
}

auto apply_compose_resource_placement(compose_action_context_t& context,
                                      wormedit::resource_placement_kind_t kind)
    -> wormedit::document_command_result_t {
    using namespace wormedit;
    const auto resource = resource_id_t{context.action.compose_resource_id};
    if (context.state.virtual_bms_cursor_row)
        return apply_virtual_bms_resource_placement(
            context.history, context.action.compose_input_document_revision,
            {.chart = context.state.chart, .resource = resource,
             .canvas_row = *context.state.virtual_bms_cursor_row,
             .lane = context.state.active_lane, .kind = kind,
             .semantic_layers = context.state.armed_note_layers});
    return apply_resource_placement(
        context.history, context.action.compose_input_document_revision,
        {.chart = context.state.chart, .resource = resource,
         .position = context.state.cursor, .lane = context.state.active_lane, .kind = kind,
         .semantic_layers = context.state.armed_note_layers});
}

std::optional<std::string> finish_compose_resource_placement(
    compose_action_context_t& context, wormedit::resource_placement_kind_t kind) {
    using namespace wormedit;
    auto& state = context.state;
    if (state.virtual_bms_cursor_row) {
        const auto* chart = selected_chart(context.history, context.workspace);
        std::optional<local_position_t> position;
        if (chart) {
            const auto built = build_measure_index(*chart);
            if (const auto* index = std::get_if<measure_index_t>(&built))
                position = timing_position_at_row(*index, *state.virtual_bms_cursor_row);
        }
        place_cursor_at(state, position.value_or(state.cursor));
    }
    const auto role = kind == resource_placement_kind_t::sample_note ? resource_role_t::sample
        : kind == resource_placement_kind_t::bgm_event ? resource_role_t::bgm : resource_role_t::bga;
    const auto next = next_eligible_placement_resource(
        context.history.document, resource_id_t{context.action.compose_resource_id}, role);
    worm_ui_frontend_resource_placed(context.action.compose_resource_id,
                                     next.value_or(resource_id_t{}).value);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "Resource placed at the edit cursor.", "Resource placed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "Resource placed at the edit cursor.");
}

std::optional<std::string> dispatch_compose_place_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before placing a resource.");
    const auto kind = compose_placement_kind(context.action);
    if (!kind) return compose_action_warning(context, "The selected resource role cannot be placed in the chart.");
    const auto applied = apply_compose_resource_placement(context, *kind);
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied) return compose_action_info(context, "Resource placement made no document change.", notification_source_t::edit);
    return finish_compose_resource_placement(context, *kind);
}

std::optional<std::string> select_resource_uses(compose_action_context_t& context) {
    using namespace wormedit;
    const resource_id_t resource{context.action.compose_resource_id};
    if (!resource || std::ranges::find(context.history.document.resources, resource,
                                       &resource_entry_t::id) == context.history.document.resources.end())
        return compose_action_warning(context, "The selected resource is no longer available.");
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end())
        return compose_action_warning(context, "The selected chart is no longer available.");
    auto uses = find_compose_resource_uses(*chart, resource);
    const auto count = uses.size();
    const auto result = dispatch_editor_action(context.history, context.state,
        select_editor_objects_action_t{.objects = std::move(uses),
                                       .mode = editor_area_selection_mode_t::replace});
    return compose_interaction_feedback(context, result, count == 0
        ? "No uses of this resource exist in the active chart."
        : std::to_string(count) + (count == 1 ? " resource use selected."
                                               : " resource uses selected."));
}

std::optional<std::string> assign_resource_to_selection(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before assigning a resource.");
    const auto applied = apply_resource_assignment(context.history,
        context.action.compose_input_document_revision,
        {.chart = context.state.chart,
         .resource = resource_id_t{context.action.compose_resource_id},
         .objects = context.state.selected_objects});
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied) return compose_action_info(context, "Selected objects already use that resource.", notification_source_t::edit);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "Resource assigned to selected objects.", "Resource assigned, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "Resource assigned to selected objects.");
}

std::optional<std::string> create_resource_from_asset(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before creating a resource.", notification_source_t::import_);
    const auto role = import_resource_role(context.action).value_or(resource_role_t::music);
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    const auto applied = apply_resource_creation(context.history,
        context.action.compose_input_document_revision,
        {.asset = asset_id_t{context.action.compose_resource_asset_id}, .role = role,
         .allocate_bms_identifier = chart != context.history.document.charts.end() &&
                                    chart->source_family == format_family_t::bms});
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front(), notification_source_t::import_);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        role == resource_role_t::bga ? "BGA resource created from the selected image." : "Resource created from the selected audio asset.", "Resource created, but workspace rebinding failed.", notification_source_t::import_);
    return compose_action_success(context, role == resource_role_t::bga
        ? "BGA resource created from the selected image."
        : "Resource created from the selected audio asset.", notification_source_t::import_);
}

std::optional<std::string> dispatch_compose_resource_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES: return select_resource_uses(context);
    case WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION:
        return assign_resource_to_selection(context);
    case WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE: return create_resource_from_asset(context);
    case WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET:
        return dispatch_compose_import_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT:
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT:
        return dispatch_compose_copy_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP:
        return dispatch_compose_export_output_group_action(context);
    case WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE:
        return dispatch_compose_place_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES:
    case WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA:
        if (editor_interaction_has_pending_gesture(context.state))
            return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing BGA.");
        return dispatch_compose_bga_action(context.action, context.history, context.workspace,
                                           context.state.chart, context.notifications);
    case WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES:
    case WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT:
    case WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS:
    case WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS:
    case WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS:
    case WORM_UI_COMPOSE_ACTION_AUDITION_SOUND:
    case WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS:
    case WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS:
        if (editor_interaction_has_pending_gesture(context.state))
            return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing sounds.");
        return dispatch_compose_sound_action(context.action, context.history, context.workspace,
                                             context.state.chart, context.application_state,
                                             context.notifications);
    default: return std::nullopt;
    }
}

std::optional<std::string> assign_bmson_selection(compose_action_context_t& context) {
    using namespace wormedit;
    auto& state = context.state;
    const bool to_lane = context.action.compose_action ==
                         WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
    if (editor_interaction_has_pending_gesture(state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before assigning BMSON sound.");
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    const auto kind = to_lane ? editor_selection_object_kind_t::bgm_event
                              : editor_selection_object_kind_t::note;
    if (state.selected_objects.size() != 1 || state.selected_objects.front().kind != kind ||
        state.selected_objects.front().id != event)
        return compose_action_warning(context, "Select one linked BMSON tap or BGM marker before assigning sound.");
    const auto assigned = assign_bmson_sound_event(
        context.history, context.action.compose_input_document_revision, state.chart, event,
        to_lane ? std::optional<lane_id_t>{state.active_lane} : std::nullopt);
    if (!assigned.diagnostics.empty()) return publish_compose_diagnostic(context, assigned.diagnostics.front());
    if (!assigned.applied) return compose_action_info(context, "BMSON sound is already assigned to that destination.", notification_source_t::edit);
    state.selected_objects = {{to_lane ? editor_selection_object_kind_t::note
                                       : editor_selection_object_kind_t::bgm_event, event}};
    state.selected_notes = to_lane ? std::vector<object_id_t>{event} : std::vector<object_id_t>{};
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        to_lane ? "BMSON BGM assigned to the active playable lane." : "BMSON tap assigned to BGM.", "BMSON sound assigned, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, to_lane ? "BMSON BGM assigned to the active playable lane."
                   : "BMSON tap assigned to BGM.");
}

auto selected_bmson_source_event(const compose_action_context_t& context,
                                 wormedit::object_id_t bgm_event)
    -> std::optional<wormedit::object_id_t> {
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &wormedit::chart_document_t::id);
    if (chart == context.history.document.charts.end() ||
        chart->source_family != wormedit::format_family_t::bmson) return std::nullopt;
    for (const auto& measure : chart->measures) {
        const auto event = std::ranges::find(measure.bgm_events, bgm_event,
                                             &wormedit::bgm_event_t::id);
        if (event != measure.bgm_events.end()) return event->sound_channel_event;
    }
    return std::nullopt;
}

std::optional<std::string> set_bmson_selection_restart(compose_action_context_t& context) {
    using namespace wormedit;
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before changing a BMSON slice.");
    if (!event || context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_warning(context, "Select one BMSON BGM marker before changing its restart state.");
    const auto source_event = selected_bmson_source_event(context, event);
    if (!source_event)
        return compose_action_warning(context, "The selected BGM marker has no BMSON sound-channel slice.");
    const bool restart = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART;
    const auto applied = set_bmson_sound_channel_restart(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        *source_event, restart);
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied)
        return compose_action_info(context, restart ? "BMSON slice already restarts." : "BMSON slice already continues.", notification_source_t::edit);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        restart ? "BMSON slice set to restart." : "BMSON slice set to continue.", "BMSON slice changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, restart ? "BMSON slice set to restart." : "BMSON slice set to continue.");
}

std::optional<std::string> move_bmson_selection_to_cursor(compose_action_context_t& context) {
    using namespace wormedit;
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before moving a BMSON marker.");
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_warning(context, "Select one BMSON BGM marker before moving it.");
    const auto moved = move_bmson_bgm_marker(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        event, context.state.cursor);
    if (!moved.diagnostics.empty()) return publish_compose_diagnostic(context, moved.diagnostics.front());
    if (!moved.applied) return compose_action_info(context, "BMSON BGM marker is already at the cursor.", notification_source_t::edit);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "BMSON BGM marker moved to the edit cursor.", "BMSON marker moved, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "BMSON BGM marker moved to the edit cursor.");
}

std::optional<std::string> insert_bmson_marker_at_cursor(compose_action_context_t& context) {
    using namespace wormedit;
    const auto event = object_id_t{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before inserting a BMSON marker.");
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_warning(context, "Select one BMSON BGM marker to choose its sound channel.");
    const auto inserted = insert_bmson_bgm_marker(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        event, context.state.cursor,
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR);
    if (!inserted.diagnostics.empty()) return publish_compose_diagnostic(context, inserted.diagnostics.front());
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "BMSON BGM marker inserted at the edit cursor.", "BMSON marker inserted, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "BMSON BGM marker inserted at the edit cursor.");
}

std::optional<std::string> stage_bmson_marker_at_cursor(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before staging a BMSON marker.");
    auto severity = notification_severity_t::warning;
    auto message = stage_bmson_bgm_marker(
        context.history, context.state, context.application_state,
        wormedit::object_id_t{context.action.compose_bmson_bgm_event_id}, context.state.cursor,
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR,
        &severity);
    return publish_compose_feedback(context, std::move(message), severity, notification_source_t::input);
}

std::optional<std::string> commit_bmson_marker_batch(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before committing BMSON markers.");
    auto& draft = context.application_state.bmson_marker_draft;
    if (!draft || draft->markers.empty() || draft->chart != context.state.chart ||
        draft->revision != context.history.document.revision) {
        draft.reset();
        return compose_action_warning(context, "The BMSON marker batch is no longer available for this chart revision.");
    }
    const auto inserted = insert_bmson_sound_markers(
        context.history, draft->revision, draft->chart, draft->reference, draft->markers);
    if (!inserted.diagnostics.empty()) return publish_compose_diagnostic(context, inserted.diagnostics.front());
    draft.reset();
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "BMSON markers inserted in one undoable edit.", "BMSON markers inserted, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "BMSON markers inserted in one undoable edit.");
}

std::optional<std::string> delete_bmson_selected_marker(compose_action_context_t& context) {
    using namespace wormedit;
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before deleting a BMSON marker.");
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_warning(context, "Select one BMSON BGM marker before deleting it.");
    const auto deleted = delete_bmson_bgm_marker(
        context.history, context.action.compose_input_document_revision, context.state.chart, event);
    if (!deleted.diagnostics.empty()) return publish_compose_diagnostic(context, deleted.diagnostics.front());
    if (!deleted.applied) return compose_action_info(context, "BMSON marker deletion made no document change.", notification_source_t::edit);
    context.state.selected_objects.clear();
    context.state.selected_notes.clear();
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        "BMSON BGM marker deleted.", "BMSON marker deleted, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "BMSON BGM marker deleted.");
}

std::optional<std::string> dispatch_compose_bmson_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART:
    case WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE:
        return set_bmson_selection_restart(context);
    case WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE:
    case WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM:
        return assign_bmson_selection(context);
    case WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR:
        return move_bmson_selection_to_cursor(context);
    case WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR:
    case WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR:
        return insert_bmson_marker_at_cursor(context);
    case WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR:
    case WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR:
        return stage_bmson_marker_at_cursor(context);
    case WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM:
        return commit_bmson_marker_batch(context);
    case WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM:
        context.application_state.bmson_marker_draft.reset();
        return compose_action_hint(context, "BMSON marker batch canceled.");
    case WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER:
        return delete_bmson_selected_marker(context);
    default:
        return std::nullopt;
    }
}

struct beat_section_edit_t {
    std::optional<wormedit::marker_edit_action_t> edit;
    std::string error;
    bool from_selection{};
};

auto finish_marker_edit_feedback(compose_action_context_t& context,
                                 const wormedit::document_command_result_t& applied,
                                 std::string success, std::string unchanged,
                                 std::string rebind_failure,
                                 notification_source_t source = notification_source_t::edit)
    -> std::string {
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front(), source);
    if (!applied.applied) return compose_action_info(context, std::move(unchanged), source);
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return compose_mutation_rebind_failure(context, rebound, std::move(success),
                                               std::move(rebind_failure), source);
    return compose_action_success(context, std::move(success), source);
}

beat_section_edit_t make_beat_section_insertion(compose_action_context_t& context,
                                                  const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    const auto built = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures) return {.error = "The chart measure geometry cannot define a section span."};
    position_span_t span;
    bool from_selection{};
    if (context.state.retained_selection_range) {
        const auto& selection = *context.state.retained_selection_range;
        const auto begin = absolute_row(*measures, selection.begin);
        const auto end = absolute_row(*measures, selection.end);
        if (!begin || !end || *begin >= *end)
            return {.error = "The retained area/Space interval is no longer valid; select it again."};
        span = selection;
        from_selection = true;
    } else {
        const auto begin = absolute_row(*measures, context.state.cursor);
        const auto denominator = context.state.active_snap_denominator;
        if (!begin || denominator == 0 || chart.divisions_per_standard_measure % denominator != 0)
            return {.error = "The active Compose grid cannot define a section span."};
        const auto step = chart.divisions_per_standard_measure / denominator;
        if (step == 0 || *begin >= measures->total_rows || step >= measures->total_rows - *begin)
            return {.error = "Move the cursor earlier to create a one-snap section."};
        const auto end = timing_position_at_row(*measures, *begin + step);
        if (!end) return {.error = "The next active-snap row is outside the chart."};
        span = {context.state.cursor, *end};
    }
    return {.edit = insert_section_action_t{context.state.chart, context.action.compose_marker_label,
                                             marker_anchor_t::beat, span, std::nullopt},
            .from_selection = from_selection};
}

auto make_existing_beat_section_edit(compose_action_context_t& context,
                                     const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    if (!context.action.compose_section_id)
        return "Choose a beat-anchored section before editing it.";
    const auto section = std::ranges::find(chart.sections,
        object_id_t{context.action.compose_section_id}, &section_t::id);
    if (section == chart.sections.end() || section->anchor != marker_anchor_t::beat ||
        !section->beat_span)
        return "That beat-anchored section is no longer available.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION)
        return marker_edit_action_t{delete_section_action_t{context.state.chart, section->id}};
    auto span = *section->beat_span;
    auto label = section->label;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL)
        label = context.action.compose_marker_label;
    else if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN)
        span.begin = context.state.cursor;
    else
        span.end = context.state.cursor;
    return marker_edit_action_t{update_section_action_t{
        context.state.chart, section->id, std::move(label), marker_anchor_t::beat, span,
        std::nullopt}};
}

std::string beat_section_success(const WormUiWorkspaceAction& action, bool from_selection) {
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
        return from_selection ? "Section added from the retained area/Space interval."
                              : "One-snap section added at the Compose cursor.";
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL: return "Section renamed.";
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN: return "Section start moved to the Compose cursor.";
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END: return "Section end moved to the Compose cursor.";
    default: return "Section deleted.";
    }
}

std::optional<std::string> edit_beat_section(compose_action_context_t& context) {
    using namespace wormedit;
    const bool adding = context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION;
    if (context.state.virtual_bms_cursor_row && adding)
        return compose_action_warning(context, "Beat sections require a materialized cursor measure.");
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing sections.");
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context, "Choose a chart before editing sections.");
    marker_edit_action_t edit;
    bool from_selection{};
    if (adding) {
        auto made = make_beat_section_insertion(context, *chart);
        if (!made.error.empty()) return compose_action_warning(context, made.error);
        edit = std::move(*made.edit);
        from_selection = made.from_selection;
    } else {
        auto made = make_existing_beat_section_edit(context, *chart);
        if (const auto* error = std::get_if<std::string>(&made)) return compose_action_warning(context, *error);
        edit = std::get<marker_edit_action_t>(std::move(made));
    }
    const auto applied = apply_marker_edit(
        context.history, context.action.compose_input_document_revision, std::move(edit));
    return finish_marker_edit_feedback(context, applied, beat_section_success(context.action, from_selection),
        "Section edit made no changes.", "Section changed, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_beat_section_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION:
        return edit_beat_section(context);
    default:
        return std::nullopt;
    }
}

auto make_audio_bookmark_edit(compose_action_context_t& context,
                              const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto deleting = context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK;
    const auto milliseconds = deleting ? std::optional<std::int64_t>{}
        : parse_timing_signed(context.action.compose_marker_audio_begin);
    if (!deleting && !milliseconds)
        return "Enter the audio bookmark position as exact signed milliseconds.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK)
        return marker_edit_action_t{insert_bookmark_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::audio_time,
            std::nullopt, milliseconds}};
    if (!context.action.compose_bookmark_id)
        return "Choose an audio-time bookmark before editing it.";
    const auto found = std::ranges::find(chart.bookmarks,
        object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_milliseconds || found->position)
        return "That audio-time bookmark is no longer available.";
    if (deleting)
        return marker_edit_action_t{delete_bookmark_action_t{context.state.chart, found->id}};
    return marker_edit_action_t{update_bookmark_action_t{
        context.state.chart, found->id, context.action.compose_marker_label,
        marker_anchor_t::audio_time, std::nullopt, milliseconds}};
}

std::optional<std::string> edit_audio_bookmark(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing bookmarks.", notification_source_t::edit);
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context, "Choose a chart before editing audio-time bookmarks.", notification_source_t::edit);
    auto built = make_audio_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built)) return compose_action_warning(context, *error, notification_source_t::edit);
    const auto applied = apply_marker_edit(context.history,
        context.action.compose_input_document_revision,
        std::get<marker_edit_action_t>(std::move(built)));
    const auto success = context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK
        ? "Audio-time bookmark added."
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK
            ? "Audio-time bookmark deleted." : "Audio-time bookmark updated.";
    return finish_marker_edit_feedback(context, applied, success,
        "Audio bookmark edit made no changes.", "Audio bookmark changed, but workspace rebinding failed.",
        notification_source_t::edit);
}

std::optional<std::string> dispatch_compose_audio_bookmark_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK:
        return edit_audio_bookmark(context);
    default:
        return std::nullopt;
    }
}

auto make_audio_section_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto deleting = context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION;
    const auto begin = deleting ? std::optional<std::int64_t>{}
        : parse_timing_signed(context.action.compose_marker_audio_begin);
    const auto end = deleting ? std::optional<std::int64_t>{}
        : parse_timing_signed(context.action.compose_marker_audio_end);
    if (!deleting && (!begin || !end))
        return "Enter both audio section bounds as exact signed milliseconds.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION)
        return marker_edit_action_t{insert_section_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::audio_time,
            std::nullopt, std::pair{*begin, *end}}};
    if (!context.action.compose_section_id)
        return "Choose an audio-time section before editing it.";
    const auto found = std::ranges::find(chart.sections,
        object_id_t{context.action.compose_section_id}, &section_t::id);
    if (found == chart.sections.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_millisecond_span || found->beat_span)
        return "That audio-time section is no longer available.";
    if (deleting)
        return marker_edit_action_t{delete_section_action_t{context.state.chart, found->id}};
    return marker_edit_action_t{update_section_action_t{
        context.state.chart, found->id, context.action.compose_marker_label,
        marker_anchor_t::audio_time, std::nullopt, std::pair{*begin, *end}}};
}

std::optional<std::string> edit_audio_section(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing sections.", notification_source_t::edit);
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context, "Choose a chart before editing audio-time sections.", notification_source_t::edit);
    auto built = make_audio_section_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built)) return compose_action_warning(context, *error, notification_source_t::edit);
    const auto applied = apply_marker_edit(context.history,
        context.action.compose_input_document_revision,
        std::get<marker_edit_action_t>(std::move(built)));
    const auto success = context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION
        ? "Audio-time section added."
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION
            ? "Audio-time section deleted." : "Audio-time section updated.";
    return finish_marker_edit_feedback(context, applied, success,
        "Audio section edit made no changes.", "Audio section changed, but workspace rebinding failed.",
        notification_source_t::edit);
}

std::optional<std::string> dispatch_compose_audio_section_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return edit_audio_section(context);
    default:
        return std::nullopt;
    }
}

auto make_beat_bookmark_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto operation = context.action.compose_action;
    if (operation == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK)
        return marker_edit_action_t{insert_bookmark_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::beat,
            context.state.cursor, std::nullopt}};
    if (!context.action.compose_bookmark_id)
        return "Choose a beat-anchored bookmark before editing it.";
    const auto found = std::ranges::find(chart.bookmarks,
        object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::beat || !found->position)
        return "That beat-anchored bookmark is no longer available.";
    if (operation == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK)
        return marker_edit_action_t{delete_bookmark_action_t{context.state.chart, found->id}};
    const bool rename = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK;
    return marker_edit_action_t{update_bookmark_action_t{
        context.state.chart, found->id, rename ? context.action.compose_marker_label : found->label,
        marker_anchor_t::beat,
        rename ? found->position : std::optional{context.state.cursor}, std::nullopt}};
}

std::optional<std::string> edit_beat_bookmark(compose_action_context_t& context) {
    using namespace wormedit;
    const auto operation = context.action.compose_action;
    if (context.state.virtual_bms_cursor_row &&
        (operation == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK ||
         operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION))
        return compose_action_warning(context, "Bookmarks can only go inside existing measures. Add measures up to the cursor first.");
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing bookmarks.");
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context, "Choose a chart before editing bookmarks.");
    auto built = make_beat_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built)) return compose_action_warning(context, *error);
    const auto applied = apply_marker_edit(context.history,
        context.action.compose_input_document_revision,
        std::get<marker_edit_action_t>(std::move(built)));
    const auto success = operation == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK
        ? "Bookmark added at the Compose cursor."
        : operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK ? "Bookmark renamed."
        : operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION
            ? "Bookmark moved to the Compose cursor." : "Bookmark deleted.";
    const auto unchanged = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
        ? "Bookmark label was unchanged." : "Bookmark edit made no changes.";
    return finish_marker_edit_feedback(context, applied, success, unchanged,
        "Bookmark changed, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_beat_bookmark_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK:
        return edit_beat_bookmark(context);
    default:
        return std::nullopt;
    }
}

std::optional<std::string> edit_selected_note_labels(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing note labels.");
    if (context.state.selected_notes.empty())
        return compose_action_warning(context, "Select one or more notes before editing their label.");
    const auto label = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
        ? std::optional<std::string>{context.action.compose_note_label} : std::nullopt;
    const auto applied = apply_note_label_edit(context.history,
        context.action.compose_input_document_revision,
        {.chart = context.state.chart, .notes = context.state.selected_notes, .label = label});
    if (!applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied)
        return compose_action_info(context, label ? "Selected notes already have that label."
                     : "Selected notes already have no label.", notification_source_t::edit);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        label ? "Note label applied." : "Note labels cleared.", "Note label changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, label ? "Note label applied." : "Note labels cleared.");
}

std::string navigate_audio_marker(compose_action_context_t& context) {
    using namespace wormedit;
    auto& app = context.application_state;
    if (!app.transport_audio_milliseconds || !app.transport_seek_audio_milliseconds)
        return compose_action_warning(context, "Start chart preview once before navigating audio-time markers.", notification_source_t::audio);
    const auto cursor = app.transport_audio_milliseconds();
    if (!cursor) return compose_action_warning(context, "Start chart preview once to compile its seekable timeline.", notification_source_t::audio);
    const auto direction = context.action.compose_action ==
            WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER
        ? marker_navigation_direction_t::previous : marker_navigation_direction_t::next;
    const auto marker = navigate_markers(context.history.document, context.history.document.revision,
                                         context.state.chart, *cursor, direction);
    if (marker.status == marker_navigation_status_t::no_result)
        return compose_action_hint(context, direction == marker_navigation_direction_t::previous
            ? "No previous audio-time bookmark or section boundary."
            : "No next audio-time bookmark or section boundary.", notification_source_t::audio);
    if (!marker || !marker.item->audio_milliseconds)
        return marker.diagnostics.empty()
            ? compose_action_warning(context, "Audio-time marker navigation could not resolve an exact millisecond.", notification_source_t::audio)
            : publish_compose_diagnostic(context, marker.diagnostics.front(), notification_source_t::audio);
    const auto status = app.transport_seek_audio_milliseconds(*marker.item->audio_milliseconds);
    return app.transport_seek_posts_feedback ? status
        : compose_action_hint(context, status, notification_source_t::audio);
}

std::string search_selected_note_labels(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before searching labels.");
    const auto step = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL
                          ? label_search_step_t::next
                      : context.action.compose_action ==
                              WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL
                          ? label_search_step_t::previous
                          : label_search_step_t::first;
    auto severity = notification_severity_t::warning;
    auto status = run_label_search(context.history, context.state, context.action.compose_note_label,
                                   step, &severity);
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::input);
}

auto selection_transform_for(WormUiComposeActionKind kind) -> std::optional<selection_transform_t> {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR: return selection_transform_t::mirror;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT: return selection_transform_t::rotate_left;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT: return selection_transform_t::rotate_right;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER: return selection_transform_t::shift_earlier;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER: return selection_transform_t::shift_later;
    default: return std::nullopt;
    }
}

std::string transform_selected_notes(compose_action_context_t& context,
                                     selection_transform_t transform) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before transforming notes.");
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return compose_action_warning(context, "Transform ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    auto status = apply_selection_transform(context.history, context.state, transform, &severity);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Transform applied, but workspace rebinding failed.";
        severity = notification_severity_t::error;
        return publish_compose_feedback(context, std::move(status), severity, notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
}

auto cursor_measure_command_for(WormUiComposeActionKind kind)
    -> std::optional<cursor_measure_command_t> {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_BEFORE: return cursor_measure_command_t::insert_before;
    case WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER: return cursor_measure_command_t::insert_after;
    case WORM_UI_COMPOSE_ACTION_REMOVE_EMPTY_MEASURE: return cursor_measure_command_t::remove_empty;
    default: return std::nullopt;
    }
}

std::string edit_cursor_measure(compose_action_context_t& context, cursor_measure_command_t command) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return compose_action_warning(context, "Measure edit ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    auto status = apply_cursor_measure_command(context.history, context.state, command, &severity);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Measure edited, but workspace rebinding failed.";
        severity = notification_severity_t::error;
        return publish_compose_feedback(context, std::move(status), severity, notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
}

// The form's recipe and the stored recipe it edits, when the action still matches the document.
struct recipe_form_target_t {
    const chart_document_t* chart{};
    const sv_recipe_t* recipe{};
};

auto recipe_form_target(compose_action_context_t& context) -> std::optional<recipe_form_target_t> {
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return std::nullopt;
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end() ||
        chart->id.value != context.action.compose_input_chart_id)
        return std::nullopt;
    const auto recipe = std::ranges::find(chart->sv_recipes,
                                          recipe_id_t{context.action.compose_recipe_id},
                                          &sv_recipe_t::id);
    if (recipe == chart->sv_recipes.end()) return std::nullopt;
    return recipe_form_target_t{&*chart, &*recipe};
}

// Shows the unapplied form candidate in the lane. A malformed form or a candidate the core
// validator rejects keeps its message for the form and draws no graph.
std::string preview_recipe_form_action(compose_action_context_t& context) {
    const auto target = recipe_form_target(context);
    if (!target) return compose_action_warning(context, "SV recipe form ignored because the recipe or the document changed.");
    auto preview = wormedit::sys::preview_recipe_form(
        *target->chart, context.history.document.revision, *target->recipe,
        context.action.compose_recipe_form);
    const auto status = preview.error.empty() ? std::string{"SV recipe preview updated."} : preview.error;
    const auto severity = preview.error.empty() ? notification_severity_t::hint : preview.severity;
    const auto source = preview.error.empty() ? notification_source_t::input : notification_source_t::edit;
    context.application_state.recipe_preview = std::move(preview);
    return publish_compose_feedback(context, status, severity, source);
}

std::string apply_recipe_form_action(compose_action_context_t& context) {
    const auto target = recipe_form_target(context);
    if (!target) return compose_action_warning(context, "SV recipe form ignored because the recipe or the document changed.");
    if (context.application_state.timing_locked) return compose_action_warning(context, "Timing is locked.");
    bool applied = false;
    auto severity = notification_severity_t::warning;
    auto status = wormedit::sys::apply_recipe_form(context.history, target->chart->id,
                                                   target->recipe->id,
                                                   context.action.compose_recipe_form, applied, &severity);
    if (!applied) return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
    context.application_state.recipe_preview.reset();
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "SV recipe updated, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
}

std::string set_cursor_measure_meter(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return compose_action_warning(context, "Meter edit ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    auto status = apply_cursor_measure_meter(context.history, context.state,
                                             context.action.timing_meter_numerator_text,
                                             context.action.timing_meter_denominator_text, &severity);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Meter edited, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
}

std::string set_measures_meter(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return compose_action_warning(context, "Meter edit ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    const auto ids = std::span<const std::uint64_t>{context.action.compose_measure_ids,
                                                    context.action.compose_measure_id_count};
    auto status = apply_measures_meter(context.history, context.state, ids,
                                       context.action.timing_meter_numerator_text,
                                       context.action.timing_meter_denominator_text, &severity);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Meter edited, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
}

std::string move_bar_line(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.history.document.revision != context.action.compose_input_document_revision ||
        context.action.compose_measure_id_count != 1 || !context.action.compose_measure_ids)
        return compose_action_warning(context, "Bar line move ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    const auto& action = context.action;
    const auto parse = [](const char* text) { return static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10)); };
    auto status = apply_bar_line_move(
        context.history, context.state, action.compose_measure_ids[0],
        {parse(action.timing_meter_numerator_text), parse(action.timing_meter_denominator_text)},
        {action.compose_second_meter_numerator, action.compose_second_meter_denominator}, &severity);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Bar line moved, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity, notification_source_t::edit);
}

std::optional<std::string> dispatch_compose_marker_action(compose_action_context_t& context) {
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE)
        return move_bar_line(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER)
        return set_measures_meter(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER)
        return set_cursor_measure_meter(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM)
        return preview_recipe_form_action(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM)
        return apply_recipe_form_action(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW) {
        context.application_state.recipe_preview.reset();
        return compose_action_hint(context, "SV recipe preview cleared.");
    }
    if (const auto measure_command = cursor_measure_command_for(context.action.compose_action))
        return edit_cursor_measure(context, *measure_command);
    if (const auto transform = selection_transform_for(context.action.compose_action))
        return transform_selected_notes(context, *transform);
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL:
        return search_selected_note_labels(context);
    case WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL:
        return edit_selected_note_labels(context);
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK:
        return dispatch_compose_beat_bookmark_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION:
        return dispatch_compose_beat_section_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK:
        return dispatch_compose_audio_bookmark_action(context);
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER:
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER:
        return navigate_audio_marker(context);
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return dispatch_compose_audio_section_action(context);
    default:
        return std::nullopt;
    }
}

auto catalog_semantic_layer_by_id(const wormedit::chart_document_t& chart,
                                  wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t* {
    const auto found = std::ranges::find(chart.semantic_layers, id, &wormedit::semantic_layer_t::id);
    return found == chart.semantic_layers.end() ? nullptr : &*found;
}

std::optional<std::string> assign_compose_layer_target(compose_action_context_t& context,
                                                       bool none, wormedit::layer_id_t target) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing semantic layers.");
    auto& app = context.application_state;
    const auto applied = wormedit::sys::apply_compose_layer_assignment(
        context.history, context.state, context.action.compose_input_document_revision,
        none, target, app.semantic_layer_target_none, app.semantic_layer_target);
    if (!applied.applied && !applied.diagnostics.empty()) return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (none) app.semantic_layer_solo_active = false;
    if (!applied.applied) return compose_action_info(context, "Semantic-layer membership was unchanged.", notification_source_t::edit);
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        static_cast<void>(compose_action_success(context, none ? "Selected objects assigned to No layer."
                                                               : "Semantic-layer membership updated."));
        return compose_action_error(context, "Semantic-layer membership changed, but workspace rebinding failed.", notification_source_t::document);
    }
    return compose_action_success(context, none ? "Selected objects assigned to No layer."
                                                : "Semantic-layer membership updated.");
}

std::optional<std::string> dispatch_compose_note_menu_action(compose_action_context_t& context) {
    using namespace wormedit;
    const auto& action = context.action;
    auto& app = context.application_state;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU: {
        if (editor_interaction_has_pending_gesture(context.state))
            return compose_action_warning(context, "Finish or cancel the active Compose gesture before opening the note menu.");
        const auto result = select_compose_note_context(context.history, context.state,
            action.compose_input_document_revision, object_id_t{action.compose_note_context_id});
        if (!result.accepted) return compose_interaction_feedback(context, result, "Note could not be selected.");
        app.note_layer_menu_chart = context.state.chart;
        app.note_layer_menu_revision = context.history.document.revision;
        app.note_layer_menu_first_row = 0;
        app.note_layer_menu_row_count = WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY;
        return "";
    }
    case WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW:
        if (app.note_layer_menu_chart != context.state.chart ||
            app.note_layer_menu_revision != context.history.document.revision) return "";
        app.note_layer_menu_first_row = action.compose_semantic_layer_first_row;
        app.note_layer_menu_row_count = std::clamp<std::size_t>(
            action.compose_semantic_layer_row_count, 1, WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY);
        return "";
    case WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU:
        app.note_layer_menu_row_count = 0;
        return "";
    case WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER:
        app.note_layer_menu_row_count = 0;
        return assign_compose_layer_target(context, action.compose_semantic_layer_id == 0,
            layer_id_t{action.compose_semantic_layer_id});
    default: return std::nullopt;
    }
}

std::optional<std::string> choose_no_semantic_layer(compose_action_context_t& context) {
    if (!selected_chart(context.history, context.workspace)) return compose_action_warning(context, "Choose a Compose chart first.");
    if (!context.state.selected_notes.empty() ||
        !selected_compose_bgm_events(context.state).empty())
        return assign_compose_layer_target(context, true, {});
    auto& app = context.application_state;
    app.semantic_layer_target.reset();
    app.semantic_layer_target_none = true;
    app.semantic_layer_solo_active = false;
    return compose_action_info(context, "Semantic-layer target: No layer");
}

std::optional<std::string> dispatch_semantic_layer_palette_action(
    compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& application_state = context.application_state;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW:
        application_state.semantic_layer_first_row = action.compose_semantic_layer_first_row;
        application_state.semantic_layer_row_count =
            std::clamp<std::size_t>(action.compose_semantic_layer_row_count, 1,
                                    WORM_UI_COMPOSE_SEMANTIC_LAYER_WINDOW_CAPACITY);
        return "";
    case WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER:
        return choose_no_semantic_layer(context);
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || action.compose_semantic_layer_id == 0)
            return compose_action_warning(context, "Choose a visible semantic layer from the current Compose palette.");
        const auto* layer = catalog_semantic_layer_by_id(
            *chart, layer_id_t{action.compose_semantic_layer_id});
        if (!layer) return compose_action_warning(context, "That semantic-layer palette entry is no longer visible.");
        application_state.semantic_layer_target = layer->id;
        application_state.semantic_layer_target_none = false;
        return compose_action_info(context, "Semantic-layer target: " + layer->name);
    }
    case WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE:
    case WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart) return compose_action_warning(context, "Choose a chart before navigating semantic-layer pages.");
        if (application_state.semantic_layer_palette_chart != chart->id) {
            application_state.semantic_layer_palette_chart = chart->id;
            application_state.semantic_layer_palette_page = 0;
        }
        const auto palette = make_semantic_layer_palette_window(
            *chart, application_state.semantic_layer_target,
            application_state.semantic_layer_palette_page, false,
            WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
        if (!palette) return compose_action_warning(context, "Semantic-layer palette navigation exceeded its bounded limit.");
        application_state.semantic_layer_palette_page = palette->page;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE) {
            if (application_state.semantic_layer_palette_page == 0)
                return compose_action_hint(context, "Semantic-layer palette is already on its first page.");
            --application_state.semantic_layer_palette_page;
        } else {
            if (palette->page_count == 0 ||
                application_state.semantic_layer_palette_page + 1 >= palette->page_count)
                return compose_action_hint(context, "Semantic-layer palette is already on its last page.");
            ++application_state.semantic_layer_palette_page;
        }
        return compose_action_hint(context, "Semantic-layer palette page " +
               std::to_string(application_state.semantic_layer_palette_page + 1) + " of " +
               std::to_string(palette->page_count) + ".");
    }
    default:
        return std::nullopt;
    }
}

std::optional<std::string> semantic_layer_edit_status(
    compose_action_context_t& context, wormedit::semantic_layer_edit_action_t edit,
    std::string_view success, std::string_view unchanged, std::string_view rebind_failure) {
    const bool inserting = std::holds_alternative<wormedit::insert_semantic_layer_action_t>(edit);
    const auto applied = wormedit::apply_semantic_layer_edit(
        context.history, context.action.compose_input_document_revision, std::move(edit));
    if (!applied.applied)
        return applied.diagnostics.empty()
            ? std::optional<std::string>{compose_action_info(context, std::string{unchanged}, notification_source_t::edit)}
            : std::optional<std::string>{publish_compose_diagnostic(
                  context, applied.diagnostics.front())};
    if (inserting) {
        const auto* chart = selected_chart(context.history, context.workspace);
        if (chart && !chart->semantic_layers.empty()) {
            context.application_state.semantic_layer_target = chart->semantic_layers.back().id;
            context.application_state.semantic_layer_target_none = false;
        }
    }
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (rebound) return compose_action_success(context, std::string{success});
    return compose_mutation_rebind_failure(context, rebound, std::string{success},
                                           std::string{rebind_failure});
}

std::optional<std::string> add_semantic_layer(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context,
            "Finish or cancel the active Compose gesture before adding a semantic layer.");
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return compose_action_warning(context, "Choose a chart before adding a semantic layer.");
    std::string name = "Layer";
    for (std::size_t suffix = 2; std::ranges::any_of(
             chart->semantic_layers, [&](const auto& item) { return item.name == name; }); ++suffix)
        name = "Layer " + std::to_string(suffix);
    constexpr std::array<semantic_layer_rgba_t, 6> colors{{
        {0.91, 0.35, 0.31, 1.0}, {0.95, 0.67, 0.24, 1.0},
        {0.36, 0.74, 0.36, 1.0}, {0.25, 0.62, 0.91, 1.0},
        {0.58, 0.42, 0.84, 1.0}, {0.87, 0.38, 0.68, 1.0},
    }};
    return semantic_layer_edit_status(
        context, insert_semantic_layer_action_t{
                    context.state.chart, std::move(name),
                    colors[chart->semantic_layers.size() % colors.size()]},
        "Semantic layer added.", "Semantic layer was not added.",
        "Semantic layer added, but workspace rebinding failed.");
}

auto selected_catalog_layer(compose_action_context_t& context)
    -> std::pair<const wormedit::chart_document_t*, const wormedit::semantic_layer_t*> {
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart || !context.application_state.semantic_layer_target) return {chart, nullptr};
    return {chart, catalog_semantic_layer_by_id(
                       *chart, *context.application_state.semantic_layer_target)};
}

std::optional<std::string> update_semantic_layer_catalog_entry(
    compose_action_context_t& context) {
    using namespace wormedit;
    const bool recolor = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, recolor
            ? "Finish or cancel the active Compose gesture before recoloring a semantic layer."
            : "Finish or cancel the active Compose gesture before renaming a semantic layer.");
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer)
        return !chart || !context.application_state.semantic_layer_target
                   ? compose_action_warning(context, recolor ? "Choose a visible semantic layer before recoloring it."
                              : "Choose a visible semantic layer before renaming it.")
                   : compose_action_warning(context, "The selected semantic layer is no longer visible.");
    const auto color = recolor
        ? semantic_layer_rgba_t{static_cast<double>(context.action.compose_semantic_layer_red) / 255.0,
                                static_cast<double>(context.action.compose_semantic_layer_green) / 255.0,
                                static_cast<double>(context.action.compose_semantic_layer_blue) / 255.0,
                                static_cast<double>(context.action.compose_semantic_layer_alpha) / 255.0}
        : semantic_layer_rgba_t{static_cast<double>(layer->color.red) / 255.0,
                                static_cast<double>(layer->color.green) / 255.0,
                                static_cast<double>(layer->color.blue) / 255.0,
                                static_cast<double>(layer->color.alpha) / 255.0};
    const auto name = recolor ? layer->name : context.action.compose_semantic_layer_name;
    return semantic_layer_edit_status(
        context, update_semantic_layer_action_t{context.state.chart, layer->id, name, color,
                                                layer->visible, layer->locked},
        recolor ? "Semantic-layer color changed." : "Semantic layer renamed.",
        recolor ? "Semantic-layer color was unchanged." : "Semantic-layer name was unchanged.",
        recolor ? "Semantic-layer color changed, but workspace rebinding failed."
                : "Semantic layer renamed, but workspace rebinding failed.");
}

std::optional<std::string> delete_semantic_layer_catalog_entry(
    compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before deleting a semantic layer.");
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return compose_action_warning(context, "Choose a visible semantic layer before deleting it.");
    if (!layer) return compose_action_warning(context, "The selected semantic layer is no longer visible.");
    const auto id = layer->id;
    const auto applied = apply_semantic_layer_edit(
        context.history, context.action.compose_input_document_revision,
        delete_semantic_layer_action_t{context.state.chart, id,
            context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS});
    if (!applied.applied) return applied.diagnostics.empty()
        ? compose_action_info(context, "Semantic layer was not deleted.", notification_source_t::edit)
        : publish_compose_diagnostic(context, applied.diagnostics.front());
    auto& state = context.application_state;
    std::erase(state.required_semantic_layer_filters, id);
    std::erase(state.excluded_semantic_layer_filters, id);
    state.semantic_layer_target.reset();
    state.semantic_layer_target_none = false;
    state.semantic_layer_paint_mode_active = false;
    state.semantic_layer_solo_active = false;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (rebound) return compose_action_success(context, "Semantic layer deleted.");
    return compose_mutation_rebind_failure(context, rebound, "Semantic layer deleted.",
        "Semantic layer deleted, but workspace rebinding failed.");
}

std::optional<std::string> move_semantic_layer_catalog_entry(
    compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before reordering semantic layers.");
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer)
        return !chart || !context.application_state.semantic_layer_target
                   ? compose_action_warning(context, "Choose a visible semantic layer before reordering it.")
                   : compose_action_warning(context, "The selected semantic layer is no longer available.");
    const auto found = std::ranges::find(chart->semantic_layers, layer->id, &semantic_layer_t::id);
    const auto index = static_cast<std::size_t>(std::distance(chart->semantic_layers.begin(), found));
    const bool up = context.action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
    if ((up && index == 0) || (!up && index + 1 == chart->semantic_layers.size()))
        return compose_action_info(context, "Semantic layer is already at that palette boundary.", notification_source_t::edit);
    std::vector<layer_id_t> order;
    order.reserve(chart->semantic_layers.size());
    for (const auto& item : chart->semantic_layers) order.push_back(item.id);
    std::swap(order[index], order[up ? index - 1 : index + 1]);
    return semantic_layer_edit_status(
        context, reorder_semantic_layers_action_t{context.state.chart, std::move(order)},
        "Semantic-layer order updated.", "Semantic-layer order was unchanged.",
        "Semantic-layer order updated, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_semantic_layer_catalog_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW:
    case WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE:
    case WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE:
        return dispatch_semantic_layer_palette_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER:
        return add_semantic_layer(context);
    case WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR:
        return update_semantic_layer_catalog_entry(context);
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS:
        return delete_semantic_layer_catalog_entry(context);
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN:
        return move_semantic_layer_catalog_entry(context);
    case WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER:
    default:
        return std::nullopt;
    }
}

void clear_semantic_layer_selection(application_command_state_t& app, bool clear_modes) {
    app.semantic_layer_target.reset();
    app.semantic_layer_target_none = false;
    if (!clear_modes) return;
    app.semantic_layer_paint_mode_active = false;
    app.semantic_layer_solo_active = false;
}

auto target_semantic_layer_action_row(compose_action_context_t& context) -> bool {
    using namespace wormedit;
    if (context.action.compose_semantic_layer_id == 0) return true;
    const auto* chart = selected_chart(context.history, context.workspace);
    const auto* layer = chart ? catalog_semantic_layer_by_id(
        *chart, layer_id_t{context.action.compose_semantic_layer_id}) : nullptr;
    if (!layer) return false;
    context.application_state.semantic_layer_target = layer->id;
    context.application_state.semantic_layer_target_none = false;
    return true;
}

auto semantic_layer_menu_action(WormUiComposeActionKind kind) -> bool {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO:
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES:
        return true;
    default: return false;
    }
}

auto locked_semantic_layer_menu_action(WormUiComposeActionKind kind) -> bool {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER:
        return true;
    default: return false;
    }
}

auto target_captured_semantic_layer_action(compose_action_context_t& context)
    -> std::optional<std::string> {
    const auto kind = context.action.compose_action;
    if (!semantic_layer_menu_action(kind) || context.action.compose_semantic_layer_id == 0)
        return std::nullopt;
    const auto* chart = selected_chart(context.history, context.workspace);
    const auto* layer = chart ? catalog_semantic_layer_by_id(
        *chart, wormedit::layer_id_t{context.action.compose_semantic_layer_id}) : nullptr;
    if (!layer) return compose_action_warning(context, "The selected semantic layer is no longer available.");
    if (layer->locked && locked_semantic_layer_menu_action(kind)) return compose_action_warning(context, "Layer is locked");
    (void)target_semantic_layer_action_row(context);
    return std::nullopt;
}

auto missing_semantic_layer_toggle_target(compose_action_context_t& context, bool toggle_lock)
    -> std::optional<std::string> {
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return toggle_lock ? compose_action_warning(context, "Choose a semantic layer before changing its lock state.")
                           : compose_action_warning(context, "Choose a semantic layer before changing its visibility.");
    if (!layer) {
        clear_semantic_layer_selection(context.application_state, toggle_lock);
        return compose_action_warning(context, "The selected semantic layer is no longer available.");
    }
    return std::nullopt;
}

auto semantic_layer_toggle_status(bool toggle_lock, bool previous) -> std::string {
    if (toggle_lock) return previous ? "Semantic layer unlocked." : "Semantic layer locked.";
    return previous ? "Semantic layer hidden." : "Semantic layer shown.";
}

std::optional<std::string> toggle_semantic_layer_visibility_or_lock(
    compose_action_context_t& context, bool toggle_lock) {
    using namespace wormedit;
    auto& app = context.application_state;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, toggle_lock
            ? "Finish or cancel the active Compose gesture before changing layer lock state."
            : "Finish or cancel the active Compose gesture before changing layer visibility.");
    if (!target_semantic_layer_action_row(context))
        return compose_action_warning(context, "The selected semantic layer is no longer available.");
    if (const auto missing = missing_semantic_layer_toggle_target(context, toggle_lock))
        return missing;
    const auto [chart, layer] = selected_catalog_layer(context);
    const bool previous = toggle_lock ? layer->locked : layer->visible;
    const auto color = semantic_layer_rgba_t{
        static_cast<double>(layer->color.red) / 255.0,
        static_cast<double>(layer->color.green) / 255.0,
        static_cast<double>(layer->color.blue) / 255.0,
        static_cast<double>(layer->color.alpha) / 255.0};
    const auto applied = apply_semantic_layer_edit(
        context.history, context.action.compose_input_document_revision,
        update_semantic_layer_action_t{context.state.chart, layer->id, layer->name, color,
                                       toggle_lock ? layer->visible : !previous,
                                       toggle_lock ? !previous : layer->locked});
    if (!applied.applied) return applied.diagnostics.empty()
        ? compose_action_info(context, "Semantic-layer state was unchanged.", notification_source_t::edit)
        : publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!toggle_lock && previous) {
        app.semantic_layer_paint_mode_active = false;
        app.semantic_layer_solo_active = false;
    }
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) return compose_mutation_rebind_failure(context, rebound,
        semantic_layer_toggle_status(toggle_lock, previous),
        "Semantic-layer state changed, but workspace rebinding failed.",
        notification_source_t::edit, notification_severity_t::info);
    if (toggle_lock && !previous) app.semantic_layer_paint_mode_active = false;
    return compose_action_info(context, semantic_layer_toggle_status(toggle_lock, previous),
                               notification_source_t::edit);
}

std::optional<std::string> toggle_semantic_layer_filter(compose_action_context_t& context) {
    using namespace wormedit;
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer) return compose_action_warning(context, "Choose a visible semantic layer before filtering by it.");
    auto& app = context.application_state;
    if (app.semantic_layer_filter_chart != chart->id) {
        app.semantic_layer_filter_chart = chart->id;
        app.required_semantic_layer_filters.clear();
        app.excluded_semantic_layer_filters.clear();
    }
    const bool require =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
    auto& enabled = require ? app.required_semantic_layer_filters : app.excluded_semantic_layer_filters;
    auto& opposite = require ? app.excluded_semantic_layer_filters : app.required_semantic_layer_filters;
    if (const auto found = std::ranges::find(enabled, layer->id); found != enabled.end()) {
        enabled.erase(found);
        return compose_action_info(context, require ? "Required semantic-layer filter removed."
                       : "Excluded semantic-layer filter removed.");
    }
    std::erase(opposite, layer->id);
    enabled.push_back(layer->id);
    return compose_action_info(context, require ? "Semantic layer required in the view filter."
                   : "Semantic layer excluded from the view filter.");
}

std::optional<std::string> toggle_semantic_layer_paint_mode(compose_action_context_t& context) {
    using namespace wormedit;
    auto& state = context.state;
    auto& app = context.application_state;
    if (editor_interaction_has_pending_gesture(state)) {
        const auto result = dispatch_editor_action(context.history, state, cancel_editor_gesture_action_t{});
        if (!result.accepted) return compose_interaction_feedback(context, result, "Compose gesture canceled.");
    }
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || (!app.semantic_layer_target_none && (!layer || layer->locked))) {
        app.semantic_layer_paint_mode_active = false;
        return compose_action_warning(context, "Choose an unlocked visible semantic layer before painting note membership.");
    }
    app.semantic_layer_paint_mode_active = !app.semantic_layer_paint_mode_active;
    if (app.semantic_layer_paint_mode_active) app.note_paint_mode_active = false;
    return compose_action_info(context, app.semantic_layer_paint_mode_active ? "Semantic-layer paint mode enabled."
                                                 : "Semantic-layer paint mode disabled.");
}

std::optional<std::string> toggle_semantic_layer_solo(compose_action_context_t& context) {
    using namespace wormedit;
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return compose_action_warning(context, "Choose a visible semantic layer before soloing it.");
    if (!layer) {
        context.application_state.semantic_layer_solo_active = false;
        return compose_action_warning(context, "The selected semantic layer is no longer visible.");
    }
    auto& solo = context.application_state.semantic_layer_solo_active;
    solo = !solo;
    return compose_action_info(context, solo ? "Semantic-layer solo enabled." : "Semantic-layer solo disabled.");
}

std::optional<std::string> select_semantic_layer_notes(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before selecting by layer.");
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return compose_action_warning(context, "Choose a visible semantic layer before selecting its notes.");
    if (!layer) {
        context.application_state.semantic_layer_target.reset();
        context.application_state.semantic_layer_target_none = false;
        context.application_state.semantic_layer_paint_mode_active = false;
        context.application_state.semantic_layer_solo_active = false;
        return compose_action_warning(context, "The selected semantic layer is no longer visible.");
    }
    std::vector<object_id_t> notes;
    for (const auto& measure : chart->measures)
        for (const auto& note : measure.notes)
            if (std::ranges::find(note.semantic_layers, layer->id) != note.semantic_layers.end())
                notes.push_back(note.id);
    const auto result = dispatch_editor_action(
        context.history, context.state, select_editor_notes_action_t{std::move(notes)});
    return compose_interaction_feedback(context, result, "Semantic-layer notes selected.");
}

std::optional<std::string> edit_semantic_layer_membership(compose_action_context_t& context) {
    using namespace wormedit;
    auto& state = context.state;
    auto& app = context.application_state;
    if (editor_interaction_has_pending_gesture(state))
        return compose_action_warning(context, "Finish or cancel the active Compose gesture before editing semantic layers.");
    const auto apply = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER;
    const auto explicit_id = context.action.compose_semantic_layer_id;
    const auto candidate_none = explicit_id == 0 && app.semantic_layer_target_none;
    if (candidate_none) {
        if (!apply) return compose_action_info(context, "No layer only assigns");
        return assign_compose_layer_target(context, true, {});
    }
    const auto* chart = selected_chart(context.history, context.workspace);
    const auto candidate = explicit_id ? layer_id_t{explicit_id} : app.semantic_layer_target.value_or(layer_id_t{});
    const auto* layer = chart ? catalog_semantic_layer_by_id(*chart, candidate) : nullptr;
    if (!chart || !layer ||
        (state.selected_notes.empty() && selected_compose_bgm_events(state).empty()))
        return compose_action_warning(context, "Choose a semantic layer and select notes or BGM events first.");
    if (apply) return assign_compose_layer_target(context, false, candidate);
    return semantic_layer_edit_status(context,
        replace_notes_semantic_layer_action_t{
            state.chart, candidate, state.selected_notes, true,
            selected_compose_bgm_events(state)},
        "Semantic-layer membership updated.", "Semantic-layer membership was unchanged.",
        "Semantic-layer membership changed, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_semantic_layer_state_action(
    compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY:
        return toggle_semantic_layer_visibility_or_lock(context, false);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK:
        return toggle_semantic_layer_visibility_or_lock(context, true);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER:
        return toggle_semantic_layer_filter(context);
    case WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER:
        context.application_state.semantic_layer_target.reset();
        context.application_state.semantic_layer_target_none = false;
        context.application_state.semantic_layer_paint_mode_active = false;
        context.application_state.semantic_layer_solo_active = false;
        return compose_action_info(context, "Semantic-layer target cleared.");
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE:
        return toggle_semantic_layer_paint_mode(context);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO:
        return toggle_semantic_layer_solo(context);
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES:
        return select_semantic_layer_notes(context);
    case WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER:
        return edit_semantic_layer_membership(context);
    default:
        return std::nullopt;
    }
}

std::string dispatch_bms_branch_choice_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    notification_sink_t notifications) {
    using namespace wormedit;
    const auto warning = [&](std::string message) {
        notifications.post(notification_severity_t::warning, notification_source_t::format, message);
        return message;
    };
    const auto diagnostic = [&](const auto& value) {
        notifications.post(compose_notification_severity(value.severity),
                           notification_source_t::format, value.message);
        return value.message;
    };
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return warning("BMS branch action ignored because the chart or document revision changed.");
    }
    if (interaction_state && editor_interaction_has_pending_gesture(*interaction_state)) {
        return warning("Finish or cancel the active Compose gesture before changing a BMS branch.");
    }
    if (action.compose_branch_id == 0) {
        return warning("Choose a current BMS RANDOM or SWITCH control first.");
    }
    const auto applied = apply_bms_branch_choice_command(
        history, action.compose_input_document_revision,
        {.chart = *workspace.selected_chart,
         .branch = branch_id_t{action.compose_branch_id},
         .selected_value = action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
                               ? std::optional<std::int64_t>{action.compose_branch_value}
                               : std::nullopt});
    if (!applied) {
        if (!applied.command.diagnostics.empty()) {
            return diagnostic(applied.command.diagnostics.front());
        }
        if (!applied.format_diagnostics.empty()) {
            return diagnostic(applied.format_diagnostics.front());
        }
        if (!applied.write_diagnostics.empty()) {
            return diagnostic(applied.write_diagnostics.front());
        }
        if (!applied.parse_diagnostics.empty()) {
            return diagnostic(applied.parse_diagnostics.front());
        }
        return warning("BMS branch choice was rejected without changing the document.");
    }
    const auto success = action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
               ? "BMS branch choice changed and its semantics were rematerialized."
               : "BMS branch choice cleared and its semantics were rematerialized.";
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) {
        notifications.post(notification_severity_t::success, notification_source_t::edit, success);
        if (!rebound.diagnostics.empty()) {
            const auto& diagnostic = rebound.diagnostics.front();
            notifications.post(compose_notification_severity(diagnostic.severity),
                               notification_source_t::document, diagnostic.message);
            return diagnostic.message;
        }
        auto message = std::string{"BMS branch changed, but workspace rebinding failed."};
        notifications.post(notification_severity_t::error, notification_source_t::document, message);
        return message;
    }
    auto message = success;
    notifications.post(notification_severity_t::success, notification_source_t::edit, message);
    return message;
}

struct bms_statement_target_t {
    const wormedit::bms_control_node_t* statement{};
    const wormedit::bms_control_node_t* leaf{};
};

auto bms_statement_leaf(wormedit::bms_control_kind_t kind) -> bool {
    using kind_t = wormedit::bms_control_kind_t;
    return kind == kind_t::if_branch || kind == kind_t::else_if_branch ||
           kind == kind_t::else_branch || kind == kind_t::case_branch ||
           kind == kind_t::default_branch;
}

template <typename diagnostic_t_>
auto publish_bms_diagnostic(notification_sink_t notifications,
                            const diagnostic_t_& diagnostic,
                            notification_source_t source = notification_source_t::format)
    -> std::string {
    notifications.post(compose_notification_severity(diagnostic.severity), source,
                       diagnostic.message);
    return diagnostic.message;
}

template <typename result_t_>
auto publish_bms_statement_diagnostic(notification_sink_t notifications,
                                      const result_t_& result) -> std::string {
    if (!result.command.diagnostics.empty())
        return publish_bms_diagnostic(notifications, result.command.diagnostics.front());
    if (!result.format_diagnostics.empty())
        return publish_bms_diagnostic(notifications, result.format_diagnostics.front());
    const std::string message{"BMS branch statement edit was rejected without changing the document."};
    notifications.post(notification_severity_t::warning, notification_source_t::format, message);
    return message;
}

void find_bms_statement_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                               const wormedit::bms_control_node_t* enclosing_leaf,
                               bms_statement_target_t& result) {
    const auto* leaf = bms_statement_leaf(node.kind) ? &node : enclosing_leaf;
    if (node.kind == wormedit::bms_control_kind_t::raw_statement && node.id.value == id) {
        result = {&node, leaf};
        return;
    }
    for (const auto& child : node.children) {
        find_bms_statement_target(child, id, leaf, result);
        if (result.statement) return;
    }
}

void retain_bms_statement_feedback(application_command_state_t& application,
                                   std::optional<wormedit::chart_id_t> chart,
                                   std::uint64_t revision, const std::string& message) {
    if (chart) {
        application.bms_branch_statement_feedback =
            application_command_state_t::bms_branch_statement_feedback_t{
                *chart, revision, message};
    } else {
        application.bms_branch_statement_feedback.reset();
    }
}

std::string dispatch_bms_branch_statement_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    notification_sink_t notifications) {
    using namespace wormedit;
    const auto warning = [&](std::string message) {
        notifications.post(notification_severity_t::warning, notification_source_t::format, message);
        return message;
    };
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id)
        return warning("BMS branch statement action ignored because the selected chart changed.");
    if (interaction_state && editor_interaction_has_pending_gesture(*interaction_state))
        return warning("Finish or cancel the active Compose gesture before editing a BMS branch.");
    if (action.compose_branch_statement_id == 0)
        return warning("Select a raw BMS branch statement first.");
    if (history.document.revision != action.compose_input_document_revision) {
        const auto stale = apply_bms_branch_statement_edit_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .statement = branch_id_t{action.compose_branch_statement_id}});
        return publish_bms_statement_diagnostic(notifications, stale);
    }
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end())
        return warning("The selected BMS chart is no longer available.");
    if (!chart->bms_control_tree) {
        const auto missing_tree = apply_bms_branch_statement_edit_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .statement = branch_id_t{action.compose_branch_statement_id}});
        return publish_bms_statement_diagnostic(notifications, missing_tree);
    }
    bms_statement_target_t target;
    find_bms_statement_target(*chart->bms_control_tree, action.compose_branch_statement_id,
                              nullptr, target);
    if (!target.statement) {
        const auto missing_target = apply_bms_branch_statement_edit_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .statement = branch_id_t{action.compose_branch_statement_id}});
        return publish_bms_statement_diagnostic(notifications, missing_target);
    }

    constexpr std::size_t maximum_text_bytes = 4096;
    const bool has_text_action =
        action.compose_action == WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT;
    if (has_text_action && (!action.compose_branch_statement_text ||
                            action.compose_branch_statement_text_bytes == 0 ||
                            action.compose_branch_statement_text_bytes > maximum_text_bytes))
        return warning("BMS branch statement text is empty or exceeds the editor's bounded capacity.");
    const std::string text = has_text_action
        ? std::string(action.compose_branch_statement_text,
                      action.compose_branch_statement_text_bytes)
        : std::string{};
    if (!target.leaf && action.compose_action ==
                            WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT) {
        const auto invalid = apply_bms_branch_statement_insert_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .after_statement = target.statement->id,
             .expected_statement_source_order = target.statement->source_order,
             .expected_statement_source_span = target.statement->source_span,
             .expected_statement_original_text = target.statement->original_text,
             .statement_text = text});
        return publish_bms_statement_diagnostic(notifications, invalid);
    }
    if (!target.leaf && action.compose_action ==
                            WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT) {
        const auto invalid = apply_bms_branch_statement_delete_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .statement = target.statement->id,
             .expected_source_order = target.statement->source_order,
             .expected_source_span = target.statement->source_span,
             .expected_original_text = target.statement->original_text});
        return publish_bms_statement_diagnostic(notifications, invalid);
    }
    bms_branch_statement_command_result_t applied;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT:
        applied = apply_bms_branch_statement_edit_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .statement = target.statement->id,
             .expected_source_order = target.statement->source_order,
             .expected_source_span = target.statement->source_span,
             .expected_original_text = target.statement->original_text,
             .replacement_text = text});
        break;
    case WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT:
        applied = apply_bms_branch_statement_insert_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .leaf_branch = target.leaf->id,
             .expected_leaf_source_order = target.leaf->source_order,
             .expected_leaf_source_span = target.leaf->source_span,
             .expected_leaf_original_text = target.leaf->original_text,
             .after_statement = target.statement->id,
             .expected_statement_source_order = target.statement->source_order,
             .expected_statement_source_span = target.statement->source_span,
             .expected_statement_original_text = target.statement->original_text,
             .statement_text = text});
        break;
    case WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT:
        applied = apply_bms_branch_statement_delete_command(
            history, action.compose_input_document_revision,
            {.chart = *workspace.selected_chart,
             .leaf_branch = target.leaf->id,
             .expected_leaf_source_order = target.leaf->source_order,
             .expected_leaf_source_span = target.leaf->source_span,
             .expected_leaf_original_text = target.leaf->original_text,
             .statement = target.statement->id,
             .expected_source_order = target.statement->source_order,
             .expected_source_span = target.statement->source_span,
             .expected_original_text = target.statement->original_text});
        break;
    default:
        return warning("Unknown BMS branch statement action.");
    }
    if (!applied) return publish_bms_statement_diagnostic(notifications, applied);
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) {
        notifications.post(notification_severity_t::success, notification_source_t::edit,
                           "Inactive BMS branch statement updated.");
        if (!rebound.diagnostics.empty())
            return publish_bms_diagnostic(notifications, rebound.diagnostics.front(),
                                          notification_source_t::document);
        const auto message = std::string{"BMS branch statement changed, but workspace rebinding failed."};
        notifications.post(notification_severity_t::error, notification_source_t::document, message);
        return message;
    }
    const auto message = std::string{"Inactive BMS branch statement updated."};
    notifications.post(notification_severity_t::success, notification_source_t::edit, message);
    return message;
}

auto quantize_request_from_action(const WormUiWorkspaceAction& action,
                                 std::uint64_t fallback_divisions,
                                 const std::vector<wormedit::object_id_t>& objects)
    -> std::optional<wormedit::quantize_request_t> {
    using namespace wormedit;
    if (action.compose_quantize_space > static_cast<std::uint8_t>(quantization_space_t::authored_measure) ||
        action.compose_quantize_tie > static_cast<std::uint8_t>(quantization_tie_t::reject) ||
        action.compose_quantize_holds > static_cast<std::uint8_t>(quantization_hold_t::both_endpoints))
        return std::nullopt;
    const auto legacy = action.compose_quantize_divisions == 0;
    const auto divisions = legacy ? fallback_divisions : action.compose_quantize_divisions;
    if (divisions == 0) return std::nullopt;
    return quantize_request_t{.divisions = divisions,
                              .space = static_cast<quantization_space_t>(action.compose_quantize_space),
                              .tie = static_cast<quantization_tie_t>(action.compose_quantize_tie),
                              .collisions = quantization_collision_t::reject,
                              .holds = legacy ? quantization_hold_t::both_endpoints
                                              : static_cast<quantization_hold_t>(action.compose_quantize_holds),
                              .allow_resolution_promotion = false,
                              .objects = objects};
}

auto quantize_settings_valid(const wormedit::quantize_request_t& request) -> bool {
    using namespace wormedit;
    return request.divisions != 0 &&
           request.space <= quantization_space_t::authored_measure &&
           request.tie <= quantization_tie_t::reject &&
           request.holds <= quantization_hold_t::both_endpoints;
}

auto quantize_editor_for(compose_action_context_t& context)
    -> application_command_state_t::quantize_editor_t& {
    auto& editor = context.application_state.quantize_editor;
    if (!editor || editor->song != context.history.document.id || editor->chart != context.state.chart) {
        const auto request = wormedit::quantize_request_t{
            .divisions = context.state.active_snap_denominator};
        editor = application_command_state_t::quantize_editor_t{
            .song = context.history.document.id, .chart = context.state.chart, .request = request};
    }
    return *editor;
}

auto apply_quantize_settings_from_action(compose_action_context_t& context)
    -> std::optional<std::string> {
    auto& editor = quantize_editor_for(context);
    const auto request = quantize_request_from_action(context.action,
        context.state.active_snap_denominator, {});
    if (!request) return compose_action_warning(context,
        "Quantization settings must use a positive grid and supported policies.");
    editor.request = *request;
    editor.preset_index.reset();
    return compose_action_info(context, "Quantization settings updated.");
}

auto select_quantize_preset(compose_action_context_t& context) -> std::string {
    using namespace wormedit;
    const auto index = context.action.compose_quantize_preset_index;
    if (index >= context.history.document.quantization_presets.size())
        return compose_action_warning(context, "The selected quantization preset is no longer available.");
    const auto& preset = context.history.document.quantization_presets[index];
    auto& editor = quantize_editor_for(context);
    editor.request = {.divisions = preset.divisions, .space = preset.space, .tie = preset.tie,
                      .collisions = quantization_collision_t::reject, .holds = preset.holds};
    editor.preset_index = index;
    return compose_action_info(context, "Quantization preset selected.");
}

auto validate_quantize_preset_save(const compose_action_context_t& context,
                                   std::size_t name_length,
                                   const std::optional<wormedit::quantize_request_t>& request)
    -> std::optional<std::string> {
    using namespace wormedit;
    if (name_length == 0 || name_length >= sizeof(context.action.compose_quantize_preset_name))
        return "Enter a name for the quantization preset (maximum 64 bytes).";
    if (name_length > quantization_preset_t::maximum_name_bytes)
        return "Quantization preset names must be at most 64 bytes.";
    const std::string_view name(context.action.compose_quantize_preset_name, name_length);
    if (std::ranges::any_of(context.history.document.quantization_presets,
                            [&](const auto& preset) { return preset.name == name; }))
        return "A quantization preset already has that name.";
    if (context.history.document.quantization_presets.size() >= quantization_preset_t::maximum_count)
        return "The project already has the maximum number of quantization presets.";
    if (!request || !quantize_settings_valid(*request))
        return "Quantization settings are invalid.";
    return std::nullopt;
}

auto save_quantize_preset(compose_action_context_t& context) -> std::string {
    using namespace wormedit;
    const auto length = strnlen(context.action.compose_quantize_preset_name,
                                sizeof(context.action.compose_quantize_preset_name));
    const auto request = quantize_request_from_action(context.action,
        context.state.active_snap_denominator, {});
    if (const auto error = validate_quantize_preset_save(context, length, request))
        return compose_action_warning(context, *error);
    auto proposed = context.history.document;
    proposed.quantization_presets.push_back({
        .name = context.action.compose_quantize_preset_name,
        .divisions = request->divisions, .space = request->space,
        .tie = request->tie, .holds = request->holds});
    const auto index = proposed.quantization_presets.size() - 1;
    const auto committed = commit_document_command(context.history, std::move(proposed),
                                                   "Save quantization preset");
    if (!committed) return committed.diagnostics.empty()
        ? compose_action_warning(context, "Quantization preset could not be saved.")
        : publish_compose_diagnostic(context, committed.diagnostics.front());
    auto& editor = quantize_editor_for(context);
    editor.request = *request;
    editor.preset_index = index;
    return compose_action_success(context, "Quantization preset saved.");
}

auto delete_quantize_preset(compose_action_context_t& context) -> std::string {
    using namespace wormedit;
    const auto index = context.action.compose_quantize_preset_index;
    if (index >= context.history.document.quantization_presets.size())
        return compose_action_warning(context, "The selected quantization preset is no longer available.");
    auto proposed = context.history.document;
    proposed.quantization_presets.erase(proposed.quantization_presets.begin() +
                                        static_cast<std::ptrdiff_t>(index));
    const auto committed = commit_document_command(context.history, std::move(proposed),
                                                   "Delete quantization preset");
    if (!committed) return committed.diagnostics.empty()
        ? compose_action_warning(context, "Quantization preset could not be deleted.")
        : publish_compose_diagnostic(context, committed.diagnostics.front());
    auto& editor = quantize_editor_for(context);
    if (editor.preset_index) {
        if (*editor.preset_index == index) editor.preset_index.reset();
        else if (*editor.preset_index > index) --*editor.preset_index;
    }
    return compose_action_success(context, "Quantization preset deleted.");
}

auto dispatch_quantize_preset_action(compose_action_context_t& context)
    -> std::optional<std::string> {
    using namespace wormedit;
    const auto kind = context.action.compose_action;
    if (kind == WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS)
        return apply_quantize_settings_from_action(context);
    if (kind == WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET)
        return select_quantize_preset(context);
    if (kind == WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET) return save_quantize_preset(context);
    if (kind == WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET)
        return delete_quantize_preset(context);
    return std::nullopt;
}

auto dispatch_compose_area_with_quantize_report(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state,
    std::optional<notification_sink_t> notifications) -> std::optional<std::string> {
    using namespace wormedit;
    const auto revision = history.document.revision;
    std::optional<quantize_preview_t> report;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE) {
        const auto chart = std::ranges::find(history.document.charts, state.chart,
                                             &chart_document_t::id);
        const auto request = quantize_request_from_action(action, state.active_snap_denominator,
                                                          state.selected_notes);
        if (chart != history.document.charts.end() && request)
            report = preview_quantize_selection(*chart, state.selected_notes, *request);
    }
    const auto response = dispatch_compose_area_action(action, history, state, notifications);
    if (response && report && history.document.revision != revision) {
        application_state.quantize_applied_report =
            application_command_state_t::quantize_applied_report_t{
                .song = history.document.id, .chart = state.chart,
                .rows = report->report_rows, .total_rows = report->report_total_rows,
                .truncated = report->report_truncated};
    }
    return response;
}

} // namespace

auto dispatch_compose_remaining_action(compose_action_context_t& context)
    -> std::optional<std::string> {
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY) {
        if (editor_interaction_has_pending_gesture(context.state))
            return compose_action_warning(context, "Finish or cancel the active Compose gesture before changing the BGA lane.");
        context.state.bga_lane_visible = !context.state.bga_lane_visible;
        return compose_action_info(context, context.state.bga_lane_visible ? "BGA lane shown." : "BGA lane hidden.");
    }
    if (auto response = dispatch_compose_view_action_group(context)) return *response;
    if (auto response = dispatch_compose_resource_action(context)) return *response;
    if (auto response = dispatch_compose_bmson_action(context)) return *response;
    if (auto response = dispatch_compose_marker_action(context)) return *response;
    if (auto response = target_captured_semantic_layer_action(context)) return *response;
    if (auto response = dispatch_compose_semantic_layer_catalog_action(context)) return *response;
    if (auto response = dispatch_compose_semantic_layer_state_action(context)) return *response;
    return std::nullopt;
}

auto resource_path_tab_action(const WormUiWorkspaceAction& action,
                              wormedit::workspace_tab_t tab) -> bool {
    if (tab == wormedit::workspace_tab_t::audio_slices)
        return action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    return tab == wormedit::workspace_tab_t::metadata &&
        (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT);
}

auto is_initial_layout_workspace_action(const WormUiWorkspaceAction& action) -> bool {
    return action.compose_action == WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE;
}

auto temporary_compose_interaction_is_current(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, bool initial_layout_action) -> bool {
    const auto* selected = selected_chart(history, workspace);
    return workspace.active_tab == wormedit::workspace_tab_t::compose &&
        workspace.selected_chart && selected &&
        action.compose_input_chart_id == workspace.selected_chart->value &&
        action.compose_input_document_revision == history.document.revision &&
        (!initial_layout_action || selected->lane_layouts.empty());
}

auto temporary_compose_interaction_expired_message(bool initial_layout_action) -> const char* {
    return initial_layout_action
        ? "Lane layout change ignored because the selected difficulty or document changed."
        : "BGM column count edit expired because the selected chart or document changed.";
}

auto dispatch_compose_view_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime,
    runtime::output_group_export_t& export_runtime, bool metadata_import_busy,
    std::optional<notification_sink_t> notifications) -> std::string {
    using namespace wormedit;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE &&
        !action.compose_area_clear_requested) return {};
    const bool metadata_path_action = resource_path_tab_action(action, workspace.active_tab);
    if ((workspace.active_tab != workspace_tab_t::compose && !metadata_path_action) ||
        !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        state.chart.value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        if (notifications)
            notifications->post(notification_severity_t::warning, notification_source_t::edit,
                                "Compose view action ignored because the chart or document revision changed.");
        return "Compose view action ignored because the chart or document revision changed.";
    }
    const auto selected_chart_it = std::ranges::find(history.document.charts, state.chart,
                                                     &chart_document_t::id);
    const auto scroll_direction =
        selected_chart_it != history.document.charts.end() &&
                compose_field_downscroll(*selected_chart_it)
            ? editor_field_scroll_direction_t::down
            : editor_field_scroll_direction_t::up;
    editor_interaction_result_t result;
    std::string status;

    compose_action_context_t context{
        action, history, workspace, state, viewport_height, application_state, asset_base,
        resource_copy_scheduler, resource_copy_runtime, export_runtime, metadata_import_busy,
        scroll_direction, notifications};
    if (auto response = dispatch_quantize_preset_action(context)) return *response;
    if (auto response = dispatch_compose_area_with_quantize_report(
            action, history, state, application_state, notifications)) return *response;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE) return {};
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT) {
        return notifications
            ? (notifications->post(notification_severity_t::warning, notification_source_t::format,
                                   "BMS branch actions must use the workspace action boundary."),
               "BMS branch actions must use the workspace action boundary.")
            : "BMS branch actions must use the workspace action boundary.";
    }
    if (auto response = dispatch_compose_note_menu_action(context)) return *response;
    if (auto response = dispatch_compose_audio_offset(context)) return *response;
    if (auto response = dispatch_compose_bookmark_action(context)) return *response;
    if (auto response = dispatch_compose_measure_navigation(context)) return *response;
    if (auto response = dispatch_compose_bgm_column_count(context)) return *response;
    if (auto response = dispatch_compose_lane_profile(context)) return *response;
    if (auto response = dispatch_compose_lane_layout_profile(context)) return *response;
    if (auto response = dispatch_compose_remaining_action(context)) return *response;
    return compose_action_warning(context, compose_result_status(result, status), notification_source_t::input);
}

auto dispatch_compose_workspace_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base,
    wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime,
    runtime::output_group_export_t& export_runtime, bool metadata_import_busy,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    notification_sink_t notifications) -> bool {
    const auto report = [&](std::string message, notification_severity_t severity,
                            notification_source_t source) {
        post_typed_compose_feedback(notifications, severity, source, message);
        return message;
    };
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE) {
        static_cast<void>(dispatch_bms_branch_choice_action(
            action, history, workspace, interaction_state, notifications));
        return true;
    }
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT) {
        const auto status = dispatch_bms_branch_statement_action(
            action, history, workspace, interaction_state, notifications);
        retain_bms_statement_feedback(application_state, workspace.selected_chart,
                                      history.document.revision, status);
        return true;
    }
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE && !action.compose_area_clear_requested) {
        return false;
    }

    const bool initial_layout_action = is_initial_layout_workspace_action(action);
    const bool temporary_bgm_interaction_action =
        action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT || initial_layout_action;
    if (!interaction_state && temporary_bgm_interaction_action) {
        if (!temporary_compose_interaction_is_current(
                action, history, workspace, initial_layout_action)) {
            report(temporary_compose_interaction_expired_message(initial_layout_action),
                   notification_severity_t::warning, notification_source_t::edit);
            return true;
        }
        wormedit::editor_interaction_state_t bgm_interaction;
        bgm_interaction.chart = *workspace.selected_chart;
        const auto status = dispatch_compose_view_action(
            action, history, workspace, bgm_interaction, viewport_height,
            application_state, asset_base, resource_copy_scheduler, resource_copy_runtime,
            export_runtime, metadata_import_busy, notifications);
        static_cast<void>(status);
        return true;
    }

    const bool metadata_path_action = resource_path_tab_action(action, workspace.active_tab);
    wormedit::editor_interaction_state_t metadata_interaction{};
    if (metadata_path_action && workspace.selected_chart) {
        metadata_interaction.chart = *workspace.selected_chart;
    }
    if (interaction_state || metadata_path_action) {
        if (metadata_path_action) {
            metadata_asset_runtime.clear_copy_preview_key();
        }
        const auto status = dispatch_compose_view_action(
            action, history, workspace,
            metadata_path_action ? metadata_interaction : *interaction_state,
            viewport_height, application_state, asset_base, resource_copy_scheduler,
            resource_copy_runtime, export_runtime, metadata_import_busy, notifications);
        static_cast<void>(status);
        if (workspace.active_tab == wormedit::workspace_tab_t::metadata &&
            (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT)) {
            metadata_drop.clear_music();
        }
    }
    return true;
}



} // namespace wormedit::sys
