#pragma once

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace wormedit::common {

// Shared SDL window vocabulary for frame submission, offscreen field work and
// display-health observation. A pending surface still needs presentation probes.
class window_presentation_t {
  public:
    [[nodiscard]] static auto from(SDL_Window* window) -> window_presentation_t {
        if (!window) return {false, false};
        const auto flags = SDL_GetWindowFlags(window);
        return {(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) != 0,
            SDL_GetBooleanProperty(SDL_GetWindowProperties(window),
                                   "wormedit.gpu.presentation_pending", false)};
    }
    [[nodiscard]] auto is_inactive() const noexcept -> bool { return inactive_; }
    [[nodiscard]] auto is_pending() const noexcept -> bool { return pending_; }
    [[nodiscard]] auto can_draw() const noexcept -> bool { return !inactive_ && !pending_; }

  private:
    window_presentation_t(bool inactive, bool pending) : inactive_(inactive), pending_(pending) {}
    bool inactive_, pending_;
};

} // namespace wormedit::common
