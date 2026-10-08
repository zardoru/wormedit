#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_input.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/recipe_form.h"
#include <cstdio>

#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/notifications.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <source_location>

// Native drop hit testing is unrelated to the action dispatcher in this
// headless target; no rendered metadata pane can accept a file drop.
WormUiMetadataDropTarget worm_ui_frontend_metadata_drop_target(uint32_t, float, float) {
    return {};
}

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
    document_history_t history{make_document_history(action_notification_song())};
    workspace_controller_t workspace{*make_workspace_controller(history).workspace};
    editor_interaction_state_t state;
    application_command_state_t application;
    std::filesystem::path asset_base;
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{sink};
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
    auto action(WormUiComposeActionKind kind) const -> WormUiWorkspaceAction {
        WormUiWorkspaceAction value{};
        value.compose_action = kind;
        value.compose_input_chart_id = state.chart.value;
        value.compose_input_project_id = history.document.id.value;
        value.compose_input_document_revision = history.document.revision;
        return value;
    }
    auto dispatch(const WormUiWorkspaceAction& value) -> std::string {
        return dispatch_compose_view_action(value, history, workspace, state, 800, application,
            asset_base, scheduler, resource_copy, export_runtime, false, sink);
    }
    auto dispatch_workspace(const WormUiWorkspaceAction& value,
                            std::optional<editor_interaction_state_t>& interaction) -> bool {
        metadata_drop_state_t drop;
        runtime::metadata_asset_t metadata{sink};
        return dispatch_compose_workspace_action(value, history, workspace, interaction, 800,
            application, asset_base, scheduler, resource_copy, export_runtime, false, drop,
            metadata, sink);
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
    auto input(WormUiComposeInputKind kind, std::uint8_t lane_kind) -> WormUiWorkspaceAction {
        auto value = action(WORM_UI_COMPOSE_ACTION_NONE);
        value.compose_input_viewport_width_pixels = 900;
        value.compose_input_count = 1;
        value.compose_inputs[0] = {.kind = kind, .object_id = 41,
            .timing_chart_id = 1, .timing_document_revision = history.document.revision,
            .resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA, .x_pixels = 150,
            .y_pixels = compose_source_y(state.pixels_per_standard_measure / 4 - state.viewport_scroll_pixels,
                state.viewport_height_pixels, compose_field_downscroll(history.document.charts.front(), application.editor_preferences)),
            .bga_lane_kind = lane_kind, .bga_lane_kind_valid = true};
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
    auto dispatch_input(WormUiWorkspaceAction& value) -> std::string {
        return dispatch_compose_input_action(value, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }
};
} // namespace

TEST_CASE("BGA production input changes Layer to Layer 2 with one undo", "[bga][compose-input]") {
    bga_input_fixture_t fixture;
    auto input = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 5);
    INFO(fixture.dispatch_input(input));
    require_action(fixture.history.undo.size() == 1);
    const auto& event = fixture.history.document.charts.front().bga_events.front();
    check_action(event.kind == visual_event_kind_t::layer2);
    check_action(event.resource == resource_id_t{31});
    check_action(event.position == local_position_t{measure_id_t{1}, 240});
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().bga_events.front().kind == visual_event_kind_t::layer);
}

TEST_CASE("BGA production dock placement targets Layer 2", "[bga][compose-input]") {
    bga_input_fixture_t fixture;
    auto input = fixture.input(WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE, 5);
    input.compose_inputs[0].object_id = 31;
    INFO(fixture.dispatch_input(input));
    require_action(fixture.history.undo.size() == 1);
    require_action(fixture.history.document.charts.front().bga_events.size() == 2);
    check_action(fixture.history.document.charts.front().bga_events.back().kind == visual_event_kind_t::layer2);
}

TEST_CASE("BGA lane reveal is view-only and rejects an unsupported kind", "[bga][compose-input]") {
    bga_input_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto reveal = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL, 1);
    fixture.dispatch_input(reveal);
    check_action(fixture.state.bga_revealed_lane_mask == 12);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    reveal = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL, 255);
    fixture.dispatch_input(reveal);
    check_action(fixture.state.bga_revealed_lane_mask == 12);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("BGA production invalid and stale move payloads preserve state", "[bga][compose-input]") {
    bga_input_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto invalid = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 255);
    fixture.dispatch_input(invalid);
    invalid = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 5);
    invalid.compose_inputs[0].timing_chart_id = 99;
    fixture.dispatch_input(invalid);
    invalid = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 5);
    invalid.compose_input_document_revision = revision + 1;
    fixture.dispatch_input(invalid);
    fixture.check_unchanged(revision);
    check_action(fixture.state.selected_objects.size() == 1);
}

