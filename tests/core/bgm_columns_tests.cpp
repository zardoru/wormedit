#include <wormedit/core/bgm_columns.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace wormedit;

namespace {

auto column_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .measures = {{
                .id = measure_id_t{3},
                .bgm_events = {{.id = object_id_t{11},
                                .position = {measure_id_t{3}, 0},
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

auto lane_count(const document_history_t& history) -> std::size_t {
    return history.document.charts.front().bgm_source_lanes.size();
}

} // namespace

TEST_CASE("BGM column count grows with fresh IDs orders and names",
          "[unit][core][bgm-columns]") {
    auto history = make_document_history(column_song());
    REQUIRE(apply_bgm_column_count(history, 4, chart_id_t{2}, 4));
    const auto& lanes = history.document.charts.front().bgm_source_lanes;
    REQUIRE(lanes.size() == 4);
    CHECK(lanes[3].name == "BGM 4");
    CHECK(lanes[3].source_order == 3);
    CHECK(lanes[1].id.value > 22);
    CHECK(lanes[2].id.value == lanes[1].id.value + 1);
    CHECK(history.document.charts.front().measures.front().bgm_events.front().id ==
          object_id_t{11});
}

TEST_CASE("BGM column count shrinks empty trailing columns only",
          "[unit][core][bgm-columns]") {
    auto history = make_document_history(column_song());
    REQUIRE(apply_bgm_column_count(history, 4, chart_id_t{2}, 3));
    const auto revision = history.document.revision;
    REQUIRE(apply_bgm_column_count(history, revision, chart_id_t{2}, 1));
    CHECK(lane_count(history) == 1);

    REQUIRE(apply_bgm_column_count(history, history.document.revision, chart_id_t{2}, 2));
    // Move the only event into the second column, then try to remove it.
    history.document.charts.front().measures.front().bgm_events.front().source_lane =
        history.document.charts.front().bgm_source_lanes.back().id;
    const auto blocked = apply_bgm_column_count(history, history.document.revision,
                                                chart_id_t{2}, 1);
    CHECK_FALSE(blocked);
    REQUIRE_FALSE(blocked.diagnostics.empty());
    CHECK(blocked.diagnostics.front().message.find("BGM 2") != std::string::npos);
    CHECK(lane_count(history) == 2);
}

TEST_CASE("BGM column count enforces its range and revision",
          "[unit][core][bgm-columns]") {
    auto history = make_document_history(column_song());
    CHECK_FALSE(apply_bgm_column_count(history, 4, chart_id_t{2}, 0));
    CHECK_FALSE(apply_bgm_column_count(history, 4, chart_id_t{2}, 129));
    CHECK_FALSE(apply_bgm_column_count(history, 3, chart_id_t{2}, 5));
    CHECK_FALSE(apply_bgm_column_count(history, 4, chart_id_t{99}, 5));
    CHECK(apply_bgm_column_count(history, 4, chart_id_t{2}, 128));
    CHECK(lane_count(history) == 128);
}

TEST_CASE("BGM column count changes undo and redo as one step",
          "[unit][core][bgm-columns]") {
    auto history = make_document_history(column_song());
    REQUIRE(apply_bgm_column_count(history, 4, chart_id_t{2}, 6));
    REQUIRE(undo_document_command(history));
    CHECK(lane_count(history) == 1);
    REQUIRE(redo_document_command(history));
    CHECK(lane_count(history) == 6);
}
