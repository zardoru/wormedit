#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Discarded presentation frames retire commands and preserve the next visible frame",
          "[frontend][event-harness][presentation]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    for (unsigned frame = 0; frame < 40; ++frame) {
        worm_ui_frontend_begin_input();
        worm_ui_frontend_end_input();
        (void)worm_ui_frontend_compose(&harness.view());
        REQUIRE(worm_ui_frontend_copy_drawn_text_for_testing(nullptr, 0) > 0);
        worm_ui_frontend_discard_frame();
        CHECK(worm_ui_frontend_copy_drawn_text_for_testing(nullptr, 0) == 0);
    }
    (void)harness.frame();
    CHECK_FALSE(harness.drawn_text().empty());
}
