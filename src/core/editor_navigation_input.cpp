#include <wormedit/core/editor_navigation_input.h>

#include <algorithm>
#include <limits>

namespace wormedit {
namespace {

constexpr std::uint64_t maximum_delay_milliseconds = 10'000;
constexpr std::uint64_t maximum_interval_milliseconds = 10'000;

[[nodiscard]] auto bounded_delay(std::uint64_t value, std::uint64_t fallback) noexcept
    -> std::uint64_t {
    return std::clamp(value == 0 ? fallback : value, std::uint64_t{1},
                      maximum_delay_milliseconds);
}

[[nodiscard]] auto add_saturating(std::uint64_t left, std::uint64_t right) noexcept
    -> std::uint64_t {
    return right > std::numeric_limits<std::uint64_t>::max() - left
               ? std::numeric_limits<std::uint64_t>::max()
               : left + right;
}

} // namespace

editor_navigation_input_controller_t::editor_navigation_input_controller_t(
    editor_navigation_input_settings_t settings) noexcept {
    set_settings(settings);
}

void editor_navigation_input_controller_t::set_settings(
    editor_navigation_input_settings_t settings) noexcept {
    settings_.initial_repeat_delay_milliseconds =
        bounded_delay(settings.initial_repeat_delay_milliseconds, 1'000);
    settings_.repeat_interval_milliseconds =
        bounded_delay(settings.repeat_interval_milliseconds, 85);
    settings_.accelerate_after_milliseconds =
        std::clamp(settings.accelerate_after_milliseconds, std::uint64_t{1},
                   maximum_delay_milliseconds);
    settings_.accelerated_repeat_interval_milliseconds = std::clamp(
        settings.accelerated_repeat_interval_milliseconds, std::uint64_t{1},
        maximum_interval_milliseconds);
}

auto editor_navigation_input_controller_t::settings() const noexcept
    -> const editor_navigation_input_settings_t& {
    return settings_;
}

auto editor_navigation_input_controller_t::press(editor_navigation_action_t action,
                                                  std::uint64_t now_milliseconds) noexcept
    -> editor_navigation_action_batch_t {
    editor_navigation_action_batch_t batch;
    if (held_action_ == action)
        return batch;

    held_action_ = action;
    pressed_at_milliseconds_ = now_milliseconds;
    next_repeat_at_milliseconds_ = add_saturating(
        now_milliseconds, settings_.initial_repeat_delay_milliseconds);
    batch.actions[batch.count++] = action;
    return batch;
}

void editor_navigation_input_controller_t::release(editor_navigation_action_t action) noexcept {
    if (held_action_ == action)
        cancel();
}

auto editor_navigation_input_controller_t::advance(std::uint64_t now_milliseconds) noexcept
    -> editor_navigation_action_batch_t {
    editor_navigation_action_batch_t batch;
    if (!held_action_ || now_milliseconds < next_repeat_at_milliseconds_)
        return batch;

    const auto interval = repeat_interval_at(now_milliseconds);
    while (batch.count < batch.actions.size() && now_milliseconds >= next_repeat_at_milliseconds_) {
        batch.actions[batch.count++] = *held_action_;
        next_repeat_at_milliseconds_ = add_saturating(next_repeat_at_milliseconds_, interval);
    }
    // Dropping an overdue backlog avoids a stalled frontend causing a burst of
    // dozens of navigation actions in a single editor frame.
    if (now_milliseconds >= next_repeat_at_milliseconds_)
        next_repeat_at_milliseconds_ = add_saturating(now_milliseconds, interval);
    return batch;
}

void editor_navigation_input_controller_t::cancel() noexcept {
    held_action_.reset();
    pressed_at_milliseconds_ = 0;
    next_repeat_at_milliseconds_ = 0;
}

void editor_navigation_input_controller_t::focus_lost() noexcept { cancel(); }
void editor_navigation_input_controller_t::text_input_started() noexcept { cancel(); }
void editor_navigation_input_controller_t::modal_opened() noexcept { cancel(); }

auto editor_navigation_input_controller_t::is_active() const noexcept -> bool {
    return held_action_.has_value();
}

auto editor_navigation_input_controller_t::repeat_interval_at(
    std::uint64_t now_milliseconds) const noexcept -> std::uint64_t {
    const auto elapsed = now_milliseconds >= pressed_at_milliseconds_
                             ? now_milliseconds - pressed_at_milliseconds_
                             : 0;
    return elapsed >= settings_.accelerate_after_milliseconds
               ? settings_.accelerated_repeat_interval_milliseconds
               : settings_.repeat_interval_milliseconds;
}

} // namespace wormedit