TEST_CASE("BGA production video overlay placement and replacement reject unchanged", "[bga][compose-input]") {
    bga_input_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto input = fixture.input(WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE, 5);
    input.compose_inputs[0].object_id = 32;
    check_action(fixture.dispatch_input(input).find("Video can only") != std::string::npos);
    input = fixture.input(WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE, 2);
    input.compose_inputs[0].object_id = 32;
    input.compose_inputs[0].lane_id = 41;
    check_action(fixture.dispatch_input(input).find("Video can only") != std::string::npos);
    fixture.check_unchanged(revision);
}

TEST_CASE("Compose dispatcher posts a semantic edit once and preserves undo", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    require_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
            "Semantic layer added.");
    require_action(fixture.history.document.charts.front().semantic_layers.size() == 1);
    check_action(fixture.history.document.revision != revision);
    check_action(fixture.history.undo.size() == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
}

TEST_CASE("Compose dispatcher rejects stale actions without changing history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    --action.compose_input_document_revision;
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(action) == "Compose view action ignored because the chart or document revision changed.");
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
}

TEST_CASE("Compose NONE remains quiet even with an absent chart", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.workspace.selected_chart.reset();
    check_action(fixture.dispatch(WormUiWorkspaceAction{}) == "");
    check_action(fixture.entries().empty());
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose area navigation and validation keep typed source and unchanged history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    SECTION("clear is a hint") {
        fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR)) == "Selected area cleared.");
        check_action_false(fixture.state.retained_selection_range.has_value());
        const auto entries = fixture.entries();
        require_action(entries.empty());
        require_action(fixture.center.current_line() != nullptr);
        check_action(fixture.center.current_line()->severity == notification_severity_t::hint);
        check_action(fixture.center.current_line()->source == notification_source_t::input);
    }
    SECTION("invalid geometry is rejected") {
        auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET);
        action.compose_area_begin_row = 960;
        action.compose_area_end_row = 960;
        const auto revision = fixture.history.document.revision;
        check_action_false(fixture.dispatch(action).empty());
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::warning);
        check_action(fixture.history.document.revision == revision);
        check_action_false(fixture.state.retained_selection_range.has_value());
    }
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose BGA no-op remains producer-owned and posts info once", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA)) == "No unused BGA resources.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::info);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose bookmark seek callback owns its feedback", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .anchor = marker_anchor_t::beat, .position = local_position_t{measure_id_t{1}, 480}}};
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.transport_seek_posts_feedback = true;
    fixture.application.transport_seek = [&](std::uint64_t) {
        fixture.sink.post(notification_severity_t::hint, notification_source_t::audio, "Playback cursor moved.");
        return std::string{"Playback cursor moved."};
    };
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK);
    action.compose_bookmark_id = 4;
    check_action(fixture.dispatch(action) == "Playback cursor moved.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().count == 1);
    check_action(entries.front().source == notification_source_t::audio);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose pending gesture rejects an edit without changing history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.pending_hold = pending_hold_t{};
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
          "Finish or cancel the active Compose gesture before adding a semantic layer.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
    check_action(fixture.state.pending_hold.has_value());
}

TEST_CASE("Compose lane profile dispatcher creates the first BGM-only layout",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile.bindings.clear();
    const auto choices = lane_profile_choices();
    const auto bms7k = make_bms_7k_profile();
    const auto target = std::ranges::find(choices, bms7k.layout.profile_id, [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    });
    REQUIRE(target != choices.end());
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    action.compose_lane_profile_index = static_cast<std::uint32_t>(target - choices.begin());
    CHECK(fixture.dispatch(action).starts_with("Lane profile set to "));
    REQUIRE(chart.lane_layouts.size() == 1);
    CHECK(chart.lane_layouts.front().layout.profile_id == bms7k.layout.profile_id);
    CHECK_FALSE(chart.input_profile.bindings.empty());
}

