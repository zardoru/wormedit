#include <wormedit/core/viewport_input.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <variant>

using namespace wormedit;

namespace {

auto scroll_at(const viewport_action_batch_t& batch, std::size_t index = 0)
    -> const viewport_scroll_action_t& {
    return std::get<viewport_scroll_action_t>(batch[index]);
}

auto zoom_at(const viewport_action_batch_t& batch, std::size_t index = 0)
    -> const viewport_zoom_action_t& {
    return std::get<viewport_zoom_action_t>(batch[index]);
}

} // namespace

TEST_CASE("viewport wheel and trackpad preserve smooth fractional motion",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller;
    controller.begin_trackpad(viewport_pane_t::chart);

    double accumulated_x{};
    double accumulated_y{};
    for (int index = 0; index < 5; ++index) {
        const auto actions = controller.handle_trackpad(0.125, -0.375);
        REQUIRE(actions.count == 1);
        const auto& scroll = scroll_at(actions);
        accumulated_x += scroll.delta_x_pixels;
        accumulated_y += scroll.delta_y_pixels;
    }
    CHECK(accumulated_x == Catch::Approx(0.625));
    CHECK(accumulated_y == Catch::Approx(-1.875));

    controller.end_trackpad();
    const auto coarse = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::coarse_steps,
        .delta_x = 0.5,
        .delta_y = -1.0,
    });
    REQUIRE(coarse.count == 1);
    CHECK(scroll_at(coarse).delta_x_pixels == 24.0);
    CHECK(scroll_at(coarse).delta_y_pixels == -48.0);
}

TEST_CASE("viewport input settings reverse direction and scale both input kinds",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller({
        .natural_direction = false,
        .smooth_sensitivity = 2.0,
        .coarse_wheel_pixels_per_step = 30.0,
    });

    const auto smooth = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_x = 1.25,
        .delta_y = -2.5,
    });
    REQUIRE(smooth.count == 1);
    CHECK(scroll_at(smooth).delta_x_pixels == -2.5);
    CHECK(scroll_at(smooth).delta_y_pixels == 5.0);

    const auto coarse = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::coarse_steps,
        .delta_y = 1.0,
    });
    REQUIRE(coarse.count == 1);
    CHECK(scroll_at(coarse).delta_y_pixels == -30.0);
}

TEST_CASE("BGM wheel input scrolls time vertically and the columns horizontally",
          "[unit][core][viewport-input][bgm]") {
    viewport_input_controller_t controller;
    const auto vertical = controller.handle_wheel({
        .pane = viewport_pane_t::bgm,
        .kind = viewport_wheel_kind_t::coarse_steps,
        .delta_y = -1.0,
    });
    // A plain wheel over the BGM lanes scrolls the chart in time, as it does over the field.
    REQUIRE(vertical.count == 1);
    CHECK(scroll_at(vertical).pane == viewport_pane_t::chart);
    CHECK(scroll_at(vertical).delta_x_pixels == 0.0);
    CHECK(scroll_at(vertical).delta_y_pixels == -48.0);

    const auto horizontal = controller.handle_wheel({
        .pane = viewport_pane_t::bgm,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_x = 2.5,
        .delta_y = 9.0,
    });
    // Sideways motion scrolls the columns while the vertical part still scrolls time.
    REQUIRE(horizontal.count == 2);
    CHECK(scroll_at(horizontal).pane == viewport_pane_t::bgm);
    CHECK(scroll_at(horizontal).delta_x_pixels == 2.5);
    CHECK(scroll_at(horizontal).delta_y_pixels == 0.0);
    CHECK(scroll_at(horizontal, 1).pane == viewport_pane_t::chart);
    CHECK(scroll_at(horizontal, 1).delta_y_pixels == 9.0);

    const auto zoom = controller.handle_wheel({
        .pane = viewport_pane_t::bgm,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_y = -10.0,
        .zoom_modifier = true,
        .cursor_x_pixels = 300.0,
        .cursor_y_pixels = 75.0,
    });
    REQUIRE(zoom.count == 1);
    CHECK(zoom_at(zoom).pane == viewport_pane_t::bgm);
    CHECK(zoom_at(zoom).cursor_y_pixels == 75.0);
}

