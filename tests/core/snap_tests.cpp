#include <wormedit/core/document.h>
#include <wormedit/core/snap.h>

#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <array>

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
    CHECK(snap_absolute_row(chart, 100, {4}) == std::optional<std::uint64_t>{0});
    CHECK(snap_absolute_row(chart, 120, {4}) == std::optional<std::uint64_t>{0});
    CHECK(snap_absolute_row(chart, 121, {4}) == std::optional<std::uint64_t>{240});
    CHECK(snap_absolute_row(chart, 960, {4}) == std::optional<std::uint64_t>{960});
    CHECK_FALSE(snap_absolute_row(chart, 10, {0}));
    CHECK_FALSE(snap_absolute_row(chart, 10, {7}));
}

TEST_CASE("turning snapping off uses the chart's finest row for every object class",
          "[unit][core][snap]") {
    const auto chart = snap_chart();
    const snap_grid_t authored{.denominator = 4, .space = quantization_space_t::authored_measure};
    CHECK(effective_edit_snap_grid(chart, authored, true) == authored);
    const auto off = effective_edit_snap_grid(chart, authored, false);
    CHECK(off == snap_grid_t{.denominator = 960});
    CHECK(snap_absolute_row(chart, 123, off) == std::optional<std::uint64_t>{123});
}

TEST_CASE("snapped positions resolve across measures and reject rows past the chart",
          "[unit][core][snap]") {
    const auto chart = snap_chart();
    const auto built = measure_index_t::from(chart);
    REQUIRE(built.has_value());
    const auto& index = built.value();
    const auto snapped = snap_position(chart, index, {measure_id_t{4}, 130}, {4});
    REQUIRE(snapped);
    CHECK((*snapped == local_position_t{measure_id_t{4}, 240}));
    const auto fine = snap_position(chart, index, {measure_id_t{4}, 130}, {960});
    REQUIRE(fine);
    CHECK((*fine == local_position_t{measure_id_t{4}, 130}));
    CHECK_FALSE(snap_position(chart, index, {measure_id_t{9}, 0}, {4}));
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
    const auto index = measure_index_t::from(chart).value();
    // Shortened boundaries are themselves grid lines, with earlier ties.
    check_measure_snap(snap_absolute_row(chart, 264, {4}) == 240);
    check_measure_snap(snap_absolute_row(chart, index, 265, {4}) == 288);
    check_measure_snap(snap_absolute_row(chart, index, 318, {4}) == 288);
    check_measure_snap(snap_absolute_row(chart, index, 319, {4}) == 348);
    check_measure_snap(snap_absolute_row(chart, index, 429, {4}) == 408);
    check_measure_snap(snap_absolute_row(chart, index, 529, {4}) == 648);
    check_measure_snap(snap_position(chart, index, {measure_id_t{4}, 40}, {4}) ==
          local_position_t{measure_id_t{5}, 0});
    check_measure_snap(snap_position(chart, index, {measure_id_t{6}, 121}, {4}) ==
          local_position_t{measure_id_t{6}, 240});
    // The compatibility helper may round to the chart end; placements still reject it.
    check_measure_snap(snap_absolute_row(chart, index, index.total_rows - 1, {4}) == index.total_rows);
    check_measure_snap(!snap_position(chart, index, {measure_id_t{6}, 959}, {4}));
    check_measure_snap(snap_canvas_row(chart, index, index.total_rows - 1, {4}, index.total_rows) == 1128);
}

TEST_CASE("Integer snapping preserves large rows measure heads and midpoint ties",
          "[unit][core][snap][shared-arithmetic]") {
    auto chart = snap_chart();
    chart.divisions_per_standard_measure = std::uint64_t{1} << 54;
    const auto built = measure_index_t::from(chart);
    REQUIRE(built);
    const auto& index = *built;
    const auto row = (std::uint64_t{1} << 53) + 1;
    const snap_grid_t exact{.denominator = chart.divisions_per_standard_measure};
    check_measure_snap(snap_absolute_row(chart, index, row, exact) == row);
    check_measure_snap(snap_canvas_row_or_exact(chart, index, row, exact, index.total_rows) == row);
    check_measure_snap(snap_position(chart, index, {measure_id_t{4}, row}, exact) ==
                       local_position_t{measure_id_t{4}, row});
    const snap_grid_t two_rows{.denominator = chart.divisions_per_standard_measure / 2};
    check_measure_snap(snap_absolute_row(chart, index, row, two_rows) == row - 1);
    check_measure_snap(snap_absolute_row(chart, index, row + 1, two_rows) == row + 1);
    check_measure_snap(snap_canvas_row_or_exact(chart, index, index.total_rows - 1,
                                               two_rows, index.total_rows) == index.total_rows - 2);
    check_measure_snap(snap_absolute_row(chart, index, index.total_rows + row, exact) == index.total_rows + row);
    check_measure_snap(grid_phase(chart, index, chart.divisions_per_standard_measure + row,
                                  quantization_space_t::standard_measure).row == row);
}

