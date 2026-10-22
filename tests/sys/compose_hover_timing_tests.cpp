#include "sys/compose/views/compose_hover_timing.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <variant>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto hover_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {
        {.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
        {.id = measure_id_t{2}, .definition = {{3, 4}, meter_t{6, 8}}},
    };
    chart.bpms = {
        {.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
         .beats_per_minute = {120, 1}},
        {.id = object_id_t{2}, .position = {measure_id_t{1}, 480},
         .beats_per_minute = {60, 1}},
    };
    chart.pauses = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 480},
                     .kind = pause_kind_t::stop, .duration = {1, 1},
                     .source_unit = pause_unit_t::seconds}};
    return chart;
}

auto label_at(const chart_document_t& chart, std::int64_t frame,
              std::uint32_t sample_rate = 48'000) -> std::string {
    const auto measure_result = measure_index_t::from(chart);
    REQUIRE(measure_result.has_value());
    const auto timing_result = timing_index_t::from(chart);
    REQUIRE(timing_result.value);
    std::array<char, 64> label{};
    REQUIRE(format_compose_hover_timing(chart, measure_result.value(),
        *timing_result.value, frame, sample_rate, label.data(), label.size()));
    return label.data();
}

} // namespace

TEST_CASE("Compose hover labels follow BPM changes, stops and measure meter",
          "[sys][compose-hover-timing]") {
    const auto chart = hover_chart();
    CHECK(label_at(chart, 24'000) == "Measure 1 · beat 2 · 0:00.500");
    CHECK(label_at(chart, 72'000) == "Measure 1 · beat 3 · 0:01.500");
    CHECK(label_at(chart, 144'000) == "Measure 1 · beat 4 · 0:03.000");
    CHECK(label_at(chart, 216'000) == "Measure 2 · beat 2 · 0:04.500");
}

TEST_CASE("Compose hover formatter prints signed time before the chart begins",
          "[sys][compose-hover-timing]") {
    const auto chart = hover_chart();
    CHECK(label_at(chart, -24'000).ends_with("-0:00.500"));
}

TEST_CASE("Compose hover continues exact beat timing in virtual measures after the chart",
          "[sys][compose-hover-timing]") {
    auto chart = hover_chart();
    chart.bpms.push_back({.id = object_id_t{4},
        .position = {measure_id_t{2}, 600}, .beats_per_minute = {137, 2}});
    CHECK(label_at(chart, 2'141, 274) == "Measure 3 · beat 2 · 0:07.814");
}

TEST_CASE("Compose hover time is signed before the beat-zero audio offset",
          "[sys][compose-hover-timing]") {
    auto chart = hover_chart();
    chart.beat_zero_audio_offset_seconds = {1, 1};
    CHECK(label_at(chart, 0).ends_with("-0:01.000"));
}

TEST_CASE("Compose hover formatter rejects missing output and sample rate",
          "[sys][compose-hover-timing]") {
    const auto chart = hover_chart();
    const auto measure_result = measure_index_t::from(chart);
    const auto timing_result = timing_index_t::from(chart);
    REQUIRE(timing_result.value);
    const auto& measures = measure_result.value();
    std::array<char, 64> label{};
    CHECK_FALSE(format_compose_hover_timing(chart, measures, *timing_result.value,
        0, 0, label.data(), label.size()));
    CHECK_FALSE(format_compose_hover_timing(chart, measures, *timing_result.value,
        0, 48'000, nullptr, label.size()));
    std::array<char, 4> too_small{};
    CHECK_FALSE(format_compose_hover_timing(chart, measures, *timing_result.value,
        0, 48'000, too_small.data(), too_small.size()));
}
