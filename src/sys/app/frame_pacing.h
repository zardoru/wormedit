#pragma once

#include <cstdint>

namespace wormedit::sys {

inline constexpr std::uint64_t active_frame_period_ns = 5'000'000;
inline constexpr std::uint64_t inactive_frame_period_ns = 50'000'000;
// Cadence for demands that change on screen without frame precision (job progress).
inline constexpr std::uint64_t low_cadence_period_ns = 100'000'000;
// Longest an otherwise idle loop sleeps. Remove (nullopt) once every animation source
// reports a frame_demand_t and the display watchdog tolerates a loop parked in a wait.
inline constexpr std::uint64_t idle_limit_ns = 100'000'000;

// Whole-frame minimum period, including useful work and any presentation wait.
// Stateless subtraction avoids deadline overflow and accumulated catch-up debt.
// A rolled-back clock requests no delay; a later coherent frame starts fresh.
[[nodiscard]] auto frame_pacing_remainder_ns(std::uint64_t frame_start_ns,
                                            std::uint64_t now_ns,
                                            bool inactive_window) noexcept -> std::uint64_t;

} // namespace wormedit::sys
