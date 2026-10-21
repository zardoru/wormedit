#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <wormedit/core/measure.h>
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
    std::array<wormedit::ui::compose_measure_row_view_t, 2> rows{{
        {.measure_index = 77, .number = 1, .length_numerator = 1, .length_denominator = 1,
         .has_meter = true, .meter_numerator = 4, .meter_denominator = 4},
        {.measure_index = 78, .number = 2, .length_numerator = 1, .length_denominator = 1,
         .has_meter = true, .meter_numerator = 4, .meter_denominator = 4}}};
    std::array<std::uint64_t, 2> ids{77, 78};
    std::array<wormedit::ui::compose_grid_line_view_t, 2> lines{{{.y_pixels = 100, .kind = wormedit::editor_grid_line_kind_t::measure, .measure_number = 1},
                                                    {.y_pixels = 500, .kind = wormedit::editor_grid_line_kind_t::measure, .measure_number = 2}}};
    float x{};
    float y_line{};
    float pixels_per_view_pixel{};

    resize_fixture_t() {
        require_resize(harness.ready());
        auto& v = harness.view().compose;
        v.scene_available = true;
        v.measure_rows.assign(rows.begin(), rows.end()); v.measure_row_ids.assign(ids.begin(), ids.end()); v.measure_rows.resize(rows.size());
        v.grid_lines.assign(lines.data(), lines.data() + lines.size());
        v.pixels_per_standard_measure = 400.0;
        v.area_rows_per_measure = 192; v.area_snap_denominator = 8; v.edit_snap_enabled = true;
        (void)harness.frame();
        wormedit::ui::testing_rect_view_t rect{};
        require_resize(worm_ui_frontend_compose_viewport_for_testing(&rect));
        x = rect.x + static_cast<float>(200 * rect.width / v.viewport_width_pixels);
        pixels_per_view_pixel = static_cast<float>(rect.height / v.viewport_height_pixels);
        y_line = rect.y + static_cast<float>(500 * pixels_per_view_pixel);
        worm_ui_nuklear_compose_state().measure_resize.measure_resize_active = false;
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
    require_resize(state.measure_resize.measure_resize_active);
    const std::array drag{resize_motion(f.x, f.screen_y(-50))};
    (void)f.harness.frame(drag, SDL_KMOD_LALT);
    check_resize(std::string(state.measure_resize.measure_resize_label.data()) == "7/8");
    const std::array release{resize_motion(f.x, f.screen_y(-50)), resize_button(f.x, f.screen_y(-50), false)};
    const auto action = f.harness.frame(release, SDL_KMOD_LALT);
    require_resize(action.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER);
    check_resize(action.get().compose_measure_ids.size() == 1);
    check_resize(action.get().compose_measure_ids[0] == 77);
    check_resize(std::string(action.get().timing_meter_numerator_text) == "7");
    check_resize(std::string(action.get().timing_meter_denominator_text) == "8");
    check_resize(!state.measure_resize.measure_resize_active);
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
    require_resize(state.measure_resize.measure_resize_active);
    const std::array escape{resize_key(SDLK_ESCAPE)};
    (void)f.harness.frame(escape, SDL_KMOD_LALT);
    check_resize(!state.measure_resize.measure_resize_active);
    const std::array release{resize_motion(f.x, f.screen_y(-50)), resize_button(f.x, f.screen_y(-50), false)};
    const auto action = f.harness.frame(release, SDL_KMOD_LALT);
    check_resize(action.get().compose_action != WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER);
}

TEST_CASE("Alt press away from a bar line does not start a resize",
          "[frontend-state][event-harness][measure-resize]") {
    resize_fixture_t f;
    const std::array press{resize_motion(f.x, f.screen_y(-120)), resize_button(f.x, f.screen_y(-120), true)};
    (void)f.harness.frame(press, SDL_KMOD_LALT);
    check_resize(!worm_ui_nuklear_compose_state().measure_resize.measure_resize_active);
}

