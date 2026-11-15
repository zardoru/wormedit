// Compose input batches: note placement, latched range, wheel displacement, paint and layer arming.
#include "frame_timing_fixtures.h"
#include "../support/native_bms_fixture.h"
#include "../support/stale_action_assertions.h"
#include "sys/app/application_defaults.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("Imported five-key BMS materializes a Compose field that responds to the wheel",
          "[sys][compose][bms-detection][regression]") {
    const std::string source{"#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n"};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.value.song.has_value());
    compose_test_fixture_t fixture(std::move(*parsed.value.song));
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.note_endpoints.size() > 0);
    const auto before = fixture.interaction->viewport_scroll_pixels;
    wormedit::ui::workspace_action_t action{};
    action.input.shortcuts.window_focused = true;
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.input.compose_viewport_inputs.resize(1);
    action.input.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = -1, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0.016);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.area_scroll_pixels > before);
    expect_compose_test(fixture.history.undo.empty());
}

namespace {
auto cursor_placement_song() -> song_document_t {
    const std::string source{"#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n"};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.value.song.has_value());
    return std::move(*parsed.value.song);
}

void place_pointer_test_note(compose_test_fixture_t& fixture, int mode) {
    const auto resource = mode == 1;
    auto& state = *fixture.interaction;
    state.fixed_cursor_screen_y_pixels = 48;
    state.cursor_subrow = 7;
    const auto cursor_before = capture_cursor_pose(state);
    const local_position_t position{(*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().id, 240};
    const auto& chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart));
    const auto view = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(view), position);
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    action.input.compose_inputs.resize(1);
    fixture.application.project.compose_gesture.note_paint_mode_active = !resource;
    action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
    action.input.compose_inputs[0] = {.kind = resource ? WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE
                                               : WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .object_id = {fixture.history.document.resources.front().id.value},
        .resource_role = wormedit::resource_role_t::sample,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.preferences.editor_preferences))};
    if (mode == 2) {
        action.input.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN;
        action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{2}));
        action.input.compose_inputs[1] = action.input.compose_inputs[0];
        action.input.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
        action.input.compose_inputs.resize(2);
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(capture_cursor_pose(state) == cursor_before);
}

void check_note_placement_preserves_cursor(int mode, bool downscroll) {
    INFO("Mode " << mode << ", downscroll " << downscroll);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    place_pointer_test_note(fixture, mode);
    expect_compose_test(fixture.interaction->fixed_cursor_screen_y_pixels == 48);
    expect_compose_test(fixture.interaction->cursor_subrow == 7);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.cursor.has_value());
    INFO("drawn cursor " << fixture.flattened.view.compose.cursor->bounds.top << " anchor " << *fixture.interaction->fixed_cursor_screen_y_pixels);
    expect_compose_test(std::abs(fixture.flattened.view.compose.cursor->bounds.top - 47) < 1e-9);
    const auto scroll = fixture.interaction->viewport_scroll_pixels;
    auto action = cursor_placement_action(fixture);
    action.input.compose_viewport_inputs.resize(1);
    action.input.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = downscroll ? -1.0 : 1.0, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0.016);
    INFO(status << ", scroll " << scroll << " -> " << fixture.interaction->viewport_scroll_pixels);
    expect_compose_test(fixture.interaction->fixed_cursor_screen_y_pixels ==
        compose_cursor_screen_anchor(fixture.interaction->viewport_height_pixels, downscroll));
    expect_compose_test(fixture.interaction->viewport_scroll_pixels > scroll);
    expect_compose_test(fixture.history.undo.size() == 1);
}

