#include <wormedit/audio/presentation_clock.h>
#include <wormedit/audio/playback_rate.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

using namespace wormedit::audio;
namespace {
constexpr std::uint64_t origin_ns = 1'000'000'000;
constexpr std::uint32_t sample_rate = 44'100;

auto report_at(std::uint64_t frame, std::uint64_t timestamp = origin_ns) -> playhead_state_t {
    return {.active = true, .timeline_frame = frame, .generation = 1,
        .transport_generation = 1, .report_time_ns = timestamp, .unwrapped_frame = frame,
        .rendered_frame = frame + 4096};
}
void check_presentation_evidence(const playhead_state_t& observed, const playhead_state_t& report) {
    CHECK(observed.report_time_ns == report.report_time_ns);
    CHECK(observed.unwrapped_frame == report.unwrapped_frame);
    CHECK(observed.rendered_frame == report.rendered_frame);
    CHECK(observed.loop_end_frame == report.loop_end_frame);
    CHECK(observed.rate_index == report.rate_index);
}
} // namespace

TEST_CASE("Presentation clock follows timestamped jittery audio smoothly at 144Hz", "[audio][presentation-clock]") {
    presentation_clock_t clock;
    auto report = report_at(0);
    std::uint64_t previous{};
    constexpr std::array<int, 6> jitter{0, -60, 35, -25, 70, -40};
    for (std::uint64_t frame = 0; frame < 1440; ++frame) {
        const auto elapsed_ns = frame * 1'000'000'000 / 144;
        const auto ideal = elapsed_ns * sample_rate / 1'000'000'000;
        if (frame % 3 == 0) {
            const auto measured = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
                static_cast<std::int64_t>(ideal) + jitter[(frame / 3) % jitter.size()]));
            report = report_at(measured, origin_ns + elapsed_ns);
        }
        const auto projected = clock.frame(report, origin_ns + elapsed_ns, sample_rate);
        CHECK(projected >= previous);
        CHECK(std::abs(static_cast<double>(projected) - static_cast<double>(ideal)) < sample_rate / 144.0);
        previous = projected;
    }
}

TEST_CASE("Presentation clock resets on seek and effective playback rate changes", "[audio][presentation-clock]") {
    presentation_clock_t clock;
    auto report = report_at(10'000);
    CHECK(clock.frame(report, origin_ns + 20'000'000, sample_rate) == 10'882);
    report = report_at(100, origin_ns + 20'000'000);
    ++report.generation;
    CHECK(clock.frame(report, origin_ns + 20'000'000, sample_rate) == 100);
    report.rate_index = static_cast<std::size_t>(playback_rate_id_t::half);
    CHECK(clock.frame(report, origin_ns + 40'000'000, sample_rate) == 541);
    report.rate_index = static_cast<std::size_t>(playback_rate_id_t::normal);
    CHECK(clock.frame(report, origin_ns + 40'000'000, sample_rate) == 982);
}

TEST_CASE("Presentation clock absorbs a late negative report correction without a backward step", "[audio][presentation-clock]") {
    presentation_clock_t clock;
    auto report = report_at(1000);
    REQUIRE(clock.frame(report, origin_ns + 5'000'000, sample_rate) == 1220);
    report = report_at(1200, origin_ns + 6'000'000);
    CHECK(clock.frame(report, report.report_time_ns, sample_rate) == 1220);
    CHECK(clock.frame(report, report.report_time_ns + 1'000'000, sample_rate) == 1244);
}

TEST_CASE("Presentation clock wraps a coherent unwrapped loop without carrying seek correction", "[audio][presentation-clock]") {
    presentation_clock_t clock;
    auto report = report_at(950);
    report.start_frame = 500;
    report.loop_end_frame = 1000;
    CHECK(clock.frame(report, origin_ns, 1000) == 950);
    CHECK(clock.frame(report, origin_ns + 100'000'000, 1000) == 550);
    report = report_at(1060, origin_ns + 110'000'000);
    report.timeline_frame = 560;
    report.start_frame = 500;
    report.loop_end_frame = 1000;
    ++report.transport_generation;
    CHECK(clock.frame(report, origin_ns + 120'000'000, 1000) == 570);
    report.loop_end_frame = 0;
    report.unwrapped_frame = report.timeline_frame = 300;
    CHECK(clock.frame(report, report.report_time_ns, 1000) == 300);
}

TEST_CASE("Presentation clock holds stalled reports and never outruns rendered or draining audio", "[audio][presentation-clock]") {
    presentation_clock_t clock;
    auto report = report_at(1000);
    report.rendered_frame = 1200;
    report.source_finished = true;
    CHECK(clock.frame(report, origin_ns + 90'000'000, sample_rate) == 1200);
    report = report_at(0);
    report.rendered_frame = 100'000;
    ++report.generation;
    CHECK(clock.frame(report, origin_ns + 1'000'000'000, sample_rate) == 4410);
    CHECK(clock.frame(report, origin_ns + 2'000'000'000, sample_rate) == 4410);
    report.active = false;
    report.timeline_frame = 90;
    CHECK(clock.frame(report, origin_ns + 2'000'000'000, sample_rate) == 90);
}

TEST_CASE("Output snapshot keeps presentation evidence coherent and absent evidence stays exact", "[audio][presentation-clock]") {
    playhead_snapshot_t snapshot;
    auto report = report_at(123);
    report.loop_end_frame = 900;
    report.rate_index = 3;
    snapshot.publish(report);
    const auto observed = snapshot.read();
    check_presentation_evidence(observed, report);
    presentation_clock_t clock;
    report.report_time_ns = 0;
    CHECK(clock.frame(report, origin_ns, sample_rate) == 123);
    report.report_time_ns = origin_ns;
    CHECK(clock.frame(report, 0, sample_rate) == 123);
}

TEST_CASE("Output snapshot presentation evidence stays coherent under concurrent reports", "[audio][presentation-clock]") {
    playhead_snapshot_t snapshot;
    std::atomic<bool> complete{};
    std::jthread writer([&] {
        for (std::uint64_t generation = 1; generation <= 100'000; ++generation) {
            snapshot.publish({.active = true, .timeline_frame = generation,
                .generation = generation, .report_time_ns = generation * 10,
                .unwrapped_frame = generation * 20, .rendered_frame = generation * 30,
                .loop_end_frame = generation * 40, .rate_index = generation % 9});
        }
        complete.store(true, std::memory_order_release);
    });
    bool coherent = true;
    do {
        const auto state = snapshot.read();
        if (!state.active) continue;
        coherent = coherent && state.timeline_frame == state.generation &&
            state.report_time_ns == state.generation * 10 &&
            state.unwrapped_frame == state.generation * 20 &&
            state.rendered_frame == state.generation * 30 &&
            state.loop_end_frame == state.generation * 40 &&
            state.rate_index == state.generation % 9;
    } while (!complete.load(std::memory_order_acquire));
    CHECK(coherent);
}
