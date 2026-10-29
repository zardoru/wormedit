#include "core/interaction_gesture_runner.h"
#include "../support/stale_action_assertions.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <wormedit/core/interaction_cursor_pose.h>
#include <wormedit/core/interaction_queries.h>
#include <wormedit/core/interaction_dispatch.h>

using namespace wormedit;
static_assert(editor_gesture<pending_note_duration_gesture_t, object_id_t, local_position_t>);
static_assert(editor_gesture<hold_gesture_t, hold_start_t, update_hold_preview_action_t>);

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
        auto index = measure_index_t::from(history.document.charts.front());
        REQUIRE(index.has_value());
        return {std::move(history), std::move(*setup.state), std::move(index).value()};
    }

    auto context() -> editor_edit_context_t {
        return {history, state, {history.document.charts.front(), measures}};
    }

    void check_capture_view(const editor_interaction_result_t& result) const {
        REQUIRE(result.view);
        CHECK(result.view->active_gesture.has_value() == state.is_pending());
    }

    void check_capture_ended(std::size_t undo_count) const {
        CHECK_FALSE((state).is_pending());
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
    fixture.check_capture_view(rejected);
    CHECK((fixture.state).is_pending());
}

TEST_CASE("Duration core update retires stale captures", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    ++fixture.history.document.revision;
    const auto stale = revise_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 480});
    CHECK_FALSE(stale);
    fixture.check_capture_view(stale);
    test::check_stale_action_message(stale.diagnostics, "Editor gesture");
    fixture.check_capture_ended(0);
}

TEST_CASE("Duration release updates then commits once", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    REQUIRE(revise_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 240}));
    const auto released = apply_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 480});
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
    const auto missing = revise_gesture<pending_note_duration_gesture_t>(context,
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
    CHECK_FALSE(revise_gesture<pending_note_duration_gesture_t>(context, invalid_end));
    CHECK((fixture.state).is_pending());
    const auto rejected = apply_gesture<pending_note_duration_gesture_t>(context, invalid_end);
    CHECK_FALSE(rejected);
    fixture.check_capture_view(rejected);
    fixture.check_capture_ended(0);
}

static_assert(editor_gesture<pending_note_move_gesture_t, object_id_t, local_position_t,
                            lane_id_t, bool, std::optional<object_id_t>, std::optional<std::uint64_t>>);

TEST_CASE("Gesture kind mismatch retains the original capture", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_move_gesture_t>(context, object_id_t{101}, false));
    const auto rejected = revise_gesture<pending_note_duration_gesture_t>(context,
        local_position_t{measure_id_t{1}, 240});
    CHECK_FALSE(rejected);
    CHECK(fixture.state.gesture<pending_note_move_gesture_t>() != nullptr);
    CHECK(fixture.state.gesture<pending_note_duration_gesture_t>() == nullptr);
    CHECK(fixture.history.undo.empty());
}

static_assert(editor_gesture<pending_note_paint_gesture_t, begin_note_paint_gesture_action_t,
                            lane_id_t, local_position_t, std::optional<bmson_source_geometry_t>>);

TEST_CASE("Paint projection retains targets without copying capture bookkeeping", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_paint_gesture_t>(context,
        begin_note_paint_gesture_action_t{.lane = lane_id_t{1}, .position = {measure_id_t{1}, 240},
                                         .intent = note_paint_intent_t::draw}));
    const auto* capture = fixture.state.gesture<pending_note_paint_gesture_t>();
    const auto preview = build_editor_gesture_preview(fixture.state.active_gesture);
    REQUIRE((capture && preview));
    const auto& paint = std::get<note_paint_gesture_preview_t>(*preview);
    CHECK(paint.targets == capture->targets);
    REQUIRE(revise_gesture<pending_note_paint_gesture_t>(context, lane_id_t{1},
        local_position_t{measure_id_t{1}, 480}, std::nullopt));
    CHECK((paint.targets->size() == 1 && capture->targets->size() == 2));
}

static_assert(editor_gesture<pending_semantic_layer_paint_gesture_t,
    begin_semantic_layer_paint_gesture_action_t, std::optional<editor_selection_object_t>>);

TEST_CASE("Semantic paint commits without a final sampled target", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    fixture.history.document.charts.front().semantic_layers = {
        {.id = layer_id_t{7}, .name = "Phrase", .color = {255, 96, 48, 255}}};
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_semantic_layer_paint_gesture_t>(context,
        begin_semantic_layer_paint_gesture_action_t{.layer = layer_id_t{7}, .note = object_id_t{101}}));
    CHECK_FALSE(build_editor_gesture_preview(fixture.state.active_gesture));
    const auto committed = apply_gesture<pending_semantic_layer_paint_gesture_t>(context,
        std::optional<editor_selection_object_t>{});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    fixture.check_capture_ended(1);
}

