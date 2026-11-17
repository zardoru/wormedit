// Paint ghosts, lane profile switching and lane-key callback dispatch.
#include "frame_timing_fixtures.h"
#include "compose_window_overloads.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void check_click_audition_preference(bool enabled) {
    compose_test_fixture_t fixture;
    fixture.application.preferences.editor_preferences.audition_on_click = enabled;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.note_endpoints.size() > 0);
    const auto& bounds = fixture.flattened.view.compose.note_endpoints[0].bounds;
    wormedit::ui::workspace_action_t action{};
    action.input.shortcuts.window_focused = true;
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .x_pixels = (bounds.left + bounds.right) / 2,
        .y_pixels = (bounds.top + bounds.bottom) / 2};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    auto notifications = notification_sink_t::muted();
    const compose_input_inputs_t environment{
        .document = {.history = fixture.history, .workspace = *fixture.binding.value,
                     .notifications = notifications, .auditions = &fixture.application.project.auditions},
        .commands = fixture.application, .controls = {fixture.cache, viewport, navigation}};
    (void)dispatch_compose_input_action(action, environment, *fixture.interaction, 0.016, false);
    expect_compose_test(!fixture.interaction->selected_notes.empty());
    expect_compose_test(fixture.application.project.auditions.has_pending_request() == enabled);
    expect_compose_test(fixture.history.undo.empty());
}
} // namespace

TEST_CASE("Note click audition can be disabled without suppressing selection",
          "[sys][compose][audition][regression]") {
    check_click_audition_preference(true);
    check_click_audition_preference(false);
}

TEST_CASE("Idle paint ghost follows hover without rebuilding the retained scene", "[sys][paint-ghost]") {
    compose_test_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto lane = view.lanes[0];
    const auto x = (lane.bounds.left + lane.bounds.right) * .5;
    const auto source_y = view.pixels_per_standard_measure * .375 - fixture.interaction->viewport_scroll_pixels;
    const auto y = view.downscroll ? view.viewport_height_pixels - source_y : source_y;
    fixture.cache.frame.paint_pointer = std::pair{x, y};
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.paint_ghost_visible);
    const auto builds = fixture.cache.scene.static_source.key()->rebuild_count;
    fixture.cache.frame.paint_pointer = std::pair{x, y + 30};
    fixture.project();
    CHECK(fixture.flattened.view.compose.paint_ghost_visible);
    CHECK(fixture.cache.scene.static_source.key()->rebuild_count == builds);
    CHECK(fixture.history.undo.empty());
    const auto endpoint = fixture.flattened.view.compose.note_endpoints[0];
    fixture.cache.frame.paint_pointer = std::pair{(endpoint.bounds.left + endpoint.bounds.right) * .5,
                                           (endpoint.bounds.top + endpoint.bounds.bottom) * .5};
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paint_ghost_visible);
    const auto ghost = fixture.flattened.view.compose.paint_ghost;
    expect_compose_test(ghost.paint_erase_preview);
    expect_compose_test(std::abs(ghost.bounds.top - endpoint.bounds.top) < 1e-9);
    expect_compose_test(std::abs(ghost.bounds.bottom - endpoint.bounds.bottom) < 1e-9);
    fixture.interaction->armed_note_kind = note_kind_t::hold;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paint_ghost.kind == note_kind_t::hold);
    expect_compose_test(fixture.flattened.view.compose.hold_bodies.size() == 0);
    fixture.cache.frame.paint_pointer.reset();
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.paint_ghost_visible);
    fixture.cache.frame.paint_pointer = std::pair{x, y};
    fixture.application.project.compose_gesture.note_paint_mode_active = false;
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.paint_ghost_visible);
}

