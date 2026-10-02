#include "sys/compose_dead_spans.h"

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
    chart.bpms = {{.id = object_id_t{10},
                   .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    return chart;
}

auto pause(std::uint64_t id, std::uint64_t row, pause_kind_t kind, std::int64_t seconds)
    -> pause_event_t {
    return {.id = object_id_t{id},
            .position = {measure_id_t{1}, row},
            .kind = kind,
            .duration = {seconds, 1},
            .source_unit = pause_unit_t::seconds};
}

auto project(const chart_document_t& chart, std::size_t limit = 64) -> dead_span_projection_t {
    const auto measures = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(measures));
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    return project_dead_spans(chart, std::get<measure_index_t>(measures), *timing.index, limit);
}

} // namespace

TEST_CASE("dead spans list stops and delays with their elapsed time", "[unit][sys][dead-spans]") {
    auto chart = base_chart();
    chart.pauses = {pause(20, 240, pause_kind_t::stop, 2), pause(21, 480, pause_kind_t::delay, 1)};
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 2);
    CHECK(result.spans[0].kind == dead_span_kind_t::stop);
    CHECK(result.spans[0].begin_row == 240);
    CHECK(result.spans[0].end_row == 240);
    CHECK(result.spans[0].seconds == signed_rational_t{2, 1});
    CHECK(result.spans[1].kind == dead_span_kind_t::delay);
    CHECK(result.spans[1].seconds == signed_rational_t{1, 1});
    CHECK_FALSE(result.truncated);
}

TEST_CASE("dead spans merge pauses sharing a row and kind", "[unit][sys][dead-spans]") {
    auto chart = base_chart();
    chart.pauses = {pause(22, 240, pause_kind_t::stop, 2), pause(21, 240, pause_kind_t::stop, 1),
                    pause(23, 240, pause_kind_t::delay, 1)};
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 2);
    CHECK(result.spans[0].kind == dead_span_kind_t::stop);
    CHECK(result.spans[0].merged_count == 2);
    CHECK(result.spans[0].object == object_id_t{21});
    CHECK(result.spans[0].seconds == signed_rational_t{3, 1});
    CHECK(result.spans[1].kind == dead_span_kind_t::delay);
}

TEST_CASE("a warp is a zero-time half-open row span", "[unit][sys][dead-spans]") {
    auto chart = base_chart();
    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 480}, {measure_id_t{2}, 0}}});
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 1);
    CHECK(result.spans[0].kind == dead_span_kind_t::warp);
    CHECK(result.spans[0].begin_row == 480);
    CHECK(result.spans[0].end_row == 960);
    CHECK(result.spans[0].seconds == signed_rational_t{0, 1});
    CHECK(row_is_in_warp(result, 480));
    CHECK(row_is_in_warp(result, 959));
    CHECK_FALSE(row_is_in_warp(result, 960));
    CHECK_FALSE(row_is_in_warp(result, 479));
}

TEST_CASE("dead spans are ordered by row and bounded by the limit", "[unit][sys][dead-spans]") {
    auto chart = base_chart();
    chart.pauses = {pause(20, 600, pause_kind_t::stop, 1), pause(21, 100, pause_kind_t::stop, 1)};
    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 700}, {measure_id_t{1}, 800}}});
    const auto all = project(chart);
    REQUIRE(all.spans.size() == 3);
    CHECK(all.spans[0].kind == dead_span_kind_t::stop);
    CHECK(all.spans[1].begin_row == 600);
    CHECK(all.spans[2].kind == dead_span_kind_t::warp);
    const auto cut = project(chart, 2);
    CHECK(cut.spans.size() == 2);
    CHECK(cut.truncated);
}

TEST_CASE("dead spans omit pauses that add no time", "[unit][sys][dead-spans]") {
    auto chart = base_chart();
    chart.pauses = {pause(20, 240, pause_kind_t::stop, 0)};
    const auto result = project(chart);
    CHECK(result.spans.empty());
    CHECK(project(base_chart()).spans.empty());
}

TEST_CASE("a pause inside a warp adds no dead span of its own", "[unit][sys][dead-spans]") {
    auto chart = base_chart();
    chart.pauses = {pause(20, 150, pause_kind_t::stop, 1)};
    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 100}, {measure_id_t{1}, 200}}});
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 1);
    CHECK(result.spans[0].kind == dead_span_kind_t::warp);
}
