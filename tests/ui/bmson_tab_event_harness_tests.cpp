#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

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

auto motion_event(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
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

void seed_bmson_selection(wormedit::ui::compose_view_t& view, bool bmson) {
    view.project_id.value = 31;
    view.chart_id.value = 7;
    view.scene_available = true;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 620;
    view.keysounds_available = true;
    view.bmson_tools_available = bmson;
    view.selected_object_count = 1;
    view.selected_bmson_bgm_event_id.value = 5;
    view.selected_bmson_source_lane_id.value = 3;
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
    require_tab(state.dock_layout.sounds_dock_bmson_tab);
    require_tab(harness.drawn_text().find("Razor continue") != std::string::npos);

    const auto razor = harness.drawn_text_center("Razor continue");
    require_tab(razor.has_value());
    click_tab(harness, razor->first, razor->second);
    check_tab(state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::continuation);

    // A non-BMSON chart drops the tab and the dock shows Sounds again.
    seed_bmson_selection(harness.view().compose, false);
    (void)harness.frame();
    check_tab(!state.dock_layout.sounds_dock_bmson_tab);
    check_tab(harness.drawn_text().find("Razor continue") == std::string::npos);
}

TEST_CASE("razor mode is dropped when another tool activates, even with the BMSON tab hidden",
          "[unit][frontend-state][event-harness][bmson-tab]") {
    nuklear_event_harness_t harness{1280, 900};
    require_tab(harness.ready());
    seed_bmson_selection(harness.view().compose, true);
    (void)harness.frame();  // A new chart resets Compose state, so arm the razor afterwards.
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.sounds_dock_bmson_tab = false;  // The Sounds tab is showing, so the razor toggle is hidden.
    state.bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::continuation;
    (void)harness.frame();
    check_tab(state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::continuation);

    // Activating Tap from the palette or keyboard sets note paint mode in the view.
    harness.view().compose.note_paint_mode_active = true;
    (void)harness.frame();
    check_tab(state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off);

    // The same holds when the selected BGM event goes away.
    harness.view().compose.note_paint_mode_active = false;
    state.bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::restart;
    harness.view().compose.selected_bmson_bgm_event_id.value = 0;
    harness.view().compose.selected_bmson_source_lane_id.value = 0;
    harness.view().compose.selected_object_count = 0;
    (void)harness.frame();
    check_tab(state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off);
}

TEST_CASE("BMSON tab content scrolls to its lower actions when the dock is short",
          "[unit][frontend-state][event-harness][bmson-tab]") {
    nuklear_event_harness_t harness{1280, 560};
    require_tab(harness.ready());
    static const std::array<std::uint64_t, 1> lanes{0};
    static const std::array<std::uint8_t, 1> restarts{0};
    auto& view = harness.view().compose;
    seed_bmson_selection(view, true);
    view.staged_bmson_marker_restarts.resize(1);
    view.staged_bmson_marker_playable_lanes.assign(lanes.begin(), lanes.end());
    view.staged_bmson_marker_restarts.assign(restarts.begin(), restarts.end());
    (void)harness.frame();
    const auto tab = harness.drawn_text_center("BMSON");
    require_tab(tab.has_value());
    click_tab(harness, tab->first, tab->second);
    require_tab(worm_ui_nuklear_compose_state().dock_layout.sounds_dock_bmson_tab);
    const auto before = harness.drawn_text_center("Remove");
    // The staged-region actions sit below the visible part of the short dock.
    check_tab(!before.has_value());

    // Wheel over the dock content: the lower actions move up instead of staying clipped.
    (void)harness.frame(std::array{motion_event(tab->first, tab->second + 120.0f)});
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.mouse_x = tab->first;
    wheel.wheel.mouse_y = tab->second + 120.0f;
    wheel.wheel.y = -1.0f;
    for (int count = 0; count < 10; ++count) (void)harness.frame(std::array{wheel});
    const auto after = harness.drawn_text_center("Remove");
    check_tab(after.has_value());
}
