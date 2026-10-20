#include "sys/compose_audition_requests.h"
#include "sys/application_defaults.h"
#include "compose_scene_cache_fixture.h"
#include "sys/compose_sound_actions.h"
#include "../support/workspace_result_assertions.h"
#include "../sys/application_dispatch_fixture.h"
#include "sys/application_command_dispatch.h"
#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_quantize.h"
#include "sys/compose_quantize_request.h"
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
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/workspace.h>
#include <chrono>
#include <bit>
#include <fstream>
#include <thread>
#include <source_location>

// Native drop hit testing is unrelated to the action dispatcher in this
// headless target; no rendered metadata pane can accept a file drop.
wormedit::ui::metadata_drop_target_t worm_ui_frontend_metadata_drop_target(uint32_t, float, float) {
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
        value.compose_action = kind;
        value.compose_input_chart_id = state.chart.value;
        value.compose_input_project_id = history.document.id.value;
        value.compose_input_document_revision = history.document.revision;
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
        value.compose_input_viewport_width_pixels = 900;
        value.compose_input_count = 1;
        value.compose_inputs[0] = {.kind = kind, .object_id = 41,
            .timing_chart_id = 1, .timing_document_revision = history.document.revision,
            .resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA, .x_pixels = 150,
            .y_pixels = compose_source_y(state.pixels_per_standard_measure / 4 - state.viewport_scroll_pixels,
                state.viewport_height_pixels, compose_field_downscroll(history.document.charts.front(), application.preferences.editor_preferences)),
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
    auto dispatch_input(wormedit::ui::workspace_action_t& value) -> std::string {
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
    for (const std::uint8_t kind : {0, 2, 5, 1}) {
        auto reveal = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL, kind);
        fixture.dispatch_input(reveal);
        check_action(fixture.history.document.revision == revision);
        check_action(fixture.history.undo.empty());
    }
    check_action(fixture.state.bga_revealed_lane_mask == 15);
    auto invalid = fixture.input(WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL, 255);
    fixture.dispatch_input(invalid);
    check_action(fixture.state.bga_revealed_lane_mask == 15);
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

TEST_CASE("Compose layer creation accepts a supplied name in the atomic undo action", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    std::snprintf(action.compose_semantic_layer_name, sizeof(action.compose_semantic_layer_name), "Chorus");
    require_action(fixture.dispatch(action) == "Semantic layer added.");
    require_action(fixture.history.document.charts.front().semantic_layers.size() == 1);
    check_action(fixture.history.document.charts.front().semantic_layers.front().name == "Chorus");
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
}

TEST_CASE("Compose dispatcher rejects stale actions without changing history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    --action.compose_input_document_revision;
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(action) ==
                 "Compose view action ignored because the project, chart or document revision changed.");
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
    check_action(fixture.dispatch(wormedit::ui::workspace_action_t{}) == "");
    check_action(fixture.entries().empty());
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose edits reject a stale boundary before reaching synchronous handlers",
          "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER,
                               WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER,
                               WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS,
                               WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE,
                               WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE,
                               WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR,
                               WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM,
                               WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM,
                               WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP);
    auto action = fixture.action(kind);
    std::snprintf(action.timing_meter_numerator_text, sizeof(action.timing_meter_numerator_text), "3");
    std::snprintf(action.timing_meter_denominator_text, sizeof(action.timing_meter_denominator_text), "4");
    const std::uint64_t measure = 1;
    action.compose_measure_ids = &measure;
    action.compose_measure_id_count = 1;
    --action.compose_input_document_revision;
    check_action(fixture.dispatch(action) ==
                 "Compose view action ignored because the project, chart or document revision changed.");
    check_action(fixture.history.document.revision == 1);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().measures.front().definition.meter ==
                 meter_t{4, 4});
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
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

TEST_CASE("Compose bookmark seek feedback is finished once and respects muting",
          "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .anchor = marker_anchor_t::beat, .position = local_position_t{measure_id_t{1}, 480}}};
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.services.transport_seek = [&](std::uint64_t) {
        return compose_action_outcome_t::from(
            std::string{"Playback cursor moved."}, notification_severity_t::hint,
            notification_source_t::audio, compose_feedback_delivery_t::notification);
    };
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK);
    action.compose_bookmark_id = 4;
    const auto post_notifications = GENERATE(true, false);
    check_action(fixture.dispatch(action, post_notifications) == "Playback cursor moved.");
    const auto entries = fixture.entries();
    require_action(entries.size() == (post_notifications ? 1 : 0));
    if (!post_notifications)
        return;
    check_action(entries.front().count == 1);
    check_action(entries.front().source == notification_source_t::audio);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose pending gesture rejects an edit without changing history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.capture_hold();
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
          "Finish or cancel the active Compose gesture before adding a semantic layer.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
    check_action((fixture.state.hold() != nullptr));
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
    fixture.capture_hold();
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
        check_action(fixture.invoke_registered("measures.insert_after"));
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
    require_action(fixture.application.project.quantize.quantize_applied_report.has_value());
    check_action(fixture.application.project.quantize.quantize_applied_report->rows.size() == 1);
    check_action(fixture.application.project.quantize.quantize_applied_report->total_rows == 1);
}

namespace {
auto draw_bga_input(bga_input_fixture_t& fixture, WormUiComposeInputKind kind,
    uint64_t row, bool erase = false) -> wormedit::ui::workspace_action_t {
    auto value = fixture.input(kind, 5);
    value.compose_inputs[0].resource_id = erase ? 0 : 31;
    value.compose_inputs[0].paint_intent = erase ? 2 : 1;
    value.compose_inputs[0].y_pixels = compose_source_y(
        row * fixture.state.pixels_per_standard_measure / 960.0 - fixture.state.viewport_scroll_pixels,
        fixture.state.viewport_height_pixels,
        compose_field_downscroll(fixture.history.document.charts.front(), fixture.application.preferences.editor_preferences));
    return value;
}
}
TEST_CASE("Draw BGA production stroke commits interpolated targets once and undo restores all",
          "[bga][draw][compose-input]") {
    bga_input_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    check_action(fixture.history.undo.empty());
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 720);
    INFO(fixture.dispatch_input(release));
    require_action(fixture.history.undo.size() == 1);
    check_action(fixture.history.document.charts.front().bga_events.size() == 4);
    check_action(!fixture.application.project.compose_gesture.bga_draw_stroke);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().bga_events.size() == 1);
}
TEST_CASE("Draw BGA deleted source and Timing focus reject without undo",
          "[bga][draw][compose-input]") {
    bga_input_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    fixture.history.document.resources.clear();
    const auto revision = fixture.history.document.revision;
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 480);
    (void)fixture.dispatch_input(release);
    fixture.check_unchanged(revision);
    check_action(!fixture.application.project.compose_gesture.bga_draw_stroke);
    fixture.application.project.compose_gesture.note_paint_mode_active = false;
    (void)fixture.dispatch_input(begin);
    fixture.check_unchanged(revision);
    check_action(!fixture.application.project.compose_gesture.bga_draw_stroke);
}
TEST_CASE("Draw BGA interpolation follows measure-local snap across a five eighths measure",
          "[bga][draw][compose-input]") {
    bga_input_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition = {{5, 8}, meter_t{5, 8}};
    chart.measures.push_back({.id = measure_id_t{2}});
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 480);
    (void)fixture.dispatch_input(begin);
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 720);
    (void)fixture.dispatch_input(release);
    require_action(fixture.history.undo.size() == 1);
    const auto& events = fixture.history.document.charts.front().bga_events;
    require_action(events.size() == 3);
    check_action(events[1].position == local_position_t{measure_id_t{1}, 480});
    check_action(events[2].position == local_position_t{measure_id_t{2}, 0});
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
    check_action(fixture.application.project.compose_gesture.draw_source_kind == 2);
    check_action(fixture.application.project.compose_gesture.draw_source_id == 31);
    fixture.check_unchanged(revision);
}

