#pragma once

#include "ui/frontend.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace wormedit::ui {

inline constexpr double compose_measure_rail_width = 42.0;
// Numeric lanes fit a sign, five significant digits and a decimal at the
// normal field font. Boolean Kiai and Anchor lanes keep their compact width.
inline constexpr std::array<double, wormedit::timing_lane_kind_count> compose_timing_lane_widths{
    64, 64, 64, 48, 48, 48, 24, 24, 48, 36};

// Pointer targets and slice-point tiles use the head; area selection keeps the full tail.
inline auto compose_bgm_head_bounds(const wormedit::ui::compose_bgm_event_view_t& event) -> wormedit::ui::compose_rect_t {
    auto bounds = event.bounds;
    if (event.has_tail) {
        bounds.top = event.head_top_pixels;
        bounds.bottom = event.head_bottom_pixels;
    }
    return bounds;
}

inline auto compose_timing_width(std::uint16_t mask) -> double {
    double width{};
    for (std::size_t index = 0; index < compose_timing_lane_widths.size(); ++index)
        if (mask & (1U << index)) width += compose_timing_lane_widths[index];
    return width;
}

// A lane never gets narrower than its icon, and the region never wider than this multiple of
// its natural width.
inline constexpr double compose_timing_lane_minimum_width = 24.0;
inline constexpr double compose_timing_region_maximum_scale = 2.0;

// When the region is too narrow for every visible lane, lanes leave in this order (first
// entry first); the last lane standing is never dropped.
inline constexpr std::array<wormedit::timing_lane_kind_t, wormedit::timing_lane_kind_count>
    compose_timing_lane_hide_order{
        wormedit::timing_lane_kind_t::sample_set, wormedit::timing_lane_kind_t::volume,
        wormedit::timing_lane_kind_t::anchor,     wormedit::timing_lane_kind_t::kiai,
        wormedit::timing_lane_kind_t::warp,       wormedit::timing_lane_kind_t::speed,
        wormedit::timing_lane_kind_t::scroll,     wormedit::timing_lane_kind_t::delay,
        wormedit::timing_lane_kind_t::stop,       wormedit::timing_lane_kind_t::bpm};

inline auto compose_timing_minimum_width(std::uint16_t mask) -> double {
    return compose_timing_lane_minimum_width * std::popcount(static_cast<unsigned>(mask));
}

// The lanes of `mask` that fit in `available` pixels at their icon-sized minimums.
inline auto compose_timing_fit_mask(std::uint16_t mask, double available) -> std::uint16_t {
    auto fitted = mask;
    for (const auto lane : compose_timing_lane_hide_order) {
        if (std::popcount(static_cast<unsigned>(fitted)) <= 1 ||
            compose_timing_minimum_width(fitted) <= available) break;
        fitted = static_cast<std::uint16_t>(fitted & ~(1U << static_cast<unsigned>(lane)));
    }
    return fitted;
}

// Width the user may give the region of `mask`: at least one icon, at most twice natural.
inline auto compose_timing_region_clamp(double width, std::uint16_t mask) -> double {
    const auto maximum = std::max(compose_timing_lane_minimum_width,
                                  compose_timing_region_maximum_scale * compose_timing_width(mask));
    return std::isfinite(width) ? std::clamp(width, compose_timing_lane_minimum_width, maximum)
                                : compose_timing_lane_minimum_width;
}

inline auto compose_timing_right(const wormedit::ui::compose_view_t& view) -> double {
    return view.bga_lane_visible ? view.bga_lane_bounds.left : view.playable_field_left_pixels;
}

// Width of one visible lane when the lanes of `mask` share `region` pixels: natural widths
// scaled up proportionally, but squeezed only down to the icon-sized minimum, so the region
// can never take a lane below its icon (the mask is fitted to the region beforehand).
inline auto compose_timing_lane_scaled_width(std::uint16_t mask, unsigned ordinal, double region) -> double {
    const auto natural = compose_timing_width(mask);
    const auto lane = compose_timing_lane_widths[ordinal];
    if (natural <= 0.0) return 0.0;
    if (region >= natural) return lane * region / natural;
    const auto minimum = compose_timing_minimum_width(mask);
    const auto squeeze = natural > minimum ? std::clamp((region - minimum) / (natural - minimum), 0.0, 1.0) : 0.0;
    return compose_timing_lane_minimum_width + (lane - compose_timing_lane_minimum_width) * squeeze;
}

// One horizontal partition for projection, picking, markers and span gestures.
// Narrow surfaces compress the partition proportionally to preserve the field.
inline auto compose_timing_lane_extent(const wormedit::ui::compose_view_t& view, wormedit::timing_lane_kind_t lane)
    -> std::optional<std::pair<double, double>> {
    const auto ordinal = static_cast<unsigned>(lane);
    if (ordinal >= compose_timing_lane_widths.size() ||
        (view.timing_lane_visibility_mask & (1U << ordinal)) == 0) return std::nullopt;
    const auto right = compose_timing_right(view);
    if (!std::isfinite(right) || right <= compose_measure_rail_width) return std::nullopt;
    const auto region = right - compose_measure_rail_width;
    double left = compose_measure_rail_width;
    for (unsigned index = 0; index < ordinal; ++index)
        if (view.timing_lane_visibility_mask & (1U << index))
            left += compose_timing_lane_scaled_width(view.timing_lane_visibility_mask, index, region);
    return std::pair{left, std::min(right, left + compose_timing_lane_scaled_width(
                                                   view.timing_lane_visibility_mask, ordinal, region))};
}

inline auto compose_timing_lane_at(const wormedit::ui::compose_view_t& view, double x)
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

inline auto compose_timing_inner_extent(const wormedit::ui::compose_view_t& view, wormedit::timing_lane_kind_t lane)
    -> std::optional<std::pair<double, double>> {
    const auto extent = compose_timing_lane_extent(view, lane);
    if (!extent) return std::nullopt;
    const auto inset = std::min(2.0, (extent->second - extent->first) * .25);
    return std::pair{extent->first + inset, extent->second - inset};
}

inline auto compose_timing_marker_bounds(const wormedit::ui::compose_view_t& view,
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
inline auto compose_bar_line_bounds(const wormedit::ui::compose_view_t& view, const wormedit::ui::compose_grid_line_view_t& line)
    -> std::optional<wormedit::ui::compose_rect_t> {
    if (!view.measure_lane.visible || !line.measure_id || line.measure_number < 2 ||
        !std::isfinite(line.y_pixels))
        return std::nullopt;
    return wormedit::ui::compose_rect_t{0.0, line.y_pixels - 4.0, compose_measure_rail_width, line.y_pixels + 4.0};
}

// The bar line whose handle holds the point, if any.
inline auto compose_bar_line_at(const wormedit::ui::compose_view_t& view, double x, double y)
    -> const wormedit::ui::compose_grid_line_view_t* {
    for (std::size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto bounds = compose_bar_line_bounds(view, view.grid_lines[index]);
        if (bounds && x >= bounds->left && x < bounds->right && y >= bounds->top && y <= bounds->bottom)
            return &view.grid_lines[index];
    }
    return nullptr;
}

// A press the measure lane takes for itself: on a bar line, or anywhere in the lane while drawing.
inline auto is_compose_measure_lane_press(const wormedit::ui::compose_view_t& view, double x, double y) -> bool {
    if (!view.measure_lane.visible || !std::isfinite(x) || x < 0.0 || x >= compose_measure_rail_width)
        return false;
    return view.note_paint_mode_active || compose_bar_line_at(view, x, y) != nullptr;
}

} // namespace wormedit::ui