void check_ten_key_note_commit() {
    auto song = cursor_placement_song();
    auto chart = *song.read_charts().front();
    const auto profile = *make_osu_mania_profile(10);
    chart.lane_layouts.front().layout = profile.layout;
    chart.input_profile = profile.input;
    for (auto& measure : chart.measures) measure.notes.clear();
    compose_test_fixture_t fixture{test::native_bms_fixture(std::move(song), std::move(chart))};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.lane_key_bindings.size() == 10);
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    for (const auto& binding : profile.input.bindings) {
        auto action = cursor_placement_action(fixture);
        action.input.compose_inputs.resize(1);
        action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
            .lane_id = {binding.lane.value}};
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
            *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
        expect_compose_test((fixture.interaction->hold() != nullptr));
        action.input.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE;
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
            *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
        expect_compose_test(!(fixture.interaction->hold() != nullptr));
    }
    const auto& notes = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().notes;
    expect_compose_test(notes.size() == 10);
    for (const auto& binding : profile.input.bindings)
        expect_compose_test(std::ranges::count(notes, binding.lane, &note_t::lane) == 1);
    expect_compose_test(fixture.history.undo.size() == 10);
}

void check_latched_range_projection(bool downscroll) {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.active_snap_denominator = 4;
    auto action = cursor_placement_action(fixture);
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(static_cast<bool>(state.gesture<wormedit::pending_space_range_selection_t>()));
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.space_range_preview_visible);
    const auto height = view.space_range_preview_bounds.bottom - view.space_range_preview_bounds.top;
    expect_compose_test(std::abs(height - state.pixels_per_standard_measure / 4) < 1e-9);
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(!static_cast<bool>(state.gesture<wormedit::pending_space_range_selection_t>()));
    expect_compose_test(state.retained_canvas_selection_range.has_value());
    const auto range = *state.retained_canvas_selection_range;
    expect_compose_test(range.end_row - range.begin_row ==
        (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).divisions_per_standard_measure / 4);
    expect_compose_test(fixture.history.undo.empty());
}

void check_wheel_displacement(bool downscroll, double delta,
                              WormUiComposeViewportInputKind kind, bool playing) {
    INFO("downscroll " << downscroll << ", delta " << delta
         << ", kind " << kind << ", playing " << playing);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.viewport_scroll_pixels = 1000.0;
    state.active_snap_denominator = 4;
    fixture.application.project.transport.preview_playhead_absolute_row = 960.0;
    fixture.application.services.transport_preview_playing = [playing] { return playing; };
    std::size_t seeks = 0;
    fixture.application.services.transport_seek = [&seeks](std::uint64_t) {
        ++seeks;
        return compose_action_outcome_t::from(std::string{"Seeked."}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    auto action = cursor_placement_action(fixture);
    action.input.compose_viewport_inputs.resize(1);
    action.input.compose_viewport_inputs[0] = {.kind = kind, .delta_y = delta,
        .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0.016);
    const bool forward = (delta > 0.0) != downscroll;
    if (playing) {
        expect_compose_test(fixture.application.project.transport.preview_playhead_absolute_row == (forward ? 1200.0 : 720.0));
        expect_compose_test(seeks == 1);
        expect_compose_test(state.viewport_scroll_pixels == 1000.0);
    } else {
        expect_compose_test((state.viewport_scroll_pixels > 1000.0) == forward);
        expect_compose_test(state.viewport_scroll_pixels != 1000.0);
        expect_compose_test(seeks == 0);
    }
    expect_compose_test(fixture.history.undo.empty());
}
} // namespace

TEST_CASE("Compose batches reject later object or viewport input after an earlier edit",
          "[sys][batch-stamp]") {
    const auto viewport_later = GENERATE(false, true);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.project();
    auto& state = *fixture.interaction;
    const auto& chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart));
    const auto count = chart.measures.front().notes.size();
    const auto inspected = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart, std::get<editor_interaction_view_model_t>(inspected),
        {chart.measures.front().id, 240});
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .object_id = {fixture.history.document.resources.front().id.value},
        .resource_role = wormedit::resource_role_t::sample,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.preferences.editor_preferences))};
    if (viewport_later) {
        action.input.compose_viewport_inputs.resize(1);
        action.input.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
            .delta_y = -1, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    } else {
        action.input.compose_inputs.resize(2);
        action.input.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_DRAW_SOURCE, .resource_role = wormedit::resource_role_t::bga, .resource_id = {999}};
    }
    const auto scroll = state.viewport_scroll_pixels;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    test::check_stale_action_message(status, viewport_later ? "Remaining Compose viewport input" : "Remaining Compose input");
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test((*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().notes.size() == count + 1);
    expect_compose_test(fixture.application.project.compose_gesture.draw_source_id != 999);
    expect_compose_test(state.viewport_scroll_pixels == scroll);
}