namespace {
auto note_draw_input(bga_input_fixture_t& fixture, WormUiComposeInputKind kind, uint8_t intent)
    -> wormedit::ui::workspace_action_t {
    auto value = fixture.input(kind, 0);
    value.compose_inputs[0].resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
    value.compose_inputs[0].resource_id = 0;
    value.compose_inputs[0].paint_intent = intent;
    const auto& chart = fixture.history.document.charts.front();
    const auto layout = compose_horizontal_layout(value.compose_input_viewport_width_pixels,
        maximum_layout_lane_count(chart), !chart.bgm_source_lanes.empty(),
        fixture.state.bga_lane_visible ? std::popcount(editor_bga_visible_lane_mask(
            chart, fixture.state.bga_revealed_lane_mask)) : 0,
        effective_compose_timing_lane_mask(fixture.application));
    value.compose_inputs[0].x_pixels = layout.field_left_pixels + layout.playable_field_width_pixels * .5;
    return value;
}
void add_draw_test_note(bga_input_fixture_t& fixture) {
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
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
    require_action(fixture.state.gesture<wormedit::pending_note_paint_gesture_t>() != nullptr);
    check_action(fixture.state.gesture<wormedit::pending_note_paint_gesture_t>()->intent == note_paint_intent_t::draw);
    auto end = note_draw_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 1);
    INFO(fixture.dispatch_input(end));
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().measures.front().notes.front().id == object_id_t{91});
    check_action(!fixture.state.gesture<wormedit::pending_note_paint_gesture_t>());
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
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    static_cast<void>(fixture.cache.scene.index.get_or_build(compose_scene_source_key_t{
        .document_identity = &fixture.history.document, .chart_identity = &chart},
        retained_scene_fixture_t{}));
    seed_stem_geometry_fixture(fixture.cache.stems.stem_index, chart_stamp_t::from(fixture.history.document, chart.id),
        {{.asset = asset_id_t{81}, .source_sample_rate = 44'100,
        .source_frame_count = 44'100, .output_sample_rate = 44'100, .output_frame_count = 44'100}});
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
    auto stale_stem_stamp = fixture.cache.stems.stem_index.key()->stamp;
    stale_stem_stamp.document.revision = 0;
    seed_stem_geometry_fixture(fixture.cache.stems.stem_index, stale_stem_stamp,
                               fixture.cache.stems.stem_index.key()->bindings);
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
    require_action(status == "Export target preparation started in the background.");
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
    // Slow: all 24 chart-selection/tab/format/path combinations publish and verify three real WAV slices.
    const bool other_selected_chart = GENERATE(false, true);
    const auto tab = GENERATE(workspace_tab_t::compose, workspace_tab_t::setup, workspace_tab_t::metadata);
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
    require_action(parsed.value.has_value());
    require_action_false(!parsed.ok());
    fixture.history = document_history_t::from(*parsed.value);
    if (other_selected_chart) {
        const auto added = apply_new_chart_command(fixture.history);
        INFO((added.command.diagnostics.empty() ? "" : added.command.diagnostics.front().message));
        require_action(static_cast<bool>(added));
        fixture.history.undo.clear();
    }
    auto& exported_chart = fixture.history.document.charts.front();
    exported_chart.semantic_layers = {{.id = layer_id_t{900}, .name = "Export layer"}};
    for (auto& measure : exported_chart.measures)
        for (auto& note : measure.notes) note.semantic_layers = {layer_id_t{900}};
    fixture.history.document.output_groups = {{.family = target,
        .charts = {fixture.history.document.charts.front().id},
        .relative_output_path = named_output
            ? (target == format_family_t::osu_mania ? "exports/chart.osu" : "exports/chart.bme")
            : "exports"}};
    const auto workspace = workspace_controller_t::from(fixture.history);
    require_action(static_cast<bool>((workspace.value && workspace.diagnostics.empty())));
    fixture.workspace = *workspace.value;
    fixture.workspace.active_tab = tab;
    fixture.workspace.selected_chart = other_selected_chart ? fixture.history.document.charts.back().id
        : fixture.history.document.charts.front().id;
    fixture.state.chart = *fixture.workspace.selected_chart;
    fixture.state.cursor = {fixture.history.document.charts.front().measures.front().id, 0};
    fixture.state.active_lane = fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().id;
    fixture.asset_base = directory;
    const std::vector<float> pcm(88'200, 0.125F);
    const auto wav = audio::encode_wav_pcm16(pcm, 44'100);
    std::ofstream(directory / "stem.wav", std::ios::binary).write(wav.data(), wav.size());
    const auto revision = fixture.history.document.revision;
    std::optional<editor_interaction_state_t> retained;
    require_action(fixture.dispatch_workspace(fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP), retained));
    const auto completed = wait_for_slice_export(fixture);
    require_action(completed.has_value());
    check_action(completed->starts_with("Output target published"));
    check_action(completed->find("conversion loss") != std::string::npos);
    check_action(completed->size() <= notification_message_max_bytes);
    const auto notifications = fixture.entries();
    check_action(std::ranges::any_of(notifications, [&](const auto& entry) {
        return entry.severity == notification_severity_t::warning && entry.message == *completed &&
               entry.detail.find("layer") != std::string::npos &&
               entry.detail.find("hold tail") != std::string::npos &&
               entry.detail.find("Chart ") != std::string::npos;
    }));
    const auto output_directory =
        directory / fixture.history.document.output_groups.front().relative_output_path;
    std::size_t charts{}, sounds{};
    for (const auto& file : std::filesystem::directory_iterator(output_directory)) {
        charts += file.path().extension() == (target == format_family_t::osu_mania ? ".osu" : ".bme");
        sounds += file.path().extension() == ".wav";
    }
    check_action(charts == 1);
    check_action(sounds == 3);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(std::filesystem::file_size(directory / "stem.wav") == wav.size());
}

TEST_CASE("BMSON editor BMS export materializes silent gameplay in typed folders",
          "[unit][sys][conversion][publication]") {
    const auto dotted_destination = GENERATE(false, true);
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
    require_action(parsed.value.has_value());
    require_action_false(!parsed.ok());
    fixture.history = document_history_t::from(*parsed.value);
    auto& chart = fixture.history.document.charts.front();
    const auto& lanes = chart.lane_layouts.front().layout.lanes;
    const auto lane = std::ranges::find(lanes, lane_role_t::playable, &lane_definition_t::role);
    require_action(lane != lanes.end());
    chart.measures.front().notes = {{.id = object_id_t{1000},
        .position = {chart.measures.front().id, 0}, .lane = lane->id}};
    fixture.history.document.output_groups = {{.family = format_family_t::bms,
        .charts = {chart.id},
        .relative_output_path = dotted_destination ? "exports/chart.pms" : "exports"}};
    const auto workspace = workspace_controller_t::from(fixture.history);
    require_action(static_cast<bool>((workspace.value && workspace.diagnostics.empty())));
    fixture.workspace = *workspace.value;
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = chart.id;
    fixture.state.chart = chart.id;
    fixture.state.cursor = {chart.measures.front().id, 0};
    fixture.state.active_lane = lane->id;
    fixture.asset_base = directory;
    const auto revision = fixture.history.document.revision;
    require_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP)) ==
                   "Export target preparation started in the background.");
    const auto completed = wait_for_slice_export(fixture);
    require_action(completed.has_value());
    check_action(completed->starts_with("Output target published"));
    const auto output_directory =
        directory / fixture.history.document.output_groups.front().relative_output_path;
    check_action(std::filesystem::exists(output_directory / "conversion-silence.wav"));
    std::size_t charts{}, sounds{};
    for (const auto& file : std::filesystem::directory_iterator(output_directory)) {
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
    fixture.application.project.quantize.quantize_editor =
        application_command_state_t::quantize_editor_t{
                                                       .stamp = chart_stamp_t::from(fixture.history.document, fixture.state.chart),
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
    check_action(fixture.application.project.quantize.quantize_editor->preset_index == 0);
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
                    bool erase = false) -> wormedit::ui::workspace_action_t {
    auto value = draw_bga_input(fixture, kind, row, erase);
    value.compose_inputs[0].resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    value.compose_inputs[0].lane_id = 50;
    value.compose_inputs[0].resource_id = erase ? 0 : 33;
    return value;
}
void prepare_bgm_draw_fixture(bga_input_fixture_t& fixture) {
    fixture.history.document.audio_assets = {{.id = asset_id_t{71}}};
    fixture.history.document.resources.push_back({.id = resource_id_t{33}, .asset = asset_id_t{71},
        .roles = {resource_role_t::sample}});
    fixture.history.document.charts.front().bgm_source_lanes = {{.id = object_id_t{50}, .name = "BGM"}};
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
}
auto draw_stroke_input(bga_input_fixture_t& fixture, bool bgm, WormUiComposeInputKind kind,
                       uint64_t row) -> wormedit::ui::workspace_action_t {
    return bgm ? draw_bgm_input(fixture, kind, row) : draw_bga_input(fixture, kind, row);
}

}
TEST_CASE("A single BGM click stroke commits once without a draw warning",
          "[bgm][draw][compose-input]") {
    bga_input_fixture_t fixture;
    prepare_bgm_draw_fixture(fixture);
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
    check_action(!fixture.application.project.compose_gesture.bga_draw_stroke);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Draw BGM production stroke commits once and erase removes the lane once",
          "[bgm][draw][compose-input]") {
    bga_input_fixture_t fixture;
    prepare_bgm_draw_fixture(fixture);
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
    check_action(!fixture.application.project.compose_gesture.bga_draw_stroke);
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
    check_action(fixture.application.project.compose_view.requested_snap_denominators.at(1) == 16);
    check_action(fixture.application.project.quantize.quantize_editor->request.divisions == 16);

    // A grid the chart cannot snap to sets only the quantize target.
    fixture.entries();
    (void)fixture.dispatch(grid(12));
    check_action(fixture.state.active_snap_denominator == 16);
    check_action(fixture.application.project.quantize.quantize_editor->request.divisions == 12);

    // Next snap moves the quantize target with it and keeps the other settings.
    fixture.application.project.quantize.quantize_editor->request.tie = quantization_tie_t::later;
    std::optional<editor_interaction_state_t> compose = fixture.state;
    std::optional<timing_lane_pseudonote_outcome_t> preview;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::navigation_next_snap}, fixture.history,
        fixture.workspace, compose, preview, fixture.application, fixture.sink);
    require_action(result.value);
    check_action(compose->active_snap_denominator == 4);
    check_action(fixture.application.project.quantize.quantize_editor->request.divisions == 4);
    check_action(fixture.application.project.quantize.quantize_editor->request.tie == quantization_tie_t::later);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("BMSON lane_mode dispatcher shares channel and sound targets with atomic refusal",
          "[unit][sys][bmson][lane_mode]") {
    const bool sounds_tab = GENERATE(false, true);
    action_notification_fixture_t fixture;
    const std::string text = R"({"version":"1.0.0","info":{"title":"Modes","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"stem.wav","notes":[{"x":0,"y":0,"c":false},{"x":0,"y":240,"c":true}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    require_action(parsed.value.has_value());
    fixture.history = document_history_t::from(*parsed.value);
    fixture.workspace = *workspace_controller_t::from(fixture.history).value;
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
    require_action(parsed.value.song.has_value() && parsed.ok());
    action_notification_fixture_t fixture;
    fixture.history = document_history_t::from(*parsed.value.song);
    fixture.workspace = *workspace_controller_t::from(fixture.history).value;
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = fixture.history.document.charts.front().id;
    fixture.state.chart = fixture.history.document.charts.front().id;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t metadata{fixture.sink};
    std::optional<editor_interaction_state_t> interaction{fixture.state};
    const auto dispatch = [&](wormedit::ui::workspace_action_t action) {
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
    wormedit::ui::workspace_action_t create{};
    create.compose_action = WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK;
    create.compose_branch_statement_id = statement->id.value;
    check_action(dispatch(create));
    check_action(fixture.history.undo.size() == 1);
    const auto* block = find_node(bms_control_kind_t::random);
    require_action(block != nullptr);
    wormedit::ui::workspace_action_t remove{};
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
    fixture.application.project.quantize.quantize_editor->request.tie = quantization_tie_t::later;
    auto snap = fixture.action(WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP);
    snap.compose_snap_denominator = 8;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch(snap);
    check_action(fixture.state.active_snap_denominator == 8);
    check_action(fixture.application.project.compose_view.requested_snap_denominators.at(1) == 8);
    check_action(fixture.application.project.quantize.quantize_editor->request.divisions == 8);
    check_action(fixture.application.project.quantize.quantize_editor->request.tie == quantization_tie_t::later);
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
        if (paste) fixture.application.project.clipboard.paste_session = application_command_state_t::paste_session_t{};
        else fixture.capture_hold();
        auto snap = fixture.action(WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP);
        snap.compose_snap_denominator = 8;
        (void)fixture.dispatch(snap);
        check_action(fixture.state.active_snap_denominator == 4);
        check_action(fixture.application.project.compose_view.requested_snap_denominators.empty());
        check_action(fixture.history.undo.empty());
    }
}

TEST_CASE("Playing Compose navigation uses beats while stopped editing retains its snap",
          "[compose-action-notifications][playhead-skip]") {
    bga_input_fixture_t f;
    f.state.active_snap_denominator = 16;
    f.state.supported_snap_denominators = {4, 16};
    f.application.project.transport.preview_playhead_absolute_row = 240;
    f.application.services.transport_preview_playing = [] { return true; };
    std::uint64_t sought{};
    f.application.services.transport_seek = [&](std::uint64_t frame) {
        sought = frame;
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0);
    action.compose_inputs[0].navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD;
    (void)f.dispatch_input(action);
    check_action(f.application.project.transport.preview_playhead_absolute_row == 480);
    check_action(sought > 0);
    check_action((f.state.cursor == local_position_t{measure_id_t{1}, 0}));
    check_action(f.state.active_snap_denominator == 16);
    f.application.services.transport_preview_playing = [] { return false; };
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
    check_action(state.viewport_scroll_pixels == (interrupt == 4 ? 128 : previous));
}

TEST_CASE("Playback wheel skips one beat in chart direction independently of snap",
          "[compose-action-notifications][playhead-skip]") {
    const auto forward = GENERATE(false, true);
    const auto downscroll = GENERATE(false, true);
    const auto inverted = GENERATE(false, true);
    bga_input_fixture_t f;
    f.state.active_snap_denominator = 16;
    f.application.preferences.editor_preferences.invert_scroll_wheel = inverted;
    f.application.preferences.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    f.application.project.transport.preview_playhead_absolute_row = 480;
    f.application.services.transport_preview_playing = [] { return true; };
    f.application.services.transport_seek = [](std::uint64_t) {
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    auto action = f.input(WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0);
    action.compose_input_count = 0;
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {
        .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = ((forward != downscroll) != inverted) ? 1.0 : -1.0,
        .cursor_x_pixels = 200, .cursor_y_pixels = 200};
    (void)f.dispatch_input(action);
    check_action(f.application.project.transport.preview_playhead_absolute_row == (forward ? 720 : 240));
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
    f.application.services.transport_preview_playing = [=] { return playing; };
    f.application.project.transport.preview_playhead_absolute_row = 959;
    f.application.services.transport_seek = [](std::uint64_t) {
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    auto action = f.input(doubled ? WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP
                                  : WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0);
    action.compose_inputs[0].navigation_action = doubled
        ? WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_FORWARD : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    (void)f.dispatch_input(action);
    const auto index = measure_index_t::from(chart).value();
    const auto expected = short_measures ? (doubled ? 1919 : 1439) : (doubled ? 2879 : 1919);
    if (playing) check_action(f.application.project.transport.preview_playhead_absolute_row == expected);
    else check_action(index.row_of(f.state.cursor) == expected);
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

TEST_CASE("Area SPEEDS duration reduces a wide row product",
          "[compose-action-notifications][speeds][wide-area]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().divisions_per_standard_measure = std::uint64_t{1} << 63;
    fixture.state.retained_selection_range =
        position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, std::uint64_t{1} << 62}};
    (void)fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN));
    const auto& speeds = fixture.history.document.charts.front().speeds;
    require_action(speeds.size() == 1);
    check_action(speeds.front().transition_beats == rational_t{2, 1});
    require_action(fixture.history.undo.size() == 1);
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
    fixture.history = document_history_t::from(fixture.history.document);
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK);
    action.compose_resource_id = 6;
    action.compose_fallback_candidate = 9;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch(action);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());

    action.compose_fallback_candidate = 1;
    check_action(fixture.dispatch(action) == "Sound relinked to snare.flac.");
    check_action(fixture.workspace.document_revision == fixture.history.document.revision);
    check_action(fixture.workspace.history.undo_depth == 1);
    check_action(fixture.history.document.audio_assets.front().source_path == std::filesystem::path{"snare.flac"});
    check_action_false(fixture.history.document.resources.front().missing);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.audio_assets.front().source_path == std::filesystem::path{"snare.wav"});
    check_action(fixture.history.document.resources.front().missing);
    std::filesystem::remove_all(base, error);
}

