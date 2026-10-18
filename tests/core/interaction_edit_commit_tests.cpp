#include "core/interaction_edit_commit.h"
#include "core/interaction_gesture_runner.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <wormedit/formats/native_project.h>

using namespace wormedit;

namespace {
struct edit_fixture_t {
    document_history_t history;
    editor_interaction_state_t state;
    measure_index_t measures;

    static auto from() -> edit_fixture_t {
        auto history = document_history_t::from(
            {.id = song_id_t{1},
             .charts = {{.id = chart_id_t{1},
                         .divisions_per_standard_measure = 960,
                         .measures = {{.id = measure_id_t{1},
                                       .notes = {{.id = object_id_t{101},
                                                  .position = {measure_id_t{1}, 0},
                                                  .lane = lane_id_t{1}}}}},
                         .lane_layouts = {
                             {.id = object_id_t{100},
                              .position = {measure_id_t{1}, 0},
                              .layout = {.profile_id = "edit-test",
                                         .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}}}}});
        const std::array snaps{std::uint64_t{4}};
        auto setup = editor_interaction_state_t::from(history, chart_id_t{1}, lane_id_t{1},
                                                      {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup);
        auto measures = measure_index_t::from(history.document.charts.front());
        REQUIRE(measures);
        return {std::move(history), std::move(*setup.state), std::move(*measures)};
    }

    void check_published(const editor_interaction_result_t& result, std::string_view label) const {
        REQUIRE(result);
        CHECK(result.document_changed);
        REQUIRE(history.undo.size() == 1);
        CHECK(history.undo.front().label == label);
    }

    void check_unpublished(const std::optional<std::string>& before) const {
        CHECK(write_native_project(history.document).value == before);
        CHECK(history.undo.empty());
    }

    auto context() -> editor_edit_context_t {
        return {history, state, {history.document.charts.front(), measures}};
    }
};
} // namespace

TEST_CASE("Editor edit finisher rejects without publishing document or effects",
          "[core][edit-commit]") {
    auto fixture = edit_fixture_t::from();
    auto context = fixture.context();
    capture_gesture(context, hold_gesture_t{pending_hold_t{.position = {measure_id_t{1}, 0},
                                                           .lane = lane_id_t{1}}});
    const auto before = write_native_project(fixture.history.document);
    const auto captured = fixture.state.active_gesture;
    auto transaction = document_command_transaction_t(fixture.history);
    fixture.history.next_revision = std::numeric_limits<std::uint64_t>::max();
    transaction.document().charts.front().measures.front().notes.front().label = "Rejected";
    bool selected{};
    const auto result = commit_editor_edit(context, transaction,
                                           {.label = "Rejected edit",
                                            .selection = std::vector{object_id_t{101}},
                                            .cursor = local_position_t{measure_id_t{1}, 240},
                                            .lane = lane_id_t{99},
                                            .consume_primary_hold = true},
                                           [&](editor_interaction_state_t&) { selected = true; });
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    fixture.check_unpublished(before.value);
    CHECK((fixture.state.selected_notes.empty() && !selected &&
           fixture.state.active_gesture == captured));
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{1}, 0} &&
           fixture.state.active_lane == lane_id_t{1}));
}

TEST_CASE("Editor edit finisher publishes selection cursor and result payload once",
          "[core][edit-commit]") {
    auto fixture = edit_fixture_t::from();
    auto context = fixture.context();
    auto transaction = document_command_transaction_t(fixture.history);
    transaction.document().charts.front().measures.front().notes.front().label = "Changed";
    const auto result = commit_editor_edit(
        context, transaction,
        {.label = "Edit note",
         .selection = std::vector{object_id_t{101}},
         .cursor = local_position_t{measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .created_note = object_id_t{101},
         .audition = editor_object_audition_request_t{
             .chart = chart_id_t{1},
             .object = {editor_selection_object_kind_t::note, object_id_t{101}}}});
    fixture.check_published(result, "Edit note");
    CHECK(fixture.state.selected_notes == std::vector{object_id_t{101}});
    CHECK(fixture.state.cursor == local_position_t{measure_id_t{1}, 240});
    CHECK(result.created_note == object_id_t{101});
    REQUIRE(result.audition);
    CHECK(std::get<editor_object_audition_request_t>(*result.audition).document_revision ==
          fixture.history.document.revision);
}

TEST_CASE("Editor edit finisher applies no-op effects without an undo record",
          "[core][edit-commit]") {
    auto fixture = edit_fixture_t::from();
    auto context = fixture.context();
    const auto revision = fixture.history.document.revision;
    auto transaction = document_command_transaction_t(fixture.history);
    transaction.document();
    const auto result = commit_editor_edit(context, transaction,
                                           {.label = "No change",
                                            .has_document_changes = false,
                                            .selection = std::vector{object_id_t{101}},
                                            .cursor = local_position_t{measure_id_t{1}, 240}});
    REQUIRE(result);
    CHECK_FALSE(result.document_changed);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
    CHECK(fixture.state.selected_notes == std::vector{object_id_t{101}});
    CHECK(fixture.state.cursor == local_position_t{measure_id_t{1}, 240});
}

TEST_CASE("Editor edit finisher restamps a surviving capture", "[core][edit-commit]") {
    auto fixture = edit_fixture_t::from();
    auto context = fixture.context();
    capture_gesture(context, hold_gesture_t{pending_hold_t{.position = {measure_id_t{1}, 240},
                                                           .lane = lane_id_t{1}}});
    const auto before = fixture.state.gesture_stamp<hold_gesture_t>()->revision;
    auto transaction = document_command_transaction_t(fixture.history);
    transaction.document().charts.front().measures.front().notes.front().label = "Changed";
    const auto result = commit_editor_edit(context, transaction, {.label = "Keep capture"});
    REQUIRE(result);
    REQUIRE(fixture.state.gesture_stamp<hold_gesture_t>());
    CHECK(fixture.state.gesture_stamp<hold_gesture_t>()->revision > before);
    CHECK(fixture.state.gesture_stamp<hold_gesture_t>()->revision ==
          fixture.history.document.revision);
    REQUIRE(result.view);
    CHECK(result.view->active_gesture.has_value());
}
