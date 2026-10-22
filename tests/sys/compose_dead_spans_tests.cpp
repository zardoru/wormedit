#include <algorithm>
#include <limits>
#include "sys/compose/compose_dead_spans.h"

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

auto project(const chart_document_t& chart, std::size_t limit = 64) -> dead_spans_view_t {
    const auto measures = measure_index_t::from(chart);
    REQUIRE(measures.has_value());
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    return dead_spans_view_t::from(chart, measures.value(), *timing.value, limit);
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

TEST_CASE("pause bands sit on their time side of the row and warps cover their rows",
          "[unit][sys][dead-spans]") {
    dead_spans_view_t projection;
    projection.spans = {{.kind = dead_span_kind_t::delay, .object = object_id_t{1},
                         .begin_row = 100, .end_row = 100, .seconds = {1, 1}},
                        {.kind = dead_span_kind_t::stop, .object = object_id_t{2},
                         .begin_row = 200, .end_row = 200, .seconds = {1, 1}},
                        {.kind = dead_span_kind_t::warp, .object = object_id_t{3},
                         .begin_row = 300, .end_row = 400, .seconds = {0, 1}}};
    const auto up = build_dead_span_bands_view(projection, 1.0, 0.0, 1000.0, false, 10.0);
    REQUIRE(up.size() == 3);
    CHECK(up[0].top == 90.0);
    CHECK(up[0].bottom == 100.0);
    CHECK(up[1].top == 200.0);
    CHECK(up[1].bottom == 210.0);
    CHECK(up[2].top == 300.0);
    CHECK(up[2].bottom == 400.0);
    CHECK(pointer_in_dead_span(up, 95.0));
    CHECK_FALSE(pointer_in_dead_span(up, 100.0));
    CHECK(pointer_in_dead_span(up, 399.0));
    const auto down = build_dead_span_bands_view(projection, 1.0, 0.0, 1000.0, true, 10.0);
    CHECK(down[1].top == 790.0);
    CHECK(down[1].bottom == 800.0);
    CHECK(down[0].top == 900.0);
    CHECK(down[0].bottom == 910.0);
    const auto scrolled = build_dead_span_bands_view(projection, 1.0, 250.0, 100.0, false, 10.0);
    REQUIRE(scrolled.size() == 1);
    CHECK(scrolled[0].top == 50.0);
}

namespace {
auto project_pause_with_boundaries(signed_rational_t earlier, signed_rational_t later)
    -> dead_spans_view_t {
    auto chart = base_chart();
    chart.pauses = {pause(20, 240, pause_kind_t::stop, 1)};
    const auto measures = measure_index_t::from(chart);
    REQUIRE(measures.has_value());
    auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    auto row = std::ranges::find(timing.value->rows, std::uint64_t{240}, &timing_row_state_t::absolute_row);
    REQUIRE(row != timing.value->rows.end());
    row->before_delays_seconds = earlier;
    row->event_seconds = earlier;
    row->after_stops_seconds = later;
    return dead_spans_view_t::from(chart, measures.value(), *timing.value, 64);
}
void check_reduced_pause_span(signed_rational_t earlier, signed_rational_t later,
                             signed_rational_t expected) {
    const auto projection = project_pause_with_boundaries(earlier, later);
    REQUIRE(projection.spans.size() == 1);
    CHECK(projection.spans.front().seconds == expected);
    CHECK_FALSE(projection.truncated);
}
}
TEST_CASE("dead spans reduce denominators before checking representability", "[sys][dead-spans][shared-arithmetic]") {
    check_reduced_pause_span({1, 10'000'000'000ULL}, {2, 10'000'000'000ULL}, {1, 10'000'000'000ULL});
}
TEST_CASE("dead spans reduce scaled numerators before checking representability", "[sys][dead-spans][shared-arithmetic]") {
    check_reduced_pause_span({1, 1'000'000}, {20'000'000'000'000LL, 1'000'000}, {19'999'999'999'999LL, 1'000'000});
}
TEST_CASE("dead spans subtract opposite signed boundaries without signed wide overflow", "[sys][dead-spans][shared-arithmetic]") {
    const auto denominator = std::numeric_limits<std::uint64_t>::max();
    check_reduced_pause_span({std::numeric_limits<std::int64_t>::min(), denominator},
                             {std::numeric_limits<std::int64_t>::max(), denominator}, {1, 1});
}
