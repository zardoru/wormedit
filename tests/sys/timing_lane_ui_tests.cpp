#include "sys/compose_view_internal.h"
#include <wormedit/core/timing_lane_catalog.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace wormedit;
using namespace wormedit::sys::compose_projection_detail;

TEST_CASE("Marker selection and area geometry agree for all timing lane families",
          "[sys][timing-lane-identity][timing-geometry]") {
    const auto& lane = timing_lanes[GENERATE(0, 1, 2, 3, 4, 5, 6, 7)];
    const editor_selection_object_t object{lane.selection_kind, object_id_t{77}};
    editor_interaction_view_model_t interaction{};
    interaction.selected_objects = {object};
    CHECK(compose_timing_marker_selected(interaction, lane.kind, 77));
    const WormUiTimingMarkerView marker{.object_id = 77, .marker_y_pixels = 300,
        .marker_right_fraction = 1, .lane = lane.kind};
    const WormUiComposeView view{.viewport_height_pixels = 600,
        .playable_field_left_pixels = 426,
        .timing_markers = &marker, .timing_marker_count = 1, .timing_lane_visibility_mask = 0xff};
    editor_area_geometry_snapshot_t area;
    append_area_timing_geometry(area, view, false);
    REQUIRE(area.objects.size() == 1);
    CHECK(area.objects.front().object == object);
}
