#pragma once

#include "ui/frontend.h"

#include <SDL3/SDL.h>

#include <span>
#include <vector>

struct event_step_t {
    std::vector<SDL_Event> events;
};

// Runs the production frontend event and compose entry points against its
// headless Nuklear context. Views and their pointed-to values live with this
// fixture for the full test frame.
class nuklear_event_harness_t {
  public:
    explicit nuklear_event_harness_t(int width = 960, int height = 720);
    ~nuklear_event_harness_t();

    nuklear_event_harness_t(const nuklear_event_harness_t&) = delete;
    auto operator=(const nuklear_event_harness_t&) -> nuklear_event_harness_t& = delete;

    [[nodiscard]] auto ready() const noexcept -> bool { return initialized_ && view_valid_; }
    [[nodiscard]] auto view() noexcept -> WormUiWorkspaceView& { return view_; }
    // Event objects are consumed synchronously and never retained. Any pointer
    // payload inside an SDL_Event must remain valid for this call.
    [[nodiscard]] auto frame(std::span<const SDL_Event> events = {}) -> WormUiWorkspaceAction;
    // Each step is one frontend frame; SDL event payload pointers in every step
    // must remain valid until this synchronous call returns.
    [[nodiscard]] auto run_script(std::span<const event_step_t> steps)
        -> std::vector<WormUiWorkspaceAction>;

  private:
    int width_{};
    int height_{};
    bool sdl_initialized_{};
    bool initialized_{};
    bool view_valid_{};
    WormUiWorkspaceView view_{};
};
