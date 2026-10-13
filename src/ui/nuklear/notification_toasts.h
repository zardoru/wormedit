#pragma once
#include "ui/field/rect.h"

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wormedit::ui::nuklear {

struct notification_rect_t {
    float x{};
    float y{};
    float width{};
    float height{};
};

inline auto notification_contains(notification_rect_t bounds, float x, float y) -> bool {
    return bounds.width > 0 && bounds.height > 0 &&
           is_point_in_rect({bounds.x, bounds.y, bounds.width, bounds.height}, x, y);
}

inline auto notification_toast_bounds(int width, int height, std::size_t index)
    -> notification_rect_t {
    constexpr float margin = 12.0f;
    const auto available_height = std::max(0.0f, static_cast<float>(height) - 42.0f);
    const auto toast_height = std::min(120.0f, available_height);
    const auto toast_width = std::clamp(static_cast<float>(width) - margin * 2, 0.0f, 420.0f);
    const auto top = available_height - toast_height -
                     static_cast<float>(index) * (toast_height + 8.0f);
    if (toast_width == 0 || toast_height == 0 || top < 0) return {};
    return {static_cast<float>(width) - margin - toast_width, top, toast_width, toast_height};
}

struct notification_toast_state_t {
    std::array<notification_rect_t, WORM_UI_NOTIFICATION_TOAST_CAPACITY> bounds{};
    std::array<notification_rect_t, WORM_UI_NOTIFICATION_TOAST_CAPACITY> close_bounds{};
    std::array<std::uint64_t, WORM_UI_NOTIFICATION_TOAST_CAPACITY> ids{};
    std::size_t count{};
    std::uint64_t captured_id{};
    std::uint32_t captured_buttons{};
    bool captured_close{};
    std::uint64_t dismiss_pending{};
};

// Feed this ownership result into the frontend's existing input occlusion
// policy. A gesture that began outside the toasts keeps its input ownership.
inline auto notification_pointer_owned(const notification_toast_state_t& state,
                                        float x, float y, bool gesture_active) -> bool {
    if (state.captured_buttons != 0) return true;
    if (gesture_active) return false;
    for (std::size_t index = 0; index < state.count; ++index) {
        if (notification_contains(state.bounds[index], x, y)) return true;
    }
    return false;
}

// Panel-wide occlusion skips editor popup rendering. Keep keyboard-owned
// fields alive; event routing still consumes every toast pointer edge.
inline auto notification_content_occluded(const notification_toast_state_t& state,
                                           float x, float y, bool gesture_active,
                                           bool text_active) -> bool {
    return !text_active && notification_pointer_owned(state, x, y, gesture_active);
}

// All mouse-button edges belong here; button 1 is the left close control.
// Capture is exclusive to toasts, never acquired from an outside gesture.
inline auto notification_capture_press(notification_toast_state_t& state, float x, float y,
                                       std::uint32_t mask, bool left) -> bool {
    for (std::size_t index = 0; index < state.count; ++index) {
        if (!notification_contains(state.bounds[index], x, y)) continue;
        state.captured_buttons |= mask;
        if (left) {
            state.captured_id = state.ids[index];
            state.captured_close = notification_contains(state.close_bounds[index], x, y);
        }
        return true;
    }
    if (state.captured_buttons == 0) return false;
    state.captured_buttons |= mask;
    return true;
}

inline auto notification_pointer_edge(notification_toast_state_t& state, float x, float y,
                                      unsigned button, bool down, bool allow_new_capture) -> bool {
    if (button == 0 || button > 32) return false;
    const auto mask = std::uint32_t{1} << (button - 1);
    if (down) {
        if (!allow_new_capture && state.captured_buttons == 0) return false;
        return notification_capture_press(state, x, y, mask, button == 1);
    }
    if ((state.captured_buttons & mask) == 0) return false;
    if (button == 1 && state.captured_close) {
        for (std::size_t index = 0; index < state.count; ++index) {
            if (state.ids[index] == state.captured_id &&
                notification_contains(state.close_bounds[index], x, y)) {
                state.dismiss_pending = state.captured_id;
                break;
            }
        }
    }
    state.captured_buttons &= ~mask;
    if (button == 1) {
        state.captured_id = 0;
        state.captured_close = false;
    }
    return true;
}

inline void notification_focus_lost(notification_toast_state_t& state) {
    state.captured_id = 0;
    state.captured_buttons = 0;
    state.captured_close = false;
    state.dismiss_pending = 0;
}

inline auto notification_take_action(notification_toast_state_t& state)
    -> WormUiNotificationAction {
    const auto id = state.dismiss_pending;
    state.dismiss_pending = 0;
    return {id == 0 ? WORM_UI_NOTIFICATION_ACTION_NONE : WORM_UI_NOTIFICATION_ACTION_DISMISS, id};
}

inline auto notification_escape_target(const WormUiNotificationView& view, bool window_focused,
                                        bool text_active, bool ime_active, bool gesture_active,
                                        bool modal_active) -> std::optional<std::uint64_t> {
    if (!window_focused || text_active || ime_active || gesture_active || modal_active ||
        view.toast_count == 0 || view.toast_count > WORM_UI_NOTIFICATION_TOAST_CAPACITY)
        return std::nullopt;
    return view.toasts[0].id == 0 ? std::nullopt : std::optional{view.toasts[0].id};
}

void render_notification_toasts(nk_context* context, const WormUiNotificationView& view,
                                notification_toast_state_t& state, int width, int height);
// A separate context keeps Nuklear's window insertion and popup focus isolated
// from the editor. It borrows the editor font/style, never its input queues.
void render_notification_overlay(nk_context* primary, nk_context* overlay,
                                  const WormUiNotificationView& view,
                                  notification_toast_state_t& state, int width, int height);

} // namespace wormedit::ui::nuklear
