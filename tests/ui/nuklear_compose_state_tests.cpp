#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <limits>
#include <source_location>

namespace {

void expect_inline(bool condition, std::source_location where = std::source_location::current()) {
    INFO("inline timing check caller line " << where.line());
    CHECK(condition);
}

auto staged_timing(WormUiTimingLaneKind lane) -> WormUiTimingView {
    WormUiTimingView view{};
    view.chart_id = 7;
    view.document_revision = 11;
    view.stage_active = true;
    view.stage_valid = true;
    view.stage_existing_object_id = 29;
    view.stage_lane = lane;
    view.stage_value = 12.5;
    view.stage_secondary_value = 3.0;
    view.stage_transition_is_seconds = true;
    view.stage_toggle_primary = true;
    view.stage_pause_unit = WORM_UI_TIMING_PAUSE_MILLISECONDS;
    std::strcpy(view.stage_primary_numerator_text, "25");
    std::strcpy(view.stage_primary_denominator_text, "2");
    std::strcpy(view.stage_secondary_numerator_text, "3");
    std::strcpy(view.stage_secondary_denominator_text, "1");
    std::strcpy(view.stage_primary_decimal_text, "12.5");
    view.stage_primary_decimal_exact = true;
    return view;
}

void check_primary(const WormUiWorkspaceAction& action, const char* numerator,
                   const char* denominator) {
    CHECK(std::strcmp(action.timing_primary_numerator_text, numerator) == 0);
    CHECK(std::strcmp(action.timing_primary_denominator_text, denominator) == 0);
}

void check_secondary(const WormUiWorkspaceAction& action, const char* numerator,
                     const char* denominator) {
    CHECK(std::strcmp(action.timing_secondary_numerator_text, numerator) == 0);
    CHECK(std::strcmp(action.timing_secondary_denominator_text, denominator) == 0);
}

void check_inline_rejected(const WormUiTimingView& view, const char* text,
                           WormUiWorkspaceAction& action, const char* unchanged_numerator) {
    CHECK_FALSE(worm_ui_nuklear_inline_timing_confirm(view, text, action));
    CHECK(std::strcmp(action.timing_primary_numerator_text, unchanged_numerator) == 0);
}

void check_inline_binding(const WormUiWorkspaceAction& action) {
    CHECK(action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM);
    CHECK(action.timing_chart_id == 7);
    CHECK(action.timing_document_revision == 11);
    CHECK(action.timing_object_id == 29);
    CHECK(action.timing_lane == WORM_UI_TIMING_LANE_BPM);
}

} // namespace

