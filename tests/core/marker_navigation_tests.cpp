#include "../support/stale_action_assertions.h"
#include <wormedit/core/marker_navigation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <ranges>

using namespace wormedit;

namespace {

// Retain the legacy query acceptance rule while inspecting canonical diagnosed outcomes.
auto is_marker_navigation_accepted(const marker_navigation_outcome_t& outcome) -> bool {
    return outcome.diagnostics.empty() && outcome.value.is_found();
}

auto is_section_selection_accepted(const section_selection_outcome_t& outcome) -> bool {
    return outcome.diagnostics.empty() && outcome.value.is_resolved();
}

auto marker_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 9,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{11},
                                  .definition = {.length = {1, 2}, .meter = std::nullopt}},
                                 {.id = measure_id_t{12},
                                  .definition = {.length = {3, 2}, .meter = std::nullopt}}},
                    .bookmarks = {{.id = object_id_t{90},
                                   .label = "boundary bookmark",
                                   .anchor = marker_anchor_t::beat,
                                   .position = local_position_t{measure_id_t{12}, 0}},
                                  {.id = object_id_t{40},
                                   .label = "later beat",
                                   .anchor = marker_anchor_t::beat,
                                   .position = local_position_t{measure_id_t{12}, 300}},
                                  {.id = object_id_t{130},
                                   .label = "audio bookmark",
                                   .anchor = marker_anchor_t::audio_time,
                                   .audio_milliseconds = -100}},
                    .sections = {{.id = object_id_t{70},
                                  .label = "beat section",
                                  .anchor = marker_anchor_t::beat,
                                  .beat_span = position_span_t{{measure_id_t{12}, 0},
                                                               {measure_id_t{12}, 500}}},
                                 {.id = object_id_t{110},
                                  .label = "audio section",
                                  .anchor = marker_anchor_t::audio_time,
                                  .audio_millisecond_span = std::pair{-50, 25}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

} // namespace

TEST_CASE("marker navigation uses variable-measure geometry and stable tie ordering",
          "[unit][core][marker-navigation]") {
    const auto song = marker_song();
    const auto filter = marker_navigation_filter_t{};

    const auto next_across_measure =
        marker_navigation_match_t::from(song, 9, chart_id_t{2}, local_position_t{measure_id_t{11}, 479},
                         marker_navigation_direction_t::next, filter);
    REQUIRE(is_marker_navigation_accepted(next_across_measure));
    CHECK(next_across_measure.value.item->object == object_id_t{70});
    CHECK(next_across_measure.value.item->kind == marker_navigation_kind_t::section_begin);
    CHECK((next_across_measure.value.item->beat_position ==
           std::optional{local_position_t{measure_id_t{12}, 0}}));

    // The bookmark and section begin share the same exact position. Object ID
    // is their available stable source-order key: next takes the lower ID and
    // previous takes the higher ID.
    const auto next_tie = marker_navigation_match_t::from(
        song, 9, chart_id_t{2}, local_position_t{measure_id_t{11}, 479},
        marker_navigation_direction_t::next,
        {.include_bookmarks = true, .include_section_begins = true, .include_section_ends = false});
    REQUIRE(is_marker_navigation_accepted(next_tie));
    CHECK(next_tie.value.item->object == object_id_t{70});

    const auto previous_tie = marker_navigation_match_t::from(
        song, 9, chart_id_t{2}, local_position_t{measure_id_t{12}, 1},
        marker_navigation_direction_t::previous,
        {.include_bookmarks = true, .include_section_begins = true, .include_section_ends = false});
    REQUIRE(is_marker_navigation_accepted(previous_tie));
    CHECK(previous_tie.value.item->object == object_id_t{90});
    CHECK(previous_tie.value.item->kind == marker_navigation_kind_t::bookmark);

    const auto strict = marker_navigation_match_t::from(
        song, 9, chart_id_t{2}, local_position_t{measure_id_t{12}, 0},
        marker_navigation_direction_t::next,
        {.include_bookmarks = true, .include_section_begins = true, .include_section_ends = false});
    REQUIRE(is_marker_navigation_accepted(strict));
    CHECK(strict.value.item->object == object_id_t{40});
}

TEST_CASE("marker navigation filters marker kinds and reports explicit no-result",
          "[unit][core][marker-navigation]") {
    const auto song = marker_song();
    const auto bookmarks_only =
        marker_navigation_match_t::from(song, 9, chart_id_t{2}, local_position_t{measure_id_t{12}, 0},
                         marker_navigation_direction_t::next,
                         {.include_bookmarks = true,
                          .include_section_begins = false,
                          .include_section_ends = false});
    REQUIRE(is_marker_navigation_accepted(bookmarks_only));
    CHECK(bookmarks_only.value.item->object == object_id_t{40});

    const auto no_markers =
        marker_navigation_match_t::from(song, 9, chart_id_t{2}, local_position_t{measure_id_t{12}, 900},
                         marker_navigation_direction_t::next,
                         {.include_bookmarks = false,
                          .include_section_begins = false,
                          .include_section_ends = false});
    CHECK(no_markers.value.status == marker_navigation_phase_t::no_result);
    CHECK_FALSE(no_markers.value.item.has_value());
    CHECK(no_markers.diagnostics.empty());
    CHECK_FALSE(no_markers.value.is_found());
}

TEST_CASE("audio marker navigation compares exact signed milliseconds",
          "[unit][core][marker-navigation]") {
    const auto song = marker_song();
    const auto next = marker_navigation_match_t::from(song, 9, chart_id_t{2}, std::int64_t{-75},
                                       marker_navigation_direction_t::next);
    REQUIRE(is_marker_navigation_accepted(next));
    CHECK(next.value.item->object == object_id_t{110});
    CHECK(next.value.item->kind == marker_navigation_kind_t::section_begin);
    CHECK(next.value.item->audio_milliseconds == -50);

    const auto previous = marker_navigation_match_t::from(song, 9, chart_id_t{2}, std::int64_t{0},
                                           marker_navigation_direction_t::previous);
    REQUIRE(is_marker_navigation_accepted(previous));
    CHECK(previous.value.item->object == object_id_t{110});
    CHECK(previous.value.item->audio_milliseconds == -50);

    const auto beat_only = marker_navigation_match_t::from(song, 9, chart_id_t{2}, std::int64_t{-200},
                                            marker_navigation_direction_t::next,
                                            {.include_bookmarks = false,
                                             .include_section_begins = false,
                                             .include_section_ends = true});
    REQUIRE(is_marker_navigation_accepted(beat_only));
    CHECK(beat_only.value.item->audio_milliseconds == 25);
}

TEST_CASE("marker navigation validates chart revision and beat cursor geometry",
          "[unit][core][marker-navigation][negative]") {
    const auto song = marker_song();
    const auto stale =
        marker_navigation_match_t::from(song, 8, chart_id_t{2}, local_position_t{measure_id_t{11}, 0},
                         marker_navigation_direction_t::next);
    CHECK(stale.value.status == failure_code_t::invalid_request);
    CHECK_FALSE(stale.diagnostics.empty());

    const auto unknown =
        marker_navigation_match_t::from(song, 9, chart_id_t{99}, local_position_t{measure_id_t{11}, 0},
                         marker_navigation_direction_t::next);
    CHECK(unknown.value.status == failure_code_t::invalid_request);

    const auto unknown_measure =
        marker_navigation_match_t::from(song, 9, chart_id_t{2}, local_position_t{measure_id_t{99}, 0},
                         marker_navigation_direction_t::next);
    CHECK(unknown_measure.value.status == failure_code_t::invalid_request);
    CHECK(std::ranges::any_of(unknown_measure.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::unknown_measure;
    }));

    const auto out_of_range =
        marker_navigation_match_t::from(song, 9, chart_id_t{2}, local_position_t{measure_id_t{11}, 480},
                         marker_navigation_direction_t::next);
    CHECK(out_of_range.value.status == failure_code_t::invalid_request);
    CHECK(std::ranges::any_of(out_of_range.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::row_out_of_range;
    }));
}

