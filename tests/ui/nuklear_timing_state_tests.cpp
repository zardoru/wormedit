#include "ui/nuklear/timing_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Nuklear timing state resets forms gestures and authored defaults",
          "[unit][frontend][timing]") {
    auto& state = worm_ui_nuklear_timing_state();
    state.stage_buffers_active = true;
    state.stage_buffers_lane = WORM_UI_TIMING_LANE_WARP;
    state.bpm_decimal[0] = '9';
    state.meter_measure.fill('9');
    state.recipe_buffers_active = true;
    state.recipe_range_drag_active = true;
    state.recipe_range_drag_mode = worm_ui_nuklear_timing_recipe_range_drag_mode_t::whole_span;
    state.recipe_drag_current_fraction = 0.75;
    state.marker_drag_active = true;
    state.marker_drag_lane = WORM_UI_TIMING_LANE_STOP;

    worm_ui_nuklear_reset_timing_state();

    CHECK_FALSE(state.stage_buffers_active);
    CHECK(state.stage_buffers_lane == WORM_UI_TIMING_LANE_BPM);
    CHECK(state.bpm_decimal[0] == '\0');
    CHECK(state.meter_measure[0] == '1');
    CHECK(state.meter_measure[1] == '\0');
    CHECK_FALSE(state.recipe_buffers_active);
    CHECK_FALSE(state.recipe_range_drag_active);
    CHECK(state.recipe_range_drag_mode ==
          worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range);
    CHECK(state.recipe_drag_current_fraction == 0.0);
    CHECK_FALSE(state.marker_drag_active);
    CHECK(state.marker_drag_lane == WORM_UI_TIMING_LANE_BPM);
}
