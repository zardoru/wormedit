#include <wormedit/formats/bms_clipboard.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/formats/bms.h>
#include <wormedit/core/bgm_columns.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace wormedit;

namespace {

auto column_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .source_family = format_family_t::bms,
            .measures = {{
                .id = measure_id_t{3},
                .bgm_events = {{.id = object_id_t{11},
                                .position = {measure_id_t{3}, 24},
                                .source_lane = object_id_t{20}}},
            }},
            .lane_layouts =
                {{.id = object_id_t{21},
                  .position = {measure_id_t{3}, 0},
                  .layout = {.profile_id = "one-key",
                             .lanes = {{.id = lane_id_t{1}, .name = "Key", .display_order = 0}}}}},
            .input_profile = {.profile_id = "one-key",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
            .bgm_source_lanes = {{.id = object_id_t{20}, .name = "BGM 1", .source_order = 0}},
            .bpms = {{.id = object_id_t{22},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto osu_mania_column_song() -> song_document_t {
    auto song = column_song();
    song.charts.front().source_family = format_family_t::osu_mania;
    song.resources.push_back({.id = resource_id_t{40},
                              .name = "background-sample.wav",
                              .asset = asset_id_t{40},
                              .roles = {resource_role_t::sample}});
    song.audio_assets.push_back({.id = asset_id_t{40},
                                 .source_path = "background-sample.wav"});
    song.charts.front().measures.front().bgm_events.front().sound = resource_id_t{40};
    const auto diagnostics = validate_document(song);
    INFO("validation diagnostics: "
         << diagnostics.size() << (diagnostics.empty() ? "" : ": " + diagnostics.front().message));
    REQUIRE(diagnostics.empty());
    return song;
}

auto bgm_only_song() -> song_document_t {
    auto song = column_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile.bindings.clear();
    REQUIRE(validate_document(song).empty());
    return song;
}

auto lane_count(const document_history_t& history) -> std::size_t {
    return history.document.charts.front().bgm_source_lanes.size();
}

void check_original_bgm_event(const chart_document_t& chart) {
    const auto& event = chart.measures.front().bgm_events.front();
    CHECK(event.id == object_id_t{11});
    CHECK((event.position == local_position_t{measure_id_t{3}, 24}));
    CHECK(event.source_lane == object_id_t{20});
}

void check_bgm_only_chart(const document_history_t& history, std::size_t expected_columns) {
    const auto& chart = history.document.charts.front();
    CHECK(chart.bgm_source_lanes.size() == expected_columns);
    CHECK(chart.lane_layouts.empty());
    CHECK(chart.input_profile.bindings.empty());
    check_original_bgm_event(chart);
}

void require_column_count(document_history_t& history, std::size_t count) {
    REQUIRE(apply_bgm_column_count(history, chart_id_t{2}, count));
}

void check_blocked_shrink(document_history_t& history) {
    const auto blocked = apply_bgm_column_count(history,
                                                chart_id_t{2}, 1);
    CHECK_FALSE(blocked);
    REQUIRE_FALSE(blocked.diagnostics.empty());
    CHECK(blocked.diagnostics.front().message.find("BGM 2") != std::string::npos);
    CHECK(lane_count(history) == 2);
}

} // namespace

TEST_CASE("BGM column count grows with fresh IDs orders and names",
          "[unit][core][bgm-columns]") {
    auto history = document_history_t::from(column_song());
    REQUIRE(apply_bgm_column_count(history, chart_id_t{2}, 4));
    const auto& lanes = history.document.charts.front().bgm_source_lanes;
    REQUIRE(lanes.size() == 4);
    CHECK(lanes[3].name == "BGM 4");
    CHECK(lanes[3].source_order == 3);
    CHECK(lanes[1].id.value > 22);
    CHECK(lanes[2].id.value == lanes[1].id.value + 1);
    check_original_bgm_event(history.document.charts.front());
}

TEST_CASE("BGM-only BMS charts change column count without inventing playable lanes",
          "[unit][core][bgm-columns]") {
    auto history = document_history_t::from(bgm_only_song());

    REQUIRE(apply_bgm_column_count(history, chart_id_t{2}, 3));
    check_bgm_only_chart(history, 3);
    REQUIRE(undo_document_command(history));
    check_bgm_only_chart(history, 1);
}

TEST_CASE("BGM column count shrinks empty trailing columns only",
          "[unit][core][bgm-columns]") {
    auto history = document_history_t::from(column_song());
    require_column_count(history, 3);
    require_column_count(history, 1);
    CHECK(lane_count(history) == 1);
    check_original_bgm_event(history.document.charts.front());

    require_column_count(history, 2);
    // Move the only event into the second column, then try to remove it.
    history.document.charts.front().measures.front().bgm_events.front().source_lane =
        history.document.charts.front().bgm_source_lanes.back().id;
    check_blocked_shrink(history);
}

TEST_CASE("BGM column count enforces its range and chart",
          "[unit][core][bgm-columns]") {
    auto history = document_history_t::from(column_song());
    CHECK_FALSE(apply_bgm_column_count(history, chart_id_t{2}, 0));
    CHECK_FALSE(apply_bgm_column_count(history, chart_id_t{2}, 129));
    CHECK_FALSE(apply_bgm_column_count(history, chart_id_t{99}, 5));
    CHECK(apply_bgm_column_count(history, chart_id_t{2}, 128));
    CHECK(lane_count(history) == 128);
}

TEST_CASE("BGM column count changes undo and redo as one step",
          "[unit][core][bgm-columns]") {
    auto history = document_history_t::from(column_song());
    REQUIRE(apply_bgm_column_count(history, chart_id_t{2}, 6));
    REQUIRE(undo_document_command(history));
    CHECK(lane_count(history) == 1);
    REQUIRE(redo_document_command(history));
    CHECK(lane_count(history) == 6);
}

TEST_CASE("osu!mania BGM column growth preserves samples and undoes as one step",
          "[unit][core][bgm-columns][osu-mania]") {
    auto song = osu_mania_column_song();
    auto history = document_history_t::from(std::move(song));

    REQUIRE(apply_bgm_column_count(history, chart_id_t{2}, 4));
    CHECK(lane_count(history) == 4);
    check_original_bgm_event(history.document.charts.front());
    CHECK(history.document.charts.front().measures.front().bgm_events.front().sound ==
          resource_id_t{40});

    REQUIRE(undo_document_command(history));
    CHECK(lane_count(history) == 1);
    check_original_bgm_event(history.document.charts.front());
    CHECK(history.document.charts.front().measures.front().bgm_events.front().sound ==
          resource_id_t{40});
}

TEST_CASE("BGM column count rejects BMSON without changing its lanes",
          "[unit][core][bgm-columns]") {
    auto song = column_song();
    song.charts.front().source_family = format_family_t::bmson;
    auto history = document_history_t::from(std::move(song));
    const auto revision = history.document.revision;
    const auto result = apply_bgm_column_count(history, chart_id_t{2}, 6);
    CHECK_FALSE(result.applied);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().message ==
          "BGM column counts are available only for BMS and osu!mania charts");
    CHECK(history.document.revision == revision);
    CHECK(lane_count(history) == 1);
    check_original_bgm_event(history.document.charts.front());
    CHECK(history.undo.empty());
}

TEST_CASE("BGM column growth reserves all IDs before reaching the ceiling",
          "[unit][core][bgm-columns]") {
    auto song = column_song();
    song.charts.front().bpms.front().id =
        object_id_t{std::numeric_limits<std::uint64_t>::max() - 2};
    auto history = document_history_t::from(song);
    const auto blocked = apply_bgm_column_count(history, chart_id_t{2}, 4);
    CHECK_FALSE(blocked);
    REQUIRE_FALSE(blocked.diagnostics.empty());
    CHECK(blocked.diagnostics.front().message == "BGM source lane ID space is exhausted");
    CHECK((history.document.revision == song.revision && history.undo.empty() &&
           lane_count(history) == 1));
    require_column_count(history, 3);
    const auto& lanes = history.document.charts.front().bgm_source_lanes;
    CHECK((lanes.size() == 3 &&
           lanes[1].id.value == std::numeric_limits<std::uint64_t>::max() - 1 &&
           lanes[2].id.value == std::numeric_limits<std::uint64_t>::max()));
}

namespace {
void check_scoped_columns(bool value) { CHECK(value); }
}
TEST_CASE("Scoped BGM count edits retain inactive column references and one Undo step", "[core][bgm-columns][bms-public-cutover]") {
    const std::string text = "#WAV01 a.wav\n#00001:01\n#RANDOM 2\n#IF 2\n#00001:01\n#ENDIF\n#ENDRANDOM\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{text.data(), text.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    check_scoped_columns(parsed.value.song.has_value());
    auto history = document_history_t::from(*parsed.value.song);
    const auto difficulty = history.document.read_charts().front()->id;
    const auto before = history.document.find_bms_program(difficulty)->prepared().bgm_columns.size();
    check_scoped_columns(before == 2);
    check_scoped_columns(apply_bgm_column_count(history, difficulty, 3).applied);
    check_scoped_columns(history.read_chart(difficulty)->bgm_source_lanes.size() == 3);
    check_scoped_columns(history.undo.size() == 1);
    const auto revision = history.document.revision;
    check_scoped_columns(!apply_bgm_column_count(history, difficulty, 1).applied);
    check_scoped_columns(history.document.revision == revision);
    check_scoped_columns(apply_bgm_column_count(history, difficulty, 2).applied);
    check_scoped_columns(undo_document_command(history));
    check_scoped_columns(history.read_chart(difficulty)->bgm_source_lanes.size() == 3);
    check_scoped_columns(undo_document_command(history));
    check_scoped_columns(history.document.find_bms_program(difficulty)->prepared().bgm_columns.size() == before);
    check_scoped_columns(redo_document_command(history));
    check_scoped_columns(history.read_chart(difficulty)->bgm_source_lanes.size() == 3);
}

TEST_CASE("Scoped BGM paste reveals retained columns before growing its preview", "[core][bgm-columns][bms-public-cutover]") {
    const std::string text = "#WAV01 a.wav\n#00001:01\n#RANDOM 2\n#IF 2\n#00001:01\n#ENDIF\n#ENDRANDOM\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{text.data(), text.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    check_scoped_columns(parsed.value.song.has_value());
    auto history = document_history_t::from(*parsed.value.song);
    const auto* chart = history.document.read_charts().front();
    const auto difficulty = chart->id;
    const auto anchor = local_position_t{chart->measures.front().id, 0};
    const auto clipboard = parse_bms_chart_clipboard("iBMSC Clipboard Data\r\n29 0 10000 0 0 0",
        history.document, *chart, chart->lane_layouts.front().layout);
    check_scoped_columns(clipboard.has_value());
    const auto hidden = chart_paste_proposal_t::propose(history.document, difficulty, *clipboard,
        anchor, history.document.revision);
    check_scoped_columns(!hidden.value.has_value());
    check_scoped_columns(history.undo.empty());
    check_scoped_columns(apply_bgm_column_count(history, difficulty, 2).applied);
    const auto visible = chart_paste_proposal_t::propose(history.document, difficulty, *clipboard,
        anchor, history.document.revision);
    check_scoped_columns(visible.value.has_value());
    check_scoped_columns(paste_chart_clipboard(history, difficulty, *clipboard, anchor).applied);
    check_scoped_columns(history.read_chart(difficulty)->bgm_source_lanes.size() == 3);
    check_scoped_columns(history.undo.size() == 2);
    check_scoped_columns(undo_document_command(history));
    check_scoped_columns(history.read_chart(difficulty)->bgm_source_lanes.size() == 2);
}
