#include "../support/native_bms_fixture.h"
#include "../support/workspace_result_assertions.h"
#include "../sys/application_dispatch_fixture.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/compose/input/compose_input.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>

#include <array>
#include <bit>
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
    std::optional<timing_lane_pseudonote_outcome_t> timing_stage;
    application_command_state_t application;
    compose_scene_cache_t cache;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;

    bool is_workspace_current_when_feedback_posted{true};
    std::size_t feedback_post_count{};
    bool advance_revision_on_feedback{};
    inline static thread_local compose_notification_fixture_t* feedback_probe{};

    static auto feedback_time() -> notification_time_t {
        if (feedback_probe) {
            if (feedback_probe->advance_revision_on_feedback && !feedback_probe->history.undo.empty()) {
                feedback_probe->advance_revision_on_feedback = false;
                ++feedback_probe->history.document.revision;
            }
            ++feedback_probe->feedback_post_count;
            feedback_probe->is_workspace_current_when_feedback_posted &=
                feedback_probe->workspace.document_revision ==
                feedback_probe->history.document.revision;
        }
        return {};
    }

    ~compose_notification_fixture_t() {
        if (feedback_probe == this)
            feedback_probe = nullptr;
    }

    explicit compose_notification_fixture_t(notification_clock_t clock = {})
        : center(std::move(clock)) {
        chart_document_t chart{
            .id = chart_id_t{7}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}}},
            .lane_layouts = {{.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
                .layout = {.profile_id = "notification-test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .bpms = {{.id = object_id_t{11}, .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}}}};
        chart.measures.front().notes = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 120}, .lane = lane_id_t{1}}};
        chart.semantic_layers = {{.id = layer_id_t{8}, .name = "Layer", .color = {1, 2, 3, 255}}};
        history = document_history_t::from({.id = song_id_t{1}, .revision = 9, .charts = {chart}});
        constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
        const auto setup = editor_interaction_state_t::from(history, chart_id_t{7}, lane_id_t{1},
                                                   {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup);
        state = *setup.state;
        state.selected_notes = {object_id_t{1}};
        state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}}};
        optional_state = state;
        const auto binding = workspace_controller_t::from(history);
        wormedit::test::require_workspace_result(binding);
        workspace = *binding.value;
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart_id_t{7};
        application.project.semantic_layer.semantic_layer_target = layer_id_t{8};
    }

    auto drain() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }

    auto place_selected_tap() -> object_id_t {
        const auto placed = dispatch_editor_action(history, state,
            place_tap_action_t{.position = state.cursor});
        REQUIRE(placed);
        REQUIRE(placed.created_note);
        state.selected_notes = {*placed.created_note};
        state.selected_objects = {{editor_selection_object_kind_t::note, *placed.created_note}};
        return *placed.created_note;
    }

    void prepare_lane_chord() {
        auto song = history.document;
        auto& chart = song.charts.front();
        chart.measures.front().notes.clear();
        chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{2}, .name = "Second"});
        chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "Third"});
        history = document_history_t::from(std::move(song));
        const std::array<std::uint64_t, 1> snaps{4};
        const auto setup = editor_interaction_state_t::from(history, chart_id_t{7}, lane_id_t{1},
            {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup.state);
        state = *setup.state;
        state.armed_hitsample = osu_hitsample_t{.clap = true};
        const auto bound = workspace_controller_t::from(history);
        REQUIRE(bound.value);
        workspace = *bound.value;
        workspace.active_tab = workspace_tab_t::compose;
        application.project.compose_gesture.note_paint_mode_active = true;
    }

    auto batch(std::span<const wormedit::ui::compose_input_action_t> inputs) -> std::string {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_chart_id.value = state.chart.value;
        action.compose_input_document_revision = history.document.revision;
        action.compose_input_viewport_width_pixels = 800;
        action.compose_inputs.assign(inputs.begin(), inputs.end());
        return dispatch_compose_input_action(action, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }

    auto input(WormUiComposeInputKind kind) -> std::string {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_chart_id.value = state.chart.value;
        action.compose_input_document_revision = history.document.revision;
        action.compose_input_viewport_width_pixels = 800;
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
        action.compose_inputs[0] = {.kind = kind};
        action.compose_inputs.resize(1);
        return dispatch_compose_input_action(action, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }
};

void check_lane_chord(compose_notification_fixture_t& fixture) {
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    REQUIRE(notes.size() == 3);
    std::vector<std::uint64_t> lanes;
    for (const auto& note : notes) {
        lanes.push_back(note.lane.value);
        CHECK(note.osu_hitsample == osu_hitsample_t{.clap = true});
    }
    std::ranges::sort(lanes);
    CHECK(lanes == std::vector<std::uint64_t>{1, 2, 3});
    const auto entries = fixture.drain();
    CHECK(std::ranges::none_of(entries, [](const auto& entry) {
        return entry.severity == notification_severity_t::warning || entry.severity == notification_severity_t::error;
    }));
}

TEST_CASE("Lane draw chords admit their own note commits within one frame", "[unit][sys][lane-chord]") {
    compose_notification_fixture_t fixture;
    fixture.prepare_lane_chord();
    std::vector<wormedit::ui::compose_input_action_t> inputs;
    SECTION("grouped press and release") {
        for (std::uint64_t lane = 1; lane <= 3; ++lane)
            inputs.push_back({.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {lane}});
        for (std::uint64_t lane = 3; lane != 0; --lane)
            inputs.push_back({.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {lane}});
    }
    SECTION("interleaved taps") {
        for (std::uint64_t lane = 1; lane <= 3; ++lane) {
            inputs.push_back({.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {lane}});
            inputs.push_back({.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {lane}});
        }
    }
    (void)fixture.batch(inputs);
    check_lane_chord(fixture);
    CHECK_FALSE(fixture.state.is_pending());
}

TEST_CASE("Held lane chords commit all releases and retain hitsound defaults", "[unit][sys][lane-chord]") {
    compose_notification_fixture_t fixture;
    fixture.prepare_lane_chord();
    const std::array presses{
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {1}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {2}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {3}}};
    (void)fixture.batch(presses);
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        move_edit_cursor_action_t{interaction_direction_t::forward}));
    const auto end = fixture.state.cursor;
    const std::array releases{
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {3}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {1}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {2}}};
    (void)fixture.batch(releases);
    check_lane_chord(fixture);
    CHECK_FALSE(fixture.state.is_pending());
    for (const auto& note : fixture.history.document.charts.front().measures.front().notes)
        CHECK(note.end == end);
}

