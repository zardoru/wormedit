#include <wormedit/core/snap.h>

#include <catch2/catch_test_macros.hpp>

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