TEST_CASE("initial BGM-only profile assignment preserves its BGM event",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile.bindings.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{20}, .name = "BGM", .source_order = 0}};
    chart.measures.front().bgm_events = {{.id = object_id_t{21},
        .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{20}}};
    const auto original_id = chart.measures.front().bgm_events.front().id;
    const auto choices = lane_profile_choices();
    const auto bms7k = make_bms_7k_profile();
    const auto target = std::ranges::find(choices, bms7k.layout.profile_id, [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    });
    REQUIRE(target != choices.end());
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    action.compose_lane_profile_index = static_cast<std::uint32_t>(target - choices.begin());
    REQUIRE(fixture.dispatch(action).starts_with("Lane profile set to "));
    REQUIRE(chart.measures.front().bgm_events.size() == 1);
    CHECK(chart.measures.front().bgm_events.front().id == original_id);
    CHECK(chart.measures.front().bgm_events.front().position.row == 120);
}

TEST_CASE("undo removes an initial BGM-only lane profile without changing BGM",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile.bindings.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{20}, .name = "BGM", .source_order = 0}};
    chart.measures.front().bgm_events = {{.id = object_id_t{21},
        .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{20}}};
    const auto original_id = chart.measures.front().bgm_events.front().id;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    const auto choices = lane_profile_choices();
    const auto bms7k = make_bms_7k_profile();
    const auto target = std::ranges::find(choices, bms7k.layout.profile_id, [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    });
    require_action(target != choices.end());
    action.compose_lane_profile_index = static_cast<std::uint32_t>(target - choices.begin());
    require_action(fixture.dispatch(action).starts_with("Lane profile set to "));
    require_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(chart.lane_layouts.empty());
    require_action(chart.measures.front().bgm_events.size() == 1);
    check_action(chart.measures.front().bgm_events.front().id == original_id);
}

TEST_CASE("Compose lane profile picker retains the active gesture warning without a layout",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().lane_layouts.clear();
    fixture.state.pending_hold = pending_hold_t{};
    const auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    check_action(fixture.dispatch(action) ==
        "Finish or cancel the active Compose gesture before changing the lane profile.");
    CHECK(fixture.history.undo.empty());
    CHECK(fixture.history.document.charts.front().lane_layouts.empty());
}

TEST_CASE("BGM-only workspace boundary applies the emitted first-profile action",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile.bindings.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{20}, .name = "BGM", .source_order = 0}};
    chart.measures.front().bgm_events = {{.id = object_id_t{21},
        .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{20}}};
    const auto original_bgm = chart.measures.front().bgm_events.front();
    const auto choices = lane_profile_choices();
    const auto target = std::ranges::find(choices, "bms-10k-scratchless", [](const auto& choice) {
        return std::string_view{choice.layout.profile_id};
    });
    REQUIRE(target != choices.end());
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    action.compose_lane_profile_index = static_cast<std::uint32_t>(target - choices.begin());
    std::optional<editor_interaction_state_t> interaction;
    require_action(fixture.dispatch_workspace(action, interaction));
    const auto& applied_chart = fixture.history.document.charts.front();
    require_action(applied_chart.lane_layouts.size() == 1);
    check_action(applied_chart.lane_layouts.front().layout.profile_id == "bms-10k-scratchless");
    check_action((applied_chart.lane_layouts.front().position == local_position_t{measure_id_t{1}, 0}));
    check_action_false(applied_chart.input_profile.bindings.empty());
    require_action(applied_chart.measures.front().bgm_events.size() == 1);
    check_action(same_bgm_event(applied_chart.measures.front().bgm_events.front(), original_bgm));
    require_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    const auto& undone_chart = fixture.history.document.charts.front();
    check_action(undone_chart.lane_layouts.empty());
    check_action(same_bgm_event(undone_chart.measures.front().bgm_events.front(), original_bgm));
}

TEST_CASE("BGM-only workspace boundary inserts an initial layout profile",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile.bindings.clear();
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE);
    action.compose_measure_number = 1;
    std::snprintf(action.compose_lane_layout_profile_id,
                  sizeof(action.compose_lane_layout_profile_id), "%s", "bms-10k-scratchless");
    std::optional<editor_interaction_state_t> interaction;
    require_action(fixture.dispatch_workspace(action, interaction));
    const auto& applied_chart = fixture.history.document.charts.front();
    require_action(applied_chart.lane_layouts.size() == 1);
    check_action(applied_chart.lane_layouts.front().layout.profile_id == "bms-10k-scratchless");
    check_action_false(applied_chart.input_profile.bindings.empty());
    check_action(fixture.history.undo.size() == 1);
}