TEST_CASE("Idle paint ghosts preview placement throughout a blank project's virtual tail",
          "[sys][paint-ghost][regression]") {
    const auto family = GENERATE(format_family_t::native_project, format_family_t::bms);
    const auto downscroll = GENERATE(false, true);
    const auto snap_enabled = GENERATE(false, true);
    const auto measure_number = GENERATE(1U, 3U, 20U);
    auto song = dense_song(1, 0);
    auto& chart = song.charts.front();
    chart.source_family = family;
    chart.virtual_tail_measures = native_unanchored_virtual_measure_count;
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.application.preferences.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    fixture.project();
    fixture.application.project.compose_view.edit_snap_enabled = snap_enabled;
    fixture.interaction->active_snap_denominator = 4;
    fixture.interaction->viewport_scroll_pixels =
        measure_number * fixture.interaction->pixels_per_standard_measure;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto lane = view.lanes.front();
    const auto x = (lane.bounds.left + lane.bounds.right) / 2;
    const auto source_y = 337.0 / 960 * view.pixels_per_standard_measure;
    const auto y = downscroll ? view.viewport_height_pixels - source_y : source_y;
    const auto revision = fixture.history.document.revision;
    fixture.cache.frame.paint_pointer = std::pair{x, y};
    fixture.project();
    expect_compose_test(view.paint_ghost_visible);
    const auto ghost = view.paint_ghost;
    CAPTURE(family, downscroll, snap_enabled, measure_number, ghost.bounds.top);
    expect_compose_test(!ghost.paint_erase_preview);
    expect_compose_test(fixture.history.document.revision == revision);
    expect_compose_test(fixture.history.read_chart(fixture.interaction->chart)->measures.size() == 1);
    expect_compose_test(fixture.history.undo.empty());

    const auto row = measure_number * 960 + (snap_enabled ? 240 : 337);
    const auto expected_y = (snap_enabled ? 240.0 : 337.0) / 960 * view.pixels_per_standard_measure;
    const auto anchor_y = downscroll ? view.viewport_height_pixels - expected_y : expected_y;
    expect_compose_test(ghost.bounds.top == Catch::Approx(downscroll
        ? anchor_y - fixture.cache.frame.preview_scene_request.note_head_height_pixels : anchor_y));
    expect_compose_test(dispatch_editor_action(fixture.history, *fixture.interaction,
        place_virtual_bms_tap_action_t{.canvas_row = row, .lane = lane.lane_id}).document_changed);
    fixture.project();
    expect_compose_test(view.note_endpoints.size() == 1);
    expect_compose_test(view.note_endpoints.front().bounds.top == Catch::Approx(ghost.bounds.top));
    expect_compose_test(view.note_endpoints.front().bounds.bottom == Catch::Approx(ghost.bounds.bottom));

    fixture.cache.frame.paint_pointer = std::pair{view.playable_field_right_pixels + 1, y};
    fixture.project();
    expect_compose_test(!view.paint_ghost_visible);
}

TEST_CASE("Lane profile switcher uses key roles instead of recognizing the current profile ID",
          "[sys][lane-profile-switch][compatible-layout]") {
    auto scratch_song = compose_test_song();
    scratch_song.charts.front().lane_layouts.front().layout = make_bms_7k_profile().layout;
    scratch_song.charts.front().measures.front().notes.clear();
    scratch_song.charts.front().input_profile = make_bms_7k_profile().input;
    compose_test_fixture_t scratch{std::move(scratch_song)};
    scratch.project();
    expect_compose_test(scratch.flattened.view.compose.lane_profile_names.size() == 0);

    auto song = compose_test_song();
    song.charts.front().lane_layouts.front().layout = make_osu_mania_profile(7)->layout;
    song.charts.front().lane_layouts.front().layout.profile_id = "created-7k";
    song.charts.front().input_profile = make_osu_mania_profile(7)->input;
    song.charts.front().measures.front().notes.clear();
    compose_test_fixture_t created{std::move(song)};
    created.project();
    const auto& view = created.flattened.view.compose;
    expect_compose_test(view.lane_profile_names.size() == 1);
    expect_compose_test(view.lane_profile_current == -1);
    expect_compose_test(std::string_view(view.lane_profile_names[0]) == "osu!mania 7K");
    const auto ids = std::span(view.lane_layout_profile_ids);
    expect_compose_test(std::ranges::any_of(ids, [](const auto& id) {
        return std::string_view(id) == "osu-mania-7k";
    }));
}

TEST_CASE("BGM-only projection offers every initial profile with distinct SpecialStyle labels",
          "[sys][lane-profile-switch][initial-profile-projection]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    chart.lane_layouts.clear();
    chart.measures.front().notes.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{70001}, .name = "Autoplay"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{70002},
        .position = {chart.measures.front().id, 0}, .source_lane = object_id_t{70001}}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.has_bgm_events);
    expect_compose_test(view.lane_profile_names.size() == 41);
    expect_compose_test(view.lane_profile_names.size() < 64);
    expect_compose_test(view.lane_profile_current == -1);
    std::vector<std::string_view> names;
    for (std::size_t index = 0; index < view.lane_profile_names.size(); ++index)
        names.emplace_back(view.lane_profile_names[index]);
    for (const auto label : {"osu!mania 12K", "osu!mania 16K",
                             "osu!mania 12K (SpecialStyle)", "osu!mania 16K (SpecialStyle)"})
        expect_compose_test(std::ranges::find(names, label) != names.end());
}

