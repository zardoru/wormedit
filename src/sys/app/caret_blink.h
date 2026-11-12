#pragma once

#include <cstdint>

namespace wormedit::sys {

// A text caret is visible for this long, then hidden for this long. Input restarts the
// cycle with the caret visible, so it never vanishes while someone is typing.
inline constexpr std::uint64_t caret_half_period_ns = 500'000'000;

// Phase of the text caret, in the SDL_GetTicksNS() timebase like frame_demand_t. Pure:
// the frame loop owns one, restarts it on keyboard/click input, reads is_visible() when
// it styles the frame, and asks next_change_ns() for the frame_demand_t deadline so an
// otherwise idle loop wakes once per phase change and not at all without a focused field.
struct caret_blink_t {
    std::uint64_t epoch_ns{};

    constexpr void restart(std::uint64_t now_ns) noexcept { epoch_ns = now_ns; }

    [[nodiscard]] constexpr auto is_visible(std::uint64_t now_ns) const noexcept -> bool {
        return phase_index(now_ns) % 2 == 0;
    }
    // First instant after `now_ns` at which is_visible() flips.
    [[nodiscard]] constexpr auto next_change_ns(std::uint64_t now_ns) const noexcept -> std::uint64_t {
        return epoch_ns + (phase_index(now_ns) + 1) * caret_half_period_ns;
    }

  private:
    // A clock that reads before the epoch counts as the first (visible) phase.
    [[nodiscard]] constexpr auto phase_index(std::uint64_t now_ns) const noexcept -> std::uint64_t {
        return now_ns > epoch_ns ? (now_ns - epoch_ns) / caret_half_period_ns : 0;
    }
};

} // namespace wormedit::sys
