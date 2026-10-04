#include <wormedit/core/editor_scene.h>
#include <wormedit/core/lane_profiles.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("field scene composes split layout bands, exact grid, holds, selection, and cursor",
          "[unit][core][editor-scene]") {
    const auto chart = scene_chart();
    const auto built = wormedit::build_editor_field_scene_index(chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(built));
    const auto& indexed = std::get<wormedit::editor_field_scene_index_t>(built);
    REQUIRE(indexed.notes.size() == 2);
    CHECK_FALSE(indexed.notes[0].sound);
    CHECK(indexed.notes[1].sound == wormedit::resource_id_t{40});
    REQUIRE(indexed.bgm_events.size() == 1);
    CHECK(indexed.bgm_events.front().event == wormedit::object_id_t{12});
    CHECK(indexed.bgm_events.front().source_lane == wormedit::object_id_t{30});
    CHECK(indexed.bgm_events.front().row == 480);
    const auto result = wormedit::make_editor_field_scene(
        chart, indexed, view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .split_stage_gutter_pixels = 12.0,
         .comparison_difference_rows = {{wormedit::measure_id_t{2}, 0}}});
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
    const auto zoomed = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), zoomed_view,
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
    const auto scene = wormedit::make_editor_field_scene(
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
        const auto result = wormedit::make_editor_field_scene(
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
        return wormedit::make_editor_field_scene(
            chart, index, visible,
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
    const auto projected = wormedit::make_editor_field_scene(chart, indexed, visible, request);
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
    const auto with_hold = wormedit::make_editor_field_scene(chart, indexed, visible, request);
    REQUIRE(with_hold);
    CHECK(std::ranges::any_of(with_hold.scene->hold_bodies, [](const auto& body) {
        return body.proposal_ghost && body.bounds.top == Catch::Approx(0.0) &&
               body.bounds.bottom == Catch::Approx(48.0);
    }));
    auto authored_only = request;
    authored_only.include_virtual_bms_tail = false;
    CHECK_FALSE(wormedit::make_editor_field_scene(chart, indexed, visible, authored_only));
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
    const auto projected = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), visible,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .include_virtual_bms_tail = true});
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
        .document_revision = 7,
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
    const auto projected = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), visible,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .include_virtual_bms_tail = true});
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
    const auto result = wormedit::make_editor_field_scene(
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

    const auto solo = wormedit::make_editor_field_scene(
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

    const auto unknown_solo = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .solo_semantic_layer = wormedit::layer_id_t{99}});
    CHECK_FALSE(unknown_solo);
    CHECK(has_code(unknown_solo.diagnostics, wormedit::diagnostic_code_t::invalid_id));

    auto filter_view = view();
    filter_view.viewport_scroll_pixels = 0.0;
    filter_view.viewport_height_pixels = 384.0;
    const auto required = wormedit::make_editor_field_scene(
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

    const auto excluded = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), filter_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .excluded_semantic_layers = {wormedit::layer_id_t{1}}});
    REQUIRE(excluded);
    REQUIRE(excluded.scene->note_endpoints.size() == 1);
    CHECK(excluded.scene->note_endpoints.front().note == wormedit::object_id_t{10});
    CHECK(excluded.scene->hold_bodies.empty());

    const auto filtered = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .required_semantic_layers = {wormedit::layer_id_t{2}, wormedit::layer_id_t{3}},
         .excluded_semantic_layers = {wormedit::layer_id_t{1}}});
    REQUIRE(filtered);
    CHECK(filtered.scene->note_endpoints.empty());
    CHECK(filtered.scene->hold_bodies.empty());

    const auto contradictory = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .notes = {wormedit::object_id_t{11}},
        .preview_position = {wormedit::measure_id_t{2}, 240},
        .preview = {{wormedit::object_id_t{11}, wormedit::lane_id_t{2},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 720}}},
    };
    const auto moved = wormedit::make_editor_field_scene(
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
    const auto secondary = wormedit::make_editor_field_scene(
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

    const auto result = wormedit::make_editor_field_scene(
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

    const auto disabled = wormedit::make_editor_field_scene(
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
    const auto capped = wormedit::make_editor_field_scene(
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

    const auto invalid_geometry = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), scene_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .bgm_pane_left_pixels = 220.0,
         .bgm_pane_width_pixels = -1.0});
    CHECK_FALSE(invalid_geometry);
    CHECK_FALSE(invalid_geometry.scene.has_value());

    const auto excessive_scroll = wormedit::make_editor_field_scene(
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
    const auto stale = wormedit::make_editor_field_scene(
        chart, index, view(8), {.field_left_pixels = 0, .field_width_pixels = 200});
    CHECK_FALSE(stale);
    CHECK_FALSE(stale.scene.has_value());

    const auto capped = wormedit::make_editor_field_scene(
        chart, index, view(),
        {.field_left_pixels = 0, .field_width_pixels = 200, .maximum_scene_primitives = 1});
    CHECK_FALSE(capped);
    CHECK_FALSE(capped.scene.has_value());
    CHECK(has_code(capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));

    const auto work_capped = wormedit::make_editor_field_scene(
        chart, index, view(),
        {.field_left_pixels = 0, .field_width_pixels = 200, .maximum_composition_operations = 1});
    CHECK_FALSE(work_capped);
    CHECK_FALSE(work_capped.scene.has_value());

    const auto bad_comparison = wormedit::make_editor_field_scene(
        chart, index, view(),
        {.field_left_pixels = 0,
         .field_width_pixels = 200,
         .comparison_difference_rows = {{wormedit::measure_id_t{999}, 0}}});
    CHECK_FALSE(bad_comparison);
    CHECK_FALSE(bad_comparison.scene.has_value());

    const auto duplicate_comparison = wormedit::make_editor_field_scene(
        chart, index, view(),
        {.field_left_pixels = 0,
         .field_width_pixels = 200,
         .comparison_difference_rows = {{wormedit::measure_id_t{1}, 720},
                                        {wormedit::measure_id_t{1}, 720}}});
    CHECK_FALSE(duplicate_comparison);
    CHECK(has_code(duplicate_comparison.diagnostics, wormedit::diagnostic_code_t::collision));

    const auto comparison_capped = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 720}},
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{2}, 240}},
            }),
    };

    const auto result = wormedit::make_editor_field_scene(
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
    stale_view.pending_note_paint_gesture->document_revision = 8;
    const auto stale = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .notes = {wormedit::object_id_t{11}},
        .preview_end = {wormedit::measure_id_t{2}, 0},
        .preview = {{wormedit::object_id_t{11}, wormedit::note_kind_t::hold,
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 0}}},
    };
    const auto duration = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .notes = {wormedit::object_id_t{11}},
        .preview_position = {wormedit::measure_id_t{2}, 240},
        .preview = {{wormedit::object_id_t{11}, wormedit::lane_id_t{2},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
                     wormedit::local_position_t{wormedit::measure_id_t{2}, 720}}},
    };
    const auto moved = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
    };
    const auto hold = wormedit::make_editor_field_scene(
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
    const auto reverse_hold = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
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
    const auto merged_hold = wormedit::make_editor_field_scene(
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
    stale_view.pending_note_duration_gesture->document_revision = 8;
    const auto stale = wormedit::make_editor_field_scene(
        chart, index, stale_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    CHECK_FALSE(stale);
    CHECK_FALSE(stale.scene.has_value());
    CHECK(has_code(stale.diagnostics, wormedit::diagnostic_code_t::invalid_hold));

    const auto baseline = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
    };
    const auto capped =
        wormedit::make_editor_field_scene(chart, index, capped_view,
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
        .document_revision = 7,
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
    };
    preview.additional_keyboard_holds.push_back(wormedit::pending_hold_t{
        .position = {wormedit::measure_id_t{2}, 0},
        .lane = wormedit::lane_id_t{2},
        .document_revision = 7,
        .preview_end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240},
    });
    const auto scene = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 240}},
            }),
    };
    const auto occupied = wormedit::make_editor_field_scene(
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
        .document_revision = 7,
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{2}, {wormedit::measure_id_t{1}, 840}},
            }),
    };
    const auto collision = wormedit::make_editor_field_scene(
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
    const auto baseline = wormedit::make_editor_field_scene(
        chart, index, base_view, {.field_left_pixels = 10.0, .field_width_pixels = 200.0});
    REQUIRE(baseline);
    const auto base_primitives =
        baseline.scene->layout_bands.size() + baseline.scene->lanes.size() +
        baseline.scene->comparison_differences.size() + baseline.scene->grid_lines.size() +
        baseline.scene->hold_bodies.size() + baseline.scene->note_endpoints.size() +
        (baseline.scene->cursor.has_value() ? 1U : 0U);
    auto empty_cell_view = base_view;
    empty_cell_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .document_revision = 7,
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 720}},
            }),
    };
    const auto primitive_capped =
        wormedit::make_editor_field_scene(chart, index, empty_cell_view,
                                          {.field_left_pixels = 10.0,
                                           .field_width_pixels = 200.0,
                                           .maximum_scene_primitives = base_primitives});
    CHECK_FALSE(primitive_capped);
    CHECK_FALSE(primitive_capped.scene.has_value());
    CHECK(has_code(primitive_capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));

    auto operation_view = base_view;
    operation_view.pending_note_paint_gesture = wormedit::note_paint_gesture_preview_t{
        .document_revision = 7,
        .targets = std::make_shared<const std::vector<wormedit::note_paint_target_t>>(
            std::vector<wormedit::note_paint_target_t>{
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{1}, 720}},
                {wormedit::lane_id_t{1}, {wormedit::measure_id_t{2}, 240}},
            }),
    };
    const auto operation_capped =
        wormedit::make_editor_field_scene(chart, index, operation_view,
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
    const auto result = wormedit::make_editor_field_scene(
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
void check_window_grid_line(const wormedit::editor_field_grid_geometry_t& a,
                            const wormedit::editor_field_grid_geometry_t& e) {
    CHECK(std::tie(a.chart_row, a.kind, a.emphasis) == std::tie(e.chart_row, e.kind, e.emphasis));
    CHECK(a.y_pixels == Catch::Approx(e.y_pixels).margin(1e-9));
}
void check_window_cursor(const wormedit::editor_field_scene_t& actual,
                         const wormedit::editor_field_scene_t& expected) {
    REQUIRE(actual.cursor.has_value() == expected.cursor.has_value());
    if (actual.cursor) {
        CHECK(actual.cursor->lane == expected.cursor->lane);
        check_window_bounds(actual.cursor->bounds, expected.cursor->bounds);
    }
}
void check_window_scene(const wormedit::editor_field_scene_t& actual,
                        const wormedit::editor_field_scene_t& expected) {
    CHECK(actual.visible_begin_row == expected.visible_begin_row);
    CHECK(actual.visible_end_row == expected.visible_end_row);
    check_window_geometry(actual.layout_bands, expected.layout_bands, check_window_band);
    check_window_geometry(actual.lanes, expected.lanes, check_window_lane);
    check_window_geometry(actual.note_endpoints, expected.note_endpoints, check_window_endpoint);
    check_window_geometry(actual.hold_bodies, expected.hold_bodies, check_window_hold);
    check_window_geometry(actual.bgm_lanes, expected.bgm_lanes, check_window_bgm_lane);
    check_window_geometry(actual.bgm_events, expected.bgm_events, check_window_bgm_event);
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
void check_reused_frame(const wormedit::chart_document_t& chart,
                        const wormedit::editor_field_scene_index_t& index,
                        const wormedit::editor_interaction_view_model_t& current,
                        const wormedit::editor_field_scene_request_t& request,
                        wormedit::editor_field_scene_window_t& window) {
    const auto reused = wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window);
    const auto fresh = wormedit::make_editor_field_scene(chart, index, current, request);
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
void check_window_rejection(const wormedit::editor_field_scene_result_t& reused,
                            const wormedit::editor_field_scene_result_t& fresh) {
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
    const auto initial = wormedit::make_editor_field_scene(chart, index, current, request);
    REQUIRE(initial);
    const auto& scene = *initial.scene;
    request.maximum_scene_primitives = scene.layout_bands.size() + scene.lanes.size() +
        scene.grid_lines.size() + scene.note_endpoints.size();
    wormedit::editor_field_scene_window_t window;
    REQUIRE(wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window));
    REQUIRE(window.scene);
    current.active_lane = wormedit::lane_id_t{1};
    const auto reused = wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window);
    const auto fresh = wormedit::make_editor_field_scene(chart, index, current, request);
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
            wormedit::make_editor_field_scene_reusing_window(chart, index, current, request,
                                                             window),
            "wormedit::make_editor_field_scene_reusing_window(chart, index, current, request,      "
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
            const auto metadata = wormedit::project_editor_field_scene_window_metadata(
                chart, index, current, request, window);
            const auto fresh = wormedit::make_editor_field_scene(chart, index, current, request);
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
        wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window),
        "         wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, "
        "window)");
    current.selected_notes.clear();
    check_metadata_false(
        wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window),
        "         wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, "
        "window)");
    check_metadata_false(
        wormedit::project_editor_field_scene_window_metadata(chart, index, current, request, window)
            .projection,
        "         wormedit::project_editor_field_scene_window_metadata(chart, index, current, "
        "request, window)             .projection");
    current = view();
    request.comparison_difference_rows = {{wormedit::measure_id_t{1}, 240}};
    check_metadata_false(
        wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window),
        "         wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, "
        "window)");
    check_metadata_false(
        wormedit::project_editor_field_scene_window_metadata(chart, index, current, request, window)
            .projection,
        "         wormedit::project_editor_field_scene_window_metadata(chart, index, current, "
        "request, window)             .projection");
    check_metadata(wormedit::make_editor_field_scene(chart, index, current, request),
                   "wormedit::make_editor_field_scene(chart, index, current, request)");
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
        wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window),
        "         wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, "
        "window)");
    current.viewport_height_pixels = -1;
    check_metadata_false(
        wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, window),
        "         wormedit::can_reuse_editor_field_scene_window(chart, index, current, request, "
        "window)");
    check_metadata_false(
        wormedit::project_editor_field_scene_window_metadata(chart, index, current, request, window)
            .projection,
        "         wormedit::project_editor_field_scene_window_metadata(chart, index, current, "
        "request, window)             .projection");
    current = view();
    request.maximum_scene_primitives = 1;
    check_metadata_false(
        wormedit::project_editor_field_scene_window_metadata(chart, index, current, request, window)
            .projection,
        "         wormedit::project_editor_field_scene_window_metadata(chart, index, current, "
        "request, window)             .projection");
    check_metadata_false(
        wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window),
        "         wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, "
        "window)");
    check_metadata_false(window.scene, "window.scene");
}