TEST_CASE("BGM middle-click autoscroll moves both axes",
          "[unit][core][viewport-input][bgm]") {
    viewport_input_controller_t controller({
        .autoscroll_dead_zone_pixels = 0.0,
        .autoscroll_speed_per_second_per_pixel = 2.0,
    });
    controller.middle_click(viewport_pane_t::bgm, 100.0, 100.0);
    controller.update_autoscroll_pointer(110.0, 70.0);
    const auto movement = controller.advance(0.1);
    REQUIRE(movement.count == 2);
    CHECK(scroll_at(movement).pane == viewport_pane_t::bgm);
    CHECK(scroll_at(movement).delta_x_pixels == 2.0);
    CHECK(scroll_at(movement).delta_y_pixels == 0.0);
    CHECK(scroll_at(movement, 1).pane == viewport_pane_t::chart);
    CHECK(scroll_at(movement, 1).delta_y_pixels != 0.0);
}

TEST_CASE("trackpad and momentum keep their starting pane while pointer routing changes",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller;
    controller.begin_trackpad(viewport_pane_t::bgm);
    const auto trackpad = controller.handle_trackpad(2.5, 4.0);
    REQUIRE(trackpad.count == 2);
    CHECK(scroll_at(trackpad).pane == viewport_pane_t::bgm);
    CHECK(scroll_at(trackpad).delta_x_pixels == 2.5);
    CHECK(scroll_at(trackpad).delta_y_pixels == 0.0);
    CHECK(scroll_at(trackpad, 1).pane == viewport_pane_t::chart);
    CHECK(scroll_at(trackpad, 1).delta_y_pixels == 4.0);

    controller.begin_momentum(viewport_pane_t::chart, 100.0, 80.0);
    REQUIRE(controller.is_momentum_active());
    const auto momentum = controller.advance(0.1);
    REQUIRE(momentum.count == 2);
    CHECK(scroll_at(momentum).pane == viewport_pane_t::bgm);
    CHECK(scroll_at(momentum).delta_y_pixels == 0.0);
    CHECK(scroll_at(momentum).delta_x_pixels ==
          Catch::Approx(100.0 * (1.0 - std::exp(-0.7)) / 7.0));
    CHECK(scroll_at(momentum, 1).pane == viewport_pane_t::chart);
    CHECK(scroll_at(momentum, 1).delta_y_pixels ==
          Catch::Approx(80.0 * (1.0 - std::exp(-0.7)) / 7.0));

    const auto interrupting = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_y = 1.0,
    });
    REQUIRE(interrupting.count == 1);
    CHECK(scroll_at(interrupting).pane == viewport_pane_t::chart);
    CHECK_FALSE(controller.is_momentum_active());
    CHECK(controller.advance(0.1).is_empty());
}

TEST_CASE("zoom requests are cursor anchored and bounded", "[unit][core][viewport-input]") {
    viewport_input_controller_t controller;
    const auto actions = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_y = -100.0,
        .zoom_modifier = true,
        .cursor_x_pixels = 31.5,
        .cursor_y_pixels = 82.25,
    });
    REQUIRE(actions.count == 1);
    CHECK(zoom_at(actions).factor > 1.0);
    CHECK(zoom_at(actions).factor <= 4.0);
    CHECK(zoom_at(actions).cursor_x_pixels == 31.5);
    CHECK(zoom_at(actions).cursor_y_pixels == 82.25);

    const auto invalid_cursor = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_y = 1.0,
        .zoom_modifier = true,
        .cursor_x_pixels = std::numeric_limits<double>::quiet_NaN(),
        .cursor_y_pixels = 1.0,
    });
    CHECK(invalid_cursor.is_empty());
}

