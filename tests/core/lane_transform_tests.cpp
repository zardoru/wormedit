#include <wormedit/core/lane_transform.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace wormedit;

namespace {

constexpr auto measure = measure_id_t{3};

auto key_lane(std::uint64_t id, std::uint32_t order, stage_group_t stage = stage_group_t::none)
    -> lane_definition_t {
    return {.id = lane_id_t{id},
            .name = "Key",
            .role = lane_role_t::playable,
            .stage = stage,
            .display_order = order};
}

auto tap(std::uint64_t id, std::uint64_t lane, std::uint64_t row) -> note_t {
    return {.id = object_id_t{id}, .position = {measure, row}, .lane = lane_id_t{lane}};
}

// Scratch lane 8 plus keys 1-7, with a tap on keys 1 and 3, a scratch tap, and
// a hold on key 7.
auto seven_key_song(std::vector<lane_definition_t> lanes = {}) -> song_document_t {
    if (lanes.empty()) {
        lanes.push_back({.id = lane_id_t{8},
                         .name = "Scratch",
                         .role = lane_role_t::scratch,
                         .display_order = 0});
        for (std::uint32_t key = 1; key <= 7; ++key)
            lanes.push_back(key_lane(key, key));
    }
    auto hold = tap(14, 7, 480);
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure, 600};
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure,
                          .notes = {tap(10, 1, 0), tap(11, 3, 120), tap(12, 8, 240), hold}}},
            .lane_layouts = {{.id = object_id_t{20},
                              .position = {measure, 0},
                              .layout = {.profile_id = "seven", .lanes = std::move(lanes)}}},
            .bpms = {{.id = object_id_t{21},
                      .position = {measure, 0},
                      .beats_per_minute = {120, 1}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto lane_of(const document_history_t& history, std::uint64_t note) -> std::uint64_t {
    for (const auto& item : history.document.charts.front().measures.front().notes)
        if (item.id.value == note)
            return item.lane.value;
    return 0;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

const std::vector<object_id_t> all_notes{object_id_t{10}, object_id_t{11}, object_id_t{12},
                                         object_id_t{14}};

} // namespace

TEST_CASE("mirror reverses keys, keeps the scratch lane, and undoes",
          "[unit][core][lane-transform]") {
    auto history = make_document_history(seven_key_song());
    const auto before = history.document.charts.front().measures.front().notes;
    const auto result = apply_lane_mirror(history, 4, {.chart = chart_id_t{2}, .notes = all_notes});
    REQUIRE(result);
    CHECK(lane_of(history, 10) == 7);
    CHECK(lane_of(history, 11) == 5);
    CHECK(lane_of(history, 12) == 8);
    CHECK(lane_of(history, 14) == 1);
    const auto& after = history.document.charts.front().measures.front().notes;
    for (std::size_t index = 0; index < after.size(); ++index) {
        CHECK(after[index].position == before[index].position);
        CHECK(after[index].kind == before[index].kind);
        CHECK(after[index].end == before[index].end);
    }
    REQUIRE(undo_document_command(history));
    CHECK(lane_of(history, 10) == 1);
    CHECK(lane_of(history, 14) == 7);
}

TEST_CASE("mirror works inside each stage of a split layout", "[unit][core][lane-transform]") {
    auto history = make_document_history(seven_key_song(
        {key_lane(1, 0, stage_group_t::left), key_lane(2, 1, stage_group_t::left),
         key_lane(3, 2, stage_group_t::right), key_lane(4, 3, stage_group_t::right),
         {.id = lane_id_t{8}, .name = "Scratch", .role = lane_role_t::scratch, .display_order = 4},
         key_lane(7, 5, stage_group_t::right)}));
    // Notes: 1 (left), 3 (right), 8 (scratch), 7 (right hold).
    const auto result = apply_lane_mirror(history, 4, {.chart = chart_id_t{2}, .notes = all_notes});
    REQUIRE(result);
    CHECK(lane_of(history, 10) == 2);
    CHECK(lane_of(history, 11) == 7);
    CHECK(lane_of(history, 12) == 8);
    CHECK(lane_of(history, 14) == 3);
}

TEST_CASE("mirror of only non-playable notes is a no-op", "[unit][core][lane-transform]") {
    auto history = make_document_history(seven_key_song());
    const auto result =
        apply_lane_mirror(history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{12}}});
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("remap swaps lanes and rejects collisions, missing lanes and bad permutations",
          "[unit][core][lane-transform]") {
    auto history = make_document_history(seven_key_song());
    const auto swap = apply_lane_remap(
        history, 4,
        {.chart = chart_id_t{2},
         .notes = {object_id_t{10}, object_id_t{11}},
         .permutation = {{lane_id_t{1}, lane_id_t{3}}, {lane_id_t{3}, lane_id_t{1}}}});
    REQUIRE(swap);
    CHECK(lane_of(history, 10) == 3);
    CHECK(lane_of(history, 11) == 1);

    // Moving only one of two same-row notes onto the other's lane would stack them.
    auto stacked = seven_key_song();
    stacked.charts.front().measures.front().notes.push_back(tap(15, 2, 0));
    auto collide = make_document_history(std::move(stacked));
    const auto collision = apply_lane_remap(
        collide, 4,
        {.chart = chart_id_t{2},
         .notes = {object_id_t{10}},
         .permutation = {{lane_id_t{1}, lane_id_t{2}}, {lane_id_t{2}, lane_id_t{1}}}});
    CHECK_FALSE(collision.applied);
    CHECK(has_code(collision, diagnostic_code_t::collision));
    CHECK(collide.document.revision == 4);

    const auto missing = apply_lane_remap(
        history, history.document.revision,
        {.chart = chart_id_t{2},
         .notes = {object_id_t{10}},
         .permutation = {{lane_id_t{3}, lane_id_t{99}}, {lane_id_t{99}, lane_id_t{3}}}});
    CHECK_FALSE(missing.applied);
    CHECK(has_code(missing, diagnostic_code_t::unknown_lane));

    const auto not_bijection = apply_lane_remap(
        history, history.document.revision,
        {.chart = chart_id_t{2},
         .notes = {object_id_t{10}},
         .permutation = {{lane_id_t{1}, lane_id_t{2}}}});
    CHECK_FALSE(not_bijection.applied);
    CHECK(has_code(not_bijection, diagnostic_code_t::invalid_resolution));
}

TEST_CASE("lane transforms reject stale revisions and unknown notes",
          "[unit][core][lane-transform]") {
    auto history = make_document_history(seven_key_song());
    const auto stale = apply_lane_mirror(history, 3, {.chart = chart_id_t{2}, .notes = all_notes});
    CHECK_FALSE(stale.applied);
    const auto unknown =
        apply_lane_mirror(history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{99}}});
    CHECK_FALSE(unknown.applied);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
    const auto wrong_chart =
        apply_lane_mirror(history, 4, {.chart = chart_id_t{9}, .notes = all_notes});
    CHECK_FALSE(wrong_chart.applied);
    const auto twice = apply_lane_mirror(
        history, 4, {.chart = chart_id_t{2}, .notes = {object_id_t{10}, object_id_t{10}}});
    CHECK(has_code(twice, diagnostic_code_t::duplicate_id));
    CHECK(history.document.revision == 4);
}
