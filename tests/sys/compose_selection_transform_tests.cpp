#include "sys/compose/actions/compose_selection_transform.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto transform_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 1,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{3},
                          .notes = {{.id = object_id_t{40},
                                     .position = {measure_id_t{3}, 0},
                                     .lane = lane_id_t{1},
                                     .kind = note_kind_t::tap}}},
                         {.id = measure_id_t{4}}},
            .lane_layouts =
                {{.id = object_id_t{21},
                  .position = {measure_id_t{3}, 0},
                  .layout = {.profile_id = "three",
                             .lanes = {{.id = lane_id_t{1}, .name = "A", .display_order = 0},
                                       {.id = lane_id_t{2}, .name = "B", .display_order = 1},
                                       {.id = lane_id_t{3}, .name = "C", .display_order = 2}}}}},
            .input_profile = {.profile_id = "three",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

struct fixture_t {
    document_history_t history{document_history_t::from(transform_song())};
    editor_interaction_state_t state;

    fixture_t() {
        constexpr std::array snaps{std::uint64_t{4}};
        auto setup = editor_interaction_state_t::from(history, chart_id_t{2}, lane_id_t{1},
                                             local_position_t{measure_id_t{3}, 0}, snaps, 4, 192.0);
        REQUIRE(setup);
        state = std::move(*setup.state);
        state.selected_notes = {object_id_t{40}};
    }

    auto note() const -> const note_t& { return history.document.charts[0].measures[0].notes[0]; }
};

} // namespace

TEST_CASE("rotating lanes wraps around the stage", "[sys][selection-transform]") {
    const auto song = transform_song();
    const auto permutation = rotate_lane_permutation(song.charts[0].lane_layouts[0].layout, true);
    REQUIRE(permutation.size() == 3);
    CHECK(permutation[0].from == lane_id_t{1});
    CHECK(permutation[0].to == lane_id_t{3});
    CHECK(permutation[2].to == lane_id_t{2});
}

TEST_CASE("mirror and rotate move the selected note", "[sys][selection-transform]") {
    fixture_t fixture;
    CHECK(apply_selection_transform(fixture.history, fixture.state, selection_transform_t::mirror) ==
          "Selection mirrored.");
    CHECK(fixture.note().lane == lane_id_t{3});
    CHECK(apply_selection_transform(fixture.history, fixture.state,
                                    selection_transform_t::rotate_right) == "Selection rotated.");
    CHECK(fixture.note().lane == lane_id_t{1});
}

TEST_CASE("shift moves the selection by one snap step", "[sys][selection-transform]") {
    fixture_t fixture;
    CHECK(apply_selection_transform(fixture.history, fixture.state,
                                    selection_transform_t::shift_later) ==
          "Selection shifted by one snap step.");
    CHECK(fixture.history.document.charts[0].measures[0].notes[0].position.row == 240);
}

TEST_CASE("authored shift steps by the selection's own measure", "[sys][selection-transform][authored-snap]") {
    fixture_t fixture;
    auto song = transform_song();
    song.charts[0].measures[0].definition = {{5, 4}, meter_t{5, 4}};
    fixture.history = document_history_t::from(song);
    fixture.state.active_snap_space = quantization_space_t::authored_measure;
    CHECK(apply_selection_transform(fixture.history, fixture.state,
                                    selection_transform_t::shift_later) ==
          "Selection shifted by one snap step.");
    // A quarter of the 1200-row 5/4 measure, not of a standard measure.
    CHECK(fixture.note().position.row == 300);
}

TEST_CASE("transforms need a selection", "[sys][selection-transform]") {
    fixture_t fixture;
    fixture.state.selected_notes.clear();
    CHECK(apply_selection_transform(fixture.history, fixture.state, selection_transform_t::mirror) ==
          "Select notes before transforming them.");
}
