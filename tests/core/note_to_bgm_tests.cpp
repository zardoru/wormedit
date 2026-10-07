#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

using namespace wormedit;

namespace {

auto plain_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 3;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.lane_layouts.push_back({.id = object_id_t{100}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "t", .lanes = {{.id = lane_id_t{1}, .name = "A"}, {.id = lane_id_t{2}, .name = "B"}}}});
    chart.bgm_source_lanes = {{.id = object_id_t{9}, .name = "BGM 1", .source_order = 0},
                              {.id = object_id_t{10}, .name = "BGM 2", .source_order = 1}};
    chart.measures[0].notes = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
                               {.id = object_id_t{2}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{2}}};
    chart.measures[1].notes = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{1}}};
    song.charts.push_back(std::move(chart));
    return song;
}

} // namespace

TEST_CASE("notes moved to the BGM area take the preferred lane and shift over when it is taken",
          "[unit][core][note-to-bgm]") {
    auto history = make_document_history(plain_song());
    const std::array both{object_id_t{1}, object_id_t{2}};
    const auto moved = move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, both, object_id_t{9});
    for (const auto& d : moved.diagnostics) WARN(d.message);
    REQUIRE(moved.applied);
    const auto& chart = history.document.charts.front();
    CHECK(chart.measures[0].notes.empty());
    REQUIRE(chart.measures[0].bgm_events.size() == 2);
    // Same row: the first takes BGM 1, the second shifts to BGM 2.
    CHECK(chart.measures[0].bgm_events[0].source_lane == object_id_t{9});
    CHECK(chart.measures[0].bgm_events[1].source_lane == object_id_t{10});
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].notes.size() == 2);
}

TEST_CASE("a third note at the same row makes a new BGM lane; unknown notes reject atomically",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    song.charts.front().measures[0].notes.push_back(
        {.id = object_id_t{4}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}});
    auto history = make_document_history(song);
    const std::array three{object_id_t{1}, object_id_t{2}, object_id_t{4}};
    REQUIRE(move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, three, std::nullopt).applied);
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 3);

    auto other = make_document_history(plain_song());
    const std::array bad{object_id_t{1}, object_id_t{999}};
    const auto revision = other.document.revision;
    CHECK_FALSE(move_notes_to_bgm(other, revision, chart_id_t{1}, bad, std::nullopt).applied);
    CHECK(other.document.revision == revision);
    CHECK(other.document.charts.front().measures[0].notes.size() == 2);
    CHECK_FALSE(move_notes_to_bgm(other, revision + 5, chart_id_t{1}, std::array{object_id_t{1}}, std::nullopt).applied);
}

TEST_CASE("a BMSON stem tap goes back to its stem's BGM lane and keeps its slice point",
          "[unit][core][note-to-bgm][bmson]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = make_document_history(std::move(song));
    const auto chart_id = history.document.charts.front().id;
    const auto& chart = history.document.charts.front();
    const auto measure_it = std::ranges::find_if(chart.measures, [](const auto& m) { return !m.bgm_events.empty(); });
    REQUIRE(measure_it != chart.measures.end());
    const auto measure_index = static_cast<std::size_t>(measure_it - chart.measures.begin());
    const auto bgm = measure_it->bgm_events.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    REQUIRE(assign_bmson_sound_event(history, history.document.revision, chart_id, bgm.id, lane).applied);
    const auto undo_before = history.undo.size();

    const std::array note{bgm.id};
    const auto back = move_notes_to_bgm(history, history.document.revision, chart_id, note, std::nullopt);
    REQUIRE(back.applied);
    const auto& restored = history.document.charts.front().measures[measure_index];
    REQUIRE(restored.bgm_events.size() == 1);
    CHECK(restored.bgm_events.front().sound_channel_event == bgm.sound_channel_event);
    CHECK(history.undo.size() == undo_before + 1);
}
