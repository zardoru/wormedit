#include "sys/compose_measure_commands.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto measure_song() -> song_document_t {
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
            .lane_layouts = {{.id = object_id_t{21},
                              .position = {measure_id_t{3}, 0},
                              .layout = {.profile_id = "one",
                                         .lanes = {{.id = lane_id_t{1}, .name = "A"}}}}},
            .input_profile = {.profile_id = "one",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

struct fixture_t {
    document_history_t history{document_history_t::from(measure_song())};
    editor_interaction_state_t state;

    explicit fixture_t(local_position_t cursor) {
        constexpr std::array snaps{std::uint64_t{4}};
        auto setup = editor_interaction_state_t::from(history, chart_id_t{2}, lane_id_t{1}, cursor, snaps, 4, 192.0);
        REQUIRE(setup);
        state = std::move(*setup.state);
    }

    auto measures() const -> const std::vector<measure_chunk_t>& {
        return history.document.charts[0].measures;
    }
};

} // namespace

TEST_CASE("inserting a measure before or after the cursor measure", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{4}, 0});
    CHECK(apply_cursor_measure_command(fixture.history, fixture.state,
                                       cursor_measure_command_t::insert_before) == "Measure inserted.");
    REQUIRE(fixture.measures().size() == 3);
    CHECK(fixture.measures()[2].id == measure_id_t{4});
    CHECK(apply_cursor_measure_command(fixture.history, fixture.state,
                                       cursor_measure_command_t::insert_after) == "Measure inserted.");
    CHECK(fixture.measures().size() == 4);
    CHECK(fixture.measures()[2].id == measure_id_t{4});
}

TEST_CASE("removing an empty measure moves the cursor to its neighbor", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{4}, 0});
    CHECK(apply_cursor_measure_command(fixture.history, fixture.state,
                                       cursor_measure_command_t::remove_empty) == "Measure removed.");
    CHECK(fixture.measures().size() == 1);
    CHECK(fixture.state.cursor.measure == measure_id_t{3});
}

TEST_CASE("removing a nonempty measure reports the core rejection", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{3}, 0});
    const auto status = apply_cursor_measure_command(fixture.history, fixture.state,
                                                     cursor_measure_command_t::remove_empty);
    CHECK(status != "Measure removed.");
    CHECK(fixture.measures().size() == 2);
    CHECK(fixture.state.cursor.measure == measure_id_t{3});
}

TEST_CASE("proposing a nonempty measure removal does not mutate the document", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{3}, 0});
    const auto proposal = propose_measure_removal(fixture.history.document.charts[0], measure_id_t{3});
    REQUIRE(proposal);
    CHECK(proposal.deleted_notes == 1);
    CHECK(proposal.deleted_bgm_events == 0);
    CHECK(proposal.deleted_bga_events == 0);
    CHECK(proposal.moved_positions == 1);
    CHECK(fixture.measures().size() == 2);
    CHECK(fixture.measures()[0].notes.size() == 1);
}

TEST_CASE("confirmed removal of a nonempty measure is one undoable command", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{3}, 0});
    CHECK(apply_cursor_measure_command(fixture.history, fixture.state,
                                       cursor_measure_command_t::remove_with_contents) ==
          "Measure removed with its contents.");
    REQUIRE(fixture.measures().size() == 1);
    CHECK(fixture.measures()[0].id == measure_id_t{4});
    CHECK(fixture.measures()[0].notes.empty());
    CHECK(fixture.state.cursor.measure == measure_id_t{4});

    REQUIRE(undo_document_command(fixture.history));
    REQUIRE(fixture.measures().size() == 2);
    CHECK(fixture.measures()[0].notes.size() == 1);
    CHECK(fixture.measures()[0].notes.front().id == object_id_t{40});
}

TEST_CASE("setting the meter of the cursor measure is one undoable command", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{4}, 0});
    CHECK(apply_cursor_measure_meter(fixture.history, fixture.state, "3", "4") == "Meter updated.");
    const auto& definition = fixture.measures()[1].definition;
    CHECK(definition.length == rational_t{3, 4});
    REQUIRE(definition.meter.has_value());
    CHECK(definition.meter->numerator == 3);
    CHECK(definition.meter->denominator == 4);
    CHECK(fixture.history.undo.size() == 1);
    // The other measure keeps its signature.
    CHECK(fixture.measures()[0].definition.length == rational_t{1, 1});

    CHECK(apply_cursor_measure_meter(fixture.history, fixture.state, "3", "4") == "Meter was unchanged.");
    CHECK(fixture.history.undo.size() == 1);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.measures()[1].definition.length == rational_t{1, 1});
}

