#include "../support/stale_action_assertions.h"
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/lane_profiles.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <limits>
#include <ranges>
#include <variant>
#include <tuple>

namespace {

auto lane(wormedit::lane_id_t id, std::uint32_t order, wormedit::stage_group_t stage)
    -> wormedit::lane_definition_t {
    return {.id = id,
            .name = "lane " + std::to_string(id.value),
            .role = wormedit::lane_role_t::playable,
            .stage = stage,
            .display_order = order,
            .glyph = wormedit::lane_glyph_t::bar};
}

auto scene_chart() -> wormedit::chart_document_t {
    wormedit::chart_document_t chart;
    chart.id = wormedit::chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {
        {.id = wormedit::measure_id_t{1},
         .definition = {{1, 1}, wormedit::meter_t{4, 4}},
         .notes = {{.id = wormedit::object_id_t{10},
                    .position = {wormedit::measure_id_t{1}, 240},
                    .lane = wormedit::lane_id_t{1}},
                   {.id = wormedit::object_id_t{11},
                    .position = {wormedit::measure_id_t{1}, 720},
                    .lane = wormedit::lane_id_t{2},
                    .kind = wormedit::note_kind_t::hold,
                    .end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
                    .sound = wormedit::resource_id_t{40}}},
         .bgm_events = {{.id = wormedit::object_id_t{12},
                         .position = {wormedit::measure_id_t{1}, 480},
                         .source_lane = wormedit::object_id_t{30},
                         .sound = wormedit::resource_id_t{40}}}},
        {.id = wormedit::measure_id_t{2}, .definition = {{1, 1}, wormedit::meter_t{4, 4}}},
    };
    chart.bgm_source_lanes = {{.id = wormedit::object_id_t{30}, .name = "BGM 1"}};
    chart.lane_layouts = {
        {.id = wormedit::object_id_t{20},
         .position = {wormedit::measure_id_t{1}, 0},
         .layout = {.profile_id = "first",
                    .display_name = "First",
                    .lanes = {lane(wormedit::lane_id_t{1}, 0, wormedit::stage_group_t::left),
                              lane(wormedit::lane_id_t{2}, 1, wormedit::stage_group_t::right)},
                    .split_stage = true,
                    .split_after_column = 1}},
        {.id = wormedit::object_id_t{21},
         .position = {wormedit::measure_id_t{2}, 0},
         .layout = {.profile_id = "second",
                    .display_name = "Second",
                    .lanes = {lane(wormedit::lane_id_t{1}, 1, wormedit::stage_group_t::right),
                              lane(wormedit::lane_id_t{2}, 0, wormedit::stage_group_t::left)},
                    .split_stage = true,
                    .split_after_column = 1}},
    };
    chart.lane_layouts[0].layout.lanes[1].glyph = wormedit::lane_glyph_t::arrow_right;
    chart.lane_layouts[1].layout.lanes[1].glyph = wormedit::lane_glyph_t::arrow_right;
    return chart;
}

auto view(std::uint64_t revision = 7) -> wormedit::editor_interaction_view_model_t {
    return {.chart = wormedit::chart_id_t{1},
            .active_lane = wormedit::lane_id_t{1},
            .active_lane_name = "lane 1",
            .cursor = {wormedit::measure_id_t{2}, 0},
            .active_snap_denominator = 4,
            .supported_snap_denominators = {4},
            .document_revision = revision,
            .pixels_per_standard_measure = 192.0,
            .viewport_scroll_pixels = 96.0,
            .viewport_height_pixels = 192.0,
            .selected_notes = {wormedit::object_id_t{11}},
            .selected_objects = {
                {wormedit::editor_selection_object_kind_t::note, wormedit::object_id_t{11}}}};
}

auto has_code(const wormedit::diagnostic_list_t& diagnostics, wormedit::diagnostic_code_t code)
    -> bool {
    return std::ranges::any_of(diagnostics, [code](const auto& item) { return item.code == code; });
}

} // namespace

namespace {
auto make_stale_scene_preview(int kind) -> wormedit::editor_interaction_view_model_t {
    using namespace wormedit;
    auto result = view();
    switch (kind) {
    case 0:
        result.pending_note_duration_gesture = pending_note_duration_gesture_t{
            .reference_note = object_id_t{11}, .stamp = wormedit::document_stamp_t::from(8), .notes = {object_id_t{11}},
            .preview_end = {measure_id_t{2}, 0},
            .preview = {{object_id_t{11}, note_kind_t::hold, local_position_t{measure_id_t{2}, 0}}}};
        break;
    case 1:
        result.pending_hold = pending_hold_t{.position = {measure_id_t{2}, 0}, .lane = lane_id_t{1},
            .stamp = wormedit::document_stamp_t::from(8), .preview_end = local_position_t{measure_id_t{2}, 240}};
        break;
    case 2:
        result.pending_virtual_hold = editor_interaction_view_model_t::virtual_hold_preview_t{
            .anchor_row = 0, .end_row = 240, .head_row = 0, .tail_row = 240,
            .lane = lane_id_t{1}, .document_revision = 8, .adds_note = true};
        break;
    case 3:
        result.pending_note_move_gesture = pending_note_move_gesture_t{
            .reference_note = object_id_t{11}, .stamp = wormedit::document_stamp_t::from(8), .notes = {object_id_t{11}},
            .preview_position = {measure_id_t{2}, 240},
            .preview = {{object_id_t{11}, lane_id_t{2}, local_position_t{measure_id_t{2}, 240},
                         local_position_t{measure_id_t{2}, 720}}}};
        break;
    case 4:
        result.pending_note_paint_gesture = note_paint_gesture_preview_t{
            .stamp = wormedit::document_stamp_t::from(8), .targets = std::make_shared<const std::vector<note_paint_target_t>>(
                std::vector<note_paint_target_t>{{lane_id_t{1}, {measure_id_t{1}, 720}}})};
        break;
    }
    return result;
}
}

TEST_CASE("Field scene index binding rejects captured chart and view revision expiry",
          "[unit][core][editor-scene][index-stamp]") {
    const auto different_chart = GENERATE(false, true);
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto index = std::get<wormedit::editor_field_scene_index_t>(built);
    auto projected = view();
    if (different_chart) index.chart = wormedit::chart_id_t{99};
    else ++projected.document_revision;
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, index, projected, {.field_left_pixels = 10, .field_width_pixels = 200});
    REQUIRE_FALSE(result);
    wormedit::test::check_stale_action_message(result.diagnostics, "Field scene index");
}

TEST_CASE("Field scene preview expiry keeps hold duration move and paint guards",
          "[unit][core][editor-scene][captured-preview-stamp]") {
    const auto kind = GENERATE(0, 1, 2, 3, 4);
    auto chart = scene_chart();
    chart.source_family = wormedit::format_family_t::bms;
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built),
        make_stale_scene_preview(kind),
        {.field_left_pixels = 10, .field_width_pixels = 200, .include_virtual_bms_tail = true});
    REQUIRE_FALSE(result);
    wormedit::test::check_stale_action_message(result.diagnostics, "Field scene preview");
}

TEST_CASE("field scene composes split layout bands, exact grid, holds, selection, and cursor",
          "[unit][core][editor-scene]") {
    const auto chart = scene_chart();
    const auto built = wormedit::editor_field_scene_index_t::from(chart, 7);
    REQUIRE(built);
    const auto& indexed = *built;
    REQUIRE(indexed.notes.size() == 2);
    CHECK_FALSE(indexed.notes[0].sound);
    CHECK(indexed.notes[1].sound == wormedit::resource_id_t{40});
    REQUIRE(indexed.bgm_events.size() == 1);
    CHECK(indexed.bgm_events.front().event == wormedit::object_id_t{12});
    CHECK(indexed.bgm_events.front().source_lane == wormedit::object_id_t{30});
    CHECK(indexed.bgm_events.front().row == 480);
    const auto result = wormedit::editor_field_scene_view_result_t::from(
        wormedit::editor_field_scene_view_result_t::from(
            chart, indexed, view(),
            {.field_left_pixels = 10.0,
             .field_width_pixels = 200.0,
             .split_stage_gutter_pixels = 12.0,
             .comparison_difference_rows = {{wormedit::measure_id_t{2}, 0}}}));
    REQUIRE(result);
    const auto& scene = *result.scene;
    CHECK(scene.visible_begin_row == 480);
    CHECK(scene.visible_end_row == 1'440);
    REQUIRE(scene.layout_bands.size() == 2);
    CHECK(scene.layout_bands[0].profile_id == "first");
    CHECK(scene.layout_bands[1].profile_id == "second");
    CHECK(scene.lanes.size() == 4);
    REQUIRE(scene.comparison_differences.size() == 1);
    CHECK(scene.comparison_differences[0].bounds.left == 10.0);
    CHECK(scene.comparison_differences[0].bounds.right == 210.0);
    CHECK(scene.comparison_differences[0].bounds.top == Catch::Approx(91.0));
    CHECK(scene.comparison_differences[0].bounds.bottom == Catch::Approx(101.0));

    const auto first_lane_two = std::ranges::find_if(scene.lanes, [](const auto& geometry) {
        return geometry.layout_band == 0 && geometry.lane == wormedit::lane_id_t{2};
    });
    const auto second_lane_two = std::ranges::find_if(scene.lanes, [](const auto& geometry) {
        return geometry.layout_band == 1 && geometry.lane == wormedit::lane_id_t{2};
    });
    REQUIRE(first_lane_two != scene.lanes.end());
    REQUIRE(second_lane_two != scene.lanes.end());
    CHECK(first_lane_two->bounds.left == 116.0);
    CHECK(first_lane_two->bounds.right == 210.0);
    CHECK(second_lane_two->bounds.left == 10.0);
    CHECK(second_lane_two->bounds.right == 104.0);

    REQUIRE(scene.hold_bodies.size() == 2);
    CHECK(std::ranges::all_of(scene.hold_bodies, [](const auto& body) { return body.selected; }));
    CHECK(scene.hold_bodies[0].bounds.top == Catch::Approx(48.0));
    CHECK(scene.hold_bodies[0].bounds.bottom == Catch::Approx(96.0));
    CHECK(scene.hold_bodies[1].bounds.top == Catch::Approx(96.0));
    CHECK(scene.hold_bodies[1].bounds.bottom == Catch::Approx(144.0));
    REQUIRE(scene.note_endpoints.size() == 2);
    CHECK(std::ranges::all_of(scene.note_endpoints, [](const auto& endpoint) {
        return endpoint.sound == wormedit::resource_id_t{40};
    }));
    CHECK(std::ranges::all_of(scene.note_endpoints,
                              [](const auto& endpoint) { return endpoint.selected; }));
    CHECK(std::ranges::all_of(scene.note_endpoints, [](const auto& endpoint) {
        return endpoint.glyph == wormedit::lane_glyph_t::arrow_right;
    }));
    CHECK_FALSE(scene.note_endpoints[0].tail);
    CHECK(scene.note_endpoints[1].tail);

    REQUIRE(scene.cursor.has_value());
    CHECK(scene.cursor->lane == wormedit::lane_id_t{1});
    CHECK(scene.cursor->bounds.left == 116.0);
    CHECK(scene.cursor->bounds.top == Catch::Approx(95.0));
    CHECK(std::ranges::any_of(scene.grid_lines, [](const auto& line) {
        return line.kind == wormedit::editor_grid_line_kind_t::measure &&
               std::abs(line.y_pixels - 96.0) < 0.000001;
    }));

    auto zoomed_view = view();
    zoomed_view.pixels_per_standard_measure = 384.0;
    zoomed_view.viewport_scroll_pixels = 192.0;
    zoomed_view.viewport_height_pixels = 384.0;
    const auto zoomed =
        wormedit::editor_field_scene_view_t::from(chart, *built, zoomed_view,
                                                  {.field_left_pixels = 10.0,
                                                   .field_width_pixels = 200.0,
                                                   .split_stage_gutter_pixels = 12.0});
    REQUIRE(zoomed);
    REQUIRE(zoomed.scene->hold_bodies.size() == 2);
    CHECK(zoomed.scene->visible_begin_row == scene.visible_begin_row);
    CHECK(zoomed.scene->visible_end_row == scene.visible_end_row);
    CHECK(zoomed.scene->hold_bodies[0].bounds.top == Catch::Approx(96.0));
    CHECK(zoomed.scene->hold_bodies[0].bounds.bottom == Catch::Approx(192.0));
}

TEST_CASE("field scene leaves a blank margin before chart row zero",
          "[unit][core][editor-scene][scroll]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto visible = view();
    visible.cursor = {wormedit::measure_id_t{1}, 0};
    visible.viewport_scroll_pixels = -visible.viewport_height_pixels * 0.2;
    const auto scene = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), visible,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(scene);
    CHECK(scene.scene->visible_begin_row == 0);
    CHECK(std::ranges::any_of(scene.scene->grid_lines, [](const auto& line) {
        return line.kind == wormedit::editor_grid_line_kind_t::measure &&
               line.y_pixels == Catch::Approx(38.4);
    }));
    CHECK(std::ranges::none_of(scene.scene->grid_lines,
                               [](const auto& line) { return line.y_pixels < 38.4 - 0.001; }));
}