TEST_CASE("Nuklear compose state resets queues gestures editors and authored defaults",
          "[unit][frontend][compose]") {
    auto& state = worm_ui_nuklear_compose_state();
    state.key_event_count = 1;
    state.wheel_event_count = 1;
    state.cancel_pending = true;
    state.pointer_hold_active = true;
    state.bmson_razor_pointer_active = true;
    state.bmson_razor_last_y_pixels = 42.0;
    state.bmson_marker_move_candidate_active = true;
    state.bmson_marker_move_active = true;
    state.bmson_marker_move_event_id = 42;
    state.note_move_active = true;
    state.timing_stage_drag_active = true;
    state.timing_marker_candidate_active = true;
    state.timing_marker_drag_active = true;
    state.timing_missing_initial_selected = true;
    state.timing_marker_object_id = 42;
    state.timing_marker_chart_row = 43;
    state.timing_marker_lane = WORM_UI_TIMING_LANE_SPEED;
    state.timing_marker_anchor_y_pixels = 12.0;
    state.timing_marker_anchor_screen_y = 13.0f;
    state.timing_marker_current_y_pixels = 14.0;
    state.timing_marker_chart_id = 15;
    state.timing_marker_revision = 16;
    state.inline_timing_open = true;
    state.inline_timing_focus_pending = true;
    state.inline_timing_chart_id = 42;
    state.inline_timing_revision = 43;
    state.inline_timing_object_id = 44;
    state.inline_timing_lane = WORM_UI_TIMING_LANE_SCROLL;
    state.inline_timing_anchor_screen_x = 45.0f;
    state.inline_timing_anchor_screen_y = 46.0f;
    state.inline_timing_text[0] = '3';
    state.inline_timing_error[0] = 'x';
    state.area_selection_active = true;
    state.area_selection_mode = WORM_UI_COMPOSE_SELECTION_TOGGLE;
    state.navigation_key_active = true;
    state.navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    state.last_chart_id = 42;
    state.last_project_id = 42;
    state.label_project_id = 42;
    state.label_chart_id = 42;
    state.note_label[0] = 'x';
    state.semantic_layer_name_revision = 42;
    state.semantic_layer_color_editor_visible = true;
    state.inspector_visible = false;
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
    state.resource_search[0] = 'x';
    state.resource_import_path[0] = '/';
    state.armed_resource_id = 42;
    state.auto_increment_on_place = true;
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
    state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::restart;

    worm_ui_nuklear_reset_compose_state();

    CHECK(state.key_event_count == 0);
    CHECK(state.wheel_event_count == 0);
    CHECK_FALSE(state.cancel_pending);
    CHECK_FALSE(state.pointer_hold_active);
    CHECK_FALSE(state.bmson_razor_pointer_active);
    CHECK(state.bmson_razor_last_y_pixels == 0.0);
    CHECK_FALSE(state.bmson_marker_move_candidate_active);
    CHECK_FALSE(state.bmson_marker_move_active);
    CHECK(state.bmson_marker_move_event_id == 0);
    CHECK_FALSE(state.note_move_active);
    CHECK_FALSE(state.timing_stage_drag_active);
    CHECK_FALSE(state.area_selection_active);
    CHECK(state.area_selection_mode == WORM_UI_COMPOSE_SELECTION_REPLACE);
    CHECK_FALSE(state.navigation_key_active);
    CHECK(state.navigation_action == WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD);
    CHECK(state.last_chart_id == 0);
    CHECK(state.last_project_id == 0);
    CHECK(state.label_project_id == std::numeric_limits<std::uint64_t>::max());
    CHECK(state.label_chart_id == std::numeric_limits<std::uint64_t>::max());
    CHECK(state.note_label[0] == '\0');
    CHECK(state.semantic_layer_name_revision == std::numeric_limits<std::uint64_t>::max());
    CHECK_FALSE(state.semantic_layer_color_editor_visible);
    CHECK(state.inspector_visible);
    CHECK(state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample);
    CHECK(state.resource_search[0] == '\0');
    CHECK(state.resource_import_path[0] == '\0');
    CHECK(state.armed_resource_id == 0);
    CHECK_FALSE(state.auto_increment_on_place);
    CHECK(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    CHECK(state.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off);
}

TEST_CASE("Compose reset clears inline timing and timing marker gestures",
          "[unit][frontend][compose]") {
    auto& state = worm_ui_nuklear_compose_state();
    state.timing_marker_drag_active = true;
    state.timing_marker_object_id = 42;
    state.timing_marker_chart_row = 43;
    state.inline_timing_open = true;
    state.inline_timing_focus_pending = true;
    state.inline_timing_text[0] = 'x';
    worm_ui_nuklear_reset_compose_state();
    CHECK_FALSE(state.timing_marker_drag_active);
    CHECK(state.timing_marker_object_id == 0);
    CHECK(state.timing_marker_chart_row == 0);
    CHECK_FALSE(state.inline_timing_open);
    CHECK_FALSE(state.inline_timing_focus_pending);
    CHECK(state.inline_timing_text[0] == '\0');
}

TEST_CASE("Compose placement picker advances only when armed and stops at exhaustion",
          "[unit][frontend][compose]") {
    worm_ui_nuklear_reset_compose_state();
    auto& state = worm_ui_nuklear_compose_state();
    state.armed_resource_id = 4;
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    worm_ui_nuklear_advance_resource_picker(4, 5);
    CHECK(state.armed_resource_id == 4);
    state.auto_increment_on_place = true;
    worm_ui_nuklear_advance_resource_picker(3, 5);
    CHECK(state.armed_resource_id == 4);
    worm_ui_nuklear_advance_resource_picker(4, 5);
    CHECK(state.armed_resource_id == 5);
    CHECK(state.auto_increment_on_place);
    worm_ui_nuklear_advance_resource_picker(5, 0);
    CHECK(state.armed_resource_id == 5);
    CHECK_FALSE(state.auto_increment_on_place);
    CHECK(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    worm_ui_nuklear_reset_compose_state();
}

TEST_CASE("Compose file picker ignores stale callbacks after frontend reset",
          "[unit][frontend][compose]") {
    worm_ui_nuklear_reset_compose_state();
    const auto first = worm_ui_nuklear_compose_file_dialog_begin();
    REQUIRE(first);
    CHECK(worm_ui_nuklear_compose_file_dialog_open());
    CHECK_FALSE(worm_ui_nuklear_compose_file_dialog_begin());
    worm_ui_nuklear_compose_file_dialog_complete(*first - 1, "/stale.wav", {}, false);
    CHECK(worm_ui_nuklear_compose_file_dialog_open());
    worm_ui_nuklear_compose_file_dialog_complete(*first, "/selected.wav", {}, false);
    CHECK_FALSE(worm_ui_nuklear_compose_file_dialog_open());
    const auto picked = worm_ui_nuklear_compose_file_dialog_take();
    REQUIRE(picked);
    CHECK(picked->path == "/selected.wav");
    CHECK_FALSE(worm_ui_nuklear_compose_file_dialog_take());

    const auto second = worm_ui_nuklear_compose_file_dialog_begin();
    REQUIRE(second);
    worm_ui_nuklear_reset_compose_state();
    worm_ui_nuklear_compose_file_dialog_complete(*second, "/late.wav", {}, false);
    CHECK_FALSE(worm_ui_nuklear_compose_file_dialog_open());
    CHECK_FALSE(worm_ui_nuklear_compose_file_dialog_take());
}

TEST_CASE("Inline timing BPM decimal emits exact editor text and stage binding",
          "[unit][frontend][compose]") {
    auto view = staged_timing(WORM_UI_TIMING_LANE_BPM);
    char initial[64]{};
    worm_ui_nuklear_inline_timing_initial_text(view, initial, sizeof(initial));
    CHECK(std::strcmp(initial, "12.5") == 0);

    WormUiWorkspaceAction action{};
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "123.456", action));
    check_inline_binding(action);
    CHECK(action.timing_bpm_decimal_edited);
    CHECK(std::strcmp(action.timing_bpm_decimal_text, "123.456") == 0);
    check_primary(action, "15432", "125");
}

