#pragma once
#include "ui/frontend/workspace.h"

#include "ui/frontend.h"
#include "ui/nuklear/chrome/icon_widget_recording.h"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct event_step_t {
    std::vector<SDL_Event> events;
    // Complete state for this frame; never inherited from previous steps.
    SDL_Keymod modifiers{SDL_KMOD_NONE};
};

// Immutable owning copy of every borrowed action payload. Copies share immutable
// storage; get() remains valid while an owning snapshot exists. A moved-from
// snapshot must be reassigned before calling get().
class workspace_action_snapshot_t {
  public:
    explicit workspace_action_snapshot_t(const wormedit::ui::workspace_action_t& action);
    [[nodiscard]] auto get() const -> const wormedit::ui::workspace_action_t&;
  private:
    struct storage_t;
    std::shared_ptr<const storage_t> storage_;
};

// Runs the production frontend event and compose entry points against its
// headless Nuklear context. Caller-supplied view pointers must remain valid for
// each synchronous frame. Only one fixture may exist in a process at a time.
class nuklear_event_harness_t {
  public:
    explicit nuklear_event_harness_t(int width = 960, int height = 720);
    ~nuklear_event_harness_t();

    nuklear_event_harness_t(const nuklear_event_harness_t&) = delete;
    auto operator=(const nuklear_event_harness_t&) -> nuklear_event_harness_t& = delete;

    [[nodiscard]] auto ready() const noexcept -> bool { return initialized_ && view_valid_; }
    [[nodiscard]] auto view() noexcept -> wormedit::ui::workspace_view_t& { return view_; }
    [[nodiscard]] auto drawn_text() const noexcept -> const std::string& { return drawn_text_; }
    [[nodiscard]] auto drawn_text_bounds() const noexcept -> std::span<const wormedit::ui::testing_drawn_text_view_t> {
        return drawn_text_bounds_;
    }
    [[nodiscard]] auto drawn_text_center(std::string_view text) const
        -> std::optional<std::pair<float, float>>;
    // Last frame's visible native icon cells, keyed by their complete tooltip.
    // Missing/ambiguous matches throw; disabled icons remain addressable.
    [[nodiscard]] auto widget_center(std::string_view tooltip) const -> std::pair<float, float>;
    [[nodiscard]] auto hover_widget(std::string_view tooltip) -> workspace_action_snapshot_t;
    [[nodiscard]] auto click_widget(std::string_view tooltip)
        -> std::vector<workspace_action_snapshot_t>;
    // Event objects are consumed synchronously and never retained. Any pointer
    // payload inside an SDL_Event must remain valid for this call. The explicit
    // modifier state overrides key.mod and SDL state, then restores SDL state.
    [[nodiscard]] auto frame(std::span<const SDL_Event> events = {},
                             SDL_Keymod modifiers = SDL_KMOD_NONE) -> workspace_action_snapshot_t;
    // Each step is one frontend frame; SDL event payload pointers in every step
    // must remain valid until this synchronous call returns.
    [[nodiscard]] auto run_script(std::span<const event_step_t> steps)
        -> std::vector<workspace_action_snapshot_t>;

  private:
    int width_{};
    int height_{};
    bool sdl_initialized_{};
    bool initialized_{};
    bool view_valid_{};
    wormedit::ui::workspace_view_t view_{};
    wormedit::ui::testing::icon_widget_snapshot_t icon_widgets_;
    std::string drawn_text_;
    std::vector<wormedit::ui::testing_drawn_text_view_t> drawn_text_bounds_;
};

namespace wormedit::test {
[[nodiscard]] auto mouse_button(bool down, float x, float y) -> SDL_Event;
[[nodiscard]] auto mouse_motion(float x, float y) -> SDL_Event;
// Two separate frames, each with motion and a button edge; modifiers are NONE.
[[nodiscard]] auto click_at(nuklear_event_harness_t& harness, float x, float y,
                            bool twice = false) -> std::vector<workspace_action_snapshot_t>;
void scroll_at(nuklear_event_harness_t& harness, float x, float y, float direction);
[[nodiscard]] auto scroll_to_text(nuklear_event_harness_t& harness, std::string_view label,
                                   float x, float y, float direction, int limit) -> bool;
} // namespace wormedit::test
