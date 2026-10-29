#pragma once

#include <wormedit/core/document.h>
#include <type_traits>

namespace wormedit {

// Structural roles, not timing-link categories. Local sample controls must never
// become linked timing merely because their positions participate in geometry.
enum class chart_collection_position_t {
    none, layout_point, timing_point, local_point, visual_point,
    span, optional_point, optional_span, nested
};
template<chart_collection_position_t Position>
using chart_collection_role_t = std::integral_constant<chart_collection_position_t, Position>;

// Flat object_id_t declarations only. References, other identity domains and
// nested declarations retain their explicit owners. The callback may stop early.
// This is the membership owner for allocation, labels, focus and point geometry.
template<class Chart, class Visitor>
auto visit_chart_collections(Chart& chart, Visitor&& visit) -> bool {
    using enum chart_collection_position_t;
    return visit(chart.bgm_source_lanes, chart_collection_role_t<none>{}) &&
        visit(chart.lane_layouts, chart_collection_role_t<layout_point>{}) &&
        visit(chart.bpms, chart_collection_role_t<timing_point>{}) &&
        visit(chart.pauses, chart_collection_role_t<timing_point>{}) &&
        visit(chart.warps, chart_collection_role_t<span>{}) &&
        visit(chart.scrolls, chart_collection_role_t<timing_point>{}) &&
        visit(chart.volumes, chart_collection_role_t<local_point>{}) &&
        visit(chart.sample_sets, chart_collection_role_t<local_point>{}) &&
        visit(chart.speeds, chart_collection_role_t<timing_point>{}) &&
        visit(chart.kiai, chart_collection_role_t<timing_point>{}) &&
        visit(chart.timing_anchors, chart_collection_role_t<timing_point>{}) &&
        visit(chart.bookmarks, chart_collection_role_t<optional_point>{}) &&
        visit(chart.sections, chart_collection_role_t<optional_span>{}) &&
        visit(chart.bga_events, chart_collection_role_t<visual_point>{}) &&
        visit(chart.sound_channels, chart_collection_role_t<nested>{});
}

// Chart-level point events, excluding layout segments. Buckets, optional marker
// positions and spans have different endpoint rules and are handled by callers.
template<class Chart, class Visitor>
auto visit_chart_point_collections(Chart& chart, Visitor&& visit) -> bool {
    return visit_chart_collections(chart, [&](auto& values, auto role) {
        using enum chart_collection_position_t;
        if constexpr (role == timing_point || role == local_point || role == visual_point)
            return visit(values, role);
        else return true;
    });
}

} // namespace wormedit
