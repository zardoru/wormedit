#include "sys/compose_kiai_spans.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto base_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {
        {.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
        {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}},
    };
    return chart;
}

void add_kiai(chart_document_t& chart, std::uint64_t id, std::size_t measure, std::uint64_t row,
              bool enabled) {
    chart.kiai.push_back({.id = object_id_t{id},
                          .position = {chart.measures[measure].id, row},
                          .enabled = enabled,
                          .provenance = {.source_order = id}});
}

auto project(const chart_document_t& chart, std::size_t limit = 64) -> kiai_spans_view_t {
    const auto measures = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(measures));
    return kiai_spans_view_t::from(chart, std::get<measure_index_t>(measures), limit);
}

} // namespace

TEST_CASE("kiai on/off pairs project to one half-open span", "[unit][sys][kiai-spans]") {
    auto chart = base_chart();
    add_kiai(chart, 1, 0, 240, true);
    add_kiai(chart, 2, 1, 0, false);
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 1);
    CHECK(result.spans[0] == kiai_span_t{.begin_row = 240,
                                         .end_row = 960,
                                         .head = object_id_t{1},
                                         .tail = object_id_t{2}});
    CHECK_FALSE(result.truncated);
}

TEST_CASE("an open kiai extends to the end of the chart", "[unit][sys][kiai-spans]") {
    auto chart = base_chart();
    add_kiai(chart, 1, 1, 480, true);
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 1);
    CHECK(result.spans[0].begin_row == 1440);
    CHECK(result.spans[0].end_row == 1920);
    CHECK(result.spans[0].open_ended);
    CHECK_FALSE(result.spans[0].tail);
}

TEST_CASE("duplicate and overlapping kiai events merge", "[unit][sys][kiai-spans]") {
    auto chart = base_chart();
    add_kiai(chart, 1, 0, 100, true);
    add_kiai(chart, 2, 0, 200, true);
    add_kiai(chart, 3, 0, 300, false);
    add_kiai(chart, 4, 0, 400, false);
    add_kiai(chart, 5, 0, 500, true);
    add_kiai(chart, 6, 0, 600, false);
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 2);
    CHECK(result.spans[0].begin_row == 100);
    CHECK(result.spans[0].end_row == 300);
    CHECK(result.spans[0].head == object_id_t{1});
    CHECK(result.spans[1].begin_row == 500);
    CHECK(result.spans[1].end_row == 600);
}

TEST_CASE("leading off, same-row pairs and unordered storage are tolerated",
          "[unit][sys][kiai-spans]") {
    auto chart = base_chart();
    add_kiai(chart, 3, 0, 700, false);
    add_kiai(chart, 2, 0, 500, false);
    add_kiai(chart, 1, 0, 500, true);
    add_kiai(chart, 4, 0, 100, false);
    add_kiai(chart, 5, 0, 300, true);
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 1);
    CHECK(result.spans[0].begin_row == 300);
    CHECK(result.spans[0].end_row == 500);
    CHECK(project(base_chart()).spans.empty());
}

TEST_CASE("kiai projection is bounded by the limit", "[unit][sys][kiai-spans]") {
    auto chart = base_chart();
    add_kiai(chart, 1, 0, 100, true);
    add_kiai(chart, 2, 0, 200, false);
    add_kiai(chart, 3, 0, 300, true);
    add_kiai(chart, 4, 0, 400, false);
    const auto result = project(chart, 1);
    CHECK(result.spans.size() == 1);
    CHECK(result.truncated);
}
