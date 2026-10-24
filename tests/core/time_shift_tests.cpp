#include <wormedit/core/time_shift.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/rational.h>
#include <wormedit/formats/bms.h>
#include "sys/project/output_group_export.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <vector>
#include <span>
#include <string>
#include <string_view>

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
    return *history.document.read_charts().front();
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

constexpr std::string_view plain_bms =
    "#PLAYER 1\n#TITLE Plain source\n#BPM 120\n#BPM01 150\n#STOP01 48\n"
    "#WAV01 kick.wav\n#00011:01000000\n#00102:0.5\n#00111:00010000\n"
    "#00101:01000000\n#00108:00010000\n#00109:00000100\n#00211:01000000\n";

auto import_shift_bms(std::string_view source) -> song_document_t {
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                 {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(parsed.value.song);
    return std::move(*parsed.value.song);
}

auto export_shift_bms(const song_document_t& song) -> song_document_t {
    // Use the same default request as the GUI's target Preview/Export path.
    const auto plan = sys::output_group_export_plan_t::from(song,
        {.family = format_family_t::bms, .charts = {song.read_charts().front()->id}});
    REQUIRE(plan.request.branches == branch_projection_t::preserve_tree);
    const auto written = convert_document(song, plan.request);
    std::string diagnostics;
    for (const auto& diagnostic : written.diagnostics) diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(written);
    REQUIRE(written.outputs.size() == 1);
    return import_shift_bms(written.outputs.front().bytes);
}

void check_plain_bms_timing(const chart_document_t& chart, bool shifted) {
    const auto resolution = chart.divisions_per_standard_measure;
    const auto bpm_position = shifted ? rational_t{1, 4} : rational_t{1, 8};
    const auto pause_position = shifted ? rational_t{3, 8} : rational_t{1, 4};
    REQUIRE(chart.bpms.size() == 2);
    REQUIRE(chart.pauses.size() == 1);
    CHECK(rows_to_fraction(chart.bpms.back().position.row, resolution) == bpm_position);
    CHECK(rows_to_fraction(chart.pauses.front().position.row, resolution) == pause_position);
}

void check_plain_bms_content(const chart_document_t& chart) {
    REQUIRE(chart.measures.size() == 3);
    const auto& measure = chart.measures[1];
    CHECK(measure.definition.length == rational_t{1, 2});
    REQUIRE(measure.notes.size() == 1);
    CHECK(rows_to_fraction(measure.notes.front().position.row, chart.divisions_per_standard_measure) ==
          rational_t{1, 4});
}

} // namespace

TEST_CASE("shifting a selection moves notes and holds exactly and undoes",
          "[unit][core][time-shift]") {
    auto history = document_history_t::from(shift_song());
    const auto result = apply_shift_selection(
        history,
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
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{11}}, .rows = 800});
    REQUIRE(result);
    const auto moved = find_note(history, 11);
    REQUIRE(moved);
    CHECK(moved->first == second_measure);
    CHECK(moved->second.position == local_position_t{second_measure, 140});

    // Backward across the boundary: measure 4 row 60 minus 100 is measure 3 row 920.
    const auto back = apply_shift_selection(
        history,
        {.chart = chart_id_t{2}, .notes = {object_id_t{13}}, .rows = -100});
    REQUIRE(back);
    CHECK(find_note(history, 13)->first == first_measure);
    CHECK(find_note(history, 13)->second.position.row == 920);
}

TEST_CASE("shifts past either end, collisions and zero shifts", "[unit][core][time-shift]") {
    auto history = document_history_t::from(shift_song());
    const auto before_start = apply_shift_selection(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = -121});
    CHECK_FALSE(before_start.command.applied);
    CHECK(has_code(before_start, diagnostic_code_t::row_out_of_range));
    const auto past_end = apply_shift_selection(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{13}}, .rows = 420});
    CHECK(has_code(past_end, diagnostic_code_t::row_out_of_range));
    const auto stacked = apply_shift_selection(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = 180});
    CHECK_FALSE(stacked.command.applied);
    CHECK(has_code(stacked, diagnostic_code_t::collision));
    const auto zero = apply_shift_selection(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .rows = 0});
    CHECK_FALSE(zero.command.applied);
    CHECK(zero.command.diagnostics.empty());
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
    const auto unknown = apply_shift_selection(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{99}}, .rows = 10});
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
}

TEST_CASE("shifting from the cursor moves later content and optionally timing",
          "[unit][core][time-shift]") {
    auto content_only = document_history_t::from(shift_song());
    const auto cursor = local_position_t{first_measure, 300};
    const auto result = apply_shift_from_cursor(
        content_only, {.chart = chart_id_t{2}, .cursor = cursor, .rows = 40});
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
        with_timing,
        {.chart = chart_id_t{2}, .cursor = cursor, .rows = 40, .include_timing = true});
    REQUIRE(timed);
    CHECK(chart_of(with_timing).bpms[0].position.row == 0);
    CHECK(chart_of(with_timing).bpms[1].position.row == 520);
    CHECK(timed.payload.moved_objects == result.payload.moved_objects + 1);
}

