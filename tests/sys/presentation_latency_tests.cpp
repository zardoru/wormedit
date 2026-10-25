#include "sys/app/frame_timing.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
auto presentation_sample(double work_ms, double pacing_ms) -> wormedit::sys::frame_timing_sample_t {
    using namespace wormedit::sys;
    frame_timing_sample_t sample;
    sample.frame_ms = work_ms + pacing_ms;
    const auto render = static_cast<std::size_t>(frame_phase_t::render);
    const auto present = static_cast<std::size_t>(frame_phase_t::render_present);
    sample.measured[render] = sample.measured[present] = true;
    sample.phase_ms[render] = pacing_ms + 1.0;
    sample.phase_ms[present] = 1.0;
    return sample;
}
}

TEST_CASE("Presentation prediction uses recent completed work without frame-pacing sleep",
          "[frame-timing][presentation]") {
    wormedit::sys::frame_timing_t timer;
    CHECK(timer.expected_presentation_ns(100) == 100);
    timer.record(presentation_sample(3.0, 20.0));
    CHECK(timer.expected_presentation_ns(100) == 3'000'100);
    timer.record(presentation_sample(5.0, 2.0));
    CHECK(timer.expected_presentation_ns(100) == 4'000'100);
    for (int i = 0; i < 8; ++i) timer.record(presentation_sample(2.0, 50.0));
    CHECK(timer.expected_presentation_ns(100) == 2'000'100);
}

TEST_CASE("Presentation prediction ignores missing measurements and bounds driver stalls",
          "[frame-timing][presentation]") {
    wormedit::sys::frame_timing_t timer;
    timer.record({.frame_ms = 5'000.0});
    timer.record(presentation_sample(std::numeric_limits<double>::quiet_NaN(), 0.0));
    CHECK(timer.expected_presentation_ns(100) == 100);
    timer.record(presentation_sample(5'000.0, 0.0));
    CHECK(timer.expected_presentation_ns(100) == 50'000'100);
    CHECK(timer.expected_presentation_ns(UINT64_MAX - 1) == UINT64_MAX);
}