TEST_CASE("strict grid neighbors include tiny measure heads and preserve fractional origins",
          "[unit][core][snap][measure-local-snap]") {
    const auto chart = fractional_snap_chart();
    const auto index = measure_index_t::from(chart).value();
    const auto next = [&](long double row, bool forward) {
        return adjacent_snap_canvas_row(chart, index, row, {4}, index.total_rows, forward);
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
    const auto index = measure_index_t::from(chart).value();
    const auto tail_end = index.total_rows + 2 * chart.divisions_per_standard_measure;
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 1128, {4}, tail_end, true) == 1368);
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 1368, {4}, tail_end, true) == 1608);
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 1368, {4}, tail_end, false) == 1128);
    check_measure_snap(snap_canvas_row(chart, index, 1450, {4}, tail_end) == 1368);
    check_measure_snap(snap_canvas_row(chart, index, 1490, {4}, tail_end) == 1608);
    check_measure_snap(adjacent_snap_canvas_row(chart, index, 2328, {4}, tail_end, true) == 2568);
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

TEST_CASE("Fitting snap uses local measure phase and exact common divisions", "[unit][core][snap][fitting]") {
    auto chart = snap_chart();
    chart.measures.front().definition = {{1, 2}, std::nullopt};
    const auto index = measure_index_t::from(chart).value();
    const std::array points{local_position_t{measure_id_t{3}, 192}, local_position_t{measure_id_t{4}, 384}};
    CHECK(fitting_snap_denominator(chart, index, points) == 5);
    const std::array mixed{points[0], local_position_t{measure_id_t{4}, 48}};
    CHECK(fitting_snap_denominator(chart, index, mixed) == 20);
    const std::array odd{local_position_t{measure_id_t{4}, 7}};
    CHECK(fitting_snap_denominator(chart, index, odd) == 960);
    CHECK_FALSE(fitting_snap_denominator(chart, index, {}));
    const std::array invalid{local_position_t{measure_id_t{4}, 960}};
    CHECK_FALSE(fitting_snap_denominator(chart, index, invalid));
}

namespace {
// A 5/4 measure (1200 rows at 960 per standard measure) between two 4/4 measures.
auto five_four_chart() -> chart_document_t {
    auto chart = snap_chart();
    chart.measures = {
        {.id = measure_id_t{3}},
        {.id = measure_id_t{4}, .definition = {.length = {5, 4}}},
        {.id = measure_id_t{5}},
    };
    return chart;
}

constexpr snap_grid_t authored_quarter{.denominator = 4,
                                       .space = quantization_space_t::authored_measure};
} // namespace

TEST_CASE("authored snap space divides each measure's own length",
          "[unit][core][snap][authored-snap]") {
    const auto chart = five_four_chart();
    const auto index = measure_index_t::from(chart).value();
    // Standard quarters keep 240-row steps, so the 5/4 measure holds five of them.
    check_measure_snap(snap_absolute_row(chart, index, 960 + 250, {4}) == 960 + 240);
    // Authored quarters split its 1200 rows into four 300-row steps.
    check_measure_snap(snap_step(chart, authored_quarter, 1200) == 300);
    check_measure_snap(snap_absolute_row(chart, index, 960 + 250, authored_quarter) == 960 + 300);
    check_measure_snap(snap_absolute_row(chart, index, 960 + 1040, authored_quarter) == 960 + 900);
    check_measure_snap(snap_absolute_row(chart, index, 960 + 1060, authored_quarter) == 960 + 1200);
    check_measure_snap(snap_position(chart, index, {measure_id_t{4}, 610}, authored_quarter) ==
                       local_position_t{measure_id_t{4}, 600});
    // 4/4 measures are the same in both spaces.
    check_measure_snap(snap_absolute_row(chart, index, 130, authored_quarter) == 240);
}