TEST_CASE("marker navigation enforces marker caps and chart-wide returned IDs",
          "[unit][core][marker-navigation][bounds]") {
    const auto song = marker_song();
    const auto capped = marker_navigation_match_t::from(
        song, 9, chart_id_t{2}, local_position_t{measure_id_t{11}, 0},
        marker_navigation_direction_t::next,
        {.include_bookmarks = true, .include_section_begins = false, .include_section_ends = false},
        {.maximum_marker_objects = 4});
    CHECK(capped.value.status == failure_code_t::invalid_request);
    CHECK(std::ranges::any_of(capped.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::invalid_resolution;
    }));

    const auto zero_cap =
        marker_navigation_match_t::from(song, 9, chart_id_t{2}, std::int64_t{0},
                         marker_navigation_direction_t::next, {}, {.maximum_marker_objects = 0});
    CHECK(zero_cap.value.status == failure_code_t::invalid_request);
    CHECK(std::ranges::any_of(zero_cap.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::invalid_resolution;
    }));

    auto duplicate_bookmark_id = marker_song();
    duplicate_bookmark_id.charts.front().timing_anchors.push_back(
        {.id = object_id_t{90}, .position = {measure_id_t{11}, 0}});
    const auto ambiguous_bookmark = marker_navigation_match_t::from(duplicate_bookmark_id, 9, chart_id_t{2},
                                                     local_position_t{measure_id_t{11}, 479},
                                                     marker_navigation_direction_t::next,
                                                     {.include_bookmarks = true,
                                                      .include_section_begins = false,
                                                      .include_section_ends = false});
    CHECK(ambiguous_bookmark.value.status == failure_code_t::invalid_request);
    CHECK(std::ranges::any_of(ambiguous_bookmark.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::duplicate_id;
    }));

    auto duplicate_section_id = marker_song();
    duplicate_section_id.charts.front().timing_anchors.push_back(
        {.id = object_id_t{70}, .position = {measure_id_t{11}, 0}});
    const auto ambiguous_section =
        section_selection_t::from(duplicate_section_id, 9, chart_id_t{2}, object_id_t{70});
    CHECK(ambiguous_section.value.status == failure_code_t::invalid_request);
    CHECK(std::ranges::any_of(ambiguous_section.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::duplicate_id;
    }));
}

