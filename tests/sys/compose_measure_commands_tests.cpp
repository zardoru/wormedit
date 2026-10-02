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
    document_history_t history{make_document_history(measure_song())};
    editor_interaction_state_t state;

    explicit fixture_t(local_position_t cursor) {
        constexpr std::array snaps{std::uint64_t{4}};
        auto setup = make_editor_interaction(history, chart_id_t{2}, lane_id_t{1}, cursor, snaps, 4, 192.0);
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
