#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_input.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/recipe_form.h"
#include <cstdio>
#include <cstring>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/bms.h>
#include <wormedit/audio/slice_render.h>
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
    require_action(fixture.dispatch(action) == "Selected objects quantized.");
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

namespace {
void prepare_t463_stem(bga_input_fixture_t& fixture) {
    auto& chart = fixture.history.document.charts.front();
    chart.source_family = format_family_t::bmson;
    fixture.history.document.audio_assets = {{.id = asset_id_t{81}}};
    fixture.history.document.resources.push_back({.id = resource_id_t{82}, .name = "voice.wav",
        .asset = asset_id_t{81}, .roles = {resource_role_t::sample, resource_role_t::bgm}});
    chart.bgm_source_lanes = {{.id = object_id_t{84}, .name = "Voice", .stem_channel = object_id_t{83}},
        {.id = object_id_t{85}, .name = "Packed"}};
    chart.sound_channels = {{.id = object_id_t{83}, .name = "voice.wav",
        .source_clip = {clip_id_t{88}, asset_id_t{81}, 0, std::nullopt},
        .events = {{.id = object_id_t{86}, .position = {measure_id_t{1}, 0}, .playable = false}}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{87}, .position = {measure_id_t{1}, 0},
        .source_lane = object_id_t{84}, .sound = resource_id_t{82}, .sound_channel_event = object_id_t{86}}};
    fixture.application.note_paint_mode_active = true;
    fixture.cache.scene_document_identity = &fixture.history.document;
    fixture.cache.scene_chart_identity = &chart;
    fixture.cache.stem_index.built = true;
    fixture.cache.stem_index.document = fixture.history.document.id;
    fixture.cache.stem_index.chart = chart.id;
    fixture.cache.stem_index.revision = fixture.history.document.revision;
    fixture.cache.stem_index.bindings = {{.asset = asset_id_t{81}, .source_sample_rate = 44'100,
        .source_frame_count = 44'100, .output_sample_rate = 44'100, .output_frame_count = 44'100}};
}
void check_t463_point(uint64_t row, bool shift, bool expected_restart, bool empty = false) {
    bga_input_fixture_t fixture;
    prepare_t463_stem(fixture);
    if (empty) {
        fixture.history.document.charts.front().sound_channels.front().events.clear();
        fixture.history.document.charts.front().measures.front().bgm_events.clear();
    }
    auto input = draw_bga_input(fixture, shift ? WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART :
        WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, row);
    input.compose_inputs[0].lane_id = 84;
    const auto before = fixture.history.document.charts.front().sound_channels.front().events.size();
    INFO(fixture.dispatch_input(input));
    require_action(fixture.history.undo.size() == 1);
    const auto& events = fixture.history.document.charts.front().sound_channels.front().events;
    require_action(events.size() == before + 1);
    check_action(events.back().continuation == !expected_restart);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().sound_channels.front().events.size() == before);
}
}
TEST_CASE("BMSON Draw uses retained native geometry for continue Shift EOF and empty stem undo",
          "[bmson][stem][draw][compose-input]") {
    check_t463_point(240, false, false);
    check_t463_point(240, true, true);
    check_t463_point(720, false, true);
    check_t463_point(240, false, true, true);
}
TEST_CASE("BMSON Draw rejects stale geometry and stem sources on packed columns atomically",
          "[bmson][stem][draw][compose-input]") {
    bga_input_fixture_t fixture;
    prepare_t463_stem(fixture);
    auto input = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 240);
    input.compose_inputs[0].lane_id = 84;
    fixture.cache.stem_index.revision = 0;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch_input(input);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    input.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW;
    input.compose_inputs[0].lane_id = 85;
    input.compose_inputs[0].resource_id = 82;
    (void)fixture.dispatch_input(input);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().sound_channels.size() == 1);
}
TEST_CASE("BMSON packed Draw creates its source channel and semantic event with one undo",
          "[bmson][stem][draw][compose-input]") {
    bga_input_fixture_t fixture;
    prepare_t463_stem(fixture);
    fixture.history.document.audio_assets.push_back({.id = asset_id_t{90}});
    fixture.history.document.resources.push_back({.id = resource_id_t{91}, .name = "kick.wav",
        .asset = asset_id_t{90}, .roles = {resource_role_t::sample}});
    auto input = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW, 240);
    input.compose_inputs[0].lane_id = 85;
    input.compose_inputs[0].resource_id = 91;
    INFO(fixture.dispatch_input(input));
    require_action(fixture.history.undo.size() == 1);
    check_action(fixture.history.document.charts.front().sound_channels.size() == 2);
    check_action(fixture.history.document.charts.front().measures.front().bgm_events.size() == 2);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().sound_channels.size() == 1);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

namespace {
auto wait_for_slice_export(action_notification_fixture_t& fixture) -> std::optional<std::string> {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (auto result = fixture.export_runtime.poll(fixture.history)) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return std::nullopt;
}
}

