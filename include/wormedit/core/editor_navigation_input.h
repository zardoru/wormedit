#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wormedit {

// Navigation is intentionally expressed independently from SDL, keyboard
// repeat settings, and the editor document. The application maps these to
// cursor actions after checking its current Compose binding.
enum class editor_navigation_action_t {
    cursor_backward,
    cursor_forward,
    page_backward,
    page_forward,
    double_page_backward,
    double_page_forward,
};

inline constexpr std::size_t maximum_editor_navigation_actions_per_advance = 4;

struct editor_navigation_action_batch_t {
    std::array<editor_navigation_action_t, maximum_editor_navigation_actions_per_advance>
        actions{};
    std::size_t count{};

    [[nodiscard]] auto empty() const noexcept -> bool { return count == 0; }
    [[nodiscard]] auto operator[](std::size_t index) const noexcept
        -> editor_navigation_action_t {
        return actions[index];
    }
};

struct editor_navigation_input_settings_t {
    // A press always emits immediately. These settings control subsequent
    // emissions while that same logical action stays held.
    std::uint64_t initial_repeat_delay_milliseconds{1'000};
    std::uint64_t repeat_interval_milliseconds{85};
    std::uint64_t accelerate_after_milliseconds{3'000};
    std::uint64_t accelerated_repeat_interval_milliseconds{35};
};

class editor_navigation_input_controller_t {
  public:
    explicit editor_navigation_input_controller_t(
        editor_navigation_input_settings_t settings = {}) noexcept;

    void set_settings(editor_navigation_input_settings_t settings) noexcept;
    [[nodiscard]] auto settings() const noexcept -> const editor_navigation_input_settings_t&;

    // Native repeated key-down events must not be passed here. A second press
    // of the already-held action is harmless, allowing frontend adapters to
    // defensively de-duplicate platform input.
    [[nodiscard]] auto press(editor_navigation_action_t action,
                             std::uint64_t now_milliseconds) noexcept
        -> editor_navigation_action_batch_t;
    void release(editor_navigation_action_t action) noexcept;

    // Advances with a caller-provided monotonic clock. Delayed frames produce
    // a bounded batch and never an unbounded backlog of cursor moves.
    [[nodiscard]] auto advance(std::uint64_t now_milliseconds) noexcept
        -> editor_navigation_action_batch_t;

    void cancel() noexcept;
    void focus_lost() noexcept;
    void text_input_started() noexcept;
    void modal_opened() noexcept;

    [[nodiscard]] auto active() const noexcept -> bool;

  private:
    [[nodiscard]] auto repeat_interval_at(std::uint64_t now_milliseconds) const noexcept
        -> std::uint64_t;

    editor_navigation_input_settings_t settings_{};
    std::optional<editor_navigation_action_t> held_action_;
    std::uint64_t pressed_at_milliseconds_{};
    std::uint64_t next_repeat_at_milliseconds_{};
};

} // namespace wormedit
