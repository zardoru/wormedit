#include <wormedit/core/lane_layout_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace wormedit;

namespace {

auto layout_with(std::vector<std::uint64_t> lane_ids, const char* profile) -> lane_layout_t {
    lane_layout_t layout{.profile_id = profile, .display_name = profile};
    for (const auto id : lane_ids) {
        layout.lanes.push_back({.id = lane_id_t{id}, .name = "Lane " + std::to_string(id)});
    }
    return layout;
}

auto layout_song(bool note_on_third_lane) -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{10}},
                                 {.id = measure_id_t{11},
                                  .notes = {{.id = object_id_t{30},
                                             .position = {measure_id_t{11}, 0},
                                             .lane = lane_id_t{note_on_third_lane ? 22U : 20U}}}},
                                 {.id = measure_id_t{12}}},
                    .lane_layouts = {{.id = object_id_t{21},
                                      .position = {measure_id_t{10}, 0},
                                      .layout = layout_with({20, 21, 22}, "three")}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto segment_count(const document_history_t& history) -> std::size_t {
    return history.document.charts.front().lane_layouts.size();
}

} // namespace

TEST_CASE("a narrower layout may start where no later note needs the dropped lanes",
          "[core][lane-layout-editor]") {
    auto history = make_document_history(layout_song(false));
    const auto applied = apply_lane_layout_edit(
        history, history.document.revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{11}, 0},
                                    layout_with({20, 21}, "two")});
    REQUIRE(applied);
    REQUIRE(segment_count(history) == 2);
    CHECK(history.document.charts.front().lane_layouts.back().layout.profile_id == "two");

    REQUIRE(undo_document_command(history));
    CHECK(segment_count(history) == 1);
}

TEST_CASE("a layout that leaves a later note unmapped rejects with the count",
          "[core][lane-layout-editor][negative]") {
    auto history = make_document_history(layout_song(true));
    const auto result = apply_lane_layout_edit(
        history, history.document.revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{11}, 0},
                                    layout_with({20, 21}, "two")});
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("1 note") != std::string::npos);
    CHECK(segment_count(history) == 1);
    CHECK(history.undo.empty());

    // Starting the narrow layout after the note leaves it on the wide one.
    CHECK(apply_lane_layout_edit(
        history, history.document.revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{12}, 0},
                                    layout_with({20, 21}, "two")}));
}

TEST_CASE("update and delete keep notes mapped and protect the chart start",
          "[core][lane-layout-editor]") {
    auto history = make_document_history(layout_song(true));
    CHECK_FALSE(apply_lane_layout_edit(
        history, history.document.revision,
        update_lane_layout_action_t{chart_id_t{2}, object_id_t{21}, layout_with({20}, "one")}));
    REQUIRE(apply_lane_layout_edit(
        history, history.document.revision,
        update_lane_layout_action_t{chart_id_t{2}, object_id_t{21},
                                    layout_with({22, 20}, "reordered")}));
    CHECK(history.document.charts.front().lane_layouts.front().layout.profile_id == "reordered");

    CHECK_FALSE(apply_lane_layout_edit(
        history, history.document.revision,
        delete_lane_layout_action_t{chart_id_t{2}, object_id_t{21}}));

    REQUIRE(apply_lane_layout_edit(
        history, history.document.revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{12}, 0},
                                    layout_with({20, 22}, "late")}));
    const auto inserted = history.document.charts.front().lane_layouts.back().id;
    REQUIRE(apply_lane_layout_edit(history, history.document.revision,
                                   delete_lane_layout_action_t{chart_id_t{2}, inserted}));
    CHECK(segment_count(history) == 1);
}

TEST_CASE("layout edits reject mid-measure starts, duplicates, stale revisions and unknown ids",
          "[core][lane-layout-editor][negative]") {
    auto history = make_document_history(layout_song(false));
    const auto revision = history.document.revision;
    const auto narrow = layout_with({20, 21}, "two");

    CHECK_FALSE(apply_lane_layout_edit(
        history, revision, insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{11}, 480}, narrow}));
    CHECK(apply_lane_layout_edit(
        history, revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{11}, 480}, narrow},
        lane_layout_change_policy_t::any_row));

    auto fresh = make_document_history(layout_song(false));
    CHECK_FALSE(apply_lane_layout_edit(
        fresh, fresh.document.revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{10}, 0}, narrow}));
    CHECK_FALSE(apply_lane_layout_edit(
        fresh, fresh.document.revision + 1,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{11}, 0}, narrow}));
    CHECK_FALSE(apply_lane_layout_edit(
        fresh, fresh.document.revision,
        insert_lane_layout_action_t{chart_id_t{2}, {measure_id_t{99}, 0}, narrow}));
    CHECK_FALSE(apply_lane_layout_edit(
        fresh, fresh.document.revision,
        update_lane_layout_action_t{chart_id_t{2}, object_id_t{999}, narrow}));
    CHECK_FALSE(apply_lane_layout_edit(
        fresh, fresh.document.revision,
        delete_lane_layout_action_t{chart_id_t{9}, object_id_t{21}}));
}
