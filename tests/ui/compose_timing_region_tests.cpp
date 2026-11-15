#include "ui/compose_edge_handle.h"
#include "ui/compose_timing_geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

TEST_CASE("Timing lanes hide in priority order when the region is too narrow",
          "[ui][timing-region]") {
    using wormedit::ui::compose_timing_fit_mask;
    const auto bit = [](wormedit::timing_lane_kind_t lane) { return 1U << static_cast<unsigned>(lane); };
    CHECK(compose_timing_fit_mask(wormedit::all_timing_lane_mask, 1000.0) == wormedit::all_timing_lane_mask);
    // Optional lanes first, then anchor, kiai, warp, speed (with SV), delay, stop and BPM last.
    CHECK(compose_timing_fit_mask(wormedit::all_timing_lane_mask, 200.0) == 0xFFU);
    CHECK(compose_timing_fit_mask(0xFFU, 150.0) ==
          (0xFFU & ~bit(wormedit::timing_lane_kind_t::anchor) & ~bit(wormedit::timing_lane_kind_t::kiai)));
    CHECK(compose_timing_fit_mask(0xFFU, 100.0) ==
          (bit(wormedit::timing_lane_kind_t::bpm) | bit(wormedit::timing_lane_kind_t::scroll) |
           bit(wormedit::timing_lane_kind_t::stop) | bit(wormedit::timing_lane_kind_t::delay)));
    CHECK(compose_timing_fit_mask(0xFFU, 50.0) ==
          (bit(wormedit::timing_lane_kind_t::bpm) | bit(wormedit::timing_lane_kind_t::stop)));
    CHECK(compose_timing_fit_mask(0xFFU, 10.0) == bit(wormedit::timing_lane_kind_t::bpm));
    // A lane the user kept alone is never dropped.
    CHECK(compose_timing_fit_mask(bit(wormedit::timing_lane_kind_t::anchor), 1.0) ==
          bit(wormedit::timing_lane_kind_t::anchor));
}

TEST_CASE("A squeezed timing region keeps every lane at least icon wide",
          "[ui][timing-region]") {
    using wormedit::ui::compose_timing_lane_scaled_width;
    constexpr std::uint16_t mask = 0b111; // BPM, SV, speed: 64 + 64 + 64 natural.
    double total{};
    for (unsigned lane = 0; lane < 3; ++lane) {
        const auto width = compose_timing_lane_scaled_width(mask, lane, 100.0);
        CHECK(width >= wormedit::ui::compose_timing_lane_minimum_width);
        total += width;
    }
    CHECK(std::abs(total - 100.0) < 1e-9);
    CHECK(std::abs(compose_timing_lane_scaled_width(mask, 0, 384.0) - 128.0) < 1e-9);
    CHECK(compose_timing_lane_scaled_width(mask, 0, 10.0) == wormedit::ui::compose_timing_lane_minimum_width);
}

TEST_CASE("Edge handles share one hit test and drag rule", "[ui][timing-region]") {
    using namespace wormedit::ui;
    CHECK(compose_edge_handle_tolerance(1000.0, 500.0) == 8.0);
    CHECK(compose_edge_handle_hit(103.0, 50.0, 100.0, 200.0, 4.0));
    CHECK_FALSE(compose_edge_handle_hit(105.0, 50.0, 100.0, 200.0, 4.0));
    CHECK_FALSE(compose_edge_handle_hit(100.0, -1.0, 100.0, 200.0, 4.0));
    CHECK_FALSE(compose_edge_handle_hit(100.0, 200.0, 100.0, 200.0, 4.0));
    CHECK(compose_edge_handle_dragged_value(120.0, 400.0, 380.0) == 100.0);
}
