#include "sys/compose/compose_sv_graph.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto base_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
                      {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}};
    return chart;
}

void add_scroll(chart_document_t& chart, std::uint64_t id, std::size_t measure, std::uint64_t row,
                std::int64_t numerator, std::uint64_t denominator = 1) {
    chart.scrolls.push_back({.id = object_id_t{id},
                             .position = {chart.measures[measure].id, row},
                             .multiplier = {numerator, denominator},
                             .provenance = {.source_order = id}});
}

auto project(const chart_document_t& chart, std::size_t limit = 64) -> sv_steps_view_t {
    const auto measures = measure_index_t::from(chart);
    REQUIRE(measures.has_value());
    return sv_steps_view_t::from(chart, measures.value(), limit);
}

auto y_of(std::uint64_t row) { return static_cast<double>(row); }

} // namespace

TEST_CASE("sv steps are ordered by row and the last event on a row wins", "[unit][sys][sv-graph]") {
    auto chart = base_chart();
    add_scroll(chart, 3, 1, 0, 2);
    add_scroll(chart, 1, 0, 100, 1, 2);
    add_scroll(chart, 2, 0, 100, 3, 2);
    const auto result = project(chart);
    REQUIRE(result.steps.size() == 2);
    CHECK(result.steps[0] == sv_step_t{100, 1.5, object_id_t{2}});
    CHECK(result.steps[1] == sv_step_t{960, 2.0, object_id_t{3}});
    CHECK(project(chart, 1).truncated);
    CHECK(project(base_chart()).steps.empty());
}

TEST_CASE("sv graph holds each value and jumps vertically at the event", "[unit][sys][sv-graph]") {
    auto chart = base_chart();
    add_scroll(chart, 1, 0, 200, 2);
    add_scroll(chart, 2, 0, 600, 1, 2);
    const auto graph = build_sv_graph(project(chart), 0, 1000, y_of);
    CHECK(graph.minimum == 0.5);
    CHECK(graph.maximum == 2.0);
    // Start at 1.0, jump to 2.0 at row 200, drop to 0.5 at row 600, hold to the end.
    REQUIRE(graph.points.size() == 6);
    CHECK(graph.points[0].y == 0.0);
    CHECK(graph.points[1].y == 200.0);
    CHECK(graph.points[1].x == graph.points[0].x);
    CHECK(graph.points[2].y == 200.0);
    CHECK(graph.points[2].x == 1.0);
    CHECK(graph.points[3].y == 600.0);
    CHECK(graph.points[3].x == 1.0);
    CHECK(graph.points[4].x == 0.0);
    CHECK(graph.points[5].y == 1000.0);
    CHECK(graph.one_x > 0.0);
    CHECK(graph.one_x < 1.0);
}

TEST_CASE("sv graph normalizes to the visible window", "[unit][sys][sv-graph]") {
    auto chart = base_chart();
    add_scroll(chart, 1, 0, 100, 4);
    add_scroll(chart, 2, 0, 500, 2);
    const auto steps = project(chart);
    // A window after the spike sees only the value in force (2.0): a flat line at the center.
    const auto window = build_sv_graph(steps, 600, 800, y_of);
    CHECK(window.minimum == 2.0);
    CHECK(window.maximum == 2.0);
    REQUIRE(window.points.size() == 2);
    CHECK(window.points[0].x == 0.5);
    CHECK(window.points[1].x == 0.5);
    CHECK(window.one_x < 0.0);
    CHECK(build_sv_graph(steps, 10, 10, y_of).points.empty());
}

TEST_CASE("SV projection converts fractions with shared careful rounding", "[sys][sv-graph][shared-arithmetic]") {
    auto chart = base_chart();
    add_scroll(chart, 1, 0, 0, 9'007'199'254'740'993LL, 3);
    const auto projection = project(chart);
    REQUIRE(projection.steps.size() == 1);
    CHECK(projection.steps.front().value == 3'002'399'751'580'331.0);
}