TEST_CASE("field cursor stays at its screen anchor when scroll reaches the first measure",
          "[unit][core][editor-scene][scroll][cursor]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto visible = view();
    visible.cursor = {wormedit::measure_id_t{1}, 0};
    visible.viewport_scroll_pixels = -visible.viewport_height_pixels * 0.2;
    for (const auto direction : {wormedit::editor_field_scroll_direction_t::up,
                                 wormedit::editor_field_scroll_direction_t::down}) {
        const auto result = wormedit::editor_field_scene_view_t::from(
            chart, std::get<wormedit::editor_field_scene_index_t>(built), visible,
            {.field_left_pixels = 10.0,
             .field_width_pixels = 200.0,
             .scroll_direction = direction,
             .fixed_cursor_screen_y_pixels = visible.viewport_height_pixels * 0.5});
        REQUIRE(result);
        REQUIRE(result.scene->cursor);
        const auto expected_y = visible.viewport_height_pixels * 0.5;
        CHECK(result.scene->cursor->bounds.top == Catch::Approx(expected_y - 1.0));
    }
}

TEST_CASE("field scene scroll direction anchors rectangular heads and keeps arrows centered",
          "[unit][core][editor-scene][scroll-direction]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& index = std::get<wormedit::editor_field_scene_index_t>(built);
    auto visible = view();
    visible.viewport_scroll_pixels = 0.0;
    const auto compose = [&](wormedit::editor_field_scroll_direction_t direction) {
        return wormedit::editor_field_scene_view_t::from(chart, index, visible,
                                                         {.field_left_pixels = 10.0,
                                                          .field_width_pixels = 200.0,
                                                          .note_head_height_pixels = 16.0,
                                                          .scroll_direction = direction,
                                                          .fixed_cursor_screen_y_pixels = 72.0,
                                                          .bgm_pane_left_pixels = 210.0,
                                                          .bgm_pane_width_pixels = 36.0});
    };
    const auto up = compose(wormedit::editor_field_scroll_direction_t::up);
    const auto down = compose(wormedit::editor_field_scroll_direction_t::down);
    REQUIRE(up);
    REQUIRE(down);
    const auto note = [](const auto& scene, wormedit::object_id_t id) {
        return std::ranges::find_if(scene.note_endpoints,
                                    [=](const auto& endpoint) { return endpoint.note == id; });
    };
    const auto up_bar = note(*up.scene, wormedit::object_id_t{10});
    const auto down_bar = note(*down.scene, wormedit::object_id_t{10});
    const auto up_arrow = note(*up.scene, wormedit::object_id_t{11});
    const auto down_arrow = note(*down.scene, wormedit::object_id_t{11});
    REQUIRE(up_bar != up.scene->note_endpoints.end());
    REQUIRE(down_bar != down.scene->note_endpoints.end());
    REQUIRE(up_arrow != up.scene->note_endpoints.end());
    REQUIRE(down_arrow != down.scene->note_endpoints.end());
    CHECK(up_bar->bounds.top == Catch::Approx(48.0));
    CHECK(up_bar->bounds.bottom == Catch::Approx(64.0));
    CHECK(down_bar->bounds.top == Catch::Approx(128.0));
    CHECK(down_bar->bounds.bottom == Catch::Approx(144.0));
    CHECK(up_arrow->bounds.top == Catch::Approx(136.0));
    CHECK(up_arrow->bounds.bottom == Catch::Approx(152.0));
    CHECK(down_arrow->bounds.top == Catch::Approx(40.0));
    CHECK(down_arrow->bounds.bottom == Catch::Approx(56.0));
    REQUIRE(up.scene->bgm_events.size() == 1);
    REQUIRE(down.scene->bgm_events.size() == 1);
    REQUIRE(up.scene->cursor.has_value());
    REQUIRE(down.scene->cursor.has_value());
    CHECK(up.scene->cursor->bounds.top == Catch::Approx(71.0));
    CHECK(down.scene->cursor->bounds.top == Catch::Approx(71.0));
    CHECK(up.scene->bgm_events.front().bounds.top == Catch::Approx(96.0));
    CHECK(down.scene->bgm_events.front().bounds.bottom == Catch::Approx(96.0));
    CHECK(std::ranges::all_of(down.scene->lanes, [](const auto& lane) {
        return lane.bounds.top <= lane.bounds.bottom;
    }));
    CHECK(std::ranges::all_of(down.scene->hold_bodies, [](const auto& hold) {
        return hold.bounds.top <= hold.bounds.bottom;
    }));
}

TEST_CASE("field scene can project the last sparse BMS measure with its active lanes",
          "[unit][core][editor-scene][virtual-canvas]") {
    auto chart = scene_chart();
    chart.source_family = wormedit::format_family_t::bms;
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& indexed = std::get<wormedit::editor_field_scene_index_t>(built);
    auto visible = view();
    visible.viewport_scroll_pixels = 999.0 * visible.pixels_per_standard_measure;
    visible.virtual_bms_cursor_row = 959'040;
    const wormedit::editor_field_scene_request_t request{
        .field_left_pixels = 10.0, .field_width_pixels = 200.0, .include_virtual_bms_tail = true};
    const auto projected =
        wormedit::editor_field_scene_view_t::from(chart, indexed, visible, request);
    REQUIRE(projected);
    const auto& scene = *projected.scene;
    CHECK(scene.visible_begin_row == 959'040);
    CHECK(scene.visible_end_row == 960'000);
    REQUIRE(scene.layout_bands.size() == 1);
    CHECK(scene.layout_bands.front().profile_id == "second");
    CHECK(scene.layout_bands.front().end_row == 960'000);
    CHECK(scene.lanes.size() == 2);
    REQUIRE(scene.cursor);
    CHECK(scene.cursor->bounds.top == Catch::Approx(-1.0));
    CHECK(std::ranges::any_of(scene.grid_lines, [](const auto& line) {
        return line.kind == wormedit::editor_grid_line_kind_t::measure &&
               line.chart_row.whole_rows == 959'040;
    }));
    CHECK(chart.measures.size() == 2);
    CHECK(indexed.measures.entries.size() == 2);
    visible.pending_virtual_hold =
        wormedit::editor_interaction_view_model_t::virtual_hold_preview_t{
            .anchor_row = 959'040,
            .end_row = 959'280,
            .head_row = 959'040,
            .tail_row = 959'280,
            .lane = wormedit::lane_id_t{1},
            .document_revision = 7,
            .removals = std::make_shared<const std::vector<wormedit::object_id_t>>(),
            .adds_note = true};
    const auto with_hold =
        wormedit::editor_field_scene_view_t::from(chart, indexed, visible, request);
    REQUIRE(with_hold);
    CHECK(std::ranges::any_of(with_hold.scene->hold_bodies, [](const auto& body) {
        return body.proposal_ghost && body.bounds.top == Catch::Approx(0.0) &&
               body.bounds.bottom == Catch::Approx(48.0);
    }));
    auto authored_only = request;
    authored_only.include_virtual_bms_tail = false;
    CHECK_FALSE(wormedit::editor_field_scene_view_t::from(chart, indexed, visible, authored_only));
}

TEST_CASE("virtual BMS hold preview draws its normalized union and subdued origins",
          "[unit][core][editor-scene][virtual-canvas][hold-preview]") {
    auto chart = scene_chart();
    chart.source_family = wormedit::format_family_t::bms;
    chart.measures[1].notes.push_back({.id = wormedit::object_id_t{200},
                                       .position = {wormedit::measure_id_t{2}, 600},
                                       .lane = wormedit::lane_id_t{1},
                                       .kind = wormedit::note_kind_t::hold,
                                       .end = wormedit::local_position_t{
                                           wormedit::measure_id_t{2}, 900}});
    chart.measures[1].notes.push_back({.id = wormedit::object_id_t{201},
                                       .position = {wormedit::measure_id_t{2}, 480},
                                       .lane = wormedit::lane_id_t{1},
                                       .kind = wormedit::note_kind_t::hold,
                                       .end = wormedit::local_position_t{
                                           wormedit::measure_id_t{2}, 600}});
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto visible = view();
    visible.viewport_scroll_pixels = 288.0;
    visible.virtual_bms_cursor_row = 1'920;
    visible.pending_virtual_hold =
        wormedit::editor_interaction_view_model_t::virtual_hold_preview_t{
            .anchor_row = 1'920,
            .end_row = 1'680,
            .head_row = 1'440,
            .tail_row = 1'920,
            .lane = wormedit::lane_id_t{1},
            .document_revision = 7,
            .removals = std::make_shared<const std::vector<wormedit::object_id_t>>(
                std::vector<wormedit::object_id_t>{wormedit::object_id_t{200},
                                                   wormedit::object_id_t{201}}),
            .adds_note = true};
    const auto projected = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), visible,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0, .include_virtual_bms_tail = true});
    REQUIRE(projected);
    CHECK(std::ranges::any_of(projected.scene->hold_bodies, [](const auto& body) {
        return body.proposal_ghost && body.bounds.top == Catch::Approx(0.0) &&
               body.bounds.bottom == Catch::Approx(96.0);
    }));
    CHECK(std::ranges::any_of(projected.scene->note_endpoints, [](const auto& endpoint) {
        return endpoint.note == wormedit::object_id_t{201} && endpoint.proposal_origin;
    }));
}

TEST_CASE("keyboard hold previews cross from authored rows into virtual BMS measures",
          "[unit][core][editor-scene][virtual-canvas][hold-preview]") {
    auto chart = scene_chart();
    chart.source_family = wormedit::format_family_t::bms;
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto visible = view();
    visible.viewport_scroll_pixels = 288.0;
    visible.pending_hold = wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{2}, 720},
        .lane = wormedit::lane_id_t{1},
        .stamp = wormedit::document_stamp_t::from(7),
        .preview_canvas_row = 2'160,
        .gesture_source = wormedit::hold_gesture_source_t::keyboard_lane};
    for (const auto lane_id : {wormedit::lane_id_t{1}, wormedit::lane_id_t{2}}) {
        visible.pending_canvas_keyboard_holds.push_back({
            .anchor_row = 1'680,
            .end_row = 2'160,
            .head_row = 1'680,
            .tail_row = 2'160,
            .lane = lane_id,
            .document_revision = 7,
            .removals = std::make_shared<const std::vector<wormedit::object_id_t>>(),
            .adds_note = true});
    }
    const auto projected = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), visible,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0, .include_virtual_bms_tail = true});
    REQUIRE(projected);
    CHECK(std::ranges::count_if(projected.scene->hold_bodies, [](const auto& body) {
              return body.proposal_ghost;
          }) == 2);
}

TEST_CASE("field scene resolves semantic layer colors by visible palette order",
          "[unit][core][editor-scene][semantic-layer]") {
    auto chart = scene_chart();
    chart.semantic_layers = {
        {.id = wormedit::layer_id_t{1},
         .name = "Hidden",
         .color = {11, 12, 13, 255},
         .visible = false},
        {.id = wormedit::layer_id_t{2}, .name = "Rhythm", .color = {31, 32, 33, 210}},
        {.id = wormedit::layer_id_t{3}, .name = "Vocal", .color = {51, 52, 53, 220}},
    };
    chart.measures.front().notes[1].semantic_layers = {
        wormedit::layer_id_t{1}, wormedit::layer_id_t{2}, wormedit::layer_id_t{3}};

    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(result);
    REQUIRE(result.scene->note_endpoints.size() == 2);
    REQUIRE(result.scene->hold_bodies.size() == 2);
    for (const auto& endpoint : result.scene->note_endpoints) {
        REQUIRE(endpoint.semantic_color.has_value());
        CHECK(*endpoint.semantic_color == wormedit::color_rgba_t{31, 32, 33, 210});
    }
    for (const auto& body : result.scene->hold_bodies) {
        REQUIRE(body.semantic_color.has_value());
        CHECK(*body.semantic_color == wormedit::color_rgba_t{31, 32, 33, 210});
    }

    const auto solo = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .solo_semantic_layer = wormedit::layer_id_t{2}});
    REQUIRE(solo);
    CHECK(std::ranges::all_of(solo.scene->note_endpoints, [](const auto& endpoint) {
        return endpoint.note == wormedit::object_id_t{11};
    }));
    CHECK(std::ranges::all_of(solo.scene->hold_bodies, [](const auto& body) {
        return body.note == wormedit::object_id_t{11};
    }));

    const auto unknown_solo = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .solo_semantic_layer = wormedit::layer_id_t{99}});
    CHECK_FALSE(unknown_solo);
    CHECK(has_code(unknown_solo.diagnostics, wormedit::diagnostic_code_t::invalid_id));

    auto filter_view = view();
    filter_view.viewport_scroll_pixels = 0.0;
    filter_view.viewport_height_pixels = 384.0;
    const auto required = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), filter_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .required_semantic_layers = {wormedit::layer_id_t{2}}});
    REQUIRE(required);
    REQUIRE(required.scene->note_endpoints.size() == 2);
    CHECK(std::ranges::all_of(required.scene->note_endpoints, [](const auto& endpoint) {
        return endpoint.note == wormedit::object_id_t{11};
    }));
    CHECK(std::ranges::all_of(required.scene->hold_bodies, [](const auto& body) {
        return body.note == wormedit::object_id_t{11};
    }));

    const auto excluded = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), filter_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .excluded_semantic_layers = {wormedit::layer_id_t{1}}});
    REQUIRE(excluded);
    REQUIRE(excluded.scene->note_endpoints.size() == 1);
    CHECK(excluded.scene->note_endpoints.front().note == wormedit::object_id_t{10});
    CHECK(excluded.scene->hold_bodies.empty());

    const auto filtered = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .required_semantic_layers = {wormedit::layer_id_t{2}, wormedit::layer_id_t{3}},
         .excluded_semantic_layers = {wormedit::layer_id_t{1}}});
    REQUIRE(filtered);
    CHECK(filtered.scene->note_endpoints.empty());
    CHECK(filtered.scene->hold_bodies.empty());

    const auto contradictory = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .required_semantic_layers = {wormedit::layer_id_t{2}},
         .excluded_semantic_layers = {wormedit::layer_id_t{2}}});
    CHECK_FALSE(contradictory);
    CHECK(has_code(contradictory.diagnostics, wormedit::diagnostic_code_t::invalid_id));

    auto move_view = view();
    move_view.pending_note_move_gesture = wormedit::pending_note_move_gesture_t{
        .reference_note = wormedit::object_id_t{11},
        .stamp = wormedit::document_stamp_t::from(7),
        .notes = {wormedit::object_id_t{11}},
        .preview_position = {wormedit::measure_id_t{2}, 240},
        .preview = {{wormedit::object_id_t{11}, wormedit::lane_id_t{2},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 720}}},
    };
    const auto moved = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), move_view,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(moved);
    const auto ghost = std::ranges::find_if(moved.scene->note_endpoints, [](const auto& endpoint) {
        return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_ghost;
    });
    REQUIRE(ghost != moved.scene->note_endpoints.end());
    REQUIRE(ghost->semantic_color.has_value());
    CHECK(*ghost->semantic_color == wormedit::color_rgba_t{31, 32, 33, 210});

    chart.semantic_layers[1].visible = false;
    const auto secondary_index = wormedit::build_editor_field_scene_index(chart, 8);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(secondary_index));
    const auto secondary_view = view(8);
    const auto secondary = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(secondary_index), secondary_view,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(secondary);
    REQUIRE(secondary.scene->note_endpoints.front().semantic_color.has_value());
    CHECK(*secondary.scene->note_endpoints.front().semantic_color ==
          wormedit::color_rgba_t{51, 52, 53, 220});
}