TEST_CASE("Lane-less chart fallback relink preserves revision guards and undo",
          "[compose-action-notifications][audio-fallback][missing-audio]") {
    action_notification_fixture_t fixture;
    const auto base = std::filesystem::temp_directory_path() / "wormedit-fallback-relink-lane-less";
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(base);
    std::ofstream(base / "snare.ogg") << "x";
    std::ofstream(base / "snare.flac") << "x";
    fixture.asset_base = base;
    fixture.history.document.audio_assets = {{.id = asset_id_t{5}, .source_path = "snare.wav"}};
    fixture.history.document.resources = {{.id = resource_id_t{6}, .name = "snare", .asset = asset_id_t{5},
                                           .missing = true}};
    fixture.history.document.charts.front().lane_layouts.clear();
    fixture.history.document.charts.front().input_profile.bindings.clear();
    fixture.history = document_history_t::from(fixture.history.document);
    std::optional<editor_interaction_state_t> interaction;
    wormedit::test::require_workspace_result(rebind_workspace(fixture.workspace, fixture.history));
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK);
    action.compose_resource_id = 6;
    action.compose_fallback_candidate = 9;
    const auto revision = fixture.history.document.revision;
    (void)fixture.dispatch_workspace(action, interaction);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());

    action.compose_fallback_candidate = 1;
    auto stale = action;
    ++stale.compose_input_document_revision;
    (void)fixture.dispatch_workspace(stale, interaction);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    require_action(fixture.dispatch_workspace(action, interaction));
    check_action(!interaction);
    check_action(fixture.history.document.charts.front().lane_layouts.empty());
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
    const bool original = compose_field_downscroll(f.history.document.charts.front(), f.application.preferences.editor_preferences);
    const auto cursor = f.state.cursor;
    const auto zoom = f.state.pixels_per_standard_measure;
    const auto revision = f.history.document.revision;
    (void)f.dispatch(f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION));
    check_action(compose_field_downscroll(f.history.document.charts.front(), f.application.preferences.editor_preferences) != original);
    check_action(f.state.scroll_direction == (original ? editor_field_scroll_direction_t::up : editor_field_scroll_direction_t::down));
    check_action(f.application.preferences.editor_preferences.scroll_directions[family] == (original ? 1 : 2));
    check_action(f.state.cursor == cursor);
    check_action(f.state.pixels_per_standard_measure == zoom);
    check_action(f.history.document.revision == revision);
    check_action(f.history.undo.empty());
    auto stale = f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION);
    stale.compose_input_document_revision += 1;
    (void)f.dispatch(stale);
    check_action(compose_field_downscroll(f.history.document.charts.front(), f.application.preferences.editor_preferences) != original);
    (void)f.dispatch(f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION));
    check_action(compose_field_downscroll(f.history.document.charts.front(), f.application.preferences.editor_preferences) == original);
}