TEST_CASE("Scoped selection shifts retain conditional outcomes while range shifts refuse",
          "[unit][core][time-shift]") {
    const auto from_cursor = GENERATE(false, true);
    auto history = document_history_t::from(import_shift_bms(std::string(plain_bms) +
        "#RANDOM 2\n#IF 1\n#00111:0100\n#ENDIF\n#ENDRANDOM\n"));
    const auto& chart = chart_of(history);
    const auto& note = chart.measures.front().notes.front();
    const auto result = from_cursor
        ? apply_shift_from_cursor(history, {.chart = chart.id, .cursor = note.position, .rows = 10})
        : apply_shift_selection(history, {.chart = chart.id, .notes = {note.id}, .rows = 10});
    CHECK(result.command.applied == !from_cursor);
    if (from_cursor) CHECK(has_code(result, diagnostic_code_t::unsupported_feature));
    CHECK(history.undo.size() == (from_cursor ? 0 : 1));
}

TEST_CASE("Retained BMS tree queries distinguish statements from all control-flow kinds",
          "[unit][core][time-shift][bms]") {
    const auto kind = GENERATE(bms_control_kind_t::root, bms_control_kind_t::raw_statement,
        bms_control_kind_t::random, bms_control_kind_t::set_random, bms_control_kind_t::if_group,
        bms_control_kind_t::if_branch, bms_control_kind_t::else_if_branch, bms_control_kind_t::else_branch,
        bms_control_kind_t::switch_block, bms_control_kind_t::set_switch,
        bms_control_kind_t::case_branch, bms_control_kind_t::default_branch, bms_control_kind_t::skip);
    const bms_control_node_t root{.kind = bms_control_kind_t::root,
        .children = {{.kind = bms_control_kind_t::root, .children = {{.kind = kind}}}}};
    CHECK(root.has_control_flow() ==
          (kind != bms_control_kind_t::root && kind != bms_control_kind_t::raw_statement));
}

TEST_CASE("Time shift allows ordinary imported BMS statements and exports moved notes",
          "[unit][core][time-shift][bms]") {
    auto history = document_history_t::from(import_shift_bms(plain_bms));
    const auto& chart = chart_of(history);
    REQUIRE(history.document.find_bms_program(chart.id));
    CHECK_FALSE(history.document.find_bms_program(chart.id)->prepared().program.has_control_flow());
    const auto note = chart.measures.front().notes.front().id;
    const auto result = apply_shift_selection(history, {.chart = chart.id, .notes = {note},
        .rows = static_cast<std::int64_t>(chart.divisions_per_standard_measure / 8)});
    REQUIRE(result);
    const auto reloaded = export_shift_bms(history.document);
    const auto& moved = *reloaded.read_charts().front();
    CHECK(rows_to_fraction(moved.measures.front().notes.front().position.row,
                           moved.divisions_per_standard_measure) == rational_t{1, 8});
    REQUIRE(undo_document_command(history));
    CHECK(chart_of(history).measures.front().notes.front().position.row == 0);
}

TEST_CASE("Time shift from cursor preserves plain BMS timing and measure lengths through export",
          "[unit][core][time-shift][bms]") {
    const auto include_timing = GENERATE(false, true);
    auto history = document_history_t::from(import_shift_bms(plain_bms));
    const auto& chart = chart_of(history);
    const auto result = apply_shift_from_cursor(history, {.chart = chart.id,
        .cursor = {chart.measures[1].id, 0},
        .rows = static_cast<std::int64_t>(chart.divisions_per_standard_measure / 8),
        .include_timing = include_timing});
    REQUIRE(result);
    check_plain_bms_content(chart_of(history));
    check_plain_bms_timing(chart_of(history), include_timing);
    const auto reloaded = export_shift_bms(history.document);
    check_plain_bms_content(*reloaded.read_charts().front());
    check_plain_bms_timing(*reloaded.read_charts().front(), include_timing);
    REQUIRE(undo_document_command(history));
    check_plain_bms_timing(chart_of(history), false);
}