namespace {
void expect_rejected_anchor_scene(const wormedit::chart_document_t& chart,
    const wormedit::editor_field_scene_index_t& index,
    const wormedit::editor_interaction_view_model_t& current,
    const wormedit::editor_field_scene_request_t& request,
    wormedit::editor_field_scene_window_t& window) {
    CHECK_FALSE(wormedit::make_editor_field_scene(chart, index, current, request));
    CHECK_FALSE(wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window));
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
    REQUIRE(wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window));
    current.viewport_scroll_pixels = -current.viewport_height_pixels * 0.35;
    CHECK_FALSE(wormedit::make_editor_field_scene(chart, index, current, request));
    CHECK_FALSE(wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window));
    current.viewport_scroll_pixels = 0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const auto [minimum, end] : {std::pair{0.19, 0.2}, std::pair{1.01, 0.2},
                                    std::pair{0.2, -0.01}, std::pair{0.2, 0.21},
                                    std::pair{nan, 0.2}, std::pair{0.2, nan}, std::pair{infinity, 0.2}}) {
        current.viewport_minimum_margin_fraction = minimum;
        current.viewport_end_margin_fraction = end;
        CHECK_FALSE(wormedit::make_editor_field_scene(chart, index, current, request));
        CHECK_FALSE(wormedit::make_editor_field_scene_reusing_window(chart, index, current, request, window));
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
    const auto result = make_editor_field_scene(
        chart, std::get<editor_field_scene_index_t>(built), dp_view,
        {.field_left_pixels = 10.0, .field_width_pixels = 400.0,
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