TEST_CASE("No-state first-profile workspace action rejects stale and existing layouts",
          "[compose-action-notifications][lane-profile]") {
    action_notification_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.clear();
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    std::optional<editor_interaction_state_t> interaction;
    SECTION("wrong chart") { ++action.compose_input_chart_id; }
    SECTION("stale revision") { --action.compose_input_document_revision; }
    SECTION("chart already has a layout") {
        chart.lane_layouts = action_notification_song().charts.front().lane_layouts;
    }
    const auto revision = fixture.history.document.revision;
    require_action(fixture.dispatch_workspace(action, interaction));
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(chart.lane_layouts.empty() == (action.compose_input_chart_id != fixture.state.chart.value ||
        action.compose_input_document_revision != revision));
}

TEST_CASE("Compose area recipe mutation records one success and one undo", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE)) ==
          "SV recipe added over the selected area.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    check_action(fixture.history.undo.size() == 1);
    require_action(fixture.history.document.charts.front().sv_recipes.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().sv_recipes.empty());
}

TEST_CASE("Compose area quantize preserves actual core rejection severity", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.selected_notes = {object_id_t{999}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE);
    action.compose_quantize_divisions = 4;
    const auto revision = fixture.history.document.revision;
    const auto message = fixture.dispatch(action);
    require_action_false(message.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().message == message);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose mutation keeps rebind failure visible with document severity", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.workspace.song = song_id_t{99};
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
          "workspace cannot be rebound to a different song identity");
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::document);
    check_action(entries.back().severity == notification_severity_t::success);
    check_action(entries.back().source == notification_source_t::edit);
    check_action(entries.back().message == "Semantic layer added.");
    check_action(entries.front().count == 1);
    check_action(entries.back().count == 1);
    check_action(fixture.history.document.charts.front().semantic_layers.size() == 1);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
}

TEST_CASE("Compose measure navigation posts a hint without editing", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.cursor = {measure_id_t{1}, 480};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE);
    action.compose_measure_number = 1;
    const auto message = fixture.dispatch(action);
    INFO(message);
    check_action(message == "Compose cursor moved to the requested measure.");
    const auto entries = fixture.entries();
    require_action(entries.empty());
    require_action(fixture.center.current_line() != nullptr);
    check_action(fixture.center.current_line()->severity == notification_severity_t::hint);
    check_action(fixture.center.current_line()->source == notification_source_t::input);
    check_action((fixture.state.cursor == local_position_t{measure_id_t{1}, 0}));
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose NONE area-clear request retains its quiet controller behavior", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_NONE);
    action.compose_area_clear_requested = true;
    check_action(fixture.dispatch(action).empty());
    check_action_false(fixture.state.retained_selection_range.has_value());
    check_action(fixture.entries().empty());
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose resource dispatcher retains exactly one start and terminal notification", "[compose-action-notifications]") {
    const auto directory = std::filesystem::temp_directory_path() /
        ("wormedit-compose-resource-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup_t {
        std::filesystem::path directory;
        ~cleanup_t() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } cleanup{directory};
    action_notification_fixture_t fixture;
    fixture.asset_base = directory / "project";
    std::filesystem::create_directories(fixture.asset_base);
    const auto source = directory / "source.wav";
    std::ofstream{source} << "actual resource copy bytes";
    fixture.history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = source});
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT);
    action.compose_resource_asset_id = 1;
    require_action(fixture.dispatch(action) == "Resource copy started in the background.");
    std::optional<std::string> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = fixture.resource_copy.poll(fixture.history, fixture.workspace);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    require_action(result.has_value());
    require_action_false(fixture.resource_copy.poll(fixture.history, fixture.workspace).has_value());
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::import_);
    check_action(entries.back().severity == notification_severity_t::info);
    check_action(entries.back().message == "Resource copy started in the background.");
    check_action(entries.front().count == 1);
    check_action(entries.back().count == 1);
    check_action(fixture.history.undo.size() == 1);
    check_action(std::filesystem::is_regular_file(fixture.asset_base / fixture.history.document.audio_assets.front().source_path));
}