TEST_CASE("Lane keys follow Draw or Select after placing a selected note", "[unit][sys][lane-chord]") {
    compose_notification_fixture_t fixture;
    fixture.prepare_lane_chord();
    const auto placed = fixture.place_selected_tap();
    const auto draw = GENERATE(false, true);
    fixture.application.project.compose_gesture.note_paint_mode_active = draw;
    const std::array inputs{
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {2}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {2}}};
    (void)fixture.batch(inputs);
    const auto& chart = fixture.history.document.charts.front();
    REQUIRE(chart.measures.front().notes.size() == (draw ? 2 : 1));
    const auto* original = chart.find_note(placed);
    REQUIRE(original);
    CHECK(original->lane == lane_id_t{draw ? 1u : 2u});
}

TEST_CASE("Lane chord continuation still rejects an external revision during feedback", "[unit][sys][lane-chord]") {
    compose_notification_fixture_t fixture{compose_notification_fixture_t::feedback_time};
    fixture.prepare_lane_chord();
    fixture.feedback_probe = &fixture;
    fixture.advance_revision_on_feedback = true;
    const std::array inputs{
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {1}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {1}},
        wormedit::ui::compose_input_action_t{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {2}}};
    (void)fixture.batch(inputs);
    CHECK(fixture.history.document.charts.front().measures.front().notes.size() == 1);
    CHECK_FALSE(fixture.state.is_pending());
    CHECK(std::ranges::any_of(fixture.drain(), [](const auto& entry) {
        return entry.severity == notification_severity_t::warning;
    }));
}

void expect_entry(const notification_entry_t& entry, notification_severity_t severity,
                  notification_source_t source, std::string_view message) {
    CHECK(entry.severity == severity);
    CHECK(entry.source == source);
    CHECK(entry.message == message);
}

auto dispatch_stale(compose_notification_fixture_t& fixture, std::uint64_t revision)
    -> std::string {
    wormedit::ui::workspace_action_t stale{};
    stale.compose_input_chart_id.value = fixture.state.chart.value;
    stale.compose_input_document_revision = revision - 1;
    stale.compose_input_viewport_width_pixels = 800;
    return dispatch_compose_input_action(stale, fixture.history, fixture.workspace,
        fixture.state, fixture.viewport, fixture.navigation, fixture.application,
        fixture.cache, 0, &fixture.sink);
}

void check_legacy_seek(compose_notification_fixture_t& fixture) {
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 120.0;
    fixture.application.services.transport_seek = [](std::uint64_t) {
        return compose_action_outcome_t::from(std::string{"Legacy seek performed."},
                                              notification_severity_t::hint,
                                              notification_source_t::input);
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
    fixture.application.services.transport_seek = [&](std::uint64_t) {
        return compose_action_outcome_t::from(
            std::string{"Audio seek was clamped."}, notification_severity_t::warning,
            notification_source_t::audio, compose_feedback_delivery_t::notification);
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
                 "Compose input ignored because the project, chart or document revision changed.");
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
    REQUIRE(result.value);
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
    wormedit::ui::workspace_action_t idle{};
    idle.compose_input_chart_id.value = fixture.state.chart.value;
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
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 120.0;
    fixture.application.services.transport_seek = [](std::uint64_t) {
        return compose_action_outcome_t::from(std::string{"Legacy wheel seek."},
                                              notification_severity_t::hint,
                                              notification_source_t::input);
    };
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = fixture.state.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_viewport_inputs.resize(1);
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
    fixture.application.services.transport_seek = [&](std::uint64_t) {
        return compose_action_outcome_t::from(
            std::string{"Producer wheel seek clamped."}, notification_severity_t::warning,
            notification_source_t::audio, compose_feedback_delivery_t::notification);
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
        fixture.application, fixture.sink).value);
    fixture.state = *fixture.optional_state;
    const auto revision = fixture.history.document.revision;
    const auto undo_size = fixture.history.undo.size();
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = fixture.state.chart.value;
    action.compose_input_document_revision = revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_inputs.resize(2);
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
    fixture.state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{45}}};
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        begin_note_move_gesture_action_t{object_id_t{45}}));
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = fixture.state.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_inputs.resize(2);
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE;
    action.compose_inputs[0].object_id.value = 45;
    action.compose_inputs[0].lane_id.value = 4;
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