static_assert(editor_gesture<pending_editor_area_selection_t,
                            begin_editor_area_selection_action_t, double, double>);

TEST_CASE("Area selection uses a borrowed dispatch view and ends without a document edit", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    const auto initial = inspect_editor_interaction(fixture.history, fixture.state);
    REQUIRE(std::holds_alternative<editor_interaction_view_model_t>(initial));
    context.initial_view = &std::get<editor_interaction_view_model_t>(initial);
    REQUIRE(begin_gesture<pending_editor_area_selection_t>(context,
        begin_editor_area_selection_action_t{.x_pixels = 10, .y_pixels = 0.5, .field_width_pixels = 100}));
    REQUIRE(apply_gesture<pending_editor_area_selection_t>(context, 20.0, 0.5));
    fixture.check_capture_ended(0);
}

TEST_CASE("View-dependent gestures reject a missing dispatch view", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    CHECK_FALSE(begin_gesture<pending_editor_area_selection_t>(context,
        begin_editor_area_selection_action_t{}));
    fixture.check_capture_ended(0);
}

TEST_CASE("Area capture permits cursor navigation and retains its pointer anchor", "[core][active-gesture][selection-navigation]") {
    auto fixture = gesture_fixture_t::from();
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        begin_editor_area_selection_action_t{.x_pixels = 10, .y_pixels = 0.5,
            .field_width_pixels = 100}));
    const auto anchor = fixture.state.gesture<pending_editor_area_selection_t>()->anchor_y_pixels;
    const std::array<editor_interaction_action_t, 4> actions{
        move_edit_cursor_action_t{interaction_direction_t::forward},
        page_edit_cursor_action_t{interaction_direction_t::forward},
        jump_edit_cursor_action_t{{measure_id_t{1}, 480}, fixture.history.document.revision},
        scroll_editor_view_action_t{48}};
    const auto navigation = GENERATE(0, 1, 2, 3);
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state, actions[navigation]));
    REQUIRE(fixture.state.gesture<pending_editor_area_selection_t>());
    CHECK(fixture.state.gesture<pending_editor_area_selection_t>()->anchor_y_pixels == anchor);
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        update_editor_area_selection_action_t{.x_pixels = 20, .y_pixels = 0.5,
            .operation = proposal_operation_t::apply}));
    fixture.check_capture_ended(0);
}

TEST_CASE("Area selection retains chart coordinates when navigation scrolls the field",
          "[core][active-gesture][selection-navigation]") {
    auto fixture = gesture_fixture_t::from();
    fixture.state.viewport_height_pixels = 96;
    fixture.history.document.charts.front().measures.front().notes.front().position.row = 240;
    const auto geometry = std::make_shared<const editor_area_geometry_snapshot_t>(
        editor_area_geometry_snapshot_t{.chart = fixture.state.chart,
            .document_revision = fixture.history.document.revision,
            .projection = editor_area_geometry_projection_t{
                .pixels_per_standard_measure = fixture.state.pixels_per_standard_measure,
                .viewport_height_pixels = fixture.state.viewport_height_pixels}});
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        begin_editor_area_selection_action_t{.x_pixels = 10, .y_pixels = 24,
            .field_width_pixels = 100, .object_geometry = geometry}));
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state, scroll_editor_view_action_t{48}));
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        update_editor_area_selection_action_t{.x_pixels = 90, .y_pixels = 1,
            .operation = proposal_operation_t::apply}));
    CHECK(fixture.state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    fixture.check_capture_ended(0);
}

TEST_CASE("Area previews preserve both existing anchor projection policies", "[core][active-gesture]") {
    const auto geometry = std::make_shared<const editor_area_geometry_snapshot_t>(
        editor_area_geometry_snapshot_t{.projection = editor_area_geometry_projection_t{
            .viewport_scroll_pixels = 200}});
    const pending_editor_area_selection_t pending{
        .anchor_y_pixels = 100, .current_y_pixels = 50, .object_geometry = geometry};
    const auto relative = editor_area_selection_preview_t::from(pending, document_stamp_t::from(7),
        250, editor_area_preview_anchor_policy_t::viewport_relative);
    const auto captured = editor_area_selection_preview_t::from(pending, document_stamp_t::from(7),
        250, editor_area_preview_anchor_policy_t::captured_coordinates);
    CHECK((relative.top_pixels == 50 && relative.bottom_pixels == 50));
    CHECK((captured.top_pixels == 50 && captured.bottom_pixels == 100));
}

