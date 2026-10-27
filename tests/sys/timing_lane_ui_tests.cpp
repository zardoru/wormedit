#include "sys/app/application_timing_stage.h"
#include "sys/compose/views/compose_view_internal.h"
#include <wormedit/core/timing_lane_catalog.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <tuple>

using namespace wormedit;
using namespace wormedit::sys::compose_projection_detail;

TEST_CASE("Marker selection and area geometry agree for all timing lane families",
          "[sys][timing-lane-identity][timing-geometry]") {
    const auto& lane = timing_lanes[GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9)];
    const editor_selection_object_t object{lane.selection_kind, object_id_t{77}};
    editor_interaction_view_model_t interaction{};
    interaction.selected_objects = {object};
    CHECK(compose_timing_marker_selected(interaction, lane.kind, 77));
    const wormedit::ui::compose_timing_marker_view_t marker{.object_id = {77}, .marker_y_pixels = 300,
        .marker_right_fraction = 1, .lane = lane.kind};
    const wormedit::ui::compose_view_t view{.viewport_height_pixels = 600,
        .playable_field_left_pixels = 426,
        .timing_markers = {marker}, .timing_lane_visibility_mask = all_timing_lane_mask};
    editor_area_geometry_snapshot_t area;
    append_area_timing_geometry(area, view, false);
    REQUIRE(area.objects.size() == 1);
    CHECK(area.objects.front().object == object);
}

TEST_CASE("Retained timing graphs preserve their chart range between refills",
          "[sys][timing-geometry][retained-view]") {
    using namespace wormedit::sys;
    const auto y_of = [](auto row) { return static_cast<double>(row); };
    sv_steps_view_t steps;
    steps.steps = {{0, 2.0, {}}, {100, 4.0, {}}};
    sv_graph_t graph;
    build_sv_graph(steps, 0, 200, y_of, graph);
    CHECK((std::tuple{graph.minimum, graph.maximum, graph.one_x} ==
           std::tuple{2.0, 4.0, -1.0}));
    const auto* points = graph.points.data();
    build_sv_graph(steps, 100, 200, y_of, graph);
    CHECK(graph.points.data() == points);
    REQUIRE(graph.points.size() == 2);
    CHECK(graph.points.front().x == 1.0);
    CHECK((std::tuple{graph.minimum, graph.maximum} == std::tuple{2.0, 4.0}));
    build_sv_graph(steps, 200, 200, y_of, graph);
    CHECK((std::tuple{graph.points.empty(), graph.minimum, graph.maximum, graph.one_x} ==
           std::tuple{true, 1.0, 1.0, -1.0}));
}

TEST_CASE("Sample-control draw requests enter the existing timing proposal transaction",
          "[sys][sample-controls][timing-stage]") {
    using namespace wormedit::sys;
    auto history = document_history_t::from(song_document_t{.id = {1}, .revision = 1,
        .charts = {{.id = {2}, .divisions_per_standard_measure = 960,
            .measures = {{.id = {3}}},
            .bpms = {{.id = {10}, .position = {{3}, 0}, .beats_per_minute = {120, 1}}}}}});
    workspace_controller_t workspace;
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{2};
    const ui::workspace_action_t action{.timing_stage_action = ui::timing_stage_action_t::quick_insert,
        .timing_chart_id = {2}, .timing_document_revision = 1,
        .timing_lane = timing_lane_kind_t::sample_set,
        .timing_has_target_y = true,
        .timing_target = {.source_document_fraction = .5},
        .timing_primary_numerator_text = "3"};
    auto proposal = begin_timing_stage(action, history, workspace, false, 4);
    REQUIRE(proposal.value);
    REQUIRE(apply_proposal(history, proposal, false));
    const auto& points = history.document.charts.front().sample_sets;
    REQUIRE(points.size() == 1);
    CHECK(points.front().value == 3);
    CHECK(points.front().position == local_position_t{{3}, 480});
    CHECK(history.undo.size() == 1);
}