TEST_CASE("Compose 10K lane presses commit Tap notes in every number row lane",
          "[sys][compose][lane-keys][regression]") {
    check_ten_key_note_commit();
}

TEST_CASE("Latched range input projects and commits one snap in both scroll directions",
          "[sys][compose][range-selection][regression]") {
    check_latched_range_projection(false);
    check_latched_range_projection(true);
}

TEST_CASE("Compose wheel displacement follows the same direction during editing and playback",
          "[sys][compose][playhead-skip][regression]") {
    for (const bool downscroll : {false, true}) {
        for (const double delta : {-1.0, 1.0}) {
            for (const auto kind : {WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
                                    WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL}) {
                for (const bool playing : {false, true}) {
                    check_wheel_displacement(downscroll, delta, kind, playing);
                }
            }
        }
    }
}

TEST_CASE("Compose latched autoscroll preserves pixel speed during playback",
          "[sys][compose][autoscroll][regression]") {
    const bool downscroll = GENERATE(false, true);
    const bool playing = GENERATE(false, true);
    const double zoom = GENERATE(240.0, 960.0);
    const int frames = GENERATE(20, 120);
    const double offset = GENERATE(8.25, 40.0);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.viewport_scroll_pixels = 1000.0;
    state.pixels_per_standard_measure = zoom;
    fixture.application.project.transport.preview_playhead_absolute_row = 960.0;
    fixture.application.services.transport_preview_playing = [playing] { return playing; };
    std::size_t seeks = 0;
    std::uint64_t sought_frame{};
    fixture.application.services.transport_seek = [&seeks, &sought_frame](std::uint64_t frame) {
        ++seeks;
        sought_frame = frame;
        return compose_action_outcome_t::from("Seeked.", notification_severity_t::hint,
                                              notification_source_t::input);
    };
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    auto action = cursor_placement_action(fixture);
    action.input.compose_viewport_middle_click = true;
    action.input.compose_viewport_middle_click_x_pixels = 200.0;
    action.input.compose_viewport_middle_click_y_pixels = 100.0;
    action.input.compose_viewport_pointer_valid = true;
    action.input.compose_viewport_pointer_x_pixels = 200.0;
    action.input.compose_viewport_pointer_y_pixels = 100.0 + offset;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0.0);
    action.input.compose_viewport_middle_click = false;
    for (int frame = 0; frame < frames; ++frame)
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
            state, viewport, navigation, fixture.application, fixture.cache, 1.0 / frames);
    // A quarter pixel beyond the dead zone requests 1.5 pixels over one second,
    // including sub-row motion at high zoom, independently of the frame count.
    const double pixels = (downscroll ? -1.0 : 1.0) * (offset - 8.0) * 6.0;
    const auto divisions = fixture.history.read_chart(state.chart)->divisions_per_standard_measure;
    if (playing) {
        const auto expected_row = 960.0 + pixels * divisions / zoom;
        expect_compose_test(*fixture.application.project.transport.preview_playhead_absolute_row ==
                            Catch::Approx(expected_row));
        const auto expected_frame = static_cast<std::uint64_t>(std::llround(
            expected_row * 240.0 * audio::output_sample_rate / (154.0 * divisions)));
        expect_compose_test(sought_frame == expected_frame);
        expect_compose_test(seeks > 0);
        expect_compose_test(state.viewport_scroll_pixels == 1000.0);
    } else {
        expect_compose_test(state.viewport_scroll_pixels == Catch::Approx(1000.0 + pixels));
        expect_compose_test(seeks == 0);
    }
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Pointer note placement preserves the yellow cursor and normal scrolling",
          "[sys][compose][cursor-placement][regression]") {
    check_note_placement_preserves_cursor(0, false);
    check_note_placement_preserves_cursor(0, true);
    check_note_placement_preserves_cursor(1, false);
    check_note_placement_preserves_cursor(1, true);
    check_note_placement_preserves_cursor(2, false);
    check_note_placement_preserves_cursor(2, true);
}

