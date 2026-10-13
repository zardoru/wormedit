#include <wormedit/core/viewport_input.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit {
namespace {

constexpr double maximum_advance_seconds = 0.25;
constexpr double momentum_stop_speed = 0.5;

[[nodiscard]] auto bounded_setting(double value, double fallback, double minimum,
                                   double maximum) noexcept -> double {
    if (!std::isfinite(value))
        return fallback;
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] auto finite_delta(double value) noexcept -> double {
    return std::isfinite(value) ? value : 0.0;
}

[[nodiscard]] auto clamp_magnitude(double value, double maximum) noexcept -> double {
    if (!std::isfinite(value))
        return 0.0;
    return std::clamp(value, -maximum, maximum);
}

[[nodiscard]] auto direction_sign(bool natural_direction) noexcept -> double {
    return natural_direction ? 1.0 : -1.0;
}

} // namespace

viewport_input_controller_t::viewport_input_controller_t(
    viewport_input_settings_t settings) noexcept {
    set_settings(settings);
}

void viewport_input_controller_t::set_settings(viewport_input_settings_t settings) noexcept {
    settings_.natural_direction = settings.natural_direction;
    settings_.smooth_sensitivity = bounded_setting(settings.smooth_sensitivity, 1.0, 0.0001, 100.0);
    settings_.coarse_wheel_pixels_per_step =
        bounded_setting(settings.coarse_wheel_pixels_per_step, 48.0, 0.1, 10'000.0);
    settings_.zoom_sensitivity_per_pixel =
        bounded_setting(settings.zoom_sensitivity_per_pixel, 0.004, 0.000001, 1.0);
    settings_.momentum_friction_per_second =
        bounded_setting(settings.momentum_friction_per_second, 7.0, 0.01, 100.0);
    settings_.autoscroll_dead_zone_pixels =
        bounded_setting(settings.autoscroll_dead_zone_pixels, 8.0, 0.0, 10'000.0);
    settings_.autoscroll_speed_per_second_per_pixel =
        bounded_setting(settings.autoscroll_speed_per_second_per_pixel, 6.0, 0.0, 100.0);
    settings_.maximum_scroll_delta_pixels =
        bounded_setting(settings.maximum_scroll_delta_pixels, 10'000.0, 1.0, 1'000'000.0);
    settings_.maximum_momentum_speed_pixels_per_second = bounded_setting(
        settings.maximum_momentum_speed_pixels_per_second, 12'000.0, 1.0, 1'000'000.0);
}

auto viewport_input_controller_t::settings() const noexcept -> const viewport_input_settings_t& {
    return settings_;
}

auto viewport_input_controller_t::handle_wheel(const viewport_wheel_input_t& input) noexcept
    -> viewport_action_batch_t {
    viewport_action_batch_t batch;
    stop_momentum();
    cancel_autoscroll();
    const auto pane = routed_pane(input.pane);

    const auto scale = input.kind == viewport_wheel_kind_t::coarse_steps
                           ? settings_.coarse_wheel_pixels_per_step
                           : settings_.smooth_sensitivity;
    const auto sign = direction_sign(settings_.natural_direction);
    auto delta_x = finite_delta(input.delta_x) * scale * sign;
    auto delta_y = finite_delta(input.delta_y) * scale * sign;
    delta_x = clamp_magnitude(delta_x, settings_.maximum_scroll_delta_pixels);
    delta_y = clamp_magnitude(delta_y, settings_.maximum_scroll_delta_pixels);

    if (input.zoom_modifier) {
        if (delta_y == 0.0 || !std::isfinite(input.cursor_x_pixels) ||
            !std::isfinite(input.cursor_y_pixels))
            return batch;
        const auto exponent = std::clamp(-delta_y * settings_.zoom_sensitivity_per_pixel,
                                         -std::log(4.0), std::log(4.0));
        const auto factor = std::exp(exponent);
        batch.actions[batch.count++] = viewport_zoom_action_t{
            .pane = pane,
            .factor = factor,
            .cursor_x_pixels = input.cursor_x_pixels,
            .cursor_y_pixels = input.cursor_y_pixels,
        };
        return batch;
    }

    add_pane_scroll(batch, pane, delta_x, delta_y);
    return batch;
}

void viewport_input_controller_t::begin_trackpad(viewport_pane_t pane) noexcept {
    cancel_autoscroll();
    stop_momentum();
    routed_pane_ = pane;
    routed_gesture_ = routed_gesture_t::trackpad;
}

auto viewport_input_controller_t::handle_trackpad(double delta_x_pixels,
                                                  double delta_y_pixels) noexcept
    -> viewport_action_batch_t {
    viewport_action_batch_t batch;
    if (routed_gesture_ != routed_gesture_t::trackpad)
        return batch;

    const auto sign = direction_sign(settings_.natural_direction);
    const auto delta_x =
        clamp_magnitude(finite_delta(delta_x_pixels) * settings_.smooth_sensitivity * sign,
                        settings_.maximum_scroll_delta_pixels);
    const auto delta_y =
        clamp_magnitude(finite_delta(delta_y_pixels) * settings_.smooth_sensitivity * sign,
                        settings_.maximum_scroll_delta_pixels);
    add_pane_scroll(batch, routed_pane_, delta_x, delta_y);
    return batch;
}

void viewport_input_controller_t::end_trackpad() noexcept {
    if (routed_gesture_ == routed_gesture_t::trackpad)
        routed_gesture_ = routed_gesture_t::none;
    clear_route_if_idle();
}

void viewport_input_controller_t::begin_momentum(viewport_pane_t pane,
                                                 double velocity_x_pixels_per_second,
                                                 double velocity_y_pixels_per_second) noexcept {
    cancel_autoscroll();
    if (routed_gesture_ != routed_gesture_t::trackpad)
        routed_pane_ = pane;
    const auto sign = direction_sign(settings_.natural_direction);
    momentum_velocity_x_ = clamp_magnitude(finite_delta(velocity_x_pixels_per_second) * sign,
                                           settings_.maximum_momentum_speed_pixels_per_second);
    momentum_velocity_y_ = clamp_magnitude(finite_delta(velocity_y_pixels_per_second) * sign,
                                           settings_.maximum_momentum_speed_pixels_per_second);
    if (std::abs(momentum_velocity_x_) < momentum_stop_speed &&
        std::abs(momentum_velocity_y_) < momentum_stop_speed) {
        stop_momentum();
        return;
    }
    routed_gesture_ = routed_gesture_t::momentum;
}

void viewport_input_controller_t::middle_click(viewport_pane_t pane, double cursor_x_pixels,
                                               double cursor_y_pixels) noexcept {
    if (routed_gesture_ == routed_gesture_t::autoscroll) {
        cancel_autoscroll();
        return;
    }
    if (!std::isfinite(cursor_x_pixels) || !std::isfinite(cursor_y_pixels))
        return;

    stop_momentum();
    routed_pane_ = pane;
    routed_gesture_ = routed_gesture_t::autoscroll;
    autoscroll_anchor_x_ = cursor_x_pixels;
    autoscroll_anchor_y_ = cursor_y_pixels;
    autoscroll_pointer_x_ = cursor_x_pixels;
    autoscroll_pointer_y_ = cursor_y_pixels;
}

void viewport_input_controller_t::update_autoscroll_pointer(double cursor_x_pixels,
                                                            double cursor_y_pixels) noexcept {
    if (routed_gesture_ != routed_gesture_t::autoscroll || !std::isfinite(cursor_x_pixels) ||
        !std::isfinite(cursor_y_pixels))
        return;
    autoscroll_pointer_x_ = cursor_x_pixels;
    autoscroll_pointer_y_ = cursor_y_pixels;
}

auto viewport_input_controller_t::advance(double delta_seconds) noexcept
    -> viewport_action_batch_t {
    viewport_action_batch_t batch;
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0)
        return batch;
    const auto dt = std::min(delta_seconds, maximum_advance_seconds);

    if (routed_gesture_ == routed_gesture_t::autoscroll) {
        const auto sign = direction_sign(settings_.natural_direction);
        const auto auto_velocity = [this, sign](double pointer, double anchor) {
            const auto distance = pointer - anchor;
            const auto magnitude =
                std::max(0.0, std::abs(distance) - settings_.autoscroll_dead_zone_pixels);
            const auto velocity = std::copysign(
                magnitude * settings_.autoscroll_speed_per_second_per_pixel, distance);
            return clamp_magnitude(velocity * sign,
                                   settings_.maximum_momentum_speed_pixels_per_second);
        };
        const auto velocity_x = auto_velocity(autoscroll_pointer_x_, autoscroll_anchor_x_);
        const auto velocity_y = auto_velocity(autoscroll_pointer_y_, autoscroll_anchor_y_);
        add_pane_scroll(batch, routed_pane_, velocity_x * dt, velocity_y * dt);
        return batch;
    }

    if (routed_gesture_ != routed_gesture_t::momentum)
        return batch;

    const auto friction = settings_.momentum_friction_per_second;
    const auto decay = std::exp(-friction * dt);
    const auto displacement_fraction = (1.0 - decay) / friction;
    const auto delta_x = momentum_velocity_x_ * displacement_fraction;
    const auto delta_y = momentum_velocity_y_ * displacement_fraction;
    momentum_velocity_x_ *= decay;
    momentum_velocity_y_ *= decay;
    add_pane_scroll(batch, routed_pane_, delta_x, delta_y);

    if (std::abs(momentum_velocity_x_) < momentum_stop_speed &&
        std::abs(momentum_velocity_y_) < momentum_stop_speed)
        stop_momentum();
    return batch;
}

void viewport_input_controller_t::escape() noexcept {
    cancel_autoscroll();
    stop_momentum();
    routed_gesture_ = routed_gesture_t::none;
    clear_route_if_idle();
}

void viewport_input_controller_t::focus_lost() noexcept {
    escape();
}

void viewport_input_controller_t::modal_opened() noexcept {
    escape();
}

auto viewport_input_controller_t::is_normal_edit_press() noexcept -> bool {
    const auto was_active = is_autoscroll_active();
    if (was_active)
        cancel_autoscroll();
    stop_momentum();
    return was_active;
}

auto viewport_input_controller_t::is_autoscroll_active() const noexcept -> bool {
    return routed_gesture_ == routed_gesture_t::autoscroll;
}

auto viewport_input_controller_t::is_momentum_active() const noexcept -> bool {
    return routed_gesture_ == routed_gesture_t::momentum;
}

auto viewport_input_controller_t::is_trackpad_active() const noexcept -> bool {
    return routed_gesture_ == routed_gesture_t::trackpad;
}

auto viewport_input_controller_t::routed_pane(viewport_pane_t fallback) const noexcept
    -> viewport_pane_t {
    return routed_gesture_ == routed_gesture_t::none ? fallback : routed_pane_;
}

void viewport_input_controller_t::stop_momentum() noexcept {
    momentum_velocity_x_ = 0.0;
    momentum_velocity_y_ = 0.0;
    if (routed_gesture_ == routed_gesture_t::momentum)
        routed_gesture_ = routed_gesture_t::none;
    clear_route_if_idle();
}

void viewport_input_controller_t::cancel_autoscroll() noexcept {
    if (routed_gesture_ == routed_gesture_t::autoscroll)
        routed_gesture_ = routed_gesture_t::none;
    clear_route_if_idle();
}

void viewport_input_controller_t::clear_route_if_idle() noexcept {
    if (routed_gesture_ == routed_gesture_t::none)
        routed_pane_ = viewport_pane_t::chart;
}

// Over the BGM strip the horizontal component scrolls its columns, and the vertical component
// scrolls the chart in time exactly as it does over the field.
void viewport_input_controller_t::add_pane_scroll(viewport_action_batch_t& batch, viewport_pane_t pane,
                                                  double delta_x, double delta_y) const noexcept {
    if (pane == viewport_pane_t::bgm) {
        add_scroll(batch, viewport_pane_t::bgm, delta_x, 0.0);
        add_scroll(batch, viewport_pane_t::chart, 0.0, delta_y);
        return;
    }
    add_scroll(batch, pane, delta_x, delta_y);
}

void viewport_input_controller_t::add_scroll(viewport_action_batch_t& batch, viewport_pane_t pane,
                                             double delta_x, double delta_y) const noexcept {
    if (batch.count >= batch.actions.size())
        return;
    delta_x = clamp_magnitude(delta_x, settings_.maximum_scroll_delta_pixels);
    delta_y = clamp_magnitude(delta_y, settings_.maximum_scroll_delta_pixels);
    if (pane == viewport_pane_t::bgm)
        delta_y = 0.0;
    if (delta_x == 0.0 && delta_y == 0.0)
        return;
    batch.actions[batch.count++] = viewport_scroll_action_t{
        .pane = pane,
        .delta_x_pixels = delta_x,
        .delta_y_pixels = delta_y,
    };
}

} // namespace wormedit
