#include <wormedit/core/timing.h>
#include "sys/compose_actions.h"
#include "sys/application_edit_inputs.h"
#include "sys/bookmark_commands.h"
#include "sys/compose_actions_internal.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_bga_actions.h"
#include "sys/compose_label_search.h"
#include "sys/compose_measure_commands.h"
#include "sys/compose_navigation.h"
#include "sys/compose_quantize_request.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/metadata_commands.h"
#include "sys/transport_player_actions.h"
#include "ui/frontend.h"
#include <cassert>
#include <wormedit/core/bmson_lane_mode.h>

#include <unordered_set>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bgm_columns.h>
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_layout_editor.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>


#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {
using namespace compose_action_detail;

namespace compose_action_detail {

struct compose_bookmark_action_t {
    bool rename{};
    const char* label_end{};
    auto operator==(const compose_bookmark_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<compose_bookmark_action_t, compose_action_context_t>);

auto compose_bookmark_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    const auto& action = context.action;
    if (action.compose_input_project_id != context.environment.document.history.document.id.value)
        return compose_action_outcome_t::blocked(wormedit::stale_action_message("Bookmark action"));
    if (auto blocked =
            context.require_no_gesture("Finish or cancel the active Compose gesture first."))
        return std::move(*blocked);
    if (rename) {
        label_end = std::find(
            std::begin(action.compose_marker_label), std::end(action.compose_marker_label), '\0');
        if (label_end == std::end(action.compose_marker_label))
            return compose_action_outcome_t::blocked("Bookmark name is too long.");
    }
    return std::nullopt;
}

auto compose_bookmark_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto& action = context.action;
    if (!rename)
        return context.environment.editor.commands.bookmarks.navigate_to_bookmark(
            context.environment.document.history, context.state,
            context.environment.editor.commands, action.compose_input_document_revision,
            context.state.chart, wormedit::object_id_t{action.compose_bookmark_id});
    return context.environment.editor.commands.bookmarks.rename_bookmark(
        context.environment.document.history, action.compose_input_document_revision,
        context.state.chart, wormedit::object_id_t{action.compose_bookmark_id},
        std::string_view(action.compose_marker_label, label_end - action.compose_marker_label));
}

struct navigate_compose_measure_action_t {
    auto operator==(const navigate_compose_measure_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<navigate_compose_measure_action_t, compose_action_context_t>);

auto navigate_compose_measure_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto navigate_compose_measure_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    return navigate_compose_measure(context.environment.document.history, context.state,
                                    context.environment.editor.commands,
                                    context.action.compose_measure_number);
}

