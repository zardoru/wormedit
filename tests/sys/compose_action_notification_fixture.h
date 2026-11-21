#pragma once
// Includes, helpers and the action-notification fixtures shared by the compose action tests.
#include "sys/compose/actions/compose_audition_requests.h"
#include "compose_window_overloads_view.h"
#include "compose_window_overloads_sound.h"
#include "compose_window_overloads_input.h"
#include "sys/app/application_defaults.h"
#include "compose_scene_cache_fixture.h"
#include "sys/compose/actions/compose_sound_actions.h"
#include "../support/workspace_result_assertions.h"
#include "../support/timing_links_fixture.h"
#include "../support/temporary_directory.h"
#include "../sys/application_dispatch_fixture.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/compose/actions/compose_actions.h"
#include "sys/compose/actions/compose_area_actions.h"
#include "sys/compose/actions/compose_quantize.h"
#include "sys/compose/actions/compose_quantize_request.h"
#include "sys/compose/input/compose_input.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "sys/compose/views/recipe_form.h"
#include <cstdio>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/bms.h>
#include <wormedit/audio/slice_render.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/command.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/interaction_cursor_pose.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/workspace.h>
#include <chrono>
#include <bit>
#include <fstream>
#include <thread>
#include <source_location>
using namespace wormedit;
using namespace wormedit::sys;

namespace {
void check_action(bool value, std::source_location source = std::source_location::current()) {
    INFO("Compose action check at " << source.line());
    CHECK(value);
}
void require_action(bool value, std::source_location source = std::source_location::current()) {
    INFO("Compose action requirement at " << source.line());
    REQUIRE(value);
}
void check_action_false(bool value, std::source_location source = std::source_location::current()) {
    check_action(!value, source);
}
void require_action_false(bool value, std::source_location source = std::source_location::current()) {
    require_action(!value, source);
}
auto same_bgm_event(const bgm_event_t& left, const bgm_event_t& right) -> bool {
    return left.id == right.id && left.position == right.position &&
        left.source_lane == right.source_lane && left.sound == right.sound &&
        left.sound_channel_event == right.sound_channel_event &&
        left.structural_slot == right.structural_slot && left.branch == right.branch &&
        left.semantic_layers == right.semantic_layers;
}
auto action_notification_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "notification-test", .display_name = "Notification test",
                   .lanes = {{.id = lane_id_t{1}, .name = "Key", .role = lane_role_t::playable}}}}};
    song.charts.push_back(std::move(chart));
    return song;
}