TEST_CASE("BMSON output export rejects missing stem before writing music or destination",
          "[unit][sys][conversion][publication]") {
    action_notification_fixture_t fixture;
    const auto directory = std::filesystem::temp_directory_path() /
        ("wormedit-app-slice-export-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup_t {
        std::filesystem::path path;
        ~cleanup_t() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{directory};
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "music.wav", std::ios::binary) << "retained source bytes";
    fixture.asset_base = directory;
    auto& document = fixture.history.document;
    document.audio_assets = {{.id = asset_id_t{20}, .source_path = "missing-stem.wav"},
                             {.id = asset_id_t{21}, .source_path = "music.wav"}};
    document.resources = {{.id = resource_id_t{22}, .asset = asset_id_t{21},
                           .roles = {resource_role_t::music}}};
    auto& chart = document.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.sound_channels = {{.id = object_id_t{23}, .source_clip = {.source = asset_id_t{20}},
        .events = {{.id = object_id_t{24}, .position = {measure_id_t{1}, 0}}}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{25},
        .position = {measure_id_t{1}, 0}, .sound_channel_event = object_id_t{24}}};
    document.output_groups = {{.family = format_family_t::osu_mania,
        .charts = {chart_id_t{1}}, .relative_output_path = "exports"}};
    const auto revision = document.revision;
    const auto status = fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP));
    require_action(status == "Output group conversion started in the background.");
    const auto completed = wait_for_slice_export(fixture);
    require_action(completed.has_value());
    check_action(completed->find("Cannot decode") != std::string::npos);
    check_action_false(std::filesystem::exists(directory / "exports"));
    check_action(std::filesystem::file_size(directory / "music.wav") == 21);
    check_action(document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("BMSON editor export publishes sliced audio and reports conversion losses",
          "[unit][sys][conversion][publication]") {
    const auto target = GENERATE(format_family_t::osu_mania, format_family_t::bms);
    const auto named_output = GENERATE(false, true);
    action_notification_fixture_t fixture;
    const auto directory = std::filesystem::temp_directory_path() /
        ("wormedit-app-slice-success-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup_t {
        std::filesystem::path path;
        ~cleanup_t() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{directory};
    std::filesystem::create_directories(directory);
    const std::string text = R"({"version":"1.0.0","info":{"title":"Editor slices","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"stem.wav","notes":[{"x":1,"y":0,"c":false},{"x":1,"y":240,"c":true},{"x":0,"y":480,"l":120,"c":false}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    require_action(parsed.song.has_value());
    require_action_false(parsed.has_errors());
    fixture.history = make_document_history(*parsed.song);
    auto& exported_chart = fixture.history.document.charts.front();
    exported_chart.semantic_layers = {{.id = layer_id_t{900}, .name = "Export layer"}};
    for (auto& measure : exported_chart.measures)
        for (auto& note : measure.notes) note.semantic_layers = {layer_id_t{900}};
    fixture.history.document.output_groups = {{.family = target,
        .charts = {fixture.history.document.charts.front().id},
        .relative_output_path = named_output
            ? (target == format_family_t::osu_mania ? "exports/chart.osu" : "exports/chart.bme")
            : "exports"}};
    const auto workspace = make_workspace_controller(fixture.history);
    require_action(static_cast<bool>(workspace));
    fixture.workspace = *workspace.workspace;
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = fixture.history.document.charts.front().id;
    fixture.state.chart = fixture.history.document.charts.front().id;
    fixture.state.cursor = {fixture.history.document.charts.front().measures.front().id, 0};
    fixture.state.active_lane = fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().id;
    fixture.asset_base = directory;
    const std::vector<float> pcm(88'200, 0.125F);
    const auto wav = audio::encode_wav_pcm16(pcm, 44'100);
    std::ofstream(directory / "stem.wav", std::ios::binary).write(wav.data(), wav.size());
    const auto revision = fixture.history.document.revision;
    require_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP)) ==
        "Output group conversion started in the background.");
    const auto completed = wait_for_slice_export(fixture);
    require_action(completed.has_value());
    check_action(completed->starts_with("Output group published"));
    check_action(completed->find("conversion loss") != std::string::npos);
    check_action(completed->size() <= notification_message_max_bytes);
    const auto notifications = fixture.entries();
    check_action(std::ranges::any_of(notifications, [&](const auto& entry) {
        return entry.severity == notification_severity_t::warning && entry.message == *completed &&
               entry.detail.find("layer") != std::string::npos &&
               entry.detail.find("hold tail") != std::string::npos &&
               entry.detail.find("Chart ") != std::string::npos;
    }));
    std::size_t charts{}, sounds{};
    for (const auto& file : std::filesystem::directory_iterator(directory / "exports")) {
        charts += file.path().extension() == (target == format_family_t::osu_mania ? ".osu" : ".bme");
        sounds += file.path().extension() == ".wav";
    }
    check_action(charts == 1);
    check_action(sounds == 3);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(std::filesystem::file_size(directory / "stem.wav") == wav.size());
}

TEST_CASE("BMSON editor BMS export materializes silent gameplay in private staging",
          "[unit][sys][conversion][publication]") {
    const auto incompatible_name = GENERATE(false, true);
    action_notification_fixture_t fixture;
    const auto directory = std::filesystem::temp_directory_path() /
        ("wormedit-app-silent-bms-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup_t {
        std::filesystem::path path;
        ~cleanup_t() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{directory};
    std::filesystem::create_directories(directory);
    const std::string text = R"({"version":"1.0.0","info":{"title":"Silent gameplay","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    require_action(parsed.song.has_value());
    require_action_false(parsed.has_errors());
    fixture.history = make_document_history(*parsed.song);
    auto& chart = fixture.history.document.charts.front();
    const auto& lanes = chart.lane_layouts.front().layout.lanes;
    const auto lane = std::ranges::find(lanes, lane_role_t::playable, &lane_definition_t::role);
    require_action(lane != lanes.end());
    chart.measures.front().notes = {{.id = object_id_t{1000},
        .position = {chart.measures.front().id, 0}, .lane = lane->id}};
    fixture.history.document.output_groups = {{.family = format_family_t::bms,
        .charts = {chart.id},
        .relative_output_path = incompatible_name ? "exports/chart.pms" : "exports"}};
    const auto workspace = make_workspace_controller(fixture.history);
    require_action(static_cast<bool>(workspace));
    fixture.workspace = *workspace.workspace;
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = chart.id;
    fixture.state.chart = chart.id;
    fixture.state.cursor = {chart.measures.front().id, 0};
    fixture.state.active_lane = lane->id;
    fixture.asset_base = directory;
    const auto revision = fixture.history.document.revision;
    require_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP)) ==
        "Output group conversion started in the background.");
    const auto completed = wait_for_slice_export(fixture);
    require_action(completed.has_value());
    if (incompatible_name) {
        check_action(completed->find("filename is incompatible") != std::string::npos);
        check_action_false(std::filesystem::exists(directory / "exports"));
        check_action(fixture.history.document.revision == revision);
        check_action(fixture.history.undo.empty());
        return;
    }
    check_action(completed->starts_with("Output group published"));
    check_action(std::filesystem::exists(directory / "exports" / "conversion-silence.wav"));
    std::size_t charts{}, sounds{};
    for (const auto& file : std::filesystem::directory_iterator(directory / "exports")) {
        charts += file.path().extension() == ".bme";
        sounds += file.path().extension() == ".wav";
    }
    check_action(charts == 1);
    check_action(sounds == 1);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(!chart.measures.front().notes.front().sound);
}

