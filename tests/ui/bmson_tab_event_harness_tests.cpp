#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <source_location>

namespace {

void check_tab(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

void require_tab(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

void click_tab(nuklear_event_harness_t& harness, float x, float y) {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.clicks = 1;
    down.button.x = x;
    down.button.y = y;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    (void)harness.frame(std::array{motion, down});
    (void)harness.frame(std::array{motion, up});
    (void)harness.frame();
}

void seed_bmson_selection(WormUiComposeView& view, bool bmson) {
    view.project_id = 31;
    view.chart_id = 7;
    view.scene_available = true;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 620;
    view.keysounds_available = true;
    view.bmson_tools_available = bmson;
    view.selected_object_count = 1;
    view.selected_bmson_bgm_event_id = 5;
    view.selected_bmson_source_lane_id = 3;
}

} // namespace

TEST_CASE("the Sounds dock offers a BMSON tab only for BMSON charts",
          "[unit][frontend-state][event-harness][bmson-tab]") {
    nuklear_event_harness_t harness{1280, 900};
    require_tab(harness.ready());
    seed_bmson_selection(harness.view().compose, false);
    (void)harness.frame();
    check_tab(harness.drawn_text().find("Razor continue") == std::string::npos);
    check_tab(harness.drawn_text_center("BMSON") == std::nullopt);

    seed_bmson_selection(harness.view().compose, true);
    (void)harness.frame();
    check_tab(harness.drawn_text_center("BMSON").has_value());
    // The Sounds tab is selected by default, so the BMSON controls are not drawn yet.
    check_tab(harness.drawn_text().find("Razor continue") == std::string::npos);
}

TEST_CASE("the razor toggle works from the BMSON tab and the tab falls back on other charts",
          "[unit][frontend-state][event-harness][bmson-tab]") {
    nuklear_event_harness_t harness{1280, 900};
    require_tab(harness.ready());
    seed_bmson_selection(harness.view().compose, true);
    (void)harness.frame();
    const auto tab = harness.drawn_text_center("BMSON");
    require_tab(tab.has_value());
    click_tab(harness, tab->first, tab->second);
    auto& state = worm_ui_nuklear_compose_state();
    require_tab(state.sounds_dock_bmson_tab);
    require_tab(harness.drawn_text().find("Razor continue") != std::string::npos);

    const auto razor = harness.drawn_text_center("Razor continue");
    require_tab(razor.has_value());
    click_tab(harness, razor->first, razor->second);
    check_tab(state.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::continuation);

    // A non-BMSON chart drops the tab and the dock shows Sounds again.
    seed_bmson_selection(harness.view().compose, false);
    (void)harness.frame();
    check_tab(!state.sounds_dock_bmson_tab);
    check_tab(harness.drawn_text().find("Razor continue") == std::string::npos);
}