TEST_CASE("authored snap neighbors step through the measure they land in",
          "[unit][core][snap][authored-snap]") {
    const auto chart = five_four_chart();
    const auto index = measure_index_t::from(chart).value();
    const auto next = [&](long double row, bool forward) {
        return adjacent_snap_canvas_row(chart, index, row, authored_quarter, index.total_rows,
                                        forward);
    };
    check_measure_snap(next(720, true) == 960);
    check_measure_snap(next(960, true) == 1260);
    check_measure_snap(next(1860, true) == 2160);
    // Backward from the next head uses the 5/4 measure's step, not the following measure's.
    check_measure_snap(next(2160, false) == 1860);
    check_measure_snap(next(2160, true) == 2400);
}

TEST_CASE("authored snap keeps standard steps where the measure cannot be divided exactly",
          "[unit][core][snap][authored-snap]") {
    const auto chart = five_four_chart();
    const auto index = measure_index_t::from(chart).value();
    const snap_grid_t authored_32nd{.denominator = 32,
                                    .space = quantization_space_t::authored_measure};
    // 1200 / 32 is not a whole row: the 5/4 measure falls back to 960 / 32 = 30 rows.
    check_measure_snap(snap_step(chart, authored_32nd, 1200) == 30);
    check_measure_snap(snap_absolute_row(chart, index, 960 + 44, authored_32nd) == 960 + 30);
    // The denominator must still divide the chart's standard measure.
    check_measure_snap(!snap_step(chart, {.denominator = 7, .space = quantization_space_t::authored_measure}, 1400));
}

TEST_CASE("exact-or-snap helpers keep the input for a zero denominator",
          "[unit][core][snap]") {
    const auto chart = five_four_chart();
    const auto index = measure_index_t::from(chart).value();
    check_measure_snap(snap_canvas_row_or_exact(chart, index, 1001, {}, index.total_rows) == 1001);
    check_measure_snap(snap_canvas_row_or_exact(chart, index, 1001, authored_quarter,
                                                index.total_rows) == 960);
    check_measure_snap(!snap_canvas_row_or_exact(chart, index, index.total_rows, {}, index.total_rows));
    check_measure_snap(snap_position_or_exact(chart, index, {measure_id_t{4}, 7}, {}) ==
                       local_position_t{measure_id_t{4}, 7});
    check_measure_snap(!snap_position_or_exact(chart, index, {measure_id_t{9}, 0}, {}));
}

TEST_CASE("grid phase measures colours against the active space's measure",
          "[unit][core][snap][authored-snap]") {
    const auto chart = five_four_chart();
    const auto index = measure_index_t::from(chart).value();
    const auto standard = grid_phase(chart, index, 960 + 300, quantization_space_t::standard_measure);
    check_measure_snap(standard.row == 300 && standard.period == 960);
    const auto authored = grid_phase(chart, index, 960 + 300, quantization_space_t::authored_measure);
    check_measure_snap(authored.row == 300 && authored.period == 1200);
    // Past the chart end both spaces use standard measures.
    const auto tail = grid_phase(chart, index, index.total_rows + 1000,
                                 quantization_space_t::authored_measure);
    check_measure_snap(tail.row == 40 && tail.period == 960);
}

TEST_CASE("adjacent snap steps come from the measure the move enters",
          "[unit][core][snap][authored-snap]") {
    const auto chart = five_four_chart();
    const auto index = measure_index_t::from(chart).value();
    check_measure_snap(adjacent_snap_step(chart, index, 960, authored_quarter, true) == 300);
    // Moving back from the 5/4 head enters the 4/4 measure before it.
    check_measure_snap(adjacent_snap_step(chart, index, 960, authored_quarter, false) == 240);
    check_measure_snap(adjacent_snap_step(chart, index, 2160, authored_quarter, false) == 300);
    check_measure_snap(adjacent_snap_step(chart, index, 960, {4}, true) == 240);
    check_measure_snap(!adjacent_snap_step(chart, index, 960, {7}, true));
}

TEST_CASE("snap length steps match the rows they snap to",
          "[unit][core][snap][authored-snap]") {
    check_measure_snap(snap_length_step(960, {4}, {5, 4}) == rational_t{1, 4});
    check_measure_snap(snap_length_step(960, authored_quarter, {5, 4}) == rational_t{5, 16});
    // 5/4 at authored 1/32 is not whole rows, so it keeps standard 1/32 steps.
    check_measure_snap(snap_length_step(960, {.denominator = 32, .space = quantization_space_t::authored_measure},
                                        {5, 4}) == rational_t{1, 32});
    check_measure_snap(!snap_length_step(960, {0}, {1, 1}));
}