#endif

namespace {
void seed_quantize_rename(action_notification_fixture_t& fixture) {
    fixture.history.document.quantization_presets = {
        {.name = "Default", .divisions = 16},
        {.name = "Trills",
         .divisions = 24,
         .space = quantization_space_t::authored_measure,
         .tie = quantization_tie_t::later,
         .holds = quantization_hold_t::start_only}};
    fixture.application.quantize_editor =
        application_command_state_t::quantize_editor_t{.song = fixture.history.document.id,
                                                       .chart = fixture.state.chart,
                                                       .request = {.divisions = 16},
                                                       .preset_index = 0};
}
} // namespace

TEST_CASE("Compose quantize preset rename preserves settings and commits one undo",
          "[compose-action-notifications][quantize]") {
    action_notification_fixture_t fixture;
    seed_quantize_rename(fixture);
    const auto presets = fixture.history.document.quantization_presets;
    const auto revision = fixture.history.document.revision;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET);
    std::snprintf(action.compose_quantize_preset_name, sizeof(action.compose_quantize_preset_name),
                  "Renamed");
    check_action(fixture.dispatch(action) == "Quantization preset renamed.");
    auto expected = presets;
    expected.front().name = "Renamed";
    check_action(fixture.history.document.quantization_presets == expected);
    check_action(fixture.history.document.revision == revision + 1);
    check_action(fixture.history.undo.size() == 1);
    check_action(fixture.application.quantize_editor->preset_index == 0);
    check_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.quantization_presets == presets);
}

TEST_CASE(
    "Compose quantize preset rename rejects invalid names indices and stale revisions atomically",
    "[compose-action-notifications][quantize]") {
    action_notification_fixture_t fixture;
    seed_quantize_rename(fixture);
    const auto presets = fixture.history.document.quantization_presets;
    const auto revision = fixture.history.document.revision;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET);
    std::snprintf(action.compose_quantize_preset_name, sizeof(action.compose_quantize_preset_name),
                  "Renamed");
    SECTION("empty name") {
        action.compose_quantize_preset_name[0] = '\0';
    }
    SECTION("duplicate name") {
        std::snprintf(action.compose_quantize_preset_name,
                      sizeof(action.compose_quantize_preset_name), "Trills");
    }
    SECTION("unterminated overlength name") {
        std::memset(action.compose_quantize_preset_name, 'x',
                    sizeof(action.compose_quantize_preset_name));
    }
    SECTION("stale index") {
        action.compose_quantize_preset_index = presets.size();
    }
    SECTION("stale revision") {
        ++action.compose_input_document_revision;
    }
    check_action(!fixture.dispatch(action).empty());
    check_action(fixture.history.document.quantization_presets == presets);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose quantize preset rename to the existing name is a history no-op",
          "[compose-action-notifications][quantize]") {
    action_notification_fixture_t fixture;
    seed_quantize_rename(fixture);
    const auto revision = fixture.history.document.revision;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET);
    std::snprintf(action.compose_quantize_preset_name, sizeof(action.compose_quantize_preset_name),
                  "Default");
    check_action(fixture.dispatch(action) == "Quantization preset name was unchanged.");
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

