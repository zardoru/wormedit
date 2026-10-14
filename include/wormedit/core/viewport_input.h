#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <variant>

namespace wormedit {

// These pane identifiers describe viewport destinations only. They are not
// document objects and viewport actions never modify the chart.
enum class viewport_pane_t {
    chart,
    bgm,
};

struct viewport_scroll_action_t {
    viewport_pane_t pane{viewport_pane_t::chart};
    double delta_x_pixels{};
    double delta_y_pixels{};
};

struct viewport_zoom_action_t {
    viewport_pane_t pane{viewport_pane_t::chart};
    // A factor above one zooms in. The frontend should keep the chart point
    // under (cursor_x_pixels, cursor_y_pixels) stationary on screen.
    double factor{1.0};
    double cursor_x_pixels{};
    double cursor_y_pixels{};
};

using viewport_action_t = std::variant<viewport_scroll_action_t, viewport_zoom_action_t>;

inline constexpr std::size_t maximum_viewport_actions_per_input = 4;

struct viewport_action_batch_t {
    std::array<viewport_action_t, maximum_viewport_actions_per_input> actions{};
    std::size_t count{};

    [[nodiscard]] auto is_empty() const noexcept -> bool {
        return count == 0;
    }
    [[nodiscard]] auto operator[](std::size_t index) const noexcept -> const viewport_action_t& {
        return actions[index];
    }
};

struct viewport_input_settings_t {
    // Input deltas are interpreted as content-offset deltas when this is true
    // and reversed when false. This makes natural scrolling a user preference
    // rather than a frontend/backend convention.
    bool natural_direction{true};
    double smooth_sensitivity{1.0};
    double coarse_wheel_pixels_per_step{48.0};
    double zoom_sensitivity_per_pixel{0.004};
    double momentum_friction_per_second{7.0};
    double autoscroll_dead_zone_pixels{8.0};
    double autoscroll_speed_per_second_per_pixel{6.0};
    double maximum_scroll_delta_pixels{10'000.0};
    double maximum_momentum_speed_pixels_per_second{12'000.0};
};

enum class viewport_wheel_kind_t {
    coarse_steps,
    smooth_pixels,
};

struct viewport_wheel_input_t {
    viewport_pane_t pane{viewport_pane_t::chart};
    viewport_wheel_kind_t kind{viewport_wheel_kind_t::smooth_pixels};
    double delta_x{};
    double delta_y{};
    bool zoom_modifier{};
    double cursor_x_pixels{};
    double cursor_y_pixels{};
};

// A backend-neutral input adapter for viewport navigation. Gesture routing is
// captured at begin_trackpad()/begin_momentum()/middle_click() and remains
// stable until the gesture ends or is canceled.
class viewport_input_controller_t {
  public:
    explicit viewport_input_controller_t(viewport_input_settings_t settings = {}) noexcept;

    void set_settings(viewport_input_settings_t settings) noexcept;
    [[nodiscard]] auto settings() const noexcept -> const viewport_input_settings_t&;

    [[nodiscard]] auto handle_wheel(const viewport_wheel_input_t& input) noexcept
        -> viewport_action_batch_t;

    void begin_trackpad(viewport_pane_t pane) noexcept;
    [[nodiscard]] auto handle_trackpad(double delta_x_pixels, double delta_y_pixels) noexcept
        -> viewport_action_batch_t;
    void end_trackpad() noexcept;

    // Momentum uses pixels/second and is advanced in deterministic caller
    // supplied time steps. Invalid velocities are treated as zero. Each step
    // is capped to keep delayed frontend frames from producing huge jumps.
    void begin_momentum(viewport_pane_t pane, double velocity_x_pixels_per_second,
                        double velocity_y_pixels_per_second) noexcept;

    void middle_click(viewport_pane_t pane, double cursor_x_pixels,
                      double cursor_y_pixels) noexcept;
    void update_autoscroll_pointer(double cursor_x_pixels, double cursor_y_pixels) noexcept;

    // Advances active momentum/autoscroll. Negative, nonfinite and zero time
    // steps produce no actions. Large steps are bounded internally.
    [[nodiscard]] auto advance(double delta_seconds) noexcept -> viewport_action_batch_t;

    void escape() noexcept;
    void focus_lost() noexcept;
    void modal_opened() noexcept;
    // Call before dispatching an ordinary edit press. Returns true if a latched
    // autoscroll was canceled and the press can then be handled normally.
    [[nodiscard]] auto is_normal_edit_press() noexcept -> bool;
    // Compatibility for the coordinated sys/UI migration.
    inline auto normal_edit_press() noexcept -> bool { return is_normal_edit_press(); }

    [[nodiscard]] auto is_autoscroll_active() const noexcept -> bool;
    // Compatibility for the coordinated sys/UI migration.
    inline auto autoscroll_active() const noexcept -> bool { return is_autoscroll_active(); }
    [[nodiscard]] auto is_momentum_active() const noexcept -> bool;
    [[nodiscard]] auto is_trackpad_active() const noexcept -> bool;


  private:
    enum class routed_gesture_t {
        none,
        trackpad,
        momentum,
        autoscroll,
    };

    [[nodiscard]] auto routed_pane(viewport_pane_t fallback) const noexcept -> viewport_pane_t;
    void stop_momentum() noexcept;
    void cancel_autoscroll() noexcept;
    void clear_route_if_idle() noexcept;
    void add_pane_scroll(viewport_action_batch_t& batch, viewport_pane_t pane, double delta_x,
                         double delta_y) const noexcept;
    void add_scroll(viewport_action_batch_t& batch, viewport_pane_t pane, double delta_x,
                    double delta_y) const noexcept;

    viewport_input_settings_t settings_{};
    routed_gesture_t routed_gesture_{routed_gesture_t::none};
    viewport_pane_t routed_pane_{viewport_pane_t::chart};
    double momentum_velocity_x_{};
    double momentum_velocity_y_{};
    double autoscroll_anchor_x_{};
    double autoscroll_anchor_y_{};
    double autoscroll_pointer_x_{};
    double autoscroll_pointer_y_{};
};

} // namespace wormedit