TEST_CASE("section selection resolves beat spans and requires projection for audio spans",
          "[unit][core][marker-navigation]") {
    const auto song = marker_song();
    const auto beat = section_selection_t::from(song, 9, chart_id_t{2}, object_id_t{70});
    REQUIRE(is_section_selection_accepted(beat));
    REQUIRE(beat.value.beat_span.has_value());
    CHECK(beat.value.beat_span->begin == local_position_t{measure_id_t{12}, 0});
    CHECK(beat.value.beat_span->end == local_position_t{measure_id_t{12}, 500});

    const auto audio = section_selection_t::from(song, 9, chart_id_t{2}, object_id_t{110});
    CHECK(audio.value.status == section_selection_phase_t::timing_projection_required);
    CHECK_FALSE(audio.value.beat_span.has_value());
    CHECK(audio.diagnostics.empty());

    const auto stale = section_selection_t::from(song, 8, chart_id_t{2}, object_id_t{70});
    test::check_stale_action_message(stale.diagnostics, "Marker navigation");
    CHECK(stale.value.status == failure_code_t::invalid_request);
    CHECK_FALSE(stale.diagnostics.empty());
}

TEST_CASE("beat marker navigation indexes large measure sets once per query",
          "[unit][core][marker-navigation][performance]") {
    constexpr std::size_t item_count = 10'000;
    song_document_t song{.id = song_id_t{1}, .revision = 3};
    chart_document_t chart{.id = chart_id_t{2}, .divisions_per_standard_measure = 960};
    chart.measures.reserve(item_count);
    chart.bookmarks.reserve(item_count);
    for (std::size_t index = 0; index < item_count; ++index) {
        const auto measure = measure_id_t{index + 1};
        chart.measures.push_back({.id = measure});
        chart.bookmarks.push_back({.id = object_id_t{index + 1},
                                   .anchor = marker_anchor_t::beat,
                                   .position = local_position_t{measure, 0}});
    }
    song.charts.push_back(std::move(chart));

    const auto started = std::chrono::steady_clock::now();
    const auto result =
        marker_navigation_match_t::from(song, 3, chart_id_t{2}, local_position_t{measure_id_t{1}, 0},
                         marker_navigation_direction_t::next,
                         {.include_bookmarks = true,
                          .include_section_begins = false,
                          .include_section_ends = false});
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(is_marker_navigation_accepted(result));
    CHECK(result.value.item->object == object_id_t{2});
    CHECK(elapsed < std::chrono::milliseconds{500});
}