TEST_CASE("Inline timing BPM fractions retain exact values", "[unit][frontend][compose]") {
    auto view = staged_timing(WORM_UI_TIMING_LANE_BPM);
    WormUiWorkspaceAction action{};
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "7/3", action));
    CHECK_FALSE(action.timing_bpm_decimal_edited);
    check_primary(action, "7", "3");
    char initial[64]{};
    view.stage_primary_decimal_exact = false;
    worm_ui_nuklear_inline_timing_initial_text(view, initial, sizeof(initial));
    CHECK(std::strcmp(initial, "25/2") == 0);
}

TEST_CASE("Inline timing scalar accepts signed scroll and zero pause exactly",
          "[unit][frontend][compose]") {
    auto view = staged_timing(WORM_UI_TIMING_LANE_SCROLL);
    WormUiWorkspaceAction action{};
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "-0.125", action));
    check_primary(action, "-1", "8");
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "0", action));
    check_primary(action, "0", "1");

    view.stage_lane = WORM_UI_TIMING_LANE_DELAY;
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "0.000", action));
    CHECK(action.timing_pause_unit == WORM_UI_TIMING_PAUSE_MILLISECONDS);
    check_primary(action, "0", "1");
    view.stage_lane = WORM_UI_TIMING_LANE_STOP;
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "3/8", action));
    check_primary(action, "3", "8");
}