struct set_compose_bgm_column_count_action_t {
    auto operator==(const set_compose_bgm_column_count_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_compose_bgm_column_count_action_t, compose_action_context_t>);

auto set_compose_bgm_column_count_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing BGM columns.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto set_compose_bgm_column_count_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    constexpr std::string_view rebind_failure =
        "BGM columns changed, but workspace rebinding failed.";
    auto applied = wormedit::apply_bgm_column_count(
        context.environment.document.history,
        context.state.chart, context.action.compose_bgm_column_count);
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::from(
                         "BGM column count unchanged.", notification_severity_t::info,
                         notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(applied),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto outcome = compose_action_outcome_t::from(
        std::string{"BGM column count set to " +
                    std::to_string(context.action.compose_bgm_column_count) + "."},
        notification_severity_t::success, notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct set_compose_lane_profile_action_t {
    std::vector<lane_input_profile_t> choices{};
    local_position_t position{};
    auto operator==(const set_compose_lane_profile_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_compose_lane_profile_action_t, compose_action_context_t>);

auto set_compose_lane_profile_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing the lane profile.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("The selected difficulty is no longer available.",
                                                 notification_source_t::edit);
    const auto* segment = wormedit::active_lane_layout_segment(*chart, context.state.cursor);
    if (segment && !wormedit::can_use_lane_key_profiles(segment->layout))
        return compose_action_outcome_t::blocked(
            "Key profiles require matching playable lanes without scratch or auxiliary roles.",
            notification_source_t::edit);
    choices = segment
        ? wormedit::lane_profile_choices(segment->layout.lanes.size())
        : wormedit::lane_profile_choices();
    if (context.action.compose_lane_profile_index >= choices.size())
        return compose_action_outcome_t::blocked("Choose a lane profile first.",
                                                 notification_source_t::edit);
    position = segment ? context.state.cursor
        : wormedit::local_position_t{chart->measures.empty()
              ? context.state.cursor.measure : chart->measures.front().id, 0};
    return std::nullopt;
}

auto set_compose_lane_profile_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto& target = choices[context.action.compose_lane_profile_index].layout;
    constexpr std::string_view rebind_failure =
        "Lane profile changed, but workspace rebinding failed.";
    auto applied = wormedit::apply_lane_profile_switch(context.environment.document.history,
                                                             context.state.chart, position, target);
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::from(
                         "Lane profile unchanged.", notification_severity_t::info,
                         notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(applied),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto outcome = compose_action_outcome_t::from(
        std::string{"Lane profile set to " + target.display_name + "."},
        notification_severity_t::success, notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
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

// A measure that so far exists only on the virtual canvas (an empty BMS, say) is created first.
// Returns the notification when that fails; `chart` is refreshed after the history changed.
std::optional<compose_action_outcome_t>
materialize_layout_measure(compose_action_context_t& context,
                           const wormedit::chart_document_t*& chart, bool& materialized) {
    if (context.action.compose_measure_number <= chart->measures.size()) return std::nullopt;
    auto created = wormedit::apply_measure_edit(
        context.environment.document.history,
        wormedit::materialize_measures_action_t{
            chart->id, static_cast<std::size_t>(context.action.compose_measure_number)});
    if (!created.applied)
        return created.diagnostics.empty()
                   ? std::optional<compose_action_outcome_t>{compose_action_outcome_t::blocked(
                         "That measure is outside the chart.", notification_source_t::edit)}
                   : std::optional<compose_action_outcome_t>{
                         compose_action_outcome_t::from(std::move(created.diagnostics), notification_source_t::edit, compose_feedback_delivery_t::message)};
    materialized = true;
    chart = selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("The selected difficulty is no longer available.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto apply_compose_lane_layout_profile(compose_action_context_t& context,
                                       const chart_document_t* chart, bool materialized)
    -> compose_action_outcome_t {
    const auto measure_index = static_cast<std::size_t>(context.action.compose_measure_number - 1);
    const auto target_position = wormedit::local_position_t{chart->measures[measure_index].id, 0};
    const auto target = layout_profile_from_id(
        *chart, target_position, context.action.compose_lane_layout_profile_id);
    if (!target)
        return compose_action_outcome_t::blocked("Choose a lane layout profile first.",
                                                 notification_source_t::edit);
    const auto exact_segment = std::ranges::find_if(chart->lane_layouts, [&](const auto& segment) {
        return segment.position.measure == target_position.measure && segment.position.row == 0;
    });
    const auto is_update = exact_segment != chart->lane_layouts.end();
    const auto* active = is_update
                             ? &*exact_segment
                             : wormedit::active_lane_layout_segment(*chart, target_position);
    if (!active && !chart->lane_layouts.empty()) active = &chart->lane_layouts.front();
    const auto layout = layout_with_preserved_lane_ids(*chart, active, *target);
    if (!layout)
        return compose_action_outcome_t::blocked(
            "Lane layout change could not preserve lane identities.", notification_source_t::edit);
    wormedit::lane_layout_action_t edit =
        wormedit::insert_edit_t<wormedit::lane_layout_edit_value_t>{
            .chart = chart->id,
            .value = {.position = target_position,
                      .layout = *layout,
                      .input = wormedit::input_profile_t::with_defaults(*chart, *layout)}};
    if (is_update)
        edit = wormedit::update_edit_t<wormedit::lane_layout_edit_value_t>{
            .chart = chart->id, .id = exact_segment->id, .value = {.layout = *layout}};
    auto applied =
        wormedit::apply_lane_layout_edit(context.environment.document.history, edit);
    if (!applied.applied && materialized)
        static_cast<void>(wormedit::undo_document_command(context.environment.document.history));
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Lane layout change was rejected.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(std::move(applied.diagnostics),
                                                    notification_source_t::edit,
                                                    compose_feedback_delivery_t::message);
    auto outcome = compose_action_outcome_t::from(
        std::string{is_update ? "Lane layout updated." : "Lane layout inserted."},
        notification_severity_t::success, notification_source_t::edit, applied.applied);
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct apply_compose_lane_layout_action_t {
    const chart_document_t* chart{};
    auto operator==(const apply_compose_lane_layout_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) -> compose_action_outcome_t;
};
static_assert(compose_action<apply_compose_lane_layout_action_t, compose_action_context_t>);

auto apply_compose_lane_layout_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing a lane layout.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    chart = selected_chart(context.environment.document.history,
                                       context.environment.document.workspace);
    if (!chart || chart->id.value != context.action.compose_input_chart_id ||
        context.action.compose_measure_number == 0)
        return compose_action_outcome_t::blocked(
            "Lane layout change ignored because the selected difficulty changed.",
            notification_source_t::edit);
    return std::nullopt;
}

auto apply_compose_lane_layout_action_t::perform(compose_action_context_t& context)
    -> compose_action_outcome_t {
    auto materialized = false;
    if (auto refusal = materialize_layout_measure(context, chart, materialized))
        return std::move(*refusal);
    auto outcome = apply_compose_lane_layout_profile(context, chart, materialized);
    outcome.mutated = outcome.mutated || materialized;
    outcome.rebind_failure = "Lane layout changed, but workspace rebinding failed.";
    return outcome;
}

struct edit_compose_audio_offset_action_t {
    auto operator==(const edit_compose_audio_offset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_compose_audio_offset_action_t, compose_action_context_t>);

auto edit_compose_audio_offset_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing audio offset.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto edit_compose_audio_offset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    constexpr std::string_view rebind_failure =
        "Audio offset changed, but workspace rebinding failed.";
    auto applied = wormedit::apply_audio_offset_edit(
        context.environment.document.history, context.state.chart,
        {context.action.compose_audio_offset_numerator,
         context.action.compose_audio_offset_denominator},
        context.environment.editor.commands.audio_offset_edits_locked());
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::from(
                         "Audio offset unchanged.", notification_severity_t::info,
                         notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(applied),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto outcome = compose_action_outcome_t::from(std::string{"Audio offset changed."},
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct select_compose_tool_action_t {
    auto operator==(const select_compose_tool_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<select_compose_tool_action_t, compose_action_context_t>);

auto select_compose_tool_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto& action = context.action;
    auto& application_state = context.environment.editor.commands;
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_SELECT_TOOL &&
        !wormedit::ui::compose_notes_editable(compose_mode_of(application_state)))
        return compose_action_outcome_t::blocked("Draw and Layer are unavailable in Timing focus.",
                                                 notification_source_t::edit);
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_LAYER_TOOL &&
        !compose_layer_brush_target_editable(context.environment.document.history.document,
                                             context.state.chart,
                                             application_state.project.semantic_layer.semantic_layer_target))
        return compose_action_outcome_t::blocked(
            "Pick an unlocked visible layer in the Layers dock.", notification_source_t::edit);
    return std::nullopt;
}

auto select_compose_tool_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& action = context.action;
    auto& application_state = context.environment.editor.commands;
    (void)dispatch_editor_action(context.environment.document.history, context.state,
                                 cancel_editor_gesture_action_t{});
    application_state.project.compose_gesture.bga_draw_stroke.reset();
    application_state.project.compose_gesture.note_paint_mode_active = action.compose_action == WORM_UI_COMPOSE_ACTION_DRAW_TOOL;
    application_state.project.compose_gesture.semantic_layer_paint_mode_active = action.compose_action == WORM_UI_COMPOSE_ACTION_LAYER_TOOL;
    return compose_action_outcome_t::from("Compose tool selected.", notification_severity_t::info,
                                          notification_source_t::input);
}

struct toggle_compose_scroll_direction_action_t {
    const chart_document_t* chart{};
    auto operator==(const toggle_compose_scroll_direction_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_compose_scroll_direction_action_t, compose_action_context_t>);

auto toggle_compose_scroll_direction_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active gesture before changing scroll direction."))
        return *blocked;
    if (context.environment.editor.commands.project.compose_gesture.bga_draw_stroke)
        return compose_action_outcome_t::blocked(
            "Finish or cancel the active gesture before changing scroll direction.",
            notification_source_t::edit);
    chart =
        context.environment.document.history.document.find_chart(context.state.chart);
    if (chart == nullptr)
        return compose_action_outcome_t::blocked("The selected difficulty is no longer available.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto toggle_compose_scroll_direction_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto family = chart->lane_layouts.empty() ? editor_mode_family_t::other
        : compose_mode_family(chart->lane_layouts.front().layout);
    const bool down = !compose_field_downscroll(
        *chart, context.environment.editor.commands.preferences.editor_preferences);
    context.environment.editor.commands.preferences.editor_preferences
        .scroll_directions[static_cast<std::size_t>(family)] = down ? 2 : 1;
    configure_cursor_viewport(context.state, context.viewport_height, down
        ? editor_field_scroll_direction_t::down : editor_field_scroll_direction_t::up);
    return compose_action_outcome_t::from(down ? "Downscroll enabled for this mode family."
                                               : "Upscroll enabled for this mode family.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

struct reset_compose_view_action_t {
    auto operator==(const reset_compose_view_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<reset_compose_view_action_t, compose_action_context_t>);

auto reset_compose_view_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto reset_compose_view_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto result =
        reset_compose_navigation_view(context.environment.document.history, context.state);
    return compose_action_outcome_t::from(std::move(result), "Compose field view reset.");
}

struct compose_zoom_action_t {
    static auto from(bool zoom_in) -> compose_zoom_action_t {
        return {zoom_in ? 1.25 : 1.0 / 1.25,
                zoom_in ? "Compose field zoomed in." : "Compose field zoomed out."};
    }
    double factor;
    std::string_view message;
    auto operator==(const compose_zoom_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        std::string status;
        auto result = dispatch_editor_action(
            context.environment.document.history, context.state,
            adjust_editor_zoom_action_t{.factor = factor, .anchor_screen_y_pixels = std::nullopt});
        status = message;
        return compose_action_outcome_t::from(std::move(result), status);
    }
};
static_assert(compose_action<compose_zoom_action_t, compose_action_context_t>);

struct compose_scroll_action_t {
    double direction;
    auto operator==(const compose_scroll_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        std::string status;
        auto result = dispatch_editor_action(
            context.environment.document.history, context.state,
            scroll_editor_view_action_t{
                direction * std::max(64.0, context.viewport_height * 0.75),
                compose_cursor_screen_anchor(
                    context.viewport_height,
                    context.scroll_direction == editor_field_scroll_direction_t::down),
                context.scroll_direction});
        status = direction < 0.0 ? "Compose field scrolled up." : "Compose field scrolled down.";
        return compose_action_outcome_t::from(std::move(result), status);
    }
};
static_assert(compose_action<compose_scroll_action_t, compose_action_context_t>);

struct toggle_compose_note_paint_action_t {
    auto operator==(const toggle_compose_note_paint_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t> {
        if (!wormedit::ui::compose_notes_editable(compose_mode_of(context.environment.editor.commands)))
            return compose_action_outcome_t::blocked(
                "Note painting is unavailable in Timing focus.", notification_source_t::edit);
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        std::string status;
        auto& application_state = context.environment.editor.commands;
        if (context.state.is_pending()) {
            auto result = dispatch_editor_action(context.environment.document.history, context.state,
                                                 cancel_editor_gesture_action_t{});
            if (!result.accepted)
                return compose_action_outcome_t::from(std::move(result), "Compose gesture canceled.");
        }
        application_state.project.compose_gesture.note_paint_mode_active = !application_state.project.compose_gesture.note_paint_mode_active;
        if (application_state.project.compose_gesture.note_paint_mode_active)
            application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
        status = application_state.project.compose_gesture.note_paint_mode_active
                     ? "Note-paint mode enabled." : "Note-paint mode disabled.";
        return compose_action_outcome_t::from(status, notification_severity_t::info,
                                              notification_source_t::input);
    }
};
static_assert(compose_action<toggle_compose_note_paint_action_t, compose_action_context_t>);

struct step_compose_rate_action_t {
    auto operator==(const step_compose_rate_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        const auto stepped = step_transport_rate_status(
            context.environment.editor.commands,
            context.action.compose_action == WORM_UI_COMPOSE_ACTION_RATE_FASTER);
        return stepped
                   ? compose_action_outcome_t::from(*stepped, notification_severity_t::info,
                                                    notification_source_t::audio)
                   : compose_action_outcome_t::blocked("Playback output rejected the rate change.",
                                                       notification_source_t::audio);
    }
};
static_assert(compose_action<step_compose_rate_action_t, compose_action_context_t>);

struct toggle_compose_timing_focus_action_t {
    auto operator==(const toggle_compose_timing_focus_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t> {
        if (auto blocked = context.require_no_gesture(
                "Finish or cancel the active Compose gesture before changing the Compose mode."))
            return std::move(*blocked);
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        auto& application_state = context.environment.editor.commands;
        application_state.project.compose_view.compose_timing_focus = !application_state.project.compose_view.compose_timing_focus;
        if (application_state.project.compose_view.compose_timing_focus) {
            // Timing focus never edits notes, so leave any note tool.
            application_state.project.compose_gesture.note_paint_mode_active = false;
            application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
        }
        return compose_action_outcome_t::from(
            application_state.project.compose_view.compose_timing_focus ? "Timing focus on." : "Timing focus off.",
            notification_severity_t::info, notification_source_t::input);
    }
};
static_assert(compose_action<toggle_compose_timing_focus_action_t, compose_action_context_t>);

struct toggle_compose_edit_snap_action_t {
    auto operator==(const toggle_compose_edit_snap_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t> {
        if (auto blocked = context.require_no_gesture(
                "Finish or cancel the active Compose gesture before changing snapping."))
            return std::move(*blocked);
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        auto& application_state = context.environment.editor.commands;
        application_state.project.compose_view.edit_snap_enabled = !application_state.project.compose_view.edit_snap_enabled;
        return compose_action_outcome_t::from(
            application_state.project.compose_view.edit_snap_enabled ? "Snap on." : "Snap off: edits use the finest row.",
            notification_severity_t::info, notification_source_t::input);
    }
};
static_assert(compose_action<toggle_compose_edit_snap_action_t, compose_action_context_t>);

struct toggle_compose_y_lock_action_t {
    auto operator==(const toggle_compose_y_lock_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        auto& application_state = context.environment.editor.commands;
        application_state.project.compose_view.y_lock = !application_state.project.compose_view.y_lock;
        context.state.y_lock = application_state.project.compose_view.y_lock;
        return compose_action_outcome_t::from(application_state.project.compose_view.y_lock
                                                  ? "Y-lock on: dragged notes keep their rows." : "Y-lock off.",
                                              notification_severity_t::info, notification_source_t::input);
    }
};
static_assert(compose_action<toggle_compose_y_lock_action_t, compose_action_context_t>);

struct toggle_compose_timing_lane_action_t {
    auto operator==(const toggle_compose_timing_lane_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t> {
        if (context.action.compose_timing_lane >= compose_timing_lane_count)
            return compose_action_outcome_t::blocked("The requested timing lane does not exist.",
                                                     notification_source_t::edit);
        if (auto blocked = context.require_no_gesture(
                "Finish or cancel the active Compose gesture before changing timing lanes."))
            return std::move(*blocked);
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        auto& application_state = context.environment.editor.commands;
        const auto bit = static_cast<std::uint8_t>(1U << context.action.compose_timing_lane);
        application_state.project.compose_view.compose_timing_lane_visibility_mask ^= bit;
        return compose_action_outcome_t::from(
            (application_state.project.compose_view.compose_timing_lane_visibility_mask & bit) != 0
                ? "Compose timing lane shown." : "Compose timing lane hidden.",
            notification_severity_t::info, notification_source_t::input);
    }
};
static_assert(compose_action<toggle_compose_timing_lane_action_t, compose_action_context_t>);

// Points a missing sound at one of its fallback candidates through the shared relink command; the
// authored token and every reference stay as they are.

struct assign_bmson_selection_action_t {
    object_id_t event{};
    bool to_lane{};
    auto operator==(const assign_bmson_selection_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<assign_bmson_selection_action_t, compose_action_context_t>);

auto assign_bmson_selection_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    auto& state = context.state;
    to_lane = context.action.compose_action ==
                         WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before assigning BMSON sound.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    event = object_id_t{context.action.compose_bmson_bgm_event_id};
    const auto kind = to_lane ? editor_selection_object_kind_t::bgm_event
                              : editor_selection_object_kind_t::note;
    if (state.selected_objects.size() != 1 || state.selected_objects.front().kind != kind ||
        state.selected_objects.front().id != event)
        return compose_action_outcome_t::blocked(
            "Select one linked BMSON tap or BGM marker before assigning sound.",
            notification_source_t::edit);
    return std::nullopt;
}

auto assign_bmson_selection_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& state = context.state;
    constexpr std::string_view rebind_failure =
        "BMSON sound assigned, but workspace rebinding failed.";
    auto assigned = assign_bmson_sound_event(
        context.environment.document.history,
        state.chart, event, to_lane ? std::optional<lane_id_t>{state.active_lane} : std::nullopt);
    if (!assigned.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(assigned),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    if (!assigned.applied)
        return compose_action_outcome_t::from(
            "BMSON sound is already assigned to that destination.", notification_severity_t::info,
            notification_source_t::edit);
    state.selected_objects = {{to_lane ? editor_selection_object_kind_t::note
                                       : editor_selection_object_kind_t::bgm_event, event}};
    state.selected_notes = to_lane ? std::vector<object_id_t>{event} : std::vector<object_id_t>{};
    auto outcome = compose_action_outcome_t::from(
        std::string{to_lane ? "BMSON BGM assigned to the active playable lane."
                            : "BMSON tap assigned to BGM."},
        notification_severity_t::success, notification_source_t::edit, assigned.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(assigned.diagnostics);
    return outcome;
}

auto selected_bmson_source_event(const compose_action_context_t& context,
                                 wormedit::object_id_t bgm_event)
    -> std::optional<wormedit::object_id_t> {
    const auto* chart =
        context.environment.document.history.document.find_chart(context.state.chart);
    if (chart == nullptr || chart->source_family != wormedit::format_family_t::bmson)
        return std::nullopt;
    for (const auto& measure : chart->measures) {
        const auto event = std::ranges::find(measure.bgm_events, bgm_event,
                                             &wormedit::bgm_event_t::id);
        if (event != measure.bgm_events.end()) return event->sound_channel_event;
    }
    return std::nullopt;
}

struct set_bmson_selection_restart_action_t {
    object_id_t event{};
    std::optional<object_id_t> source_event{};
    auto operator==(const set_bmson_selection_restart_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_bmson_selection_restart_action_t, compose_action_context_t>);

auto set_bmson_selection_restart_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    event = object_id_t{context.action.compose_bmson_bgm_event_id};
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing a BMSON slice.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    if (!event || context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_outcome_t::blocked(
            "Select one BMSON BGM marker before changing its restart state.",
            notification_source_t::edit);
    source_event = selected_bmson_source_event(context, event);
    if (!source_event)
        return compose_action_outcome_t::blocked(
            "The selected BGM marker has no BMSON sound-channel slice.",
            notification_source_t::edit);
    return std::nullopt;
}

auto set_bmson_selection_restart_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "BMSON slice changed, but workspace rebinding failed.";
    const bool restart = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART;
    auto applied = set_bmson_sound_channel_restart(
        context.environment.document.history,
        context.state.chart, *source_event, restart);
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    if (!applied.applied)
        return compose_action_outcome_t::from(
            restart ? "BMSON slice already restarts." : "BMSON slice already continues.",
            notification_severity_t::info, notification_source_t::edit);
    auto outcome = compose_action_outcome_t::from(
        std::string{restart ? "BMSON slice set to restart." : "BMSON slice set to continue."},
        notification_severity_t::success, notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct move_bmson_selection_to_cursor_action_t {
    object_id_t event{};
    auto operator==(const move_bmson_selection_to_cursor_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<move_bmson_selection_to_cursor_action_t, compose_action_context_t>);

auto move_bmson_selection_to_cursor_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    event = object_id_t{context.action.compose_bmson_bgm_event_id};
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before moving a BMSON marker.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_outcome_t::blocked("Select one BMSON BGM marker before moving it.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto move_bmson_selection_to_cursor_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "BMSON marker moved, but workspace rebinding failed.";
    auto moved = move_bmson_bgm_marker(context.environment.document.history,
                                             context.state.chart, event, context.state.cursor);
    if (!moved.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(moved), compose_action_description_t{.rebind_failure = rebind_failure,
                                                           .source = notification_source_t::edit});
    if (!moved.applied)
        return compose_action_outcome_t::from("BMSON BGM marker is already at the cursor.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);
    auto outcome = compose_action_outcome_t::from(
        std::string{"BMSON BGM marker moved to the edit cursor."}, notification_severity_t::success,
        notification_source_t::edit, moved.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(moved.diagnostics);
    return outcome;
}

struct insert_bmson_marker_at_cursor_action_t {
    object_id_t event{};
    auto operator==(const insert_bmson_marker_at_cursor_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<insert_bmson_marker_at_cursor_action_t, compose_action_context_t>);

auto insert_bmson_marker_at_cursor_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    event = object_id_t{context.action.compose_bmson_bgm_event_id};
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before inserting a BMSON marker.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_outcome_t::blocked(
            "Select one BMSON BGM marker to choose its sound channel.",
            notification_source_t::edit);
    return std::nullopt;
}

auto insert_bmson_marker_at_cursor_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "BMSON marker inserted, but workspace rebinding failed.";
    auto inserted = insert_bmson_bgm_marker(
        context.environment.document.history,
        context.state.chart, event, context.state.cursor,
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR);
    if (!inserted.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(inserted),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    auto outcome = compose_action_outcome_t::from(
        std::string{"BMSON BGM marker inserted at the edit cursor."},
        notification_severity_t::success, notification_source_t::edit, inserted.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(inserted.diagnostics);
    return outcome;
}

struct stage_bmson_marker_at_cursor_action_t {
    auto operator==(const stage_bmson_marker_at_cursor_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<stage_bmson_marker_at_cursor_action_t, compose_action_context_t>);

auto stage_bmson_marker_at_cursor_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before staging a BMSON marker.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto stage_bmson_marker_at_cursor_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto severity = notification_severity_t::warning;
    auto message = propose_bmson_bgm_marker(
        context.environment.document.history, context.state,
        context.environment.editor.commands,
        wormedit::object_id_t{context.action.compose_bmson_bgm_event_id}, context.state.cursor,
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR,
        &severity);
    return compose_action_outcome_t::from(std::move(message), severity,
                                          notification_source_t::input);
}

struct apply_bmson_marker_batch_action_t {
    auto operator==(const apply_bmson_marker_batch_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<apply_bmson_marker_batch_action_t, compose_action_context_t>);

auto apply_bmson_marker_batch_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before committing BMSON markers.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto apply_bmson_marker_batch_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "BMSON markers inserted, but workspace rebinding failed.";
    auto& draft = context.environment.editor.commands.project.compose_gesture.bmson_marker_draft;
    if (!draft || draft->payload.markers.empty()) {
        draft.reset();
        return compose_action_outcome_t::blocked(
            "The BMSON marker batch is no longer available for this chart revision.",
            notification_source_t::edit);
    }
    if (draft->stamp.chart != context.state.chart) {
        draft.reset();
        return compose_action_outcome_t::blocked(stale_action_message("BMSON marker batch"),
                                                 notification_source_t::edit);
    }
    auto transaction = draft->transaction(
        context.environment.document.history, "BMSON marker batch",
        document_identity_policy_t::revision_only);
    if (!transaction.is_ready()) {
        draft.reset();
        return compose_action_outcome_t::blocked(transaction.outcome().diagnostics.front().message,
                                                 notification_source_t::edit);
    }
    auto inserted = insert_bmson_sound_markers(transaction, draft->stamp.chart,
                                                    draft->payload.reference, draft->payload.markers);
    if (!inserted.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(inserted),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    draft.reset();
    auto outcome = compose_action_outcome_t::from(
        std::string{"BMSON markers inserted in one undoable edit."},
        notification_severity_t::success, notification_source_t::edit, inserted.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(inserted.diagnostics);
    return outcome;
}

struct delete_bmson_selected_marker_action_t {
    object_id_t event{};
    auto operator==(const delete_bmson_selected_marker_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<delete_bmson_selected_marker_action_t, compose_action_context_t>);

auto delete_bmson_selected_marker_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    event = object_id_t{context.action.compose_bmson_bgm_event_id};
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before deleting a BMSON marker.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return compose_action_outcome_t::blocked("Select one BMSON BGM marker before deleting it.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto delete_bmson_selected_marker_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "BMSON marker deleted, but workspace rebinding failed.";
    auto deleted = delete_bmson_bgm_marker(context.environment.document.history,
                                                 context.state.chart, event);
    if (!deleted.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(deleted),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    if (!deleted.applied)
        return compose_action_outcome_t::from("BMSON marker deletion made no document change.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);
    context.state.selected_objects.clear();
    context.state.selected_notes.clear();
    auto outcome = compose_action_outcome_t::from(std::string{"BMSON BGM marker deleted."},
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, deleted.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(deleted.diagnostics);
    return outcome;
}

auto lane_mode_channel(const compose_action_context_t& context) -> wormedit::object_id_t {
    if (context.action.compose_resource_id) {
        const auto bindings = wormedit::bmson_lane_mode_resource_bindings(
            context.environment.document.history.document, context.state.chart);
        const auto binding = std::ranges::find(bindings, wormedit::resource_id_t{context.action.compose_resource_id},
            &wormedit::bmson_channel_resource_binding_t::resource);
        if (binding != bindings.end() && !binding->ambiguous && binding->channel) return *binding->channel;
        return {};
    }
    return wormedit::object_id_t{context.action.compose_bmson_channel_id};
}

struct change_bmson_lane_mode_action_t {
    auto operator==(const change_bmson_lane_mode_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<change_bmson_lane_mode_action_t, compose_action_context_t>);

auto change_bmson_lane_mode_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (context.action.compose_input_project_id !=
        context.environment.document.history.document.id.value)
        return compose_action_outcome_t::blocked(
            wormedit::stale_action_message("Channel lane action"), notification_source_t::edit);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing channel lanes.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto change_bmson_lane_mode_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure = "Channel changed, but workspace rebinding failed.";
    const auto kind = context.action.compose_action;
    auto result = set_bmson_channel_lane_mode(
        context.environment.document.history,
        context.state.chart, lane_mode_channel(context),
        kind == WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM ? bmson_channel_lane_mode_t::stem
                                                       : bmson_channel_lane_mode_t::packed,
        kind == WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL);
    if (!result.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(result), compose_action_description_t{.rebind_failure = rebind_failure,
                                                            .source = notification_source_t::edit});
    if (!result.applied)
        return compose_action_outcome_t::from("Channel already uses that lane mode.",
                                              notification_severity_t::info,
                                              notification_source_t::input);
    auto outcome = compose_action_outcome_t::from(std::string{"BMSON channel lane mode changed."},
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, result.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(result.diagnostics);
    return outcome;
}

// The form's recipe and the stored recipe it edits, when the action still matches the document.

// Shows the unapplied form candidate in the lane. A malformed form or a candidate the core
// validator rejects keeps its message for the form and draws no graph.


auto catalog_semantic_layer_by_id(const wormedit::chart_document_t& chart,
                                  wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t* {
    const auto found = std::ranges::find(chart.semantic_layers, id, &wormedit::semantic_layer_t::id);
    return found == chart.semantic_layers.end() ? nullptr : &*found;
}

struct assign_compose_layer_target_action_t {
    bool none{};
    layer_id_t target{};
    auto operator==(const assign_compose_layer_target_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<assign_compose_layer_target_action_t, compose_action_context_t>);

auto assign_compose_layer_target_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing semantic layers.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto assign_compose_layer_target_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    constexpr std::string_view rebind_failure =
        "Semantic-layer membership changed, but workspace rebinding failed.";
    auto& app = context.environment.editor.commands;
    auto applied = wormedit::sys::apply_compose_layer_assignment(
        context.environment.document.history, context.state,
        context.action.compose_input_document_revision, none, target,
        app.project.semantic_layer.semantic_layer_target_none, app.project.semantic_layer.semantic_layer_target);
    if (!applied.applied && !applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    if (none) app.project.semantic_layer.semantic_layer_solo_active = false;
    if (!applied.applied)
        return compose_action_outcome_t::from("Semantic-layer membership was unchanged.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);
    auto outcome = compose_action_outcome_t::from(
        std::string{none ? "Selected objects assigned to No layer."
                         : "Semantic-layer membership updated."},
        notification_severity_t::success, notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct add_selected_compose_sounds_to_layer_action_t {
    auto operator==(const add_selected_compose_sounds_to_layer_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<add_selected_compose_sounds_to_layer_action_t, compose_action_context_t>);

auto add_selected_compose_sounds_to_layer_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active gesture before assigning sound layers.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    auto& app = context.environment.editor.commands;
    if (context.action.compose_input_project_id !=
            context.environment.document.history.document.id.value ||
        app.project.semantic_layer.note_layer_menu_chart != context.state.chart ||
        app.project.semantic_layer.note_layer_menu_revision != context.action.compose_input_document_revision)
        return compose_action_outcome_t::blocked(wormedit::stale_action_message("Sound layer menu"),
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto add_selected_compose_sounds_to_layer_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& app = context.environment.editor.commands;
    constexpr std::string_view rebind_failure =
        "Sound layers changed, but workspace rebinding failed.";
    app.project.semantic_layer.note_layer_menu_row_count = 0;
    auto applied = apply_compose_selected_sound_layer_assignment(
        context.environment.document.history, context.state,
        context.action.compose_input_document_revision,
        layer_id_t{context.action.compose_semantic_layer_id});
    if (!applied.applied) {
        if (!applied.diagnostics.empty())
            return compose_action_outcome_t::from(
                std::move(applied),
                compose_action_description_t{.rebind_failure = rebind_failure,
                                             .source = notification_source_t::edit});
        return compose_action_outcome_t::from(
            "Selected sound layers were unchanged; silent objects are ignored.",
            notification_severity_t::info, notification_source_t::edit);
    }
    auto outcome = compose_action_outcome_t::from(
        std::string{
            "Selected sounds added to the layer; unassigned matching objects inherited it."},
        notification_severity_t::success, notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.rebind_success_message = "Selected sounds added to the layer.";
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct open_compose_note_layer_menu_action_t {
    auto operator==(const open_compose_note_layer_menu_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<open_compose_note_layer_menu_action_t, compose_action_context_t>);

auto open_compose_note_layer_menu_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before opening the note menu.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto open_compose_note_layer_menu_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& action = context.action;
    auto& app = context.environment.editor.commands;
    auto result = select_compose_note_context(
        context.environment.document.history, context.state, action.compose_input_document_revision,
        object_id_t{action.compose_note_context_id});
    if (!result.accepted)
        return compose_action_outcome_t::from(std::move(result), "Note could not be selected.");
    app.project.semantic_layer.note_layer_menu_chart = context.state.chart;
    app.project.semantic_layer.note_layer_menu_revision = context.environment.document.history.document.revision;
    app.project.semantic_layer.note_layer_menu_first_row = 0;
    app.project.semantic_layer.note_layer_menu_row_count = WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY;
    return compose_action_outcome_t::from({}, notification_severity_t::hint);
}

struct open_compose_sound_layer_menu_action_t {
    auto operator==(const open_compose_sound_layer_menu_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<open_compose_sound_layer_menu_action_t, compose_action_context_t>);

auto open_compose_sound_layer_menu_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    const auto& action = context.action;
    if (auto blocked =
            context.require_no_gesture("The selection changed or a Compose gesture is active."))
        return *blocked;
    if (action.compose_input_project_id != context.environment.document.history.document.id.value ||
        action.compose_input_document_revision !=
            context.environment.document.history.document.revision)
        return compose_action_outcome_t::blocked(
            "The selection changed or a Compose gesture is active.",
            notification_source_t::edit);
    return std::nullopt;
}

auto open_compose_sound_layer_menu_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    app.project.semantic_layer.note_layer_menu_chart = context.state.chart;
    app.project.semantic_layer.note_layer_menu_revision = context.environment.document.history.document.revision;
    app.project.semantic_layer.note_layer_menu_first_row = 0;
    app.project.semantic_layer.note_layer_menu_row_count = WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY;
    return compose_action_outcome_t::from({}, notification_severity_t::hint);
}

struct set_compose_note_layer_menu_window_action_t {
    auto operator==(const set_compose_note_layer_menu_window_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_compose_note_layer_menu_window_action_t, compose_action_context_t>);

auto set_compose_note_layer_menu_window_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    auto& app = context.environment.editor.commands;
    if (app.project.semantic_layer.note_layer_menu_chart != context.state.chart ||
        app.project.semantic_layer.note_layer_menu_revision != context.environment.document.history.document.revision)
        return compose_action_outcome_t::from({}, notification_severity_t::hint);
    return std::nullopt;
}

auto set_compose_note_layer_menu_window_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    const auto& action = context.action;
    app.project.semantic_layer.note_layer_menu_first_row = action.compose_semantic_layer_first_row;
    app.project.semantic_layer.note_layer_menu_row_count = std::clamp<std::size_t>(
        action.compose_semantic_layer_row_count, 1, WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY);
    return compose_action_outcome_t::from({}, notification_severity_t::hint);
}

struct close_compose_note_layer_menu_action_t {
    auto operator==(const close_compose_note_layer_menu_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<close_compose_note_layer_menu_action_t, compose_action_context_t>);

auto close_compose_note_layer_menu_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    auto& app = context.environment.editor.commands;
    const auto& action = context.action;
    if ((action.compose_input_project_id &&
         action.compose_input_project_id !=
             context.environment.document.history.document.id.value) ||
        app.project.semantic_layer.note_layer_menu_chart.value != action.compose_input_chart_id ||
        app.project.semantic_layer.note_layer_menu_revision != action.compose_input_document_revision)
        return compose_action_outcome_t::from({}, notification_severity_t::hint);
    return std::nullopt;
}

auto close_compose_note_layer_menu_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    app.project.semantic_layer.note_layer_menu_row_count = 0;
    return compose_action_outcome_t::from({}, notification_severity_t::hint);
}

struct assign_compose_note_menu_layer_action_t {
    auto operator==(const assign_compose_note_menu_layer_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<assign_compose_note_menu_layer_action_t, compose_action_context_t>);

auto assign_compose_note_menu_layer_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto assign_compose_note_menu_layer_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    const auto& action = context.action;
    app.project.semantic_layer.note_layer_menu_row_count = 0;
    return run_compose_action(
        assign_compose_layer_target_action_t{
            .none = action.compose_semantic_layer_id == 0,
            .target = layer_id_t{action.compose_semantic_layer_id}}, context);
}

struct set_semantic_layer_window_action_t {
    auto operator==(const set_semantic_layer_window_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_semantic_layer_window_action_t, compose_action_context_t>);

auto set_semantic_layer_window_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto set_semantic_layer_window_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& application_state = context.environment.editor.commands;
    const auto& action = context.action;
    application_state.project.semantic_layer.semantic_layer_first_row = action.compose_semantic_layer_first_row;
    application_state.project.semantic_layer.semantic_layer_row_count =
        std::clamp<std::size_t>(action.compose_semantic_layer_row_count, 1,
                                WORM_UI_COMPOSE_SEMANTIC_LAYER_WINDOW_CAPACITY);
    return compose_action_outcome_t::from({}, notification_severity_t::hint);
}

struct select_semantic_layer_target_action_t {
    const semantic_layer_t* layer{};
    auto operator==(const select_semantic_layer_target_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<select_semantic_layer_target_action_t, compose_action_context_t>);

auto select_semantic_layer_target_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;
    const auto& action = context.action;
    const auto* chart = selected_chart(history, workspace);
    if (!chart || action.compose_semantic_layer_id == 0)
        return compose_action_outcome_t::blocked(
            "Choose a visible semantic layer from the current Compose palette.",
            notification_source_t::edit);
    layer = catalog_semantic_layer_by_id(
        *chart, layer_id_t{action.compose_semantic_layer_id});
    if (!layer)
        return compose_action_outcome_t::blocked(
            "That semantic-layer palette entry is no longer visible.",
            notification_source_t::edit);
    return std::nullopt;
}

auto select_semantic_layer_target_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& application_state = context.environment.editor.commands;
    application_state.project.semantic_layer.semantic_layer_target = layer->id;
    application_state.project.semantic_layer.semantic_layer_target_none = false;
    return compose_action_outcome_t::from("Semantic-layer target: " + layer->name,
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

struct step_semantic_layer_palette_action_t {
    const chart_document_t* chart{};
    auto operator==(const step_semantic_layer_palette_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<step_semantic_layer_palette_action_t, compose_action_context_t>);

auto step_semantic_layer_palette_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;
    chart = selected_chart(history, workspace);
    if (!chart)
        return compose_action_outcome_t::blocked(
            "Choose a chart before navigating semantic-layer pages.",
            notification_source_t::edit);
    return std::nullopt;
}

auto step_semantic_layer_palette_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& application_state = context.environment.editor.commands;
    const auto& action = context.action;
    if (application_state.project.semantic_layer.semantic_layer_palette_chart != chart->id) {
        application_state.project.semantic_layer.semantic_layer_palette_chart = chart->id;
        application_state.project.semantic_layer.semantic_layer_palette_page = 0;
    }
    const auto palette = wormedit::semantic_layer_palette_window_t::from(
        *chart, application_state.project.semantic_layer.semantic_layer_target,
        application_state.project.semantic_layer.semantic_layer_palette_page, false,
        WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
    if (!palette)
        return compose_action_outcome_t::blocked(
            "Semantic-layer palette navigation exceeded its bounded limit.",
            notification_source_t::edit);
    application_state.project.semantic_layer.semantic_layer_palette_page = palette->page;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE) {
        if (application_state.project.semantic_layer.semantic_layer_palette_page == 0)
            return compose_action_outcome_t::from(
                "Semantic-layer palette is already on its first page.",
                notification_severity_t::hint, notification_source_t::input);
        --application_state.project.semantic_layer.semantic_layer_palette_page;
    } else {
        if (palette->page_count == 0 ||
            application_state.project.semantic_layer.semantic_layer_palette_page + 1 >= palette->page_count)
            return compose_action_outcome_t::from(
                "Semantic-layer palette is already on its last page.",
                notification_severity_t::hint, notification_source_t::input);
        ++application_state.project.semantic_layer.semantic_layer_palette_page;
    }
    return compose_action_outcome_t::from(
        "Semantic-layer palette page " +
            std::to_string(
                application_state.project.semantic_layer.semantic_layer_palette_page + 1) +
            " of " + std::to_string(palette->page_count) + ".",
        notification_severity_t::hint, notification_source_t::input);
}

struct choose_no_semantic_layer_action_t {
    auto operator==(const choose_no_semantic_layer_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<choose_no_semantic_layer_action_t, compose_action_context_t>);

auto choose_no_semantic_layer_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (!selected_chart(context.environment.document.history, context.environment.document.workspace))
        return compose_action_outcome_t::blocked("Choose a Compose chart first.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto choose_no_semantic_layer_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    if (!context.state.selected_notes.empty() ||
        !selected_compose_bgm_events(context.state).empty())
        return run_compose_action(assign_compose_layer_target_action_t{.none = true, .target = {}}, context);
    auto& app = context.environment.editor.commands;
    app.project.semantic_layer.semantic_layer_target.reset();
    app.project.semantic_layer.semantic_layer_target_none = true;
    app.project.semantic_layer.semantic_layer_solo_active = false;
    return compose_action_outcome_t::from("Semantic-layer target: No layer",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

compose_action_outcome_t
semantic_layer_edit_status(compose_action_context_t& context,
                           wormedit::semantic_layer_edit_action_t edit, std::string_view success,
                           std::string_view unchanged, std::string_view rebind_failure) {
    const bool inserting =
        std::holds_alternative<wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>>(
            edit);
    auto applied =
        wormedit::apply_semantic_layer_edit(context.environment.document.history, std::move(edit));
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::from(
                         std::string{unchanged}, notification_severity_t::info,
                         notification_source_t::edit)
                   : compose_action_outcome_t::from(std::move(applied.diagnostics), notification_source_t::edit, compose_feedback_delivery_t::message);
    if (inserting) {
        const auto* chart =
            selected_chart(context.environment.document.history, context.environment.document.workspace);
        if (chart && !chart->semantic_layers.empty()) {
            context.environment.editor.commands.project.semantic_layer.semantic_layer_target =
                chart->semantic_layers.back().id;
            context.environment.editor.commands.project.semantic_layer.semantic_layer_target_none = false;
        }
    }
    auto outcome =
        compose_action_outcome_t::from(std::string{success}, notification_severity_t::success,
                                       notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    return outcome;
}

struct add_semantic_layer_action_t {
    const chart_document_t* chart{};
    auto operator==(const add_semantic_layer_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<add_semantic_layer_action_t, compose_action_context_t>);

auto add_semantic_layer_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before adding a semantic layer.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("Choose a chart before adding a semantic layer.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto add_semantic_layer_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    std::string name = context.action.compose_semantic_layer_name;
    if (name.empty()) name = "Layer";
    for (std::size_t suffix = 2; !context.action.compose_semantic_layer_name[0] && std::ranges::any_of(
             chart->semantic_layers, [&](const auto& item) { return item.name == name; }); ++suffix)
        name = "Layer " + std::to_string(suffix);
    constexpr std::array<semantic_layer_rgba_t, 6> colors{{
        {0.91, 0.35, 0.31, 1.0}, {0.95, 0.67, 0.24, 1.0},
        {0.36, 0.74, 0.36, 1.0}, {0.25, 0.62, 0.91, 1.0},
        {0.58, 0.42, 0.84, 1.0}, {0.87, 0.38, 0.68, 1.0},
    }};
    return semantic_layer_edit_status(
        context,
        wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = context.state.chart,
            .value = {.name = std::move(name),
                      .color = colors[chart->semantic_layers.size() % colors.size()]}},
        "Semantic layer added.", "Semantic layer was not added.",
        "Semantic layer added, but workspace rebinding failed.");
}

auto selected_catalog_layer(compose_action_context_t& context)
    -> std::pair<const wormedit::chart_document_t*, const wormedit::semantic_layer_t*> {
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart || !context.environment.editor.commands.project.semantic_layer.semantic_layer_target)
        return {chart, nullptr};
    return {chart,
            catalog_semantic_layer_by_id(
                *chart, *context.environment.editor.commands.project.semantic_layer.semantic_layer_target)};
}

struct update_semantic_layer_catalog_entry_action_t {
    std::pair<const chart_document_t*, const semantic_layer_t*> target{};
    bool recolor{};
    auto operator==(const update_semantic_layer_catalog_entry_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<update_semantic_layer_catalog_entry_action_t, compose_action_context_t>);

auto update_semantic_layer_catalog_entry_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    recolor = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
    if (auto blocked = context.require_no_gesture(
            recolor
                ? "Finish or cancel the active Compose gesture before recoloring a semantic layer."
                : "Finish or cancel the active Compose gesture before renaming a semantic "
                  "layer.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    target = selected_catalog_layer(context);
    const auto& [chart, layer] = target;
    if (!chart || !layer)
        return !chart || !context.environment.editor.commands.project.semantic_layer
                              .semantic_layer_target
                   ? compose_action_outcome_t::blocked(
                         recolor ? "Choose a visible semantic layer before recoloring it."
                                 : "Choose a visible semantic layer before renaming it.",
                         notification_source_t::edit)
                   : compose_action_outcome_t::blocked(
                         "The selected semantic layer is no longer visible.",
                         notification_source_t::edit);
    return std::nullopt;
}

auto update_semantic_layer_catalog_entry_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& [chart, layer] = target;
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
        context,
        wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = context.state.chart,
            .id = layer->id,
            .value =
                {.name = name, .color = color, .visible = layer->visible, .locked = layer->locked}},
        recolor ? "Semantic-layer color changed." : "Semantic layer renamed.",
        recolor ? "Semantic-layer color was unchanged." : "Semantic-layer name was unchanged.",
        recolor ? "Semantic-layer color changed, but workspace rebinding failed."
                : "Semantic layer renamed, but workspace rebinding failed.");
}

struct delete_semantic_layer_catalog_entry_action_t {
    std::pair<const chart_document_t*, const semantic_layer_t*> target{};
    auto operator==(const delete_semantic_layer_catalog_entry_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<delete_semantic_layer_catalog_entry_action_t, compose_action_context_t>);

auto delete_semantic_layer_catalog_entry_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before deleting a semantic layer.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    target = selected_catalog_layer(context);
    const auto& [chart, layer] = target;
    if (!chart || !context.environment.editor.commands.project.semantic_layer.semantic_layer_target)
        return compose_action_outcome_t::blocked(
            "Choose a visible semantic layer before deleting it.", notification_source_t::edit);
    if (!layer)
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer visible.", notification_source_t::edit);
    return std::nullopt;
}

auto delete_semantic_layer_catalog_entry_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto* layer = target.second;
    constexpr std::string_view rebind_failure =
        "Semantic layer deleted, but workspace rebinding failed.";
    const auto id = layer->id;
    auto applied = apply_semantic_layer_edit(
        context.environment.document.history,
        wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = context.state.chart,
            .id = id,
            .options = {.remove_from_notes =
                            context.action.compose_action ==
                            WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS}});
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::from("Semantic layer was not deleted.",
                                                    notification_severity_t::info,
                                                    notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(applied),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto& state = context.environment.editor.commands;
    std::erase(state.project.semantic_layer.required_semantic_layer_filters, id);
    std::erase(state.project.semantic_layer.excluded_semantic_layer_filters, id);
    state.project.semantic_layer.semantic_layer_target.reset();
    state.project.semantic_layer.semantic_layer_target_none = false;
    state.project.compose_gesture.semantic_layer_paint_mode_active = false;
    state.project.semantic_layer.semantic_layer_solo_active = false;
    auto outcome = compose_action_outcome_t::from(std::string{"Semantic layer deleted."},
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct move_semantic_layer_catalog_entry_action_t {
    std::pair<const chart_document_t*, const semantic_layer_t*> target{};
    std::size_t index{};
    bool up{};
    auto operator==(const move_semantic_layer_catalog_entry_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<move_semantic_layer_catalog_entry_action_t, compose_action_context_t>);

auto move_semantic_layer_catalog_entry_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before reordering semantic layers.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    target = selected_catalog_layer(context);
    const auto& [chart, layer] = target;
    if (!chart || !layer)
        return !chart || !context.environment.editor.commands.project.semantic_layer
                              .semantic_layer_target
                   ? compose_action_outcome_t::blocked(
                         "Choose a visible semantic layer before reordering it.",
                         notification_source_t::edit)
                   : compose_action_outcome_t::blocked(
                         "The selected semantic layer is no longer available.",
                         notification_source_t::edit);
    const auto found = std::ranges::find(chart->semantic_layers, layer->id, &semantic_layer_t::id);
    index = static_cast<std::size_t>(std::distance(chart->semantic_layers.begin(), found));
    up = context.action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
    if ((up && index == 0) || (!up && index + 1 == chart->semantic_layers.size()))
        return compose_action_outcome_t::from("Semantic layer is already at that palette boundary.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);
    return std::nullopt;
}

auto move_semantic_layer_catalog_entry_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto* chart = target.first;
    std::vector<layer_id_t> order;
    order.reserve(chart->semantic_layers.size());
    for (const auto& item : chart->semantic_layers) order.push_back(item.id);
    std::swap(order[index], order[up ? index - 1 : index + 1]);
    return semantic_layer_edit_status(
        context, reorder_semantic_layers_action_t{context.state.chart, std::move(order)},
        "Semantic-layer order updated.", "Semantic-layer order was unchanged.",
        "Semantic-layer order updated, but workspace rebinding failed.");
}

void clear_semantic_layer_selection(application_command_state_t& app, bool clear_modes) {
    app.project.semantic_layer.semantic_layer_target.reset();
    app.project.semantic_layer.semantic_layer_target_none = false;
    if (!clear_modes) return;
    app.project.compose_gesture.semantic_layer_paint_mode_active = false;
    app.project.semantic_layer.semantic_layer_solo_active = false;
}

auto target_semantic_layer_action_row(compose_action_context_t& context) -> bool {
    using namespace wormedit;
    if (context.action.compose_semantic_layer_id == 0) return true;
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    const auto* layer = chart ? catalog_semantic_layer_by_id(
        *chart, layer_id_t{context.action.compose_semantic_layer_id}) : nullptr;
    if (!layer) return false;
    context.environment.editor.commands.project.semantic_layer.semantic_layer_target = layer->id;
    context.environment.editor.commands.project.semantic_layer.semantic_layer_target_none = false;
    return true;
}

auto target_captured_semantic_layer_action(compose_action_context_t& context,
                                           bool rejects_locked_target)
    -> std::optional<compose_action_outcome_t> {
    if (context.action.compose_semantic_layer_id == 0)
        return std::nullopt;
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    const auto* layer = chart ? catalog_semantic_layer_by_id(
        *chart, wormedit::layer_id_t{context.action.compose_semantic_layer_id}) : nullptr;
    if (!layer)
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer available.", notification_source_t::edit);
    if (layer->locked && rejects_locked_target)
        return compose_action_outcome_t::blocked("Layer is locked", notification_source_t::edit);
    (void)target_semantic_layer_action_row(context);
    return std::nullopt;
}

auto missing_semantic_layer_toggle_target(compose_action_context_t& context, bool toggle_lock)
    -> std::optional<compose_action_outcome_t> {
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.environment.editor.commands.project.semantic_layer.semantic_layer_target)
        return toggle_lock ? compose_action_outcome_t::blocked(
                                 "Choose a semantic layer before changing its lock state.",
                                 notification_source_t::edit)
                           : compose_action_outcome_t::blocked(
                                 "Choose a semantic layer before changing its visibility.",
                                 notification_source_t::edit);
    if (!layer) {
        clear_semantic_layer_selection(context.environment.editor.commands,
                                       toggle_lock);
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer available.", notification_source_t::edit);
    }
    return std::nullopt;
}

auto semantic_layer_toggle_status(bool toggle_lock, bool previous) -> std::string {
    if (toggle_lock) return previous ? "Semantic layer unlocked." : "Semantic layer locked.";
    return previous ? "Semantic layer hidden." : "Semantic layer shown.";
}

struct toggle_semantic_layer_visibility_or_lock_action_t {
    bool toggle_lock{};
    auto operator==(const toggle_semantic_layer_visibility_or_lock_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_semantic_layer_visibility_or_lock_action_t, compose_action_context_t>);

auto toggle_semantic_layer_visibility_or_lock_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            toggle_lock
                ? "Finish or cancel the active Compose gesture before changing layer lock state."
                : "Finish or cancel the active Compose gesture before changing layer "
                  "visibility.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto toggle_semantic_layer_visibility_or_lock_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    constexpr std::string_view rebind_failure =
        "Semantic-layer state changed, but workspace rebinding failed.";
    if (!target_semantic_layer_action_row(context))
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer available.", notification_source_t::edit);
    if (auto missing = missing_semantic_layer_toggle_target(context, toggle_lock))
        return std::move(*missing);
    const auto [chart, layer] = selected_catalog_layer(context);
    const bool previous = toggle_lock ? layer->locked : layer->visible;
    const auto color = semantic_layer_rgba_t{
        static_cast<double>(layer->color.red) / 255.0,
        static_cast<double>(layer->color.green) / 255.0,
        static_cast<double>(layer->color.blue) / 255.0,
        static_cast<double>(layer->color.alpha) / 255.0};
    auto applied = apply_semantic_layer_edit(
        context.environment.document.history,
        wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = context.state.chart,
            .id = layer->id,
            .value = {.name = layer->name,
                      .color = color,
                      .visible = toggle_lock ? layer->visible : !previous,
                      .locked = toggle_lock ? !previous : layer->locked}});
    if (!applied.applied)
        return applied.diagnostics.empty()
                   ? compose_action_outcome_t::from("Semantic-layer state was unchanged.",
                                                    notification_severity_t::info,
                                                    notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(applied),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    if (!toggle_lock && previous) {
        app.project.compose_gesture.semantic_layer_paint_mode_active = false;
        app.project.semantic_layer.semantic_layer_solo_active = false;
    }
    auto outcome = compose_action_outcome_t::from(
        std::string{semantic_layer_toggle_status(toggle_lock, previous)},
        notification_severity_t::info, notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.clear_semantic_layer_paint_after_rebind = toggle_lock && !previous;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct toggle_semantic_layer_filter_action_t {
    std::pair<const chart_document_t*, const semantic_layer_t*> target{};
    auto operator==(const toggle_semantic_layer_filter_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_semantic_layer_filter_action_t, compose_action_context_t>);

auto toggle_semantic_layer_filter_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    target = selected_catalog_layer(context);
    const auto& [chart, layer] = target;
    if (!chart || !layer)
        return compose_action_outcome_t::blocked(
            "Choose a visible semantic layer before filtering by it.", notification_source_t::edit);
    return std::nullopt;
}

auto toggle_semantic_layer_filter_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& [chart, layer] = target;
    auto& app = context.environment.editor.commands;
    if (app.project.semantic_layer.semantic_layer_filter_chart != chart->id) {
        app.project.semantic_layer.semantic_layer_filter_chart = chart->id;
        app.project.semantic_layer.required_semantic_layer_filters.clear();
        app.project.semantic_layer.excluded_semantic_layer_filters.clear();
    }
    const bool require =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
    auto& enabled = require ? app.project.semantic_layer.required_semantic_layer_filters : app.project.semantic_layer.excluded_semantic_layer_filters;
    auto& opposite = require ? app.project.semantic_layer.excluded_semantic_layer_filters : app.project.semantic_layer.required_semantic_layer_filters;
    if (const auto found = std::ranges::find(enabled, layer->id); found != enabled.end()) {
        enabled.erase(found);
        return compose_action_outcome_t::from(require ? "Required semantic-layer filter removed."
                                                      : "Excluded semantic-layer filter removed.",
                                              notification_severity_t::info,
                                              notification_source_t::input);
    }
    std::erase(opposite, layer->id);
    enabled.push_back(layer->id);
    return compose_action_outcome_t::from(require ? "Semantic layer required in the view filter."
                                                  : "Semantic layer excluded from the view filter.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

struct toggle_semantic_layer_paint_mode_action_t {
    auto operator==(const toggle_semantic_layer_paint_mode_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_semantic_layer_paint_mode_action_t, compose_action_context_t>);

auto toggle_semantic_layer_paint_mode_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto toggle_semantic_layer_paint_mode_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& state = context.state;
    auto& app = context.environment.editor.commands;
    if (state.is_pending()) {
        auto result = dispatch_editor_action(context.environment.document.history, state,
                                                   cancel_editor_gesture_action_t{});
        if (!result.accepted)
            return compose_action_outcome_t::from(std::move(result), "Compose gesture canceled.");
    }
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || (!app.project.semantic_layer.semantic_layer_target_none && (!layer || layer->locked))) {
        app.project.compose_gesture.semantic_layer_paint_mode_active = false;
        return compose_action_outcome_t::blocked(
            "Choose an unlocked visible semantic layer before painting note membership.",
            notification_source_t::edit);
    }
    app.project.compose_gesture.semantic_layer_paint_mode_active = !app.project.compose_gesture.semantic_layer_paint_mode_active;
    if (app.project.compose_gesture.semantic_layer_paint_mode_active) app.project.compose_gesture.note_paint_mode_active = false;
    return compose_action_outcome_t::from(
        app.project.compose_gesture.semantic_layer_paint_mode_active
            ? "Semantic-layer paint mode enabled."
            : "Semantic-layer paint mode disabled.",
        notification_severity_t::info, notification_source_t::input);
}

struct toggle_semantic_layer_solo_action_t {
    std::pair<const chart_document_t*, const semantic_layer_t*> target{};
    auto operator==(const toggle_semantic_layer_solo_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_semantic_layer_solo_action_t, compose_action_context_t>);

auto toggle_semantic_layer_solo_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    target = selected_catalog_layer(context);
    const auto& [chart, layer] = target;
    if (!chart || !context.environment.editor.commands.project.semantic_layer.semantic_layer_target)
        return compose_action_outcome_t::blocked(
            "Choose a visible semantic layer before soloing it.", notification_source_t::edit);
    return std::nullopt;
}

auto toggle_semantic_layer_solo_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto* layer = target.second;
    if (!layer) {
        context.environment.editor.commands.project.semantic_layer.semantic_layer_solo_active = false;
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer visible.", notification_source_t::edit);
    }
    auto& solo = context.environment.editor.commands.project.semantic_layer.semantic_layer_solo_active;
    solo = !solo;
    return compose_action_outcome_t::from(
        solo ? "Semantic-layer solo enabled." : "Semantic-layer solo disabled.",
        notification_severity_t::info, notification_source_t::input);
}

struct select_semantic_layer_notes_action_t {
    std::pair<const chart_document_t*, const semantic_layer_t*> target{};
    auto operator==(const select_semantic_layer_notes_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<select_semantic_layer_notes_action_t, compose_action_context_t>);

auto select_semantic_layer_notes_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before selecting by layer.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    target = selected_catalog_layer(context);
    const auto& [chart, layer] = target;
    if (!chart || !context.environment.editor.commands.project.semantic_layer.semantic_layer_target)
        return compose_action_outcome_t::blocked(
            "Choose a visible semantic layer before selecting its notes.",
            notification_source_t::edit);
    return std::nullopt;
}

auto select_semantic_layer_notes_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& [chart, layer] = target;
    if (!layer) {
        context.environment.editor.commands.project.semantic_layer.semantic_layer_target.reset();
        context.environment.editor.commands.project.semantic_layer.semantic_layer_target_none = false;
        context.environment.editor.commands.project.compose_gesture.semantic_layer_paint_mode_active = false;
        context.environment.editor.commands.project.semantic_layer.semantic_layer_solo_active = false;
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer visible.", notification_source_t::edit);
    }
    std::vector<object_id_t> notes;
    for (const auto& measure : chart->measures)
        for (const auto& note : measure.notes)
            if (std::ranges::find(note.semantic_layers, layer->id) != note.semantic_layers.end())
                notes.push_back(note.id);
    auto result = dispatch_editor_action(context.environment.document.history, context.state,
                                               select_editor_notes_action_t{std::move(notes)});
    return compose_action_outcome_t::from(std::move(result), "Semantic-layer notes selected.");
}

struct edit_semantic_layer_membership_action_t {
    auto operator==(const edit_semantic_layer_membership_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_semantic_layer_membership_action_t, compose_action_context_t>);

auto edit_semantic_layer_membership_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing semantic layers.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto edit_semantic_layer_membership_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& state = context.state;
    auto& app = context.environment.editor.commands;
    const auto apply = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER;
    const auto explicit_id = context.action.compose_semantic_layer_id;
    const auto candidate_none = explicit_id == 0 && app.project.semantic_layer.semantic_layer_target_none;
    if (candidate_none) {
        if (!apply)
            return compose_action_outcome_t::from("No layer only assigns",
                                                  notification_severity_t::info,
                                                  notification_source_t::input);
        return run_compose_action(assign_compose_layer_target_action_t{.none = true, .target = {}}, context);
    }
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    const auto candidate = explicit_id ? layer_id_t{explicit_id} : app.project.semantic_layer.semantic_layer_target.value_or(layer_id_t{});
    const auto* layer = chart ? catalog_semantic_layer_by_id(*chart, candidate) : nullptr;
    if (!chart || !layer ||
        (state.selected_notes.empty() && selected_compose_bgm_events(state).empty()))
        return compose_action_outcome_t::blocked(
            "Choose a semantic layer and select notes or BGM events first.",
            notification_source_t::edit);
    if (apply) return run_compose_action(assign_compose_layer_target_action_t{.none = false, .target = candidate}, context);
    return semantic_layer_edit_status(context,
        replace_notes_semantic_layer_action_t{
            state.chart, candidate, state.selected_notes, true,
            selected_compose_bgm_events(state)},
        "Semantic-layer membership updated.", "Semantic-layer membership was unchanged.",
        "Semantic-layer membership changed, but workspace rebinding failed.");
}

struct toggle_layer_assist_tick_action_t {
    const chart_document_t* chart{};
    layer_id_t id{};
    auto operator==(const toggle_layer_assist_tick_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_layer_assist_tick_action_t, compose_action_context_t>);

auto toggle_layer_assist_tick_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (context.action.compose_input_project_id !=
        context.environment.document.history.document.id.value)
        return compose_action_outcome_t::blocked(
            "Layer assist tick request belongs to a different project.",
            notification_source_t::edit);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing assist ticks.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    auto& app = context.environment.editor.commands;
    chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    id = layer_id_t{context.action.compose_semantic_layer_id};
    if (!chart || !catalog_semantic_layer_by_id(*chart, id))
        return compose_action_outcome_t::blocked(
            "The selected semantic layer is no longer available.", notification_source_t::edit);
    if (!app.metronome_settings().available || !app.services.transport_set_layer_assist)
        return compose_action_outcome_t::blocked(
            "Layer assist ticks need an available audio preview.", notification_source_t::edit);
    return std::nullopt;
}

auto toggle_layer_assist_tick_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& app = context.environment.editor.commands;
    auto layers = app.project.semantic_layer.layer_assist_chart == chart->id ? app.project.semantic_layer.layer_assist_layers : std::vector<layer_id_t>{};
    const auto existing = std::ranges::find(layers, id);
    const bool enabled = existing == layers.end();
    if (enabled) layers.push_back(id);
    else layers.erase(existing);
    if (!app.services.transport_set_layer_assist(chart->id, layers))
        return compose_action_outcome_t::blocked("Layer assist ticks could not be updated.",
                                                 notification_source_t::edit);
    app.project.semantic_layer.layer_assist_chart = chart->id;
    app.project.semantic_layer.layer_assist_layers = std::move(layers);
    return compose_action_outcome_t::from(
        enabled ? "Layer assist ticks enabled." : "Layer assist ticks disabled.",
        notification_severity_t::info, notification_source_t::input);
}

struct clear_semantic_layer_target_action_t {
    auto operator==(const clear_semantic_layer_target_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        context.environment.editor.commands.project.semantic_layer.semantic_layer_target.reset();
        context.environment.editor.commands.project.semantic_layer.semantic_layer_target_none = false;
        context.environment.editor.commands.project.compose_gesture.semantic_layer_paint_mode_active = false;
        context.environment.editor.commands.project.semantic_layer.semantic_layer_solo_active = false;
        return compose_action_outcome_t::from("Semantic-layer target cleared.",
                                              notification_severity_t::info,
                                              notification_source_t::input);
    }
};
static_assert(compose_action<clear_semantic_layer_target_action_t, compose_action_context_t>);

struct edit_compose_bms_branch_choice_action_t {
    auto operator==(const edit_compose_bms_branch_choice_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_compose_bms_branch_choice_action_t, compose_action_context_t>);

auto edit_compose_bms_branch_choice_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;

    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return compose_action_outcome_t::blocked(
            wormedit::stale_action_message("BMS branch action"), notification_source_t::format);
    }
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing a BMS branch.")) {
        blocked->source = notification_source_t::format;
        return std::move(*blocked);
    }
    if (action.compose_branch_id == 0) {
        return compose_action_outcome_t::blocked(
            "Choose a current BMS RANDOM or SWITCH control first.", notification_source_t::format);
    }
    return std::nullopt;
}

auto edit_compose_bms_branch_choice_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;
    auto applied = apply_bms_branch_choice_command(
        history,
        {.chart = *workspace.selected_chart,
         .branch = branch_id_t{action.compose_branch_id},
         .selected_value = action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
                               ? std::optional<std::int64_t>{action.compose_branch_value}
                               : std::nullopt});
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .success = action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
                           ? "BMS branch choice changed and its semantics were rematerialized."
                           : "BMS branch choice cleared and its semantics were rematerialized.",
            .unchanged = "BMS branch choice was rejected without changing the document.",
            .rebind_failure = "BMS branch changed, but workspace rebinding failed.",
            .rejected_source = notification_source_t::format,
            .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only,
            .unchanged_severity = notification_severity_t::warning,
            .is_unchanged_accepted = false});
}

void find_bms_statement_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                               const wormedit::bms_control_node_t* enclosing_leaf,
                               bms_statement_target_t& result) {
    const auto* leaf = wormedit::is_branch_body(node.kind) ? &node : enclosing_leaf;
    if (node.kind == wormedit::bms_control_kind_t::raw_statement && node.id.value == id) {
        result = {&node, leaf};
        return;
    }
    for (const auto& child : node.children) {
        find_bms_statement_target(child, id, leaf, result);
        if (result.statement) return;
    }
}

struct edit_compose_bms_branch_statement_action_t {
    const chart_document_t* chart{};
    static constexpr compose_action_description_t description{
        .success = "Inactive BMS branch statement updated.",
        .unchanged = "BMS branch statement edit was rejected without changing the document.",
        .rebind_failure = "BMS branch statement changed, but workspace rebinding failed.",
        .rejected_source = notification_source_t::format,
        .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only,
        .unchanged_severity = notification_severity_t::warning,
        .is_unchanged_accepted = false};
    auto operator==(const edit_compose_bms_branch_statement_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_compose_bms_branch_statement_action_t, compose_action_context_t>);

auto edit_compose_bms_branch_statement_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;

    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id)
        return compose_action_outcome_t::blocked(
            wormedit::stale_action_message("BMS branch statement action"),
            notification_source_t::format);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing a BMS branch.")) {
        blocked->source = notification_source_t::format;
        return std::move(*blocked);
    }
    if (action.compose_branch_statement_id == 0)
        return compose_action_outcome_t::blocked("Select a raw BMS branch statement first.",
                                                 notification_source_t::format);
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, action.compose_input_document_revision, "BMS branch statement action"))
        return compose_action_outcome_t::from(
            bms_branch_statement_command_result_t{.command = std::move(*rejected)}, description);
    chart = history.document.find_chart(*workspace.selected_chart);
    if (chart == nullptr)
        return compose_action_outcome_t::blocked("The selected BMS chart is no longer available.",
                                                 notification_source_t::format);
    return std::nullopt;
}

auto edit_compose_bms_branch_statement_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;
    if (!chart->bms_control_tree) {
        auto missing_tree = apply_bms_branch_statement_edit_command(
            history, {.chart = *workspace.selected_chart,
                      .statement = branch_id_t{action.compose_branch_statement_id}});
        return compose_action_outcome_t::from(std::move(missing_tree), description);
    }
    bms_statement_target_t target;
    find_bms_statement_target(*chart->bms_control_tree, action.compose_branch_statement_id,
                              nullptr, target);
    if (!target.statement) {
        auto missing_target = apply_bms_branch_statement_edit_command(
            history, {.chart = *workspace.selected_chart,
                      .statement = branch_id_t{action.compose_branch_statement_id}});
        return compose_action_outcome_t::from(std::move(missing_target), description);
    }

    constexpr std::size_t maximum_text_bytes = 4096;
    const bool has_text_action =
        action.compose_action == WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT;
    if (has_text_action && (!action.compose_branch_statement_text ||
                            action.compose_branch_statement_text_bytes == 0 ||
                            action.compose_branch_statement_text_bytes > maximum_text_bytes))
        return compose_action_outcome_t::blocked(
            "BMS branch statement text is empty or exceeds the editor's bounded capacity.",
            notification_source_t::format);
    const std::string text = has_text_action
        ? std::string(action.compose_branch_statement_text,
                      action.compose_branch_statement_text_bytes)
        : std::string{};
    if (!target.leaf && action.compose_action ==
                            WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT) {
        auto invalid = apply_bms_branch_statement_insert_command(
            history, {.chart = *workspace.selected_chart,
                      .after_statement = target.statement->id,
                      .expected_statement_source_order = target.statement->source_order,
                      .expected_statement_source_span = target.statement->source_span,
                      .expected_statement_original_text = target.statement->original_text,
                      .statement_text = text});
        return compose_action_outcome_t::from(std::move(invalid), description);
    }
    if (!target.leaf && action.compose_action ==
                            WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT) {
        auto invalid = apply_bms_branch_statement_delete_command(
            history, {.chart = *workspace.selected_chart,
                      .statement = target.statement->id,
                      .expected_source_order = target.statement->source_order,
                      .expected_source_span = target.statement->source_span,
                      .expected_original_text = target.statement->original_text});
        return compose_action_outcome_t::from(std::move(invalid), description);
    }
    bms_branch_statement_command_result_t applied;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT:
        applied = apply_bms_branch_statement_edit_command(
            history,
            {.chart = *workspace.selected_chart,
             .statement = target.statement->id,
             .expected_source_order = target.statement->source_order,
             .expected_source_span = target.statement->source_span,
             .expected_original_text = target.statement->original_text,
             .replacement_text = text});
        break;
    case WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT:
        applied = apply_bms_branch_statement_insert_command(
            history,
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
            history,
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
        return compose_action_outcome_t::blocked("Unknown BMS branch statement action.",
                                                 notification_source_t::format);
    }
    if (!applied)
        return compose_action_outcome_t::from(std::move(applied), description);
    return compose_action_outcome_t::from(std::move(applied), description);
}

// The #RANDOM block whose single if-group holds the leaf with this id.
void find_bms_leaf_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                          bms_leaf_target_t& result) {
    using kind_t = wormedit::bms_control_kind_t;
    if (node.kind == kind_t::random)
        for (const auto& group : node.children)
            for (const auto& leaf : group.children)
                if (leaf.id.value == id) {
                    result = {&node, &leaf};
                    return;
                }
    for (const auto& child : node.children) {
        find_bms_leaf_target(child, id, result);
        if (result.leaf) return;
    }
}

auto find_bms_control_node(const wormedit::bms_control_node_t& node, std::uint64_t id)
    -> const wormedit::bms_control_node_t* {
    if (node.id.value == id) return &node;
    for (const auto& child : node.children)
        if (const auto* found = find_bms_control_node(child, id)) return found;
    return nullptr;
}

auto run_bms_block_command(const WormUiWorkspaceAction& action,
                           wormedit::document_history_t& history,
                           const wormedit::chart_document_t& chart, wormedit::chart_id_t chart_id)
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr compose_action_description_t description{
        .unchanged = "BMS branch block edit was rejected without changing the document.",
        .rebind_failure = "BMS branch changed, but workspace rebinding failed.",
        .rejected_source = notification_source_t::format,
        .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only,
        .unchanged_severity = notification_severity_t::warning,
        .is_unchanged_accepted = false,
        .rebind_failure_policy = compose_rebind_failure_policy_t::message_only};
    const auto& tree = *chart.bms_control_tree;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK) {
        const auto* statement = find_bms_control_node(tree, action.compose_branch_statement_id);
        if (!statement || statement->kind != bms_control_kind_t::raw_statement)
            return compose_action_outcome_t::blocked(
                "Select a raw BMS statement at the top level first.",
                notification_source_t::format);
        return compose_action_outcome_t::from(
            apply_bms_branch_block_create_command(history,
                                                  {.chart = chart_id,
                                                   .first_statement = statement->id,
                                                   .first_source_order = statement->source_order,
                                                   .first_source_span = statement->source_span,
                                                   .first_original_text = statement->original_text,
                                                   .last_statement = statement->id,
                                                   .last_source_order = statement->source_order,
                                                   .last_source_span = statement->source_span,
                                                   .last_original_text = statement->original_text}),
            description, "BMS branch block created.");
    }
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_REMOVE_BMS_BRANCH_BLOCK) {
        const auto* block = find_bms_control_node(tree, action.compose_branch_id);
        if (!block)
            return compose_action_outcome_t::blocked("Select a BMS RANDOM block first.",
                                                     notification_source_t::format);
        return compose_action_outcome_t::from(
            apply_bms_branch_block_remove_command(history,
                                                  {.chart = chart_id,
                                                   .random_block = block->id,
                                                   .expected_source_order = block->source_order,
                                                   .expected_source_span = block->source_span,
                                                   .expected_original_text = block->original_text}),
            description, "Empty BMS branch block removed.");
    }
    bms_leaf_target_t target;
    find_bms_leaf_target(tree, action.compose_branch_id, target);
    if (!target.leaf)
        return compose_action_outcome_t::blocked("Select a BMS branch leaf first.",
                                                 notification_source_t::format);
    return compose_action_outcome_t::from(
        apply_bms_branch_leaf_move_command(
            history, {.chart = chart_id,
                      .random_block = target.block->id,
                      .leaf = target.leaf->id,
                      .expected_leaf_source_order = target.leaf->source_order,
                      .expected_leaf_original_text = target.leaf->original_text,
                      .direction = action.compose_branch_value < 0
                                       ? bms_branch_leaf_move_direction_t::up
                                       : bms_branch_leaf_move_direction_t::down}),
        description, "BMS branch leaf moved.");
}

struct edit_compose_bms_branch_block_action_t {
    const chart_document_t* chart{};
    auto operator==(const edit_compose_bms_branch_block_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_compose_bms_branch_block_action_t, compose_action_context_t>);

auto edit_compose_bms_branch_block_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;

    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id)
        return compose_action_outcome_t::blocked(
            wormedit::stale_action_message("BMS branch block action"),
            notification_source_t::format);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing a BMS branch.")) {
        blocked->source = notification_source_t::format;
        return std::move(*blocked);
    }
    chart = history.document.find_chart(*workspace.selected_chart);
    if (chart == nullptr || !chart->bms_control_tree)
        return compose_action_outcome_t::blocked("The selected chart has no BMS branch outline.",
                                                 notification_source_t::format);
    if (history.document.revision != action.compose_input_document_revision)
        return compose_action_outcome_t::blocked(
            wormedit::stale_action_message("BMS branch block action"),
            notification_source_t::format);
    return std::nullopt;
}

auto edit_compose_bms_branch_block_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.environment.document.history;
    auto& workspace = context.environment.document.workspace;
    return run_bms_block_command(action, history, *chart, *workspace.selected_chart);
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
    auto& editor = context.environment.editor.commands.project.quantize.quantize_editor;
    if (!editor || !editor->stamp.is_identity_current(
        context.environment.document.history.document, context.state.chart)) {
        const auto request = wormedit::quantize_request_t{
            .divisions = context.state.active_snap_denominator};
        editor = application_command_state_t::quantize_editor_t{
            .stamp = wormedit::chart_stamp_t::from(context.environment.document.history.document,
                                                   context.state.chart),
            .request = request};
    }
    return *editor;
}

// The Quantize grid and the editing snap are one grid: setting the target also sets the snap
// when the chart supports it. An unsupported grid stays quantize-only, silently.
void share_quantize_grid_with_snap(compose_action_context_t& context, std::uint64_t divisions) {
    const auto result =
        wormedit::dispatch_editor_action(context.environment.document.history, context.state,
                                         wormedit::set_edit_snap_action_t{divisions});
    if (result.accepted && context.state.active_snap_denominator == divisions)
        context.environment.editor.commands.project.compose_view.requested_snap_denominators
            .insert_or_assign(context.state.chart.value, divisions);
}

struct apply_compose_quantize_settings_action_t {
    auto operator==(const apply_compose_quantize_settings_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<apply_compose_quantize_settings_action_t, compose_action_context_t>);

auto apply_compose_quantize_settings_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto apply_compose_quantize_settings_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& editor = quantize_editor_for(context);
    const auto request = quantize_request_from_action(context.action, {});
    if (!request)
        return compose_action_outcome_t::blocked(
            "Quantization settings must use a positive grid and supported policies.",
            notification_source_t::edit);
    editor.request = *request;
    editor.preset_index.reset();
    share_quantize_grid_with_snap(context, request->divisions);
    return compose_action_outcome_t::from("Quantization settings updated.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

struct select_compose_quantize_preset_action_t {
    decltype(WormUiWorkspaceAction::compose_quantize_preset_index) index{};
    const quantization_preset_t* selected_preset{};
    auto operator==(const select_compose_quantize_preset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<select_compose_quantize_preset_action_t, compose_action_context_t>);

auto select_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    index = context.action.compose_quantize_preset_index;
    if (index >= context.environment.document.history.document.quantization_presets.size())
        return compose_action_outcome_t::blocked(
            "The selected quantization preset is no longer available.",
            notification_source_t::edit);
    selected_preset = &context.environment.document.history.document.quantization_presets[index];
    return std::nullopt;
}

auto select_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& preset = *selected_preset;
    auto& editor = quantize_editor_for(context);
    editor.request = {.divisions = preset.divisions, .space = preset.space, .tie = preset.tie,
                      .collisions = quantization_collision_t::reject, .holds = preset.holds};
    editor.preset_index = index;
    share_quantize_grid_with_snap(context, preset.divisions);
    return compose_action_outcome_t::from("Quantization preset selected.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
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
    if (std::ranges::any_of(context.environment.document.history.document.quantization_presets,
                            [&](const auto& preset) { return preset.name == name; }))
        return "A quantization preset already has that name.";
    if (context.environment.document.history.document.quantization_presets.size() >=
        quantization_preset_t::maximum_count)
        return "The project already has the maximum number of quantization presets.";
    if (!request || !quantize_settings_valid(*request))
        return "Quantization settings are invalid.";
    return std::nullopt;
}

struct save_compose_quantize_preset_action_t {
    std::optional<quantize_request_t> request{};
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<save_compose_quantize_preset_action_t, compose_action_context_t>);

auto save_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto length = strnlen(context.action.compose_quantize_preset_name,
                                sizeof(context.action.compose_quantize_preset_name));
    request = quantize_request_from_action(context.action, {});
    if (const auto error = validate_quantize_preset_save(context, length, request))
        return compose_action_outcome_t::blocked(*error, notification_source_t::edit);
    return std::nullopt;
}

auto save_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Quantization preset saved, but workspace rebinding failed.";
    auto transaction = document_command_transaction_t(context.environment.document.history);
    auto& proposed = transaction.document();
    proposed.quantization_presets.push_back({
        .name = context.action.compose_quantize_preset_name,
        .divisions = request->divisions, .space = request->space,
        .tie = request->tie, .holds = request->holds});
    const auto index = proposed.quantization_presets.size() - 1;
    auto committed = transaction.commit("Save quantization preset");
    if (!committed)
        return committed.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Quantization preset could not be saved.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(committed),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto& editor = quantize_editor_for(context);
    editor.request = *request;
    editor.preset_index = index;
    auto outcome = compose_action_outcome_t::from("Quantization preset saved.",
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, committed.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(committed.diagnostics);
    return outcome;
}

struct delete_compose_quantize_preset_action_t {
    decltype(WormUiWorkspaceAction::compose_quantize_preset_index) index{};
    auto operator==(const delete_compose_quantize_preset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<delete_compose_quantize_preset_action_t, compose_action_context_t>);

auto delete_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    index = context.action.compose_quantize_preset_index;
    if (index >= context.environment.document.history.document.quantization_presets.size())
        return compose_action_outcome_t::blocked(
            "The selected quantization preset is no longer available.",
            notification_source_t::edit);
    return std::nullopt;
}

auto delete_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Quantization preset deleted, but workspace rebinding failed.";
    auto transaction = document_command_transaction_t(context.environment.document.history);
    auto& proposed = transaction.document();
    proposed.quantization_presets.erase(proposed.quantization_presets.begin() +
                                        static_cast<std::ptrdiff_t>(index));
    auto committed = transaction.commit("Delete quantization preset");
    if (!committed)
        return committed.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Quantization preset could not be deleted.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(committed),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto& editor = quantize_editor_for(context);
    if (editor.preset_index) {
        if (*editor.preset_index == index) editor.preset_index.reset();
        else if (*editor.preset_index > index) --*editor.preset_index;
    }
    auto outcome = compose_action_outcome_t::from("Quantization preset deleted.",
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, committed.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(committed.diagnostics);
    return outcome;
}

struct rename_compose_quantize_preset_action_t {
    decltype(WormUiWorkspaceAction::compose_quantize_preset_index) index{};
    std::string_view name{};
    auto operator==(const rename_compose_quantize_preset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<rename_compose_quantize_preset_action_t, compose_action_context_t>);

auto rename_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    index = context.action.compose_quantize_preset_index;
    const auto& presets = context.environment.document.history.document.quantization_presets;
    if (index >= presets.size())
        return compose_action_outcome_t::blocked(
            "The selected quantization preset is no longer available.",
            notification_source_t::edit);
    const auto length = strnlen(context.action.compose_quantize_preset_name,
                                sizeof(context.action.compose_quantize_preset_name));
    if (length == 0 || length > quantization_preset_t::maximum_name_bytes)
        return compose_action_outcome_t::blocked(
            "Enter a name for the quantization preset (maximum 64 bytes).",
            notification_source_t::edit);
    name = std::string_view(context.action.compose_quantize_preset_name, length);
    for (std::size_t other = 0; other < presets.size(); ++other)
        if (other != index && presets[other].name == name)
            return compose_action_outcome_t::blocked("A quantization preset already has that name.",
                                                     notification_source_t::edit);
    if (presets[index].name == name)
        return compose_action_outcome_t::from("Quantization preset name was unchanged.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);
    return std::nullopt;
}

auto rename_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Quantization preset renamed, but workspace rebinding failed.";
    auto transaction = document_command_transaction_t(context.environment.document.history);
    auto& proposed = transaction.document();
    proposed.quantization_presets[index].name = name;
    auto committed = transaction.commit("Rename quantization preset");
    if (!committed)
        return committed.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Quantization preset could not be renamed.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(committed),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto outcome = compose_action_outcome_t::from("Quantization preset renamed.",
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, committed.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(committed.diagnostics);
    return outcome;
}

struct set_compose_edit_snap_action_t {
    static auto from(const compose_action_context_t& context) -> set_compose_edit_snap_action_t {
        return {.divisions = context.action.compose_snap_denominator};
    }
    std::uint64_t divisions{};
    auto operator==(const set_compose_edit_snap_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_compose_edit_snap_action_t, compose_action_context_t>);

auto set_compose_edit_snap_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing snapping."))
        return *blocked;
    if (context.environment.editor.commands.project.clipboard.paste_session)
        return compose_action_outcome_t::blocked(
            "Finish or cancel the active Compose gesture before changing snapping.",
            notification_source_t::edit);
    if (std::ranges::find(context.state.supported_snap_denominators, divisions) ==
        context.state.supported_snap_denominators.end())
        return compose_action_outcome_t::blocked("This chart cannot snap to that grid.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto set_compose_edit_snap_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto result =
        wormedit::dispatch_editor_action(context.environment.document.history, context.state,
                                         wormedit::set_edit_snap_action_t{divisions});
    if (!result.accepted)
        return compose_action_outcome_t::from(std::move(result), "Editing grid was unchanged.");
    context.environment.editor.commands.project.compose_view.requested_snap_denominators.insert_or_assign(
        context.state.chart.value, divisions);
    auto& editor = quantize_editor_for(context);
    editor.request.divisions = divisions;
    editor.preset_index.reset();
    return compose_action_outcome_t::from("Editing grid: 1/" + std::to_string(divisions) + ".",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

auto selected_snap_positions(const chart_document_t& chart, const editor_interaction_state_t& state)
    -> std::vector<local_position_t> {
    const std::unordered_set<std::uint64_t> notes = [&] {
        std::unordered_set<std::uint64_t> ids;
        for (const auto id : state.selected_notes) ids.insert(id.value);
        return ids;
    }();
    std::unordered_set<std::uint64_t> bgm;
    for (const auto id : selected_compose_bgm_events(state)) bgm.insert(id.value);
    std::vector<local_position_t> positions;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!notes.contains(note.id.value)) continue;
            positions.push_back(note.position);
            if (note.end) positions.push_back(*note.end);
        }
        for (const auto& event : measure.bgm_events)
            if (bgm.contains(event.id.value)) positions.push_back(event.position);
    }
    return positions;
}

struct find_compose_selection_snap_action_t {
    std::uint64_t divisions{};
    std::vector<std::uint64_t> previous{};
    auto operator==(const find_compose_selection_snap_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) -> compose_action_outcome_t;
};
static_assert(compose_action<find_compose_selection_snap_action_t, compose_action_context_t>);

auto find_compose_selection_snap_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (context.action.compose_input_project_id !=
        context.environment.document.history.document.id.value)
        return compose_action_outcome_t::blocked(
            "Fitting snap request belongs to a different project.", notification_source_t::edit);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing snapping."))
        return *blocked;
    if (context.environment.editor.commands.project.clipboard.paste_session)
        return compose_action_outcome_t::blocked(
            "Finish or cancel the active Compose gesture before changing snapping.",
            notification_source_t::edit);
    const auto* chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("Select a chart first.",
                                                 notification_source_t::edit);
    const auto positions = selected_snap_positions(*chart, context.state);
    const auto& geometry = context.environment.document.history.indices(chart->id)->measures;
    if (!geometry)
        return compose_action_outcome_t::blocked("This chart has invalid measure geometry.",
                                                 notification_source_t::edit);
    const auto fitting = fitting_snap_denominator(*chart, *geometry, positions);
    if (!fitting)
        return compose_action_outcome_t::blocked(
            "Select notes or BGM events to find a fitting snap.", notification_source_t::edit);
    divisions = *fitting;
    // Downbeats/thirds use familiar quarter/triplet grids when those contain every point.
    if (divisions < 4 && chart->divisions_per_standard_measure % (divisions * 4) == 0)
        divisions *= 4;
    previous = context.state.supported_snap_denominators;
    return std::nullopt;
}

auto find_compose_selection_snap_action_t::perform(compose_action_context_t& context)
    -> compose_action_outcome_t {
    if (std::ranges::find(previous, divisions) == previous.end())
        context.state.supported_snap_denominators.push_back(divisions);
    auto result = run_compose_action(set_compose_edit_snap_action_t{.divisions = divisions}, context);
    if (context.state.active_snap_denominator != divisions)
        context.state.supported_snap_denominators = std::move(previous);
    return result;
}

struct toggle_compose_quantize_preview_action_t {
    auto operator==(const toggle_compose_quantize_preview_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_compose_quantize_preview_action_t, compose_action_context_t>);

auto toggle_compose_quantize_preview_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    invoke_compose_quantize_command(true, context.environment.document.history, context.state,
                                    context.environment.editor.commands);
    return compose_action_outcome_t::from(
        context.environment.editor.commands.project.quantize.quantize_editor->preview_enabled
            ? "Quantize preview enabled."
            : "Quantize preview disabled.",
        notification_severity_t::info, notification_source_t::input);
}

} // namespace compose_action_detail

auto invoke_compose_quantize_command(bool toggle_preview, document_history_t& history,
                                     editor_interaction_state_t& state,
                                     application_command_state_t& application)
    -> compose_action_outcome_t {
    auto& editor = application.project.quantize.quantize_editor;
    if (!editor || !editor->stamp.is_identity_current(history.document, state.chart))
        editor = application_command_state_t::quantize_editor_t{
            .stamp = wormedit::chart_stamp_t::from(history.document, state.chart),
            .request = {.divisions = state.active_snap_denominator}};
    if (toggle_preview) {
        editor->preview_enabled = !editor->preview_enabled;
        return compose_action_outcome_t::from({}, notification_severity_t::info,
                                              notification_source_t::input);
    }
    if (!can_apply_compose_quantize(application, state))
        return compose_action_outcome_t::blocked(compose_quantize_idle_message,
                                                 notification_source_t::input);
    WormUiWorkspaceAction action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
    action.compose_input_project_id = history.document.id.value;
    action.compose_input_chart_id = state.chart.value;
    action.compose_input_document_revision = history.document.revision;
    action.compose_quantize_divisions = editor->request.divisions;
    action.compose_quantize_space = static_cast<std::uint8_t>(editor->request.space);
    action.compose_quantize_tie = static_cast<std::uint8_t>(editor->request.tie);
    action.compose_quantize_holds = static_cast<std::uint8_t>(editor->request.holds);
    return apply_compose_quantize_selection(history, state, application, action);
}

struct toggle_compose_bga_visibility_action_t {
    const chart_document_t* chart{};
    auto operator==(const toggle_compose_bga_visibility_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_compose_bga_visibility_action_t, compose_action_context_t>);

auto toggle_compose_bga_visibility_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before changing the BGA lane.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    chart =
        selected_chart(context.environment.document.history, context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("Choose a chart before changing the BGA lane.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto toggle_compose_bga_visibility_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto mask = wormedit::editor_bga_visible_lane_mask(*chart, context.state.bga_revealed_lane_mask);
    const auto visible = context.state.bga_lane_visible && mask != 0;
    context.state.bga_lane_visible = !visible;
    if (!visible && mask == 0) context.state.bga_revealed_lane_mask |= 1U;
    return compose_action_outcome_t::from(visible ? "BGA lane hidden." : "BGA lane shown.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

namespace compose_action_detail {

auto cancel_compose_bmson_marker_batch(compose_action_context_t& context)
    -> compose_action_outcome_t {
    context.environment.editor.commands.project.compose_gesture.bmson_marker_draft.reset();
    return compose_action_outcome_t::from("BMSON marker batch canceled.",
                                          notification_severity_t::hint,
                                          notification_source_t::input);
}

namespace {
constexpr auto local_route_entries = std::array{
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK,
                           .operation = run_routed_compose_action<compose_bookmark_action_t, true>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK,
                           .operation =
                               run_routed_compose_action<compose_bookmark_action_t, false>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE,
                           .operation =
                               run_routed_compose_action<navigate_compose_measure_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT,
                           .operation =
                               run_routed_compose_action<set_compose_bgm_column_count_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE,
                           .operation =
                               run_routed_compose_action<set_compose_lane_profile_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE,
                           .operation =
                               run_routed_compose_action<apply_compose_lane_layout_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET,
                           .operation =
                               run_routed_compose_action<edit_compose_audio_offset_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION,
                           .operation =
                               run_routed_compose_action<toggle_compose_scroll_direction_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ZOOM_IN,
                           .operation = run_routed_compose_action<compose_zoom_action_t, true>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ZOOM_OUT,
                           .operation = run_routed_compose_action<compose_zoom_action_t, false>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SCROLL_UP,
                           .operation = run_routed_compose_action<compose_scroll_action_t, -1>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SCROLL_DOWN,
                           .operation = run_routed_compose_action<compose_scroll_action_t, 1>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RESET_VIEW,
                           .operation = run_routed_compose_action<reset_compose_view_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_TOOL,
                           .operation = run_routed_compose_action<select_compose_tool_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DRAW_TOOL,
                           .operation = run_routed_compose_action<select_compose_tool_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_LAYER_TOOL,
                           .operation = run_routed_compose_action<select_compose_tool_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE,
                           .operation =
                               run_routed_compose_action<toggle_compose_note_paint_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RATE_SLOWER,
                           .operation = run_routed_compose_action<step_compose_rate_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RATE_FASTER,
                           .operation = run_routed_compose_action<step_compose_rate_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS,
                           .operation =
                               run_routed_compose_action<toggle_compose_timing_focus_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_EDIT_SNAP,
                           .operation =
                               run_routed_compose_action<toggle_compose_edit_snap_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK,
                           .operation = run_routed_compose_action<toggle_compose_y_lock_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY,
                           .operation =
                               run_routed_compose_action<toggle_compose_timing_lane_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM,
                           .operation = run_routed_compose_action<change_bmson_lane_mode_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_PACK_BMSON_CHANNEL,
                           .operation = run_routed_compose_action<change_bmson_lane_mode_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL,
                           .operation = run_routed_compose_action<change_bmson_lane_mode_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART,
                           .operation =
                               run_routed_compose_action<set_bmson_selection_restart_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE,
                           .operation =
                               run_routed_compose_action<set_bmson_selection_restart_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE,
                           .operation = run_routed_compose_action<assign_bmson_selection_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM,
                           .operation = run_routed_compose_action<assign_bmson_selection_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR,
                           .operation =
                               run_routed_compose_action<move_bmson_selection_to_cursor_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR,
                           .operation =
                               run_routed_compose_action<insert_bmson_marker_at_cursor_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR,
                           .operation =
                               run_routed_compose_action<insert_bmson_marker_at_cursor_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR,
                           .operation =
                               run_routed_compose_action<stage_bmson_marker_at_cursor_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR,
                           .operation =
                               run_routed_compose_action<stage_bmson_marker_at_cursor_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM,
                           .operation =
                               run_routed_compose_action<apply_bmson_marker_batch_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER,
                           .operation =
                               run_routed_compose_action<delete_bmson_selected_marker_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU,
                           .operation =
                               run_routed_compose_action<open_compose_note_layer_menu_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_OPEN_SELECTED_SOUND_LAYER_MENU,
                           .operation =
                               run_routed_compose_action<open_compose_sound_layer_menu_action_t>},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER,
        .operation = run_routed_compose_action<add_selected_compose_sounds_to_layer_action_t>},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW,
        .operation = run_routed_compose_action<set_compose_note_layer_menu_window_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU,
                           .operation =
                               run_routed_compose_action<close_compose_note_layer_menu_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER,
                           .operation =
                               run_routed_compose_action<assign_compose_note_menu_layer_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW,
                           .operation =
                               run_routed_compose_action<set_semantic_layer_window_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER,
                           .operation =
                               run_routed_compose_action<choose_no_semantic_layer_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER,
                           .operation =
                               run_routed_compose_action<select_semantic_layer_target_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE,
                           .operation =
                               run_routed_compose_action<step_semantic_layer_palette_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE,
                           .operation =
                               run_routed_compose_action<step_semantic_layer_palette_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER,
                           .operation = run_routed_compose_action<add_semantic_layer_action_t>},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER,
        .operation = run_routed_compose_action<update_semantic_layer_catalog_entry_action_t>,
        .captures_semantic_target = true,
        .rejects_locked_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR,
        .operation = run_routed_compose_action<update_semantic_layer_catalog_entry_action_t>,
        .captures_semantic_target = true,
        .rejects_locked_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER,
        .operation = run_routed_compose_action<delete_semantic_layer_catalog_entry_action_t>,
        .captures_semantic_target = true,
        .rejects_locked_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS,
        .operation = run_routed_compose_action<delete_semantic_layer_catalog_entry_action_t>,
        .captures_semantic_target = true,
        .rejects_locked_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP,
        .operation = run_routed_compose_action<move_semantic_layer_catalog_entry_action_t>,
        .captures_semantic_target = true,
        .rejects_locked_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN,
        .operation = run_routed_compose_action<move_semantic_layer_catalog_entry_action_t>,
        .captures_semantic_target = true,
        .rejects_locked_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY,
        .operation =
            run_routed_compose_action<toggle_semantic_layer_visibility_or_lock_action_t, false>,
        .captures_semantic_target = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK,
        .operation =
            run_routed_compose_action<toggle_semantic_layer_visibility_or_lock_action_t, true>,
        .captures_semantic_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER,
                           .operation =
                               run_routed_compose_action<toggle_semantic_layer_filter_action_t>,
                           .captures_semantic_target = true,
                           .rejects_locked_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER,
                           .operation =
                               run_routed_compose_action<toggle_semantic_layer_filter_action_t>,
                           .captures_semantic_target = true,
                           .rejects_locked_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER,
                           .operation =
                               run_routed_compose_action<clear_semantic_layer_target_action_t>},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE,
        .operation = run_routed_compose_action<toggle_semantic_layer_paint_mode_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO,
                           .operation =
                               run_routed_compose_action<toggle_semantic_layer_solo_action_t>,
                           .captures_semantic_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES,
                           .operation =
                               run_routed_compose_action<select_semantic_layer_notes_action_t>,
                           .captures_semantic_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER,
                           .operation =
                               run_routed_compose_action<edit_semantic_layer_membership_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER,
                           .operation =
                               run_routed_compose_action<edit_semantic_layer_membership_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_QUANTIZE_PREVIEW,
                           .operation =
                               run_routed_compose_action<toggle_compose_quantize_preview_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP,
                           .operation = run_routed_compose_action<set_compose_edit_snap_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP,
                           .operation =
                               run_routed_compose_action<find_compose_selection_snap_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
                           .operation =
                               run_routed_compose_action<apply_compose_quantize_settings_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<select_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<save_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<delete_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<rename_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY,
                           .operation =
                               run_routed_compose_action<toggle_compose_bga_visibility_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK,
                           .operation =
                               run_routed_compose_action<toggle_layer_assist_tick_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM,
                           .operation = cancel_compose_bmson_marker_batch},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE,
                           .operation =
                               run_routed_compose_action<edit_compose_bms_branch_choice_action_t>,
                           .is_workspace_only = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE,
                           .operation =
                               run_routed_compose_action<edit_compose_bms_branch_choice_action_t>,
                           .is_workspace_only = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT,
        .operation = run_routed_compose_action<edit_compose_bms_branch_statement_action_t>,
        .is_workspace_only = true,
        .has_bms_statement_feedback = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT,
        .operation = run_routed_compose_action<edit_compose_bms_branch_statement_action_t>,
        .is_workspace_only = true,
        .has_bms_statement_feedback = true},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT,
        .operation = run_routed_compose_action<edit_compose_bms_branch_statement_action_t>,
        .is_workspace_only = true,
        .has_bms_statement_feedback = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK,
                           .operation =
                               run_routed_compose_action<edit_compose_bms_branch_block_action_t>,
                           .is_workspace_only = true,
                           .has_bms_statement_feedback = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_REMOVE_BMS_BRANCH_BLOCK,
                           .operation =
                               run_routed_compose_action<edit_compose_bms_branch_block_action_t>,
                           .is_workspace_only = true,
                           .has_bms_statement_feedback = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF,
                           .operation =
                               run_routed_compose_action<edit_compose_bms_branch_block_action_t>,
                           .is_workspace_only = true,
                           .has_bms_statement_feedback = true},
};
} // namespace
constinit const std::span<const compose_action_route_t> compose_local_routes{local_route_entries};

namespace {
constexpr auto compose_kind_count =
    static_cast<std::size_t>(WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER) + 1;
auto build_compose_action_routes() -> std::array<compose_action_route_t, compose_kind_count> {
    std::array<compose_action_route_t, compose_kind_count> routes{};
    for (const auto module :
         {compose_local_routes, compose_resource_routes, compose_marker_routes,
          compose_bookmark_routes, compose_area_routes, compose_bga_routes, compose_sound_routes}) {
        for (const auto& route : module) {
            const auto index = static_cast<std::size_t>(route.kind);
            assert(index < routes.size() && !routes[index].operation);
            routes[index] = route;
        }
    }
    return routes;
}
const auto compose_routes = build_compose_action_routes();
} // namespace

auto compose_action_route_t::find(WormUiComposeActionKind kind) -> const compose_action_route_t* {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= compose_routes.size() || !compose_routes[index].operation)
        return nullptr;
    return &compose_routes[index];
}

auto process_compose_action_boundary(compose_action_context_t& context,
                                     const compose_action_route_t* route)
    -> std::optional<compose_action_outcome_t> {
    if (route && route->bypasses_area_clear)
        return std::nullopt;
    auto& application = context.environment.editor.commands;
    const auto& action = context.action;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE) {
        if (application.project.compose_view.compose_timing_focus ||
            application.project.clipboard.paste_session)
            return compose_action_outcome_t::blocked(compose_quantize_idle_message,
                                                     notification_source_t::input);
        if (auto blocked = context.require_no_gesture(compose_quantize_idle_message)) {
            blocked->source = notification_source_t::input;
            return blocked;
        }
    }
    if (compose_area_pause_conversion(action.compose_action) &&
        (application.project.clipboard.paste_session ||
         application.project.compose_gesture.bga_draw_stroke ||
         application.project.compose_gesture.bmson_razor_stroke)) {
        return compose_action_outcome_t::from(
            application.project.clipboard.paste_session
                ? "Finish the paste before converting the area."
                : "Finish the current edit before converting the area.",
            notification_severity_t::hint, notification_source_t::edit);
    }
    return process_compose_area_boundary(context);
}

auto perform_routed_compose_action(compose_action_context_t& context,
                                   const compose_action_route_t& route)
    -> compose_action_outcome_t {
    if (route.captures_semantic_target) {
        if (auto blocked =
                target_captured_semantic_layer_action(context, route.rejects_locked_target))
            return std::move(*blocked);
    }
    if (route.requires_resources) {
        if (auto blocked = context.require_resources())
            return std::move(*blocked);
    }
    auto outcome = route.operation(context);
    outcome.has_bms_statement_feedback = route.has_bms_statement_feedback;
    return outcome;
}

auto perform_compose_view_action(compose_action_context_t& context) -> compose_action_outcome_t {
    const auto* route = compose_action_route_t::find(context.action.compose_action);
    if (auto blocked = process_compose_action_boundary(context, route))
        return std::move(*blocked);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        return {.delivery = compose_feedback_delivery_t::none};
    if (!route || route->is_standalone_only)
        return compose_action_outcome_t::blocked(
            compose_result_status(editor_interaction_result_t{}, {}), notification_source_t::input);
    if (route->is_workspace_only) {
        auto outcome = compose_action_outcome_t::blocked(
            "BMS branch actions must use the workspace action boundary.",
            notification_source_t::format, compose_feedback_delivery_t::notification);
        return outcome;
    }
    return perform_routed_compose_action(context, *route);
}
} // namespace compose_action_detail
auto resource_path_tab_action(const WormUiWorkspaceAction& action,
                              wormedit::workspace_tab_t tab) -> bool {
    if (tab == wormedit::workspace_tab_t::audio_slices)
        return action.compose_action == WORM_UI_COMPOSE_ACTION_AUDITION_SOUND ||
            action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
            action.compose_action == WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM ||
            action.compose_action == WORM_UI_COMPOSE_ACTION_PACK_BMSON_CHANNEL ||
            action.compose_action == WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL;
    return tab == wormedit::workspace_tab_t::metadata &&
        (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT);
}

auto is_initial_layout_workspace_action(const WormUiWorkspaceAction& action) -> bool {
    return action.compose_action == WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE;
}

auto is_chart_only_workspace_action(const WormUiWorkspaceAction& action) -> bool {
    return action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT ||
        is_initial_layout_workspace_action(action) ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK;
}

auto temporary_compose_interaction_is_current(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, bool initial_layout_action) -> bool {
    const auto* selected = selected_chart(history, workspace);
    return workspace.active_tab == wormedit::workspace_tab_t::compose &&
        workspace.selected_chart && selected &&
        action.compose_input_chart_id == workspace.selected_chart->value &&
        action.compose_input_document_revision == history.document.revision &&
        (action.compose_action != WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK ||
         action.compose_input_project_id == history.document.id.value) &&
        (!initial_layout_action || selected->lane_layouts.empty());
}

auto temporary_compose_interaction_expired_message(bool initial_layout_action, bool sound_relink) -> std::string {
    if (sound_relink) return wormedit::stale_action_message("Sound relink");
    return initial_layout_action
        ? wormedit::stale_action_message("Lane layout change")
        : wormedit::stale_action_message("BGM column count edit");
}

auto dispatch_compose_view_action(const WormUiWorkspaceAction& action,
                                  const compose_action_inputs_t& environment,
                                  wormedit::editor_interaction_state_t& state,
                                  double viewport_height, bool metadata_import_busy,
                                  bool post_notifications) -> std::string {
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    auto& application_state = environment.editor.commands;
    const notification_sink_t notifications =
        post_notifications ? environment.document.notifications : notification_sink_t::muted();

    using namespace wormedit;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE &&
        !action.compose_area_clear_requested) return {};
    const bool metadata_path_action = resource_path_tab_action(action, workspace.active_tab);
    const chart_stamp_t stamp{
        .document = {history.document.id, action.compose_input_document_revision},
        .chart = chart_id_t{action.compose_input_chart_id}};
    if ((workspace.active_tab != workspace_tab_t::compose && !metadata_path_action) ||
        !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        !stamp.is_current(history.document, state.chart)) {
        const auto message = stale_action_message("Compose view action");
        notifications.post(notification_severity_t::warning, notification_source_t::edit, message);
        return message;
    }
    const auto* selected_chart_it = history.document.find_chart(state.chart);
    const auto scroll_direction =
        selected_chart_it != nullptr &&
                compose_field_downscroll(*selected_chart_it,
                                         application_state.preferences.editor_preferences)
            ? editor_field_scroll_direction_t::down
            : editor_field_scroll_direction_t::up;
    compose_action_context_t context{
        environment,          action,           state,        viewport_height,
        metadata_import_busy, scroll_direction, notifications};
    return finish_compose_action(context, perform_compose_view_action(context));
}

auto dispatch_project_export_action(const WormUiWorkspaceAction& action,
                                    const compose_action_inputs_t& environment,
                                    bool metadata_import_busy) -> bool {
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    auto& retained = environment.editor.retained;
    auto& application = environment.editor.commands;
    auto& notifications = environment.document.notifications;

    if (action.compose_action != WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP) return false;
    const auto* chart = selected_chart(history, workspace);
    if (!chart || action.compose_input_project_id != history.document.id.value ||
        action.compose_input_chart_id != chart->id.value ||
        action.compose_input_document_revision != history.document.revision) {
        notifications.post(notification_severity_t::warning, notification_source_t::export_,
                           wormedit::stale_action_message("Export"));
        return true;
    }
    if (application.project.clipboard.paste_session || (retained && retained->is_pending())) {
        notifications.post(notification_severity_t::warning, notification_source_t::export_,
                           "Finish or cancel the active editor gesture before exporting.");
        return true;
    }
    wormedit::editor_interaction_state_t state{};
    state.chart = chart->id;
    const auto index = action.compose_output_group_index;
    if (index < history.document.output_groups.size() && !history.document.output_groups[index].charts.empty())
        state.chart = history.document.output_groups[index].charts.front();
    compose_action_context_t context{environment,
                                     action,
                                     state,
                                     0,
                                     metadata_import_busy,
                                     wormedit::editor_field_scroll_direction_t::up,
                                     notifications};
    const auto* route = compose_action_route_t::find(action.compose_action);
    assert(route);
    static_cast<void>(
        finish_compose_action(context, perform_routed_compose_action(context, *route)));
    return true;
}

namespace {
auto dispatch_compose_workspace_action_with_resources(const WormUiWorkspaceAction& action,
                                                      const compose_action_inputs_t& environment,
                                                      double viewport_height,
                                                      bool metadata_import_busy) -> bool {
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    auto& interaction_state = environment.editor.retained;
    auto& application_state = environment.editor.commands;
    auto& metadata_drop = environment.resources->metadata.drop;
    auto& metadata_asset_runtime = environment.resources->metadata.runtime;
    auto& notifications = environment.document.notifications;

    if (dispatch_project_export_action(action, environment, metadata_import_busy))
        return true;
    editor_interaction_state_t no_interaction{};
    auto& bms_state = interaction_state ? *interaction_state : no_interaction;
    compose_action_context_t bms_context{environment,
                                         action,
                                         bms_state,
                                         viewport_height,
                                         metadata_import_busy,
                                         editor_field_scroll_direction_t::up,
                                         notifications};
    const auto* route = compose_action_route_t::find(action.compose_action);
    if (route && route->is_workspace_only) {
        static_cast<void>(
            finish_compose_action(bms_context, perform_routed_compose_action(bms_context, *route)));
        return true;
    }
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE &&
        !action.compose_area_clear_requested) {
        return false;
    }

    const bool initial_layout_action = is_initial_layout_workspace_action(action);
    if (!interaction_state && is_chart_only_workspace_action(action)) {
        if (!temporary_compose_interaction_is_current(action, history, workspace,
                                                      initial_layout_action)) {
            notifications.post_feedback(
                notification_severity_t::warning, notification_source_t::edit,
                temporary_compose_interaction_expired_message(
                    initial_layout_action,
                    action.compose_action == WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK));
            return true;
        }
        wormedit::editor_interaction_state_t chart_interaction;
        chart_interaction.chart = *workspace.selected_chart;
        const auto status = dispatch_compose_view_action(
            action, environment, chart_interaction, viewport_height, metadata_import_busy, true);
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
            action, environment, metadata_path_action ? metadata_interaction : *interaction_state,
            viewport_height, metadata_import_busy, true);
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
} // namespace

auto dispatch_compose_workspace_action(const WormUiWorkspaceAction& action,
                                       const compose_action_inputs_t& environment,
                                       double viewport_height, bool metadata_import_busy) -> bool {
    if (!environment.resources)
        return false;
    return dispatch_compose_workspace_action_with_resources(action, environment, viewport_height,
                                                            metadata_import_busy);
}

} // namespace wormedit::sys
