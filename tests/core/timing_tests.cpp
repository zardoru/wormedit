#include <wormedit/core/rational.h>
#include <wormedit/core/timing.h>

#include "../../src/core/timing_arithmetic.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <span>
#include <source_location>
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

// Structural timing data from the original x-Aria charts, with no audio/note
// assets required. Keep the irregular meters and dense hex-BPM ramp intact.
auto aria_timing_chart() -> chart_document_t {
    auto chart = base_chart();
    chart.divisions_per_standard_measure = 3840;
    chart.beat_zero_audio_offset_seconds = {0, 1};
    chart.measures.clear();
    for (std::uint64_t index = 0; index < 85; ++index) {
        rational_t length{1, 1};
        if (index >= 49 && index <= 56) length = {1, 2};
        if (index == 57) length = {5, 4};
        chart.measures.push_back({.id = measure_id_t{index + 1},
                                  .definition = {length, std::nullopt}});
    }
    chart.bpms = {
        {.id = object_id_t{100}, .position = {measure_id_t{1}, 0},
         .beats_per_minute = {130, 1}},
        {.id = object_id_t{101}, .position = {measure_id_t{50}, 0},
         .beats_per_minute = {65, 1}}};
    constexpr std::array<int, 14> ramp{66, 71, 76, 81, 86, 91, 96, 101, 106, 111,
                                       116, 121, 126, 130};
    for (std::size_t index = 0; index < ramp.size(); ++index) {
        chart.bpms.push_back({.id = object_id_t{102 + index},
                              .position = {measure_id_t{59}, index * 40},
                              .beats_per_minute = {ramp[index], 1}});
    }
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

auto bounded_segment_round_trip(const timing_index_t& timing, const timing_segment_t& segment)
    -> bool {
    const auto forward = timing_at(timing, exact_chart_row_t{segment.begin_row, {1, 4}});
    if (!forward) return false;
    const auto inverse = musical_position_at(timing, *forward.seconds);
    if (!inverse) return false;
    const auto row = static_cast<long double>(inverse.absolute_row->numerator) /
                     inverse.absolute_row->denominator;
    return std::abs(row - (static_cast<long double>(segment.begin_row) + 0.25L)) < 1e-6L;
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

TEST_CASE("fractional grid rows retain exact timing across tempo and warp boundaries",
          "[unit][core][timing][grid]") {
    auto chart = base_chart();
    const auto built = build_timing_index(chart);
    REQUIRE(built);
    const auto& timing = *built.index;
    const auto before_change = timing_at(timing, exact_chart_row_t{959, {1, 2}});
    const auto after_change = timing_at(timing, exact_chart_row_t{960, {1, 2}});
    REQUIRE(before_change);
    REQUIRE(after_change);
    CHECK(*before_change.seconds == signed_rational_t{2'879, 960});
    CHECK(*after_change.seconds == signed_rational_t{1'441, 480});
    CHECK_FALSE(timing_at(timing, exact_chart_row_t{960, {1, 0}}));
    CHECK_FALSE(timing_at(timing, exact_chart_row_t{960, {2, 2}}));

    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 480}, {measure_id_t{2}, 0}}});
    const auto warped = build_timing_index(chart);
    REQUIRE(warped);
    const auto inside = timing_at(*warped.index, exact_chart_row_t{720, {1, 2}});
    REQUIRE(inside);
    CHECK(inside.inside_warp);
    CHECK(*inside.seconds == signed_rational_t{2, 1});
}

TEST_CASE("chart waveform pointer resolves fractional rows to source frames",
          "[unit][core][timing][waveform-pointer]") {
    const auto built = build_timing_index(base_chart());
    REQUIRE(built);
    const auto& timing = *built.index;
    CHECK(sample_frame_at_chart_y(timing, 0.0, 240.0, 60.0, 300.0, 48'000) == 72'000);
    CHECK(sample_frame_at_chart_y(timing, 0.0, 240.0, 90.0, 300.0, 48'000) == 84'000);
    CHECK(sample_frame_at_chart_y(timing, 240.0, 240.0, 60.0, 300.0, 48'000) == 192'000);
    CHECK(sample_frame_at_chart_y(timing, -60.0, 240.0, 120.0, 300.0, 48'000) == 72'000);
    CHECK_FALSE(sample_frame_at_chart_y(timing, -60.0, 240.0, 30.0, 300.0, 48'000));
    CHECK_FALSE(sample_frame_at_chart_y(timing, 0.0, 240.0, -1.0, 300.0, 48'000));
    CHECK_FALSE(sample_frame_at_chart_y(timing, 0.0, 240.0, 60.0, 300.0, 0));
}

TEST_CASE("timing preview projects rows onto the fixed source waveform",
          "[unit][core][timing][preview-projection]") {
    auto chart = base_chart();
    const auto original = build_timing_index(chart);
    REQUIRE(original);
    chart.bpms.front().beats_per_minute = {60, 1};
    const auto proposed = build_timing_index(chart);
    REQUIRE(proposed);
    CHECK(proposed_timing_y_on_fixed_waveform(*original.index, *proposed.index, {480, {}}, 0.0,
                                              240.0) == 240.0);
    CHECK(proposed_timing_y_on_fixed_waveform(*original.index, *proposed.index, {240, {}}, 30.0,
                                              240.0) == 90.0);

    chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    const auto offset = build_timing_index(chart);
    REQUIRE(offset);
    CHECK(proposed_timing_y_on_fixed_waveform(*original.index, *offset.index, {240, {}}, 0.0,
                                              240.0) == 180.0);

    chart = base_chart();
    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 480}, {measure_id_t{2}, 0}}});
    const auto warped = build_timing_index(chart);
    REQUIRE(warped);
    CHECK(proposed_timing_y_on_fixed_waveform(*warped.index, *warped.index, {720, {1, 2}}, 0.0,
                                              240.0) == 180.125);
    CHECK_FALSE(
        proposed_timing_y_on_fixed_waveform(*original.index, *proposed.index, {480, {}}, 0.0, 0.0));
}

TEST_CASE("interval beat fitting solves exact source frames across pauses and warps",
          "[unit][core][timing][beat-fit]") {
    auto chart = base_chart();
    const auto beat = local_position_t{measure_id_t{1}, 480};
    const auto fit = [&](std::uint64_t frame) {
        return fit_beat_interval(chart, object_id_t{10}, beat, frame, 48'000);
    };
    auto proposed = fit(3 * 48'000);
    REQUIRE(proposed);
    CHECK(proposed.beats_per_minute == signed_rational_t{60, 1});
    proposed = fit(112'000);
    REQUIRE(proposed);
    CHECK(proposed.beats_per_minute == signed_rational_t{90, 1});

    chart.pauses.push_back({.id = object_id_t{19},
                            .position = {measure_id_t{1}, 0},
                            .kind = pause_kind_t::delay,
                            .duration = {1, 1},
                            .source_unit = pause_unit_t::beats});
    proposed = fit(4 * 48'000);
    REQUIRE(proposed);
    CHECK(proposed.beats_per_minute == signed_rational_t{60, 1});
    chart.pauses.clear();

    chart.pauses.push_back({.id = object_id_t{20},
                            .position = {measure_id_t{1}, 240},
                            .kind = pause_kind_t::stop,
                            .duration = {1, 1},
                            .source_unit = pause_unit_t::seconds});
    proposed = fit(4 * 48'000);
    REQUIRE(proposed);
    CHECK(proposed.beats_per_minute == signed_rational_t{60, 1});

    chart.pauses.front().duration = {48, 1};
    chart.pauses.front().source_unit = pause_unit_t::bms_stop_ticks;
    proposed = fit(4 * 48'000);
    REQUIRE(proposed);
    CHECK(proposed.beats_per_minute == signed_rational_t{60, 1});

    chart.pauses.clear();
    chart.warps.push_back(
        {.id = object_id_t{21}, .span = {{measure_id_t{1}, 240}, {measure_id_t{1}, 480}}});
    proposed = fit(2 * 48'000);
    REQUIRE(proposed);
    CHECK(proposed.beats_per_minute == signed_rational_t{60, 1});
}

TEST_CASE("interval beat fitting rejects conflicts and unreachable targets",
          "[unit][core][timing][beat-fit]") {
    auto chart = base_chart();
    const auto beat = local_position_t{measure_id_t{1}, 480};
    chart.timing_anchors.push_back({.id = object_id_t{30},
                                    .position = beat,
                                    .source_sample_frame = 2 * 48'000,
                                    .locked = true});
    auto fit = fit_beat_interval(chart, object_id_t{10}, beat, 3 * 48'000, 48'000);
    REQUIRE_FALSE(fit);
    REQUIRE(fit.error);
    CHECK(fit.error->code == timing_error_code_t::locked_anchor_conflict);

    chart.timing_anchors.clear();
    fit = fit_beat_interval(chart, object_id_t{10}, beat, 48'000, 48'000);
    REQUIRE_FALSE(fit);
    REQUIRE(fit.error);
    CHECK(fit.error->code == timing_error_code_t::invalid_fit);
    fit = fit_beat_interval(chart, object_id_t{10}, {measure_id_t{2}, 240}, 4 * 48'000, 48'000);
    REQUIRE_FALSE(fit);
    REQUIRE(fit.error);
    CHECK(fit.error->code == timing_error_code_t::invalid_fit);
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
    const auto pre_zero = musical_position_at(
        timing, {-3, 2}, {.extrapolate_before_chart = true});
    REQUIRE(pre_zero);
    CHECK(pre_zero.absolute_row == signed_rational_t{-240, 1});
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

TEST_CASE("sample-frame conversion is exact with explicit rounding",
          "[unit][core][timing][sample-frame]") {
    CHECK(sample_frame_at({1, 2}, 44'100).frame == 22'050);
    CHECK(sample_frame_at({1, 2}, 1, sample_rounding_t::floor).frame == 0);
    CHECK(sample_frame_at({1, 2}, 1, sample_rounding_t::ceil).frame == 1);
    CHECK(sample_frame_at({1, 2}, 1, sample_rounding_t::nearest_ties_to_even).frame == 0);
    CHECK(sample_frame_at({3, 2}, 1, sample_rounding_t::nearest_ties_to_even).frame == 2);
}

TEST_CASE("sample-frame conversion rejects negative times",
          "[unit][core][timing][sample-frame]") {
    const auto negative = sample_frame_at({-1, 2}, 44'100);
    REQUIRE_FALSE(negative);
    CHECK(negative.error->code == timing_error_code_t::negative_sample_frame);
}

TEST_CASE("sample-frame conversion divides a wide coprime rational",
          "[unit][core][timing][sample-frame]") {
    const auto near_one = sample_frame_at(
        {9'000'000'000'000'000'001LL, 100'000'000'000'000'003ULL}, 48'000,
        sample_rounding_t::nearest_ties_to_even);
    REQUIRE(near_one);
    CHECK(near_one.frame == 4'320'000);
}

TEST_CASE("sample-frame conversion handles the osu cursor time at 44.1 kHz",
          "[unit][core][timing][sample-frame]") {
    const auto cursor_row = sample_frame_at(
        {1'046'902'753'305'360'727LL, 14'284'000'000'000'000ULL}, 44'100,
        sample_rounding_t::floor);
    REQUIRE(cursor_row);
    CHECK(cursor_row.frame == 3'232'176);
    const auto rounded_cursor_row = sample_frame_at(
        {1'046'902'753'305'360'727LL, 14'284'000'000'000'000ULL}, 44'100,
        sample_rounding_t::ceil);
    REQUIRE(rounded_cursor_row);
    CHECK(rounded_cursor_row.frame == 3'232'177);
}

TEST_CASE("sample-frame conversion supports the full sample-rate range",
          "[unit][core][timing][sample-frame]") {
    constexpr auto rate = std::numeric_limits<std::uint32_t>::max();
    const auto maximum_rate = sample_frame_at({1, 1}, rate, sample_rounding_t::floor);
    REQUIRE(maximum_rate);
    CHECK(maximum_rate.frame == rate);
    const auto wide_maximum_rate = sample_frame_at(
        {std::numeric_limits<std::int64_t>::max(),
         static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - 1},
        rate, sample_rounding_t::nearest_ties_to_even);
    REQUIRE(wide_maximum_rate);
    CHECK(wide_maximum_rate.frame == rate);
}

TEST_CASE("sample-frame conversion supports the maximum uint64 frame",
          "[unit][core][timing][sample-frame]") {
    constexpr auto maximum_frame = std::numeric_limits<std::uint64_t>::max();
    const auto exact_maximum = sample_frame_at({6'148'914'691'236'517'205LL, 1}, 3,
                                               sample_rounding_t::floor);
    REQUIRE(exact_maximum);
    CHECK(exact_maximum.frame == maximum_frame);
}

TEST_CASE("sample-frame conversion reports a rounding carry past UINT64_MAX",
          "[unit][core][timing][sample-frame]") {
    constexpr auto maximum_frame = std::numeric_limits<std::uint64_t>::max();
    const auto below_rounded_maximum = sample_frame_at(
        {8'198'552'921'648'689'607LL, 4}, 9, sample_rounding_t::floor);
    REQUIRE(below_rounded_maximum);
    CHECK(below_rounded_maximum.frame == maximum_frame);
    const auto ceil_overflow = sample_frame_at(
        {8'198'552'921'648'689'607LL, 4}, 9, sample_rounding_t::ceil);
    REQUIRE_FALSE(ceil_overflow);
    CHECK(ceil_overflow.error->code == timing_error_code_t::arithmetic_overflow);
    const auto nearest_overflow = sample_frame_at(
        {8'198'552'921'648'689'607LL, 4}, 9, sample_rounding_t::nearest_ties_to_even);
    REQUIRE_FALSE(nearest_overflow);
    CHECK(nearest_overflow.error->code == timing_error_code_t::arithmetic_overflow);
}

TEST_CASE("sample-frame conversion rejects a quotient that exceeds uint64",
          "[unit][core][timing][sample-frame]") {
    constexpr auto rate = std::numeric_limits<std::uint32_t>::max();
    const auto overflow = sample_frame_at({std::numeric_limits<std::int64_t>::max(), 1}, rate,
                                          sample_rounding_t::floor);
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error->code == timing_error_code_t::arithmetic_overflow);
    const auto invalid = sample_frame_at({1, 0}, rate);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == timing_error_code_t::invalid_document);
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

auto music_source_tracks_frames_for_signed_offsets() -> bool {
    for (const auto offset : {signed_rational_t{-2, 1}, signed_rational_t{},
                              signed_rational_t{2, 1}}) {
        auto chart = base_chart();
        chart.beat_zero_audio_offset_seconds = offset;
        const auto timing = build_timing_index(chart);
        if (!timing) return false;
        const auto beat_zero = timing_at(*timing.index, 0);
        if (!beat_zero || *beat_zero.seconds != offset) return false;
        const auto music = preview_schedule_t::music_event_t::from(resource_id_t{7}, 200'000);
        if (!music) return false;
        for (const auto frame : {0ULL, 1ULL, 88'200ULL, 150'000ULL})
            if (preview_music_source_frame_at(*music, frame) != frame) return false;
    }
    return true;
}

auto beat_zero_note_frame_matches_music_source_time() -> bool {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    chart.measures[0].notes = {{.id = object_id_t{50},
                                .position = {measure_id_t{1}, 0},
                                .lane = lane_id_t{1},
                                .sound = resource_id_t{5}}};
    const auto timing = build_timing_index(chart);
    if (!timing) return false;
    const auto beat_zero_time = timing_at(*timing.index, 0);
    if (!beat_zero_time || *beat_zero_time.seconds != signed_rational_t{2, 1}) return false;
    const auto note_schedule = build_preview_schedule(
        chart, *timing.index, {.sample_rate = 44'100, .include_metronome = false});
    if (!note_schedule || note_schedule.schedule->playable.size() != 1) return false;
    const auto music = preview_schedule_t::music_event_t::from(resource_id_t{7}, 200'000);
    if (!music) return false;
    const auto audio_time = sample_frame_at(*beat_zero_time.seconds, 44'100);
    return audio_time &&
           note_schedule.schedule->playable.front().sample_frame == *audio_time.frame &&
           preview_music_source_frame_at(*music,
                                         note_schedule.schedule->playable.front().sample_frame) ==
               *audio_time.frame;
}

TEST_CASE("preview music source follows timeline across offsets and beat zero aligns by index",
          "[unit][core][timing][preview][music]") {
    CHECK((music_source_tracks_frames_for_signed_offsets() &&
           beat_zero_note_frame_matches_music_source_time()));
}

TEST_CASE("preview schedule materializes BMSON same-pulse discontinuity order",
          "[unit][core][timing][preview][discontinuity][bmson]") {
    auto chart = base_chart();
    chart.source_family = format_family_t::bmson;
    chart.beat_zero_audio_offset_seconds = {};
    chart.bpms.push_back({.id = object_id_t{12},
                          .position = {measure_id_t{1}, 240},
                          .beats_per_minute = {60, 1},
                          .provenance = {.source_order = 90}});
    chart.pauses.push_back({.id = object_id_t{22},
                            .position = {measure_id_t{1}, 240},
                            .kind = pause_kind_t::stop,
                            .duration = {1, 4},
                            .source_unit = pause_unit_t::seconds,
                            .provenance = {.source_order = 100}});
    chart.measures.front().notes.push_back({.id = object_id_t{52},
                                            .position = {measure_id_t{1}, 240},
                                            .lane = lane_id_t{1},
                                            .sound = resource_id_t{7},
                                            .provenance = {.source_order = 20}});
    chart.bga_events.push_back({.id = object_id_t{72},
                                .position = {measure_id_t{1}, 240},
                                .resource = resource_id_t{9},
                                .provenance = {.source_order = 10}});

    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    CHECK(timing.index->discontinuity_profile == timing_discontinuity_profile_t::bmson_1_0);
    const auto built = build_preview_schedule(
        chart, *timing.index, {.sample_rate = 48'000, .include_metronome = false});
    REQUIRE(built);
    CHECK(built.schedule->discontinuity_profile == timing_discontinuity_profile_t::bmson_1_0);

    std::vector<preview_discontinuity_kind_t> at_pulse;
    for (const auto& event : built.schedule->discontinuities) {
        if (event.sample_frame == 24'000) {
            at_pulse.push_back(event.kind);
        }
    }
    REQUIRE(at_pulse.size() == 4);
    CHECK(at_pulse[0] == preview_discontinuity_kind_t::bga);
    CHECK(at_pulse[1] == preview_discontinuity_kind_t::playable);
    CHECK(at_pulse[2] == preview_discontinuity_kind_t::bpm);
    CHECK(at_pulse[3] == preview_discontinuity_kind_t::stop);

    const auto capped = build_preview_schedule(
        chart, *timing.index,
        {.sample_rate = 48'000,
         .maximum_discontinuity_events = 3,
         .include_metronome = false});
    REQUIRE_FALSE(capped);
    CHECK(capped.diagnostics.back().code == timing_error_code_t::capacity_exceeded);
}

TEST_CASE("timing index permits an explicit discontinuity-profile override",
          "[unit][core][timing][preview][discontinuity]") {
    auto chart = base_chart();
    chart.source_family = format_family_t::bmson;
    const auto timing = build_timing_index(
        chart, {.discontinuity_profile = timing_discontinuity_profile_t::canonical});
    REQUIRE(timing);
    CHECK(timing.index->discontinuity_profile == timing_discontinuity_profile_t::canonical);

    const auto invalid = build_timing_index(
        chart, {.discontinuity_profile = static_cast<timing_discontinuity_profile_t>(255)});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.diagnostics.back().code == timing_error_code_t::invalid_document);
}

TEST_CASE("visual count-in extrapolates from row-zero arrival without weakening strict queries",
          "[unit][core][timing][preview][count-in][scroll]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    chart.pauses.push_back({.id = object_id_t{20},
                            .position = {measure_id_t{1}, 0},
                            .kind = pause_kind_t::delay,
                            .duration = {1, 2},
                            .source_unit = pause_unit_t::seconds});
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto scroll = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(scroll);

    const auto strict = musical_position_at(*timing.index, {3, 2});
    REQUIRE_FALSE(strict);
    const auto preview = musical_position_at(
        *timing.index, {3, 2}, {.extrapolate_before_chart = true});
    REQUIRE(preview);
    CHECK(preview.absolute_row == signed_rational_t{-240, 1});

    const auto strict_scroll =
        scroll_speed_at(*scroll.index, *timing.index, *preview.absolute_row);
    REQUIRE_FALSE(strict_scroll);
    const auto preview_scroll = scroll_speed_at(*scroll.index, *timing.index,
                                                *preview.absolute_row,
                                                timing_boundary_side_t::event, true);
    REQUIRE(preview_scroll);
    CHECK(preview_scroll.state->displacement_beats == signed_rational_t{-1, 1});
    CHECK(preview_scroll.state->render_displacement_beats == -1.0);
}

TEST_CASE("preview schedule canonicalizes fused linked notes without merging ordinary polyphony",
          "[unit][core][timing][preview][bmson][layers]") {
    auto chart = base_chart();
    chart.measures.front().notes = {
        {.id = object_id_t{50},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .sound = resource_id_t{5},
         .sound_channel_event = object_id_t{500}},
        {.id = object_id_t{51},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .sound = resource_id_t{6},
         .sound_channel_event = object_id_t{501}},
        {.id = object_id_t{52},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .sound = resource_id_t{7}},
    };
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);

    const auto built = build_preview_schedule(chart, *timing.index,
                                              {.sample_rate = 48'000, .include_metronome = false});
    REQUIRE(built);
    REQUIRE(built.schedule->playable.size() == 2);
    CHECK(built.schedule->playable[0].object == object_id_t{50});
    CHECK(built.schedule->playable[1].object == object_id_t{52});
    CHECK(built.schedule->playable[0].sample_frame == built.schedule->playable[1].sample_frame);
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

TEST_CASE("unmetered partial measures keep quarter-note clicks without blocking preview",
          "[unit][core][timing][preview][partial-measure]") {
    auto chart = base_chart();
    chart.measures[0].definition = {{721, 960}, std::nullopt};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto built = build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000});
    REQUIRE(built);
    REQUIRE(built.diagnostics.empty());
    const auto& clicks = built.schedule->metronome;
    REQUIRE(clicks.size() == 7);
    CHECK(clicks[1].sample_frame == 72'000);
    CHECK(clicks[3].sample_frame == 120'000);
}

TEST_CASE("partial unmetered count-in retains quarter-note spacing",
          "[unit][core][timing][preview][partial-measure]") {
    auto chart = base_chart();
    chart.measures[0].definition = {{721, 960}, std::nullopt};
    chart.beat_zero_audio_offset_seconds = {4, 1};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto built = build_preview_schedule(chart, *timing.index,
        {.sample_rate = 48'000, .count_in_pulses = 4});
    REQUIRE(built);
    REQUIRE(built.schedule->metronome.size() == 11);
    CHECK(built.schedule->metronome[0].sample_frame == 96'000);
    CHECK(built.schedule->metronome[3].sample_frame == 168'000);
}

TEST_CASE("one-row unmetered measures have a single metronome onset",
          "[unit][core][timing][preview][partial-measure]") {
    auto chart = base_chart();
    chart.measures[0].definition = {{1, 960}, std::nullopt};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto built = build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000});
    REQUIRE(built);
    REQUIRE(built.schedule->metronome.size() == 4);
    CHECK(built.schedule->metronome.front().sample_frame == 48'000);
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

TEST_CASE("speed transition spans preserve their clock and clip at replacements and chart end",
          "[unit][core][timing][scroll-speed][span]") {
    auto beat_chart = base_chart();
    beat_chart.beat_zero_audio_offset_seconds = {};
    beat_chart.pauses.push_back({.id = object_id_t{80},
                                 .position = {measure_id_t{1}, 240},
                                 .kind = pause_kind_t::stop,
                                 .duration = {2, 1},
                                 .source_unit = pause_unit_t::seconds});
    beat_chart.warps.push_back(
        {.id = object_id_t{81}, .span = {{measure_id_t{1}, 480}, {measure_id_t{1}, 720}}});
    beat_chart.speeds.push_back({.id = object_id_t{82},
                                 .position = {measure_id_t{1}, 0},
                                 .multiplier = {4, 1},
                                 .transition_beats = {3, 1}});
    const auto beat_timing = build_timing_index(beat_chart);
    REQUIRE(beat_timing);
    const auto beat_speed = build_scroll_speed_index(beat_chart, *beat_timing.index);
    REQUIRE(beat_speed);
    const auto beat_span =
        scroll_speed_transition_span(*beat_speed.index, *beat_timing.index, object_id_t{82});
    REQUIRE(beat_span);
    CHECK(beat_span.span->begin_row == signed_rational_t{0, 1});
    CHECK(beat_span.span->end_row == signed_rational_t{720, 1});
    CHECK(beat_span.span->begin_seconds == signed_rational_t{0, 1});
    CHECK(beat_span.span->end_seconds == signed_rational_t{3, 1});
    CHECK_FALSE(beat_span.span->transition_is_seconds);
    CHECK_FALSE(beat_span.span->clipped);

    auto seconds_chart = base_chart();
    seconds_chart.beat_zero_audio_offset_seconds = {};
    seconds_chart.pauses.push_back({.id = object_id_t{83},
                                    .position = {measure_id_t{1}, 240},
                                    .kind = pause_kind_t::stop,
                                    .duration = {2, 1},
                                    .source_unit = pause_unit_t::seconds});
    seconds_chart.speeds.push_back({.id = object_id_t{84},
                                    .position = {measure_id_t{1}, 0},
                                    .multiplier = {3, 1},
                                    .transition_beats = {2, 1},
                                    .transition_is_seconds = true});
    const auto seconds_timing = build_timing_index(seconds_chart);
    REQUIRE(seconds_timing);
    const auto seconds_speed = build_scroll_speed_index(seconds_chart, *seconds_timing.index);
    REQUIRE(seconds_speed);
    const auto seconds_span =
        scroll_speed_transition_span(*seconds_speed.index, *seconds_timing.index, object_id_t{84});
    REQUIRE(seconds_span);
    CHECK(seconds_span.span->begin_row == signed_rational_t{0, 1});
    CHECK(seconds_span.span->end_row == signed_rational_t{240, 1});
    CHECK(seconds_span.span->begin_seconds == signed_rational_t{0, 1});
    CHECK(seconds_span.span->end_seconds == signed_rational_t{2, 1});
    CHECK(seconds_span.span->transition_is_seconds);
    CHECK_FALSE(seconds_span.span->clipped);

    auto reducible_duration_chart = base_chart();
    reducible_duration_chart.beat_zero_audio_offset_seconds = {};
    reducible_duration_chart.speeds.push_back({
        .id = object_id_t{89},
        .position = {measure_id_t{1}, 0},
        .multiplier = {3, 1},
        .transition_beats = {std::numeric_limits<std::uint64_t>::max(),
                             std::numeric_limits<std::uint64_t>::max()},
        .transition_is_seconds = true,
    });
    const auto reducible_timing = build_timing_index(reducible_duration_chart);
    REQUIRE(reducible_timing);
    const auto reducible_speed =
        build_scroll_speed_index(reducible_duration_chart, *reducible_timing.index);
    REQUIRE(reducible_speed);
    const auto reducible_span = scroll_speed_transition_span(
        *reducible_speed.index, *reducible_timing.index, object_id_t{89});
    REQUIRE(reducible_span);
    CHECK(reducible_span.span->end_seconds == signed_rational_t{1, 1});
    CHECK_FALSE(reducible_span.span->clipped);

    auto replaced_chart = base_chart();
    replaced_chart.beat_zero_audio_offset_seconds = {};
    replaced_chart.pauses.push_back({.id = object_id_t{85},
                                     .position = {measure_id_t{1}, 240},
                                     .kind = pause_kind_t::delay,
                                     .duration = {500, 1},
                                     .source_unit = pause_unit_t::milliseconds});
    replaced_chart.speeds = {
        {.id = object_id_t{86},
         .position = {measure_id_t{1}, 0},
         .multiplier = {4, 1},
         .transition_beats = {10, 1},
         .transition_is_seconds = true},
        {.id = object_id_t{87},
         .position = {measure_id_t{1}, 240},
         .multiplier = {2, 1},
         .transition_beats = {0, 1}},
    };
    const auto replaced_timing = build_timing_index(replaced_chart);
    REQUIRE(replaced_timing);
    const auto replaced_speed = build_scroll_speed_index(replaced_chart, *replaced_timing.index);
    REQUIRE(replaced_speed);
    const auto clipped_span = scroll_speed_transition_span(*replaced_speed.index,
                                                           *replaced_timing.index, object_id_t{86});
    REQUIRE(clipped_span);
    CHECK(clipped_span.span->end_row == signed_rational_t{240, 1});
    CHECK(clipped_span.span->end_seconds == signed_rational_t{1, 1});
    CHECK(clipped_span.span->clipped);
    const auto zero_span = scroll_speed_transition_span(*replaced_speed.index,
                                                        *replaced_timing.index, object_id_t{87});
    REQUIRE(zero_span);
    CHECK(zero_span.span->begin_row == zero_span.span->end_row);
    CHECK(zero_span.span->begin_seconds == zero_span.span->end_seconds);
    CHECK_FALSE(zero_span.span->clipped);

    auto chart_end_chart = base_chart();
    chart_end_chart.beat_zero_audio_offset_seconds = {};
    chart_end_chart.speeds.push_back({.id = object_id_t{88},
                                      .position = {measure_id_t{2}, 480},
                                      .multiplier = {5, 1},
                                      .transition_beats = {2, 1}});
    const auto chart_end_timing = build_timing_index(chart_end_chart);
    REQUIRE(chart_end_timing);
    const auto chart_end_speed = build_scroll_speed_index(chart_end_chart, *chart_end_timing.index);
    REQUIRE(chart_end_speed);
    const auto chart_end_span = scroll_speed_transition_span(
        *chart_end_speed.index, *chart_end_timing.index, object_id_t{88});
    REQUIRE(chart_end_span);
    CHECK(chart_end_span.span->begin_row == signed_rational_t{1'440, 1});
    CHECK(chart_end_span.span->end_row == signed_rational_t{1'680, 1});
    CHECK(chart_end_span.span->clipped);
}

TEST_CASE("bulk SPEEDS transition spans preserve compiled order and report caps",
          "[unit][core][timing][scroll-speed][span]") {
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.speeds = {
        {.id = object_id_t{91},
         .position = {measure_id_t{1}, 0},
         .multiplier = {4, 1},
         .transition_beats = {10, 1},
         .transition_is_seconds = true},
        {.id = object_id_t{92},
         .position = {measure_id_t{1}, 240},
         .multiplier = {2, 1},
         .transition_beats = {1, 2}},
        {.id = object_id_t{93},
         .position = {measure_id_t{1}, 480},
         .multiplier = {3, 1},
         .transition_beats = {0, 1}},
    };
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto speed = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(speed);

    const auto all = scroll_speed_transition_spans(*speed.index, *timing.index);
    REQUIRE(all);
    REQUIRE(all.spans.size() == speed.index->speed_segments.size());
    REQUIRE(all.diagnostics.empty());
    for (std::size_t i = 0; i < all.spans.size(); ++i) {
        const auto single = scroll_speed_transition_span(
            *speed.index, *timing.index, speed.index->speed_segments[i].source_event);
        REQUIRE(single);
        CHECK(all.spans[i].source_event == speed.index->speed_segments[i].source_event);
        CHECK(all.spans[i].begin_row == single.span->begin_row);
        CHECK(all.spans[i].end_row == single.span->end_row);
        CHECK(all.spans[i].begin_seconds == single.span->begin_seconds);
        CHECK(all.spans[i].end_seconds == single.span->end_seconds);
        CHECK(all.spans[i].transition_is_seconds == single.span->transition_is_seconds);
        CHECK(all.spans[i].clipped == single.span->clipped);
    }

    const auto capped =
        scroll_speed_transition_spans(*speed.index, *timing.index, {.maximum_spans = 1});
    REQUIRE(capped);
    REQUIRE(capped.spans.size() == 1);
    CHECK(capped.truncated);
    REQUIRE(capped.diagnostics.size() == 1);
    CHECK(capped.diagnostics[0].severity == diagnostic_severity_t::warning);
    CHECK(capped.diagnostics[0].code == timing_error_code_t::capacity_exceeded);
    CHECK(capped.spans[0].source_event == speed.index->speed_segments[0].source_event);

    const auto zero_cap =
        scroll_speed_transition_spans(*speed.index, *timing.index, {.maximum_spans = 0});
    REQUIRE(zero_cap);
    CHECK(zero_cap.spans.empty());
    CHECK(zero_cap.truncated);
    REQUIRE(zero_cap.diagnostics.size() == 1);
    CHECK(zero_cap.diagnostics[0].severity == diagnostic_severity_t::warning);

    auto stale_timing = *timing.index;
    ++stale_timing.source_fingerprint;
    const auto stale = scroll_speed_transition_spans(*speed.index, stale_timing);
    REQUIRE_FALSE(stale);
    REQUIRE(stale.diagnostics.size() == 1);
    CHECK(stale.diagnostics[0].code == timing_error_code_t::invalid_document);
}

TEST_CASE("bulk SPEEDS span evaluation remains bounded for fifty thousand events",
          "[unit][core][timing][scroll-speed][span][performance]") {
    constexpr std::size_t speed_count = 50'000;
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {};
    chart.speeds.reserve(speed_count);
    for (std::size_t i = 0; i < speed_count; ++i) {
        chart.speeds.push_back({.id = object_id_t{static_cast<std::uint64_t>(1'000 + i)},
                                .position = {measure_id_t{1}, i % 960},
                                .multiplier = {2, 1}});
    }
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto speed = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(speed);
    REQUIRE(speed.index->speed_segments.size() == speed_count);

    const auto start = std::chrono::steady_clock::now();
    const auto spans =
        scroll_speed_transition_spans(*speed.index, *timing.index, {.maximum_spans = speed_count});
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(spans);
    CHECK_FALSE(spans.truncated);
    CHECK(spans.diagnostics.empty());
    REQUIRE(spans.spans.size() == speed_count);
    for (std::size_t i = 0; i < speed_count; ++i) {
        CHECK(spans.spans[i].source_event == speed.index->speed_segments[i].source_event);
    }
    CHECK(elapsed < std::chrono::milliseconds{125});
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


TEST_CASE("timing rational sums preserve representable wide cancellation",
          "[unit][core][timing][wide]") {
    using timing_arithmetic::sum;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    CHECK(sum({maximum, 2}, {maximum, 2}) == signed_rational_t{maximum, 1});
    CHECK(sum({maximum, 3}, {-maximum, 2}) == signed_rational_t{-maximum, 6});
    CHECK(sum({minimum, 1}, {minimum, 1}, true) == signed_rational_t{0, 1});
    CHECK(sum({minimum, 2}, {minimum, 2}) == signed_rational_t{minimum, 1});
    CHECK(sum({1, 10'000'000'000'000'000'000ULL}, {1, 15'000'000'000'000'000'000ULL}) ==
          signed_rational_t{1, 6'000'000'000'000'000'000ULL});
}

TEST_CASE("timing rational sums reject invalid and genuinely unrepresentable results",
          "[unit][core][timing][wide]") {
    using timing_arithmetic::sum;
    CHECK_FALSE(sum({std::numeric_limits<std::int64_t>::max(), 1}, {1, 1}));
    CHECK_FALSE(sum({std::numeric_limits<std::int64_t>::min(), 1}, {1, 1}, true));
    CHECK_FALSE(sum({1, 0}, {1, 1}));
    CHECK_FALSE(sum({6'566'130'408'425'776'333, 59'618'142'813'797'568}, {5, 232}));
}

TEST_CASE("bounded timing clock rounds ties evenly and rejects excessive times",
          "[unit][core][timing][bounded]") {
    using namespace timing_arithmetic;
    CHECK(clock_ticks({1, 2'000'000'000'000}) == 0);
    CHECK(clock_ticks({3, 2'000'000'000'000}) == 2);
    CHECK(clock_ticks({-3, 2'000'000'000'000}) == -2);
    CHECK_FALSE(clock_ticks({1, 0}));
    CHECK_FALSE(clock_ticks({std::numeric_limits<std::int64_t>::max(), 1}));
}

TEST_CASE("bounded timing tick addition preserves the full signed range",
          "[unit][core][timing][bounded]") {
    using namespace timing_arithmetic;
    CHECK(advance_ticks(std::numeric_limits<std::int64_t>::min(), negative_limit) == 0);
    CHECK(advance_ticks(-1, positive_limit + 1) == std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(advance_ticks(1, positive_limit));
}

TEST_CASE("bounded timing interpolation preserves half rows across negative time",
          "[unit][core][timing][bounded]") {
    using namespace timing_arithmetic;
    CHECK(interpolate_clock(-10, 10, {1, 2}, 1) == signed_rational_t{0, 1});
    CHECK(interpolate_clock(0, 3, {1, 2}, 1) == signed_rational_t{1, 500'000'000'000});
    CHECK(interpolate_row(0, 1, -10, 10, 0) == signed_rational_t{1, 2});
    CHECK_FALSE(interpolate_row(0, 1, 1, 1, 1));
    CHECK_FALSE(interpolate_row(0, positive_limit / row_quantum + 1, 0, 1, 0));
}


TEST_CASE("dense original Aria timing requires explicit bounded clock opt in",
          "[unit][core][timing][bounded]") {
    const auto chart = aria_timing_chart();
    CHECK_FALSE(build_timing_index(chart));
    const auto built = build_timing_index(chart, {.allow_bounded_clock = true});
    REQUIRE(built);
    CHECK(built.index->bounded_clock);
    CHECK_FALSE(built.diagnostics.empty());
    CHECK(chart.bpms.back().beats_per_minute == signed_rational_t{130, 1});
}

TEST_CASE("bounded timing opt in preserves ordinary exact clocks",
          "[unit][core][timing][bounded]") {
    const auto exact = build_timing_index(base_chart());
    const auto allowed = build_timing_index(base_chart(), {.allow_bounded_clock = true});
    REQUIRE(exact);
    REQUIRE(allowed);
    CHECK_FALSE(allowed.index->bounded_clock);
    CHECK(allowed.diagnostics.empty());
    CHECK(seconds_at(*allowed.index, 1) == seconds_at(*exact.index, 1));
}

TEST_CASE("dense Aria bounded clock retains precise chart completion samples",
          "[unit][core][timing][bounded]") {
    const auto built = build_timing_index(aria_timing_chart(), {.allow_bounded_clock = true});
    REQUIRE(built);
    const auto end = timing_at(*built.index, built.index->total_rows);
    REQUIRE(end);
    const auto frame = sample_frame_at(*end.seconds, 44100);
    REQUIRE(frame);
    CHECK(*frame.frame == 7'046'944);
}

TEST_CASE("bounded timing rejects excessive clock and row ranges",
          "[unit][core][timing][bounded]") {
    auto chart = aria_timing_chart();
    chart.beat_zero_audio_offset_seconds = {10'000'000, 1};
    CHECK_FALSE(build_timing_index(chart, {.allow_bounded_clock = true}));
    chart.beat_zero_audio_offset_seconds = {0, 1};
    chart.measures.back().definition.length = {3'000'000, 1};
    chart.measures.back().definition.meter.reset();
    CHECK_FALSE(build_timing_index(chart, {.allow_bounded_clock = true}));
}

TEST_CASE("bounded timing rejects positive intervals that collapse to zero",
          "[unit][core][timing][bounded]") {
    auto chart = aria_timing_chart();
    chart.bpms.back().beats_per_minute = {std::numeric_limits<std::int64_t>::max(), 1};
    CHECK_FALSE(build_timing_index(chart, {.allow_bounded_clock = true}));
}


TEST_CASE("bounded Aria forward and inverse queries cover every segment interior",
          "[unit][core][timing][bounded]") {
    const auto built = build_timing_index(aria_timing_chart(), {.allow_bounded_clock = true});
    REQUIRE(built);
    for (const auto& segment : built.index->segments) {
        CHECK(bounded_segment_round_trip(*built.index, segment));
    }
}

TEST_CASE("bounded Aria preview schedules playable events after the overflowing ramp",
          "[unit][core][timing][bounded][preview]") {
    auto chart = aria_timing_chart();
    chart.measures.back().notes.push_back({.id = object_id_t{200},
                                          .position = {measure_id_t{85}, 0},
                                          .lane = lane_id_t{1}, .sound = resource_id_t{7}});
    const auto built = build_timing_index(chart, {.allow_bounded_clock = true});
    REQUIRE(built);
    const auto schedule = build_preview_schedule(chart, *built.index);
    REQUIRE(schedule);
    REQUIRE(schedule.schedule->playable.size() == 1);
    CHECK(schedule.schedule->playable.front().sample_frame > 6'000'000);
    CHECK_FALSE(schedule.schedule->metronome.empty());
}


TEST_CASE("bounded timing respects explicitly lowered rounding capacity",
          "[unit][core][timing][bounded]") {
    CHECK_FALSE(build_timing_index(aria_timing_chart(),
        {.allow_bounded_clock = true, .maximum_bounded_clock_roundings = 1}));
    CHECK(build_timing_index(base_chart(),
        {.allow_bounded_clock = true, .maximum_bounded_clock_roundings = 0}));
}


TEST_CASE("bounded timing retains stop boundary ownership after the dense ramp",
          "[unit][core][timing][bounded]") {
    auto chart = aria_timing_chart();
    chart.pauses.push_back({.id = object_id_t{300}, .position = {measure_id_t{70}, 0},
                            .kind = pause_kind_t::stop, .duration = {1, 4},
                            .source_unit = pause_unit_t::seconds});
    const auto built = build_timing_index(chart, {.allow_bounded_clock = true});
    REQUIRE(built);
    CHECK(row_at(*built.index, seconds_at(*built.index, 250'560)) ==
          signed_rational_t{250'560, 1});
    CHECK(row_at(*built.index, seconds_at(*built.index, 250'560,
                                         timing_boundary_side_t::after_stops)) ==
          signed_rational_t{250'560, 1});
}

TEST_CASE("bounded timing retains warp plateau entry and exit ownership",
          "[unit][core][timing][bounded]") {
    auto chart = aria_timing_chart();
    chart.warps.push_back({.id = object_id_t{301},
                           .span = {{measure_id_t{72}, 0}, {measure_id_t{73}, 0}}});
    const auto built = build_timing_index(chart, {.allow_bounded_clock = true});
    REQUIRE(built);
    const auto plateau = seconds_at(*built.index, 258'240);
    CHECK(row_at(*built.index, plateau) == signed_rational_t{258'240, 1});
    CHECK(row_at(*built.index, plateau, {.warp_side = timing_warp_inverse_side_t::exit}) ==
          signed_rational_t{262'080, 1});
}


TEST_CASE("bounded preview clock extrapolates negative rows before a positive audio offset",
          "[unit][core][timing][bounded][preview]") {
    auto chart = aria_timing_chart();
    chart.beat_zero_audio_offset_seconds = {2, 1};
    const auto built = build_timing_index(chart, {.allow_bounded_clock = true});
    REQUIRE(built);
    const auto prechart = musical_position_at(*built.index, {0, 1},
                                               {.extrapolate_before_chart = true});
    REQUIRE(prechart);
    CHECK(prechart.absolute_row == signed_rational_t{-4'160, 1});
    CHECK_FALSE(musical_position_at(*built.index, {0, 1}));
}


TEST_CASE("bounded clock prechart inverse preserves large in-range fractional queries",
          "[unit][core][timing][bounded][preview]") {
    auto chart = aria_timing_chart();
    chart.beat_zero_audio_offset_seconds = {3'000'000, 1};
    const auto built = build_timing_index(chart, {.allow_bounded_clock = true});
    REQUIRE(built);
    const auto query = musical_position_at(*built.index, {1, 44100},
                                            {.extrapolate_before_chart = true});
    REQUIRE(query);
    const auto row = static_cast<long double>(query.absolute_row->numerator) /
                     query.absolute_row->denominator;
    CHECK(std::abs(row - (-6'240'000'000.0L + 2080.0L / 44100)) < 1e-6L);
    CHECK_FALSE(musical_position_at(*built.index, {-6'000'000, 1},
                                     {.extrapolate_before_chart = true}));
}

TEST_CASE("bounded factor ratio reduces before its rounded wide quotient",
          "[unit][core][timing][bounded][wide]") {
    using namespace timing_arithmetic;
    CHECK(rounded_factor_ratio({3'000'000'000'000'000'000ULL - 22'675'737, 130, 3840,
                                 row_quantum}, {clock_quantum, 1, 240}) ==
          6'239'999'999'952'834'467ULL);
    CHECK_FALSE(rounded_factor_ratio({1, 1, 1, 1}, {0, 1, 1}));
    CHECK_FALSE(factor_product(std::array<std::uint64_t, 3>{
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(), 2}));
}

namespace {
void check_layer_tick(bool value, std::source_location at = std::source_location::current()) {
    INFO("Layer tick check at " << at.line());
    CHECK(value);
}
void require_layer_tick(bool value, std::source_location at = std::source_location::current()) {
    INFO("Layer tick requirement at " << at.line());
    REQUIRE(value);
}
auto layer_tick_chart() -> chart_document_t {
    auto chart = base_chart();
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "Lead"},
                             {.id = layer_id_t{9}, .name = "Rhythm"}};
    constexpr std::array kinds{note_kind_t::tap, note_kind_t::hold, note_kind_t::roll,
        note_kind_t::lift, note_kind_t::mine, note_kind_t::fake, note_kind_t::invisible};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        note_t note{.id = object_id_t{100+i}, .position = {measure_id_t{1}, (i % 4)*240},
                    .lane = lane_id_t{1}, .kind = kinds[i], .semantic_layers = {layer_id_t{7}}};
        if (kinds[i] == note_kind_t::hold || kinds[i] == note_kind_t::roll)
            note.end = local_position_t{measure_id_t{2}, 0};
        chart.measures.front().notes.push_back(std::move(note));
    }
    chart.measures.back().notes = {{.id = object_id_t{200}, .position = {measure_id_t{2}, 240},
        .lane = lane_id_t{1}, .semantic_layers = {layer_id_t{7}, layer_id_t{9}}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{201}, .position = {measure_id_t{1}, 480},
        .source_lane = object_id_t{500}, .semantic_layers = {layer_id_t{9}}}};
    return chart;
}
}
TEST_CASE("Layer assist positions include soundless heads and BGM through shared exact timing",
          "[unit][timing][layer-assist]") {
    const auto chart = layer_tick_chart();
    const auto timing = build_timing_index(chart);
    require_layer_tick(static_cast<bool>(timing));
    const auto ordinary = build_preview_schedule(chart, *timing.index, {.sample_rate = 48000});
    require_layer_tick(static_cast<bool>(ordinary));
    check_layer_tick(ordinary.schedule->layer_ticks.empty());
    const auto ticks = build_preview_schedule(chart, *timing.index,
        {.sample_rate = 48000, .count_in_pulses = 4, .include_layer_ticks = true});
    require_layer_tick(static_cast<bool>(ticks));
    require_layer_tick(ticks.schedule->layer_ticks.size() == 6);
    check_layer_tick(ticks.schedule->playable.empty() && ticks.schedule->bgm.empty());
    check_layer_tick(ticks.schedule->layer_ticks.front().sample_frame == 48000);
    check_layer_tick(ticks.schedule->layer_ticks[4].sample_frame == 96000);
    check_layer_tick(ticks.schedule->layer_ticks.back().sample_frame == 192000);
    check_layer_tick(ticks.schedule->layer_ticks.back().layers.size() == 2);
}
TEST_CASE("Layer assist construction rejects its own bounded capacity without truncation",
          "[unit][timing][layer-assist]") {
    const auto chart = layer_tick_chart();
    const auto timing = build_timing_index(chart);
    require_layer_tick(static_cast<bool>(timing));
    const auto ticks = build_preview_schedule(chart, *timing.index,
        {.include_layer_ticks = true, .maximum_layer_tick_events = 2});
    check_layer_tick(!ticks);
    check_layer_tick(!ticks.schedule.has_value());
}

namespace {
void check_timing_query(bool value, std::source_location at = std::source_location::current()) {
    INFO(at.file_name() << ":" << at.line());
    CHECK(value);
}
void require_timing_query(bool value, std::source_location at = std::source_location::current()) {
    INFO(at.file_name() << ":" << at.line());
    REQUIRE(value);
}
} // namespace
TEST_CASE("Timing owner queries retain exact boundary sides and inverse options", "[unit][timing]") {
    auto chart = base_chart();
    chart.pauses = {{.id = object_id_t{90}, .position = {measure_id_t{1}, 240},
                     .kind = pause_kind_t::stop, .duration = {1, 1}}};
    const auto built = build_timing_index(chart);
    require_timing_query(static_cast<bool>(built));
    const auto& index = *built.index;
    for (const auto side : {timing_boundary_side_t::before_delays,
                            timing_boundary_side_t::event, timing_boundary_side_t::after_stops}) {
        const auto member = index.seconds_at(240, side);
        const auto original = timing_at(index, 240, side);
        require_timing_query(static_cast<bool>(member.seconds));
        check_timing_query(static_cast<bool>(member.seconds == original.seconds));
        check_timing_query(static_cast<bool>(member.inside_warp == original.inside_warp));
    }
    const auto fractional = exact_chart_row_t{120, {1, 2}};
    check_timing_query(static_cast<bool>(index.seconds_at(fractional).seconds == timing_at(index, fractional).seconds));
    const auto inverse = index.row_at({0, 1}, {.extrapolate_before_chart = true});
    check_timing_query(static_cast<bool>(inverse.absolute_row == musical_position_at(index, {0, 1},
        {.extrapolate_before_chart = true}).absolute_row));
    check_timing_query(static_cast<bool>(index.row_at({0, 1}).error->code == timing_error_code_t::position_out_of_range));
}

TEST_CASE("Timing display queries name and preserve each chart end policy", "[unit][timing]") {
    const auto built = build_timing_index(base_chart());
    require_timing_query(static_cast<bool>(built));
    const auto& index = *built.index;
    const auto end = index.seconds_at(index.total_rows, timing_boundary_side_t::after_stops);
    require_timing_query(static_cast<bool>(end.seconds));
    const auto end_time = to_double(*end.seconds);
    const auto tail = signed_rational_t{end.seconds->numerator +
                                       static_cast<std::int64_t>(end.seconds->denominator),
                                       end.seconds->denominator};
    check_timing_query(!(index.row_at_approx(tail, chart_end_policy_t::reject)));
    check_timing_query(static_cast<bool>(index.row_at_approx(tail, chart_end_policy_t::clamp_to_end) ==
          std::optional<double>{static_cast<double>(index.total_rows)}));
    check_timing_query(static_cast<bool>(index.row_at_approx(tail, chart_end_policy_t::clamp_to_last_row) ==
          std::optional<double>{static_cast<double>(index.total_rows - 1)}));
    require_timing_query(static_cast<bool>(index.tail_seconds_per_row()));
    check_timing_query(static_cast<bool>(index.tail_seconds_per_row() == std::optional<double>{1.0 / 240.0}));
    check_timing_query(static_cast<bool>(index.row_at_approx(tail, chart_end_policy_t::extrapolate_tail) ==
          std::optional<double>{static_cast<double>(index.total_rows + 240)}));
    check_timing_query(static_cast<bool>(index.row_at(tail, chart_end_policy_t::extrapolate_tail).absolute_row ==
          std::optional<signed_rational_t>{{static_cast<std::int64_t>(index.total_rows + 240), 1}}));
    check_timing_query(!(index.seconds_at_approx(index.total_rows + 240)));
    check_timing_query(static_cast<bool>(index.seconds_at_approx(index.total_rows + 240, chart_end_policy_t::extrapolate_tail) ==
          std::optional<double>{end_time + 1.0}));
    check_timing_query(static_cast<bool>(index.seconds_at_approx(index.total_rows + 240, chart_end_policy_t::clamp_to_end) ==
          std::optional<double>{end_time}));
    check_timing_query(static_cast<bool>(index.seconds_at_approx(index.total_rows, chart_end_policy_t::clamp_to_last_row) ==
          index.seconds_at_approx(index.total_rows - 1)));
    check_timing_query(static_cast<bool>(index.row_at_approx({0, 1}, chart_end_policy_t::clamp_to_end) == std::optional<double>{0}));
    check_timing_query(static_cast<bool>(index.row_at_frame(static_cast<std::uint64_t>((end_time + 1) * 48000), 48000,
          chart_end_policy_t::clamp_to_last_row) ==
          std::optional<double>{static_cast<double>(index.total_rows - 1)}));
}

TEST_CASE("Timing owner display queries reject invalid clocks and frame bounds", "[unit][timing]") {
    const auto built = build_timing_index(base_chart());
    require_timing_query(static_cast<bool>(built));
    const auto& index = *built.index;
    check_timing_query(!(index.row_at_approx({1, 0}, chart_end_policy_t::clamp_to_end)));
    check_timing_query(!(index.row_at_frame(1, 0, chart_end_policy_t::clamp_to_end)));
    check_timing_query(!(index.row_at_frame(std::numeric_limits<std::uint64_t>::max(), 48000,
                                  chart_end_policy_t::clamp_to_end)));
    const timing_index_t empty;
    check_timing_query(!(empty.tail_seconds_per_row()));
    check_timing_query(!(empty.seconds_at_approx(1, chart_end_policy_t::extrapolate_tail)));
    check_timing_query(!(empty.row_at_approx({1, 1}, chart_end_policy_t::clamp_to_end)));
}

TEST_CASE("Timing frame last-row policy preserves playback clock rejection", "[unit][timing]") {
    const auto built = build_timing_index(base_chart());
    require_timing_query(static_cast<bool>(built));
    check_timing_query(!built.index->row_at_frame(0, 48000, chart_end_policy_t::clamp_to_last_row));
    check_timing_query(built.index->row_at_frame(0, 48000, chart_end_policy_t::clamp_to_end) ==
                       std::optional<double>{0});
    auto chart = base_chart();
    chart.beat_zero_audio_offset_seconds = {-100, 1};
    const auto before_zero = build_timing_index(chart);
    require_timing_query(static_cast<bool>(before_zero));
    check_timing_query(!before_zero.index->row_at_frame(0, 48000, chart_end_policy_t::clamp_to_last_row));
}
