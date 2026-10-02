#include "sys/compose_playhead_skip.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

// Two measures of 960 rows, so snap 4 is a 240-row step.
auto two_measures() -> measure_index_t {
    measure_index_t index;
    index.entries.push_back({.id = measure_id_t{1}, .absolute_begin_row = 0, .row_capacity = 960});
    index.entries.push_back(
        {.id = measure_id_t{2}, .absolute_begin_row = 960, .row_capacity = 960});
    index.total_rows = 1920;
    return index;
}

auto skip(double row, playhead_skip_unit_t unit, double amount) -> std::optional<std::uint64_t> {
    return playhead_skip_target_row(two_measures(), row, unit, amount, 960, 4);
}

} // namespace

TEST_CASE("snap skips go strictly past the playhead", "[sys][playhead-skip]") {
    CHECK(skip(100.0, playhead_skip_unit_t::snap, 1) == 240);
    CHECK(skip(100.0, playhead_skip_unit_t::snap, -1) == 0);
    CHECK(skip(240.0, playhead_skip_unit_t::snap, 1) == 480);
    CHECK(skip(240.0, playhead_skip_unit_t::snap, -1) == 0);
    CHECK(skip(300.0, playhead_skip_unit_t::snap, -1) == 240);
}

TEST_CASE("playhead skips clamp to the chart", "[sys][playhead-skip]") {
    CHECK(skip(1900.0, playhead_skip_unit_t::snap, 1) == 1919);
    CHECK(skip(10.0, playhead_skip_unit_t::rows, -50) == 0);
}

TEST_CASE("measure skips keep the playhead offset", "[sys][playhead-skip]") {
    CHECK(skip(100.0, playhead_skip_unit_t::measure, 1) == 1060);
    CHECK(skip(1060.0, playhead_skip_unit_t::measure, -1) == 100);
    CHECK(skip(1060.0, playhead_skip_unit_t::measure, 1) == 1060);
}

TEST_CASE("invalid snaps and rows are rejected", "[sys][playhead-skip]") {
    CHECK_FALSE(playhead_skip_target_row(two_measures(), 0.0, playhead_skip_unit_t::snap, 1, 960, 7));
    CHECK_FALSE(playhead_skip_target_row(two_measures(), std::nan(""), playhead_skip_unit_t::rows, 1,
                                         960, 4));
}
