#include "sys/timing_lane_ui.h"
#include "sys/compose_view_internal.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>

using namespace wormedit;
using namespace wormedit::sys;
using namespace wormedit::sys::compose_projection_detail;

namespace {
struct timing_lane_case_t {
    WormUiTimingLaneKind ui;
    timing_lane_kind_t core;
    editor_selection_object_kind_t selection;
    auto operator==(const timing_lane_case_t&) const -> bool = default;
};
constexpr std::array timing_lane_cases{
    timing_lane_case_t{WORM_UI_TIMING_LANE_BPM, timing_lane_kind_t::bpm, editor_selection_object_kind_t::bpm_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_SCROLL, timing_lane_kind_t::scroll, editor_selection_object_kind_t::scroll_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_SPEED, timing_lane_kind_t::speed, editor_selection_object_kind_t::speed_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_STOP, timing_lane_kind_t::stop, editor_selection_object_kind_t::stop_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_DELAY, timing_lane_kind_t::delay, editor_selection_object_kind_t::delay_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_WARP, timing_lane_kind_t::warp, editor_selection_object_kind_t::warp_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_KIAI, timing_lane_kind_t::kiai, editor_selection_object_kind_t::kiai_event},
    timing_lane_case_t{WORM_UI_TIMING_LANE_ANCHOR, timing_lane_kind_t::anchor, editor_selection_object_kind_t::timing_anchor},
};
}

TEST_CASE("Every timing lane crosses the UI boundary with its catalog identity",
          "[sys][timing-lane-identity]") {
    const auto expected = timing_lane_cases[GENERATE(0, 1, 2, 3, 4, 5, 6, 7)];
    const auto* descriptor = ui_codec<WormUiTimingLaneKind>::descriptor(expected.ui);
    REQUIRE(descriptor);
    CHECK(ui_codec<WormUiTimingLaneKind>::into(expected.ui) == expected.core);
    CHECK(ui_codec<WormUiTimingLaneKind>::from(expected.core) == expected.ui);
    CHECK(descriptor->kind == expected.core);
    CHECK(descriptor->selection_kind == expected.selection);
}

TEST_CASE("Marker selection and area geometry agree for all timing lane families",
          "[sys][timing-lane-identity][timing-geometry]") {
    const auto expected = timing_lane_cases[GENERATE(0, 1, 2, 3, 4, 5, 6, 7)];
    const editor_selection_object_t object{expected.selection, object_id_t{77}};
    editor_interaction_view_model_t interaction{};
    interaction.selected_objects = {object};
    CHECK(compose_timing_marker_selected(interaction, expected.ui, 77));
    const WormUiTimingMarkerView marker{.object_id = 77, .marker_y_pixels = 300,
        .marker_right_fraction = 1, .lane = expected.ui};
    const WormUiComposeView view{.viewport_height_pixels = 600,
        .playable_field_left_pixels = 426,
        .timing_markers = &marker, .timing_marker_count = 1, .timing_lane_visibility_mask = 0xff};
    editor_area_geometry_snapshot_t area;
    append_area_timing_geometry(area, view, false);
    REQUIRE(area.objects.size() == 1);
    CHECK(area.objects.front().object == object);
}

TEST_CASE("Unknown timing lane inputs reject while projection keeps its BPM fallback",
          "[sys][timing-lane-identity]") {
    const auto invalid = GENERATE(WORM_UI_TIMING_LANE_COUNT,
        static_cast<WormUiTimingLaneKind>(99), static_cast<WormUiTimingLaneKind>(-1));
    CHECK_FALSE(ui_codec<WormUiTimingLaneKind>::into(invalid));
    CHECK(ui_codec<WormUiTimingLaneKind>::descriptor(invalid) == nullptr);
    CHECK(ui_codec<WormUiTimingLaneKind>::from(static_cast<timing_lane_kind_t>(99)) == WORM_UI_TIMING_LANE_BPM);
}