TEST_CASE("Compose workspace boundary publishes branch validation without mutation", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t metadata{fixture.sink};
    std::optional<editor_interaction_state_t> interaction{fixture.state};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE);
    auto expected = notification_severity_t::warning;
    SECTION("missing selected control is validation") { action.compose_branch_id = 0; }
    SECTION("core rejection keeps its severity") {
        action.compose_branch_id = 999;
        expected = notification_severity_t::error;
    }
    const auto revision = fixture.history.document.revision;
    check_action(dispatch_compose_workspace_action(action, fixture.history, fixture.workspace,
        interaction, 800, fixture.application, {}, fixture.scheduler, fixture.resource_copy,
        fixture.export_runtime, false, drop, metadata, fixture.sink));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == expected);
    check_action(entries.front().source == notification_source_t::format);
    check_action(entries.front().count == 1);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose measure helpers publish their actual edit and validation outcomes", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    SECTION("insert mutation") {
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER)) == "Measure inserted.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::success);
        check_action(entries.front().source == notification_source_t::edit);
        check_action(fixture.history.undo.size() == 1);
        check_action(fixture.history.document.charts.front().measures.size() == 2);
        require_action(undo_document_command(fixture.history));
        check_action(fixture.history.document.charts.front().measures.size() == 1);
    }
    SECTION("invalid meter") {
        const auto revision = fixture.history.document.revision;
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER)) ==
            "A meter needs a non-zero numerator and denominator.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::warning);
        check_action(entries.front().source == notification_source_t::edit);
        check_action(fixture.history.document.revision == revision);
        check_action(fixture.history.undo.empty());
    }
}

TEST_CASE("Compose transform helpers retain success and rejection metadata", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    SECTION("shift mutation") {
        fixture.history.document.charts.front().measures.front().notes = {{.id = object_id_t{4},
            .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}}};
        fixture.state.selected_notes = {object_id_t{4}};
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER)) ==
            "Selection shifted by one snap step.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::success);
        check_action(entries.front().source == notification_source_t::edit);
        check_action(fixture.history.document.charts.front().measures.front().notes.front().position.row == 480);
        require_action(undo_document_command(fixture.history));
        check_action(fixture.history.document.charts.front().measures.front().notes.front().position.row == 240);
    }
    SECTION("no selection") {
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR)) ==
            "Select notes before transforming them.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::warning);
        check_action(fixture.history.undo.empty());
    }
}

TEST_CASE("Compose label search navigates with a hint and preserves document history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().measures.front().notes = {{.id = object_id_t{4},
        .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}, .label = "Chorus"}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL);
    std::snprintf(action.compose_note_label, sizeof(action.compose_note_label), "%s", "Chorus");
    check_action(fixture.dispatch(action) == "Selected 1 note(s) with that label.");
    const auto entries = fixture.entries();
    require_action(entries.empty());
    require_action(fixture.center.current_line() != nullptr);
    check_action(fixture.center.current_line()->severity == notification_severity_t::hint);
    check_action(fixture.center.current_line()->source == notification_source_t::input);
    check_action(fixture.state.cursor.row == 240);
    check_action(fixture.state.selected_notes == std::vector{object_id_t{4}});
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose recipe form preview and edit preserve typed helper outcomes", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto& recipes = fixture.history.document.charts.front().sv_recipes;
    recipes.push_back({.id = recipe_id_t{1}, .name = "Recipe",
        .range = {{measure_id_t{1}, 0}, {measure_id_t{1}, 480}}, .output = sv_recipe_output_t::sv,
        .curve = sv_recipe_curve_t::linear,
        .parameters = {.value_from = {1, 1}, .value_to = {2, 1}}});
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM);
    action.compose_recipe_id = 1;
    action.compose_recipe_form = recipe_form_from_recipe(recipes.front());
    auto expected = notification_severity_t::hint;
    auto source = notification_source_t::input;
    SECTION("valid preview") {
        check_action(fixture.dispatch(action) == "SV recipe preview updated.");
        check_action(fixture.history.undo.empty());
    }
    SECTION("invalid syntax") {
        std::snprintf(action.compose_recipe_form.value_to.numerator, sizeof(action.compose_recipe_form.value_to.numerator), "%s", "bad");
        check_action(fixture.dispatch(action) == "Values must be exact fractions and counts must be whole numbers.");
        expected = notification_severity_t::warning;
        source = notification_source_t::edit;
        check_action(fixture.history.undo.empty());
    }
    SECTION("committed form") {
        action.compose_action = WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM;
        std::snprintf(action.compose_recipe_form.value_to.numerator, sizeof(action.compose_recipe_form.value_to.numerator), "%s", "3");
        check_action(fixture.dispatch(action) == "SV recipe updated.");
        expected = notification_severity_t::success;
        source = notification_source_t::edit;
        check_action(fixture.history.undo.size() == 1);
        require_action(undo_document_command(fixture.history));
        check_action((fixture.history.document.charts.front().sv_recipes.front().parameters.value_to == signed_rational_t{2, 1}));
    }
    const auto entries = fixture.entries();
    if (expected == notification_severity_t::hint) {
        require_action(entries.empty());
        require_action(fixture.center.current_line() != nullptr);
        check_action(fixture.center.current_line()->severity == expected);
        check_action(fixture.center.current_line()->source == source);
    } else {
        require_action(entries.size() == 1);
        check_action(entries.front().severity == expected);
        check_action(entries.front().source == source);
    }
}