TEST_CASE("Direction switching rejects an unfinished hold without changing preferences", "[compose-action-notifications][feedback]") {
    action_notification_fixture_t f;
    f.capture_hold();
    const auto preferences = f.application.preferences.editor_preferences;
    (void)f.dispatch(f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION));
    check_action(f.application.preferences.editor_preferences == preferences);
    check_action((f.state.hold() != nullptr));
    check_action(f.history.document.revision == 1);
    check_action(f.history.undo.empty());
}

TEST_CASE("Field wheel inversion reverses stopped coarse and smooth movement", "[compose-action-notifications][feedback]") {
    const auto downscroll = GENERATE(false, true);
    const auto inverted = GENERATE(false, true);
    const auto kind = GENERATE(WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL, WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL);
    bga_input_fixture_t f;
    f.application.preferences.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    f.application.preferences.editor_preferences.invert_scroll_wheel = inverted;
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
    f.application.preferences.editor_preferences.invert_scroll_wheel = inverted;
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

TEST_CASE("Sounds tab audition reaches the shared resource dispatcher without a Compose interaction",
          "[sys][sound-audition]") {
    action_notification_fixture_t fixture;
    fixture.workspace.active_tab = workspace_tab_t::audio_slices;
    fixture.history.document.audio_assets = {{.id = asset_id_t{10}, .source_path = "voice.wav"}};
    fixture.history.document.resources = {{.id = resource_id_t{20}, .name = "Voice",
        .asset = asset_id_t{10}, .roles = {resource_role_t::sample}}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AUDITION_SOUND);
    action.compose_resource_id = 20;
    std::optional<editor_interaction_state_t> interaction;
    REQUIRE(fixture.dispatch_workspace(action, interaction));
    CHECK(fixture.application.project.auditions.has_pending_request(resource_id_t{20}));
    CHECK_FALSE(interaction.has_value());
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Sounds tab audition rejects a stale document before scheduling audio",
          "[sys][sound-audition]") {
    action_notification_fixture_t fixture;
    fixture.workspace.active_tab = workspace_tab_t::audio_slices;
    fixture.history.document.audio_assets = {{.id = asset_id_t{10}, .source_path = "voice.wav"}};
    fixture.history.document.resources = {{.id = resource_id_t{20}, .asset = asset_id_t{10},
        .roles = {resource_role_t::sample}}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AUDITION_SOUND);
    action.compose_resource_id = 20;
    ++action.compose_input_document_revision;
    std::optional<editor_interaction_state_t> interaction;
    REQUIRE(fixture.dispatch_workspace(action, interaction));
    CHECK_FALSE(fixture.application.project.auditions.has_pending_request());
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("BGA visibility reveals an empty base lane on the first click without a document edit",
          "[compose-action-notifications][bga][bga-visibility]") {
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    check_action(editor_bga_visible_lane_mask(fixture.history.document.charts.front(), 0) == 0);
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY)) ==
        "BGA lane shown.");
    check_action(fixture.state.bga_lane_visible);
    check_action(fixture.state.bga_revealed_lane_mask == 1);
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY)) ==
        "BGA lane hidden.");
    check_action(!fixture.state.bga_lane_visible);
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY)) ==
        "BGA lane shown.");
    check_action(fixture.state.bga_revealed_lane_mask == 1);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().bga_events.empty());
}

TEST_CASE("BGA visibility preserves authored and revealed kinds and rejects pending gestures",
          "[compose-action-notifications][bga][bga-visibility]") {
    bga_input_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    const auto mask = fixture.state.bga_revealed_lane_mask;
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY)) ==
        "BGA lane hidden.");
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY)) ==
        "BGA lane shown.");
    check_action(fixture.state.bga_revealed_lane_mask == mask);
    fixture.capture_hold();
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY)).find("Finish or cancel") != std::string::npos);
    check_action(fixture.state.bga_lane_visible);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().bga_events.size() == 1);
}

TEST_CASE("Created seven-key layout dispatches a compatible profile without moving notes",
          "[compose-action-notifications][lane-profile][compatible-layout]") {
    action_notification_fixture_t fixture;
    const auto profile = make_osu_mania_profile(7);
    require_action(profile.has_value());
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.front().layout = profile->layout;
    chart.lane_layouts.front().layout.profile_id = "created-7k";
    chart.input_profile = profile->input;
    chart.measures.front().notes = {{.id = object_id_t{30}, .position = {measure_id_t{1}, 240},
                                    .lane = lane_id_t{3}}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    action.compose_lane_profile_index = 0;
    check_action(fixture.dispatch(action) == "Lane profile set to 7K.");
    const auto& updated = fixture.history.document.charts.front();
    check_action(updated.lane_layouts.front().layout.profile_id == "osu-mania-7k");
    check_action(updated.measures.front().notes.front().lane == lane_id_t{3});
    check_action(updated.measures.front().notes.front().position.row == 240);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().lane_layouts.front().layout.profile_id == "created-7k");
}

