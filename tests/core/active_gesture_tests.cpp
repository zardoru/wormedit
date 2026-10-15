#include "core/interaction_gesture_runner.h"
#include "../support/stale_action_assertions.h"
#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace wormedit;
static_assert(editor_gesture<pending_note_duration_gesture_t, object_id_t, local_position_t>);

namespace {
struct gesture_fixture_t {
    document_history_t history;
    editor_interaction_state_t state;
    measure_index_t measures;

    static auto from() -> gesture_fixture_t {
        auto history = document_history_t::from({.id = song_id_t{1}, .charts = {{
            .id = chart_id_t{1}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}, .notes = {{.id = object_id_t{101},
                .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}}}}},
            .lane_layouts = {{.id = object_id_t{100}, .position = {measure_id_t{1}, 0},
                .layout = {.profile_id = "gesture-test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}}
        }}});
        constexpr std::array snaps{std::uint64_t{4}};
        auto setup = editor_interaction_state_t::from(history, chart_id_t{1}, lane_id_t{1},
            {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup);
        auto index = build_measure_index(history.document.charts.front());
        REQUIRE(std::holds_alternative<measure_index_t>(index));
        return {std::move(history), std::move(*setup.state), std::get<measure_index_t>(std::move(index))};
    }

    auto context() -> editor_edit_context_t {
        return {history, state, {history.document.charts.front(), measures}};
    }

    void check_capture_ended(std::size_t undo_count) const {
        CHECK_FALSE(has_pending_editor_gesture(state));
        REQUIRE(history.undo.size() == undo_count);
    }
};
} // namespace

TEST_CASE("Duration begin rejects every existing live capture at its core boundary", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    SECTION("paint") {
        REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
            begin_note_paint_gesture_action_t{.lane = lane_id_t{1}, .position = {measure_id_t{1}, 240}}));
    }
    SECTION("range") {
        REQUIRE(dispatch_editor_action(fixture.history, fixture.state, space_range_selection_action_t{}));
    }
    const auto rejected = begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101});
    CHECK_FALSE(rejected);
    CHECK(has_pending_editor_gesture(fixture.state));
}

TEST_CASE("Duration core update retires stale captures", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    ++fixture.history.document.revision;
    const auto stale = update_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 480});
    CHECK_FALSE(stale);
    test::check_stale_action_message(stale.diagnostics, "Editor gesture");
    fixture.check_capture_ended(0);
}

TEST_CASE("Duration release updates then commits once", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    REQUIRE(update_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 240}));
    const auto released = commit_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 480});
    REQUIRE(released);
    CHECK(released.document_changed);
    fixture.check_capture_ended(1);
    CHECK(fixture.history.undo.front().label == "Resize note duration");
    CHECK(fixture.history.document.charts.front().find_note(object_id_t{101})->end ==
        local_position_t{measure_id_t{1}, 480});
}

TEST_CASE("Gesture cancellation clears the capture without a command", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state, cancel_editor_gesture_action_t{}));
    fixture.check_capture_ended(0);
}

TEST_CASE("Gesture updates require an active capture", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    const auto missing = update_gesture<pending_note_duration_gesture_t>(context,
        local_position_t{measure_id_t{1}, 240});
    CHECK_FALSE(missing);
    REQUIRE(missing.diagnostics.size() == 1);
    CHECK(missing.diagnostics.front().message == "duration gesture update requires an active gesture");
}

TEST_CASE("Gesture terminal errors clear captures and ordinary update errors retain them", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    const auto invalid_end = local_position_t{measure_id_t{99}, 0};
    CHECK_FALSE(update_gesture<pending_note_duration_gesture_t>(context, invalid_end));
    CHECK(has_pending_editor_gesture(fixture.state));
    CHECK_FALSE(commit_gesture<pending_note_duration_gesture_t>(context, invalid_end));
    fixture.check_capture_ended(0);
}