TEST_CASE("Compose bookmark rename separates no-op from validation without changing legacy flags", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .label = "Intro", .anchor = marker_anchor_t::beat,
        .position = local_position_t{measure_id_t{1}, 240}}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK);
    action.compose_bookmark_id = 4;
    std::snprintf(action.compose_marker_label, sizeof(action.compose_marker_label), "%s", "Intro");
    auto severity = notification_severity_t::info;
    SECTION("unchanged label") { check_action(fixture.dispatch(action) == "Bookmark label was unchanged."); }
    SECTION("missing bookmark") {
        action.compose_bookmark_id = 999;
        check_action(fixture.dispatch(action) == "That bookmark is no longer available.");
        severity = notification_severity_t::warning;
    }
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == severity);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().bookmarks.front().label == "Intro");
}

TEST_CASE("Compose bookmark rename mutation precedes its failed rebind notification", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .label = "Intro", .anchor = marker_anchor_t::beat,
        .position = local_position_t{measure_id_t{1}, 240}}};
    fixture.workspace.song = song_id_t{99};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK);
    action.compose_bookmark_id = 4;
    std::snprintf(action.compose_marker_label, sizeof(action.compose_marker_label), "%s", "Outro");
    check_action(fixture.dispatch(action) == "workspace cannot be rebound to a different song identity");
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::document);
    check_action(entries.back().message == "Bookmark renamed.");
    check_action(entries.back().severity == notification_severity_t::success);
    check_action(entries.back().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    check_action(entries.back().count == 1);
    check_action(fixture.history.undo.size() == 1);
    check_action(fixture.history.document.charts.front().bookmarks.front().label == "Outro");
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().bookmarks.front().label == "Intro");
}

TEST_CASE("Compose audio offset is a document edit with one success notification", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET);
    action.compose_audio_offset_numerator = 1;
    action.compose_audio_offset_denominator = 4;
    check_action(fixture.dispatch(action) == "Audio offset changed.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
}

TEST_CASE("Compose area quantize records the applied report rows", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto& notes = fixture.history.document.charts.front().measures.front().notes;
    notes.push_back({.id = object_id_t{10}, .position = {measure_id_t{1}, 250},
                     .lane = lane_id_t{1}, .kind = note_kind_t::tap});
    fixture.state.selected_notes = {object_id_t{10}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE);
    action.compose_quantize_divisions = 4;
    require_action(fixture.dispatch(action) == "Selected notes quantized.");
    require_action(fixture.application.quantize_applied_report.has_value());
    check_action(fixture.application.quantize_applied_report->rows.size() == 1);
    check_action(fixture.application.quantize_applied_report->total_rows == 1);
}

