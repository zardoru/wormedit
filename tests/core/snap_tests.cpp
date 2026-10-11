#include <wormedit/core/snap.h>

#include <catch2/catch_test_macros.hpp>
#include <source_location>

using namespace wormedit;

namespace {

auto snap_chart() -> chart_document_t {
    return {.id = chart_id_t{1},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}}};
}

} // namespace

TEST_CASE("snapping rounds to the nearest grid row and ties go earlier",
          "[unit][core][snap]") {
    const auto chart = snap_chart();
    // A quarter-note grid of a 960-row measure steps by 240 rows.
    CHECK(snap_absolute_row(chart, 100, 4) == std::optional<std::uint64_t>{0});
    CHECK(snap_absolute_row(chart, 120, 4) == std::optional<std::uint64_t>{0});
    CHECK(snap_absolute_row(chart, 121, 4) == std::optional<std::uint64_t>{240});
    CHECK(snap_absolute_row(chart, 960, 4) == std::optional<std::uint64_t>{960});
    CHECK_FALSE(snap_absolute_row(chart, 10, 0));
    CHECK_FALSE(snap_absolute_row(chart, 10, 7));
}

TEST_CASE("turning snapping off uses the chart's finest row for every object class",
          "[unit][core][snap]") {
    const auto chart = snap_chart();
    CHECK(effective_edit_snap_denominator(chart, 4, true) == 4);
    CHECK(effective_edit_snap_denominator(chart, 4, false) == 960);
    const auto off = effective_edit_snap_denominator(chart, 4, false);
    CHECK(snap_absolute_row(chart, 123, off) == std::optional<std::uint64_t>{123});
}

TEST_CASE("snapped positions resolve across measures and reject rows past the chart",
          "[unit][core][snap]") {
    const auto chart = snap_chart();
    const auto built = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(built));
    const auto& index = std::get<measure_index_t>(built);
    const auto snapped = snap_position(chart, index, {measure_id_t{4}, 130}, 4);
    REQUIRE(snapped);
    CHECK((*snapped == local_position_t{measure_id_t{4}, 240}));
    const auto fine = snap_position(chart, index, {measure_id_t{4}, 130}, 960);
    REQUIRE(fine);
    CHECK((*fine == local_position_t{measure_id_t{4}, 130}));
    CHECK_FALSE(snap_position(chart, index, {measure_id_t{9}, 0}, 4));
    CHECK_FALSE(position_at_absolute_row(index, index.total_rows));
}

namespace {
void check_measure_snap(bool value, std::source_location source = std::source_location::current()) {
    INFO("snap assertion line " << source.line());
    CHECK(value);
}

auto fractional_snap_chart() -> chart_document_t {
    auto chart = snap_chart();
    chart.measures = {
        {.id = measure_id_t{3}, .definition = {.length = {3, 10}}},
        {.id = measure_id_t{4}, .definition = {.length = {1, 16}}},
        {.id = measure_id_t{5}, .definition = {.length = {1, 16}}},
        {.id = measure_id_t{6}},
    };
    return chart;
}
}

TEST_CASE("fractional measures restart snapping and their heads remain reachable",
          "[unit][core][snap][measure-local-snap]") {
    const auto chart = fractional_snap_chart();
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    // Shortened boundaries are themselves grid lines, with earlier ties.
    check_measure_snap(snap_absolute_row(chart, 264, 4) == 240);
    check_measure_snap(snap_absolute_row(chart, index, 265, 4) == 288);
    check_measure_snap(snap_absolute_row(chart, index, 318, 4) == 288);
    check_measure_snap(snap_absolute_row(chart, index, 319, 4) == 348);
    check_measure_snap(snap_absolute_row(chart, index, 429, 4) == 408);
    check_measure_snap(snap_absolute_row(chart, index, 529, 4) == 648);
    check_measure_snap(snap_position(chart, index, {measure_id_t{4}, 40}, 4) ==
          local_position_t{measure_id_t{5}, 0});
    check_measure_snap(snap_position(chart, index, {measure_id_t{6}, 121}, 4) ==
          local_position_t{measure_id_t{6}, 240});
    // The compatibility helper may round to the chart end; placements still reject it.
    check_measure_snap(snap_absolute_row(chart, index, index.total_rows - 1, 4) == index.total_rows);
    check_measure_snap(!snap_position(chart, index, {measure_id_t{6}, 959}, 4));
    check_measure_snap(snap_canvas_row(chart, index, index.total_rows - 1, 4, index.total_rows) == 1128);
}

TEST_CASE("strict grid neighbors include tiny measure heads and preserve fractional origins",
          "[unit][core][snap][measure-local-snap]") {
    const auto chart = fractional_snap_chart();
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    const auto next = [&](long double row, bool forward) {
        return adjacent_snap_canvas_row(chart, index, row, 4, index.total_rows, forward);
    };
    check_measure_snap(next(240, true) == 288);
    check_measure_snap(next(288, true) == 348);
    check_measure_snap(next(348, true) == 408);
    check_measure_snap(next(408, true) == 648);
    check_measure_snap(next(408, false) == 348);
    check_measure_snap(next(348, false) == 288);
    check_measure_snap(next(288, false) == 240);
    check_measure_snap(next(348.5L, false) == 348);
    check_measure_snap(next(347.5L, true) == 348);
    check_measure_snap(!next(0, false));
    check_measure_snap(!next(1128, true));
}

TEST_CASE("standard virtual tail grids start at the fractional authored end",
          "[unit][core][snap][measure-local-snap]") {
    const auto chart = fractional_snap_chart();
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    const auto tail_end = index.total_rows + 2 * chart.divisions_per_standard_measure;
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 1128, 4, tail_end, true) == 1368);
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 1368, 4, tail_end, true) == 1608);
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 1368, 4, tail_end, false) == 1128);
    check_measure_snap(snap_canvas_row(chart, index, 1450, 4, tail_end) == 1368);
    check_measure_snap(snap_canvas_row(chart, index, 1490, 4, tail_end) == 1608);
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 2328, 4, tail_end, true) == 2568);
}

TEST_CASE("measure grid neighbors support fractional beats and reject invalid geometry",
          "[unit][core][snap][measure-local-snap]") {
    const measure_index_t index{.entries = {{measure_id_t{1}, 0, 2}, {measure_id_t{2}, 2, 5}},
                                .total_rows = 7};
    check_measure_snap(adjacent_measure_grid_row(index, 1.25L, 1.25L, 5, 7, true) == 2.0L);
    check_measure_snap(adjacent_measure_grid_row(index, 2.0L, 1.25L, 5, 7, false) == 1.25L);
    check_measure_snap(adjacent_measure_grid_row(index, 2.1L, 1.25L, 5, 7, true) == 3.25L);
    check_measure_snap(!adjacent_measure_grid_row(index, 2, 0, 5, 7, true));
    check_measure_snap(!adjacent_measure_grid_row(index, -1, 1, 5, 7, true));
    check_measure_snap(!adjacent_measure_grid_row(index, 7, 1, 5, 7, true));
    check_measure_snap(!adjacent_measure_grid_row(index, 2, 1, 0, 7, true));
}
