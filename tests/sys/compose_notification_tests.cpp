#include "sys/application_command_dispatch.h"
#include "sys/compose_input.h"

#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/notifications.h>

#include <array>
#include <optional>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct compose_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    document_history_t history;
    workspace_controller_t workspace;
    editor_interaction_state_t state;
    std::optional<editor_interaction_state_t> optional_state;
    std::optional<timing_lane_pseudonote_preview_t> timing_stage;
    application_command_state_t application;
    compose_scene_cache_t cache;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;

    compose_notification_fixture_t() {
        chart_document_t chart{
            .id = chart_id_t{7}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}}},
            .lane_layouts = {{.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
                .layout = {.profile_id = "notification-test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .bpms = {{.id = object_id_t{11}, .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}}}};
        chart.measures.front().notes = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 120}, .lane = lane_id_t{1}}};
        chart.semantic_layers = {{.id = layer_id_t{8}, .name = "Layer", .color = {1, 2, 3, 255}}};
        history = make_document_history({.id = song_id_t{1}, .revision = 9, .charts = {chart}});
        constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
        const auto setup = make_editor_interaction(history, chart_id_t{7}, lane_id_t{1},
                                                   {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup);
        state = *setup.state;
        state.selected_notes = {object_id_t{1}};
        state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}}};
        optional_state = state;
        const auto binding = make_workspace_controller(history);
        REQUIRE(binding);
        workspace = *binding.workspace;
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart_id_t{7};
        application.semantic_layer_target = layer_id_t{8};
    }

    auto drain() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }

    auto input(WormUiComposeInputKind kind) -> std::string {
        WormUiWorkspaceAction action{};
        action.compose_input_chart_id = state.chart.value;
        action.compose_input_document_revision = history.document.revision;
        action.compose_input_viewport_width_pixels = 800;
        action.compose_inputs[0] = {.kind = kind};
        action.compose_input_count = 1;
        return dispatch_compose_input_action(action, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }
};

void expect_entry(const notification_entry_t& entry, notification_severity_t severity,
                  notification_source_t source, std::string_view message) {
    CHECK(entry.severity == severity);
    CHECK(entry.source == source);
    CHECK(entry.message == message);
}

auto dispatch_stale(compose_notification_fixture_t& fixture, std::uint64_t revision)
    -> std::string {
    WormUiWorkspaceAction stale{};
    stale.compose_input_chart_id = fixture.state.chart.value;
    stale.compose_input_document_revision = revision - 1;
    stale.compose_input_viewport_width_pixels = 800;
    return dispatch_compose_input_action(stale, fixture.history, fixture.workspace,
        fixture.state, fixture.viewport, fixture.navigation, fixture.application,
        fixture.cache, 0, &fixture.sink);
}

void check_legacy_seek(compose_notification_fixture_t& fixture) {
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.preview_playhead_absolute_row = 120.0;
    fixture.application.transport_seek = [](std::uint64_t) {
        return std::string{"Legacy seek performed."};
    };
    editor_navigation_action_batch_t actions;
    actions.actions[actions.count++] = editor_navigation_action_t::cursor_forward;
    const auto status = dispatch_compose_navigation_actions(actions, fixture.history,
        fixture.state, &fixture.application, &fixture.sink);
    CHECK(status == "Legacy seek performed.");
    const auto entries = fixture.drain();
    REQUIRE(entries.empty());
    REQUIRE(fixture.center.current_line() != nullptr);
    expect_entry(*fixture.center.current_line(), notification_severity_t::hint, notification_source_t::input,
                 "Legacy seek performed.");
}

void check_producer_seek(compose_notification_fixture_t& fixture) {
    fixture.application.transport_seek_posts_feedback = true;
    fixture.application.transport_seek = [&](std::uint64_t) {
        fixture.sink.post(notification_severity_t::warning, notification_source_t::audio,
                          "Audio seek was clamped.");
        return std::string{"Audio seek was clamped."};
    };
    editor_navigation_action_batch_t actions;
    actions.actions[actions.count++] = editor_navigation_action_t::cursor_forward;
    const auto status = dispatch_compose_navigation_actions(actions, fixture.history,
        fixture.state, &fixture.application, &fixture.sink);
    CHECK(status == "Audio seek was clamped.");
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::warning, notification_source_t::audio,
                 "Audio seek was clamped.");
    CHECK(fixture.center.statistics().posted == 1);
}

void check_navigation_hint(compose_notification_fixture_t& fixture) {
    const auto revision = fixture.history.document.revision;
    CHECK(fixture.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL) == "Compose navigation canceled.");
    const auto entries = fixture.drain();
    REQUIRE(entries.empty());
    REQUIRE(fixture.center.current_line() != nullptr);
    expect_entry(*fixture.center.current_line(), notification_severity_t::hint, notification_source_t::input,
                 "Compose navigation canceled.");
    CHECK(fixture.history.document.revision == revision);
}

void check_stale_rejection(compose_notification_fixture_t& fixture,
                           std::uint64_t revision) {
    CHECK_FALSE(dispatch_stale(fixture, revision).empty());
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::warning, notification_source_t::input,
                 "Compose input ignored because the chart or document revision changed.");
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}
}

