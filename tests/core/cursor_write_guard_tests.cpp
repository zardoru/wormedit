#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/interaction.h>

TEST_CASE("Cursor moves and temporary restoration preserve the screen anchor",
          "[unit][core][interaction][cursor]") {
    wormedit::editor_interaction_state_t state;
    wormedit::configure_cursor_viewport(state, 400, wormedit::editor_field_scroll_direction_t::down);
    wormedit::set_cursor_subrow(state, .5);
    const auto before = wormedit::capture_cursor_pose(state);
    wormedit::place_cursor_at(state, {wormedit::measure_id_t{1}, 240});
    CHECK(state.fixed_cursor_screen_y_pixels == 320);
    CHECK(state.cursor_subrow == 0);
    wormedit::place_cursor_on_canvas_row(state, std::nullopt, 96000, .25);
    CHECK(state.virtual_bms_cursor_row == 96000);
    CHECK(state.fixed_cursor_screen_y_pixels == 320);
    wormedit::set_cursor_screen_anchor(state, 50, wormedit::editor_field_scroll_direction_t::up);
    wormedit::restore_cursor_pose(state, before);
    CHECK(wormedit::capture_cursor_pose(state) == before);
}

TEST_CASE("Cursor viewport changes keep the anchor fraction and preserve source geometry",
          "[unit][core][interaction][cursor]") {
    wormedit::editor_interaction_state_t state;
    wormedit::configure_cursor_viewport(state, 400, wormedit::editor_field_scroll_direction_t::down);
    wormedit::set_viewport_scroll_pixels(state, 80);
    wormedit::configure_cursor_viewport(state, 800, wormedit::editor_field_scroll_direction_t::down);
    CHECK(state.fixed_cursor_screen_y_pixels == 640);
    CHECK(state.viewport_scroll_pixels == 0);
    wormedit::configure_cursor_viewport(state, 800, wormedit::editor_field_scroll_direction_t::up);
    CHECK(state.fixed_cursor_screen_y_pixels == 160);
    CHECK(state.viewport_scroll_pixels == 0);
}