TEST_CASE("field scene composes an independently scrolled BGM pane",
          "[unit][core][editor-scene][bgm]") {
    auto chart = scene_chart();
    chart.bgm_source_lanes = {
        {.id = wormedit::object_id_t{31}, .name = "first", .source_order = 7},
        {.id = wormedit::object_id_t{30}, .name = "second", .source_order = 3},
        {.id = wormedit::object_id_t{32}, .name = "third", .source_order = 9},
    };
    chart.measures[0].bgm_events = {
        {.id = wormedit::object_id_t{12},
         .position = {wormedit::measure_id_t{1}, 480},
         .source_lane = wormedit::object_id_t{30},
         .sound = wormedit::resource_id_t{40}},
        {.id = wormedit::object_id_t{13},
         .position = {wormedit::measure_id_t{1}, 0},
         .source_lane = wormedit::object_id_t{31}},
    };
    chart.measures[1].bgm_events = {
        {.id = wormedit::object_id_t{14},
         .position = {wormedit::measure_id_t{2}, 480},
         .source_lane = wormedit::object_id_t{32}},
    };
    auto scene_view = view();
    scene_view.selected_objects.push_back(
        {wormedit::editor_selection_object_kind_t::bgm_event, wormedit::object_id_t{12}});
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));

    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .bgm_pane_left_pixels = 220.0,
         .bgm_pane_width_pixels = 60.0,
         .bgm_lane_width_pixels = 40.0,
         .bgm_horizontal_scroll_pixels = 20.0});
    REQUIRE(result);
    REQUIRE(result.scene->bgm_lanes.size() == 2);
    CHECK(result.scene->bgm_lanes[0].source_lane == wormedit::object_id_t{31});
    CHECK(result.scene->bgm_lanes[0].source_order == 7);
    CHECK(result.scene->bgm_lanes[0].bounds.left == 220.0);
    CHECK(result.scene->bgm_lanes[0].bounds.right == 240.0);
    CHECK(result.scene->bgm_lanes[1].source_lane == wormedit::object_id_t{30});
    CHECK(result.scene->bgm_lanes[1].source_order == 3);
    CHECK(result.scene->bgm_lanes[1].bounds.left == 240.0);
    CHECK(result.scene->bgm_lanes[1].bounds.right == 280.0);
    REQUIRE(result.scene->bgm_events.size() == 1);
    CHECK(result.scene->bgm_events[0].event == wormedit::object_id_t{12});
    CHECK(result.scene->bgm_events[0].source_lane == wormedit::object_id_t{30});
    CHECK(result.scene->bgm_events[0].sound == wormedit::resource_id_t{40});
    CHECK(result.scene->bgm_events[0].selected);
    CHECK(result.scene->bgm_events[0].bounds.left == 240.0);
    CHECK(result.scene->bgm_events[0].bounds.right == 280.0);
    CHECK(result.scene->bgm_events[0].bounds.top == Catch::Approx(0.0));
    CHECK(result.scene->bgm_events[0].bounds.bottom == Catch::Approx(10.0));

    const auto disabled = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(disabled);
    CHECK(disabled.scene->bgm_lanes.empty());
    CHECK(disabled.scene->bgm_events.empty());

    const auto primitive_count =
        result.scene->layout_bands.size() + result.scene->lanes.size() +
        result.scene->comparison_differences.size() + result.scene->grid_lines.size() +
        result.scene->hold_bodies.size() + result.scene->note_endpoints.size() +
        result.scene->bgm_lanes.size() + result.scene->bgm_events.size() +
        static_cast<std::size_t>(result.scene->cursor.has_value());
    const auto capped = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .bgm_pane_left_pixels = 220.0,
         .bgm_pane_width_pixels = 60.0,
         .bgm_lane_width_pixels = 40.0,
         .bgm_horizontal_scroll_pixels = 20.0,
         .maximum_scene_primitives = primitive_count - 1});
    CHECK_FALSE(capped);
    CHECK_FALSE(capped.scene.has_value());

    const auto invalid_geometry = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .bgm_pane_left_pixels = 220.0,
         .bgm_pane_width_pixels = -1.0});
    CHECK_FALSE(invalid_geometry);
    CHECK_FALSE(invalid_geometry.scene.has_value());

    const auto excessive_scroll = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .bgm_pane_left_pixels = 220.0,
         .bgm_pane_width_pixels = 60.0,
         .bgm_lane_width_pixels = 40.0,
         .bgm_horizontal_scroll_pixels = 61.0});
    CHECK_FALSE(excessive_scroll);
    CHECK_FALSE(excessive_scroll.scene.has_value());
}

TEST_CASE("field scene composes selectable BGA events in a dedicated lane",
          "[unit][core][editor-scene][bga]") {
    auto chart = scene_chart();
    chart.bga_events = {
        {.id = wormedit::object_id_t{70},
         .position = {wormedit::measure_id_t{1}, 480},
         .resource = wormedit::resource_id_t{90}},
        {.id = wormedit::object_id_t{71},
         .position = {wormedit::measure_id_t{1}, 480},
         .resource = wormedit::resource_id_t{91},
         .kind = wormedit::visual_event_kind_t::video},
        {.id = wormedit::object_id_t{72},
         .position = {wormedit::measure_id_t{1}, 480},
         .resource = wormedit::resource_id_t{92},
         .kind = wormedit::visual_event_kind_t::layer},
    };
    auto scene_view = view();
    scene_view.selected_objects.push_back(
        {wormedit::editor_selection_object_kind_t::bga_event, wormedit::object_id_t{71}});
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto request = wormedit::editor_field_scene_request_t{
        .field_left_pixels = 10.0,
        .field_width_pixels = 200.0,
        .note_head_height_pixels = 10.0,
        .scroll_direction = wormedit::editor_field_scroll_direction_t::up,
        .bgm_pane_left_pixels = 220.0,
        .bgm_pane_width_pixels = 60.0,
        .bgm_lane_width_pixels = 40.0,
        .bga_pane_left_pixels = 290.0,
        .bga_pane_width_pixels = 48.0,
    };
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view, request);
    REQUIRE(result);
    REQUIRE(result.scene->bga_lane.has_value());
    CHECK(result.scene->bga_lane->bounds.left == 290.0);
    CHECK(result.scene->bga_lane->bounds.right == 338.0);
    REQUIRE(result.scene->bga_events.size() == 3);
    CHECK(result.scene->bga_events[0].event == wormedit::object_id_t{70});
    CHECK(result.scene->bga_events[0].resource == wormedit::resource_id_t{90});
    CHECK(result.scene->bga_events[0].bounds.left == 290.0);
    CHECK(result.scene->bga_events[0].bounds.right == 302.0);
    CHECK(result.scene->bga_events[0].bounds.top == Catch::Approx(0.0));
    CHECK(result.scene->bga_events[0].bounds.bottom == Catch::Approx(10.0));
    CHECK(result.scene->bga_events[1].event == wormedit::object_id_t{71});
    CHECK(result.scene->bga_events[1].kind == wormedit::visual_event_kind_t::video);
    CHECK(result.scene->bga_events[1].selected);
    CHECK(result.scene->bga_events[1].bounds.left == 302.0);
    CHECK(result.scene->bga_events[1].bounds.right == 314.0);

    auto downscroll_request = request;
    downscroll_request.scroll_direction = wormedit::editor_field_scroll_direction_t::down;
    const auto downscroll = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        downscroll_request);
    REQUIRE(downscroll);
    REQUIRE(downscroll.scene->bga_events.size() == 3);
    CHECK(downscroll.scene->bga_events[0].bounds.top >
          result.scene->bga_events[0].bounds.top);
    CHECK(downscroll.scene->bga_events[0].bounds.bottom >
          result.scene->bga_events[0].bounds.bottom);

    auto hidden_view = scene_view;
    hidden_view.bga_lane_visible = false;
    const auto hidden = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), hidden_view, request);
    REQUIRE(hidden);
    CHECK_FALSE(hidden.scene->bga_lane.has_value());
    CHECK(hidden.scene->bga_events.empty());
}

void check_lane_less_bga_result(const wormedit::editor_field_scene_view_t& scene);

TEST_CASE("BGA lane stays available on lane-less BMS virtual canvas",
          "[unit][core][editor-scene][bga][bms]") {
    auto chart = scene_chart();
    for (auto& measure : chart.measures) measure.notes.clear();
    chart.source_family = wormedit::format_family_t::bms;
    chart.lane_layouts.clear();
    chart.bga_events = {{.id = wormedit::object_id_t{80},
                         .position = {wormedit::measure_id_t{1}, 240},
                         .resource = wormedit::resource_id_t{90}}};
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto scene_view = view();
    scene_view.selected_notes.clear();
    scene_view.selected_objects.clear();
    scene_view.bga_lane_visible = true;
    scene_view.viewport_scroll_pixels = 0.0;
    scene_view.viewport_height_pixels = 240.0;
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 100.0,
         .bga_pane_left_pixels = 120.0,
         .bga_pane_width_pixels = 48.0,
         .include_virtual_bms_tail = true});
    for (const auto& diagnostic : result.diagnostics) WARN(diagnostic.message);
    REQUIRE(result);
    check_lane_less_bga_result(*result.scene);
}

void check_lane_less_bga_result(const wormedit::editor_field_scene_view_t& scene) {
    CHECK(scene.lanes.empty());
    REQUIRE(scene.bga_lane.has_value());
    REQUIRE(scene.bga_events.size() == 1);
    CHECK(scene.bga_events.front().event == wormedit::object_id_t{80});
}

void check_dense_bga_stack(const wormedit::editor_field_scene_view_t& scene) {
    REQUIRE(scene.bga_events.size() == 50);
    for (const auto& event : scene.bga_events) {
        CHECK(event.overlap_count == 50);
        CHECK(event.bounds.left == 210.0);
        CHECK(event.bounds.right == 258.0);
    }
}

TEST_CASE("dense BGA rows use one bounded overlap stack target", "[unit][core][editor-scene][bga]") {
    auto chart = scene_chart();
    for (std::uint64_t index = 1; index <= 50; ++index) {
        chart.bga_events.push_back({.id = wormedit::object_id_t{1000 + index},
            .position = {wormedit::measure_id_t{1}, 480},
            .resource = wormedit::resource_id_t{2000 + index}});
    }
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto request = wormedit::editor_field_scene_request_t{
        .field_width_pixels = 200.0, .note_head_height_pixels = 10.0,
        .scroll_direction = wormedit::editor_field_scroll_direction_t::up,
        .bga_pane_left_pixels = 210.0, .bga_pane_width_pixels = 48.0};
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(), request);
    REQUIRE(result);
    check_dense_bga_stack(*result.scene);
}

