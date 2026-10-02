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

TEST_CASE("Meter suggestions list exact signatures for a typed length",
          "[unit][frontend][timing]") {
    const auto three_quarters = worm_ui_nuklear_suggest_meters("3", "4");
    REQUIRE(three_quarters.count == 3);
    CHECK(three_quarters.items[0].numerator == 3);
    CHECK(three_quarters.items[0].denominator == 4);
    CHECK(three_quarters.items[1].numerator == 6);
    CHECK(three_quarters.items[1].denominator == 8);
    CHECK(three_quarters.items[2].numerator == 12);

    const auto seven_eighths = worm_ui_nuklear_suggest_meters("7", "8");
    REQUIRE(seven_eighths.count == 2);
    CHECK(seven_eighths.items[0].numerator == 7);
    CHECK(seven_eighths.items[0].denominator == 8);
    CHECK(seven_eighths.items[1].numerator == 14);

    CHECK(worm_ui_nuklear_suggest_meters("1", "3").count == 0);
    CHECK(worm_ui_nuklear_suggest_meters("", "4").count == 0);
    CHECK(worm_ui_nuklear_suggest_meters("0", "4").count == 0);
    CHECK(worm_ui_nuklear_suggest_meters("3", "x").count == 0);
}
