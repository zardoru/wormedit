#include "ui/nuklear/setup_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Nuklear setup view bounds every scalar export choice",
          "[unit][frontend][setup]") {
    WormUiSetupView view{
        .visible = true,
        .stepmania_file_profile = WORM_UI_STEPMANIA_FILE_SSC,
        .stepmania_timing_syntax = WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS,
        .stepmania_implementation = WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1,
        .stepmania_policy_valid = true,
        .stepmania_policy_status = "Ready",
    };
    CHECK(worm_ui_nuklear_setup_view_valid(&view));

    view.stepmania_file_profile = static_cast<WormUiStepmaniaFileProfile>(99);
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));
    view.stepmania_file_profile = WORM_UI_STEPMANIA_FILE_SSC;
    view.stepmania_policy_status = nullptr;
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));

    WormUiSetupView hidden{};
    CHECK(worm_ui_nuklear_setup_view_valid(&hidden));
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(nullptr));
}
