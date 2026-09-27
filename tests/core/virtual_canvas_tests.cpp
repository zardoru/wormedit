#include <wormedit/core/virtual_canvas.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

auto chart_with_two_measures(wormedit::format_family_t family) -> wormedit::chart_document_t {
    wormedit::chart_document_t chart;
    chart.id = wormedit::chart_id_t{1};
    chart.source_family = family;
    chart.divisions_per_standard_measure = 960;
    chart.measures = {
        {.id = wormedit::measure_id_t{10}, .definition = {{1, 1}, wormedit::meter_t{4, 4}}},
        {.id = wormedit::measure_id_t{11}, .definition = {{1, 2}, wormedit::meter_t{2, 4}}}};
    return chart;
}

} // namespace

TEST_CASE("BMS virtual canvas addresses #000 through #999 without adding measure buckets",
          "[unit][core][virtual-canvas]") {
    const auto chart = chart_with_two_measures(wormedit::format_family_t::bms);
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::virtual_canvas_t>(built));
    const auto& canvas = std::get<wormedit::virtual_canvas_t>(built);
    CHECK(chart.measures.size() == 2);
    CHECK(canvas.authored_index.entries.size() == 2);
    CHECK(canvas.measure_count == 1'000);
    CHECK(canvas.total_rows == 959'520);
    REQUIRE(canvas.measure_at(0));
    CHECK(canvas.measure_at(0)->authored);
    CHECK(canvas.measure_at(0)->begin_row == 0);
    REQUIRE(canvas.measure_at(1));
    CHECK(canvas.measure_at(1)->begin_row == 960);
    CHECK(canvas.measure_at(1)->row_capacity == 480);
    REQUIRE(canvas.measure_at(2));
    CHECK_FALSE(canvas.measure_at(2)->authored);
    CHECK(canvas.measure_at(2)->begin_row == 1'440);
    REQUIRE(canvas.measure_at(999));
    CHECK(canvas.measure_at(999)->begin_row == 958'560);
    CHECK_FALSE(canvas.measure_at(1'000));
    REQUIRE(canvas.measure_containing(1'439));
    CHECK(canvas.measure_containing(1'439)->number == 1);
    REQUIRE(canvas.measure_containing(1'440));
    CHECK(canvas.measure_containing(1'440)->number == 2);
    REQUIRE(canvas.measure_containing(canvas.total_rows - 1));
    CHECK(canvas.measure_containing(canvas.total_rows - 1)->number == 999);
    CHECK_FALSE(canvas.measure_containing(canvas.total_rows));
    CHECK(chart.measures.size() == 2);
}

TEST_CASE("non-BMS virtual canvas stops at authored measure geometry",
          "[unit][core][virtual-canvas]") {
    const auto chart = chart_with_two_measures(wormedit::format_family_t::native_project);
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::virtual_canvas_t>(built));
    const auto& canvas = std::get<wormedit::virtual_canvas_t>(built);
    CHECK(canvas.measure_count == 2);
    CHECK(canvas.total_rows == 1'440);
    CHECK_FALSE(canvas.measure_at(2));
    CHECK_FALSE(canvas.measure_containing(1'440));
}

TEST_CASE("virtual BMS tail rejects row overflow without changing the chart",
          "[unit][core][virtual-canvas]") {
    auto chart = chart_with_two_measures(wormedit::format_family_t::bms);
    chart.measures.resize(1);
    chart.divisions_per_standard_measure = std::numeric_limits<std::uint64_t>::max() / 999 + 1;
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::diagnostic_list_t>(built));
    CHECK_FALSE(std::get<wormedit::diagnostic_list_t>(built).empty());
    CHECK(chart.measures.size() == 1);
}