namespace {

constexpr auto bar_a = measure_id_t{5};
constexpr auto bar_b = measure_id_t{6};
constexpr auto bar_c = measure_id_t{7};

// Three 4/4 measures of 960 rows: notes at absolute rows 120, 1000 and 1920, a BPM change at 1500.
auto bar_line_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = bar_a, .notes = {tap(40, 1, bar_a, 120)}},
                         {.id = bar_b, .notes = {tap(41, 1, bar_b, 40)}},
                         {.id = bar_c, .notes = {tap(42, 2, bar_c, 0)}}},
            .lane_layouts = {{.id = object_id_t{20},
                              .position = {bar_a, 0},
                              .layout = {.profile_id = "two",
                                         .lanes = {{.id = lane_id_t{1}, .name = "A"},
                                                   {.id = lane_id_t{2}, .name = "B"}}}}},
            .bpms = {{.id = object_id_t{21}, .position = {bar_a, 0}, .beats_per_minute = {120, 1}},
                     {.id = object_id_t{22}, .position = {bar_b, 540}, .beats_per_minute = {150, 1}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

struct bar_line_rows_t {
    std::vector<std::uint64_t> measure_rows;
    std::uint64_t total{};
    std::vector<std::uint64_t> notes;
    std::uint64_t bpm{};
};

auto bar_line_rows(const document_history_t& history) -> bar_line_rows_t {
    const auto& chart = chart_of(history);
    const auto index = measure_index_t::from(chart).value();
    bar_line_rows_t rows{.measure_rows = {}, .total = index.total_rows, .notes = {}, .bpm = 0};
    for (const auto& entry : index.entries) rows.measure_rows.push_back(entry.row_capacity);
    for (const auto id : {40, 41, 42})
        rows.notes.push_back(index.row_of(find_note(history, id)->second.position).value());
    rows.bpm = index.row_of(chart.bpms.back().position).value();
    return rows;
}

auto bar_line_shift(std::int64_t rows, bar_line_time_t time = bar_line_time_t::keep_song_time)
    -> shift_selection_action_t {
    return {.chart = chart_id_t{2}, .notes = {}, .bgm_events = {}, .rows = rows,
            .bar_lines = {bar_b}, .bar_line_time = time};
}

} // namespace

TEST_CASE("Moving a bar line keeps every object's song time by default",
          "[unit][core][time-shift][bar-line]") {
    auto history = document_history_t::from(bar_line_song());
    const auto result = apply_shift_selection(history, bar_line_shift(240));
    for (const auto& diagnostic : result.command.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.command.applied);
    const auto rows = bar_line_rows(history);
    CHECK(rows.measure_rows == std::vector<std::uint64_t>{1200, 720, 960});
    CHECK(rows.total == 2880);
    CHECK(rows.notes == std::vector<std::uint64_t>{120, 1000, 1920});
    CHECK(rows.bpm == 1500);
    CHECK(validate_document(history.document).empty());
    CHECK(history.undo.size() == 1);
}

TEST_CASE("A propagated bar-line move carries everything after it",
          "[unit][core][time-shift][bar-line]") {
    auto history = document_history_t::from(bar_line_song());
    auto action = bar_line_shift(240, bar_line_time_t::propagate);
    SECTION("timing rides along by default") {
        REQUIRE(apply_shift_selection(history, action).command.applied);
        const auto rows = bar_line_rows(history);
        CHECK(rows.measure_rows == std::vector<std::uint64_t>{1200, 960, 960});
        CHECK(rows.total == 3120);
        CHECK(rows.notes == std::vector<std::uint64_t>{120, 1240, 2160});
        CHECK(rows.bpm == 1740);
    }
    SECTION("timing can stay put") {
        action.include_timing = false;
        REQUIRE(apply_shift_selection(history, action).command.applied);
        CHECK(bar_line_rows(history).bpm == 1500);
    }
    SECTION("a selected note after the bar line moves once") {
        action.notes = {object_id_t{41}};
        REQUIRE(apply_shift_selection(history, action).command.applied);
        CHECK(bar_line_rows(history).notes == std::vector<std::uint64_t>{120, 1240, 2160});
    }
    SECTION("an earlier move shrinks the measure before it") {
        action.rows = -240;
        REQUIRE(apply_shift_selection(history, action).command.applied);
        const auto rows = bar_line_rows(history);
        CHECK(rows.measure_rows == std::vector<std::uint64_t>{720, 960, 960});
        CHECK(rows.notes == std::vector<std::uint64_t>{120, 760, 1680});
    }
    CHECK(validate_document(history.document).empty());
}

TEST_CASE("Staged bar-line moves leave notes the caller already moved",
          "[unit][core][time-shift][bar-line]") {
    auto document = bar_line_song();
    auto action = bar_line_shift(240, bar_line_time_t::propagate);
    action.already_moved_notes = {object_id_t{41}};
    const auto result = stage_shift_selection(document, action);
    REQUIRE(result.command.diagnostics.empty());
    const auto staged = document_history_t::from(document);
    CHECK(bar_line_rows(staged).notes == std::vector<std::uint64_t>{120, 1000, 2160});
}

TEST_CASE("Invalid bar-line moves reject without changing history",
          "[unit][core][time-shift][bar-line]") {
    auto history = document_history_t::from(bar_line_song());
    CHECK_FALSE(apply_shift_selection(history, bar_line_shift(960)).command.applied);
    CHECK_FALSE(apply_shift_selection(history, bar_line_shift(-960)).command.applied);
    auto first = bar_line_shift(240);
    first.bar_lines = {bar_a};
    CHECK_FALSE(apply_shift_selection(history, first).command.applied);
    auto empty = bar_line_shift(240, bar_line_time_t::propagate);
    empty.bar_lines.clear();
    CHECK_FALSE(apply_shift_selection(history, empty).command.applied);
    CHECK(history.undo.empty());
    CHECK(history.document.revision == 4);
}
