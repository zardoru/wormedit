#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace {

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
