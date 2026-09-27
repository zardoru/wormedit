#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Nuklear compose state resets queues gestures editors and authored defaults",
          "[unit][frontend][compose]") {
    auto& state = worm_ui_nuklear_compose_state();
    state.key_event_count = 1;
    state.wheel_event_count = 1;
    state.cancel_pending = true;
    state.pointer_hold_active = true;
    state.note_move_active = true;
    state.timing_stage_drag_active = true;
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

    worm_ui_nuklear_reset_compose_state();

    CHECK(state.key_event_count == 0);
    CHECK(state.wheel_event_count == 0);
    CHECK_FALSE(state.cancel_pending);
    CHECK_FALSE(state.pointer_hold_active);
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
