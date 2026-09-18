#include <wormedit/core/editor_scene.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <variant>

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
                    .end = wormedit::local_position_t{wormedit::measure_id_t{2}, 240}}}},
        {.id = wormedit::measure_id_t{2}, .definition = {{1, 1}, wormedit::meter_t{4, 4}}},
    };
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
    const auto result = wormedit::make_editor_field_scene(
        chart, std::get<wormedit::editor_field_scene_index_t>(built), view(),
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
    CHECK(std::ranges::all_of(scene.note_endpoints,
                              [](const auto& endpoint) { return endpoint.selected; }));
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

TEST_CASE("field scene rejects stale indexes and returns no partial geometry at hard caps",
          "[unit][core][editor-scene][bounds]") {
    const auto chart = scene_chart();
    const auto too_small = wormedit::build_editor_field_scene_index(
        chart, 7, {.maximum_notes = 1, .maximum_layout_segments = 2});
    REQUIRE(std::holds_alternative<wormedit::diagnostic_list_t>(too_small));
    CHECK(has_code(std::get<wormedit::diagnostic_list_t>(too_small),
                   wormedit::diagnostic_code_t::invalid_resolution));

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
               endpoint.bounds.top == Catch::Approx(43.0);
    });
    const auto second =
        std::ranges::find_if(result.scene->note_endpoints, [](const auto& endpoint) {
            return endpoint.paint_preview && endpoint.lane == wormedit::lane_id_t{1} &&
                   endpoint.bounds.top == Catch::Approx(139.0);
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
        .preview = {{wormedit::object_id_t{11},
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
              return endpoint.note == wormedit::object_id_t{11} && endpoint.proposal_ghost;
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
        .placement_preview = wormedit::pending_hold_placement_preview_t{
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
    const auto capped = wormedit::make_editor_field_scene(
        chart, index, capped_view,
        {.field_left_pixels = 10.0,
         .field_width_pixels = 200.0,
         .maximum_scene_primitives = baseline_primitives});
    CHECK_FALSE(capped);
    CHECK_FALSE(capped.scene.has_value());
    CHECK(has_code(capped.diagnostics, wormedit::diagnostic_code_t::invalid_resolution));
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
                     endpoint.bounds.top == Catch::Approx(43.0);
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
