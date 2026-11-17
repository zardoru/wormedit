// Compose hit-test cache, measure-lane drags and timing-ghost identities.
#include "frame_timing_fixtures.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("Compose hit-test cache reuses a chart revision and rebuilds after invalidation",
          "[sys][hit-test-cache]") {
    auto song = compose_test_song();
    const auto& chart = song.charts.front();
    compose_scene_cache_t cache;
    const auto* index = cache.hit_test_index_for(chart, song.revision);
    REQUIRE(index);
    CHECK(cache.hit_test_index_for(chart, song.revision) == index);
    CHECK(cache.scene.hit_test_index.build_count() == 1);
    CHECK(cache.hit_test_index_for(chart, song.revision + 1)->document_revision == song.revision + 1);
    CHECK(cache.scene.hit_test_index.build_count() == 2);
    cache.invalidate_scene_cache();
    CHECK_FALSE(cache.scene.hit_test_index.has_value());
}

TEST_CASE("Compose hit-test cache retries failed geometry without retaining a failure",
          "[sys][hit-test-cache]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    const auto divisions = chart.divisions_per_standard_measure;
    chart.divisions_per_standard_measure = 0;
    compose_scene_cache_t cache;
    CHECK_FALSE(cache.hit_test_index_for(chart, song.revision));
    CHECK_FALSE(cache.scene.hit_test_index.has_value());
    chart.divisions_per_standard_measure = divisions;
    CHECK(cache.hit_test_index_for(chart, song.revision));
    CHECK(cache.scene.hit_test_index.build_count() == 1);
}

TEST_CASE("Compose measure geometry is shared with the committed snapshot",
          "[sys][timing-projection][chart-indices]") {
    auto history = document_history_t::from(compose_test_song());
    auto workspace = workspace_controller_t::from(history).value.value();
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{1};
    compose_scene_cache_t cache;
    std::optional<editor_interaction_state_t> interaction;
    populate_measure_cache_frame(history, workspace, cache, interaction);
    const auto* indices = history.indices(chart_id_t{1});
    REQUIRE(indices);
    CHECK(cache.scene.cached_measure_index.get() == &indices->measures.value());
    history = document_history_t::from(compose_test_song());
    CHECK(cache.scene.cached_measure_index->total_rows == 960);
}

TEST_CASE("Area selection reaches bar lines only through a shown measure lane",
          "[frame-timing][measure-lane]") {
    std::array<wormedit::ui::compose_grid_line_view_t, 3> lines{{
        {.y_pixels = 100, .kind = editor_grid_line_kind_t::measure, .measure_number = 1, .measure_id = {10}},
        {.y_pixels = 300, .kind = editor_grid_line_kind_t::measure, .measure_number = 2, .measure_id = {11}},
        {.y_pixels = 400, .kind = editor_grid_line_kind_t::beat}}};
    wormedit::ui::compose_view_t view{};
    view.viewport_height_pixels = 600;
    view.grid_lines.assign(lines.begin(), lines.end());
    editor_area_geometry_snapshot_t hidden;
    compose_projection_detail::append_area_bar_line_geometry(hidden, view, false);
    CHECK(hidden.objects.empty());
    view.measure_lane.visible = true;
    editor_area_geometry_snapshot_t shown;
    compose_projection_detail::append_area_bar_line_geometry(shown, view, false);
    // Only measure 11's bar line: the chart's first is fixed and beat lines have none.
    REQUIRE(shown.objects.size() == 1);
    const auto& handle = shown.objects.front();
    CHECK(handle.object == editor_selection_object_t{editor_selection_object_kind_t::bar_line, object_id_t{11}});
    CHECK(handle.left_pixels == 0.0);
    CHECK(handle.right_pixels == wormedit::ui::compose_measure_rail_width);
    CHECK(handle.top_pixels < 300.0);
    CHECK(handle.bottom_pixels > 300.0);
}

