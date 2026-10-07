#include "sys/frame_pacing.h"

#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
using wormedit::sys::frame_pacing_remainder_ns;
std::size_t checks{};
void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void check_period_and_clock_boundaries() {
    expect(frame_pacing_remainder_ns(10, 10, false) == 5'000'000, "active zero work");
    expect(frame_pacing_remainder_ns(10, 10, true) == 50'000'000, "inactive zero work");
    expect(frame_pacing_remainder_ns(10, 5'000'009, false) == 1, "active last nanosecond");
    expect(frame_pacing_remainder_ns(10, 5'000'010, false) == 0, "active exact budget");
    expect(frame_pacing_remainder_ns(10, 50'000'010, true) == 0, "inactive exact budget");
    expect(frame_pacing_remainder_ns(10, 9, true) == 0, "rollback cannot request long sleep");
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    expect(frame_pacing_remainder_ns(maximum - 20, maximum, false) == 4'999'980,
           "near overflow uses subtraction");
    expect(frame_pacing_remainder_ns(0, maximum, true) == 0, "large elapsed bounded");
    expect(frame_pacing_remainder_ns(maximum, 0, false) == 0, "wrapped clock bounded");
}

// Useful work and present wait are separate injected durations. No SDL sleep or device runs.
void check_simulated_active_schedule() {
    constexpr std::uint64_t start = 1'000'000'000;
    const auto check = [&](std::uint64_t work, std::uint64_t present, std::uint64_t expected) {
        const auto delay = frame_pacing_remainder_ns(start, start + work + present, false);
        expect(delay == expected, "whole-frame work and presentation count toward period");
        std::printf("work_ns=%llu present_wait_ns=%llu legacy_sleep_ns=1000000 remainder_ns=%llu\n",
                    static_cast<unsigned long long>(work), static_cast<unsigned long long>(present),
                    static_cast<unsigned long long>(delay));
    };
    check(1'000'000, 4'000'000, 0); // VSync already paces to 200 Hz.
    check(1'000'000, 6'000'000, 0); // Slower display: no added delay.
    check(1'000'000, 100'000, 3'900'000); // Nonblocking/failed VSync stays bounded.
    check(6'000'000, 100'000, 0); // Useful work itself exceeded the period.
}

void check_visibility_and_fresh_frame_sequence() {
    std::uint64_t now = 100'000'000;
    for (std::size_t frame = 0; frame < 120; ++frame) {
        const auto start = now;
        const bool inactive = frame >= 40 && frame < 80;
        const auto period = inactive ? 50'000'000ULL : 5'000'000ULL;
        now += 1'000'000;
        const auto delay = frame_pacing_remainder_ns(start, now, inactive);
        expect(delay <= period, "sequence delay bounded by current visibility");
        now += delay;
        expect(now - start == period, "visibility transition uses fresh whole-frame deadline");
    }
    const auto slow_start = now;
    now += 100'000'000;
    expect(frame_pacing_remainder_ns(slow_start, now, false) == 0, "overrun has no extra delay");
    const auto next_start = now;
    now += 1'000'000;
    expect(frame_pacing_remainder_ns(next_start, now, false) == 4'000'000,
           "next frame has no catch-up debt");
    expect(frame_pacing_remainder_ns(next_start, next_start - 1, false) == 0,
           "rollback no delay");
    expect(frame_pacing_remainder_ns(0, 1'000'000, false) == 4'000'000,
           "coherent new epoch resumes pacing");
}
} // namespace

int main() {
    try {
        check_period_and_clock_boundaries();
        check_simulated_active_schedule();
        check_visibility_and_fresh_frame_sequence();
        std::printf("PASS %zu active pacing checks (injected clocks; no display certification)\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
