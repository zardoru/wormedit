#include "sys/workspace_view.h"
#include "sys/compose_view.h"
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <optional>

namespace {
auto bga_history() -> wormedit::document_history_t {
    using namespace wormedit;
    chart_document_t chart{.id = chart_id_t{7},
                           .divisions_per_standard_measure = 960,
                           .measures = {{.id = measure_id_t{1},
                                         .definition = {{1, 1}, meter_t{4, 4}}},
                                        {.id = measure_id_t{2},
                                         .definition = {{1, 1}, meter_t{4, 4}}}}};
    chart.bga_events = {
        {.id = object_id_t{11},
         .position = {measure_id_t{1}, 120},
         .resource = resource_id_t{31},
         .kind = visual_event_kind_t::background,
         .provenance = {.family = format_family_t::bms, .source_field = "04 slot 1",
                        .source_spelling = "#00104:01", .source_order = 4}},
        {.id = object_id_t{12},
         .position = {measure_id_t{1}, 240},
         .resource = resource_id_t{32},
         .kind = visual_event_kind_t::video},
        {.id = object_id_t{13},
         .position = {measure_id_t{1}, 360},
         .resource = resource_id_t{33},
         .kind = visual_event_kind_t::layer},
    };
    const auto profile = make_bms_5k_profile();
    chart.lane_layouts = {{.id = object_id_t{5},
        .position = {measure_id_t{1}, 0},
        .layout = profile.layout}};
    chart.bgm_source_lanes = {{.id = object_id_t{40}, .name = "BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{41},
                                          .position = {measure_id_t{1}, 480},
                                          .source_lane = object_id_t{40}}};
    chart.measures.front().notes = {{.id = object_id_t{51},
                                     .position = {measure_id_t{1}, 600},
                                     .lane = profile.layout.lanes.front().id}};
    song_document_t song{.id = song_id_t{1}, .revision = 7, .charts = {chart}};
    song.resources = {
        {.id = resource_id_t{31}, .name = "background", .asset = asset_id_t{61},
         .roles = {resource_role_t::bga}},
        {.id = resource_id_t{32}, .name = "video", .asset = asset_id_t{62},
         .roles = {resource_role_t::bga}},
        {.id = resource_id_t{33}, .name = "layer", .asset = asset_id_t{63},
         .roles = {resource_role_t::bga}},
        {.id = resource_id_t{34}, .name = "replacement", .asset = asset_id_t{64},
         .roles = {resource_role_t::bga}},
    };
    song.visual_assets = {{.id = asset_id_t{61}, .kind = asset_kind_t::image},
                          {.id = asset_id_t{62}, .kind = asset_kind_t::video},
                          {.id = asset_id_t{63}, .kind = asset_kind_t::image},
                          {.id = asset_id_t{64}, .kind = asset_kind_t::image}};
    return document_history_t::from(std::move(song));
}

void check_projection(bool condition) { CHECK(condition); }

void bga_scene_projection_is_ordered() {
    using namespace wormedit;
    using namespace wormedit::sys;
    auto history = bga_history();
    auto binding = workspace_controller_t::from(history);
    if (!binding.value) { check_projection(false); return; }
    binding.value->active_tab = workspace_tab_t::compose;
    binding.value->selected_chart = chart_id_t{7};
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    populate_compose_view(flattened, history, *binding.value, cache, 900, 600,
                          interaction, application, nullptr);
    const auto& view = flattened.view.compose;
    check_projection(view.scene_available);
    check_projection(view.bga_lane_visible);
    check_projection(view.bga_lane_bounds.right == view.playable_field_left_pixels);
    check_projection(view.bga_lane_bounds.right - view.bga_lane_bounds.left == 96.0);
    check_projection(view.bga_lane_count == 2);
    check_projection(view.bga_events.size() == 3);
    for (std::size_t index = 0; index < view.bga_events.size(); ++index) {
        check_projection(view.bga_events[index].bounds.left >= view.bga_lane_bounds.left);
        check_projection(view.bga_events[index].bounds.right <= view.bga_lane_bounds.right);
    }
}
}

TEST_CASE("BGA scene projection places its lane between timing and playable fields",
          "[unit][sys][bga-lane][horizontal-layout]") {
    bga_scene_projection_is_ordered();
}