TEST_CASE("latched autoscroll emits two-axis movement and toggles off on middle click",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller({
        .autoscroll_dead_zone_pixels = 0.0,
        .autoscroll_speed_per_second_per_pixel = 2.0,
    });
    controller.middle_click(viewport_pane_t::chart, 100.0, 100.0);
    REQUIRE(controller.is_autoscroll_active());
    controller.update_autoscroll_pointer(110.0, 80.0);
    const auto movement = controller.advance(0.1);
    REQUIRE(movement.count == 1);
    CHECK(scroll_at(movement).pane == viewport_pane_t::chart);
    CHECK(scroll_at(movement).delta_x_pixels == 2.0);
    CHECK(scroll_at(movement).delta_y_pixels == -4.0);

    controller.middle_click(viewport_pane_t::chart, 500.0, 500.0);
    CHECK_FALSE(controller.is_autoscroll_active());
    CHECK(controller.advance(0.1).is_empty());
}

TEST_CASE("autoscroll cancels on escape, focus loss, modal opening, and edit press",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller;
    controller.middle_click(viewport_pane_t::chart, 10.0, 10.0);
    controller.escape();
    CHECK_FALSE(controller.is_autoscroll_active());

    controller.middle_click(viewport_pane_t::chart, 10.0, 10.0);
    controller.focus_lost();
    CHECK_FALSE(controller.is_autoscroll_active());

    controller.middle_click(viewport_pane_t::chart, 10.0, 10.0);
    controller.modal_opened();
    CHECK_FALSE(controller.is_autoscroll_active());

    controller.middle_click(viewport_pane_t::chart, 10.0, 10.0);
    CHECK(controller.is_normal_edit_press());
    CHECK_FALSE(controller.is_autoscroll_active());
    CHECK_FALSE(controller.is_normal_edit_press());
}

TEST_CASE("invalid deltas, velocities, cursors, settings, and time steps stay bounded",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller({
        .smooth_sensitivity = std::numeric_limits<double>::infinity(),
        .coarse_wheel_pixels_per_step = -1.0,
        .maximum_scroll_delta_pixels = std::numeric_limits<double>::quiet_NaN(),
    });
    CHECK(controller.settings().smooth_sensitivity == 1.0);
    CHECK(controller.settings().coarse_wheel_pixels_per_step == 0.1);
    CHECK(controller.settings().maximum_scroll_delta_pixels == 10'000.0);

    const auto invalid = controller.handle_wheel({
        .pane = viewport_pane_t::chart,
        .kind = viewport_wheel_kind_t::smooth_pixels,
        .delta_x = std::numeric_limits<double>::infinity(),
        .delta_y = std::numeric_limits<double>::quiet_NaN(),
    });
    CHECK(invalid.is_empty());

    controller.begin_momentum(viewport_pane_t::chart, std::numeric_limits<double>::infinity(),
                              100.0);
    CHECK(controller.is_momentum_active());
    CHECK(controller.advance(std::numeric_limits<double>::quiet_NaN()).is_empty());
    const auto huge_step = controller.advance(10'000.0);
    REQUIRE(huge_step.count == 1);
    CHECK(std::abs(scroll_at(huge_step).delta_y_pixels) <=
          controller.settings().maximum_scroll_delta_pixels);
}

TEST_CASE("invalid middle click and pointer positions cannot poison autoscroll",
          "[unit][core][viewport-input]") {
    viewport_input_controller_t controller;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    controller.middle_click(viewport_pane_t::chart, nan, 10.0);
    CHECK_FALSE(controller.is_autoscroll_active());

    controller.middle_click(viewport_pane_t::chart, 10.0, 10.0);
    controller.update_autoscroll_pointer(nan, std::numeric_limits<double>::infinity());
    CHECK(controller.advance(0.1).is_empty());
}
