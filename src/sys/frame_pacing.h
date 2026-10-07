#pragma once

#include <cstdint>

namespace wormedit::sys {

inline constexpr std::uint64_t active_frame_period_ns = 5'000'000;
inline constexpr std::uint64_t inactive_frame_period_ns = 50'000'000;

// Whole-frame minimum period, including useful work and any presentation wait.
// Stateless subtraction avoids deadline overflow and accumulated catch-up debt.
// A rolled-back clock requests no delay; a later coherent frame starts fresh.
[[nodiscard]] auto frame_pacing_remainder_ns(std::uint64_t frame_start_ns,
                                            std::uint64_t now_ns,
                                            bool inactive_window) noexcept -> std::uint64_t;

} // namespace wormedit::sys