struct action_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    document_history_t history{document_history_t::from(action_notification_song())};
    workspace_controller_t workspace{*workspace_controller_t::from(history).value};
    editor_interaction_state_t state;
    application_command_state_t application;
    std::filesystem::path asset_base;
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{scheduler, sink};
    runtime::output_group_export_t export_runtime{sink};

    action_notification_fixture_t() {
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart_id_t{1};
        state.chart = chart_id_t{1};
        state.active_lane = lane_id_t{1};
        state.active_snap_denominator = 4;
        state.supported_snap_denominators = {4};
        state.cursor = {measure_id_t{1}, 0};
    }
    void capture_hold() {
        state.active_gesture = proposal_t<hold_gesture_t, document_stamp_t>{
            .stamp = document_stamp_t::from(history.document.revision),
            .payload = hold_gesture_t{pending_hold_t{}}};
    }
    auto action(WormUiComposeActionKind kind) const -> wormedit::ui::workspace_action_t {
        wormedit::ui::workspace_action_t value{};
        value.compose.kind = kind;
        value.stamp.chart_id.value = state.chart.value;
        value.stamp.project_id.value = history.document.id.value;
        value.stamp.document_revision = history.document.revision;
        return value;
    }
    auto dispatch(const wormedit::ui::workspace_action_t& value, bool post_notifications = true)
        -> std::string {
        return dispatch_compose_view_action(value, history, workspace, state, 800, application,
                                            asset_base, scheduler, resource_copy, export_runtime,
                                            false, sink, post_notifications);
    }
    auto dispatch_workspace(const wormedit::ui::workspace_action_t& value,
                            std::optional<editor_interaction_state_t>& interaction) -> bool {
        metadata_drop_state_t drop;
        runtime::metadata_asset_t metadata{sink};
        const compose_action_inputs_t environment{
            .document = {.history = history, .workspace = workspace, .notifications = sink,
                         .auditions = &application.project.auditions},
            .editor = {application, interaction},
            .resources = compose_resource_inputs_t{.asset_base = asset_base, .jobs = {scheduler, resource_copy, export_runtime},
                                                   .metadata = {drop, metadata}}};
        return dispatch_compose_workspace_action(value, environment, 800, false);
    }
    auto invoke_registered(const char* id) -> bool {
        std::optional<editor_interaction_state_t> interaction = state;
        std::optional<timing_lane_pseudonote_outcome_t> timing;
        const auto registry = command_registry_t::from([&](const auto& request) {
            const auto command = parse_application_command(request.command.value);
            return command ? invoke_application_command(*command, history, workspace, interaction,
                timing, application, sink) : dispatch_outcome_t{};
        });
        const auto result = invoke_command(registry, {id}, {.workspace_active = true,
            .active_tab = workspace_tab_t::compose, .visible_tabs = {workspace_tab_t::compose}});
        state = *interaction;
        return result.value.is_invoked();
    }
    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{});
        return center.snapshot(256);
    }
};

struct bga_input_fixture_t : action_notification_fixture_t {
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    compose_scene_cache_t cache;
    bga_input_fixture_t() {
        history.document.visual_assets = {{.id = asset_id_t{61}, .kind = asset_kind_t::image},
            {.id = asset_id_t{62}, .kind = asset_kind_t::video}};
        history.document.resources = {{.id = resource_id_t{31}, .asset = asset_id_t{61}, .roles = {resource_role_t::bga}},
            {.id = resource_id_t{32}, .asset = asset_id_t{62}, .roles = {resource_role_t::bga}}};
        history.document.charts.front().bga_events = {{.id = object_id_t{41},
            .position = {measure_id_t{1}, 240}, .resource = resource_id_t{31}, .kind = visual_event_kind_t::layer}};
        state.selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{41}}};
        state.bga_revealed_lane_mask = 4;
        state.viewport_height_pixels = 600;
    }
    auto input(WormUiComposeInputKind kind, std::uint8_t lane_kind) -> wormedit::ui::workspace_action_t {
        auto value = action(WORM_UI_COMPOSE_ACTION_NONE);
        value.input.compose_input_viewport_width_pixels = 900;
        value.input.compose_inputs.resize(1);
        value.input.compose_inputs[0] = {.kind = kind, .object_id = {41},
            .timing_chart_id = {1}, .timing_document_revision = history.document.revision,
            .resource_role = wormedit::resource_role_t::bga, .x_pixels = 150,
            .y_pixels = compose_source_y(state.pixels_per_standard_measure / 4 - state.viewport_scroll_pixels,
                state.viewport_height_pixels, compose_field_downscroll(history.document.charts.front(), application.preferences.editor_preferences)),
            .bga_lane_kind = static_cast<wormedit::visual_event_kind_t>(lane_kind), .bga_lane_kind_valid = true};
        return value;
    }
    void check_unchanged(std::uint64_t revision) const {
        check_action(history.document.revision == revision);
        check_action(history.undo.empty());
        const auto& event = history.document.charts.front().bga_events.front();
        check_action(event.kind == visual_event_kind_t::layer);
        check_action(event.resource == resource_id_t{31});
        check_action(event.position == local_position_t{measure_id_t{1}, 240});
    }
    auto dispatch_input(wormedit::ui::workspace_action_t& value) -> std::string {
        return dispatch_compose_input_action(value, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }
};
} // namespace