TEST_CASE("Compose edit success stays current after a later navigation cancellation", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::semantic_layer_apply}, fixture.history,
        fixture.workspace, fixture.optional_state, fixture.timing_stage,
        fixture.application, fixture.sink);
    REQUIRE(result.accepted);
    fixture.state = *fixture.optional_state;
    auto kind = WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL;
    auto message = "Compose navigation canceled.";
    SECTION("navigation cancellation") {}
    SECTION("generic gesture cancellation") {
        kind = WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE;
        message = "Compose gesture canceled.";
    }
    CHECK(fixture.input(kind) == message);
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::success, notification_source_t::edit,
                 "Semantic-layer membership updated.");
    REQUIRE(fixture.center.current_line() != nullptr);
    CHECK(fixture.center.current_line()->severity == notification_severity_t::success);
}

TEST_CASE("Compose navigation hints and stale input rejections are typed without mutation", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    const auto before = fixture.history.document.revision;
    check_navigation_hint(fixture);
    check_stale_rejection(fixture, before);
}

TEST_CASE("Compose accepted history no-op is informational and does not mutate", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    CHECK(fixture.input(WORM_UI_COMPOSE_INPUT_UNDO) == "Nothing to undo.");
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::info, notification_source_t::edit,
                 "Nothing to undo.");
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Idle Compose input emits no notification", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    WormUiWorkspaceAction idle{};
    idle.compose_input_chart_id = fixture.state.chart.value;
    idle.compose_input_document_revision = fixture.history.document.revision;
    idle.compose_input_viewport_width_pixels = 800;
    (void)dispatch_compose_input_action(idle, fixture.history, fixture.workspace, fixture.state,
        fixture.viewport, fixture.navigation, fixture.application, fixture.cache, 0, &fixture.sink);
    CHECK(fixture.drain().empty());
}

TEST_CASE("Playhead navigation preserves legacy and producer-owned seek feedback", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    check_legacy_seek(fixture);
    check_producer_seek(fixture);
}

TEST_CASE("Viewport wheel seek feedback keeps explicit callback ownership", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.preview_playhead_absolute_row = 120.0;
    fixture.application.transport_seek = [](std::uint64_t) { return std::string{"Legacy wheel seek."}; };
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = fixture.state.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = 1, .cursor_x_pixels = 1, .cursor_y_pixels = 1};
    auto dispatch = [&] { return dispatch_compose_input_action(action, fixture.history,
        fixture.workspace, fixture.state, fixture.viewport, fixture.navigation,
        fixture.application, fixture.cache, 0, &fixture.sink); };
    CHECK(dispatch() == "Legacy wheel seek.");
    auto entries = fixture.drain();
    REQUIRE(entries.empty());
    REQUIRE(fixture.center.current_line() != nullptr);
    expect_entry(*fixture.center.current_line(), notification_severity_t::hint, notification_source_t::input,
        "Legacy wheel seek.");
    fixture.application.transport_seek_posts_feedback = true;
    fixture.application.transport_seek = [&](std::uint64_t) {
        fixture.sink.post(notification_severity_t::warning, notification_source_t::audio,
            "Producer wheel seek clamped.");
        return std::string{"Producer wheel seek clamped."};
    };
    CHECK(dispatch() == "Producer wheel seek clamped.");
    entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::warning, notification_source_t::audio,
        "Producer wheel seek clamped.");
    CHECK(fixture.center.statistics().posted == 1);
}

TEST_CASE("Paste cancellation keeps the original stop boundary", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    REQUIRE(invoke_application_command({.kind = application_command_kind_t::semantic_layer_apply},
        fixture.history, fixture.workspace, fixture.optional_state, fixture.timing_stage,
        fixture.application, fixture.sink).accepted);
    fixture.state = *fixture.optional_state;
    const auto revision = fixture.history.document.revision;
    const auto undo_size = fixture.history.undo.size();
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = fixture.state.chart.value;
    action.compose_input_document_revision = revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_input_count = 2;
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_PASTE_CANCEL;
    action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_UNDO;
    CHECK(dispatch_compose_input_action(action, fixture.history, fixture.workspace, fixture.state,
        fixture.viewport, fixture.navigation, fixture.application, fixture.cache, 0,
        &fixture.sink) == "Paste preview cancelled.");
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::success, notification_source_t::edit,
                 "Semantic-layer membership updated.");
    expect_entry(*fixture.center.current_line(), notification_severity_t::success, notification_source_t::edit,
                 "Semantic-layer membership updated.");
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.size() == undo_size);
}

TEST_CASE("Unchanged BGM move keeps the original stop boundary", "[unit][sys][compose-notification]") {
    compose_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::arrow_up;
    chart.bgm_source_lanes = {{.id = object_id_t{4}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{45},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{4}}};
    fixture.state.viewport_height_pixels = 400;
    fixture.state.pixels_per_standard_measure = 240;
    fixture.state.viewport_scroll_pixels = 0;
    fixture.application.bmson_marker_move_preview = application_command_state_t::bmson_marker_move_preview_t{
        .chart = fixture.state.chart, .revision = fixture.history.document.revision,
        .event = object_id_t{45}, .source_lane = object_id_t{4}, .linked_bmson = false,
        .snap_denominator = 4, .target = {measure_id_t{1}, 240}, .valid = true};
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = fixture.state.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_input_count = 2;
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE;
    action.compose_inputs[0].object_id = 45;
    action.compose_inputs[0].lane_id = 4;
    action.compose_inputs[0].y_pixels = 60;
    action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_UNDO;
    CHECK(dispatch_compose_input_action(action, fixture.history, fixture.workspace, fixture.state,
        fixture.viewport, fixture.navigation, fixture.application, fixture.cache, 0,
        &fixture.sink) == "BGM event remained at its original position.");
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries[0], notification_severity_t::info, notification_source_t::edit,
        "BGM event remained at its original position.");
    CHECK(fixture.history.undo.empty());
}
