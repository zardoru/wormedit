#pragma once

#include <cstdint>

namespace wormedit::sys {

inline constexpr std::uint64_t active_frame_period_ns = 5'000'000;
inline constexpr std::uint64_t inactive_frame_period_ns = 50'000'000;
// Cadence for demands that change on screen without frame precision (job progress).
inline constexpr std::uint64_t low_cadence_period_ns = 100'000'000;
// Longest an otherwise idle loop sleeps. It is a safety net, not the plan: input,
// wake_event_loop() and frame_demand_t deadlines end a sleep earlier, and this limit
// only catches a source that reports nothing (a late frame instead of a frozen one).
// Keep it well under the display watchdog's 2 s stall threshold.
inline constexpr std::uint64_t idle_limit_ns = 1'000'000'000;

// Whole-frame minimum period, including useful work and any presentation wait.
// Stateless subtraction avoids deadline overflow and accumulated catch-up debt.
// A rolled-back clock requests no delay; a later coherent frame starts fresh.
[[nodiscard]] auto frame_pacing_remainder_ns(std::uint64_t frame_start_ns,
                                            std::uint64_t now_ns,
                                            bool inactive_window) noexcept -> std::uint64_t;

} // namespace wormedit::sys
