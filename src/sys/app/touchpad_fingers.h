#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_touch.h>

#include <algorithm>
#include <vector>

namespace wormedit::sys {

// Counts the fingers resting on touch devices that SDL reports as finger events. On macOS that
// is the trackpad (and Magic Mouse); on other platforms touchpads usually produce none, and
// `is_reported()` stays false so callers fall back to input timing alone.
class touchpad_fingers_t {
  public:
    void handle_event(const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
            // Touch screens are direct input and never mean a hand resting on a scroll surface.
            if (SDL_GetTouchDeviceType(event.tfinger.touchID) == SDL_TOUCH_DEVICE_DIRECT) return;
            seen_ = true;
            if (!is_down(event.tfinger)) down_.push_back({event.tfinger.touchID, event.tfinger.fingerID});
            break;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            std::erase_if(down_, [&](const finger_t& finger) {
                return finger.touch == event.tfinger.touchID && finger.finger == event.tfinger.fingerID;
            });
            break;
        default:
            break;
        }
    }
    [[nodiscard]] auto count() const noexcept -> int { return static_cast<int>(down_.size()); }
    [[nodiscard]] auto is_reported() const noexcept -> bool { return seen_; }

  private:
    struct finger_t {
        SDL_TouchID touch;
        SDL_FingerID finger;
    };
    [[nodiscard]] auto is_down(const SDL_TouchFingerEvent& event) const -> bool {
        return std::ranges::any_of(down_, [&](const finger_t& finger) {
            return finger.touch == event.touchID && finger.finger == event.fingerID;
        });
    }

    std::vector<finger_t> down_;
    bool seen_{};
};

// The process-wide count, fed by the application's event loop.
inline auto touchpad_fingers() -> touchpad_fingers_t& {
    static touchpad_fingers_t fingers;
    return fingers;
}

} // namespace wormedit::sys