TEST_CASE("field scene rejects stale indexes and returns no partial geometry at hard caps",
          "[unit][core][editor-scene][bounds]") {
    const auto chart = scene_chart();
    const auto too_small = wormedit::build_editor_field_scene_index(
        chart, 7, {.maximum_notes = 1, .maximum_layout_segments = 2});
    REQUIRE(std::holds_alternative<wormedit::diagnostic_list_t>(too_small));
    CHECK(has_code(std::get<wormedit::diagnostic_list_t>(too_small),
                   wormedit::diagnostic_code_t::invalid_resolution));

    const auto no_bgm_capacity = wormedit::build_editor_field_scene_index(
        chart, 7, {.maximum_notes = 2, .maximum_bgm_events = 0, .maximum_layout_segments = 2});
    REQUIRE(std::holds_alternative<wormedit::diagnostic_list_t>(no_bgm_capacity));
    CHECK(has_code(std::get<wormedit::diagnostic_list_t>(no_bgm_capacity),
                   wormedit::diagnostic_code_t::invalid_resolution));

    auto duplicate_bgm = chart;
    duplicate_bgm.measures[1].bgm_events.push_back({.id = wormedit::object_id_t{12},
                                                    .position = {wormedit::measure_id_t{2}, 0},
                                                    .source_lane = wormedit::object_id_t{30}});
    const auto duplicate_bgm_index = wormedit::build_editor_field_scene_index(duplicate_bgm, 7);
    REQUIRE(std::holds_alternative<wormedit::diagnostic_list_t>(duplicate_bgm_index));
    CHECK(has_code(std::get<wormedit::diagnostic_list_t>(duplicate_bgm_index),
                   wormedit::diagnostic_code_t::duplicate_id));

    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& index = std::get<wormedit::editor_field_scene_index_t>(built);
    const auto stale = wormedit::editor_field_scene_view_t::from(
        chart, index, view(8), {.field_left_pixels = 0, .field_width_pixels = 200});
    CHECK_FALSE(stale);
    CHECK_FALSE(stale.scene.has_value());

    const auto capped = wormedit::editor_field_scene_view_t::from(
        chart, index, view(),
        {.field_left_pixels = 0, .field_width_pixels = 200, .maximum_scene_primitives = 1});
    CHECK_FALSE(capped);
    CHECK_FALSE(capped.scene.has_value());
    CHECK(has_code(capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));

    const auto work_capped = wormedit::editor_field_scene_view_t::from(
        chart, index, view(),
        {.field_left_pixels = 0, .field_width_pixels = 200, .maximum_composition_operations = 1});
    CHECK_FALSE(work_capped);
    CHECK_FALSE(work_capped.scene.has_value());

    const auto bad_comparison = wormedit::editor_field_scene_view_t::from(
        chart, index, view(),
        {.field_left_pixels = 0,
         .field_width_pixels = 200,
         .comparison_difference_rows = {{wormedit::measure_id_t{999}, 0}}});
    CHECK_FALSE(bad_comparison);
    CHECK_FALSE(bad_comparison.scene.has_value());

    const auto duplicate_comparison = wormedit::editor_field_scene_view_t::from(
        chart, index, view(),
        {.field_left_pixels = 0,
         .field_width_pixels = 200,
         .comparison_difference_rows = {{wormedit::measure_id_t{1}, 720},
                                        {wormedit::measure_id_t{1}, 720}}});
    CHECK_FALSE(duplicate_comparison);
    CHECK(has_code(duplicate_comparison.diagnostics, wormedit::diagnostic_code_t::collision));

    const auto comparison_capped = wormedit::editor_field_scene_view_t::from(
        chart, index, view(),
        {.field_left_pixels = 0,
         .field_width_pixels = 200,
         .comparison_difference_rows = {{wormedit::measure_id_t{1}, 720},
                                        {wormedit::measure_id_t{2}, 0}},
         .maximum_comparison_difference_rows = 1});
    CHECK_FALSE(comparison_capped);
    CHECK_FALSE(comparison_capped.scene.has_value());
}

TEST_CASE("field scene composes visible paint previews across measure and layout boundaries",
          "[unit][core][editor-scene][paint]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto scene_view = view();
    scene_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .stamp = wormedit::document_stamp_t::from(7),
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 720}},
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{2}, 240}},
            }),
    };

    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(result);
    REQUIRE(result.scene->note_endpoints.size() == 4);
    const auto first = std::ranges::find_if(result.scene->note_endpoints, [](const auto& endpoint) {
        return endpoint.paint_preview && endpoint.lane == wormedit::lane_id_t{1} &&
               endpoint.bounds.top == Catch::Approx(48.0);
    });
    const auto second =
        std::ranges::find_if(result.scene->note_endpoints, [](const auto& endpoint) {
            return endpoint.paint_preview && endpoint.lane == wormedit::lane_id_t{1} &&
                   endpoint.bounds.top == Catch::Approx(144.0);
        });
    REQUIRE(first != result.scene->note_endpoints.end());
    REQUIRE(second != result.scene->note_endpoints.end());
    CHECK(first->note == wormedit::object_id_t{});
    CHECK(second->note == wormedit::object_id_t{});
    CHECK_FALSE(first->paint_erase_preview);
    CHECK_FALSE(second->paint_erase_preview);
    CHECK(first->bounds.left == Catch::Approx(10.0));
    CHECK(second->bounds.left == Catch::Approx(116.0));

    auto stale_view = scene_view;
    stale_view.pending_note_paint_gesture->stamp = wormedit::document_stamp_t::from(8);
    const auto stale = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), stale_view,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    CHECK_FALSE(stale);
    CHECK_FALSE(stale.scene.has_value());
    CHECK(has_code(stale.diagnostics, wormedit::diagnostic_code_t::invalid_hold));
}

TEST_CASE("field scene renders exact hold and duration proposal ghosts",
          "[unit][core][editor-scene][proposal]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& index = std::get<wormedit::editor_field_scene_index_t>(built);

    auto duration_view = view();
    duration_view.pending_note_duration_gesture = wormedit::pending_note_duration_gesture_t{
        .reference_note = wormedit::object_id_t{11},
        .stamp = wormedit::document_stamp_t::from(7),
        .notes = {wormedit::object_id_t{11}},
        .preview_end = {wormedit::measure_id_t{2}, 0},
        .preview = {{wormedit::object_id_t{11}, wormedit::note_kind_t::hold,
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 0}}},
    };
    const auto duration = wormedit::editor_field_scene_view_t::from(
        chart, index, duration_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(duration);
    CHECK(std::ranges::count_if(duration.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_origin;
          }) == 2);
    CHECK(std::ranges::count_if(duration.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_ghost;
          }) == 2);
    const auto proposed_tail =
        std::ranges::find_if(duration.scene->note_endpoints, [](const auto& endpoint) {
            return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_ghost &&
                   endpoint.tail;
        });
    REQUIRE(proposed_tail != duration.scene->note_endpoints.end());
    CHECK(proposed_tail->bounds.top == Catch::Approx(91.0));
    CHECK(std::ranges::count_if(duration.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{11} && body.proposal_origin;
          }) == 2);
    CHECK(std::ranges::count_if(duration.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{11} && body.proposal_ghost;
          }) == 1);

    auto move_view = view();
    move_view.pending_note_move_gesture = wormedit::pending_note_move_gesture_t{
        .reference_note = wormedit::object_id_t{11},
        .stamp = wormedit::document_stamp_t::from(7),
        .notes = {wormedit::object_id_t{11}},
        .preview_position = {wormedit::measure_id_t{2}, 240},
        .preview = {{wormedit::object_id_t{11}, wormedit::lane_id_t{2},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 720}}},
    };
    const auto moved = wormedit::editor_field_scene_view_t::from(
        chart, index, move_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(moved);
    CHECK(std::ranges::count_if(moved.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_origin;
          }) == 2);
    CHECK(std::ranges::count_if(moved.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{11} && body.proposal_origin;
          }) == 2);
    CHECK(std::ranges::count_if(moved.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_ghost &&
                     endpoint.lane == wormedit::lane_id_t{2};
          }) == 1);
    CHECK(std::ranges::count_if(moved.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{11} && body.proposal_ghost;
          }) == 1);

    auto hold_view = view();
    hold_view.pending_hold = wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{2}, 0},
        .lane = wormedit::lane_id_t{1},
        .stamp = wormedit::document_stamp_t::from(7),
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
    };
    const auto hold = wormedit::editor_field_scene_view_t::from(
        chart, index, hold_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(hold);
    CHECK(std::ranges::count_if(hold.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{} && endpoint.proposal_ghost;
          }) == 2);
    CHECK(std::ranges::count_if(hold.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{} && body.proposal_ghost;
          }) == 1);

    auto reverse_hold_view = hold_view;
    reverse_hold_view.pending_hold->preview_end =
        wormedit::local_position_t{wormedit::measure_id_t{1}, 720};
    const auto reverse_hold = wormedit::editor_field_scene_view_t::from(
        chart, index, reverse_hold_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(reverse_hold);
    CHECK(std::ranges::count_if(reverse_hold.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{} && endpoint.proposal_ghost;
          }) == 2);
    CHECK(std::ranges::count_if(reverse_hold.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{} && body.proposal_ghost;
          }) == 1);

    auto merged_hold_view = view();
    merged_hold_view.pending_hold = wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{1}, 480},
        .lane = wormedit::lane_id_t{2},
        .stamp = wormedit::document_stamp_t::from(7),
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{1}, 720},
        .placement_preview =
            wormedit::pending_hold_placement_preview_t{
                .head = {wormedit::measure_id_t{1}, 480},
                .tail = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
                .removals = std::make_shared<const std::vector<wormedit::object_id_t>>(
                    std::vector<wormedit::object_id_t>{wormedit::object_id_t{11}}),
                .adds_note = true,
            },
    };
    const auto merged_hold = wormedit::editor_field_scene_view_t::from(
        chart, index, merged_hold_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(merged_hold);
    CHECK(std::ranges::count_if(merged_hold.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_origin;
          }) == 2);
    CHECK(std::ranges::count_if(merged_hold.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{11} && body.proposal_origin;
          }) == 2);
    CHECK(std::ranges::count_if(merged_hold.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{} && endpoint.proposal_ghost;
          }) == 2);
    CHECK(std::ranges::count_if(merged_hold.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{} && body.proposal_ghost;
          }) == 2);

    auto stale_view = duration_view;
    stale_view.pending_note_duration_gesture->stamp = wormedit::document_stamp_t::from(8);
    const auto stale = wormedit::editor_field_scene_view_t::from(
        chart, index, stale_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    CHECK_FALSE(stale);
    CHECK_FALSE(stale.scene.has_value());
    CHECK(has_code(stale.diagnostics, wormedit::diagnostic_code_t::invalid_hold));

    const auto baseline = wormedit::editor_field_scene_view_t::from(
        chart, index, view(), {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(baseline);
    const auto baseline_primitives =
        baseline.scene->layout_bands.size() + baseline.scene->lanes.size() +
        baseline.scene->comparison_differences.size() + baseline.scene->grid_lines.size() +
        baseline.scene->hold_bodies.size() + baseline.scene->note_endpoints.size() +
        (baseline.scene->cursor.has_value() ? 1U : 0U);
    auto capped_view = view();
    capped_view.pending_hold = wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{2}, 0},
        .lane = wormedit::lane_id_t{1},
        .stamp = wormedit::document_stamp_t::from(7),
    };
    const auto capped = wormedit::editor_field_scene_view_t::from(
        chart, index, capped_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .maximum_scene_primitives = baseline_primitives});
    CHECK_FALSE(capped);
    CHECK_FALSE(capped.scene.has_value());
    CHECK(has_code(capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));
}

TEST_CASE("field scene shows simultaneous keyboard hold previews",
          "[unit][core][editor-scene][proposal]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto preview = view();
    preview.pending_hold = wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{2}, 0},
        .lane = wormedit::lane_id_t{1},
        .stamp = wormedit::document_stamp_t::from(7),
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
    };
    preview.additional_keyboard_holds.push_back(wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{2}, 0},
        .lane = wormedit::lane_id_t{2},
        .stamp = wormedit::document_stamp_t::from(7),
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
    });
    const auto scene = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), preview,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(scene);
    CHECK(std::ranges::count_if(scene.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{} && endpoint.proposal_ghost;
          }) == 4);
    CHECK(std::ranges::count_if(scene.scene->hold_bodies, [](const auto& body) {
              return body.note == wormedit::object_id_t{} && body.proposal_ghost;
          }) == 2);
}

TEST_CASE("field scene marks painted taps for erasure and enforces paint scene caps",
          "[unit][core][editor-scene][paint][bounds]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& index = std::get<wormedit::editor_field_scene_index_t>(built);
    auto occupied_view = view();
    occupied_view.viewport_scroll_pixels = 0.0;
    occupied_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .stamp = wormedit::document_stamp_t::from(7),
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 240}},
            }),
    };
    const auto occupied = wormedit::editor_field_scene_view_t::from(
        chart, index, occupied_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(occupied);
    const auto erased =
        std::ranges::find_if(occupied.scene->note_endpoints, [](const auto& endpoint) {
            return endpoint.note == wormedit::object_id_t{10};
        });
    REQUIRE(erased != occupied.scene->note_endpoints.end());
    CHECK(erased->paint_preview);
    CHECK(erased->paint_erase_preview);
    CHECK(std::ranges::count_if(occupied.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.paint_preview && endpoint.lane == wormedit::lane_id_t{1} &&
                     endpoint.bounds.top == Catch::Approx(48.0);
          }) == 1);

    auto collision_view = view();
    collision_view.viewport_scroll_pixels = 0.0;
    collision_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .stamp = wormedit::document_stamp_t::from(7),
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{2}, {wormedit::measure_id_t{1}, 840}},
            }),
    };
    const auto collision = wormedit::editor_field_scene_view_t::from(
        chart, index, collision_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(collision);
    const auto invalid =
        std::ranges::find_if(collision.scene->note_endpoints, [](const auto& endpoint) {
            return endpoint.paint_preview && endpoint.paint_invalid_preview;
        });
    REQUIRE(invalid != collision.scene->note_endpoints.end());
    CHECK_FALSE(invalid->paint_erase_preview);
    CHECK(invalid->lane == wormedit::lane_id_t{2});
    CHECK(invalid->bounds.top == Catch::Approx(163.0));

    auto base_view = view();
    const auto baseline = wormedit::editor_field_scene_view_t::from(
        chart, index, base_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(baseline);
    const auto base_primitives =
        baseline.scene->layout_bands.size() + baseline.scene->lanes.size() +
        baseline.scene->comparison_differences.size() + baseline.scene->grid_lines.size() +
        baseline.scene->hold_bodies.size() + baseline.scene->note_endpoints.size() +
        (baseline.scene->cursor.has_value() ? 1U : 0U);
    auto empty_cell_view = base_view;
    empty_cell_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .stamp = wormedit::document_stamp_t::from(7),
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 720}},
            }),
    };
    const auto primitive_capped =
        wormedit::editor_field_scene_view_t::from(chart, index, empty_cell_view,
                                                  {.field_left_pixels = 10.0,
                                                   .field_width_pixels = 200.0,
                                                   .maximum_scene_primitives = base_primitives});
    CHECK_FALSE(primitive_capped);
    CHECK_FALSE(primitive_capped.scene.has_value());
    CHECK(has_code(primitive_capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));

    auto operation_view = base_view;
    operation_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .stamp = wormedit::document_stamp_t::from(7),
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 720}},
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{2}, 240}},
            }),
    };
    const auto operation_capped =
        wormedit::editor_field_scene_view_t::from(chart, index, operation_view,
                                                  {.field_left_pixels = 10.0,
                                                   .field_width_pixels = 200.0,
                                                   .maximum_composition_operations = 3});
    CHECK_FALSE(operation_capped);
    CHECK_FALSE(operation_capped.scene.has_value());
    CHECK(has_code(operation_capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));
}