TEST_CASE("Layer assist toggles are chart stamped session changes without document or Undo edits",
          "[unit][compose-actions][layer-assist]") {
    action_notification_fixture_t f;
    f.history.document.charts.front().semantic_layers = {{.id=layer_id_t{7},.name="Ticks",.locked=true}};
    wormedit::sys::metronome_t metronome;
    metronome.settings.available = true;
    f.application.metronome = &metronome;
    std::vector<layer_id_t> configured;
    f.application.services.transport_set_layer_assist = [&](chart_id_t chart, std::span<const layer_id_t> layers) {
        check_action(chart == chart_id_t{1});
        configured.assign(layers.begin(),layers.end());
        return true;
    };
    auto action = f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK);
    action.compose_semantic_layer_id=7;
    (void)f.dispatch(action);
    check_action(configured == std::vector<layer_id_t>{layer_id_t{7}});
    check_action(f.application.project.semantic_layer.layer_assist_layers == configured);
    check_action(f.history.document.revision == 1 && f.history.undo.empty());
    (void)f.dispatch(action);
    check_action(configured.empty() && f.application.project.semantic_layer.layer_assist_layers.empty());
    action.compose_input_document_revision=0;
    (void)f.dispatch(action);
    check_action(configured.empty());
    action.compose_input_document_revision=1;
    action.compose_input_project_id=99;
    const auto wrong_project = f.dispatch(action);
    INFO(wrong_project);
    INFO("project IDs " << action.compose_input_project_id << " / " << f.history.document.id.value);
    check_action(wrong_project.find("different project") != std::string::npos);
    check_action(configured.empty());
    action.compose_input_project_id=f.history.document.id.value;
    action.compose_semantic_layer_id=99;
    (void)f.dispatch(action);
    check_action(configured.empty());
}

TEST_CASE("Unavailable and rejected layer assist callbacks leave the current selection untouched",
          "[unit][compose-actions][layer-assist]") {
    action_notification_fixture_t f;
    f.history.document.charts.front().semantic_layers = {{.id=layer_id_t{7},.name="Ticks"}};
    auto action=f.action(WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK);
    action.compose_semantic_layer_id=7;
    (void)f.dispatch(action);
    check_action(f.application.project.semantic_layer.layer_assist_layers.empty());
    wormedit::sys::metronome_t metronome;
    metronome.settings.available = true;
    f.application.metronome = &metronome;
    f.application.services.transport_set_layer_assist=[](chart_id_t,std::span<const layer_id_t>){return false;};
    (void)f.dispatch(action);
    check_action(f.application.project.semantic_layer.layer_assist_layers.empty());
    check_action(f.history.document.revision==1 && f.history.undo.empty());
}

TEST_CASE("Find fitting snap includes hold ends and BGM and changes only session grid", "[unit][compose-actions][fitting-snap]") {
    action_notification_fixture_t f;
    auto& chart=f.history.document.charts.front();
    chart.measures.front().notes={{.id=object_id_t{7},.position={measure_id_t{1},192},.lane=lane_id_t{1},
        .kind=note_kind_t::hold,.end=local_position_t{measure_id_t{1},384}}};
    f.state.selected_notes={object_id_t{7}};
    auto action=f.action(WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP);
    (void)f.dispatch(action);
    check_action(f.state.active_snap_denominator==5);
    check_action(f.application.project.quantize.quantize_editor->request.divisions==5);
    chart.bgm_source_lanes={{.id=object_id_t{9}}};
    chart.measures.front().bgm_events={{.id=object_id_t{10},.position={measure_id_t{1},48},.source_lane=object_id_t{9}}};
    f.state.selected_objects={{editor_selection_object_kind_t::bgm_event,object_id_t{10}}};
    (void)f.dispatch(action);
    check_action(f.state.active_snap_denominator==20);
    check_action(f.application.project.compose_view.requested_snap_denominators.at(1)==20);
    check_action(f.history.document.revision==1 && f.history.undo.empty());
    check_action(chart.measures.front().notes.front().position.row==192);
}

TEST_CASE("Find fitting snap rejects empty stale and gesturing selection", "[unit][compose-actions][fitting-snap]") {
    action_notification_fixture_t f;
    auto action=f.action(WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP);
    (void)f.dispatch(action);
    check_action(f.state.active_snap_denominator==4);
    f.history.document.charts.front().measures.front().notes={{.id=object_id_t{7},
        .position={measure_id_t{1},192},.lane=lane_id_t{1}}};
    f.state.selected_notes={object_id_t{7}};
    action.compose_input_document_revision=0;
    (void)f.dispatch(action);
    check_action(f.state.active_snap_denominator==4);
    action.compose_input_document_revision=1;
    action.compose_input_project_id=99;
    (void)f.dispatch(action);
    check_action(f.state.active_snap_denominator==4);
    action.compose_input_project_id=f.history.document.id.value;
    f.capture_hold();
    (void)f.dispatch(action);
    check_action(f.state.supported_snap_denominators==std::vector<std::uint64_t>{4});
    check_action(f.history.undo.empty());
}

TEST_CASE("Project export rejects stale stamps and reports missing paths without Compose state",
          "[unit][sys][project-access]") {
    const bool stale = GENERATE(false, true);
    action_notification_fixture_t fixture;
    fixture.workspace.active_tab = workspace_tab_t::setup;
    std::optional<editor_interaction_state_t> state;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP);
    if (stale) ++action.compose_input_project_id;
    require_action(fixture.dispatch_workspace(action, state));
    const auto entries = fixture.entries();
    const std::string_view expected = stale ? "project, chart or document revision changed" : "saved native project";
    INFO("Expected export rejection: " << expected);
    check_action(std::ranges::any_of(entries, [&](const auto& entry) {
        return entry.message.find(expected) != std::string::npos;
    }));
    check_action(!state);
    check_action(fixture.history.undo.empty());
    check_action(!fixture.export_runtime.is_busy());
}