TEST_CASE("Legacy hold blocks starting a Space range capture", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        begin_hold_action_t{{measure_id_t{1}, 240}}));
    const auto rejected = dispatch_editor_action(fixture.history, fixture.state,
        space_range_selection_action_t{});
    CHECK_FALSE(rejected);
    CHECK((fixture.state.hold() != nullptr));
    CHECK_FALSE(static_cast<bool>(fixture.state.gesture<wormedit::pending_space_range_selection_t>()));
    CHECK(fixture.history.undo.empty());
}

static_assert(editor_gesture<pending_space_range_selection_t, space_range_selection_action_t>);

TEST_CASE("Space commits its captured range preview without a document edit", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state, space_range_selection_action_t{}));
    place_cursor_at(fixture.state, {measure_id_t{1}, 480});
    const auto initial = inspect_editor_interaction(fixture.history, fixture.state);
    REQUIRE(std::holds_alternative<editor_interaction_view_model_t>(initial));
    auto context = fixture.context();
    context.initial_view = &std::get<editor_interaction_view_model_t>(initial);
    const auto objects = context.initial_view->gesture<space_range_selection_preview_t>()->objects;
    REQUIRE(apply_gesture<pending_space_range_selection_t>(context,
        editor_gesture_apply_view_policy_t::captured));
    CHECK(fixture.state.selected_objects == *objects);
    fixture.check_capture_ended(0);
}

TEST_CASE("Hold begin rejects replacing an active legacy capture", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    editor_interaction_action_t another = begin_hold_action_t{{measure_id_t{1}, 480}};
    SECTION("another legacy begin") {}
    SECTION("a keyboard lane begin") {
        fixture.history.document.charts.front().lane_layouts.front().layout.lanes.push_back(
            {.id = lane_id_t{2}, .name = "Second key"});
        another = begin_lane_key_hold_gesture_action_t{.lane = lane_id_t{2}};
    }
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state,
        begin_hold_action_t{{measure_id_t{1}, 240}}));
    const auto rejected = dispatch_editor_action(fixture.history, fixture.state, another);
    CHECK_FALSE(rejected);
    CHECK((fixture.state.hold() &&
        fixture.state.hold()->position == local_position_t{measure_id_t{1}, 240} &&
        !fixture.state.hold()->gesture_source));
    CHECK(fixture.state.additional_keyboard_holds().empty());
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Hold core updates retire stale captures", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<hold_gesture_t>(context, hold_start_t{.lane = lane_id_t{1}, .position = {measure_id_t{1}, 240}}));
    ++fixture.history.document.revision;
    const auto stale = revise_gesture<hold_gesture_t>(context, update_hold_preview_action_t{{measure_id_t{1}, 480}});
    CHECK_FALSE(stale);
    test::check_stale_action_message(stale.diagnostics, "Editor gesture");
    CHECK_FALSE((fixture.state).is_pending());
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Rejected hold completion ends the public capture", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    fixture.state.armed_note_kind = note_kind_t::hold;
    fixture.state.cursor = {measure_id_t{1}, 240};
    const bool is_keyboard = GENERATE(false, true);
    const editor_interaction_action_t begin = is_keyboard
        ? editor_interaction_action_t{begin_lane_key_hold_gesture_action_t{lane_id_t{1}}}
        : editor_interaction_action_t{begin_hold_action_t{{measure_id_t{1}, 240}}};
    const editor_interaction_action_t commit = is_keyboard
        ? editor_interaction_action_t{release_lane_key_hold_gesture_action_t{lane_id_t{1}}}
        : editor_interaction_action_t{commit_hold_action_t{{measure_id_t{1}, 240}}};
    REQUIRE(dispatch_editor_action(fixture.history, fixture.state, begin));
    CHECK_FALSE(dispatch_editor_action(fixture.history, fixture.state, commit));
    fixture.check_capture_ended(0);
}

TEST_CASE("Direct gesture calls validate a replacement capture on a reused context", "[core][active-gesture]") {
    auto fixture = gesture_fixture_t::from();
    auto context = fixture.context();
    REQUIRE(begin_gesture<pending_note_duration_gesture_t>(context, object_id_t{101}));
    REQUIRE(revise_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 240}));
    auto* stored = gesture_storage<pending_note_duration_gesture_t>(&*fixture.state.active_gesture);
    REQUIRE(stored);
    stored->stamp.revision = fixture.history.document.revision + 1;
    const auto stale = revise_gesture<pending_note_duration_gesture_t>(context, local_position_t{measure_id_t{1}, 480});
    CHECK_FALSE(stale);
    test::check_stale_action_message(stale.diagnostics, "Editor gesture");
    fixture.check_capture_ended(0);
}