TEST_CASE("field scene interval tree narrows a sparse 50k-note viewport",
          "[unit][core][editor-scene][stress]") {
    auto chart = scene_chart();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.resize(1);
    chart.measures.front().notes.clear();
    chart.measures.front().notes.reserve(50'000);
    for (std::uint64_t note = 0; note < 50'000; ++note) {
        chart.measures.front().notes.push_back({.id = wormedit::object_id_t{100 + note},
                                                .position = {wormedit::measure_id_t{1}, note * 2},
                                                .lane = wormedit::lane_id_t{1}});
    }
    chart.lane_layouts.resize(1);
    auto scene_view = view();
    scene_view.cursor = {wormedit::measure_id_t{1}, 98'000};
    scene_view.pixels_per_standard_measure = 4'096.0;
    scene_view.viewport_height_pixels = 1.0;
    scene_view.viewport_scroll_pixels = 4'013.58;
    scene_view.selected_notes.clear();
    scene_view.selected_objects.clear();

    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 0,
         .field_width_pixels = 100,
         .note_head_height_pixels = 0.1,
         .maximum_composition_operations = 2'000});
    REQUIRE(result);
    CHECK(result.scene->note_endpoints.size() < 20);
    CHECK(result.scene->cursor.has_value());
}

namespace {
void check_window_bounds(const wormedit::editor_field_rect_t& actual,
                         const wormedit::editor_field_rect_t& expected) {
    CHECK(actual.left == Catch::Approx(expected.left).margin(1e-9));
    CHECK(actual.right == Catch::Approx(expected.right).margin(1e-9));
    CHECK(actual.top == Catch::Approx(expected.top).margin(1e-9));
    CHECK(actual.bottom == Catch::Approx(expected.bottom).margin(1e-9));
}
template <typename Geometry, typename Check>
void check_window_geometry(const std::vector<Geometry>& actual,
                           const std::vector<Geometry>& expected, Check check) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) check(actual[i], expected[i]);
}
void check_window_band(const wormedit::editor_field_layout_band_t& a,
                       const wormedit::editor_field_layout_band_t& e) {
    CHECK(std::tie(a.begin_row, a.end_row, a.profile_id, a.split_stage) ==
          std::tie(e.begin_row, e.end_row, e.profile_id, e.split_stage));
    CHECK(a.top_pixels == Catch::Approx(e.top_pixels).margin(1e-9));
    CHECK(a.bottom_pixels == Catch::Approx(e.bottom_pixels).margin(1e-9));
}
void check_window_lane(const wormedit::editor_field_lane_geometry_t& a,
                       const wormedit::editor_field_lane_geometry_t& e) {
    CHECK(std::tie(a.layout_band, a.lane, a.role, a.stage, a.glyph, a.display_order) ==
          std::tie(e.layout_band, e.lane, e.role, e.stage, e.glyph, e.display_order));
    check_window_bounds(a.bounds, e.bounds);
}
void check_window_endpoint(const wormedit::editor_field_note_endpoint_geometry_t& a,
                           const wormedit::editor_field_note_endpoint_geometry_t& e) {
    CHECK(std::tie(a.note, a.lane, a.sound, a.glyph, a.kind, a.tail, a.selected,
        a.semantic_color, a.proposal_origin, a.proposal_ghost, a.paint_preview,
        a.paint_erase_preview, a.paint_invalid_preview) ==
          std::tie(e.note, e.lane, e.sound, e.glyph, e.kind, e.tail, e.selected,
        e.semantic_color, e.proposal_origin, e.proposal_ghost, e.paint_preview,
        e.paint_erase_preview, e.paint_invalid_preview));
    check_window_bounds(a.bounds, e.bounds);
}
void check_window_hold(const wormedit::editor_field_hold_body_geometry_t& a,
                       const wormedit::editor_field_hold_body_geometry_t& e) {
    CHECK(std::tie(a.note, a.lane, a.selected, a.semantic_color, a.proposal_origin, a.proposal_ghost) ==
          std::tie(e.note, e.lane, e.selected, e.semantic_color, e.proposal_origin, e.proposal_ghost));
    check_window_bounds(a.bounds, e.bounds);
}
void check_window_bgm_lane(const wormedit::editor_field_bgm_lane_geometry_t& a,
                           const wormedit::editor_field_bgm_lane_geometry_t& e) {
    CHECK(std::tie(a.source_lane, a.source_order) == std::tie(e.source_lane, e.source_order));
    check_window_bounds(a.bounds, e.bounds);
}
void check_window_bgm_event(const wormedit::editor_field_bgm_event_geometry_t& a,
                            const wormedit::editor_field_bgm_event_geometry_t& e) {
    CHECK(std::tie(a.event, a.source_lane, a.sound, a.bmson_restart, a.selected) ==
          std::tie(e.event, e.source_lane, e.sound, e.bmson_restart, e.selected));
    check_window_bounds(a.bounds, e.bounds);
}
void check_window_bga_event(const wormedit::editor_field_bga_event_geometry_t& a,
                            const wormedit::editor_field_bga_event_geometry_t& e) {
    CHECK(std::tie(a.event, a.resource, a.kind, a.row, a.overlap_count, a.selected) ==
          std::tie(e.event, e.resource, e.kind, e.row, e.overlap_count, e.selected));
    check_window_bounds(a.bounds, e.bounds);
}
void check_window_grid_line(const wormedit::editor_field_grid_geometry_t& a,
                            const wormedit::editor_field_grid_geometry_t& e) {
    CHECK(std::tie(a.chart_row, a.kind, a.emphasis) == std::tie(e.chart_row, e.kind, e.emphasis));
    CHECK(a.y_pixels == Catch::Approx(e.y_pixels).margin(1e-9));
}
void check_window_cursor(const wormedit::editor_field_scene_view_t& actual,
                         const wormedit::editor_field_scene_view_t& expected) {
    REQUIRE(actual.cursor.has_value() == expected.cursor.has_value());
    if (actual.cursor) {
        CHECK(actual.cursor->lane == expected.cursor->lane);
        check_window_bounds(actual.cursor->bounds, expected.cursor->bounds);
    }
}
void check_window_scene(const wormedit::editor_field_scene_view_t& actual,
                        const wormedit::editor_field_scene_view_t& expected) {
    CHECK(actual.visible_begin_row == expected.visible_begin_row);
    CHECK(actual.visible_end_row == expected.visible_end_row);
    check_window_geometry(actual.layout_bands, expected.layout_bands, check_window_band);
    check_window_geometry(actual.lanes, expected.lanes, check_window_lane);
    check_window_geometry(actual.note_endpoints, expected.note_endpoints, check_window_endpoint);
    check_window_geometry(actual.hold_bodies, expected.hold_bodies, check_window_hold);
    check_window_geometry(actual.bgm_lanes, expected.bgm_lanes, check_window_bgm_lane);
    check_window_geometry(actual.bgm_events, expected.bgm_events, check_window_bgm_event);
    check_window_geometry(actual.bga_events, expected.bga_events, check_window_bga_event);
    check_window_geometry(actual.grid_lines, expected.grid_lines, check_window_grid_line);
    check_window_cursor(actual, expected);
    REQUIRE(actual.comparison_differences.size() == expected.comparison_differences.size());
}
auto window_chart() -> wormedit::chart_document_t {
    auto chart = scene_chart();
    for (std::uint64_t i = 3; i <= 24; ++i)
        chart.measures.push_back({.id = wormedit::measure_id_t{i},
            .definition = {{1, 1}, wormedit::meter_t{4, 4}},
            .notes = {{.id = wormedit::object_id_t{100 + i},
                .position = {wormedit::measure_id_t{i}, i * 17},
                .lane = wormedit::lane_id_t{1}}}});
    chart.measures[0].notes[1].end = wormedit::local_position_t{wormedit::measure_id_t{8}, 240};
    return chart;
}
void require_two_bga_events(const wormedit::editor_field_scene_view_result_t& result,
                            wormedit::object_id_t first, wormedit::object_id_t second) {
    REQUIRE(result);
    REQUIRE(result.scene->bga_events.size() == 2);
    CHECK(result.scene->bga_events[0].event == first);
    CHECK(result.scene->bga_events[1].event == second);
}
void require_metadata_bga_events(
    const wormedit::editor_field_scene_window_metadata_view_t& metadata) {
    REQUIRE(metadata.projection);
    REQUIRE(metadata.projection.scene->bga_events.size() == 2);
}
void check_bga_window_projection(const wormedit::chart_document_t& chart,
                                const wormedit::editor_field_scene_index_t& index,
                                const wormedit::editor_interaction_view_model_t& current,
                                const wormedit::editor_field_scene_request_t& request) {
    wormedit::editor_field_scene_window_t window;
    const auto initial = wormedit::editor_field_scene_view_result_t::from(
        wormedit::editor_field_scene_view_result_t::from(chart, index, current, request, window));
    REQUIRE(initial);
    const auto metadata = wormedit::editor_field_scene_window_metadata_view_t::from(
        chart, index, current, request, window);
    require_metadata_bga_events(metadata);
    const auto cached =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    const auto fresh = wormedit::editor_field_scene_view_t::from(chart, index, current, request);
    require_two_bga_events(cached, wormedit::object_id_t{900}, wormedit::object_id_t{901});
    require_two_bga_events(fresh, wormedit::object_id_t{900}, wormedit::object_id_t{901});
    check_window_scene(*cached.scene, *fresh.scene);
}
void check_reused_frame(const wormedit::chart_document_t& chart,
                        const wormedit::editor_field_scene_index_t& index,
                        const wormedit::editor_interaction_view_model_t& current,
                        const wormedit::editor_field_scene_request_t& request,
                        wormedit::editor_field_scene_window_t& window) {
    const auto reused =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    const auto fresh = wormedit::editor_field_scene_view_t::from(chart, index, current, request);
    REQUIRE(reused); REQUIRE(fresh);
    check_window_scene(*reused.scene, *fresh.scene);
}
} // namespace

TEST_CASE("Compose scene window matches fresh geometry in both directions", "[editor-scene][scene-window]") {
    const auto chart = window_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& index = std::get<wormedit::editor_field_scene_index_t>(built);
    for (const auto direction : {wormedit::editor_field_scroll_direction_t::up,
                                 wormedit::editor_field_scroll_direction_t::down}) {
        wormedit::editor_field_scene_window_t window;
        auto current = view();
        wormedit::editor_field_scene_request_t request{.field_left_pixels = 10,
            .field_width_pixels = 200, .scroll_direction = direction,
            .bgm_pane_left_pixels = 220, .bgm_pane_width_pixels = 36};
        for (const double scroll : {0.0, 19.75, 64.25, 120.5, 220.75, 401.25, 411.5, 500.25, 799.75}) {
            current.viewport_scroll_pixels = scroll;
            check_reused_frame(chart, index, current, request, window);
        }
        CHECK(window.reuse_count > 0);
        CHECK(window.rebuild_count < 9);
        current.active_lane = wormedit::lane_id_t{2};
        current.cursor = {wormedit::measure_id_t{4}, 480};
        check_reused_frame(chart, index, current, request, window);
    }
}

TEST_CASE("Compose scene window rebuilds for stable input changes and falls back for previews", "[editor-scene][scene-window]") {
    auto chart = window_chart();
    auto built = wormedit::build_editor_field_scene_index(chart, 7);
    auto index = std::get<wormedit::editor_field_scene_index_t>(built);
    auto current = view();
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 200};
    wormedit::editor_field_scene_window_t window;
    check_reused_frame(chart, index, current, request, window);
    const auto rebuilt = window.rebuild_count;
    current.selected_notes.clear();
    check_reused_frame(chart, index, current, request, window);
    CHECK(window.rebuild_count == rebuilt + 1);
    current.pixels_per_standard_measure = 384;
    check_reused_frame(chart, index, current, request, window);
    CHECK(window.rebuild_count == rebuilt + 2);
    request.field_width_pixels = 240;
    check_reused_frame(chart, index, current, request, window);
    CHECK(window.rebuild_count == rebuilt + 3);
    current.document_revision = 8;
    index = std::get<wormedit::editor_field_scene_index_t>(wormedit::build_editor_field_scene_index(chart, 8));
    check_reused_frame(chart, index, current, request, window);
    CHECK(window.rebuild_count == rebuilt + 4);
    request.comparison_difference_rows = {{wormedit::measure_id_t{1}, 240}};
    check_reused_frame(chart, index, current, request, window);
    CHECK_FALSE(window.scene);
}

