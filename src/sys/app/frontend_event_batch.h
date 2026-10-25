#pragma once

#include <SDL3/SDL_events.h>
#include <array>
#include <optional>
#include <utility>

namespace wormedit::sys {

// Leave later SDL events queued when a control needs its next frame edge.
// Only fixed-size key/button events are retained; SDL-owned text/drop pointers
// never outlive their event pump.
class frontend_event_batch_t {
  public:
    void begin() { keys_.fill(false); buttons_.fill(false); }
    [[nodiscard]] auto pending() -> std::optional<SDL_Event> {
        return std::exchange(pending_, std::nullopt);
    }
    [[nodiscard]] auto admit(const SDL_Event& event) -> bool {
        bool* changed{};
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const auto code = static_cast<std::size_t>(event.key.scancode);
            if (code < keys_.size() && !event.key.repeat) changed = &keys_[code];
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            changed = &buttons_[event.button.button];
        }
        if (!changed) return true;
        if (*changed) { pending_ = event; return false; }
        *changed = true;
        return true;
    }
  private:
    std::array<bool, SDL_SCANCODE_COUNT> keys_{};
    std::array<bool, 256> buttons_{};
    std::optional<SDL_Event> pending_;
};

} // namespace wormedit::sys
