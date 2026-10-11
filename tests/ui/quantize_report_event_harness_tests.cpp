#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
    const auto bgm = GENERATE(false, true);
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    seed_selection(view);
    const std::array<WormUiComposeQuantizeReportRowView, 1> rows{{
        {.object_id = 77, .lane_id = 1, .start_before_absolute_row = 250,
         .start_after_absolute_row = 240, .moved = true, .bgm = bgm}}};
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
    CHECK(harness.drawn_text().find(bgm ? "BGM 77" : "Note 77") != std::string::npos);
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

TEST_CASE("a BGM-only selection enables quantize",
          "[unit][frontend-state][event-harness][quantize-report][mixed-quantize]") {
    nuklear_event_harness_t harness{1600, 1200};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    seed_selection(view);
    view.selected_note_count = 0;
    view.selected_bgm_event_count = 1;
    view.quantize_divisions = 4;
    view.quantize_preview_moved = 1;
    static const std::array<WormUiComposeQuantizeReportRowView, 1> rows{{
        {.object_id = 78, .lane_id = 9, .start_before_absolute_row = 250,
         .start_after_absolute_row = 240, .moved = true, .bgm = true}}};
    view.quantize_preview_rows = rows.data();
    view.quantize_preview_row_count = view.quantize_preview_total_rows = rows.size();
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Quantize (1)") != std::string::npos);
    const auto center = harness.drawn_text_center("Quantize");
    REQUIRE(center.has_value());
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.clicks = 1;
    down.button.x = center->first;
    down.button.y = center->second;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = center->first;
    motion.motion.y = center->second;
    const auto pressed = harness.frame(std::array{motion, down});
    const auto applied = harness.frame(std::array{motion, up});
    CHECK((pressed.get().compose_action == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE ||
           applied.get().compose_action == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE));
}
