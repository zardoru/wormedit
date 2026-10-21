#pragma once

#include "ui/frontend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace wormedit::ui {

inline constexpr double compose_measure_rail_width = 42.0;
// Numeric lanes fit a sign, five significant digits and a decimal at the
// normal field font. Boolean Kiai and Anchor lanes keep their compact width.
inline constexpr std::array<double, wormedit::timing_lane_kind_count> compose_timing_lane_widths{
    64, 64, 64, 48, 48, 48, 24, 24};

// Pointer targets and slice-point tiles use the head; area selection keeps the full tail.
inline auto compose_bgm_head_bounds(const wormedit::ui::compose_bgm_event_view_t& event) -> wormedit::ui::compose_rect_t {
    auto bounds = event.bounds;
    if (event.has_tail) {
        bounds.top = event.head_top_pixels;
        bounds.bottom = event.head_bottom_pixels;
    }
    return bounds;
}

inline auto compose_timing_width(std::uint8_t mask) -> double {
    double width{};
    for (std::size_t index = 0; index < compose_timing_lane_widths.size(); ++index)
        if (mask & (1U << index)) width += compose_timing_lane_widths[index];
    return width;
}

inline auto compose_timing_right(const WormUiComposeView& view) -> double {
    return view.bga_lane_visible ? view.bga_lane_bounds.left : view.playable_field_left_pixels;
}

// One horizontal partition for projection, picking, markers and span gestures.
// Narrow surfaces compress the partition proportionally to preserve the field.
inline auto compose_timing_lane_extent(const WormUiComposeView& view, wormedit::timing_lane_kind_t lane)
    -> std::optional<std::pair<double, double>> {
    const auto ordinal = static_cast<unsigned>(lane);
    if (ordinal >= compose_timing_lane_widths.size() ||
        (view.timing_lane_visibility_mask & (1U << ordinal)) == 0) return std::nullopt;
    const auto right = compose_timing_right(view);
    if (!std::isfinite(right) || right <= compose_measure_rail_width) return std::nullopt;
    const auto scale = (right - compose_measure_rail_width) /
                       compose_timing_width(view.timing_lane_visibility_mask);
    double left = compose_measure_rail_width;
    for (unsigned index = 0; index < ordinal; ++index)
        if (view.timing_lane_visibility_mask & (1U << index))
            left += compose_timing_lane_widths[index] * scale;
    return std::pair{left, std::min(right, left + compose_timing_lane_widths[ordinal] * scale)};
}

inline auto compose_timing_lane_at(const WormUiComposeView& view, double x)
    -> std::optional<wormedit::timing_lane_kind_t> {
    if (!std::isfinite(x) || x < compose_measure_rail_width || x >= compose_timing_right(view))
        return std::nullopt;
    for (unsigned ordinal = 0; ordinal < wormedit::timing_lane_kind_count; ++ordinal) {
        const auto lane = static_cast<wormedit::timing_lane_kind_t>(ordinal);
        const auto extent = compose_timing_lane_extent(view, lane);
        if (extent && x >= extent->first && x < extent->second) return lane;
    }
    return std::nullopt;
}

inline auto compose_timing_inner_extent(const WormUiComposeView& view, wormedit::timing_lane_kind_t lane)
    -> std::optional<std::pair<double, double>> {
    const auto extent = compose_timing_lane_extent(view, lane);
    if (!extent) return std::nullopt;
    const auto inset = std::min(2.0, (extent->second - extent->first) * .25);
    return std::pair{extent->first + inset, extent->second - inset};
}

inline auto compose_timing_marker_bounds(const WormUiComposeView& view,
                                         const wormedit::ui::compose_timing_marker_view_t& marker)
    -> wormedit::ui::compose_rect_t {
    const auto extent = compose_timing_inner_extent(view, marker.lane);
    if (!extent) return {};
    const auto width = extent->second - extent->first;
    const auto left = extent->first + width * std::clamp(marker.marker_left_fraction, 0.0, 1.0);
    const auto right = extent->first + width * std::clamp(marker.marker_right_fraction, 0.0, 1.0);
    const auto top = std::clamp(marker.marker_y_pixels - 8.0, 0.0,
                                std::max(0.0, view.viewport_height_pixels - 16.0));
    return {left, top, std::min(extent->second, std::max(left, right)), top + 16.0};
}

// The measure lane is the measure rail made editable: each selectable bar line (every real measure
// line but the chart's first) has a handle across the rail. Drawing, picking and area selection all
// use this rectangle; a hidden lane has none.
inline auto compose_bar_line_bounds(const WormUiComposeView& view, const wormedit::ui::compose_grid_line_view_t& line)
    -> std::optional<wormedit::ui::compose_rect_t> {
    if (!view.measure_lane.visible || !line.measure_id || line.measure_number < 2 ||
        !std::isfinite(line.y_pixels))
        return std::nullopt;
    return wormedit::ui::compose_rect_t{0.0, line.y_pixels - 4.0, compose_measure_rail_width, line.y_pixels + 4.0};
}

// The bar line whose handle holds the point, if any.
inline auto compose_bar_line_at(const WormUiComposeView& view, double x, double y)
    -> const wormedit::ui::compose_grid_line_view_t* {
    for (std::size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto bounds = compose_bar_line_bounds(view, view.grid_lines[index]);
        if (bounds && x >= bounds->left && x < bounds->right && y >= bounds->top && y <= bounds->bottom)
            return &view.grid_lines[index];
    }
    return nullptr;
}

// A press the measure lane takes for itself: on a bar line, or anywhere in the lane while drawing.
inline auto is_compose_measure_lane_press(const WormUiComposeView& view, double x, double y) -> bool {
    if (!view.measure_lane.visible || !std::isfinite(x) || x < 0.0 || x >= compose_measure_rail_width)
        return false;
    return view.note_paint_mode_active || compose_bar_line_at(view, x, y) != nullptr;
}

} // namespace wormedit::ui