namespace {
void check_mixed_input(bool value) { CHECK(value); }
void setup_mixed_bgm_input(compose_notification_fixture_t& fixture) {
    auto& chart = fixture.history.document.charts.front();
    chart.lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::arrow_up;
    chart.bgm_source_lanes = {{.id = object_id_t{4}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{45},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{4}}};
    fixture.state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, object_id_t{45}});
    fixture.state.viewport_height_pixels = 400;
    fixture.state.pixels_per_standard_measure = 240;
    fixture.state.viewport_scroll_pixels = 0;
}
auto mixed_bgm_input(compose_notification_fixture_t& fixture, WormUiComposeInputKind kind, double y, std::uint64_t lane = 4)
    -> std::string {
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = fixture.state.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = 800;
    action.compose_inputs.resize(1);
    action.compose_inputs[0].kind = kind;
    action.compose_inputs[0].y_pixels = y;
    action.compose_inputs[0].lane_id.value = lane;
    action.compose_inputs[0].object_id.value = 45;
    return dispatch_compose_input_action(action, fixture.history, fixture.workspace, fixture.state,
        fixture.viewport, fixture.navigation, fixture.application, fixture.cache, 0, &fixture.sink);
}
}

TEST_CASE("BGM drag dispatch moves a mixed selection through the shared core gesture",
          "[unit][sys][compose-notification][mixed-move]") {
    compose_notification_fixture_t fixture;
    setup_mixed_bgm_input(fixture);
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 60);
    REQUIRE(fixture.state.gesture<wormedit::pending_note_move_gesture_t>());
    const auto updated = mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE, 120);
    INFO(updated);
    REQUIRE(fixture.state.gesture<wormedit::pending_note_move_gesture_t>());
    check_mixed_input(fixture.state.gesture<wormedit::pending_note_move_gesture_t>()->bgm_preview.front().position == local_position_t{measure_id_t{1}, 480});
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, 120);
    const auto& measure = fixture.history.document.charts.front().measures.front();
    check_mixed_input(measure.notes.front().position == local_position_t{measure_id_t{1}, 360});
    check_mixed_input(measure.bgm_events.front().position == local_position_t{measure_id_t{1}, 480});
    check_mixed_input(fixture.history.undo.size() == 1 && !fixture.state.gesture<wormedit::pending_note_move_gesture_t>());
    check_mixed_input(fixture.state.selected_objects.size() == 2);
}

TEST_CASE("BGM drag dispatch cancels or rejects the whole mixed selection",
          "[unit][sys][compose-notification][mixed-move]") {
    compose_notification_fixture_t fixture;
    setup_mixed_bgm_input(fixture);
    fixture.history.document.charts.front().measures.front().notes.push_back(
        {.id = object_id_t{2}, .position = {measure_id_t{1}, 360}, .lane = lane_id_t{1}});
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 60);
    REQUIRE(fixture.state.gesture<wormedit::pending_note_move_gesture_t>());
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL, 60);
    check_mixed_input(!fixture.state.gesture<wormedit::pending_note_move_gesture_t>() && fixture.history.undo.empty());
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 60);
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, 120);
    check_mixed_input(!fixture.state.gesture<wormedit::pending_note_move_gesture_t>() && fixture.history.undo.empty());
    check_mixed_input(fixture.history.document.charts.front().measures.front().notes.front().position == local_position_t{measure_id_t{1}, 120});
}

TEST_CASE("BGM drag Y-lock holds the origin even outside authored chart rows",
          "[unit][sys][compose-notification][mixed-move][y-lock]") {
    compose_notification_fixture_t fixture;
    setup_mixed_bgm_input(fixture);
    fixture.state.y_lock = true;
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 60);
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, -10000);
    check_mixed_input(!fixture.state.gesture<wormedit::pending_note_move_gesture_t>() && fixture.history.undo.empty());
    check_mixed_input(fixture.history.document.charts.front().measures.front().bgm_events.front().position == local_position_t{measure_id_t{1}, 240});
    fixture.state.selected_notes.clear();
    fixture.state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{45}}};
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 60);
    REQUIRE(fixture.state.gesture<wormedit::pending_note_move_gesture_t>());
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, -10000);
    check_mixed_input(fixture.history.undo.empty());
    check_mixed_input(fixture.history.document.charts.front().measures.front().bgm_events.front().position == local_position_t{measure_id_t{1}, 240});
}