namespace {
auto draw_bgm_input(bga_input_fixture_t& fixture, WormUiComposeInputKind kind, uint64_t row,
                    bool erase = false) -> WormUiWorkspaceAction {
    auto value = draw_bga_input(fixture, kind, row, erase);
    value.compose_inputs[0].resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    value.compose_inputs[0].lane_id = 50;
    value.compose_inputs[0].resource_id = erase ? 0 : 33;
    return value;
}
}
TEST_CASE("A single BGM click stroke commits once without a draw warning",
          "[bgm][draw][compose-input]") {
    bga_input_fixture_t fixture;
    fixture.history.document.audio_assets = {{.id = asset_id_t{71}}};
    fixture.history.document.resources.push_back({.id = resource_id_t{33}, .asset = asset_id_t{71},
        .roles = {resource_role_t::sample}});
    fixture.history.document.charts.front().bgm_source_lanes = {{.id = object_id_t{50}, .name = "BGM"}};
    fixture.application.note_paint_mode_active = true;
    // A click presses and releases on the same row, so the release lands on the begin's row.
    auto begin = draw_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    check_action(fixture.dispatch_input(begin) == "BGM Draw stroke preview updated.");
    auto release = draw_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 240);
    check_action(fixture.dispatch_input(release) == "BGM Draw stroke committed.");
    require_action(fixture.history.undo.size() == 1);
    std::size_t total{};
    for (const auto& measure : fixture.history.document.charts.front().measures)
        total += measure.bgm_events.size();
    check_action(total == 1);
    check_action(!fixture.application.bga_draw_stroke);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Draw BGM production stroke commits once and erase removes the lane once",
          "[bgm][draw][compose-input]") {
    bga_input_fixture_t fixture;
    fixture.history.document.audio_assets = {{.id = asset_id_t{71}}};
    fixture.history.document.resources.push_back({.id = resource_id_t{33}, .asset = asset_id_t{71},
        .roles = {resource_role_t::sample}});
    fixture.history.document.charts.front().bgm_source_lanes = {{.id = object_id_t{50}, .name = "BGM"}};
    fixture.application.note_paint_mode_active = true;
    auto begin = draw_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    check_action(fixture.history.undo.empty());
    auto release = draw_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 720);
    INFO(fixture.dispatch_input(release));
    require_action(fixture.history.undo.size() == 1);
    const auto count = [&] {
        std::size_t total{};
        for (const auto& measure : fixture.history.document.charts.front().measures)
            total += measure.bgm_events.size();
        return total;
    };
    check_action(count() == 3);
    check_action(!fixture.application.bga_draw_stroke);
    auto erase_begin = draw_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240, true);
    (void)fixture.dispatch_input(erase_begin);
    auto erase_release = draw_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 480, true);
    INFO(fixture.dispatch_input(erase_release));
    check_action(fixture.history.undo.size() == 2);
    check_action(count() == 1);
    require_action(undo_document_command(fixture.history));
    require_action(undo_document_command(fixture.history));
    check_action(count() == 0);
}

#include "sys/application_command_dispatch.h"

TEST_CASE("Quantize grid and editing snap are one shared grid",
          "[unit][sys][compose-action][quantize-snap]") {
    action_notification_fixture_t fixture;
    fixture.state.supported_snap_denominators = {4, 8, 16};
    auto grid = [&](std::uint64_t divisions) {
        auto value = fixture.action(WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS);
        value.compose_quantize_divisions = divisions;
        return value;
    };
    const auto revision = fixture.history.document.revision;

    (void)fixture.dispatch(grid(16));
    check_action(fixture.state.active_snap_denominator == 16);
    check_action(fixture.application.requested_snap_denominators.at(1) == 16);
    check_action(fixture.application.quantize_editor->request.divisions == 16);

    // A grid the chart cannot snap to sets only the quantize target.
    fixture.entries();
    (void)fixture.dispatch(grid(12));
    check_action(fixture.state.active_snap_denominator == 16);
    check_action(fixture.application.quantize_editor->request.divisions == 12);

    // Next snap moves the quantize target with it and keeps the other settings.
    fixture.application.quantize_editor->request.tie = quantization_tie_t::later;
    std::optional<editor_interaction_state_t> compose = fixture.state;
    std::optional<timing_lane_pseudonote_preview_t> preview;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::navigation_next_snap}, fixture.history,
        fixture.workspace, compose, preview, fixture.application, fixture.sink);
    require_action(result.accepted);
    check_action(compose->active_snap_denominator == 4);
    check_action(fixture.application.quantize_editor->request.divisions == 4);
    check_action(fixture.application.quantize_editor->request.tie == quantization_tie_t::later);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("BMSON lane_mode dispatcher shares channel and sound targets with atomic refusal",
          "[unit][sys][bmson][lane_mode]") {
    const bool sounds_tab = GENERATE(false, true);
    action_notification_fixture_t fixture;
    const std::string text = R"({"version":"1.0.0","info":{"title":"Modes","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"stem.wav","notes":[{"x":0,"y":0,"c":false},{"x":0,"y":240,"c":true}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    require_action(parsed.song.has_value());
    fixture.history = make_document_history(*parsed.song);
    fixture.workspace = *make_workspace_controller(fixture.history).workspace;
    fixture.workspace.active_tab = sounds_tab ? workspace_tab_t::audio_slices : workspace_tab_t::compose;
    fixture.state.chart = fixture.history.document.charts.front().id;
    fixture.state.cursor = {fixture.history.document.charts.front().measures.front().id, 0};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_PACK_BMSON_CHANNEL);
    action.compose_bmson_channel_id = fixture.history.document.charts.front().sound_channels.front().id.value;
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(action) == "continuation points need a stem lane");
    check_action(fixture.history.document.revision == revision && fixture.history.undo.empty());
    action.compose_action = WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL;
    require_action(fixture.dispatch(action) == "BMSON channel lane mode changed.");
    check_action(fixture.history.undo.size() == 1);
    action = fixture.action(WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM);
    action.compose_resource_id = fixture.history.document.resources.front().id.value;
    require_action(fixture.dispatch(action) == "BMSON channel lane mode changed.");
    check_action(fixture.history.undo.size() == 2);
    check_action(fixture.history.document.resources.front().sound_kind == sound_kind_t::stem);
    check_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.resources.front().sound_kind == sound_kind_t::keysound);
    check_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().sound_channels.front().events.back().continuation);
}