namespace {
void check_lane_key_callback(bool virtual_measure, bool hold) {
    lane_key_callback_fixture_t f;
    if (virtual_measure) {
        for (int step = 0; step < 8; ++step)
            (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
        expect_compose_test(f.compose.interaction->virtual_bms_cursor_row == 1920);
    }
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_compose_test((f.compose.interaction->hold() != nullptr));
    expect_compose_test(f.compose.interaction->hold()->sound == resource_id_t{7});
    expect_compose_test(f.compose.history.undo.empty());
    expect_compose_test(placed_resource_notifications.empty());
    if (hold) (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
    // A different release payload must never retarget the sound captured on key-down.
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
    const auto notes = f.notes();
    expect_compose_test(notes.size() == 1);
    expect_compose_test(notes.front().sound == resource_id_t{7});
    expect_compose_test(notes.front().semantic_layers == std::vector{layer_id_t{3}});
    expect_compose_test(notes.front().end.has_value() == hold);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
    expect_compose_test(f.compose.history.undo.size() == 1);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_UNDO);
    expect_compose_test(f.notes().empty());
    expect_compose_test(f.compose.history.document.charts.front().measures.size() == 2);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
}
} // namespace

TEST_CASE("Lane-key dispatcher emits one captured-sound callback per authored or virtual tap and hold",
          "[sys][compose][lane-key-sound]") {
    for (const bool virtual_measure : {false, true})
        for (const bool hold : {false, true})
            check_lane_key_callback(virtual_measure, hold);
}

TEST_CASE("Lane-key dispatcher keeps simultaneous held sounds and emits each release once",
          "[sys][compose][lane-key-sound]") {
    lane_key_callback_fixture_t f;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 7);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 2, 8);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
    expect_compose_test(placed_resource_notifications.empty());
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 2, 7);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>({7, 8}));
    const auto notes = f.notes();
    expect_compose_test(notes.size() == 2);
    expect_compose_test(notes[0].lane == lane_id_t{1} && notes[0].sound == resource_id_t{7});
    expect_compose_test(notes[1].lane == lane_id_t{2} && notes[1].sound == resource_id_t{8});
    expect_compose_test(notes[0].end == notes[1].end);
    expect_compose_test(f.compose.history.undo.size() == 2);
}

TEST_CASE("Lane-key dispatcher does not advance for rejection silent notes deletion or selected-note movement",
          "[sys][compose][lane-key-sound]") {
    lane_key_callback_fixture_t f;
    SECTION("missing resource rejects atomically") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 999);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 999);
        expect_compose_test(f.notes().empty());
        expect_compose_test(f.compose.history.undo.empty());
    }
    SECTION("silent insertion") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 0);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 0);
        const auto notes = f.notes();
        expect_compose_test(notes.size() == 1 && !notes.front().sound);
    }
    SECTION("sounded note removal never re-sounds or advances") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
        placed_resource_notifications.clear();
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 0);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 0);
        expect_compose_test(f.notes().empty());
        expect_compose_test(f.compose.history.undo.size() == 2);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_UNDO);
        const auto notes = f.notes();
        expect_compose_test(notes.size() == 1 && notes.front().sound == resource_id_t{7});
    }
    SECTION("Shift lane move preserves the selected sound") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
        placed_resource_notifications.clear();
        const auto note = f.notes().front();
        f.compose.interaction->selected_notes = {note.id};
        f.compose.interaction->selected_objects = {{editor_selection_object_kind_t::note, note.id}};
        f.compose.application.project.compose_gesture.note_paint_mode_active = false;
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 2, 8, true);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 2, 8);
        const auto notes = f.notes();
        expect_compose_test(notes.size() == 1 && notes.front().lane == lane_id_t{2});
        expect_compose_test(notes.front().sound == resource_id_t{7});
    }
    expect_compose_test(placed_resource_notifications.empty());
}

TEST_CASE("Lane-key dispatcher cancels held sound sources without placement callbacks",
          "[sys][compose][lane-key-sound]") {
    lane_key_callback_fixture_t f;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_compose_test((f.compose.interaction->hold() != nullptr));
    SECTION("explicit cancellation") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
    }
    SECTION("resource deleted after key-down") {
        auto& document = f.compose.history.document;
        std::erase_if(document.resources, [](const auto& sound) { return sound.id == resource_id_t{7}; });
        document.charts.front().sound_layer_defaults.clear();
        ++document.revision;
    }
    const auto revision = f.compose.history.document.revision;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
    expect_compose_test(f.notes().empty());
    expect_compose_test(f.compose.history.document.revision == revision);
    expect_compose_test(f.compose.history.undo.empty());
    expect_compose_test(placed_resource_notifications.empty());
}