namespace {
void dispatch_virtual_paint(compose_test_fixture_t& fixture, WormUiComposeInputKind kind,
    std::uint64_t row) {
    const auto& view = fixture.flattened.view.compose;
    auto action = cursor_placement_action(fixture);
    const auto& state = *fixture.interaction;
    const auto source_y = static_cast<double>(row) * state.pixels_per_standard_measure /
        fixture.history.read_chart(state.chart)->divisions_per_standard_measure - state.viewport_scroll_pixels;
    action.input.compose_inputs = {{.kind = kind,
        .x_pixels = view.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(source_y, state.viewport_height_pixels, view.downscroll)}};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}

void check_virtual_paint_commit(compose_test_fixture_t& fixture) {
    const auto* chart = fixture.history.read_chart(*fixture.binding.value->selected_chart);
    const auto id = chart->id;
    expect_compose_test(chart->measures.size() == 2);
    expect_compose_test(chart->measures.back().notes.size() == 3);
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(undo_document_command(fixture.history));
    expect_compose_test(fixture.history.read_chart(id)->measures.size() == 1);
}
} // namespace

TEST_CASE("New project mouse paint previews virtual measures and materializes only on release",
          "[sys][compose][paint][virtual-canvas][new-project]") {
    const auto layout = GENERATE("osu-mania-7k", "bme-7k-outside-scratch");
    auto document = make_initial_document(song_id_t{991}, layout);
    REQUIRE(document);
    compose_test_fixture_t fixture{std::move(*document)};
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    const auto divisions = fixture.history.read_chart(fixture.interaction->chart)->divisions_per_standard_measure;
    dispatch_virtual_paint(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, divisions + divisions / 4);
    expect_compose_test(fixture.interaction->gesture<pending_note_paint_gesture_t>() != nullptr);
    dispatch_virtual_paint(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE, divisions + 3 * divisions / 4);
    fixture.project();
    expect_compose_test(std::ranges::any_of(fixture.flattened.view.compose.note_endpoints,
        [](const auto& endpoint) { return endpoint.paint_preview; }));
    expect_compose_test(fixture.history.undo.empty());
    const auto cancelled = GENERATE(false, true);
    if (cancelled) {
        expect_compose_test(static_cast<bool>(dispatch_editor_action(fixture.history, *fixture.interaction,
            cancel_editor_gesture_action_t{})));
        expect_compose_test(fixture.history.read_chart(fixture.interaction->chart)->measures.size() == 1);
    } else {
        dispatch_virtual_paint(fixture, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, divisions + 3 * divisions / 4);
        check_virtual_paint_commit(fixture);
    }
}