TEST_CASE("Compose workspace boundary creates and removes a BMS branch block and undoes each",
          "[compose-action-notifications][bms-branch]") {
    constexpr std::string_view text = "#PLAYER 1\n#TITLE Block action test\n#BPM 120\n"
                                      "#WAV01 one.wav\n#WAV02 two.wav\n#00011:0100\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{text.data(), text.size()}), std::move(options));
    require_action(parsed.song.has_value() && !parsed.has_errors());
    action_notification_fixture_t fixture;
    fixture.history = make_document_history(*parsed.song);
    fixture.workspace = *make_workspace_controller(fixture.history).workspace;
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = fixture.history.document.charts.front().id;
    fixture.state.chart = fixture.history.document.charts.front().id;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t metadata{fixture.sink};
    std::optional<editor_interaction_state_t> interaction{fixture.state};
    const auto dispatch = [&](WormUiWorkspaceAction action) {
        action.compose_input_chart_id = fixture.history.document.charts.front().id.value;
        action.compose_input_document_revision = fixture.history.document.revision;
        return dispatch_compose_workspace_action(action, fixture.history, fixture.workspace,
            interaction, 800, fixture.application, {}, fixture.scheduler, fixture.resource_copy,
            fixture.export_runtime, false, drop, metadata, fixture.sink);
    };
    const auto find_node = [&](bms_control_kind_t kind) -> const bms_control_node_t* {
        const auto* found = static_cast<const bms_control_node_t*>(nullptr);
        const auto visit = [&](const auto& self, const bms_control_node_t& node) -> void {
            if (!found && node.kind == kind && node.original_text != "#00011:0100") found = &node;
            for (const auto& child : node.children) self(self, child);
        };
        visit(visit, *fixture.history.document.charts.front().bms_control_tree);
        return found;
    };
    const auto* statement = [&] {
        const auto* raw = static_cast<const bms_control_node_t*>(nullptr);
        for (const auto& node : fixture.history.document.charts.front().bms_control_tree->children)
            if (node.kind == bms_control_kind_t::raw_statement && node.original_text == "#WAV02 two.wav")
                raw = &node;
        return raw;
    }();
    require_action(statement != nullptr);
    WormUiWorkspaceAction create{};
    create.compose_action = WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK;
    create.compose_branch_statement_id = statement->id.value;
    check_action(dispatch(create));
    check_action(fixture.history.undo.size() == 1);
    const auto* block = find_node(bms_control_kind_t::random);
    require_action(block != nullptr);
    WormUiWorkspaceAction remove{};
    remove.compose_action = WORM_UI_COMPOSE_ACTION_REMOVE_BMS_BRANCH_BLOCK;
    remove.compose_branch_id = block->id.value;
    // The created block holds the wrapped statement, so removal rejects without changing history.
    const auto revision = fixture.history.document.revision;
    check_action(dispatch(remove));
    check_action(fixture.history.document.revision == revision && fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(find_node(bms_control_kind_t::random) == nullptr);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Snap badge changes supported editing grid without editing the document or quantize policies", "[unit][sys][compose-action][snap-grid]") {
    action_notification_fixture_t fixture;
    fixture.state.supported_snap_denominators = {4, 8, 16};
    auto settings = fixture.action(WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS);
    settings.compose_quantize_divisions = 4;
    (void)fixture.dispatch(settings);
    fixture.application.quantize_editor->request.tie = quantization_tie_t::later;
    auto snap = fixture.action(WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP);
    snap.compose_snap_denominator = 8;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch(snap);
    check_action(fixture.state.active_snap_denominator == 8);
    check_action(fixture.application.requested_snap_denominators.at(1) == 8);
    check_action(fixture.application.quantize_editor->request.divisions == 8);
    check_action(fixture.application.quantize_editor->request.tie == quantization_tie_t::later);
    snap.compose_snap_denominator = 7;
    (void)fixture.dispatch(snap);
    check_action(fixture.state.active_snap_denominator == 8);
    snap.compose_snap_denominator = 16;
    ++snap.compose_input_document_revision;
    (void)fixture.dispatch(snap);
    check_action(fixture.state.active_snap_denominator == 8);
    check_action(fixture.history.document.revision == revision && fixture.history.undo.empty());
}

TEST_CASE("Snap badge refuses active gestures and paste previews", "[unit][sys][compose-action][snap-grid]") {
    for (const bool paste : {false, true}) {
        action_notification_fixture_t fixture;
        fixture.state.supported_snap_denominators = {4, 8};
        if (paste) fixture.application.paste_session = application_command_state_t::paste_session_t{};
        else fixture.state.pending_hold.emplace();
        auto snap = fixture.action(WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP);
        snap.compose_snap_denominator = 8;
        (void)fixture.dispatch(snap);
        check_action(fixture.state.active_snap_denominator == 4);
        check_action(fixture.application.requested_snap_denominators.empty());
        check_action(fixture.history.undo.empty());
    }
}

TEST_CASE("Playing Compose navigation uses beats while stopped editing retains its snap",
          "[compose-action-notifications][playhead-skip]") {
    bga_input_fixture_t f;
    f.state.active_snap_denominator = 16;
    f.state.supported_snap_denominators = {4, 16};
    f.application.preview_playhead_absolute_row = 240;
    f.application.transport_preview_playing = [] { return true; };
    std::uint64_t sought{};
    f.application.transport_seek = [&](std::uint64_t frame) { sought = frame; return std::string{}; };
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0);
    action.compose_inputs[0].navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD;
    (void)f.dispatch_input(action);
    check_action(f.application.preview_playhead_absolute_row == 480);
    check_action(sought > 0);
    check_action((f.state.cursor == local_position_t{measure_id_t{1}, 0}));
    check_action(f.state.active_snap_denominator == 16);
    f.application.transport_preview_playing = [] { return false; };
    f.navigation.cancel();
    (void)f.dispatch_input(action);
    check_action((f.state.cursor == local_position_t{measure_id_t{1}, 60}));
    f.check_unchanged(1);
}

