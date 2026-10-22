#include "sys/compose/views/compose_view_internal.h"
#include <wormedit/core/timing_lane_catalog.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <tuple>

using namespace wormedit;
using namespace wormedit::sys::compose_projection_detail;

TEST_CASE("Marker selection and area geometry agree for all timing lane families",
          "[sys][timing-lane-identity][timing-geometry]") {
    const auto& lane = timing_lanes[GENERATE(0, 1, 2, 3, 4, 5, 6, 7)];
    const editor_selection_object_t object{lane.selection_kind, object_id_t{77}};
    editor_interaction_view_model_t interaction{};
    interaction.selected_objects = {object};
    CHECK(compose_timing_marker_selected(interaction, lane.kind, 77));
    const wormedit::ui::compose_timing_marker_view_t marker{.object_id = {77}, .marker_y_pixels = 300,
        .marker_right_fraction = 1, .lane = lane.kind};
    const wormedit::ui::compose_view_t view{.viewport_height_pixels = 600,
        .playable_field_left_pixels = 426,
        .timing_markers = {marker}, .timing_lane_visibility_mask = 0xff};
    editor_area_geometry_snapshot_t area;
    append_area_timing_geometry(area, view, false);
    REQUIRE(area.objects.size() == 1);
    CHECK(area.objects.front().object == object);
}

TEST_CASE("Retained timing graphs reset their visible range between refills",
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
    CHECK(graph.points.front().x == 0.5);
    CHECK((std::tuple{graph.minimum, graph.maximum} == std::tuple{4.0, 4.0}));
    build_sv_graph(steps, 200, 200, y_of, graph);
    CHECK((std::tuple{graph.points.empty(), graph.minimum, graph.maximum, graph.one_x} ==
           std::tuple{true, 1.0, 1.0, -1.0}));
}