TEST_CASE("Note paint begin input carries the armed sound into the document and advances once",
          "[sys][compose][paint][sound]") {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.project();
    const auto sound = fixture.history.document.resources.front().id;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    placed_resource_notifications.clear();
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto count_notes_with_sound = [&] {
        std::size_t count{};
        for (const auto& measure : (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures)
            for (const auto& note : measure.notes)
                if (note.sound == sound) ++count;
        return count;
    };
    const auto before = count_notes_with_sound();
    auto action = paint_stroke_inputs(fixture, sound.value);
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(count_notes_with_sound() == before + 1);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{sound.value});

    // A silent stroke is committed without notifying the dock.
    fixture.project();
    placed_resource_notifications.clear();
    auto silent = paint_stroke_inputs(fixture, 0);
    (void)dispatch_compose_input_action(silent, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(placed_resource_notifications.empty());
}

namespace {
void check_armed_layer_placement(int mode, bool no_layer) {
    auto song = cursor_placement_song();
    auto chart = *song.read_charts().front();
    chart.semantic_layers = {{.id = layer_id_t{75000}, .name = "Chosen"},
                             {.id = layer_id_t{85000}, .name = "Sound default"}};
    chart.sound_layer_defaults = {{.resource = song.resources.front().id, .layers = {layer_id_t{85000}}}};
    compose_test_fixture_t fixture{test::native_bms_fixture(std::move(song), std::move(chart))};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    const auto chosen = fixture.history.read_chart(*fixture.binding.value->selected_chart)->semantic_layers.front().id;
    fixture.application.project.semantic_layer.semantic_layer_target = no_layer ? std::nullopt : std::optional{chosen};
    fixture.application.project.semantic_layer.semantic_layer_target_none = no_layer;
    fixture.application.project.compose_gesture.note_paint_mode_active = mode == 0 || mode == 1;
    fixture.project();
    const auto sound = fixture.history.document.resources.front().id;
    auto action = paint_stroke_inputs(fixture, sound.value);
    if (mode == 1) {
        place_cursor_at(*fixture.interaction,
            {(*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().id, 240});
        const auto lane = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).lane_layouts.front().layout.lanes.front().id;
        action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
        action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {lane.value}};
        action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{2}));
        action.input.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {lane.value}};
    } else if (mode == 2) {
        action.input.compose_inputs.resize(1);
        action.input.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE;
        action.input.compose_inputs[0].object_id.value = sound.value;
        action.input.compose_inputs[0].resource_role = wormedit::resource_role_t::sample;
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    const auto& notes = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().notes;
    const auto note = std::ranges::find(notes, 240U, [](const auto& value) { return value.position.row; });
    expect_compose_test(note != notes.end());
    expect_compose_test(note->semantic_layers == (no_layer ? std::vector<layer_id_t>{}
                                                          : std::vector{chosen}));
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(undo_document_command(fixture.history));
    const auto& restored = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().notes;
    expect_compose_test(std::ranges::none_of(restored, [](const auto& value) { return value.position.row == 240; }));
}
} // namespace

TEST_CASE("Compose layer target arms new pointer keyboard and resource notes with one undo",
          "[sys][compose][semantic-layer][regression]") {
    for (const auto mode : {0, 1, 2}) {
        check_armed_layer_placement(mode, false);
        check_armed_layer_placement(mode, true);
    }
}

TEST_CASE("Compose BMS layout picker includes DP5 without free zones",
          "[sys][compose][dp5-standard]") {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto names = std::span(view.lane_layout_profile_names);
    CHECK(std::ranges::any_of(names, [](const auto& name) {
        return std::string_view{name} == "BMS DP5 / 10K";
    }));
}

TEST_CASE("Stopped BMS transport seek moves cursor and viewport without editing",
          "[sys][compose][seek-bar][regression]") {
    notification_center_t center;
    notification_sink_t sink{center};
    const std::string source{"#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n"};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.value.song.has_value());
    compose_test_fixture_t fixture(std::move(*parsed.value.song));
    fixture.binding.value->selected_chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart)).id;
    fixture.project();
    const auto before = fixture.interaction->cursor;
    auto status = finish_compose_action(
        sink, seek_stopped_compose_transport(fixture.history, *fixture.interaction, fixture.cache,
                                             20 * 44'100));
    expect_compose_test(status.message == "Compose cursor moved to the seek position.");
    expect_compose_test(fixture.interaction->cursor != before);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels > 0);
    expect_compose_test(!fixture.interaction->virtual_bms_cursor_row);
    expect_compose_test(fixture.history.undo.empty());
    center.drain_inbox(notification_time_t{0});
    auto messages = center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 1);
    expect_compose_test(messages[0].severity == notification_severity_t::hint);
    expect_compose_test(messages[0].source == notification_source_t::input);
    const auto sought = fixture.interaction->cursor;
    ++fixture.history.document.revision;
    (void)finish_compose_action(sink, seek_stopped_compose_transport(
                                          fixture.history, *fixture.interaction, fixture.cache, 0));
    expect_compose_test(fixture.interaction->cursor == sought);
    center.drain_inbox(notification_time_t{1});
    messages = center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 2);
    expect_compose_test(messages[0].severity == notification_severity_t::warning);
    expect_compose_test(messages[0].message == "Compose seek is unavailable until the chart is ready.");
}