namespace {
void check_bga_kind_geometry(const wormedit::editor_field_scene_view_t& scene, std::size_t index,
                             wormedit::visual_event_kind_t kind) {
    CHECK(scene.bga_lanes[index].kind == kind);
    CHECK(scene.bga_events[index].bounds.left == 210 + 48 * index);
    CHECK(scene.bga_events[index].overlap_count == 1);
}
}

TEST_CASE("BGA scene separates four kinds and overlaps only within each lane", "[editor-scene][bga]") {
    using namespace wormedit;
    auto chart = scene_chart();
    constexpr std::array kinds{visual_event_kind_t::background, visual_event_kind_t::layer,
                               visual_event_kind_t::layer2, visual_event_kind_t::poor};
    for (std::size_t index = 0; index < kinds.size(); ++index)
        chart.bga_events.push_back({.id = object_id_t{900 + index},
            .position = {measure_id_t{1}, 720}, .resource = resource_id_t{90 + index}, .kind = kinds[index]});
    const auto built = build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<editor_field_scene_index_t>(built));
    const auto projected = editor_field_scene_view_t::from(
        chart, std::get<editor_field_scene_index_t>(built), view(),
        {.field_width_pixels = 200, .bga_pane_left_pixels = 210, .bga_pane_width_pixels = 192});
    REQUIRE(projected);
    REQUIRE(projected.scene->bga_lanes.size() == 4);
    REQUIRE(projected.scene->bga_events.size() == 4);
    for (std::size_t index = 0; index < kinds.size(); ++index)
        check_bga_kind_geometry(*projected.scene, index, kinds[index]);
}

TEST_CASE("BGA visibility combines authored kinds and a bounded reveal mask", "[editor-scene][bga]") {
    using namespace wormedit;
    auto chart = scene_chart();
    CHECK(editor_bga_visible_lane_mask(chart, 0) == 0);
    CHECK(editor_bga_visible_lane_mask(chart, 0xF0) == 0);
    CHECK(editor_bga_visible_lane_mask(chart, 4) == 4);
    chart.bga_events = {{.kind = visual_event_kind_t::video}, {.kind = visual_event_kind_t::storyboard}};
    CHECK(editor_bga_visible_lane_mask(chart, 4) == 5);
    CHECK(editor_bga_visible_lane_mask(chart, 0) == 1);
}

TEST_CASE("cached BGA projection returns each event once with fresh geometry parity",
          "[editor-scene][scene-window][bga]") {
    auto chart = window_chart();
    chart.bga_events = {
        {.id = wormedit::object_id_t{900},
         .position = {wormedit::measure_id_t{1}, 720},
         .resource = wormedit::resource_id_t{90}},
        {.id = wormedit::object_id_t{901},
         .position = {wormedit::measure_id_t{1}, 900},
         .resource = wormedit::resource_id_t{91},
         .kind = wormedit::visual_event_kind_t::video},
    };
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    current.bga_lane_visible = true;
    auto request = wormedit::editor_field_scene_request_t{
        .field_left_pixels = 10.0,
        .field_width_pixels = 200.0,
        .bgm_pane_left_pixels = 220.0,
        .bgm_pane_width_pixels = 36.0,
        .bga_pane_left_pixels = 270.0,
        .bga_pane_width_pixels = 48.0,
    };
    for (const auto direction : {wormedit::editor_field_scroll_direction_t::up,
                                 wormedit::editor_field_scroll_direction_t::down}) {
        request.scroll_direction = direction;
        check_bga_window_projection(chart, index, current, request);
    }
}

namespace {
void check_window_rejection(const wormedit::editor_field_scene_view_result_t& reused,
                            const wormedit::editor_field_scene_view_result_t& fresh) {
    REQUIRE_FALSE(reused);
    REQUIRE_FALSE(fresh);
    REQUIRE_FALSE(reused.diagnostics.empty());
    REQUIRE_FALSE(fresh.diagnostics.empty());
    CHECK(std::tie(reused.diagnostics.front().code, reused.diagnostics.front().message) ==
          std::tie(fresh.diagnostics.front().code, fresh.diagnostics.front().message));
}
} // namespace

TEST_CASE("Compose scene window preserves primitive rejection when cursor enters cached window", "[editor-scene][scene-window]") {
    auto chart = scene_chart();
    chart.measures.resize(1);
    chart.measures.front().notes.resize(1);
    chart.lane_layouts.resize(1);
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& index = std::get<wormedit::editor_field_scene_index_t>(built);
    auto current = view();
    current.cursor = {wormedit::measure_id_t{1}, 240};
    current.active_lane = wormedit::lane_id_t{999};
    current.viewport_scroll_pixels = 0;
    current.viewport_height_pixels = 384;
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 200};
    const auto initial = wormedit::editor_field_scene_view_t::from(chart, index, current, request);
    REQUIRE(initial);
    const auto& scene = *initial.scene;
    request.maximum_scene_primitives = scene.layout_bands.size() + scene.lanes.size() +
        scene.grid_lines.size() + scene.note_endpoints.size();
    wormedit::editor_field_scene_window_t window;
    REQUIRE(wormedit::editor_field_scene_view_t::from(chart, index, current, request, window));
    REQUIRE(window.scene);
    current.active_lane = wormedit::lane_id_t{1};
    const auto reused =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    const auto fresh = wormedit::editor_field_scene_view_t::from(chart, index, current, request);
    check_window_rejection(reused, fresh);
    CHECK_FALSE(window.scene);
    CHECK(window.reuse_count == 0);
}

namespace {
template <class Condition>
void check_metadata(const Condition& condition, const char* expectation) {
    INFO(expectation);
    CHECK(static_cast<bool>(condition));
}
template <class Condition>
void check_metadata_false(const Condition& condition, const char* expectation) {
    INFO(expectation);
    CHECK_FALSE(static_cast<bool>(condition));
}
template <class Condition>
void require_metadata(const Condition& condition, const char* expectation) {
    INFO(expectation);
    REQUIRE(static_cast<bool>(condition));
}
} // namespace

TEST_CASE("Read-only window metadata matches fresh current layout and cursor",
          "[editor-scene][window-metadata]") {
    const auto chart = window_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    for (const auto direction : {wormedit::editor_field_scroll_direction_t::up,
                                 wormedit::editor_field_scroll_direction_t::down}) {
        auto current = view();
        wormedit::editor_field_scene_request_t request{.field_left_pixels = 10,
                                                       .field_width_pixels = 200,
                                                       .scroll_direction = direction,
                                                       .bgm_pane_left_pixels = 220,
                                                       .bgm_pane_width_pixels = 36};
        wormedit::editor_field_scene_window_t window;
        require_metadata(
            wormedit::editor_field_scene_view_t::from(chart, index, current, request, window),
            "wormedit::editor_field_scene_view_t::from(chart, index, current, request,      "
            "                                                            window)");
        const auto counters = std::pair{window.rebuild_count, window.reuse_count};
        for (const double scroll : {0.0, 19.75, 64.25, 19.75, 0.0}) {
            current.viewport_scroll_pixels = scroll;
            current.active_lane = wormedit::lane_id_t{2};
            current.cursor = {wormedit::measure_id_t{4}, 480};
            require_metadata(
                wormedit::can_reuse_editor_field_scene_window(chart, index, current, request,
                                                              window),
                "wormedit::can_reuse_editor_field_scene_window(chart, index, current, request,     "
                "                                                              window)");
            const auto metadata = wormedit::editor_field_scene_window_metadata_view_t::from(
                chart, index, current, request, window);
            const auto fresh =
                wormedit::editor_field_scene_view_t::from(chart, index, current, request);
            require_metadata(metadata.projection, "metadata.projection");
            require_metadata(fresh, "fresh");
            const auto& actual = *metadata.projection.scene;
            const auto& expected = *fresh.scene;
            check_metadata(actual.visible_begin_row == expected.visible_begin_row,
                           "actual.visible_begin_row == expected.visible_begin_row");
            check_metadata(actual.visible_end_row == expected.visible_end_row,
                           "actual.visible_end_row == expected.visible_end_row");
            check_window_geometry(actual.layout_bands, expected.layout_bands, check_window_band);
            check_window_geometry(actual.lanes, expected.lanes, check_window_lane);
            check_window_geometry(actual.bgm_lanes, expected.bgm_lanes, check_window_bgm_lane);
            check_window_cursor(actual, expected);
            check_metadata(actual.note_endpoints.empty(), "actual.note_endpoints.empty()");
            check_metadata(actual.hold_bodies.empty(), "actual.hold_bodies.empty()");
            check_metadata(actual.bgm_events.empty(), "actual.bgm_events.empty()");
            for (const auto& hold : expected.hold_bodies) {
                check_metadata(hold.bounds.top >= metadata.hold_clip_top - 1e-9,
                               "hold.bounds.top >= metadata.hold_clip_top - 1e-9");
                check_metadata(hold.bounds.bottom <= metadata.hold_clip_bottom + 1e-9,
                               "hold.bounds.bottom <= metadata.hold_clip_bottom + 1e-9");
            }
            check_metadata(std::pair{window.rebuild_count, window.reuse_count} == counters,
                           "std::pair{window.rebuild_count, window.reuse_count} == counters");
        }
    }
}
TEST_CASE("Read-only metadata rejects stale window keys and transient composition",
          "[editor-scene][window-metadata]") {
    const auto chart = window_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 200};
    wormedit::editor_field_scene_window_t window;
    require_metadata(
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window),
        "         wormedit::editor_field_scene_view_t::from(chart, index, current, request, "
        "window)");
    current.selected_notes.clear();
    check_metadata_false(
        wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window),
        "         wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, "
        "window)");
    check_metadata_false(
        wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, request,
                                                                  window)
            .projection,
        "         wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, "
        "request, window)             .projection");
    current = view();
    request.comparison_difference_rows = {{wormedit::measure_id_t{1}, 240}};
    check_metadata_false(
        wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window),
        "         wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, "
        "window)");
    check_metadata_false(
        wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, request,
                                                                  window)
            .projection,
        "         wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, "
        "request, window)             .projection");
    check_metadata(wormedit::editor_field_scene_view_t::from(chart, index, current, request),
                   "wormedit::editor_field_scene_view_t::from(chart, index, current, request)");
}

TEST_CASE("Window metadata preserves invalid geometry and primitive limit fallback",
          "[editor-scene][window-metadata]") {
    const auto chart = window_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 200};
    wormedit::editor_field_scene_window_t window;
    require_metadata(
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window),
        "         wormedit::editor_field_scene_view_t::from(chart, index, current, request, "
        "window)");
    current.viewport_height_pixels = -1;
    check_metadata_false(
        wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window),
        "         wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, "
        "window)");
    check_metadata_false(
        wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, request,
                                                                  window)
            .projection,
        "         wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, "
        "request, window)             .projection");
    current = view();
    request.maximum_scene_primitives = 1;
    check_metadata_false(
        wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, request,
                                                                  window)
            .projection,
        "         wormedit::editor_field_scene_window_metadata_view_t::from(chart, index, current, "
        "request, window)             .projection");
    check_metadata_false(
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window),
        "         wormedit::editor_field_scene_view_t::from(chart, index, current, request, "
        "window)");
    check_metadata_false(window.scene, "window.scene");
}

namespace {
void expect_rejected_anchor_scene(const wormedit::chart_document_t& chart,
    const wormedit::editor_field_scene_index_t& index,
    const wormedit::editor_interaction_view_model_t& current,
    const wormedit::editor_field_scene_request_t& request,
    wormedit::editor_field_scene_window_t& window) {
    CHECK_FALSE(wormedit::editor_field_scene_view_t::from(chart, index, current, request));
    CHECK_FALSE(wormedit::editor_field_scene_view_t::from(chart, index, current, request, window));
}
void check_configured_scene_window(const wormedit::chart_document_t& chart,
    const wormedit::editor_field_scene_index_t& index,
    wormedit::editor_field_scroll_direction_t direction, double anchor, double height) {
    auto current = view();
    current.viewport_height_pixels = height;
    current.viewport_minimum_margin_fraction = std::max(0.2, anchor);
    current.viewport_end_margin_fraction = std::min(0.2, anchor);
    wormedit::editor_field_scene_request_t request{
        .field_left_pixels = 10, .field_width_pixels = 200, .scroll_direction = direction};
    wormedit::editor_field_scene_window_t window;
    const double minimum = -height * current.viewport_minimum_margin_fraction;
    const double maximum = wormedit::editor_maximum_viewport_scroll(384.0, height,
        current.viewport_end_margin_fraction);
    for (const double scroll : {0.0, minimum, minimum + 0.5, maximum - 0.5, maximum}) {
        current.viewport_scroll_pixels = scroll;
        check_reused_frame(chart, index, current, request, window);
    }
    REQUIRE(window.reuse_count > 0);
    for (const double scroll : {minimum - 0.01, maximum + 0.01}) {
        current.viewport_scroll_pixels = scroll;
        expect_rejected_anchor_scene(chart, index, current, request, window);
    }
}
}
TEST_CASE("configured margins keep fresh and cached scene primitives bounded in both directions",
          "[editor-scene][anchor-scene]") {
    const auto chart = scene_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    for (const auto direction : {wormedit::editor_field_scroll_direction_t::up,
                                 wormedit::editor_field_scroll_direction_t::down})
        for (const double anchor : {0.2, 0.35, 0.1})
            for (const double height : {192.0, 600.0})
                check_configured_scene_window(chart, index, direction, anchor, height);
}
TEST_CASE("scene margin validation rejects invalid values and retains default twenty percent",
          "[editor-scene][anchor-scene]") {
    const auto chart = scene_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    const wormedit::editor_field_scene_request_t request{.field_left_pixels = 10, .field_width_pixels = 200};
    wormedit::editor_field_scene_window_t window;
    REQUIRE(wormedit::editor_field_scene_view_t::from(chart, index, current, request, window));
    current.viewport_scroll_pixels = -current.viewport_height_pixels * 0.35;
    CHECK_FALSE(wormedit::editor_field_scene_view_t::from(chart, index, current, request));
    CHECK_FALSE(wormedit::editor_field_scene_view_t::from(chart, index, current, request, window));
    current.viewport_scroll_pixels = 0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const auto [minimum, end] : {std::pair{0.19, 0.2}, std::pair{1.01, 0.2},
                                    std::pair{0.2, -0.01}, std::pair{0.2, 0.21},
                                    std::pair{nan, 0.2}, std::pair{0.2, nan}, std::pair{infinity, 0.2}}) {
        current.viewport_minimum_margin_fraction = minimum;
        current.viewport_end_margin_fraction = end;
        CHECK_FALSE(wormedit::editor_field_scene_view_t::from(chart, index, current, request));
        CHECK_FALSE(
            wormedit::editor_field_scene_view_t::from(chart, index, current, request, window));
    }
}

