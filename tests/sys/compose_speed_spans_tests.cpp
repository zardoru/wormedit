#include "sys/compose_speed_spans.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto base_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
                      {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}};
    chart.bpms = {{.id = object_id_t{10},
                   .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    return chart;
}

auto speed(std::uint64_t id, std::size_t measure, std::uint64_t row, std::int64_t numerator,
           rational_t transition, bool seconds) -> speed_event_t {
    return {.id = object_id_t{id},
            .position = {measure_id_t{measure}, row},
            .multiplier = {numerator, 1},
            .transition_beats = transition,
            .transition_is_seconds = seconds,
            .provenance = {.source_order = id}};
}

auto project(const chart_document_t& chart, std::size_t limit = 64) -> speed_spans_t {
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    return project_speed_spans(chart, *timing.index, limit);
}

} // namespace

TEST_CASE("speed spans cover each transition and are clipped by the next event",
          "[unit][sys][speed-spans]") {
    auto chart = base_chart();
    // 2 beats of transition from row 240 (one beat = 240 rows), then a replacement at row 480.
    chart.speeds = {speed(20, 1, 240, 2, {2, 1}, false), speed(21, 1, 480, 3, {0, 1}, false)};
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 2);
    CHECK(result.spans[0].object == object_id_t{20});
    CHECK(result.spans[0].begin_row == Catch::Approx(240.0));
    CHECK(result.spans[0].end_row == Catch::Approx(480.0));
    CHECK(result.spans[0].clipped);
    CHECK(result.spans[0].multiplier == Catch::Approx(2.0));
    CHECK_FALSE(result.spans[0].transition_is_seconds);
    CHECK(result.spans[1].multiplier == Catch::Approx(3.0));
    CHECK_FALSE(result.truncated);
}

TEST_CASE("a seconds transition reports its length in seconds and a bounded list reports truncation",
          "[unit][sys][speed-spans]") {
    auto chart = base_chart();
    // At 120 BPM one second is two beats = 480 rows.
    chart.speeds = {speed(20, 1, 0, 2, {1, 1}, true), speed(21, 2, 0, 4, {0, 1}, false)};
    const auto result = project(chart);
    REQUIRE(result.spans.size() == 2);
    CHECK(result.spans[0].transition_is_seconds);
    CHECK(result.spans[0].transition_length == Catch::Approx(1.0));
    CHECK(result.spans[0].end_row == Catch::Approx(480.0));
    CHECK_FALSE(result.spans[0].clipped);
    const auto cut = project(chart, 1);
    CHECK(cut.spans.size() <= 1);
    CHECK(cut.truncated);
}

TEST_CASE("a chart without speeds has no spans", "[unit][sys][speed-spans]") {
    const auto result = project(base_chart());
    CHECK(result.spans.empty());
    CHECK_FALSE(result.truncated);
}
