#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string>

namespace {
void require_resize(bool value) { REQUIRE(value); }
void check_resize(bool value) { CHECK(value); }

auto resize_motion(float x, float y) -> SDL_Event {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x; e.motion.y = y; e.motion.state = SDL_BUTTON_LMASK;
    return e;
}
auto resize_button(float x, float y, bool down) -> SDL_Event {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down;
    e.button.x = x; e.button.y = y;
    return e;
}
auto resize_key(SDL_Keycode key) -> SDL_Event {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key; e.key.down = true;
    return e;
}

struct resize_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    WormUiComposeMeasureRowView row{.measure_id = 77, .number = 1, .length_numerator = 1,
                                    .length_denominator = 1, .has_meter = true,
                                    .meter_numerator = 4, .meter_denominator = 4};
    std::uint64_t id{77};
    std::array<WormUiComposeGridLineView, 2> lines{{{.y_pixels = 100, .kind = 3, .measure_number = 1},
                                                    {.y_pixels = 500, .kind = 3, .measure_number = 2}}};
    float x{};
    float y_line{};
    float pixels_per_view_pixel{};

    resize_fixture_t() {
        require_resize(harness.ready());
        auto& v = harness.view().compose;
        v.scene_available = true;
        v.measure_rows = &row; v.measure_row_ids = &id; v.measure_row_count = 1;
        v.grid_lines = lines.data(); v.grid_line_count = lines.size();
        v.pixels_per_standard_measure = 400.0;
        v.area_snap_denominator = 8; v.edit_snap_enabled = true;
        (void)harness.frame();
        WormUiTestingRect rect{};
        require_resize(worm_ui_frontend_compose_viewport_for_testing(&rect));
        x = rect.x + static_cast<float>(200 * rect.width / v.viewport_width_pixels);
        pixels_per_view_pixel = static_cast<float>(rect.height / v.viewport_height_pixels);
        y_line = rect.y + static_cast<float>(500 * pixels_per_view_pixel);
        worm_ui_nuklear_compose_state().measure_resize_active = false;
    }
    auto screen_y(float view_offset) const { return y_line + view_offset * pixels_per_view_pixel; }
};
} // namespace

TEST_CASE("Alt+drag of a bar line sets the measure meter on release",
          "[frontend-state][event-harness][measure-resize]") {
    resize_fixture_t f;
    const std::array press{resize_motion(f.x, f.y_line), resize_button(f.x, f.y_line, true)};
    (void)f.harness.frame(press, SDL_KMOD_LALT);
    auto& state = worm_ui_nuklear_compose_state();
    require_resize(state.measure_resize_active);
    const std::array drag{resize_motion(f.x, f.screen_y(-50))};
    (void)f.harness.frame(drag, SDL_KMOD_LALT);
    check_resize(std::string(state.measure_resize_label.data()) == "7/8");
    const std::array release{resize_motion(f.x, f.screen_y(-50)), resize_button(f.x, f.screen_y(-50), false)};
    const auto action = f.harness.frame(release, SDL_KMOD_LALT);
    require_resize(action.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER);
    check_resize(action.get().compose_measure_id_count == 1);
    check_resize(action.get().compose_measure_ids[0] == 77);
    check_resize(std::string(action.get().timing_meter_numerator_text) == "7");
    check_resize(std::string(action.get().timing_meter_denominator_text) == "8");
    check_resize(!state.measure_resize_active);
}

TEST_CASE("Alt+drag of a bar line can lengthen the measure",
          "[frontend-state][event-harness][measure-resize]") {
    resize_fixture_t f;
    const std::array press{resize_motion(f.x, f.y_line), resize_button(f.x, f.y_line, true)};
    (void)f.harness.frame(press, SDL_KMOD_LALT);
    const std::array drag{resize_motion(f.x, f.screen_y(100))};
    (void)f.harness.frame(drag, SDL_KMOD_LALT);
    const std::array release{resize_motion(f.x, f.screen_y(100)), resize_button(f.x, f.screen_y(100), false)};
    const auto action = f.harness.frame(release, SDL_KMOD_LALT);
    require_resize(action.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER);
    check_resize(std::string(action.get().timing_meter_numerator_text) == "5");
    check_resize(std::string(action.get().timing_meter_denominator_text) == "4");
}

TEST_CASE("Escape cancels an Alt+drag of a bar line", "[frontend-state][event-harness][measure-resize]") {
    resize_fixture_t f;
    const std::array press{resize_motion(f.x, f.y_line), resize_button(f.x, f.y_line, true)};
    (void)f.harness.frame(press, SDL_KMOD_LALT);
    auto& state = worm_ui_nuklear_compose_state();
    require_resize(state.measure_resize_active);
    const std::array escape{resize_key(SDLK_ESCAPE)};
    (void)f.harness.frame(escape, SDL_KMOD_LALT);
    check_resize(!state.measure_resize_active);
    const std::array release{resize_motion(f.x, f.screen_y(-50)), resize_button(f.x, f.screen_y(-50), false)};
    const auto action = f.harness.frame(release, SDL_KMOD_LALT);
    check_resize(action.get().compose_action != WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER);
}

TEST_CASE("Alt press away from a bar line does not start a resize",
          "[frontend-state][event-harness][measure-resize]") {
    resize_fixture_t f;
    const std::array press{resize_motion(f.x, f.screen_y(-120)), resize_button(f.x, f.screen_y(-120), true)};
    (void)f.harness.frame(press, SDL_KMOD_LALT);
    check_resize(!worm_ui_nuklear_compose_state().measure_resize_active);
}