TEST_CASE("cached scene validates changed margins instead of retaining its old allowance",
          "[editor-scene][anchor-scene]") {
    const auto chart = scene_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    const wormedit::editor_field_scene_request_t request{.field_left_pixels = 10, .field_width_pixels = 200};
    wormedit::editor_field_scene_window_t window;
    check_reused_frame(chart, index, current, request, window);
    current.viewport_minimum_margin_fraction = 0.35;
    current.viewport_scroll_pixels = -current.viewport_height_pixels * 0.35;
    check_reused_frame(chart, index, current, request, window);
    REQUIRE(window.reuse_count > 0);
    const auto reused = window.reuse_count;
    current.viewport_minimum_margin_fraction = 0.2;
    expect_rejected_anchor_scene(chart, index, current, request, window);
    CHECK(window.reuse_count == reused);
    current.viewport_scroll_pixels = 0;
    current.viewport_end_margin_fraction = 0.1;
    check_reused_frame(chart, index, current, request, window);
}

namespace {
auto coverage_window_chart() -> wormedit::chart_document_t {
    auto chart = window_chart();
    for (std::uint64_t i = 25; i <= 100; ++i) {
        chart.measures.push_back({.id = wormedit::measure_id_t{i},
            .definition = {{1, 1}, wormedit::meter_t{4, 4}},
            .notes = {{.id = wormedit::object_id_t{100 + i},
                .position = {wormedit::measure_id_t{i}, (i * 17) % 960},
                .lane = wormedit::lane_id_t{1}}}});
    }
    return chart;
}
void check_complete_tile_coverage(const wormedit::editor_field_scene_window_t& window,
                                 const wormedit::editor_interaction_view_model_t& current,
                                 double scale, bool downscroll) {
    constexpr double guard = 96;
    const auto begin = current.viewport_scroll_pixels;
    const auto end = begin + current.viewport_height_pixels;
    const auto source_begin = window.view.viewport_scroll_pixels;
    const auto source_end = source_begin + window.view.viewport_height_pixels;
    const auto covered_begin = (source_begin == 0 ? 0 : source_begin + guard) * scale;
    const auto covered_end = (source_end - guard) * scale;
    const auto visible_begin = downscroll ? -end * scale : begin * scale;
    const auto visible_end = downscroll ? -begin * scale : end * scale;
    const auto tile_begin = std::floor(visible_begin / 512) * 512;
    const auto tile_end = std::ceil(visible_end / 512) * 512;
    CHECK((downscroll ? -covered_end : covered_begin) <= tile_begin + 1e-8);
    CHECK((downscroll ? -covered_begin : covered_end) >= tile_end - 1e-8);
}
}
TEST_CASE("Retained source margin covers complete GPU tiles without moving visible geometry",
          "[editor-scene][tile-window-coverage]") {
    const auto chart = coverage_window_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    for (const auto direction : {wormedit::editor_field_scroll_direction_t::up,
                                 wormedit::editor_field_scroll_direction_t::down}) {
        auto current = view();
        current.viewport_height_pixels = 532;
        wormedit::editor_field_scene_request_t request{.field_left_pixels = 10,
            .field_width_pixels = 200, .scroll_direction = direction,
            .bgm_pane_left_pixels = 220, .bgm_pane_width_pixels = 36,
            .retained_window_minimum_margin_pixels = 1120};
        wormedit::editor_field_scene_window_t window;
        for (const double scroll : {0.0, 67.25, 620.74, 785.694, 2048.75, 2896.25,
                                   2200.5, 785.0, 0.0}) {
            current.viewport_scroll_pixels = scroll;
            check_reused_frame(chart, index, current, request, window);
            for (const double scale : {1.0, .992, 496.0 / 532.0, .5})
                check_complete_tile_coverage(window, current, scale,
                    direction == wormedit::editor_field_scroll_direction_t::down);
        }
        CHECK(window.reuse_count > 0);
    }
}
TEST_CASE("Default source padding preserves three viewport windows and invalid padding falls back",
          "[editor-scene][tile-window-coverage]") {
    const auto chart = coverage_window_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 200};
    wormedit::editor_field_scene_window_t window;
    check_reused_frame(chart, index, current, request, window);
    CHECK(window.view.viewport_height_pixels == current.viewport_height_pixels * 3);
    const auto rebuilt = window.rebuild_count;
    request.retained_window_minimum_margin_pixels = 1120;
    check_reused_frame(chart, index, current, request, window);
    CHECK(window.rebuild_count == rebuilt + 1);
    for (const double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
        request.retained_window_minimum_margin_pixels = invalid;
        CHECK_FALSE(wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window));
        check_reused_frame(chart, index, current, request, window);
    }
}

TEST_CASE("Expanded source budget failure retains valid fresh visible primitives",
          "[editor-scene][tile-window-coverage]") {
    const auto chart = coverage_window_chart();
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    current.viewport_height_pixels = 532;
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 200,
        .maximum_scene_primitives = 64,
        .retained_window_minimum_margin_pixels = 1120};
    wormedit::editor_field_scene_window_t window;
    check_reused_frame(chart, index, current, request, window);
    CHECK_FALSE(window.scene);
    CHECK(window.rebuild_count == 0);
    CHECK_FALSE(wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window));
}

namespace {

void check_dp_field_lane(const auto& painted, wormedit::lane_id_t expected, bool left) {
    CHECK(painted.lane == expected);
    CHECK(painted.stage == (left ? wormedit::stage_group_t::left : wormedit::stage_group_t::right));
}

void check_dp_field_layout(const wormedit::lane_layout_t& layout) {
    using namespace wormedit;
    INFO(layout.profile_id);
    auto chart = scene_chart();
    chart.lane_layouts.resize(1);
    chart.lane_layouts.front().layout = layout;
    for (auto& measure : chart.measures) measure.notes.clear();
    const auto built = build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<editor_field_scene_index_t>(built));
    auto dp_view = view();
    dp_view.active_lane = layout.lanes.front().id;
    dp_view.selected_notes.clear();
    dp_view.selected_objects.clear();
    const auto result =
        editor_field_scene_view_t::from(chart, std::get<editor_field_scene_index_t>(built), dp_view,
                                        {.field_left_pixels = 10.0,
                                         .field_width_pixels = 400.0,
                                         .split_stage_gutter_pixels = 12.0});
    REQUIRE(result);
    REQUIRE(result.scene->lanes.size() == layout.lanes.size());
    const auto split = *layout.split_after_column;
    for (std::size_t i = 0; i < layout.lanes.size(); ++i) {
        const auto& painted = result.scene->lanes[i];
        check_dp_field_lane(painted, layout.lanes[i].id, i < split);
    }
    CHECK(result.scene->lanes[split].bounds.left -
          result.scene->lanes[split - 1].bounds.right == Catch::Approx(12.0));

}

} // namespace

TEST_CASE("DP field keeps all P1 lanes before P2 with stage-local display orders",
          "[unit][core][editor-scene]") {
    for (const auto& descriptor : wormedit::built_in_bms_lane_profiles()) {
        if (descriptor.profile.layout.split_stage) check_dp_field_layout(descriptor.profile.layout);
    }
}

namespace {
auto bgm_width_window_snapshots_owned_values() -> bool {
    auto chart = scene_chart();
    chart.bgm_source_lanes.front().stem_channel = wormedit::object_id_t{100};
    const auto index = std::get<wormedit::editor_field_scene_index_t>(
        wormedit::build_editor_field_scene_index(chart, 7));
    auto current = view();
    std::vector<double> widths{120.0};
    wormedit::editor_field_scene_request_t request{.field_left_pixels = 10,
        .field_width_pixels = 200, .bgm_pane_left_pixels = 220,
        .bgm_pane_width_pixels = 300};
    request.bgm_lane_widths_pixels = widths;
    wormedit::editor_field_scene_window_t window;
    auto initial =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    if (!initial || window.bgm_lane_widths_snapshot != std::vector<double>{120.0} ||
        !window.request.bgm_lane_widths_pixels.empty()) return false;
    const auto first_rebuild = window.rebuild_count;
    widths[0] = 150.0;
    if (window.bgm_lane_widths_snapshot.front() != 120.0) return false;
    request.bgm_lane_widths_pixels = widths;
    auto changed =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    if (!changed || window.rebuild_count != first_rebuild + 1 ||
        window.bgm_lane_widths_snapshot != std::vector<double>{150.0}) return false;
    const auto second_rebuild = window.rebuild_count;
    auto reused = wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    if (!reused || window.rebuild_count != second_rebuild || window.reuse_count == 0) return false;

    widths.reserve(64);
    widths[0] = 180.0;
    request.bgm_lane_widths_pixels = widths;
    if (window.bgm_lane_widths_snapshot.front() != 150.0) return false;
    const auto reallocated =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    return reallocated && window.rebuild_count == second_rebuild + 1 &&
        window.bgm_lane_widths_snapshot == std::vector<double>{180.0} &&
        window.request.bgm_lane_widths_pixels.empty();
}
} // namespace

TEST_CASE("BGM scene window snapshots lane widths and invalidates on in-place changes",
          "[unit][core][editor-scene][scene-window][bgm-width]") {
    CHECK(bgm_width_window_snapshots_owned_values());
}

namespace {
void check_move_feedback_bodies(const wormedit::editor_field_scene_view_t& scene) {
    for (const auto& body : scene.hold_bodies) {
        if (body.proposal_ghost)
            CHECK(body.move_conflict == wormedit::note_move_conflict_t::occupied);
        if (body.note == wormedit::object_id_t{11})
            CHECK(body.move_conflict_occupant);
    }
}
auto move_feedback_scene(const wormedit::chart_document_t& chart,
                         std::vector<wormedit::note_move_preview_t> proposals,
                         std::vector<wormedit::object_id_t> occupants = {}, double scroll = 0,
                         double gutter = 0) -> wormedit::editor_field_scene_view_result_t {
    auto projected = view();
    projected.viewport_height_pixels = 480;
    projected.viewport_scroll_pixels = scroll;
    projected.selected_notes.clear();
    projected.selected_objects.clear();
    for (const auto& proposal : proposals) {
        projected.selected_notes.push_back(proposal.note);
        projected.selected_objects.push_back(
            {wormedit::editor_selection_object_kind_t::note, proposal.note});
    }
    projected.pending_note_move_gesture = wormedit::pending_note_move_gesture_t{
        .reference_note = proposals.front().note,
        .stamp = wormedit::document_stamp_t::from(7),
        .notes = projected.selected_notes,
        .preview_position = proposals.front().position,
        .preview = std::move(proposals),
        .occupants = std::move(occupants),
    };
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    return wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), projected,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .split_stage_gutter_pixels = gutter});
}
} // namespace

TEST_CASE("move scene carries conflicting hold ghosts and every occupant outline",
          "[unit][core][editor-scene][move][t420]") {
    auto chart = scene_chart();
    auto& source = chart.measures.front().notes.front();
    source.kind = wormedit::note_kind_t::hold;
    source.end = wormedit::local_position_t{wormedit::measure_id_t{1}, 480};
    const auto scene =
        move_feedback_scene(chart,
                            {{.note = wormedit::object_id_t{10},
                              .lane = wormedit::lane_id_t{2},
                              .position = {wormedit::measure_id_t{1}, 720},
                              .end = wormedit::local_position_t{wormedit::measure_id_t{2}, 480},
                              .conflict = wormedit::note_move_conflict_t::occupied,
                              .occupant = wormedit::object_id_t{11}}},
                            {wormedit::object_id_t{11}});
    REQUIRE(scene);
    CHECK(std::ranges::count_if(scene.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.proposal_ghost &&
                     endpoint.move_conflict == wormedit::note_move_conflict_t::occupied;
          }) == 2);
    CHECK(std::ranges::count_if(scene.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.move_conflict_occupant;
          }) == 2);
    check_move_feedback_bodies(*scene.scene);
}