namespace {

auto projected_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{2}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
                      {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}};
    chart.bpms = {{.id = object_id_t{10},
                   .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.pauses = {{.id = object_id_t{11},
                     .position = {measure_id_t{1}, 480},
                     .kind = pause_kind_t::stop,
                     .duration = {1, 1},
                     .source_unit = pause_unit_t::seconds}};
    chart.bookmarks = {{.id = object_id_t{1},
                        .anchor = marker_anchor_t::beat,
                        .position = local_position_t{measure_id_t{2}, 0}},
                       {.id = object_id_t{2},
                        .anchor = marker_anchor_t::audio_time,
                        .audio_milliseconds = 250},
                       {.id = object_id_t{3},
                        .anchor = marker_anchor_t::audio_time,
                        .audio_milliseconds = 1500},
                       {.id = object_id_t{4},
                        .anchor = marker_anchor_t::audio_time,
                        .audio_milliseconds = 600'000}};
    chart.sections = {{.id = object_id_t{5},
                       .anchor = marker_anchor_t::audio_time,
                       .audio_millisecond_span = std::pair{500, 750}}};
    return {.id = song_id_t{1}, .revision = 4, .charts = {std::move(chart)}};
}

auto next_from(const song_document_t& song, std::uint32_t row, const timing_index_t* timing)
    -> marker_navigation_outcome_t {
    return marker_navigation_match_t::from(song, 4, chart_id_t{2}, local_position_t{measure_id_t{1}, row},
                            marker_navigation_direction_t::next, {}, {}, timing);
}

} // namespace

TEST_CASE("timing projection adds audio-time markers at exact rows",
          "[unit][core][marker-navigation]") {
    const auto song = projected_song();
    const auto built = build_timing_index(song.charts.front());
    REQUIRE(built);
    const auto* timing = &*built.index;

    // 120 BPM: 250 ms is row 120, 500 ms row 240, 750 ms row 360.
    const auto first = next_from(song, 0, timing);
    REQUIRE(first.value.status == marker_navigation_phase_t::found);
    CHECK(first.value.item->object == object_id_t{2});
    CHECK(first.value.item->beat_position == local_position_t{measure_id_t{1}, 120});
    CHECK(first.value.item->audio_milliseconds == 250);

    const auto begin = next_from(song, 120, timing);
    CHECK(begin.value.item->object == object_id_t{5});
    CHECK(begin.value.item->kind == marker_navigation_kind_t::section_begin);
    CHECK(begin.value.item->beat_position == local_position_t{measure_id_t{1}, 240});
    const auto end = next_from(song, 240, timing);
    CHECK(end.value.item->kind == marker_navigation_kind_t::section_end);
    CHECK(end.value.item->beat_position == local_position_t{measure_id_t{1}, 360});

    // 1500 ms falls inside the one-second stop at row 480 and maps to that row.
    const auto stop = next_from(song, 360, timing);
    CHECK(stop.value.item->object == object_id_t{3});
    CHECK(stop.value.item->beat_position == local_position_t{measure_id_t{1}, 480});

    const auto beat = next_from(song, 480, timing);
    CHECK(beat.value.item->object == object_id_t{1});
}

TEST_CASE("timing projection skips out-of-range markers with a diagnostic",
          "[unit][core][marker-navigation][negative]") {
    const auto song = projected_song();
    const auto built = build_timing_index(song.charts.front());
    REQUIRE(built);

    const auto result = next_from(song, 480, &*built.index);
    REQUIRE(result.value.status == marker_navigation_phase_t::found);
    CHECK(result.value.item->object == object_id_t{1});
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().severity == diagnostic_severity_t::warning);
    CHECK(result.diagnostics.front().object == object_id_t{4});
}

TEST_CASE("beat navigation without a timing index ignores audio-time markers",
          "[unit][core][marker-navigation]") {
    const auto song = projected_song();
    const auto result = next_from(song, 0, nullptr);
    REQUIRE(is_marker_navigation_accepted(result));
    CHECK(result.value.item->object == object_id_t{1});

    const auto built = build_timing_index(song.charts.front());
    REQUIRE(built);
    const auto previous = marker_navigation_match_t::from(
        song, 4, chart_id_t{2}, local_position_t{measure_id_t{2}, 0},
        marker_navigation_direction_t::previous, {}, {}, &*built.index);
    REQUIRE(previous.value.status == marker_navigation_phase_t::found);
    CHECK(previous.value.item->object == object_id_t{3});
}

TEST_CASE("marker navigation targets include any bookmark or section",
          "[core][marker_navigation]") {
    chart_document_t chart{.id = chart_id_t{2}};
    CHECK_FALSE(has_marker_navigation_targets(chart));
    chart.bookmarks.push_back({.id = object_id_t{7}, .label = "audio",
                               .anchor = marker_anchor_t::audio_time,
                               .audio_milliseconds = 1000});
    CHECK(has_marker_navigation_targets(chart));
}

TEST_CASE("an unusable timing index falls back to beat markers with one warning",
          "[unit][core][marker-navigation][negative]") {
    const auto song = projected_song();
    const timing_index_t unusable{};
    const auto result = next_from(song, 0, &unusable);
    REQUIRE(result.value.status == marker_navigation_phase_t::found);
    CHECK(result.value.item->object == object_id_t{1});
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().severity == diagnostic_severity_t::warning);
}