namespace {
auto draw_bga_input(bga_input_fixture_t& fixture, WormUiComposeInputKind kind,
    uint64_t row, bool erase = false) -> WormUiWorkspaceAction {
    auto value = fixture.input(kind, 5);
    value.compose_inputs[0].resource_id = erase ? 0 : 31;
    value.compose_inputs[0].paint_intent = erase ? 2 : 1;
    value.compose_inputs[0].y_pixels = compose_source_y(
        row * fixture.state.pixels_per_standard_measure / 960.0 - fixture.state.viewport_scroll_pixels,
        fixture.state.viewport_height_pixels,
        compose_field_downscroll(fixture.history.document.charts.front(), fixture.application.editor_preferences));
    return value;
}
}
TEST_CASE("Draw BGA production stroke commits interpolated targets once and undo restores all",
          "[bga][draw][compose-input]") {
    bga_input_fixture_t fixture;
    fixture.application.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    check_action(fixture.history.undo.empty());
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 720);
    INFO(fixture.dispatch_input(release));
    require_action(fixture.history.undo.size() == 1);
    check_action(fixture.history.document.charts.front().bga_events.size() == 4);
    check_action(!fixture.application.bga_draw_stroke);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().bga_events.size() == 1);
}
TEST_CASE("Draw BGA deleted source and Timing focus reject without undo",
          "[bga][draw][compose-input]") {
    bga_input_fixture_t fixture;
    fixture.application.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    fixture.history.document.resources.clear();
    const auto revision = fixture.history.document.revision;
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 480);
    (void)fixture.dispatch_input(release);
    fixture.check_unchanged(revision);
    check_action(!fixture.application.bga_draw_stroke);
    fixture.application.note_paint_mode_active = false;
    (void)fixture.dispatch_input(begin);
    fixture.check_unchanged(revision);
    check_action(!fixture.application.bga_draw_stroke);
}
TEST_CASE("Draw BGA interpolation preserves absolute snap across a five eighths measure",
          "[bga][draw][compose-input]") {
    bga_input_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition = {{5, 8}, meter_t{5, 8}};
    chart.measures.push_back({.id = measure_id_t{2}});
    fixture.application.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 480);
    (void)fixture.dispatch_input(begin);
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 720);
    (void)fixture.dispatch_input(release);
    require_action(fixture.history.undo.size() == 1);
    const auto& events = fixture.history.document.charts.front().bga_events;
    require_action(events.size() == 3);
    check_action(events[1].position == local_position_t{measure_id_t{1}, 480});
    check_action(events[2].position == local_position_t{measure_id_t{2}, 120});
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().bga_events.size() == 1);
}
TEST_CASE("Draw source choice projects session state without changing the document",
          "[draw][compose-input]") {
    bga_input_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto input = fixture.input(WORM_UI_COMPOSE_INPUT_DRAW_SOURCE, 0);
    input.compose_inputs[0].resource_role = 2;
    input.compose_inputs[0].resource_id = 31;
    (void)fixture.dispatch_input(input);
    check_action(fixture.application.draw_source_kind == 2);
    check_action(fixture.application.draw_source_id == 31);
    fixture.check_unchanged(revision);
}

namespace {
auto note_draw_input(bga_input_fixture_t& fixture, WormUiComposeInputKind kind, uint8_t intent)
    -> WormUiWorkspaceAction {
    auto value = fixture.input(kind, 0);
    value.compose_inputs[0].resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
    value.compose_inputs[0].resource_id = 0;
    value.compose_inputs[0].paint_intent = intent;
    value.compose_inputs[0].x_pixels = 400;
    return value;
}
void add_draw_test_note(bga_input_fixture_t& fixture) {
    fixture.application.note_paint_mode_active = true;
    fixture.history.document.charts.front().measures.front().notes = {{
        .id = object_id_t{91}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}}};
}
}
TEST_CASE("Draw intent reaches the production note dispatcher and preserves occupied taps",
          "[draw][compose-input]") {
    bga_input_fixture_t fixture;
    add_draw_test_note(fixture);
    auto begin = note_draw_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 1);
    INFO(fixture.dispatch_input(begin));
    require_action(fixture.state.pending_note_paint_gesture.has_value());
    check_action(fixture.state.pending_note_paint_gesture->intent == note_paint_intent_t::draw);
    auto end = note_draw_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 1);
    INFO(fixture.dispatch_input(end));
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().measures.front().notes.front().id == object_id_t{91});
    check_action(!fixture.state.pending_note_paint_gesture);
}
TEST_CASE("Explicit erase reaches the production note dispatcher and undo restores the note",
          "[draw][compose-input]") {
    bga_input_fixture_t fixture;
    add_draw_test_note(fixture);
    auto begin = note_draw_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 2);
    (void)fixture.dispatch_input(begin);
    auto end = note_draw_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 2);
    INFO(fixture.dispatch_input(end));
    require_action(fixture.history.undo.size() == 1);
    check_action(fixture.history.document.charts.front().measures.front().notes.empty());
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().measures.front().notes.front().id == object_id_t{91});
}
