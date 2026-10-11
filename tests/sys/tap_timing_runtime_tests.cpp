#include "sys/tap_timing_runtime.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <tuple>

namespace {
using namespace wormedit;
using namespace wormedit::sys;
auto tap_clock(std::uint64_t event_ns, std::uint64_t event_frame,
               std::uint64_t delay_ns = 100'000'000, rational_t rate = {1, 1}) -> tap_timing_clock_t {
    const auto denominator = 1'000'000'000 * rate.denominator;
    const auto delay_frames = (delay_ns * 44'100 * rate.numerator + denominator / 2) / denominator;
    return {.chart = 1, .revision = 2, .asset = 3, .epoch = 4,
        .epoch_begin_ns = 1'000'000'000, .observed_ns = event_ns + delay_ns,
        .source_frame = event_frame + delay_frames, .playback_rate = rate, .playing = true};
}
void record_timing_taps(tap_timing_runtime_t& runtime, rational_t rate = {1, 1}) {
    runtime.set_visible(true);
    for (std::uint64_t i = 0; i < 10; ++i) {
        const auto event = 2'000'000'000 + i * 500'000'000 * rate.denominator / rate.numerator;
        (void)runtime.tap(event, tap_clock(event, 88'200 + i * 22'050, 100'000'000, rate));
    }
}
void check_timing_estimate(const tap_timing_runtime_t& runtime) {
    const auto state = runtime.state();
    REQUIRE(state.estimate);
    CHECK(std::tie(state.estimate->bpm, state.estimate->phase_frame, state.estimate->stable) ==
        std::tuple{120.0, 88'200.0, true});
    CHECK(state.tap_count == 10);
}
} // namespace

TEST_CASE("Tap timing reconstructs event-time music frames at every transport rate", "[sys][tap-timing]") {
    for (const rational_t rate : {rational_t{1, 10}, {1, 4}, {1, 2}, {9, 10}, {1, 1}}) {
        tap_timing_runtime_t runtime;
        record_timing_taps(runtime, rate);
        check_timing_estimate(runtime);
    }
}

TEST_CASE("Tap timing checked interpolation rejects unavailable and stale event clocks", "[sys][tap-timing]") {
    auto clock = tap_clock(2'000'000'000, 44'100);
    CHECK(tap_source_frame_at_event(clock, 2'000'000'000) == 44'100);
    CHECK_FALSE(tap_source_frame_at_event(clock, clock.observed_ns + 1));
    CHECK_FALSE(tap_source_frame_at_event(clock, clock.epoch_begin_ns - 1));
    clock.observed_ns = 10'000'000'000;
    CHECK_FALSE(tap_source_frame_at_event(clock, 2'000'000'000));
    clock = tap_clock(2'000'000'000, 0);
    clock.source_frame = 0;
    CHECK_FALSE(tap_source_frame_at_event(clock, 2'000'000'000));
    clock.source_frame.reset();
    CHECK_FALSE(tap_source_frame_at_event(clock, clock.observed_ns));
}

TEST_CASE("Tap timing nearest-frame conversion avoids unsigned overflow", "[sys][tap-timing]") {
    auto clock = tap_clock(2'000'000'000, 88'200, 0);
    clock.sample_rate = 10;
    clock.observed_ns += 150'000'000;
    CHECK(tap_source_frame_at_event(clock, 2'000'000'000) == 88'198);
    clock.playback_rate = {std::numeric_limits<std::uint64_t>::max(), 1};
    CHECK_FALSE(tap_source_frame_at_event(clock, 2'000'000'000));
}

TEST_CASE("Tap timing resets for forward seek and rejects pre-seek timestamps", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    record_timing_taps(runtime);
    auto seek = tap_clock(8'000'000'000, 500'000);
    ++seek.epoch;
    seek.epoch_begin_ns = seek.observed_ns;
    REQUIRE(runtime.observe(seek));
    CHECK(runtime.state().tap_count == 0);
    CHECK(runtime.tap(8'000'000'000, seek) == tap_timing_result_t::stale);
    CHECK(runtime.tap(seek.observed_ns, seek) == tap_timing_result_t::accepted);
    CHECK(runtime.state().tap_count == 1);
}

TEST_CASE("Tap timing rate changes invalidate the estimate and older queued taps", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    record_timing_taps(runtime);
    auto changed = tap_clock(8'000'000'000, 500'000, 0, {1, 2});
    REQUIRE(runtime.observe(changed));
    CHECK(runtime.state().tap_count == 0);
    CHECK(runtime.tap(changed.observed_ns - 1, changed) == tap_timing_result_t::stale);
    CHECK(runtime.tap(changed.observed_ns, changed) == tap_timing_result_t::accepted);
}

TEST_CASE("Tap timing stops and stale snapshots cannot resurrect estimates", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    record_timing_taps(runtime);
    auto stopped = tap_clock(8'000'000'000, 500'000);
    stopped.playing = false;
    REQUIRE(runtime.observe(stopped));
    CHECK(runtime.state().tap_count == 0);
    CHECK(runtime.tap(stopped.observed_ns, stopped) == tap_timing_result_t::not_playing);
    CHECK_FALSE(runtime.observe(tap_clock(2'000'000'000, 88'200)));
    CHECK_FALSE(runtime.state().estimate);
}

TEST_CASE("Tap timing follows source phase and downbeat animation", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    record_timing_taps(runtime);
    auto current = tap_clock(8'000'000'000, 88'200 + 12 * 22'050, 0);
    REQUIRE(runtime.observe(current));
    CHECK(runtime.state().active_segment == 0);
    CHECK(runtime.state().downbeat_flash);
    current.observed_ns += 250'000'000;
    *current.source_frame += 11'025;
    REQUIRE(runtime.observe(current));
    CHECK_FALSE(runtime.state().downbeat_flash);
}

TEST_CASE("Tap timing meter changes reset the local window", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    record_timing_taps(runtime);
    auto current = tap_clock(8'000'000'000, 400'000);
    current.beats_per_measure = 3;
    REQUIRE(runtime.observe(current));
    CHECK(runtime.state().tap_capacity == 12);
    CHECK(runtime.state().tap_count == 0);
}

TEST_CASE("Tap timing closed panel and Reset discard pending local input", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    auto clock = tap_clock(2'000'000'000, 88'200);
    CHECK(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::closed);
    record_timing_taps(runtime);
    runtime.reset();
    CHECK(runtime.state().tap_count == 0);
    CHECK(runtime.tap(6'500'000'000, tap_clock(7'000'000'000, 400'000)) == tap_timing_result_t::stale);
}

TEST_CASE("Tap timing invalid snapshots clear estimates", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    record_timing_taps(runtime);
    auto clock = tap_clock(8'000'000'000, 400'000);
    clock.sample_rate = 0;
    REQUIRE(runtime.observe(clock));
    CHECK_FALSE(runtime.state().estimate);
    CHECK_FALSE(runtime.state().source_available);
    CHECK(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::unavailable);
}

namespace { void expect_pulse(bool condition) { REQUIRE(condition); } }

TEST_CASE("Tap ring pulses accepted segments for exactly 120ms and clears on Reset or stop", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    runtime.set_visible(true);
    auto clock = tap_clock(2'000'000'000, 88'200, 0);
    expect_pulse(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::accepted);
    expect_pulse(runtime.state().tap_flash);
    expect_pulse(runtime.state().tap_segment == 0);
    clock.observed_ns += 119'999'999;
    expect_pulse(runtime.observe(clock));
    expect_pulse(runtime.state().tap_flash);
    ++clock.observed_ns;
    expect_pulse(runtime.observe(clock));
    expect_pulse(!runtime.state().tap_flash);
    clock = tap_clock(2'500'000'000, 110'250, 0);
    expect_pulse(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::accepted);
    expect_pulse(runtime.state().tap_segment == 1);
    expect_pulse(runtime.state().tap_flash);
    runtime.reset();
    expect_pulse(!runtime.state().tap_flash);
    clock = tap_clock(3'000'000'000, 132'300, 0);
    expect_pulse(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::accepted);
    expect_pulse(runtime.state().tap_segment == 0);
    clock.playing = false;
    expect_pulse(runtime.observe(clock));
    expect_pulse(!runtime.state().tap_flash);
}

TEST_CASE("Tap ring advances after its rolling estimate buffer fills", "[sys][tap-timing]") {
    tap_timing_runtime_t runtime;
    runtime.set_visible(true);
    for (std::uint64_t i = 0; i < 18; ++i) {
        const auto clock = tap_clock(2'000'000'000 + i * 500'000'000, 88'200 + i * 22'050, 0);
        expect_pulse(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::accepted);
        expect_pulse(runtime.state().tap_segment == i % 4);
    }
}
