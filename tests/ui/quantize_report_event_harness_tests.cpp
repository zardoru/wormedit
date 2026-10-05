#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

namespace {

void seed_selection(WormUiComposeView& view) {
    view.project_id = 31;
    view.chart_id = 7;
    view.selected_note_count = 1;
}

} // namespace

TEST_CASE("the inspector hides the quantize Last applied report until an apply recorded rows",
          "[unit][frontend-state][event-harness][quantize-report]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    seed_selection(harness.view().compose);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Quantize (1)") != std::string::npos);
    CHECK(harness.drawn_text().find("Last applied report") == std::string::npos);
}

TEST_CASE("the quantize Last applied report opens with its moved rows",
          "[unit][frontend-state][event-harness][quantize-report]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    seed_selection(view);
    static const std::array<WormUiComposeQuantizeReportRowView, 1> rows{{
        {.object_id = 77, .lane_id = 1, .start_before_absolute_row = 250,
         .start_after_absolute_row = 240, .moved = true}}};
    view.quantize_applied_rows = rows.data();
    view.quantize_applied_row_count = view.quantize_applied_total_rows = rows.size();
    (void)harness.frame();
    // The report sits at the bottom of the quantize section; scroll the inspector to it.
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 1500.0f;
    motion.motion.y = 800.0f;
    (void)harness.frame(std::array{motion});
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.mouse_x = 1500.0f;
    wheel.wheel.mouse_y = 800.0f;
    wheel.wheel.y = -1.0f;
    for (int count = 0; count < 12; ++count) (void)harness.frame(std::array{wheel});
    CHECK(harness.drawn_text().find("Last applied report") != std::string::npos);
    CHECK(harness.drawn_text().find("Note 77") != std::string::npos);
}

TEST_CASE("the inspector no longer draws a Semantic layers section",
          "[unit][frontend-state][event-harness][inspector-sections]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    seed_selection(harness.view().compose);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Quantize (1)") != std::string::npos);
    CHECK(harness.drawn_text().find("Semantic layers") == std::string::npos);
    CHECK(harness.drawn_text().find("Select notes in layer") == std::string::npos);
}
