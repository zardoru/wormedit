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
    void begin() { keys_.fill(false); buttons_.fill(false); admitted_ = 0; }
    // Events handed to the frontend since begin(); zero means the frame saw no input.
    [[nodiscard]] auto events_admitted() const noexcept -> std::size_t { return admitted_; }
    // A retained event needs the next frame to run immediately.
    [[nodiscard]] auto has_pending() const noexcept -> bool { return pending_.has_value(); }
    [[nodiscard]] auto pending() -> std::optional<SDL_Event> {
        return std::exchange(pending_, std::nullopt);
    }
    [[nodiscard]] auto admit(const SDL_Event& event) -> bool {
        bool* changed{};
        ++admitted_;
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
    std::size_t admitted_{};
};

} // namespace wormedit::sys