TEST_CASE("Tab measure navigation moves cursor immediately and scrolls over several frames",
          "[compose-action-notifications][navigation-travel]") {
    bga_input_fixture_t f;
    auto& chart = f.history.document.charts.front();
    chart.measures.push_back({.id = measure_id_t{2}});
    f.state.pixels_per_standard_measure = 600;
    f.state.viewport_scroll_pixels = -100;
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP, 0);
    action.compose_inputs[0].navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    (void)f.dispatch_input(action);
    check_action((f.state.cursor == local_position_t{measure_id_t{2}, 0}));
    check_action(f.cache.navigation_travel.target.has_value());
    const auto target = *f.cache.navigation_travel.target;
    check_action(f.state.viewport_scroll_pixels == -100);
    action.compose_input_count = 0;
    (void)dispatch_compose_input_action(action, f.history, f.workspace, f.state, f.viewport,
        f.navigation, f.application, f.cache, 0.02, &f.sink);
    check_action(f.state.viewport_scroll_pixels > -100 && f.state.viewport_scroll_pixels < target);
    (void)dispatch_compose_input_action(action, f.history, f.workspace, f.state, f.viewport,
        f.navigation, f.application, f.cache, 0.1, &f.sink);
    check_action(f.state.viewport_scroll_pixels == target);
    check_action(!f.cache.navigation_travel.target);
    f.check_unchanged(1);
}

TEST_CASE("Measure travel cancels on external scroll zoom revision chart and playback changes",
          "[compose-action-notifications][navigation-travel]") {
    const auto interrupt = GENERATE(0, 1, 2, 3, 4, 5);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1}; state.pixels_per_standard_measure = 128;
    state.viewport_scroll_pixels = 128;
    compose_navigation_travel_t travel;
    travel.begin(state, 1, 0);
    if (interrupt == 0) state.viewport_scroll_pixels = 7;
    if (interrupt == 1) state.pixels_per_standard_measure = 256;
    if (interrupt == 2) state.chart = chart_id_t{2};
    if (interrupt == 5) place_cursor_at(state, {measure_id_t{1}, 240});
    const auto previous = state.viewport_scroll_pixels;
    travel.advance(state, interrupt == 3 ? 2 : 1, interrupt == 4, 0.02);
    check_action(!travel.target);
    check_action(state.viewport_scroll_pixels == previous);
}

TEST_CASE("Playback wheel skips one beat in chart direction independently of snap",
          "[compose-action-notifications][playhead-skip]") {
    const auto forward = GENERATE(false, true);
    const auto downscroll = GENERATE(false, true);
    const auto inverted = GENERATE(false, true);
    bga_input_fixture_t f;
    f.state.active_snap_denominator = 16;
    f.application.editor_preferences.invert_scroll_wheel = inverted;
    f.application.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    f.application.preview_playhead_absolute_row = 480;
    f.application.transport_preview_playing = [] { return true; };
    f.application.transport_seek = [](std::uint64_t) { return std::string{}; };
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0);
    action.compose_input_count = 0;
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {
        .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = ((forward != downscroll) != inverted) ? 1.0 : -1.0,
        .cursor_x_pixels = 200, .cursor_y_pixels = 200};
    (void)f.dispatch_input(action);
    check_action(f.application.preview_playhead_absolute_row == (forward ? 720 : 240));
    check_action(f.state.active_snap_denominator == 16);
    f.check_unchanged(1);
}

TEST_CASE("Interrupted Tab travel settles at the chosen cursor without changing history",
          "[compose-action-notifications][navigation-travel]") {
    const auto interrupt = GENERATE(WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL,
                                    WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
    bga_input_fixture_t f;
    f.history.document.charts.front().measures.push_back({.id = measure_id_t{2}});
    f.state.pixels_per_standard_measure = 600;
    f.state.viewport_scroll_pixels = -100;
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP, 0);
    action.compose_inputs[0].navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    (void)f.dispatch_input(action);
    require_action(f.cache.navigation_travel.target.has_value());
    const auto destination = *f.cache.navigation_travel.target;
    action.compose_inputs[0].kind = interrupt;
    (void)f.dispatch_input(action);
    check_action(!f.cache.navigation_travel.target);
    check_action(f.state.viewport_scroll_pixels == destination);
    check_action((f.state.cursor == local_position_t{measure_id_t{2}, 0}));
    f.check_unchanged(1);
}