TEST_CASE("Single BGM dispatcher routes horizontal and virtual targets through the shared gesture",
          "[unit][sys][compose-notification][single-bgm-move]") {
    const auto virtual_target = GENERATE(false, true);
    compose_notification_fixture_t fixture;
    setup_mixed_bgm_input(fixture);
    auto& chart = fixture.history.document.charts.front();
    chart.source_family = format_family_t::bms;
    chart.bgm_source_lanes.push_back({.id = object_id_t{5}, .source_order = 1});
    fixture.state.selected_notes.clear();
    fixture.state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{45}}};
    fixture.state.y_lock = !virtual_target;
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 60);
    REQUIRE(fixture.state.gesture<wormedit::pending_note_move_gesture_t>());
    // 240 px per 960 rows: y=300 is row 1200, in the first virtual measure.
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE, 300, 5);
    REQUIRE(!fixture.state.gesture<wormedit::pending_note_move_gesture_t>()->bgm_preview.empty());
    check_mixed_input(fixture.state.gesture<wormedit::pending_note_move_gesture_t>()->bgm_preview.front().lane == object_id_t{5});
    check_mixed_input(fixture.history.undo.empty() && chart.measures.size() == 1);
    (void)mixed_bgm_input(fixture, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, 300, 5);
    check_mixed_input(!fixture.state.gesture<wormedit::pending_note_move_gesture_t>() && fixture.history.undo.size() == 1);
    const auto& committed = fixture.history.document.charts.front();
    const auto& moved = committed.measures[virtual_target ? 1 : 0].bgm_events.front();
    check_mixed_input(moved.source_lane == object_id_t{5});
    check_mixed_input(moved.position.row == 240);
    check_mixed_input(committed.measures.size() == (virtual_target ? 2 : 1));
    REQUIRE(undo_document_command(fixture.history));
    check_mixed_input(fixture.history.document.charts.front().measures.size() == 1);
    check_mixed_input(fixture.history.document.charts.front().measures.front().bgm_events.front().source_lane == object_id_t{4});
}

TEST_CASE("Delete dispatcher commits stem conversion and ordinary deletion as one shared command",
          "[unit][sys][compose-notification][mixed-delete][bmson]") {
    compose_notification_fixture_t f;
    f.history.document.audio_assets = {{.id = asset_id_t{5}, .stream = {48000, 2, 480000, true}}};
    f.history.document.resources = {{.id = resource_id_t{6}, .asset = asset_id_t{5}, .roles = {resource_role_t::sample}}};
    auto& chart = f.history.document.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.bgm_source_lanes = {{.id = object_id_t{22}, .stem_channel = object_id_t{30}}};
    auto& note = chart.measures.front().notes.front();
    note.sound = resource_id_t{6};
    note.sound_channel_event = object_id_t{31};
    chart.sound_channels = {{.id = object_id_t{30}, .name = "Stem",
        .source_clip = {.id = clip_id_t{32}, .source = asset_id_t{5}, .end_frame = 480000},
        .events = {{.id = object_id_t{31}, .position = note.position, .lane = note.lane}}}};
    chart.measures.front().notes.push_back({.id = object_id_t{2}, .position = {measure_id_t{1}, 360}, .lane = lane_id_t{1}});
    f.state.selected_notes.push_back(object_id_t{2});
    f.state.selected_objects.push_back({editor_selection_object_kind_t::note, object_id_t{2}});
    REQUIRE(validate_document(f.history.document).empty());
    f.optional_state = f.state;
    const auto result = invoke_application_command({.kind = application_command_kind_t::edit_delete},
        f.history, f.workspace, f.optional_state, f.timing_stage, f.application, f.sink);
    check_mixed_input(result.value);
    const auto& deleted = f.history.document.charts.front();
    check_mixed_input(deleted.measures.front().notes.empty());
    REQUIRE(deleted.measures.front().bgm_events.size() == 1);
    check_mixed_input(deleted.measures.front().bgm_events.front().sound_channel_event == object_id_t{31});
    check_mixed_input(!deleted.sound_channels.front().events.front().playable);
    check_mixed_input(f.history.undo.size() == 1);
    REQUIRE(undo_document_command(f.history));
    check_mixed_input(f.history.document.charts.front().measures.front().notes.size() == 2);
}

namespace {
// One Stop at row 480 and a Warp over rows [720, 840) in a one-lane chart with a single note.
struct dead_span_fixture_t : compose_notification_fixture_t {
    std::uint64_t before_revision{};
    std::size_t before_notes{};

