#pragma once
// Chart, lane and view builders shared by the editor-scene test files.
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/lane_profiles.h>

#include <string>

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
