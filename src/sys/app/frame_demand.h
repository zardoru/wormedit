#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace wormedit::sys {

// What one subsystem needs from the frame loop after the frame that just ended.
// Times use the SDL_GetTicksNS() timebase, like frame_pacing. A source that needs
// nothing returns none(); the loop combines every source with merge_all() and sleeps
// in SDL_WaitEventTimeout() until the earliest deadline, an input event, or an
// explicit wake (event_loop_wake.h). `reason` must be a string literal: it outlives
// every demand and costs nothing in the idle path.
struct frame_demand_t {
    std::string_view reason{};
    bool continuous{};
    std::optional<std::uint64_t> deadline_ns{};

    [[nodiscard]] static constexpr auto none() noexcept -> frame_demand_t { return {}; }
    [[nodiscard]] static constexpr auto every_frame(std::string_view why) noexcept -> frame_demand_t {
        return {.reason = why, .continuous = true};
    }
    [[nodiscard]] static constexpr auto at(std::uint64_t deadline, std::string_view why) noexcept
        -> frame_demand_t {
        return {.reason = why, .deadline_ns = deadline};
    }
    // One frame `period_ns` after `now_ns`: a low-cadence source (hover fade, job
    // progress, caret phase) asks again each time it is served.
    [[nodiscard]] static constexpr auto after(std::uint64_t now_ns, std::uint64_t period_ns,
                                              std::string_view why) noexcept -> frame_demand_t {
        return at(now_ns + period_ns, why);
    }

    [[nodiscard]] constexpr auto is_none() const noexcept -> bool {
        return !continuous && !deadline_ns;
    }
    auto operator==(const frame_demand_t&) const noexcept -> bool = default;
};

// A continuous demand wins; otherwise the earlier deadline. Ties keep `first`, so the
// reason reported for a tie is the source listed first.
[[nodiscard]] constexpr auto merge(frame_demand_t first, frame_demand_t second) noexcept
    -> frame_demand_t {
    if (first.continuous) return first;
    if (second.continuous) return second;
    if (!second.deadline_ns) return first;
    if (!first.deadline_ns || *second.deadline_ns < *first.deadline_ns) return second;
    return first;
}

[[nodiscard]] constexpr auto merge_all(std::initializer_list<frame_demand_t> demands) noexcept
    -> frame_demand_t {
    auto result = frame_demand_t::none();
    for (const auto& demand : demands) result = merge(result, demand);
    return result;
}

// How long the loop may wait for events before starting the next frame:
// 0 = start it now, nullopt = no limit. `idle_limit_ns` caps the wait when nothing has
// a deadline (and caps every deadline); nullopt means a truly idle loop sleeps until
// an event or wake arrives. Lower it to nullopt only once every animation reports its
// demand; until then it is the safety net that turns a missed source into a slow
// refresh instead of a frozen one.
[[nodiscard]] constexpr auto frame_wait_budget_ns(const frame_demand_t& demand, std::uint64_t now_ns,
                                                  std::optional<std::uint64_t> idle_limit_ns) noexcept
    -> std::optional<std::uint64_t> {
    if (demand.continuous) return std::uint64_t{0};
    std::optional<std::uint64_t> budget = idle_limit_ns;
    if (demand.deadline_ns) {
        const auto remaining = *demand.deadline_ns > now_ns ? *demand.deadline_ns - now_ns : 0;
        budget = budget ? std::min(*budget, remaining) : remaining;
    }
    return budget;
}

// SDL_WaitEventTimeout() argument: whole milliseconds, rounded up so a sub-millisecond
// remainder cannot degrade into a busy loop; -1 waits without limit.
[[nodiscard]] constexpr auto wait_timeout_ms(std::optional<std::uint64_t> budget_ns) noexcept -> std::int32_t {
    if (!budget_ns) return -1;
    constexpr std::uint64_t maximum_ms = 60'000;
    const auto ms = (*budget_ns + 999'999) / 1'000'000;
    return static_cast<std::int32_t>(ms < maximum_ms ? ms : maximum_ms);
}

} // namespace wormedit::sys