    dead_span_fixture_t() {
        auto& chart = history.document.charts.front();
        chart.pauses = {{.id = object_id_t{20}, .position = {measure_id_t{1}, 480},
                         .kind = pause_kind_t::stop, .duration = {2, 1},
                         .source_unit = pause_unit_t::seconds}};
        chart.warps = {{.id = object_id_t{21}, .span = {{measure_id_t{1}, 720}, {measure_id_t{1}, 840}}}};
        REQUIRE(validate_document(history.document).empty());
        history = document_history_t::from(history.document);
        const auto measures = measure_index_t::from(history.document.charts.front());
        REQUIRE(measures.has_value());
        const auto timing = timing_index_t::from(history.document.charts.front());
        REQUIRE(timing.value);
        cache.timing.dead_spans = dead_spans_view_t::from(history.document.charts.front(),
            measures.value(), *timing.value, 64);
        state.viewport_height_pixels = 400;
        state.pixels_per_standard_measure = 480;
        state.viewport_scroll_pixels = 0;
        before_revision = history.document.revision;
        before_notes = history.document.charts.front().measures.front().notes.size();
    }
    // Screen y at the middle of the Stop band.
    auto stop_band_y() const -> double {
        const auto& chart = history.document.charts.front();
        const auto bands = build_dead_span_bands_view(*cache.timing.dead_spans,
            state.pixels_per_standard_measure / 960.0, state.viewport_scroll_pixels,
            state.viewport_height_pixels,
            compose_field_downscroll(chart, application.preferences.editor_preferences),
            dead_span_pause_band_pixels);
        for (const auto& band : bands)
            if (band.kind == dead_span_kind_t::stop) return (band.top + band.bottom) / 2.0;
        FAIL("no stop band");
        return 0;
    }
    // An x inside the single playable lane, whatever the timing lanes reserve on the left.
    auto lane_x() const -> double {
        const auto layout = compose_horizontal_layout(
            800, 1, false, 0, effective_compose_timing_lane_mask(application));
        return layout.field_left_pixels + layout.playable_field_width_pixels / 2.0;
    }
    auto run(std::initializer_list<wormedit::ui::compose_input_action_t> inputs) -> std::string {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_chart_id.value = state.chart.value;
        action.compose_input_document_revision = history.document.revision;
        action.compose_input_viewport_width_pixels = 800;
        for (const auto& input : inputs) action.compose_inputs.emplace_back() = input;
        return dispatch_compose_input_action(action, history, workspace, state, viewport,
            navigation, application, cache, 0, &sink);
    }
    void check_document_unchanged() {
        CHECK(history.document.revision == before_revision);
        CHECK(history.undo.empty());
        CHECK(history.document.charts.front().measures.front().notes.size() == before_notes);
    }
};
} // namespace

TEST_CASE("pasting inside a visual Stop band uses its valid chart row",
          "[unit][sys][compose-notification][dead-span]") {
    dead_span_fixture_t fixture;
    fixture.state.selected_notes = {object_id_t{1}};
    auto copied = chart_clipboard_t::from(fixture.history.document, fixture.state,
                                       fixture.history.document.revision);
    REQUIRE(copied);
    fixture.application.project.clipboard.chart_clipboard = std::move(*copied);
    std::string status;
    REQUIRE(begin_compose_paste(fixture.application, fixture.history, fixture.state, status));
    const auto y = fixture.stop_band_y();
    (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_PASTE_UPDATE, .x_pixels = fixture.lane_x(), .y_pixels = y},
                       {.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT}});
    CHECK(fixture.history.document.revision == fixture.before_revision + 1);
    CHECK(fixture.history.undo.size() == 1);
    CHECK(fixture.history.document.charts.front().measures.front().notes.size() == 2);
}

TEST_CASE("dropping a dragged note inside a visual Stop band uses its valid chart row",
          "[unit][sys][compose-notification][dead-span]") {
    dead_span_fixture_t fixture;
    fixture.state.selected_notes = {object_id_t{1}};
    fixture.state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}}};
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
                                   begin_note_move_gesture_action_t{object_id_t{1}}));
    const auto y = fixture.stop_band_y();
    (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, .x_pixels = fixture.lane_x(), .y_pixels = y},
                       {.kind = WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, .x_pixels = fixture.lane_x(), .y_pixels = y}});
    CHECK(fixture.history.document.revision == fixture.before_revision + 1);
    CHECK(fixture.history.undo.size() == 1);
    CHECK(fixture.history.document.charts.front().measures.front().notes.front().position ==
          local_position_t{measure_id_t{1}, 480});
}

TEST_CASE("a lane key at a cursor inside a Warp places nothing",
          "[unit][sys][compose-notification][dead-span]") {
    dead_span_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    if (GENERATE(false, true)) {
        fixture.state.selected_notes.clear();
        fixture.state.selected_objects.clear();
    }
    fixture.state.cursor = {measure_id_t{1}, 780};
    (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {1}},
                       {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {1}}});
    fixture.check_document_unchanged();
}