TEST_CASE("a meter rejects text that is not a bounded non-zero number", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{4}, 0});
    const auto revision = fixture.history.document.revision;
    for (const auto* bad : {"", "0", "x", "-3", "3.5", "4294967296", "99999999999"}) {
        CHECK(apply_cursor_measure_meter(fixture.history, fixture.state, bad, "4") ==
              "A meter needs a non-zero numerator and denominator.");
        CHECK(apply_cursor_measure_meter(fixture.history, fixture.state, "4", bad) ==
              "A meter needs a non-zero numerator and denominator.");
    }
    CHECK(fixture.history.document.revision == revision);
}

TEST_CASE("mass meter sets every listed measure in one undo step", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{4}, 0});
    const std::vector<std::uint64_t> both{0, 1};
    CHECK(apply_measures_meter(fixture.history, fixture.state, both, "3", "4") ==
          "Meter updated on 2 measures.");
    CHECK(fixture.measures()[0].definition.length == rational_t{3, 4});
    CHECK(fixture.measures()[1].definition.length == rational_t{3, 4});
    CHECK(fixture.history.undo.size() == 1);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.measures()[0].definition.length == rational_t{1, 1});
    CHECK(fixture.measures()[1].definition.length == rational_t{1, 1});

    const auto revision = fixture.history.document.revision;
    const std::vector<std::uint64_t> unknown{0, 5000};
    CHECK(apply_measures_meter(fixture.history, fixture.state, unknown, "3", "4") !=
          "Meter updated on 2 measures.");
    CHECK(apply_measures_meter(fixture.history, fixture.state, {}, "3", "4") ==
          "Select at least one measure first.");
    CHECK(apply_measures_meter(fixture.history, fixture.state, both, "0", "4") ==
          "A meter needs a non-zero numerator and denominator.");
    CHECK(fixture.history.document.revision == revision);
}

TEST_CASE("moving a bar line trades length between neighbors in one undo step", "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{3}, 0});
    CHECK(apply_bar_line_move(fixture.history, fixture.state, 0, meter_t{7, 8}, meter_t{9, 8}) ==
          "Bar line moved.");
    CHECK(fixture.measures()[0].definition.length == rational_t{7, 8});
    CHECK(fixture.measures()[1].definition.length == rational_t{9, 8});
    CHECK(fixture.history.undo.size() == 1);
    CHECK(apply_bar_line_move(fixture.history, fixture.state, 0, meter_t{7, 8}, meter_t{9, 8}) ==
          "Bar line was unchanged.");
    const auto status = apply_bar_line_move(fixture.history, fixture.state, 0, meter_t{1, 4}, meter_t{9, 8});
    CHECK(status != "Bar line moved.");
    CHECK(fixture.history.undo.size() == 1);
}

#include "sys/compose_note_to_bgm.h"

TEST_CASE("selected notes move to the BGM area and leave the selection", "[sys][note-to-bgm]") {
    fixture_t fixture({measure_id_t{3}, 0});
    fixture.state.selected_notes = {object_id_t{40}};
    fixture.state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{40}}};
    const auto outcome = move_editor_selection_to_bgm(fixture.history, fixture.history.document.revision, fixture.state);
    REQUIRE(outcome.payload.handled);
    REQUIRE(outcome.command.applied);
    CHECK(fixture.measures()[0].notes.empty());
    REQUIRE(fixture.measures()[0].bgm_events.size() == 1);
    CHECK(fixture.state.selected_notes.empty());
    REQUIRE(fixture.state.selected_objects.size() == 1);
    CHECK(fixture.state.selected_objects.front().kind == editor_selection_object_kind_t::bgm_event);
    CHECK(fixture.history.undo.size() == 1);
}


TEST_CASE("confirmed removal drops deleted notes from the selection so the field scene stays valid",
          "[sys][measure-commands]") {
    fixture_t fixture({measure_id_t{3}, 0});
    fixture.state.selected_notes = {object_id_t{40}};
    CHECK(apply_cursor_measure_command(fixture.history, fixture.state,
                                       cursor_measure_command_t::remove_with_contents) ==
          "Measure removed with its contents.");
    CHECK(fixture.state.selected_notes.empty());
    CHECK(fixture.state.cursor.measure == measure_id_t{4});
}