TEST_CASE("An edit during Tab travel finishes at the chosen cursor after advancing history",
          "[compose-action-notifications][navigation-travel]") {
    bga_input_fixture_t f;
    f.history.document.charts.front().measures.push_back({.id = measure_id_t{2}});
    f.state.pixels_per_standard_measure = 600;
    f.state.viewport_scroll_pixels = -100;
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP, 0);
    action.compose_inputs[0].navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    (void)f.dispatch_input(action);
    require_action(f.cache.navigation_travel.target.has_value());
    const auto destination = *f.cache.navigation_travel.target;
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_DELETE_SELECTION;
    (void)f.dispatch_input(action);
    check_action(f.history.document.revision == 2);
    check_action(f.history.undo.size() == 1);
    check_action(f.history.document.charts.front().bga_events.empty());
    check_action(!f.cache.navigation_travel.target);
    check_action(f.state.viewport_scroll_pixels == destination);
    check_action((f.state.cursor == local_position_t{measure_id_t{2}, 0}));
}

TEST_CASE("Page navigation distance and minimum match while stopped and playing",
          "[compose-action-notifications][navigation-travel][playhead-skip]") {
    const auto playing = GENERATE(false, true);
    const auto doubled = GENERATE(false, true);
    const auto short_measures = GENERATE(false, true);
    bga_input_fixture_t f;
    auto& chart = f.history.document.charts.front();
    chart.measures.push_back({.id = measure_id_t{2}});
    chart.measures.push_back({.id = measure_id_t{3}});
    chart.measures.push_back({.id = measure_id_t{4}});
    if (short_measures) {
        chart.measures[1].definition = {{1, 960}, std::nullopt};
        chart.measures[2].definition = {{1, 960}, std::nullopt};
    }
    place_cursor_at(f.state, {measure_id_t{1}, 959});
    f.application.transport_preview_playing = [=] { return playing; };
    f.application.preview_playhead_absolute_row = 959;
    f.application.transport_seek = [](std::uint64_t) { return std::string{}; };
    auto action = f.input(doubled ? WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP
                                  : WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0);
    action.compose_inputs[0].navigation_action = doubled
        ? WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_FORWARD : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    (void)f.dispatch_input(action);
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    const auto expected = short_measures ? (doubled ? 1919 : 1439) : (doubled ? 2879 : 1919);
    if (playing) check_action(f.application.preview_playhead_absolute_row == expected);
    else check_action(absolute_row(index, f.state.cursor) == expected);
    f.check_unchanged(1);
}

TEST_CASE("Measure key release freezes the viewport and discards all remaining travel",
          "[compose-action-notifications][navigation-travel]") {
    const auto doubled = GENERATE(false, true);
    bga_input_fixture_t f;
    auto& chart = f.history.document.charts.front();
    chart.measures.push_back({.id = measure_id_t{2}});
    chart.measures.push_back({.id = measure_id_t{3}});
    f.state.pixels_per_standard_measure = 600;
    f.state.viewport_scroll_pixels = -100;
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP, 0);
    action.compose_inputs[0].navigation_action = doubled
        ? WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_FORWARD : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    (void)f.dispatch_input(action);
    require_action(f.cache.navigation_travel.target.has_value());
    auto idle = action;
    idle.compose_input_count = 0;
    (void)dispatch_compose_input_action(idle, f.history, f.workspace, f.state, f.viewport,
        f.navigation, f.application, f.cache, 0.02, &f.sink);
    const auto stopped_scroll = f.state.viewport_scroll_pixels;
    const auto stopped_cursor = f.state.cursor;
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE;
    (void)f.dispatch_input(action);
    check_action(!f.cache.navigation_travel.target);
    (void)dispatch_compose_input_action(idle, f.history, f.workspace, f.state, f.viewport,
        f.navigation, f.application, f.cache, 0.1, &f.sink);
    check_action(f.state.viewport_scroll_pixels == stopped_scroll);
    check_action(f.state.cursor == stopped_cursor);
    f.check_unchanged(1);
}

TEST_CASE("Large measure travel reaches only the latest destination within 75 milliseconds",
          "[compose-action-notifications][navigation-travel]") {
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.pixels_per_standard_measure = 100;
    state.viewport_scroll_pixels = 2000;
    compose_navigation_travel_t travel;
    travel.begin(state, 1, 0);
    travel.advance(state, 1, false, 0.02);
    const auto intermediate = state.viewport_scroll_pixels;
    check_action(intermediate > 0 && intermediate < 2000);
    state.viewport_scroll_pixels = 4000;
    travel.begin(state, 1, intermediate);
    travel.advance(state, 1, false, 0.075);
    check_action(state.viewport_scroll_pixels == 4000);
    check_action(!travel.target);
    travel.advance(state, 1, false, 0.1);
    check_action(state.viewport_scroll_pixels == 4000);
}

