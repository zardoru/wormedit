#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <span>
#include <vector>

using namespace wormedit;

namespace {

auto base_chart() -> chart_document_t {
    chart_document_t chart{
        .id = chart_id_t{1},
        .divisions_per_standard_measure = 960,
        .beat_zero_audio_offset_seconds = {1, 1},
    };
    chart.measures = {
        {.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
        {.id = measure_id_t{2}, .definition = {{3, 4}, meter_t{3, 4}}},
    };
    chart.bpms = {
        {.id = object_id_t{10}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}},
        {.id = object_id_t{11}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {60, 1}},
    };
    return chart;
}

auto seconds_at(const timing_index_t& timing, std::uint64_t row,
                timing_boundary_side_t side = timing_boundary_side_t::event) -> signed_rational_t {
    const auto result = timing_at(timing, row, side);
    REQUIRE(result);
    return *result.seconds;
}

} // namespace

TEST_CASE("exact timing distinguishes delay event and stop departure boundaries",
          "[unit][core][timing]") {
    auto chart = base_chart();
    chart.pauses = {
        {.id = object_id_t{20},
         .position = {measure_id_t{1}, 240},
         .kind = pause_kind_t::stop,
         .duration = {48, 1},
         .source_unit = pause_unit_t::bms_stop_ticks,
         .provenance = {.source_order = 1}},
        {.id = object_id_t{21},
         .position = {measure_id_t{1}, 240},
         .kind = pause_kind_t::delay,
         .duration = {500, 1},
         .source_unit = pause_unit_t::milliseconds,
         .provenance = {.source_order = 2}},
    };

    const auto built = build_timing_index(chart);
    REQUIRE(built);
    REQUIRE(built.diagnostics.empty());
    const auto& timing = *built.index;
    CHECK(seconds_at(timing, 0) == signed_rational_t{1, 1});
    CHECK(seconds_at(timing, 240, timing_boundary_side_t::before_delays) ==
          signed_rational_t{3, 2});
    CHECK(seconds_at(timing, 240) == signed_rational_t{2, 1});
    CHECK(seconds_at(timing, 240, timing_boundary_side_t::after_stops) == signed_rational_t{5, 2});
    CHECK(seconds_at(timing, 480) == signed_rational_t{3, 1});
    CHECK(seconds_at(timing, 960) == signed_rational_t{4, 1});
    CHECK(seconds_at(timing, 1'200) == signed_rational_t{5, 1});
}

TEST_CASE("warps consume no elapsed time and suppress interior pauses and events",
          "[unit][core][timing][warp]") {
    auto chart = base_chart();
    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 480}, {measure_id_t{2}, 0}}});
    chart.pauses.push_back({.id = object_id_t{31},
                            .position = {measure_id_t{1}, 720},
                            .kind = pause_kind_t::stop,
                            .duration = {2, 1},
                            .source_unit = pause_unit_t::seconds});

    const auto built = build_timing_index(chart);
    REQUIRE(built);
    REQUIRE(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].severity == diagnostic_severity_t::warning);
    const auto& timing = *built.index;
    const auto begin = timing_at(timing, 480);
    const auto inside = timing_at(timing, 720);
    const auto end = timing_at(timing, 960);
    REQUIRE(begin);
    REQUIRE(inside);
    REQUIRE(end);
    CHECK(begin.inside_warp);
    CHECK(inside.inside_warp);
    CHECK_FALSE(end.inside_warp);
    CHECK(*begin.seconds == signed_rational_t{2, 1});
    CHECK(*inside.seconds == signed_rational_t{2, 1});
    CHECK(*end.seconds == signed_rational_t{2, 1});
    CHECK(seconds_at(timing, 1'200) == signed_rational_t{3, 1});
}

TEST_CASE("same-row BPM source order controls beat-derived pause duration",
          "[unit][core][timing][ordering]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.bpms[0].provenance.source_order = 1;
    chart.bpms.push_back({.id = object_id_t{12},
                          .position = {measure_id_t{1}, 0},
                          .beats_per_minute = {60, 1},
                          .provenance = {.source_order = 2}});
    chart.pauses.push_back({.id = object_id_t{22},
                            .position = {measure_id_t{1}, 0},
                            .kind = pause_kind_t::stop,
                            .duration = {1, 1},
                            .source_unit = pause_unit_t::beats});

    const auto built = build_timing_index(chart);
    REQUIRE(built);
    CHECK(seconds_at(*built.index, 0) == signed_rational_t{});
    CHECK(seconds_at(*built.index, 0, timing_boundary_side_t::after_stops) ==
          signed_rational_t{1, 1});
    CHECK(seconds_at(*built.index, 240) == signed_rational_t{2, 1});
}

TEST_CASE("sample-frame conversion is exact with explicit rounding and negative bounds",
          "[unit][core][timing][sample-frame]") {
    CHECK(sample_frame_at({1, 2}, 44'100).frame == 22'050);
    CHECK(sample_frame_at({1, 2}, 1, sample_rounding_t::floor).frame == 0);
    CHECK(sample_frame_at({1, 2}, 1, sample_rounding_t::ceil).frame == 1);
    CHECK(sample_frame_at({1, 2}, 1, sample_rounding_t::nearest_ties_to_even).frame == 0);
    CHECK(sample_frame_at({3, 2}, 1, sample_rounding_t::nearest_ties_to_even).frame == 2);
    const auto negative = sample_frame_at({-1, 2}, 44'100);
    REQUIRE_FALSE(negative);
    CHECK(negative.error->code == timing_error_code_t::negative_sample_frame);
}

TEST_CASE("preview schedule keeps separate sorted queues and stable simultaneous order",
          "[unit][core][timing][preview]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.bgm_source_lanes = {
        {.id = object_id_t{40}, .name = "late", .source_order = 2},
        {.id = object_id_t{41}, .name = "early", .source_order = 1},
    };
    chart.measures[0].notes = {
        {.id = object_id_t{50},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .sound = resource_id_t{5},
         .provenance = {.source_order = 4}},
        {.id = object_id_t{51},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{2},
         .sound = resource_id_t{6},
         .provenance = {.source_order = 3}},
    };
    chart.measures[0].bgm_events = {
        {.id = object_id_t{60},
         .position = {measure_id_t{1}, 240},
         .source_lane = object_id_t{40},
         .sound = resource_id_t{7},
         .provenance = {.source_order = 1}},
        {.id = object_id_t{61},
         .position = {measure_id_t{1}, 240},
         .source_lane = object_id_t{41},
         .sound = resource_id_t{8},
         .provenance = {.source_order = 9}},
    };
    chart.bga_events = {
        {.id = object_id_t{70},
         .position = {measure_id_t{1}, 240},
         .resource = resource_id_t{9},
         .kind = visual_event_kind_t::layer,
         .provenance = {.source_order = 1}},
        {.id = object_id_t{71},
         .position = {measure_id_t{1}, 240},
         .resource = resource_id_t{10},
         .kind = visual_event_kind_t::background,
         .provenance = {.source_order = 9}},
    };

    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto built = build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000});
    REQUIRE(built);
    const auto& schedule = *built.schedule;
    REQUIRE(schedule.playable.size() == 2);
    REQUIRE(schedule.bgm.size() == 2);
    REQUIRE(schedule.bga.size() == 2);
    CHECK(schedule.playable[0].object == object_id_t{51});
    CHECK(schedule.playable[1].object == object_id_t{50});
    CHECK(schedule.bgm[0].object == object_id_t{61});
    CHECK(schedule.bgm[1].object == object_id_t{60});
    CHECK(schedule.bga[0].object == object_id_t{71});
    CHECK(schedule.bga[1].object == object_id_t{70});
    CHECK(schedule.playable[0].sample_frame == 24'000);
    REQUIRE(schedule.metronome.size() == 7);
    CHECK(schedule.metronome[0].accent == metronome_accent_t::measure);
    CHECK(schedule.metronome[4].accent == metronome_accent_t::measure);
}

TEST_CASE("preview queue seek is binary-searchable and advancement is allocation free",
          "[unit][core][timing][preview][queue]") {
    const std::array<preview_audio_event_t, 4> events{{
        {.sample_frame = 10},
        {.sample_frame = 20},
        {.sample_frame = 20},
        {.sample_frame = 30},
    }};
    preview_queue_head_t head;
    seek_preview_queue(std::span<const preview_audio_event_t>{events}, head, 20);
    CHECK(head.index == 1);
    const auto first =
        advance_preview_queue(std::span<const preview_audio_event_t>{events}, head, 21);
    REQUIRE(first.size() == 2);
    CHECK(first[0].sample_frame == 20);
    CHECK(head.index == 3);
    const auto second =
        advance_preview_queue(std::span<const preview_audio_event_t>{events}, head, 40);
    REQUIRE(second.size() == 1);
    CHECK(second[0].sample_frame == 30);
}

TEST_CASE("timing and preview capacities reject atomically", "[unit][core][timing][bounds]") {
    auto chart = base_chart();
    const auto timing_too_small = build_timing_index(chart, {.maximum_boundaries = 1});
    REQUIRE_FALSE(timing_too_small);
    CHECK(timing_too_small.diagnostics.back().code == timing_error_code_t::capacity_exceeded);
    const auto events_too_small =
        build_timing_index(chart, {.maximum_boundaries = 100, .maximum_timing_events = 1});
    REQUIRE_FALSE(events_too_small);
    CHECK(events_too_small.diagnostics.back().code == timing_error_code_t::capacity_exceeded);

    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    chart.measures[0].notes.push_back({.id = object_id_t{80},
                                       .position = {measure_id_t{1}, 0},
                                       .lane = lane_id_t{1},
                                       .sound = resource_id_t{1}});
    const auto schedule = build_preview_schedule(
        chart, *timing.index, {.maximum_playable_events = 0, .include_metronome = false});
    REQUIRE_FALSE(schedule);
    CHECK_FALSE(schedule.schedule.has_value());
    CHECK(schedule.diagnostics.back().code == timing_error_code_t::capacity_exceeded);

    auto changed = chart;
    changed.bpms[0].beats_per_minute = {90, 1};
    const auto stale = build_preview_schedule(changed, *timing.index, {.include_metronome = false});
    REQUIRE_FALSE(stale);
    CHECK(stale.diagnostics.back().code == timing_error_code_t::invalid_document);
}

TEST_CASE("fifty-thousand-note preview compilation stays within the interaction safety ceiling",
          "[unit][core][timing][preview][stress]") {
    chart_document_t chart{
        .id = chart_id_t{1},
        .divisions_per_standard_measure = 960,
    };
    chart.measures.reserve(1'000);
    auto next_note = std::uint64_t{10'000};
    for (std::uint64_t measure = 0; measure < 1'000; ++measure) {
        measure_chunk_t chunk{.id = measure_id_t{measure + 1}};
        chunk.notes.reserve(50);
        for (std::uint64_t note = 0; note < 50; ++note) {
            chunk.notes.push_back({.id = object_id_t{next_note++},
                                   .position = {chunk.id, note * 19},
                                   .lane = lane_id_t{note % 8 + 1},
                                   .sound = resource_id_t{1}});
        }
        chart.measures.push_back(std::move(chunk));
    }
    chart.bpms.push_back(
        {.id = object_id_t{1}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}});
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto started = std::chrono::steady_clock::now();
    const auto schedule = build_preview_schedule(
        chart, *timing.index, {.maximum_playable_events = 50'000, .include_metronome = false});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(schedule);
    REQUIRE(schedule.schedule->playable.size() == 50'000);
    CHECK(std::ranges::is_sorted(schedule.schedule->playable, [](const auto& left,
                                                                 const auto& right) {
        return left.sample_frame < right.sample_frame ||
               (left.sample_frame == right.sample_frame && left.object.value < right.object.value);
    }));
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    INFO("50k preview compilation took " << milliseconds << " ms");
    if (milliseconds > 125) {
        WARN("50k preview compilation exceeded the 125 ms target on this host");
    }
    CHECK(milliseconds < 500);
}