TEST_CASE("Inline timing edit retains secondary fields and rejects invalid input atomically",
          "[unit][frontend][compose]") {
    auto view = staged_timing(WORM_UI_TIMING_LANE_SPEED);
    WormUiWorkspaceAction action{};
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "2.75", action));
    check_primary(action, "11", "4");
    check_secondary(action, "3", "1");
    CHECK(action.timing_transition_is_seconds);
    check_inline_rejected(view, "2/0", action, "11");
    check_inline_rejected(view, "x", action, "11");
    check_inline_rejected(view, "-1", action, "11");
    view.stage_lane = WORM_UI_TIMING_LANE_KIAI;
    check_inline_rejected(view, "1", action, "11");
}

TEST_CASE("Inline timing warp uses integer secondary fields",
          "[unit][frontend][compose]") {
    auto view = staged_timing(WORM_UI_TIMING_LANE_WARP);
    char initial[64]{};
    worm_ui_nuklear_inline_timing_initial_text(view, initial, sizeof(initial));
    CHECK(std::strcmp(initial, "3") == 0);
    WormUiWorkspaceAction action{};
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "42", action));
    check_secondary(action, "42", "1");
    CHECK(std::strcmp(action.timing_primary_numerator_text, "25") == 0);
    CHECK_FALSE(worm_ui_nuklear_inline_timing_confirm(view, "1.5", action));
    CHECK(std::strcmp(action.timing_secondary_numerator_text, "42") == 0);
}

TEST_CASE("Inline timing anchor edits preserve lock state", "[unit][frontend][compose]") {
    auto view = staged_timing(WORM_UI_TIMING_LANE_ANCHOR);
    WormUiWorkspaceAction action{};
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "1234", action));
    check_primary(action, "1234", "1");
    CHECK(action.timing_toggle_primary);
    REQUIRE(worm_ui_nuklear_inline_timing_confirm(view, "18446744073709551615", action));
    check_primary(action, "18446744073709551615", "1");
    CHECK_FALSE(worm_ui_nuklear_inline_timing_confirm(view, "-1", action));
    CHECK(std::strcmp(action.timing_primary_numerator_text, "18446744073709551615") == 0);
}

namespace {

void check_overview_invalid_dimensions() {
    CHECK_FALSE(worm_ui_nuklear_overview_fraction(10.0, 0.0, 0.0));
    CHECK_FALSE(worm_ui_nuklear_overview_fraction(
        std::numeric_limits<double>::infinity(), 0.0, 100.0));
}

void check_overview_capture_host(const worm_ui_nuklear_overview_capture_t& capture,
                                 const WormUiComposeView& view) {
    CHECK_FALSE(worm_ui_nuklear_overview_capture_matches(capture, view, 10.0, 100.0, false, true));
    CHECK_FALSE(worm_ui_nuklear_overview_capture_matches(capture, view, 10.0, 100.0, true, false));
    CHECK_FALSE(worm_ui_nuklear_overview_capture_matches(capture, view, 10.0, 101.0, true, true));
    CHECK_FALSE(worm_ui_nuklear_overview_capture_matches(capture, view, 11.0, 100.0, true, true));
}

void check_overview_capture_identity(const worm_ui_nuklear_overview_capture_t& capture,
                                     WormUiComposeView view) {
    ++view.document_revision;
    CHECK_FALSE(worm_ui_nuklear_overview_capture_matches(capture, view, 10.0, 100.0, true, true));
    --view.document_revision;
    ++view.selected_audio_asset_id;
    CHECK_FALSE(worm_ui_nuklear_overview_capture_matches(capture, view, 10.0, 100.0, true, true));
}

} // namespace

TEST_CASE("Horizontal overview coordinates clamp and reject invalid dimensions",
          "[frontend][compose][overview]") {
    CHECK(worm_ui_nuklear_overview_fraction(10.0, 10.0, 100.0) == 0.0);
    CHECK(worm_ui_nuklear_overview_fraction(110.0, 10.0, 100.0) == 1.0);
    CHECK(worm_ui_nuklear_overview_fraction(-20.0, 10.0, 100.0) == 0.0);
    CHECK(worm_ui_nuklear_overview_fraction(200.0, 10.0, 100.0) == 1.0);
    CHECK(worm_ui_nuklear_overview_fraction(60.0, 10.0, 100.0) == 0.5);
    check_overview_invalid_dimensions();
}