TEST_CASE("Workspace stale actions use the shared message without mutation",
          "[compose-action-notifications][stale-message]") {
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE,
                              WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT,
                              WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK,
                              WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE);
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto action = fixture.action(kind);
    --action.compose_input_document_revision;
    std::optional<editor_interaction_state_t> interaction;
    require_action(fixture.dispatch_workspace(action, interaction));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    const char* label = "BMS branch action";
    if (kind == WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE) label = "Lane layout change";
    if (kind == WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT) label = "BGM column count edit";
    if (kind == WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK) label = "Sound relink";
    check_action(entries.front().message == stale_action_message(label));
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Standalone sound-layer entry rejects stale revisions with shared feedback",
          "[compose-action-notifications][sound-layer-stamp]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER);
    --action.compose_input_document_revision;
    const std::uint64_t resource = 21;
    action.compose_sound_resource_ids = &resource;
    action.compose_sound_resource_count = 1;
    action.compose_semantic_layer_id = 7;
    const auto revision = fixture.history.document.revision;
    const auto response = dispatch_compose_sound_action(action, fixture.history, fixture.workspace,
        fixture.state.chart, fixture.application, fixture.sink);
    require_action(response.has_value());
    check_action(*response == stale_action_message("Sound layer edit"));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Standalone sound edits use shared stale feedback without mutation",
          "[compose-action-notifications][sound-command-stamp]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().source_family = format_family_t::bms;
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS,
                               WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS,
                               WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS,
                               WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS);
    auto action = fixture.action(kind);
    --action.compose_input_document_revision;
    const std::uint64_t resource = 21;
    action.compose_sound_resource_ids = &resource;
    action.compose_sound_resource_count = 1;
    const wormedit::ui::sound_group_assignment_t assignment{.name = "Group", .sound_ids = &resource,
                                               .sound_count = 1};
    action.compose_sound_group_assignments = &assignment;
    action.compose_sound_group_assignment_count = 1;
    const auto revision = fixture.history.document.revision;
    const auto response = dispatch_compose_sound_action(action, fixture.history, fixture.workspace,
        fixture.state.chart, fixture.application, fixture.sink);
    require_action(response.has_value());
    const auto* label = kind == WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS ? "Resource deletion"
        : kind == WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS ? "BMS identifier renumber"
        : "Sound group edit";
    check_action(*response == stale_action_message(label));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Captured BMSON marker batches share stale feedback and keep missing targets separate",
          "[compose-action-notifications][bmson-marker-stamp]") {
    action_notification_fixture_t fixture;
    const auto kind = GENERATE(0, 1, 2, 3);
    auto& draft = fixture.application.project.compose_gesture.bmson_marker_draft;
    if (kind != 0) {
        draft.emplace();
        draft->stamp = chart_stamp_t::from(fixture.history.document,
            kind == 3 ? chart_id_t{99} : fixture.state.chart);
        draft->stamp.document.revision -= kind == 2 ? 1 : 0;
        if (kind != 1) draft->payload.markers.push_back({.position = {measure_id_t{1}, 120}});
    }
    const auto action = fixture.action(WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM);
    const auto response = fixture.dispatch(action);
    check_action(response == (kind < 2
        ? "The BMSON marker batch is no longer available for this chart revision."
        : stale_action_message("BMSON marker batch")));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(!draft.has_value());
    check_action(fixture.history.document.revision == 1);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("BMS statement boundary shares stale feedback after missing-target checks",
          "[compose-action-notifications][bms-statement-stamp]") {
    action_notification_fixture_t fixture;
    const bool missing = GENERATE(false, true);
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT);
    --action.compose_input_document_revision;
    action.compose_branch_statement_id = missing ? 0 : 7;
    std::optional<editor_interaction_state_t> interaction = fixture.state;
    require_action(fixture.dispatch_workspace(action, interaction));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().message == (missing ? "Select a raw BMS branch statement first."
        : stale_action_message("BMS branch statement action")));
    check_action(entries.front().severity == (missing ? notification_severity_t::warning
                                                    : notification_severity_t::error));
    check_action(entries.front().source == notification_source_t::format);
    check_action(fixture.history.document.revision == 1);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Standalone sound-import confirmation shares stale feedback",
          "[compose-action-notifications][sound-import-stamp]") {
    action_notification_fixture_t fixture;
    const auto kind = GENERATE(0, 1, 2);
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT);
    action.sound_import_project_id = fixture.history.document.id.value + (kind == 0 ? 1 : 0);
    if (kind == 1) ++action.compose_input_chart_id;
    if (kind == 2) --action.compose_input_document_revision;
    const auto response = dispatch_compose_sound_action(action, fixture.history, fixture.workspace,
        fixture.state.chart, fixture.application, fixture.sink);
    require_action(response.has_value());
    check_action(*response == stale_action_message("Sound import"));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(entries.front().source == notification_source_t::import_);
    check_action(fixture.history.document.revision == 1);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Captured BGA and BGM stroke expiry shares stale feedback without a document edit",
          "[bga][draw][compose-input][stroke-stamp]") {
    const auto bgm = GENERATE(false, true);
    bga_input_fixture_t fixture;
    if (bgm) prepare_bgm_draw_fixture(fixture);
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    auto begin = draw_stroke_input(fixture, bgm, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    require_action(fixture.application.project.compose_gesture.bga_draw_stroke.has_value());
    --fixture.application.project.compose_gesture.bga_draw_stroke->stamp.document.revision;
    auto release = draw_stroke_input(fixture, bgm, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 480);
    check_action(fixture.dispatch_input(release) == stale_action_message(bgm ? "BGM Draw stroke" : "BGA Draw stroke"));
    check_action(!fixture.application.project.compose_gesture.bga_draw_stroke);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Captured BGA stroke keeps revision-only project identity",
          "[bga][draw][compose-input][stroke-stamp]") {
    bga_input_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    auto begin = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 240);
    (void)fixture.dispatch_input(begin);
    require_action(fixture.application.project.compose_gesture.bga_draw_stroke.has_value());
    fixture.application.project.compose_gesture.bga_draw_stroke->stamp.document.project = song_id_t{99};
    auto release = draw_bga_input(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 480);
    check_action(fixture.dispatch_input(release) == "BGA Draw stroke committed.");
    check_action(fixture.history.undo.size() == 1);
}

TEST_CASE("Captured BMSON razor expiry is distinct from starting a new stroke",
          "[compose-input][bmson-razor-stamp]") {
    bga_input_fixture_t fixture;
    fixture.history.document.charts.front().source_family = format_family_t::bmson;
    const auto kind = GENERATE(0, 1, 2, 3);
    auto& stroke = fixture.application.project.compose_gesture.bmson_razor_stroke;
    if (kind != 0) {
        stroke = application_command_state_t::bmson_razor_stroke_proposal_t{
            .stamp = chart_stamp_t::from(fixture.history.document, fixture.state.chart),
            .payload = {.reference = object_id_t{kind == 2 ? 99u : 41u}}};
        if (kind == 1) --stroke->stamp.document.revision;
        if (kind == 3) stroke->stamp.chart = chart_id_t{99};
    }
    auto action = fixture.input(WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART, 0);
    const auto message = fixture.dispatch_input(action);
    check_action(message == (kind == 1 ? stale_action_message("BMSON razor stroke")
        : "Begin a new BMSON razor stroke on the selected sound channel."));
    check_action(!stroke);
    check_action(fixture.history.undo.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
}

TEST_CASE("Rejected Compose meter edits keep their validation feedback without rebinding",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    fixture.workspace.song = song_id_t{99};
    const auto selected = GENERATE(false, true);
    auto action = fixture.action(selected ? WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER
                                          : WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER);
    const std::array<std::uint64_t, 1> ids{0};
    action.compose_measure_ids = ids.data();
    action.compose_measure_id_count = ids.size();
    std::snprintf(action.timing_meter_numerator_text, sizeof(action.timing_meter_numerator_text),
                  "0");
    std::snprintf(action.timing_meter_denominator_text,
                  sizeof(action.timing_meter_denominator_text), "4");
    check_action(fixture.dispatch(action) == "A meter needs a non-zero numerator and denominator.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Rejected Compose transforms and measure edits retain validation feedback",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    fixture.workspace.song = song_id_t{99};
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR,
                               WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS,
                               WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE);
    auto action = fixture.action(kind);
    const std::array<std::uint64_t, 1> ids{0};
    action.compose_measure_ids = ids.data();
    action.compose_measure_id_count = ids.size();
    const auto expected = kind == WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR
                              ? "Select notes before transforming them."
                          : kind == WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS
                              ? "A chart needs at least one measure."
                              : "A meter needs a non-zero numerator and denominator.";
    check_action(fixture.dispatch(action) == expected);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose area mutations rebind the workspace before publishing feedback",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    fixture.state.retained_selection_range =
        position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 240}};
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE,
                               WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN);
    const auto status = fixture.dispatch(fixture.action(kind));
    check_action(status == (kind == WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE
                                ? "SV recipe added over the selected area."
                                : "SPEEDS span added over the selected area."));
    check_action(fixture.history.undo.size() == 1);
    check_action(fixture.workspace.document_revision == fixture.history.document.revision);
    check_action(fixture.workspace.history.undo_depth == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
}

TEST_CASE("Registered quantize rebind failure posts the edit and one error while rejecting the command",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().measures.front().notes = {
        {.id = object_id_t{4}, .position = {measure_id_t{1}, 130}, .lane = lane_id_t{1}}};
    fixture.state.selected_notes = {object_id_t{4}};
    fixture.workspace.song = song_id_t{99};
    check_action_false(fixture.invoke_registered("edit.apply_quantize"));
    check_action(fixture.history.undo.size() == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::document);
    check_action(entries.back().severity == notification_severity_t::success);
    check_action(entries.back().source == notification_source_t::edit);
}