TEST_CASE("Add SPEEDS span from an area writes the length in beats as one undo step",
          "[compose-action-notifications][speeds]") {
    action_notification_fixture_t fixture;
    // Rows 240..720 of a 960-row measure: half a measure, which is two beats.
    fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 240}, {measure_id_t{1}, 720}};
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN)) ==
                 "SPEEDS span added over the selected area.");
    require_action(fixture.history.undo.size() == 1);
    const auto& speeds = fixture.history.document.charts.front().speeds;
    require_action(speeds.size() == 1);
    check_action(speeds.front().position == local_position_t{measure_id_t{1}, 240});
    check_action(speeds.front().transition_beats == rational_t{2, 1});
    check_action_false(speeds.front().transition_is_seconds);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().speeds.empty());
}

TEST_CASE("Add SPEEDS span without an area changes nothing", "[compose-action-notifications][speeds]") {
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN));
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
}

TEST_CASE("Relinking a missing sound to a fallback candidate is one undo step",
          "[compose-action-notifications][audio-fallback]") {
    action_notification_fixture_t fixture;
    const auto base = std::filesystem::temp_directory_path() / "wormedit-fallback-relink";
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(base);
    std::ofstream(base / "snare.ogg") << "x";
    std::ofstream(base / "snare.flac") << "x";
    fixture.asset_base = base;
    fixture.history.document.audio_assets = {{.id = asset_id_t{5}, .source_path = "snare.wav"}};
    fixture.history.document.resources = {{.id = resource_id_t{6}, .name = "snare", .asset = asset_id_t{5},
                                           .missing = true}};
    fixture.history = make_document_history(fixture.history.document);
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK);
    action.compose_resource_id = 6;
    action.compose_fallback_candidate = 9;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch(action);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());

    action.compose_fallback_candidate = 1;
    check_action(fixture.dispatch(action) == "Sound relinked to snare.flac.");
    check_action(fixture.history.document.audio_assets.front().source_path == std::filesystem::path{"snare.flac"});
    check_action_false(fixture.history.document.resources.front().missing);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.audio_assets.front().source_path == std::filesystem::path{"snare.wav"});
    check_action(fixture.history.document.resources.front().missing);
    std::filesystem::remove_all(base, error);
}

TEST_CASE("Scroll direction toolbar changes only its family and preserves authored state", "[compose-action-notifications][feedback]") {
    action_notification_fixture_t f;
    const auto family = static_cast<std::size_t>(compose_mode_family(f.history.document.charts.front().lane_layouts.front().layout));
    const bool original = compose_field_downscroll(f.history.document.charts.front(), f.application.editor_preferences);
    const auto cursor = f.state.cursor;
    const auto zoom = f.state.pixels_per_standard_measure;
    const auto revision = f.history.document.revision;
    (void)f.dispatch(f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION));
    check_action(compose_field_downscroll(f.history.document.charts.front(), f.application.editor_preferences) != original);
    check_action(f.state.scroll_direction == (original ? editor_field_scroll_direction_t::up : editor_field_scroll_direction_t::down));
    check_action(f.application.editor_preferences.scroll_directions[family] == (original ? 1 : 2));
    check_action(f.state.cursor == cursor);
    check_action(f.state.pixels_per_standard_measure == zoom);
    check_action(f.history.document.revision == revision);
    check_action(f.history.undo.empty());
    auto stale = f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION);
    stale.compose_input_document_revision += 1;
    (void)f.dispatch(stale);
    check_action(compose_field_downscroll(f.history.document.charts.front(), f.application.editor_preferences) != original);
    (void)f.dispatch(f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION));
    check_action(compose_field_downscroll(f.history.document.charts.front(), f.application.editor_preferences) == original);
}

TEST_CASE("Direction switching rejects an unfinished hold without changing preferences", "[compose-action-notifications][feedback]") {
    action_notification_fixture_t f;
    f.state.pending_hold = pending_hold_t{};
    const auto preferences = f.application.editor_preferences;
    (void)f.dispatch(f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION));
    check_action(f.application.editor_preferences == preferences);
    check_action(f.state.pending_hold.has_value());
    check_action(f.history.document.revision == 1);
    check_action(f.history.undo.empty());
}

TEST_CASE("Field wheel inversion reverses stopped coarse and smooth movement", "[compose-action-notifications][feedback]") {
    const auto downscroll = GENERATE(false, true);
    const auto inverted = GENERATE(false, true);
    const auto kind = GENERATE(WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL, WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL);
    bga_input_fixture_t f;
    f.application.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    f.application.editor_preferences.invert_scroll_wheel = inverted;
    f.state.viewport_scroll_pixels = 600;
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0);
    action.compose_input_count = 0;
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = kind, .delta_y = 1.0,
        .cursor_x_pixels = 200, .cursor_y_pixels = 200};
    (void)f.dispatch_input(action);
    check_action(f.state.viewport_scroll_pixels != 600);
    check_action((f.state.viewport_scroll_pixels > 600) == (inverted == downscroll));
    f.check_unchanged(1);
}

TEST_CASE("Wheel inversion leaves modifier zoom unchanged", "[compose-action-notifications][feedback]") {
    const auto inverted = GENERATE(false, true);
    bga_input_fixture_t f;
    f.application.editor_preferences.invert_scroll_wheel = inverted;
    const auto zoom = f.state.pixels_per_standard_measure;
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0);
    action.compose_input_count = 0;
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .zoom_modifier = true, .delta_y = 1.0, .cursor_x_pixels = 200, .cursor_y_pixels = 200};
    (void)f.dispatch_input(action);
    check_action(f.state.pixels_per_standard_measure < zoom);
    f.check_unchanged(1);
}