TEST_CASE("Shift+Alt drag moves only the bar line and trades length with the next measure",
          "[frontend-state][event-harness][measure-resize]") {
    resize_fixture_t f;
    const auto both = static_cast<SDL_Keymod>(SDL_KMOD_LALT | SDL_KMOD_LSHIFT);
    const std::array press{resize_motion(f.x, f.y_line), resize_button(f.x, f.y_line, true)};
    (void)f.harness.frame(press, both);
    auto& state = worm_ui_nuklear_compose_state();
    require_resize(state.measure_resize.measure_resize_active);
    check_resize(state.measure_resize.measure_resize_move_line);
    const std::array drag{resize_motion(f.x, f.screen_y(-50))};
    (void)f.harness.frame(drag, both);
    check_resize(std::string(state.measure_resize.measure_resize_label.data()) == "7/8 | 9/8");
    const std::array release{resize_motion(f.x, f.screen_y(-50)), resize_button(f.x, f.screen_y(-50), false)};
    const auto action = f.harness.frame(release, both);
    require_resize(action.get().compose_action == WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE);
    check_resize(action.get().compose_measure_ids[0] == 77);
    check_resize(std::string(action.get().timing_meter_numerator_text) == "7");
    check_resize(std::string(action.get().timing_meter_denominator_text) == "8");
    check_resize(action.get().compose_second_meter_numerator == 9);
    check_resize(action.get().compose_second_meter_denominator == 8);
}

TEST_CASE("Measure resize equality does not wrap 64 bit cross products",
          "[frontend-state][event-harness][shared-value]") {
    resize_fixture_t fixture;
    const auto numerator = (std::uint64_t{5} << 60) + 18;
    const auto denominator = (std::uint64_t{1} << 62) + 8;
    const auto measure = wormedit::measure_definition_t{
        .length = {numerator, denominator}, .meter = std::nullopt};
    require_resize(wormedit::validate_measure(denominator, measure).row_capacity == numerator);
    fixture.rows[0].length_numerator = numerator;
    fixture.rows[0].length_denominator = denominator;
    fixture.rows[0].has_meter = false;
    fixture.harness.view().compose.measure_rows.assign(fixture.rows.begin(), fixture.rows.end());
    fixture.harness.view().compose.grid_lines[0].y_pixels = 375;
    fixture.harness.view().compose.area_rows_per_measure = denominator;
    fixture.harness.view().compose.pixels_per_standard_measure = 100;
    const std::array press{resize_motion(fixture.x, fixture.y_line),
                           resize_button(fixture.x, fixture.y_line, true)};
    (void)fixture.harness.frame(press, SDL_KMOD_LALT);
    require_resize(worm_ui_nuklear_compose_state().measure_resize.measure_resize_active);
    const std::array release{resize_motion(fixture.x, fixture.screen_y(100)),
                             resize_button(fixture.x, fixture.screen_y(100), false)};
    const auto action = fixture.harness.frame(release, SDL_KMOD_LALT);
    check_resize(action.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER);
    check_resize(std::string(action.get().timing_meter_numerator_text) == "9");
    check_resize(std::string(action.get().timing_meter_denominator_text) == "4");
}

TEST_CASE("A bar line drag starts when it is the only bar line on screen",
          "[frontend-state][event-harness][measure-resize]") {
    // At a deep zoom the neighboring bar lines are off screen; the view's scroll direction
    // still tells which way the measure grows.
    resize_fixture_t f;
    f.lines[0] = f.lines[1];
    auto& view = f.harness.view().compose;
    view.grid_lines = {f.lines[0]};
    const auto both = static_cast<SDL_Keymod>(SDL_KMOD_LALT | SDL_KMOD_LSHIFT);
    const std::array press{resize_motion(f.x, f.y_line), resize_button(f.x, f.y_line, true)};
    (void)f.harness.frame(press, both);
    auto& state = worm_ui_nuklear_compose_state();
    require_resize(state.measure_resize.measure_resize_active);
    const std::array drag{resize_motion(f.x, f.screen_y(-50))};
    (void)f.harness.frame(drag, both);
    check_resize(std::string(state.measure_resize.measure_resize_label.data()) == "7/8 | 9/8");
    const std::array release{resize_motion(f.x, f.screen_y(-50)), resize_button(f.x, f.screen_y(-50), false)};
    const auto action = f.harness.frame(release, both);
    check_resize(action.get().compose_action == WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE);
}