TEST_CASE("Horizontal overview drag remains sample-frame exact and bounded",
          "[frontend][compose][overview]") {
    CHECK(worm_ui_nuklear_overview_frame_delta(60.0, 10.0, 100.0, 48'000) == 24'000);
    CHECK(worm_ui_nuklear_overview_frame_delta(-40.0, 10.0, 100.0, 48'000) == -24'000);
    CHECK(worm_ui_nuklear_overview_frame_delta(11.0, 10.0, 96'000.0, 48'000) == 1);
    CHECK(worm_ui_nuklear_overview_frame_delta(10.0, 10.0, 0.0, 48'000) == 0);
    CHECK(worm_ui_nuklear_overview_frame_delta(1e100, 0.0, 1.0, 48'000) ==
          std::numeric_limits<std::int64_t>::max());
    CHECK(worm_ui_nuklear_overview_frame_delta(-1e100, 0.0, 1.0, 48'000) ==
          std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("Overview capture rejects stale source focus availability and resize",
          "[frontend][compose][overview]") {
    WormUiComposeView view{};
    view.scene_available = true;
    view.chart_id = 7;
    view.document_revision = 11;
    view.selected_audio_asset_id = 19;
    view.waveform_overview_total_frames = 48'000;
    view.waveform_overview_sample_rate = 48'000;
    const worm_ui_nuklear_overview_capture_t capture{7, 11, 19, 48'000, 48'000, 10.0, 100.0};
    CHECK(worm_ui_nuklear_overview_capture_matches(capture, view, 10.0, 100.0, true, true));
    check_overview_capture_host(capture, view);
    check_overview_capture_identity(capture, view);
}

namespace {

auto source_playhead_view() -> WormUiComposeView {
    WormUiComposeView view{};
    view.chart_id = 7;
    view.document_revision = 11;
    view.selected_audio_asset_id = 19;
    view.waveform_overview_total_frames = 96'000;
    view.waveform_overview_sample_rate = 48'000;
    view.source_playhead_available = true;
    view.source_playhead_asset_id = 19;
    view.source_playhead_frame = 44'100;
    view.source_playhead_sample_rate = 44'100;
    view.source_playhead_chart_id = 7;
    view.source_playhead_document_revision = 11;
    return view;
}

void check_source_playhead_identity_mismatches() {
    auto view = source_playhead_view();
    ++view.source_playhead_asset_id;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
    view = source_playhead_view();
    ++view.source_playhead_chart_id;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
    view = source_playhead_view();
    ++view.source_playhead_document_revision;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
}

void check_source_playhead_missing_geometry() {
    auto view = source_playhead_view();
    view.source_playhead_sample_rate = 0;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
    view = source_playhead_view();
    view.waveform_overview_sample_rate = 0;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
    view = source_playhead_view();
    view.waveform_overview_total_frames = 0;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
}

} // namespace

TEST_CASE("Minimap source playhead uses source seconds across sample rates",
          "[frontend][compose][overview][source-playhead]") {
    auto view = source_playhead_view();
    CHECK(worm_ui_nuklear_overview_source_fraction(view) == 0.5);
    view.source_playhead_frame = 0;
    CHECK(worm_ui_nuklear_overview_source_fraction(view) == 0.0);
    view.source_playhead_sample_rate = 48'000;
    view.source_playhead_frame = 48'000;
    CHECK(worm_ui_nuklear_overview_source_fraction(view) == 0.5);
    view.source_playhead_frame = 95'999;
    REQUIRE(worm_ui_nuklear_overview_source_fraction(view));
    CHECK(*worm_ui_nuklear_overview_source_fraction(view) < 1.0);
}

TEST_CASE("Minimap source playhead hides unknown stale and out-of-range positions",
          "[frontend][compose][overview][source-playhead]") {
    check_source_playhead_identity_mismatches();
    check_source_playhead_missing_geometry();
    auto view = source_playhead_view();
    view.source_playhead_available = false;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
    view.source_playhead_available = true;
    view.source_playhead_frame = 88'200;
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
    view.source_playhead_frame = std::numeric_limits<std::uint64_t>::max();
    CHECK_FALSE(worm_ui_nuklear_overview_source_fraction(view));
}

TEST_CASE("Go to measure parses display labels within the BMS virtual range",
          "[frontend][compose][measure-navigation]") {
    WormUiComposeView view{};
    view.bms_measure_labels = true;
    CHECK(worm_ui_nuklear_compose_measure_number(view, "000") == 0);
    CHECK(worm_ui_nuklear_compose_measure_number(view, "999") == 999);
    CHECK_FALSE(worm_ui_nuklear_compose_measure_number(view, "1000"));
    CHECK_FALSE(worm_ui_nuklear_compose_measure_number(view, "-1"));
    CHECK_FALSE(worm_ui_nuklear_compose_measure_number(view, ""));
}

TEST_CASE("Go to measure validates authored display numbers and bounded input",
          "[frontend][compose][measure-navigation]") {
    WormUiComposeView view{};
    view.compose_measure_count = 3;
    CHECK(worm_ui_nuklear_compose_measure_number(view, "1") == 1);
    CHECK(worm_ui_nuklear_compose_measure_number(view, "3") == 3);
    CHECK_FALSE(worm_ui_nuklear_compose_measure_number(view, "0"));
    CHECK_FALSE(worm_ui_nuklear_compose_measure_number(view, "4"));
    CHECK_FALSE(worm_ui_nuklear_compose_measure_number(view, "18446744073709551616"));
}

#include "ui/nuklear/compose_area_menu.h"

TEST_CASE("Retained area rows project through zoom scroll and reversed field", "[unit][frontend][compose]") {
    WormUiComposeView view{};
    view.area_rows_per_measure = 192;
    view.pixels_per_standard_measure = 384;
    view.area_scroll_pixels = 100;
    view.viewport_height_pixels = 600;
    CHECK(compose_area_y(view, 96) == 92);
    CHECK(compose_area_y(view, 288) == 476);
    view.downscroll = true;
    CHECK(compose_area_y(view, 96) == 508);
    CHECK(compose_area_y(view, 288) == 124);
    view.pixels_per_standard_measure = 192;
    CHECK(compose_area_y(view, 1000) == -300);
    view.area_rows_per_measure = 0;
    CHECK(compose_area_y(view, 1000) == 0);
}

TEST_CASE("Area edge capture restores exact original virtual range and source binding", "[unit][frontend][compose]") {
    WormUiComposeView view{};
    view.chart_id = 7;
    view.document_revision = 11;
    view.area_range_begin_row = 144;
    view.area_range_end_row = 10000;
    view.area_range_virtual = true;
    view.selected_note_count = 3;
    view.selected_object_count = 5;
    ComposeAreaCapture capture;
    capture.start(view, false);
    REQUIRE(capture.active);
    CHECK_FALSE(capture.begin_edge);
    CHECK(capture.current(view));
    auto moved = view;
    moved.area_range_end_row = 12000;
    WormUiWorkspaceAction action{};
    capture.restore(action);
    CHECK(action.compose_action == WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE);
    CHECK(action.compose_area_begin_row == 144);
    CHECK(action.compose_area_end_row == 10000);
    CHECK(action.compose_area_virtual);
    CHECK(action.compose_input_chart_id == 7);
    CHECK(action.compose_input_document_revision == 11);
    CHECK(view.selected_note_count == 3);
    CHECK(view.selected_object_count == 5);
    moved.document_revision = 12;
    CHECK_FALSE(capture.current(moved));
    moved.document_revision = 11;
    moved.chart_id = 8;
    CHECK_FALSE(capture.current(moved));
    capture.start(view, true);
    CHECK(capture.begin_edge);
}

TEST_CASE("Compose retained bands own Escape and captured range gestures own shortcuts", "[unit][frontend][compose]") {
    ComposeAreaShortcutContext context{true, true, false, false, false, true, true};
    CHECK(compose_area_consumes_shortcut(context));
    context.escape = false;
    CHECK_FALSE(compose_area_consumes_shortcut(context));
    context.capture = true;
    CHECK(compose_area_consumes_shortcut(context));
    context.band = false;
    CHECK(compose_area_consumes_shortcut(context));
}

TEST_CASE("Compose area shortcuts defer to other tabs unfocused windows text fields and dialogs", "[unit][frontend][compose]") {
    ComposeAreaShortcutContext context{true, true, false, false, true, true, true};
    SECTION("other tab") { context.compose_active = false; }
    SECTION("unfocused window") { context.focused = false; }
    SECTION("text entry") { context.text_entry = true; }
    SECTION("dialog") { context.blocked = true; }
    CHECK_FALSE(compose_area_consumes_shortcut(context));
}

TEST_CASE("Minimap gestures distinguish clicks drags and focus cancellation", "[unit][frontend][compose]") {
    worm_ui_nuklear_overview_gesture_t gesture;
    using event = worm_ui_nuklear_overview_event_t;
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 10, true, true, true) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 13.9, false, true, true) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 13.9, false, false, true) == event::click);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 13.9, false, false, true) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 10, true, true, true) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 14, false, true, true) == event::scroll);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 40, false, true, true) == event::scroll);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 40, false, false, true) == event::click);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 40, false, false, true) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 10, true, true, true) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 15, false, true, false) == event::none);
    CHECK(worm_ui_nuklear_overview_gesture(gesture, 15, false, false, true) == event::none);
}

