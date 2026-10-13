#include <wormedit/core/time_shift.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace wormedit;

namespace {

constexpr auto first_measure = measure_id_t{3};
constexpr auto second_measure = measure_id_t{4};

auto tap(std::uint64_t id, std::uint64_t lane, measure_id_t measure, std::uint64_t row) -> note_t {
    return {.id = object_id_t{id}, .position = {measure, row}, .lane = lane_id_t{lane}};
}

// Measure 3 has 960 rows, measure 4 is a half-length measure with 480 rows.
auto shift_song() -> song_document_t {
    auto hold = tap(12, 2, first_measure, 600);
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{first_measure, 720};
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures =
                {{.id = first_measure,
                  .notes = {tap(10, 1, first_measure, 120), tap(11, 1, first_measure, 300), hold}},
                 {.id = second_measure,
                  .definition = {{1, 2}, meter_t{2, 4}},
                  .notes = {tap(13, 1, second_measure, 60)}}},
            .lane_layouts = {{.id = object_id_t{20},
                              .position = {first_measure, 0},
                              .layout = {.profile_id = "two",
                                         .lanes = {{.id = lane_id_t{1}, .name = "A"},
                                                   {.id = lane_id_t{2}, .name = "B"}}}}},
            .bpms = {{.id = object_id_t{21},
                      .position = {first_measure, 0},
                      .beats_per_minute = {120, 1}},
                     {.id = object_id_t{22},
                      .position = {first_measure, 480},
                      .beats_per_minute = {150, 1}}},
            .bookmarks = {{.id = object_id_t{30},
                           .label = "beat",
                           .anchor = marker_anchor_t::beat,
                           .position = local_position_t{first_measure, 600}},
                          {.id = object_id_t{31},
                           .label = "audio",
                           .anchor = marker_anchor_t::audio_time,
                           .audio_milliseconds = 1500}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto chart_of(const document_history_t& history) -> const chart_document_t& {
    return history.document.charts.front();
}

auto find_note(const document_history_t& history, std::uint64_t id)
    -> std::optional<std::pair<measure_id_t, note_t>> {
    for (const auto& measure : chart_of(history).measures)
        for (const auto& note : measure.notes)
            if (note.id.value == id)
                return std::pair{measure.id, note};
    return std::nullopt;
}

auto has_code(const time_shift_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.command.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("shifting a selection moves notes and holds exactly and undoes",
          "[unit][core][time-shift]") {
    auto history = document_history_t::from(shift_song());
    const auto result = apply_shift_selection(
        history, 4,
        {.chart = chart_id_t{2}, .notes = {object_id_t{10}, object_id_t{12}}, .rows = 100});
    REQUIRE(result);
    CHECK(result.payload.moved_objects == 2);
    CHECK(find_note(history, 10)->second.position.row == 220);
    const auto hold = find_note(history, 12)->second;
    CHECK(hold.position.row == 700);
    CHECK(hold.end->row == 820);
    CHECK(find_note(history, 11)->second.position.row == 300);
    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, 10)->second.position.row == 120);
}

TEST_CASE("a shift across a measure-length change lands on the exact row",
          "[unit][core][time-shift]") {
    auto history = document_history_t::from(shift_song());
    // Row 300 + 400 = absolute 700 stays in measure 3; 600 + 800 = 1400 is row 440 of measure 4.
    const auto result = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{11}}, .rows = 800});
    REQUIRE(result);
    const auto moved = find_note(history, 11);
    REQUIRE(moved);
    CHECK(moved->first == second_measure);
    CHECK(moved->second.position == local_position_t{second_measure, 140});

    // Backward across the boundary: measure 4 row 60 minus 100 is measure 3 row 920.
    const auto back = apply_shift_selection(
        history, history.document.revision,
        {.chart = chart_id_t{2}, .notes = {object_id_t{13}}, .rows = -100});
    REQUIRE(back);
    CHECK(find_note(history, 13)->first == first_measure);
    CHECK(find_note(history, 13)->second.position.row == 920);
}

TEST_CASE("shifts past either end, collisions and zero shifts", "[unit][core][time-shift]") {
    auto history = document_history_t::from(shift_song());
    const auto before_start = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = -121});
    CHECK_FALSE(before_start.command.applied);
    CHECK(has_code(before_start, diagnostic_code_t::row_out_of_range));
    const auto past_end = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{13}}, .rows = 420});
    CHECK(has_code(past_end, diagnostic_code_t::row_out_of_range));
    const auto stacked = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = 180});
    CHECK_FALSE(stacked.command.applied);
    CHECK(has_code(stacked, diagnostic_code_t::collision));
    const auto zero = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = 0});
    CHECK_FALSE(zero.command.applied);
    CHECK(zero.command.diagnostics.empty());
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
    const auto stale = apply_shift_selection(
        history, 3, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = 10});
    CHECK_FALSE(stale.command.applied);
    const auto unknown = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{99}}, .rows = 10});
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
}

TEST_CASE("shifting from the cursor moves later content and optionally timing",
          "[unit][core][time-shift]") {
    auto content_only = document_history_t::from(shift_song());
    const auto cursor = local_position_t{first_measure, 300};
    const auto result = apply_shift_from_cursor(
        content_only, 4, {.chart = chart_id_t{2}, .cursor = cursor, .rows = 40});
    REQUIRE(result);
    CHECK(find_note(content_only, 10)->second.position.row == 120);
    CHECK(find_note(content_only, 11)->second.position.row == 340);
    CHECK(find_note(content_only, 12)->second.end->row == 760);
    CHECK(find_note(content_only, 13)->second.position.row == 100);
    CHECK(chart_of(content_only).bpms[1].position.row == 480);
    CHECK(chart_of(content_only).bookmarks[0].position->row == 640);
    CHECK(chart_of(content_only).bookmarks[1].audio_milliseconds == 1500);
    CHECK(result.payload.audio_anchored_skipped == 1);

    auto with_timing = document_history_t::from(shift_song());
    const auto timed = apply_shift_from_cursor(
        with_timing, 4,
        {.chart = chart_id_t{2}, .cursor = cursor, .rows = 40, .include_timing = true});
    REQUIRE(timed);
    CHECK(chart_of(with_timing).bpms[0].position.row == 0);
    CHECK(chart_of(with_timing).bpms[1].position.row == 520);
    CHECK(timed.payload.moved_objects == result.payload.moved_objects + 1);
}

TEST_CASE("time shift refuses charts that retain BMS control flow",
          "[unit][core][time-shift]") {
    auto song = shift_song();
    song.charts.front().bms_measure_length_projections.push_back({.measure_index = 0});
    auto history = document_history_t::from(std::move(song));
    const auto result = apply_shift_selection(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = 10});
    CHECK_FALSE(result.command.applied);
    CHECK(has_code(result, diagnostic_code_t::unsupported_feature));
}