namespace {
// A pointer 120 px away from the Stop band on its earlier-row side (row 240, free of dead spans).
auto free_row_y(const dead_span_fixture_t& fixture) -> double {
    const auto downscroll = compose_field_downscroll(fixture.history.document.charts.front(),
                                                    fixture.application.preferences.editor_preferences);
    return fixture.stop_band_y() + (downscroll ? 120.0 : -120.0);
}
} // namespace

TEST_CASE("the same paste and drag succeed away from the Stop band",
          "[unit][sys][compose-notification][dead-span]") {
    SECTION("paste") {
        dead_span_fixture_t fixture;
        fixture.state.selected_notes = {object_id_t{1}};
        auto copied = chart_clipboard_t::from(fixture.history.document, fixture.state,
                                           fixture.history.document.revision);
        REQUIRE(copied);
        fixture.application.project.clipboard.chart_clipboard = std::move(*copied);
        std::string status;
        REQUIRE(begin_compose_paste(fixture.application, fixture.history, fixture.state, status));
        (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_PASTE_UPDATE, .x_pixels = fixture.lane_x(), .y_pixels = free_row_y(fixture)},
                           {.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT}});
        CHECK(fixture.history.document.charts.front().measures.front().notes.size() == 2);
    }
    SECTION("drag") {
        dead_span_fixture_t fixture;
        fixture.state.selected_notes = {object_id_t{1}};
        fixture.state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}}};
        REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
                                       begin_note_move_gesture_action_t{object_id_t{1}}));
        const auto y = free_row_y(fixture);
        (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, .x_pixels = fixture.lane_x(), .y_pixels = y},
                           {.kind = WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, .x_pixels = fixture.lane_x(), .y_pixels = y}});
        CHECK(fixture.history.document.revision != fixture.before_revision);
    }
}

TEST_CASE("a lane key at a cursor just before the Warp places a note",
          "[unit][sys][compose-notification][dead-span]") {
    dead_span_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.state.selected_notes.clear();
    fixture.state.selected_objects.clear();
    fixture.state.cursor = {measure_id_t{1}, 600};
    (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {1}},
                       {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {1}}});
    CHECK(fixture.history.document.charts.front().measures.front().notes.size() == 2);
}

TEST_CASE("pasting a note at a cursor inside a Warp is rejected and changes nothing",
          "[unit][sys][compose-notification][dead-span]") {
    dead_span_fixture_t fixture;
    fixture.state.selected_notes = {object_id_t{1}};
    auto copied = chart_clipboard_t::from(fixture.history.document, fixture.state,
                                       fixture.history.document.revision);
    REQUIRE(copied);
    fixture.application.project.clipboard.chart_clipboard = std::move(*copied);
    fixture.state.cursor = {measure_id_t{1}, 780};
    std::string status;
    REQUIRE(begin_compose_paste(fixture.application, fixture.history, fixture.state, status));
    (void)fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT}});
    fixture.check_document_unchanged();
}

TEST_CASE("Compose input mutation rebind failures preserve both feedback entries",
          "[compose-notification][compose-outcome]") {
    compose_notification_fixture_t fixture;
    fixture.workspace.limits.maximum_charts = 0;
    const auto status = fixture.input(WORM_UI_COMPOSE_INPUT_DELETE_SELECTION);
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 2);
    check_mixed_input(status == entries.front().message);
    expect_entry(entries.front(), notification_severity_t::error, notification_source_t::document,
                 status);
    check_mixed_input(entries.back().severity == notification_severity_t::success);
    check_mixed_input(entries.back().source == notification_source_t::edit);
    check_mixed_input(fixture.history.undo.size() == 1);
    check_mixed_input(fixture.workspace.document_revision != fixture.history.document.revision);
}

TEST_CASE("Compose input history rebind failures preserve one document error",
          "[compose-notification][compose-outcome]") {
    compose_notification_fixture_t fixture;
    (void)fixture.input(WORM_UI_COMPOSE_INPUT_DELETE_SELECTION);
    (void)fixture.drain();
    fixture.center.dismiss_all();
    fixture.workspace.limits.maximum_charts = 0;
    const auto status = fixture.input(WORM_UI_COMPOSE_INPUT_UNDO);
    const auto entries = fixture.drain();
    REQUIRE(entries.size() == 1);
    expect_entry(entries.front(), notification_severity_t::error, notification_source_t::document,
                 status);
    check_mixed_input(fixture.history.undo.empty());
    check_mixed_input(fixture.history.redo.size() == 1);
    check_mixed_input(fixture.history.document.charts.front().measures.front().notes.size() == 1);
}

