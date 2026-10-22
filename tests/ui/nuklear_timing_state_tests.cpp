#include "ui/nuklear/timing/timing_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Nuklear timing state resets stage editor buffers",
          "[unit][frontend][timing]") {
    auto& state = worm_ui_nuklear_timing_state();
    state.stage_buffers_active = true;
    state.stage_buffers_lane = wormedit::timing_lane_kind_t::warp;
    state.bpm_decimal[0] = '9';

    worm_ui_nuklear_reset_timing_state();

    CHECK_FALSE(state.stage_buffers_active);
    CHECK(state.stage_buffers_lane == wormedit::timing_lane_kind_t::bpm);
    CHECK(state.bpm_decimal[0] == '\0');
}