TEST_CASE("Measure-lane drag inputs dispatch through the bar-line move gesture",
          "[frame-timing][measure-lane][gesture]") {
    auto song = compose_test_song();
    song.charts.front().measures.push_back({.id = measure_id_t{2}});
    song.charts.front().measures.push_back({.id = measure_id_t{3}});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.project.compose_view.measure_lane_visible = true;
    fixture.project();
    REQUIRE(dispatch_editor_action(
        fixture.history, *fixture.interaction,
        select_editor_objects_action_t{{{
            editor_selection_object_kind_t::bar_line, object_id_t{2}}}}));
    fixture.project();

    notification_center_t center;
    notification_sink_t sink{center};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto dispatch = [&](WormUiComposeInputKind kind, std::uint64_t row) {
        wormedit::ui::workspace_action_t action{};
        action.stamp.project_id.value = fixture.history.document.id.value;
        action.stamp.chart_id.value = 1;
        action.stamp.document_revision = fixture.history.document.revision;
        action.input.compose_inputs.resize(1);
        const auto& view = fixture.flattened.view.compose;
        const auto downscroll = fixture.cache.frame.preview_scene_request.scroll_direction ==
            editor_field_scroll_direction_t::down;
        action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
        action.input.compose_inputs[0] = {
            .kind = kind,
            .object_id = {2},
            .x_pixels = 20,
            .y_pixels = compose_project_y(
                static_cast<double>(row), view.pixels_per_standard_measure / 960.0,
                fixture.interaction->viewport_scroll_pixels, view.viewport_height_pixels,
                downscroll)};
        action.input.compose_input_viewport_width_pixels = view.viewport_width_pixels;
        return dispatch_compose_input_action(
            action, fixture.history, *fixture.binding.value, *fixture.interaction,
            viewport, navigation, fixture.application, fixture.cache, 0, &sink);
    };

    (void)dispatch(WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_BEGIN, 960);
    REQUIRE(fixture.interaction->gesture<pending_bar_line_move_gesture_t>() != nullptr);
    (void)dispatch(WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_UPDATE, 1200);
    CHECK(fixture.interaction->gesture<pending_bar_line_move_gesture_t>()->preview_rows == 240);
    (void)dispatch(WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE, 1200);
    CHECK_FALSE(fixture.interaction->gesture<pending_bar_line_move_gesture_t>());
    const auto index = measure_index_t::from(fixture.history.document.charts.front()).value();
    CHECK(index.entries[1].absolute_begin_row == 1200);
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Pasted timing ghosts retain all eight lane identities",
          "[sys][clipboard][paste-preview]") {
    using lane_case = std::pair<chart_clipboard_payload_t, wormedit::timing_lane_kind_t>;
    const local_position_t position{measure_id_t{1}, 0};
    const std::array cases{
        lane_case{bpm_event_t{.position = position, .beats_per_minute = {150, 1}}, wormedit::timing_lane_kind_t::bpm},
        lane_case{scroll_event_t{.position = position, .multiplier = {2, 1}}, wormedit::timing_lane_kind_t::scroll},
        lane_case{speed_event_t{.position = position, .multiplier = {2, 1}}, wormedit::timing_lane_kind_t::speed},
        lane_case{pause_event_t{.position = position, .kind = pause_kind_t::stop, .duration = {1, 4}}, wormedit::timing_lane_kind_t::stop},
        lane_case{pause_event_t{.position = position, .kind = pause_kind_t::delay, .duration = {1, 4}}, wormedit::timing_lane_kind_t::delay},
        lane_case{warp_event_t{.span = {position, {measure_id_t{1}, 60}}}, wormedit::timing_lane_kind_t::warp},
        lane_case{kiai_event_t{.position = position, .enabled = true}, wormedit::timing_lane_kind_t::kiai},
        lane_case{timing_anchor_t{.position = position}, wormedit::timing_lane_kind_t::anchor}};
    for (const auto& [payload, lane] : cases) {
        CAPTURE(lane);
        compose_test_fixture_t fixture{paste_preview_song()};
        fixture.project();
        fixture.application.project.clipboard.chart_clipboard = chart_clipboard_t{
            .source_song = fixture.history.document.id, .source_chart = chart_id_t{1},
            .objects = {{.offset = {}, .duration = std::holds_alternative<warp_event_t>(payload)
                ? rational_t{1, 16} : rational_t{}, .payload = payload}}};
        fixture.interaction->cursor.row = 240;
        std::string status;
        REQUIRE(begin_compose_paste(fixture.application, fixture.history, *fixture.interaction, status));
        fixture.project();
        const auto& view = fixture.flattened.view.compose;
        REQUIRE(view.paste_ghosts.size() == 1);
        CHECK(view.paste_ghosts[0].kind == ui::compose_paste_ghost_kind_t::timing_object);
        CHECK(view.paste_ghosts[0].timing_lane == lane);
    }
}

TEST_CASE("Oversized owned Compose input batches cancel the active gesture",
          "[sys][compose][input-admission]") {
    const bool viewport_batch = GENERATE(false, true);
    auto song = compose_test_song();
    song.charts.front().measures.front().notes.clear();
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto begun = dispatch_editor_action(fixture.history, *fixture.interaction,
        begin_lane_key_hold_gesture_action_t{lane_id_t{1}});
    REQUIRE(begun.accepted);
    REQUIRE(fixture.interaction->is_pending());
    auto action = cursor_placement_action(fixture);
    if (viewport_batch)
        action.input.compose_viewport_inputs.resize(ui::compose_viewport_input_admission_limit + 1);
    else
        action.input.compose_inputs.resize(ui::compose_input_admission_limit + 1);
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto revision = fixture.history.document.revision;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(status.find("exceeded its capacity") != std::string::npos);
    CHECK_FALSE(fixture.interaction->is_pending());
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}