TEST_CASE("Inline timing confirm and blur share the BPM and SV lifecycle",
          "[unit][frontend][compose][inline-timing]") {
    const auto lane = GENERATE(WORM_UI_TIMING_LANE_BPM, WORM_UI_TIMING_LANE_SCROLL);
    auto view = staged_timing(lane);
    worm_ui_nuklear_compose_state_t editor{};
    editor.inline_timing_open = true;
    std::strcpy(editor.inline_timing_text.data(), "2.5");
    WormUiWorkspaceAction action{};
    SECTION("Enter confirms the typed value and closes") {
        worm_ui_nuklear_inline_timing_update(editor, view, action, true, false);
        expect_inline(!editor.inline_timing_open);
        expect_inline(action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM);
        expect_inline(action.timing_lane == lane);
        check_primary(action, "5", "2");
    }
    SECTION("Invalid Enter retains the draft and requests focus again") {
        std::strcpy(editor.inline_timing_text.data(), "invalid");
        worm_ui_nuklear_inline_timing_update(editor, view, action, true, false);
        expect_inline(editor.inline_timing_open);
        expect_inline(editor.inline_timing_focus_pending);
        expect_inline(std::strcmp(editor.inline_timing_text.data(), "invalid") == 0);
        expect_inline(editor.inline_timing_error[0] != '\0');
        expect_inline(action.timing_stage_action == WORM_UI_TIMING_STAGE_NONE);
    }
    SECTION("Blur cancels without applying the draft") {
        worm_ui_nuklear_inline_timing_update(editor, view, action, false, true);
        expect_inline(!editor.inline_timing_open);
        expect_inline(!editor.inline_timing_focus_pending);
        expect_inline(action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL);
        expect_inline(editor.inline_timing_popup_close_pending);
        expect_inline(editor.inline_timing_input_consumed);
        expect_inline(action.timing_primary_numerator_text[0] == '\0');
    }
    SECTION("Escape uses the same cancellation") {
        editor.inline_timing_focus_pending = true;
        worm_ui_nuklear_inline_timing_cancel(editor, action);
        expect_inline(!editor.inline_timing_open);
        expect_inline(!editor.inline_timing_focus_pending);
        expect_inline(action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL);
        expect_inline(editor.inline_timing_popup_close_pending);
        expect_inline(editor.inline_timing_input_consumed);
        expect_inline(action.timing_primary_numerator_text[0] == '\0');
    }
}