TEST_CASE("move scene retains an unavailable target lane at its proposed display column",
          "[unit][core][editor-scene][move][t420]") {
    auto chart = scene_chart();
    chart.measures.front().notes.erase(chart.measures.front().notes.begin() + 1);
    chart.lane_layouts.back().layout.lanes.clear();
    const auto scene =
        move_feedback_scene(chart, {{.note = wormedit::object_id_t{10},
                                     .lane = wormedit::lane_id_t{},
                                     .position = {wormedit::measure_id_t{2}, 0},
                                     .conflict = wormedit::note_move_conflict_t::out_of_lane,
                                     .display_column = 1,
                                     .display_columns = 2}});
    REQUIRE(scene);
    const auto ghost = std::ranges::find_if(
        scene.scene->note_endpoints, [](const auto& endpoint) { return endpoint.proposal_ghost; });
    REQUIRE(ghost != scene.scene->note_endpoints.end());
    CHECK(ghost->move_conflict == wormedit::note_move_conflict_t::out_of_lane);
    CHECK(ghost->bounds.left == Catch::Approx(110));
    CHECK(ghost->bounds.right == Catch::Approx(210));
}

TEST_CASE("move scene displays out of chart holds without rejecting collapsed carriers",
          "[unit][core][editor-scene][move][boundary][t420]") {
    std::uint64_t source_tail = 120;
    SECTION("fully outside with both carriers at zero") {}
    SECTION("partly outside") {
        source_tail = 480;
    }
    auto chart = scene_chart();
    auto& source = chart.measures.front().notes.back();
    source.position = {wormedit::measure_id_t{1}, 0};
    source.end = wormedit::local_position_t{wormedit::measure_id_t{1}, source_tail};
    const auto carrier_end = source_tail > 240 ? source_tail - 240 : 0;
    const auto scene = move_feedback_scene(
        chart,
        {{.note = wormedit::object_id_t{10},
          .lane = wormedit::lane_id_t{1},
          .position = {wormedit::measure_id_t{1}, 0}},
         {.note = wormedit::object_id_t{11},
          .lane = wormedit::lane_id_t{2},
          .position = {wormedit::measure_id_t{1}, 0},
          .end = wormedit::local_position_t{wormedit::measure_id_t{1}, carrier_end},
          .conflict = wormedit::note_move_conflict_t::out_of_chart,
          .display_column = 1,
          .display_columns = 2,
          .display_begin_row = -240.0L,
          .display_end_row = static_cast<long double>(source_tail) - 240}},
        {}, -48);
    REQUIRE(scene);
    const auto body = std::ranges::find_if(
        scene.scene->hold_bodies, [](const auto& candidate) { return candidate.proposal_ghost; });
    REQUIRE(body != scene.scene->hold_bodies.end());
    CHECK(body->move_conflict == wormedit::note_move_conflict_t::out_of_chart);
    CHECK(body->bounds.bottom - body->bounds.top == Catch::Approx(source_tail * 0.2));
    CHECK(std::ranges::count_if(scene.scene->note_endpoints, [](const auto& endpoint) {
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_ghost;
          }) == 2);
}

TEST_CASE("move scene rejects malformed conflict metadata and nonfinite display rows",
          "[unit][core][editor-scene][move][negative][t420]") {
    const auto chart = scene_chart();
    wormedit::note_move_preview_t proposal{.note = wormedit::object_id_t{10},
                                           .lane = wormedit::lane_id_t{1},
                                           .position = {wormedit::measure_id_t{1}, 480}};
    SECTION("unknown conflict category") {
        proposal.conflict = static_cast<wormedit::note_move_conflict_t>(255);
    }
    SECTION("unknown occupant") {
        proposal.conflict = wormedit::note_move_conflict_t::occupied;
        proposal.occupant = wormedit::object_id_t{999};
    }
    SECTION("nonfinite raw display row") {
        proposal.conflict = wormedit::note_move_conflict_t::out_of_chart;
        proposal.display_column = 0;
        proposal.display_columns = 2;
        proposal.display_begin_row = std::numeric_limits<long double>::infinity();
    }
    CHECK_FALSE(move_feedback_scene(chart, {proposal}));
}

TEST_CASE("move missing columns use split-stage physical slots with sparse display orders",
          "[unit][core][editor-scene][move][t420]") {
    auto chart = scene_chart();
    chart.measures.front().notes.erase(chart.measures.front().notes.begin() + 1);
    auto& layout = chart.lane_layouts.front().layout;
    layout.split_stage = true;
    layout.split_after_column = 2;
    layout.lanes = {{.id = wormedit::lane_id_t{1},
                     .name = "L1",
                     .stage = wormedit::stage_group_t::left,
                     .display_order = 10},
                    {.id = wormedit::lane_id_t{2},
                     .name = "L2",
                     .stage = wormedit::stage_group_t::left,
                     .display_order = 20},
                    {.id = wormedit::lane_id_t{3},
                     .name = "R1",
                     .stage = wormedit::stage_group_t::right,
                     .display_order = 30},
                    {.id = wormedit::lane_id_t{4},
                     .name = "R2",
                     .stage = wormedit::stage_group_t::right,
                     .display_order = 40}};
    const auto scene =
        move_feedback_scene(chart,
                            {{.note = wormedit::object_id_t{10},
                              .lane = {},
                              .position = {wormedit::measure_id_t{1}, 480},
                              .conflict = wormedit::note_move_conflict_t::out_of_lane,
                              .display_column = 25,
                              .display_columns = 4}},
                            {}, 0, 20);
    REQUIRE(scene);
    const auto ghost = std::ranges::find_if(
        scene.scene->note_endpoints, [](const auto& endpoint) { return endpoint.proposal_ghost; });
    REQUIRE(ghost != scene.scene->note_endpoints.end());
    CHECK(ghost->bounds.left == Catch::Approx(120));
    CHECK(ghost->bounds.right == Catch::Approx(165));
}

TEST_CASE("move translated missing column follows the destination stage beyond the field",
          "[unit][core][editor-scene][move][t420]") {
    auto chart = scene_chart();
    chart.measures.front().notes.erase(chart.measures.front().notes.begin() + 1);
    auto& layout = chart.lane_layouts.front().layout;
    layout.split_stage = true;
    layout.split_after_column = 5;
    layout.lanes.clear();
    for (std::uint32_t column = 0; column < 10; ++column)
        layout.lanes.push_back(
            {.id = wormedit::lane_id_t{column + 1},
             .name = "Key",
             .stage = column < 5 ? wormedit::stage_group_t::left : wormedit::stage_group_t::right,
             .display_order = column});
    const auto scene =
        move_feedback_scene(chart,
                            {{.note = wormedit::object_id_t{10},
                              .lane = {},
                              .position = {wormedit::measure_id_t{1}, 480},
                              .conflict = wormedit::note_move_conflict_t::out_of_lane,
                              .display_column = 10,
                              .display_columns = 10}},
                            {}, 0, 20);
    REQUIRE(scene);
    const auto ghost = std::ranges::find_if(
        scene.scene->note_endpoints, [](const auto& endpoint) { return endpoint.proposal_ghost; });
    REQUIRE(ghost != scene.scene->note_endpoints.end());
    CHECK(ghost->bounds.left == Catch::Approx(210));
    CHECK(ghost->bounds.right == Catch::Approx(228));
}

TEST_CASE("move empty target band retains sparse source lane physical geometry",
          "[unit][core][editor-scene][move][t420]") {
    auto chart = scene_chart();
    auto& source = chart.measures.front().notes.back();
    source.kind = wormedit::note_kind_t::tap;
    source.end.reset();
    chart.lane_layouts.front().layout.lanes[0].display_order = 10;
    chart.lane_layouts.front().layout.lanes[1].display_order = 20;
    chart.lane_layouts.back().layout.lanes.clear();
    const auto scene =
        move_feedback_scene(chart, {{.note = wormedit::object_id_t{11},
                                     .lane = wormedit::lane_id_t{2},
                                     .position = {wormedit::measure_id_t{2}, 0},
                                     .conflict = wormedit::note_move_conflict_t::out_of_lane,
                                     .display_column = 20,
                                     .display_columns = 2}});
    REQUIRE(scene);
    const auto ghost = std::ranges::find_if(
        scene.scene->note_endpoints, [](const auto& endpoint) { return endpoint.proposal_ghost; });
    REQUIRE(ghost != scene.scene->note_endpoints.end());
    CHECK(ghost->bounds.left == Catch::Approx(110));
    CHECK(ghost->bounds.right == Catch::Approx(210));
}

TEST_CASE("field scene skips selected IDs that no longer exist instead of rejecting the view",
          "[unit][core][editor-scene][selection]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    auto stale = view();
    stale.selected_notes = {wormedit::object_id_t{11}, wormedit::object_id_t{900},
                            wormedit::object_id_t{901}};
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), stale,
        {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(result);
    CHECK(result.scene->visible_end_row > result.scene->visible_begin_row);
}

namespace {
auto scratch_geometry_chart(const wormedit::lane_input_profile_t& profile)
    -> wormedit::chart_document_t {
    auto chart = scene_chart();
    chart.lane_layouts.resize(1);
    chart.lane_layouts.front().layout = profile.layout;
    chart.input_profile = profile.input;
    for (auto& measure : chart.measures) measure.notes.clear();
    return chart;
}
auto scratch_geometry_view(const wormedit::chart_document_t& chart,
                           wormedit::editor_scratch_presentation_t sides)
    -> wormedit::editor_interaction_view_model_t {
    auto current = view();
    current.scratch_on_right = sides;
    current.active_lane = chart.lane_layouts.front().layout.lanes.front().id;
    current.selected_notes.clear();
    current.selected_objects.clear();
    return current;
}
auto scratch_geometry_index(const wormedit::chart_document_t& chart)
    -> wormedit::editor_field_scene_index_t {
    auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    return std::get<wormedit::editor_field_scene_index_t>(std::move(built));
}
void check_scratch_lane(const wormedit::editor_field_scene_view_t& scene, std::size_t slot,
                        wormedit::stage_group_t stage) {
    REQUIRE(scene.lanes.size() > slot);
    CHECK(scene.lanes[slot].role == wormedit::lane_role_t::scratch);
    CHECK(scene.lanes[slot].stage == stage);
}
}

TEST_CASE("Scratch presentation previews SP7 from the right without changing authored lanes",
          "[unit][core][editor-scene][scratch-presentation]") {
    const auto profile = wormedit::make_bms_7k_profile();
    auto chart = scratch_geometry_chart(profile);
    CHECK(wormedit::editor_presented_column(profile.layout, wormedit::lane_id_t{9999}, {}) == -1);
    const wormedit::editor_scratch_presentation_t sides{true, std::nullopt};
    const auto current = scratch_geometry_view(chart, sides);
    const auto index = scratch_geometry_index(chart);
    const auto result = wormedit::editor_field_scene_view_t::from(
        chart, index, current, {.field_width_pixels = 800, .scratch_on_right = sides});
    REQUIRE(result);
    check_scratch_lane(*result.scene, 7, wormedit::stage_group_t::left);
    CHECK(result.scene->lanes.front().role == wormedit::lane_role_t::playable);
    CHECK(chart.lane_layouts.front().layout.lanes.front().id == profile.layout.lanes.front().id);
    CHECK(chart.lane_layouts.front().layout.lanes.front().display_order == profile.layout.lanes.front().display_order);
}

TEST_CASE("Scratch presentation moves both DP5 scratches to each side's left and invalidates scene cache",
          "[unit][core][editor-scene][scratch-presentation]") {
    auto chart = scratch_geometry_chart(wormedit::make_bms_10k_standard_profile());
    const auto index = scratch_geometry_index(chart);
    auto current = scratch_geometry_view(chart, {});
    wormedit::editor_field_scene_request_t request{.field_width_pixels = 1212};
    wormedit::editor_field_scene_window_t window;
    REQUIRE(wormedit::editor_field_scene_view_t::from(chart, index, current, request, window));
    const auto rebuilds = window.rebuild_count;
    current.scratch_on_right = request.scratch_on_right = {false, false};
    CHECK_FALSE(wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window));
    const auto result =
        wormedit::editor_field_scene_view_t::from(chart, index, current, request, window);
    REQUIRE(result);
    check_scratch_lane(*result.scene, 0, wormedit::stage_group_t::left);
    check_scratch_lane(*result.scene, 6, wormedit::stage_group_t::right);
    CHECK(window.rebuild_count == rebuilds + 1);
}

TEST_CASE("field-scene index expected errors retain validation diagnostics", "[unit][core][editor-scene][results]") {
    auto chart = scene_chart();
    chart.id = {};
    const auto built = wormedit::editor_field_scene_index_t::from(chart, 7);
    REQUIRE_FALSE(built);
    REQUIRE(built.error().size() == 1);
    CHECK(built.error().front().code == wormedit::diagnostic_code_t::invalid_resolution);
    CHECK(built.error().front().message == "field-scene index limits and chart identity must be valid");
}

namespace {
auto has_warning_scene_adapter() -> bool {
    wormedit::editor_field_scene_view_outcome_t outcome{
        .value = wormedit::editor_field_scene_view_t{},
        .diagnostics = {{.severity = wormedit::diagnostic_severity_t::warning,
                         .code = wormedit::diagnostic_code_t::unsupported_feature,
                         .message = "warning"}}};
    if (!outcome.ok()) return false;
    auto legacy = wormedit::editor_field_scene_view_result_t::from(std::move(outcome));
    if (!legacy || legacy.has_errors()) return false;
    auto restored = std::move(legacy).into_outcome();
    return restored.value.has_value() && restored.ok() && restored.value->primitive_count() == 0 &&
        restored.diagnostics.size() == 1 && restored.diagnostics.front().message == "warning";
}
} // namespace
TEST_CASE("field-scene adapters preserve warning acceptance and scene presence", "[unit][core][editor-scene][results]") {
    CHECK(has_warning_scene_adapter());
}
