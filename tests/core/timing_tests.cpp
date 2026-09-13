#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <limits>
#include <span>
#include <string>
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

auto row_at(const timing_index_t& timing, signed_rational_t seconds,
            timing_inverse_options_t options = {}) -> signed_rational_t {
    const auto result = musical_position_at(timing, seconds, options);
    REQUIRE(result);
    return *result.absolute_row;
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

TEST_CASE("inverse timing maps delays and stops to their row and preserves fractional rows",
          "[unit][core][timing][inverse]") {
    auto chart = base_chart();
    chart.pauses = {
        {.id = object_id_t{20},
         .position = {measure_id_t{1}, 240},
         .kind = pause_kind_t::stop,
         .duration = {48, 1},
         .source_unit = pause_unit_t::bms_stop_ticks},
        {.id = object_id_t{21},
         .position = {measure_id_t{1}, 240},
         .kind = pause_kind_t::delay,
         .duration = {500, 1},
         .source_unit = pause_unit_t::milliseconds},
    };
    const auto built = build_timing_index(chart);
    REQUIRE(built);
    const auto& timing = *built.index;

    CHECK(row_at(timing, {5, 4}) == signed_rational_t{120, 1});
    CHECK(row_at(timing, {3, 2}) == signed_rational_t{240, 1});
    CHECK(row_at(timing, {7, 4}) == signed_rational_t{240, 1});
    CHECK(row_at(timing, {2, 1}) == signed_rational_t{240, 1});
    CHECK(row_at(timing, {9, 4}) == signed_rational_t{240, 1});
    CHECK(row_at(timing, {5, 2}) == signed_rational_t{240, 1});
    CHECK(row_at(timing, {11, 4}) == signed_rational_t{360, 1});

    // At 120 BPM one row is 1/480 second, so this exact audio time is row 247/2.
    CHECK(row_at(timing, {1'207, 960}) == signed_rational_t{247, 2});
    CHECK(row_at(timing, seconds_at(timing, 240, timing_boundary_side_t::before_delays)) ==
          signed_rational_t{240, 1});
    CHECK(row_at(timing, seconds_at(timing, 240)) == signed_rational_t{240, 1});
    CHECK(row_at(timing, seconds_at(timing, 240, timing_boundary_side_t::after_stops)) ==
          signed_rational_t{240, 1});
}

TEST_CASE("inverse timing exposes the selected side of a half-open warp plateau",
          "[unit][core][timing][inverse][warp]") {
    auto chart = base_chart();
    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 480}, {measure_id_t{2}, 0}}});
    const auto built = build_timing_index(chart);
    REQUIRE(built);
    const auto& timing = *built.index;

    const auto entry = musical_position_at(timing, {2, 1});
    REQUIRE(entry);
    CHECK(entry.absolute_row == signed_rational_t{480, 1});
    CHECK(entry.inside_warp);
    const auto exit =
        musical_position_at(timing, {2, 1}, {.warp_side = timing_warp_inverse_side_t::exit});
    REQUIRE(exit);
    CHECK(exit.absolute_row == signed_rational_t{960, 1});
    CHECK_FALSE(exit.inside_warp);
    CHECK(row_at(timing, {9, 4}) == signed_rational_t{1'020, 1});

    const auto begin_round_trip = timing_at(timing, 480);
    const auto end_round_trip = timing_at(timing, 960);
    REQUIRE(begin_round_trip);
    REQUIRE(end_round_trip);
    CHECK(*begin_round_trip.seconds == *end_round_trip.seconds);
    CHECK(row_at(timing, *begin_round_trip.seconds,
                 {.warp_side = timing_warp_inverse_side_t::entry}) == signed_rational_t{480, 1});
    CHECK(row_at(timing, *end_round_trip.seconds,
                 {.warp_side = timing_warp_inverse_side_t::exit}) == signed_rational_t{960, 1});
}

TEST_CASE("inverse timing honors signed beat-zero offsets and reports bounded failures",
          "[unit][core][timing][inverse][bounds]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {-1, 1};
    const auto built = build_timing_index(chart);
    REQUIRE(built);
    const auto& timing = *built.index;

    CHECK(row_at(timing, {-1, 1}) == signed_rational_t{0, 1});
    CHECK(row_at(timing, {-3, 4}) == signed_rational_t{120, 1});
    const auto too_early = musical_position_at(timing, {-3, 2});
    REQUIRE_FALSE(too_early);
    CHECK(too_early.error->code == timing_error_code_t::position_out_of_range);
    const auto too_late = musical_position_at(timing, {10, 1});
    REQUIRE_FALSE(too_late);
    CHECK(too_late.error->code == timing_error_code_t::position_out_of_range);
    const auto invalid_rational = musical_position_at(timing, {0, 0});
    REQUIRE_FALSE(invalid_rational);
    CHECK(invalid_rational.error->code == timing_error_code_t::invalid_document);

    chart.beat_zero_audio_offset_seconds = {};
    const auto zero_offset = build_timing_index(chart);
    REQUIRE(zero_offset);
    const auto unrepresentable =
        musical_position_at(*zero_offset.index, {std::numeric_limits<std::int64_t>::max(),
                                                 std::numeric_limits<std::uint64_t>::max() - 2});
    REQUIRE_FALSE(unrepresentable);
    CHECK(unrepresentable.error->code == timing_error_code_t::arithmetic_overflow);
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

TEST_CASE("metronome count-in extrapolates exact signed times from row zero",
          "[unit][core][timing][preview][count-in]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    chart.pauses.push_back({.id = object_id_t{20},
                            .position = {measure_id_t{1}, 0},
                            .kind = pause_kind_t::delay,
                            .duration = {500, 1},
                            .source_unit = pause_unit_t::milliseconds});
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto built =
        build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000, .count_in_pulses = 4});
    REQUIRE(built);
    const auto& clicks = built.schedule->metronome;
    REQUIRE(clicks.size() == 11);
    CHECK(clicks[0].sample_frame == 24'000);
    CHECK(clicks[0].pulse == -4);
    CHECK(clicks[0].accent == metronome_accent_t::measure);
    CHECK(clicks[1].sample_frame == 48'000);
    CHECK(clicks[1].pulse == -3);
    CHECK(clicks[1].accent == metronome_accent_t::regular);
    CHECK(clicks[3].sample_frame == 96'000);
    CHECK(clicks[3].pulse == -1);
    CHECK(clicks[4].sample_frame == 120'000);
    CHECK(clicks[4].pulse == 0);
    CHECK(clicks[4].accent == metronome_accent_t::measure);
}

TEST_CASE("count-in accents retain compound meter grouping across negative pulses",
          "[unit][core][timing][preview][count-in][meter]") {
    auto chart = base_chart();
    chart.measures[0].definition = {{3, 4}, meter_t{6, 8}};
    chart.beat_zero_audio_offset_seconds = {3, 1};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto built =
        build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000, .count_in_pulses = 6});
    REQUIRE(built);
    const auto& clicks = built.schedule->metronome;
    REQUIRE(clicks.size() >= 7);
    CHECK(clicks[0].sample_frame == 72'000);
    CHECK(clicks[0].pulse == -6);
    CHECK(clicks[0].accent == metronome_accent_t::measure);
    CHECK(clicks[3].pulse == -3);
    CHECK(clicks[3].accent == metronome_accent_t::group);
    CHECK(clicks[6].pulse == 0);
    CHECK(clicks[6].accent == metronome_accent_t::measure);
}

TEST_CASE("count-in follows row-zero event time while chart clicks honor pauses and warps",
          "[unit][core][timing][preview][count-in][discontinuity]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    chart.pauses = {
        {.id = object_id_t{20},
         .position = {measure_id_t{1}, 0},
         .kind = pause_kind_t::delay,
         .duration = {500, 1},
         .source_unit = pause_unit_t::milliseconds},
        {.id = object_id_t{21},
         .position = {measure_id_t{1}, 240},
         .kind = pause_kind_t::delay,
         .duration = {250, 1},
         .source_unit = pause_unit_t::milliseconds},
        {.id = object_id_t{22},
         .position = {measure_id_t{1}, 240},
         .kind = pause_kind_t::stop,
         .duration = {1, 1},
         .source_unit = pause_unit_t::seconds},
    };
    chart.warps.push_back(
        {.id = object_id_t{23}, .span = {{measure_id_t{1}, 480}, {measure_id_t{1}, 720}}});
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto built =
        build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000, .count_in_pulses = 4});
    REQUIRE(built);
    const auto& clicks = built.schedule->metronome;
    const auto row_zero = std::ranges::find_if(clicks, [](const auto& click) {
        return click.measure == measure_id_t{1} && click.pulse == 0;
    });
    const auto after_delay = std::ranges::find_if(clicks, [](const auto& click) {
        return click.measure == measure_id_t{1} && click.pulse == 1;
    });
    const auto warp_exit = std::ranges::find_if(clicks, [](const auto& click) {
        return click.measure == measure_id_t{1} && click.pulse == 3;
    });
    REQUIRE(row_zero != clicks.end());
    REQUIRE(after_delay != clicks.end());
    REQUIRE(warp_exit != clicks.end());
    CHECK(row_zero->sample_frame == 120'000);    // Offset 2s plus the row-zero delay.
    CHECK(after_delay->sample_frame == 156'000); // Event side includes delay, before stop.
    CHECK(warp_exit->sample_frame == 228'000);   // Stop elapses; the warp interior stays silent.
    CHECK(std::ranges::none_of(clicks, [](const auto& click) {
        return click.measure == measure_id_t{1} && click.pulse == 2;
    }));
}

TEST_CASE("negative count-in times are diagnosed without wrapping into large frames",
          "[unit][core][timing][preview][count-in][offset]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {-1, 1};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto built = build_preview_schedule(chart, *timing.index,
                                              {.sample_rate = 48'000, .count_in_pulses = 128});
    REQUIRE(built);
    CHECK(std::ranges::none_of(built.schedule->metronome,
                               [](const auto& click) { return click.pulse < 0; }));
    CHECK(std::ranges::all_of(built.schedule->metronome,
                              [](const auto& click) { return click.sample_frame < 1'000'000; }));
    CHECK(std::ranges::any_of(built.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == timing_error_code_t::negative_sample_frame &&
               diagnostic.severity == diagnostic_severity_t::warning;
    }));
    CHECK(std::ranges::count_if(built.diagnostics, [](const auto& diagnostic) {
              return diagnostic.message.find("count-in pulses") != std::string::npos;
          }) == 1);
}

TEST_CASE("count-in construction obeys the metronome queue cap atomically",
          "[unit][core][timing][preview][count-in][bounds]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto built = build_preview_schedule(
        chart, *timing.index,
        {.sample_rate = 48'000, .maximum_metronome_events = 10, .count_in_pulses = 4});
    REQUIRE_FALSE(built);
    CHECK_FALSE(built.schedule.has_value());
    CHECK(built.diagnostics.back().code == timing_error_code_t::capacity_exceeded);
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

TEST_CASE("scroll and speed evaluation is exact, composable, and seekable",
          "[unit][core][timing][scroll-speed]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.scrolls = {
        {.id = object_id_t{30},
         .position = {measure_id_t{1}, 240},
         .multiplier = {-2, 1},
         .provenance = {.source_order = 20}},
        {.id = object_id_t{31},
         .position = {measure_id_t{1}, 240},
         .multiplier = {0, 1},
         .provenance = {.source_order = 10}},
    };
    chart.speeds = {
        {.id = object_id_t{32},
         .position = {measure_id_t{1}, 0},
         .multiplier = {3, 1},
         .transition_beats = {2, 1}},
        {.id = object_id_t{33},
         .position = {measure_id_t{1}, 480},
         .multiplier = {0, 1},
         .transition_beats = {2, 1}},
    };

    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto built = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(built);
    CHECK(scroll_speed_index_matches(*built.index, chart, *timing.index));

    const auto midpoint = scroll_speed_at(*built.index, *timing.index, {120, 1});
    REQUIRE(midpoint);
    CHECK(midpoint.state->speed_multiplier == signed_rational_t{3, 2});
    CHECK(midpoint.state->displacement_beats == signed_rational_t{5, 8});

    const auto row_240_before = scroll_speed_at(*built.index, *timing.index, {240, 1},
                                                timing_boundary_side_t::before_delays);
    const auto row_240 = scroll_speed_at(*built.index, *timing.index, {240, 1});
    REQUIRE(row_240_before);
    REQUIRE(row_240);
    CHECK(row_240_before.state->scroll_multiplier == signed_rational_t{1, 1});
    CHECK(row_240.state->scroll_multiplier == signed_rational_t{-2, 1});
    CHECK(row_240.state->speed_multiplier == signed_rational_t{2, 1});
    CHECK(row_240.state->effective_multiplier == signed_rational_t{-4, 1});
    CHECK(row_240.state->displacement_beats == signed_rational_t{3, 2});

    const auto row_720 = scroll_speed_at(*built.index, *timing.index, {720, 1});
    REQUIRE(row_720);
    CHECK(row_720.state->speed_multiplier == signed_rational_t{3, 2});
    CHECK(row_720.state->effective_multiplier == signed_rational_t{-3, 1});

    // Seeking backwards and jumping past several compiled starts uses the
    // same binary-searchable evaluator and cumulative displacement prefix.
    const auto late = scroll_speed_at(*built.index, *timing.index, {960, 1});
    const auto early_again = scroll_speed_at(*built.index, *timing.index, {120, 1});
    REQUIRE(late);
    REQUIRE(early_again);
    CHECK(late.state->speed_multiplier == signed_rational_t{0, 1});
    CHECK(late.state->displacement_beats == built.index->total_displacement_beats);
    CHECK(early_again.state->displacement_beats == midpoint.state->displacement_beats);
}

TEST_CASE("same-row speed source order retargets from the value reached at that instant",
          "[unit][core][timing][scroll-speed][ordering]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.speeds = {
        {.id = object_id_t{40},
         .position = {measure_id_t{1}, 0},
         .multiplier = {3, 1},
         .transition_beats = {2, 1}},
        // The second source statement is stored first on purpose.
        {.id = object_id_t{42},
         .position = {measure_id_t{1}, 240},
         .multiplier = {5, 1},
         .transition_beats = {1, 1},
         .provenance = {.source_order = 2}},
        {.id = object_id_t{41},
         .position = {measure_id_t{1}, 240},
         .multiplier = {4, 1},
         .transition_beats = {1, 1},
         .provenance = {.source_order = 1}},
    };
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto built = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(built);

    const auto at_retarget = scroll_speed_at(*built.index, *timing.index, {240, 1});
    const auto one_beat_later = scroll_speed_at(*built.index, *timing.index, {480, 1});
    REQUIRE(at_retarget);
    REQUIRE(one_beat_later);
    CHECK(at_retarget.state->speed_multiplier == signed_rational_t{2, 1});
    CHECK(one_beat_later.state->speed_multiplier == signed_rational_t{5, 1});
}

TEST_CASE("beat and second speed transitions use distinct stop and warp clocks",
          "[unit][core][timing][scroll-speed][discontinuity]") {
    auto stopped = base_chart();
    stopped.beat_zero_audio_offset_seconds = {};
    stopped.pauses.push_back({.id = object_id_t{50},
                              .position = {measure_id_t{1}, 240},
                              .kind = pause_kind_t::stop,
                              .duration = {2, 1},
                              .source_unit = pause_unit_t::seconds});
    stopped.speeds.push_back({.id = object_id_t{51},
                              .position = {measure_id_t{1}, 0},
                              .multiplier = {3, 1},
                              .transition_beats = {1, 1},
                              .transition_is_seconds = true});
    auto stopped_timing = build_timing_index(stopped);
    REQUIRE(stopped_timing);
    const auto stopped_speed = build_scroll_speed_index(stopped, *stopped_timing.index);
    REQUIRE(stopped_speed);
    const auto arrival = scroll_speed_at(*stopped_speed.index, *stopped_timing.index, {240, 1},
                                         timing_boundary_side_t::before_delays);
    const auto departure = scroll_speed_at(*stopped_speed.index, *stopped_timing.index, {240, 1},
                                           timing_boundary_side_t::after_stops);
    REQUIRE(arrival);
    REQUIRE(departure);
    CHECK(arrival.state->speed_multiplier == signed_rational_t{2, 1});
    CHECK(departure.state->speed_multiplier == signed_rational_t{3, 1});
    CHECK(arrival.state->displacement_beats == departure.state->displacement_beats);

    auto beat_stopped = base_chart();
    beat_stopped.beat_zero_audio_offset_seconds = {};
    beat_stopped.pauses = stopped.pauses;
    beat_stopped.speeds.push_back({.id = object_id_t{52},
                                   .position = {measure_id_t{1}, 0},
                                   .multiplier = {3, 1},
                                   .transition_beats = {2, 1}});
    const auto beat_timing = build_timing_index(beat_stopped);
    REQUIRE(beat_timing);
    const auto beat_speed = build_scroll_speed_index(beat_stopped, *beat_timing.index);
    REQUIRE(beat_speed);
    const auto beat_arrival = scroll_speed_at(*beat_speed.index, *beat_timing.index, {240, 1},
                                              timing_boundary_side_t::before_delays);
    const auto beat_departure = scroll_speed_at(*beat_speed.index, *beat_timing.index, {240, 1},
                                                timing_boundary_side_t::after_stops);
    REQUIRE(beat_arrival);
    REQUIRE(beat_departure);
    CHECK(beat_arrival.state->speed_multiplier == signed_rational_t{2, 1});
    CHECK(beat_departure.state->speed_multiplier == signed_rational_t{2, 1});

    auto warped = base_chart();
    warped.beat_zero_audio_offset_seconds = {};
    warped.warps.push_back(
        {.id = object_id_t{60}, .span = {{measure_id_t{1}, 240}, {measure_id_t{1}, 720}}});
    warped.speeds.push_back({.id = object_id_t{61},
                             .position = {measure_id_t{1}, 0},
                             .multiplier = {3, 1},
                             .transition_beats = {2, 1}});
    const auto warped_timing = build_timing_index(warped);
    REQUIRE(warped_timing);
    const auto warped_speed = build_scroll_speed_index(warped, *warped_timing.index);
    REQUIRE(warped_speed);
    const auto warp_exit = scroll_speed_at(*warped_speed.index, *warped_timing.index, {720, 1});
    REQUIRE(warp_exit);
    CHECK(warp_exit.state->speed_multiplier == signed_rational_t{3, 1});
    CHECK(warp_exit.state->displacement_beats == signed_rational_t{3, 2});

    auto seconds_warp = base_chart();
    seconds_warp.beat_zero_audio_offset_seconds = {};
    seconds_warp.warps = warped.warps;
    seconds_warp.speeds.push_back({.id = object_id_t{63},
                                   .position = {measure_id_t{1}, 0},
                                   .multiplier = {3, 1},
                                   .transition_beats = {2, 1},
                                   .transition_is_seconds = true});
    const auto seconds_warp_timing = build_timing_index(seconds_warp);
    REQUIRE(seconds_warp_timing);
    const auto seconds_warp_speed =
        build_scroll_speed_index(seconds_warp, *seconds_warp_timing.index);
    REQUIRE(seconds_warp_speed);
    const auto seconds_warp_exit =
        scroll_speed_at(*seconds_warp_speed.index, *seconds_warp_timing.index, {720, 1});
    REQUIRE(seconds_warp_exit);
    CHECK(seconds_warp_exit.state->speed_multiplier == signed_rational_t{3, 2});

    auto skipped_event = warped;
    skipped_event.scrolls.push_back(
        {.id = object_id_t{62}, .position = {measure_id_t{1}, 240}, .multiplier = {-4, 1}});
    const auto skipped_index = build_scroll_speed_index(skipped_event, *warped_timing.index);
    REQUIRE(skipped_index);
    REQUIRE(skipped_index.diagnostics.size() == 1);
    CHECK(skipped_index.diagnostics[0].severity == diagnostic_severity_t::warning);
    const auto after_warp = scroll_speed_at(*skipped_index.index, *warped_timing.index, {720, 1});
    REQUIRE(after_warp);
    CHECK(after_warp.state->scroll_multiplier == signed_rational_t{1, 1});
}

TEST_CASE("scroll and speed compilation rejects stale malformed and over-cap inputs",
          "[unit][core][timing][scroll-speed][bounds]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.scrolls.push_back(
        {.id = object_id_t{70}, .position = {measure_id_t{1}, 0}, .multiplier = {-1, 0}});
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto malformed_scroll = build_scroll_speed_index(chart, *timing.index);
    REQUIRE_FALSE(malformed_scroll);
    CHECK(malformed_scroll.diagnostics.back().code == timing_error_code_t::invalid_scroll);

    chart.scrolls[0].multiplier = {-1, 1};
    const auto cap = build_scroll_speed_index(chart, *timing.index,
                                              {.maximum_events = 10, .maximum_segments = 1});
    REQUIRE_FALSE(cap);
    CHECK(cap.diagnostics.back().code == timing_error_code_t::capacity_exceeded);
    const auto boundary_cap = build_scroll_speed_index(
        chart, *timing.index,
        {.maximum_events = 10, .maximum_timing_boundaries = 1, .maximum_segments = 10});
    REQUIRE_FALSE(boundary_cap);
    CHECK(boundary_cap.diagnostics.back().code == timing_error_code_t::capacity_exceeded);

    const auto built = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(built);
    auto changed = chart;
    changed.scrolls[0].multiplier = {2, 1};
    CHECK_FALSE(scroll_speed_index_matches(*built.index, changed, *timing.index));
    const auto negative_row = scroll_speed_at(*built.index, *timing.index, {-1, 1});
    const auto invalid_row = scroll_speed_at(*built.index, *timing.index, {0, 0});
    REQUIRE_FALSE(negative_row);
    REQUIRE_FALSE(invalid_row);

    auto invalid_speed_chart = base_chart();
    invalid_speed_chart.speeds.push_back(
        {.id = object_id_t{71}, .position = {measure_id_t{1}, 0}, .multiplier = {-1, 1}});
    const auto invalid_speed_timing = build_timing_index(invalid_speed_chart);
    REQUIRE(invalid_speed_timing);
    const auto invalid_speed =
        build_scroll_speed_index(invalid_speed_chart, *invalid_speed_timing.index);
    REQUIRE_FALSE(invalid_speed);
    CHECK(invalid_speed.diagnostics.back().code == timing_error_code_t::invalid_speed);

    auto overflow_chart = base_chart();
    overflow_chart.scrolls.push_back({.id = object_id_t{72},
                                      .position = {measure_id_t{2}, 719},
                                      .multiplier = {std::numeric_limits<std::int64_t>::max(), 1}});
    overflow_chart.speeds.push_back({.id = object_id_t{73},
                                     .position = {measure_id_t{2}, 719},
                                     .multiplier = {std::numeric_limits<std::int64_t>::max(), 1}});
    const auto overflow_timing = build_timing_index(overflow_chart);
    REQUIRE(overflow_timing);
    const auto overflow_index = build_scroll_speed_index(overflow_chart, *overflow_timing.index);
    REQUIRE_FALSE(overflow_index);
    CHECK(overflow_index.diagnostics.back().code == timing_error_code_t::arithmetic_overflow);
}