TEST_CASE("Sound and BGA rebind failures keep one edit-sourced error and their distinct text",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    const auto bga = GENERATE(false, true);
    if (bga)
        fixture.history.document.visual_assets = {{.id = asset_id_t{61}, .kind = asset_kind_t::image}};
    else
        fixture.history.document.audio_assets = {{.id = asset_id_t{61}, .source_path = "unused.wav"}};
    fixture.history.document.resources = {{.id = resource_id_t{31}, .name = "Unused",
        .asset = asset_id_t{61}, .roles = {bga ? resource_role_t::bga : resource_role_t::sample}}};
    fixture.workspace.song = song_id_t{99};
    auto action = fixture.action(bga ? WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA
                                    : WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS);
    const std::array<std::uint64_t, 1> ids{31};
    action.compose_sound_resource_ids = ids.data();
    action.compose_sound_resource_count = ids.size();
    check_action(fixture.dispatch(action) == (bga ? "BGA changed, but workspace rebinding failed."
        : "workspace cannot be rebound to a different song identity"));
    check_action(fixture.history.undo.size() == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
}

TEST_CASE("Sound import rebind failure retains one import-sourced diagnostic", "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    const auto directory = std::filesystem::temp_directory_path() /
        ("wormedit-feedback-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "sound.wav";
    const std::vector<float> pcm(882, 0.125F);
    const auto wav = audio::encode_wav_pcm16(pcm, 44'100);
    std::ofstream(path, std::ios::binary).write(wav.data(), wav.size());
    fixture.workspace.song = song_id_t{99};
    const std::array<std::string, 1> paths{path.string()};
    const auto message = import_keysound_files(fixture.history, fixture.workspace, fixture.state.chart,
                                               paths, fixture.sink);
    std::filesystem::remove_all(directory);
    check_action(message == "workspace cannot be rebound to a different song identity");
    check_action(fixture.history.undo.size() == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::import_);
    check_action(entries.front().count == 1);
}

TEST_CASE("Quantization preset mutations bind the workspace to the edited history",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
                               WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
                               WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET);
    if (kind != WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET)
        fixture.history.document.quantization_presets = {{.name = "Original", .divisions = 4}};
    auto action = fixture.action(kind);
    std::snprintf(action.compose_quantize_preset_name, sizeof(action.compose_quantize_preset_name),
                  "Updated");
    action.compose_quantize_divisions = 4;
    action.compose_quantize_preset_index = 0;
    fixture.dispatch(action);
    check_action(fixture.history.undo.size() == 1);
    check_action(fixture.workspace.document_revision == fixture.history.document.revision);
    check_action(fixture.workspace.history.undo_depth == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
}

TEST_CASE("Late lane-layout rejection rebinds an already materialized virtual measure",
          "[compose-action-notifications][compose-outcome]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().source_family = format_family_t::bms;
    fixture.history = document_history_t::from(fixture.history.document);
    wormedit::test::require_workspace_result(rebind_workspace(fixture.workspace, fixture.history));
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE);
    action.compose_measure_number = 2;
    check_action(fixture.dispatch(action) == "Choose a lane layout profile first.");
    check_action(fixture.history.document.charts.front().measures.size() == 2);
    check_action(fixture.history.undo.size() == 1);
    check_action(fixture.workspace.document_revision == fixture.history.document.revision);
    check_action(fixture.workspace.history.undo_depth == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
}

TEST_CASE("Compose unknown and unsupported enum routes reject once without binding",
          "[compose-action-notifications][route-parity]") {
    action_notification_fixture_t fixture;
    const auto kind = GENERATE(static_cast<WormUiComposeActionKind>(255),
                               WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER,
                               WORM_UI_COMPOSE_ACTION_SUGGEST_SOUND_GROUPS);
    fixture.workspace.song = song_id_t{99};
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(fixture.action(kind)) == "Compose action was rejected.");
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().message == "Compose action was rejected.");
    check_action(entries.front().source == notification_source_t::input);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(entries.front().count == 1);
}

TEST_CASE("Compose area clear side effect retains precedence across enum groups",
          "[compose-action-notifications][route-parity]") {
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_NONE,
                               WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
                               WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK,
                               static_cast<WormUiComposeActionKind>(255));
    action_notification_fixture_t fixture;
    fixture.state.retained_selection_range =
        position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
    fixture.state.retained_canvas_selection_range = canvas_row_span_t{0, 480};
    auto action = fixture.action(kind);
    action.compose_area_clear_requested = true;
    action.compose_quantize_divisions = 4;
    const auto revision = fixture.history.document.revision;
    const auto message = fixture.dispatch(action);
    const bool is_quantize_setting = kind == WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS;
    check_action(fixture.state.retained_selection_range.has_value() == is_quantize_setting);
    check_action(fixture.state.retained_canvas_selection_range.has_value() == is_quantize_setting);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    if (kind == WORM_UI_COMPOSE_ACTION_NONE) {
        check_action(message.empty());
        check_action(fixture.entries().empty());
        check_action(fixture.center.current_line() == nullptr);
    } else if (is_quantize_setting) {
        require_action(static_cast<bool>(fixture.application.project.quantize.quantize_editor));
        check_action(fixture.application.project.quantize.quantize_editor->request.divisions == 4);
    } else if (kind == WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK) {
        check_action(fixture.application.project.compose_view.y_lock);
    } else {
        check_action(message == "Compose action was rejected.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().count == 1);
    }
}

TEST_CASE("Compose quantize special route records one report and one bound mutation",
          "[compose-action-notifications][route-parity]") {
    const auto timing_focus = GENERATE(false, true);
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().measures.front().notes = {
        {.id = object_id_t{4}, .position = {measure_id_t{1}, 130}, .lane = lane_id_t{1}}};
    fixture.state.selected_notes = {object_id_t{4}};
    fixture.state.retained_selection_range =
        position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
    fixture.application.project.compose_view.compose_timing_focus = timing_focus;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE);
    action.compose_area_clear_requested = true;
    action.compose_quantize_divisions = 4;
    const auto message = fixture.dispatch(action);
    check_action(fixture.history.undo.size() == (timing_focus ? 0 : 1));
    check_action(fixture.state.retained_selection_range.has_value() == timing_focus);
    check_action(fixture.application.project.quantize.quantize_applied_report.has_value() == !timing_focus);
    if (!timing_focus) {
        check_action(message == "Selected objects quantized.");
        check_action(fixture.history.document.charts.front().measures.front().notes.front().position.row == 240);
        check_action(fixture.workspace.document_revision == fixture.history.document.revision);
        check_action(fixture.workspace.history.undo_depth == 1);
        check_action(fixture.application.project.quantize.quantize_applied_report->total_rows == 1);
    } else {
        check_action(message == compose_quantize_idle_message);
        check_action(fixture.history.document.charts.front().measures.front().notes.front().position.row == 130);
    }
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().message == message);
    check_action(entries.front().count == 1);
}

TEST_CASE("Compose BMS kinds retain the workspace-only view rejection",
          "[compose-action-notifications][route-parity]") {
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE,
                               WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT,
                               WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK);
    action_notification_fixture_t fixture;
    check_action(fixture.dispatch(fixture.action(kind)) ==
          "BMS branch actions must use the workspace action boundary.");
    check_action(fixture.history.undo.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().source == notification_source_t::format);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(entries.front().count == 1);
}

TEST_CASE("Compose note-menu and palette routes retain their distinct session effects",
          "[compose-action-notifications][route-parity]") {
    const auto palette = GENERATE(false, true);
    action_notification_fixture_t fixture;
    auto& semantic = fixture.application.project.semantic_layer;
    semantic.note_layer_menu_chart = fixture.state.chart;
    semantic.note_layer_menu_revision = fixture.history.document.revision;
    semantic.note_layer_menu_row_count = 5;
    semantic.semantic_layer_target = layer_id_t{42};
    semantic.semantic_layer_solo_active = true;
    fixture.workspace.song = song_id_t{99}; // Session routes must not attempt a document rebind.
    const auto message = fixture.dispatch(fixture.action(palette
        ? WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER
        : WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU));
    check_action(message == (palette ? "Semantic-layer target: No layer" : ""));
    check_action(semantic.note_layer_menu_row_count == (palette ? 5 : 0));
    check_action(semantic.semantic_layer_target_none == palette);
    check_action(semantic.semantic_layer_target.has_value() == !palette);
    check_action(semantic.semantic_layer_solo_active == !palette);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.revision == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == (palette ? 1 : 0));
    if (palette) {
        check_action(entries.front().source == notification_source_t::input);
        check_action(entries.front().count == 1);
    }
}

TEST_CASE("Compose nested marker and resource groups retain domain rejection at the view boundary",
          "[compose-action-notifications][route-parity]") {
    const auto route = GENERATE(
        std::pair{WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK,
                  "Choose a beat-anchored bookmark before editing it."},
        std::pair{WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION,
                  "Choose a beat-anchored section before editing it."},
        std::pair{WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK,
                  "Choose an audio-time bookmark before editing it."},
        std::pair{WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION,
                  "Choose an audio-time section before editing it."},
        std::pair{WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET,
                  "Resource import used an unknown catalog role."},
        std::pair{WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE,
                  "The selected resource role cannot be placed in the chart."});
    action_notification_fixture_t fixture;
    fixture.workspace.song = song_id_t{99};
    auto action = fixture.action(route.first);
    action.compose_resource_role = 255;
    check_action(fixture.dispatch(action) == route.second);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.revision == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().message == route.second);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(entries.front().source == (route.first == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET
        ? notification_source_t::import_ : notification_source_t::edit));
    check_action(entries.front().count == 1);
}