TEST_CASE("Compose paste keeps its original status after posting a rebind error",
          "[compose-notification][compose-outcome]") {
    dead_span_fixture_t fixture;
    fixture.state.selected_notes = {object_id_t{1}};
    auto copied = chart_clipboard_t::from(fixture.history.document, fixture.state,
                                          fixture.history.document.revision);
    REQUIRE(copied);
    fixture.application.project.clipboard.chart_clipboard = std::move(*copied);
    std::string status;
    REQUIRE(
        begin_compose_paste(fixture.application, fixture.history, fixture.state, status));
    fixture.workspace.limits.maximum_charts = 0;
    const auto returned = fixture.run({{.kind = WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
                                        .x_pixels = fixture.lane_x(),
                                        .y_pixels = fixture.stop_band_y()},
                                       {.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT}});
    fixture.center.drain_inbox(notification_time_t{});
    const auto entries = fixture.center.snapshot(notification_capacity);
    REQUIRE(entries.size() == 2);
    check_mixed_input(returned == "Selection pasted.");
    expect_entry(entries.back(), notification_severity_t::success, notification_source_t::edit,
                 returned);
    check_mixed_input(entries.front().severity == notification_severity_t::error);
    check_mixed_input(entries.front().source == notification_source_t::document);
    check_mixed_input(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose input binds the workspace before publishing mutation feedback",
          "[compose-notification][compose-outcome][compose-publication-order]") {
    compose_notification_fixture_t fixture{compose_notification_fixture_t::feedback_time};
    compose_notification_fixture_t::feedback_probe = &fixture;
    (void)fixture.input(WORM_UI_COMPOSE_INPUT_DELETE_SELECTION);
    check_mixed_input(fixture.feedback_post_count == 1);
    check_mixed_input(fixture.is_workspace_current_when_feedback_posted);
    check_mixed_input(fixture.workspace.document_revision == fixture.history.document.revision);
    check_mixed_input(fixture.history.undo.size() == 1);
}

namespace {
struct bms_editor_paste_fixture_t {
    document_history_t history;
    editor_interaction_state_t state;
    application_command_state_t application;
    std::string system_clipboard;

    bms_editor_paste_fixture_t() {
        const std::string_view source = "#PLAYER 1\r\n#BPM 120\r\n#WAV01 a.ogg\r\n#00111:01\r\n#00116:01\r\n#00118:01\r\n#00119:01\r\n";
        auto parsed = parse_bms_bytes(std::as_bytes(std::span{source}),
            {.file_extension = ".bme", .lane_profile_id = "bme-7k-outside-scratch"});
        REQUIRE(parsed.value.song);
        history = document_history_t::from(std::move(*parsed.value.song));
        state.chart = (*history.document.read_charts().front()).id;
        application.services.system_clipboard_text = [this] { return system_clipboard; };
    }
    auto paste(std::string& status) -> bool {
        const auto pasted = begin_compose_paste(application, history, state, status);
        UNSCOPED_INFO(status);
        return pasted;
    }
    auto pasted_object_count() const -> std::size_t {
        return application.project.clipboard.paste_session->clipboard.objects.size();
    }
    void set_bgm_columns(unsigned count) {
        auto chart = *history.document.read_charts().front();
        chart.bgm_source_lanes.clear();
        for (unsigned index = 0; index < count; ++index)
            chart.bgm_source_lanes.push_back({.id = object_id_t{1000 + index},
                .name = "Existing", .source_order = index});
        if (count) chart.measures.front().bgm_events.push_back({.id = object_id_t{2000},
            .position = {chart.measures.front().id, 24},
            .source_lane = chart.bgm_source_lanes.front().id});
        history = document_history_t::from(test::native_bms_fixture(history.document, std::move(chart)));
        state.cursor = {(*history.document.read_charts().front()).measures.front().id, 0};
    }
};

constexpr std::string_view ibmsc_one_note = "iBMSC Clipboard Data\r\n6 0 10000 0 0 0";

auto paste_document_content(song_document_t song) -> std::string {
    song.revision = 0;
    const auto serialized = write_native_project(song);
    REQUIRE(serialized.value);
    return *serialized.value;
}

auto check_bgm_paste_preview(const bms_editor_paste_fixture_t& fixture)
    -> std::vector<bgm_source_lane_t> {
    const auto& session = *fixture.application.project.clipboard.paste_session;
    REQUIRE(session.proposal.value);
    CHECK(session.geometry.bgm_source_lanes.size() == 3);
    const auto lane = session.geometry.bgm_source_lanes.back().id;
    CHECK(session.geometry.measures.front().bgm_events.front().source_lane == lane);
    return session.geometry.bgm_source_lanes;
}

void check_retained_bgm_events(const song_document_t& before, const chart_document_t& after) {
    const auto& events = before.read_charts().front()->measures.front().bgm_events;
    REQUIRE(after.measures.front().bgm_events.size() == events.size() + 1);
    for (const auto& event : events) {
        const auto* retained = after.find_bgm_event(event.id);
        CHECK((retained && *retained == event));
    }
}

void check_bgm_paste_commit(bms_editor_paste_fixture_t& fixture,
                            const std::vector<bgm_source_lane_t>& expected_lanes) {
    const auto original = fixture.history.document.read_charts().front()->bgm_source_lanes;
    const auto maximum = id_ceiling_t<object_id_t>::from(fixture.history.document).maximum;
    std::string status;
    const auto applied = apply_compose_paste(fixture.application, fixture.history, fixture.state, status);
    INFO(status);
    REQUIRE(applied);
    const auto& after = (*fixture.history.document.read_charts().front());
    check_mixed_input(after.bgm_source_lanes.size() == expected_lanes.size());
    for (std::size_t index = 0; index < original.size(); ++index)
        check_mixed_input(after.bgm_source_lanes[index] == original[index]);
    for (auto index = original.size(); index < after.bgm_source_lanes.size(); ++index) {
        check_mixed_input(after.bgm_source_lanes[index].id.value > maximum);
        check_mixed_input(after.bgm_source_lanes[index].name == expected_lanes[index].name);
        check_mixed_input(after.bgm_source_lanes[index].source_order == expected_lanes[index].source_order);
    }
    CHECK(std::ranges::any_of(after.measures.front().bgm_events, [&](const auto& event) {
        return event.source_lane == after.bgm_source_lanes.back().id && event.position.row == 0;
    }));
}
} // namespace

TEST_CASE("Paste decodes iBMSC text from the system clipboard",
          "[unit][sys][compose-notification][bms-clipboard]") {
    bms_editor_paste_fixture_t fixture;
    fixture.system_clipboard = ibmsc_one_note;
    std::string status;
    REQUIRE(fixture.paste(status));
    CHECK(fixture.pasted_object_count() == 1);
    CHECK(status.starts_with("Pasting BMS editor data"));
}

TEST_CASE("External BGM paste previews column growth and commits it with the events",
          "[unit][sys][compose-notification][bms-clipboard]") {
    bms_editor_paste_fixture_t fixture;
    const auto initial_count = GENERATE(0U, 1U, 3U);
    fixture.set_bgm_columns(initial_count);
    const auto before_document = fixture.history.document;
    const auto before_content = paste_document_content(fixture.history.document);
    fixture.system_clipboard = GENERATE("iBMSC Clipboard Data\r\n29 0 10000 0 0 0",
        "BMSE ClipBoard Object Data Format\r\n103000000001\r\n");
    std::string status;
    REQUIRE(fixture.paste(status));
    const auto expected_lanes = check_bgm_paste_preview(fixture);
    CHECK(paste_document_content(fixture.history.document) == before_content);
    check_bgm_paste_commit(fixture, expected_lanes);
    const auto& after = (*fixture.history.document.read_charts().front());
    check_retained_bgm_events(before_document, after);
    CHECK(fixture.history.undo.size() == 1);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(paste_document_content(fixture.history.document) == before_content);
}

TEST_CASE("Paste keeps wormedit's own copy until the system clipboard changes",
          "[unit][sys][compose-notification][bms-clipboard]") {
    bms_editor_paste_fixture_t fixture;
    fixture.system_clipboard = ibmsc_one_note;
    fixture.application.project.clipboard.chart_clipboard = chart_clipboard_t{};
    fixture.application.project.clipboard.system_text_at_copy = std::string{ibmsc_one_note};
    std::string status;
    REQUIRE(fixture.paste(status));
    CHECK(fixture.pasted_object_count() == 0);

    fixture.system_clipboard = std::string{ibmsc_one_note} + "\r\n6 48 10000 0 0 0";
    REQUIRE(fixture.paste(status));
    CHECK(fixture.pasted_object_count() == 2);
}

TEST_CASE("Rejected external paste does not publish proposed BGM columns",
          "[unit][sys][compose-notification][bms-clipboard]") {
    bms_editor_paste_fixture_t fixture;
    fixture.set_bgm_columns(0);
    const auto& chart = *fixture.history.document.read_charts().front();
    fixture.state.cursor = chart.measures[1].notes.front().position;
    fixture.history = document_history_t::from(fixture.history.document);
    const auto before = paste_document_content(fixture.history.document);
    fixture.system_clipboard = "iBMSC Clipboard Data\r\n6 0 10000 0 0 0\r\n29 0 10000 0 0 0";
    std::string status;
    REQUIRE(fixture.paste(status));
    REQUIRE_FALSE(fixture.application.project.clipboard.paste_session->proposal.value);
    CHECK_FALSE(apply_compose_paste(fixture.application, fixture.history, fixture.state, status));
    CHECK(paste_document_content(fixture.history.document) == before);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Paste ignores unrelated system clipboard text",
          "[unit][sys][compose-notification][bms-clipboard]") {
    bms_editor_paste_fixture_t fixture;
    fixture.system_clipboard = "some notes from a text editor";
    std::string status;
    CHECK_FALSE(fixture.paste(status));
    CHECK(status == "Paste requires a copied selection and no pending gesture.");
}

TEST_CASE("Paste reports BMS editor text it cannot decode without pasting",
          "[unit][sys][compose-notification][bms-clipboard]") {
    bms_editor_paste_fixture_t fixture;
    fixture.system_clipboard = "iBMSC Clipboard Data\r\n6 0 990000 0 0 0";
    std::string status;
    CHECK_FALSE(fixture.paste(status));
    CHECK(status.starts_with("BMS editor clipboard was not pasted: "));
    CHECK_FALSE(fixture.application.project.clipboard.paste_session);
}