namespace {
struct measure_lane_input_fixture_t : action_notification_fixture_t {
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    compose_scene_cache_t cache;
    measure_lane_input_fixture_t() {
        history.document.charts.front().measures.push_back({.id = measure_id_t{2}});
        state.viewport_height_pixels = 600;
    }
    auto input(WormUiComposeInputKind kind, std::uint64_t row = 0, std::uint64_t measure = 0)
        -> wormedit::ui::workspace_action_t {
        auto value = action(WORM_UI_COMPOSE_ACTION_NONE);
        value.compose_input_viewport_width_pixels = 900;
        value.compose_input_count = 1;
        const auto y = static_cast<double>(row) * state.pixels_per_standard_measure / 960.0 -
                       state.viewport_scroll_pixels;
        value.compose_inputs[0] = {.kind = kind, .object_id = measure, .x_pixels = 20,
            .y_pixels = compose_source_y(y, state.viewport_height_pixels,
                compose_field_downscroll(history.document.charts.front(), application.preferences.editor_preferences))};
        return value;
    }
    auto dispatch_input(wormedit::ui::workspace_action_t value) -> std::string {
        return dispatch_compose_input_action(value, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }
    auto bar_line_selected(std::uint64_t measure) const -> bool {
        return std::ranges::find(state.selected_objects, editor_selection_object_t{
            editor_selection_object_kind_t::bar_line, object_id_t{measure}}) != state.selected_objects.end();
    }
};
} // namespace

TEST_CASE("Measure lane toggles are session view state and hiding drops bar-line selection",
          "[compose-input][measure-lane]") {
    measure_lane_input_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    INFO(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_MEASURE_LANE)));
    check_action(fixture.application.project.compose_view.measure_lane_visible);
    INFO(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BAR_LINE_TIME)));
    check_action(fixture.application.project.compose_view.bar_line_time == bar_line_time_t::propagate);
    check_action(fixture.state.bar_line_time == bar_line_time_t::propagate);
    INFO(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_BAR_LINE_TIME)));
    check_action(fixture.state.bar_line_time == bar_line_time_t::keep_song_time);
    INFO(fixture.dispatch_input(fixture.input(WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT, 0, 2)));
    check_action(fixture.bar_line_selected(2));
    INFO(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TOGGLE_MEASURE_LANE)));
    check_action(!fixture.application.project.compose_view.measure_lane_visible);
    check_action(!fixture.bar_line_selected(2));
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Measure lane input selects a bar line only while the lane is shown",
          "[compose-input][measure-lane]") {
    measure_lane_input_fixture_t fixture;
    INFO(fixture.dispatch_input(fixture.input(WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT, 0, 2)));
    check_action(!fixture.bar_line_selected(2));
    fixture.application.project.compose_view.measure_lane_visible = true;
    INFO(fixture.dispatch_input(fixture.input(WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT, 0, 2)));
    check_action(fixture.bar_line_selected(2));
    // The chart's first bar line is fixed and cannot be selected.
    INFO(fixture.dispatch_input(fixture.input(WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT, 0, 1)));
    check_action(!fixture.bar_line_selected(1));
}

TEST_CASE("Draw in the measure lane splits the measure and selects the new bar line",
          "[compose-input][measure-lane]") {
    measure_lane_input_fixture_t fixture;
    fixture.application.project.compose_view.measure_lane_visible = true;
    INFO(fixture.dispatch_input(fixture.input(WORM_UI_COMPOSE_INPUT_BAR_LINE_PLACE, 480)));
    const auto& chart = fixture.history.document.charts.front();
    require_action(chart.measures.size() == 3);
    check_action(fixture.history.undo.size() == 1);
    const auto index = measure_index_t::from(chart).value();
    check_action(index.entries[1].absolute_begin_row == 480);
    check_action(fixture.bar_line_selected(chart.measures[1].id.value));
    // Drawing on an existing bar line changes nothing.
    INFO(fixture.dispatch_input(fixture.input(WORM_UI_COMPOSE_INPUT_BAR_LINE_PLACE, 960)));
    check_action(fixture.history.document.charts.front().measures.size() == 3);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().measures.size() == 2);
}

TEST_CASE("Compose finisher submits an initially accepted audition once before rebind",
          "[sys][compose-outcome][audition]") {
    action_notification_fixture_t fixture;
    auto& auditions = fixture.application.project.auditions;
    const document_edit_inputs_t document{.history = fixture.history, .workspace = fixture.workspace,
        .notifications = fixture.sink, .auditions = &auditions};
    auto outcome = compose_action_outcome_t::from({}, notification_severity_t::hint);
    outcome.audition = resource_id_t{20};
    auto finished = finish_compose_action(document, fixture.sink, std::move(outcome));
    CHECK_FALSE(finished.audition.has_value());
    CHECK(auditions.has_pending_request(resource_id_t{20}));
    auditions.submit(resource_id_t{21});
    (void)finish_compose_action(document, fixture.sink, std::move(finished));
    CHECK(auditions.has_pending_request(resource_id_t{21}));
    auto rejected = compose_action_outcome_t::blocked("Rejected");
    rejected.audition = resource_id_t{22};
    (void)finish_compose_action(document, fixture.sink, std::move(rejected));
    CHECK(auditions.has_pending_request(resource_id_t{21}));
    fixture.workspace.song = song_id_t{99};
    auto mutation = compose_action_outcome_t::from({}, notification_severity_t::success,
        notification_source_t::edit, true);
    mutation.audition = resource_id_t{23};
    finished = finish_compose_action(document, fixture.sink, std::move(mutation));
    CHECK(finished.has_rebind_failed);
    CHECK(auditions.has_pending_request(resource_id_t{23}));
}

TEST_CASE("Project edit reset discards a catalog audition retained for the next frame",
          "[sys][compose-outcome][audition]") {
    action_notification_fixture_t fixture;
    fixture.application.project.auditions.submit(resource_id_t{20});
    CHECK(fixture.application.project.auditions.has_pending_request(resource_id_t{20}));
    reset_application_project_edit_state(fixture.application);
    CHECK_FALSE(fixture.application.project.auditions.has_pending_request());
}


TEST_CASE("New project exports group charts with an excluded difficulty active",
          "[unit][sys][output-groups][workflow-blockers]") {
    const bool empty = GENERATE(false, true);
    action_notification_fixture_t fixture;
    auto document = make_initial_document(song_id_t{1}, "osu-mania-7k");
    require_action(document.has_value());
    fixture.history = document_history_t::from(std::move(*document));
    const auto added = apply_add_chart_command(fixture.history, fixture.history.document.charts.front().id);
    require_action(added.payload.chart.has_value());
    fixture.workspace = *workspace_controller_t::from(fixture.history).value;
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = *added.payload.chart;
    fixture.state.chart = *added.payload.chart;
    const auto root = std::filesystem::temp_directory_path() / "wormedit-excluded-export";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    fixture.asset_base = root;
    if (empty) fixture.history.document.output_groups.front().charts.clear();
    const auto status = fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP));
    INFO(status);
    if (empty) {
        check_action(!fixture.export_runtime.is_busy());
        check_action(status == "The selected output group has no matching charts.");
        check_action(!std::filesystem::exists(root / "osu"));
    } else {
        require_action(fixture.export_runtime.is_busy());
        const auto completed = wait_for_slice_export(fixture);
        require_action(completed.has_value());
        INFO(*completed);
        check_action(completed->starts_with("Output target published"));
        const auto output = root / "exports/osu";
        check_action(std::filesystem::is_directory(output));
        std::size_t charts{};
        for (const auto& entry : std::filesystem::directory_iterator(output))
            if (entry.path().extension() == ".osu") ++charts;
        check_action(charts == 1);
    }
    std::filesystem::remove_all(root);
}
