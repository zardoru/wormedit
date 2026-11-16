#pragma once

#include <algorithm>
#include <cmath>

namespace wormedit::ui {

// Shared geometry of the vertical resize handles on Compose column edges (stem lanes and the
// timing/BGA region). Pointer coordinates and edges are in source pixels, the units of
// compose_view_t; the handle's grab width is fixed on screen, so callers convert it first.
inline constexpr double compose_edge_handle_screen_tolerance_pixels = 4.0;

// Grab tolerance in source pixels for a viewport drawn at `screen_width` for `source_width`.
[[nodiscard]] inline auto compose_edge_handle_tolerance(double source_width, double screen_width) noexcept
    -> double {
    return screen_width > 0.0 && std::isfinite(source_width) && std::isfinite(screen_width)
        ? compose_edge_handle_screen_tolerance_pixels * source_width / screen_width
        : compose_edge_handle_screen_tolerance_pixels;
}

// True when (x, y) is on the handle of a column edge that spans the whole viewport height.
[[nodiscard]] inline auto compose_edge_handle_hit(double x, double y, double edge, double height,
                                                  double tolerance) noexcept -> bool {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(edge) && y >= 0.0 && y < height &&
           std::abs(x - edge) <= tolerance;
}

// The value a drag that began at `anchor_pointer` with `anchor_value` yields at `pointer`.
[[nodiscard]] inline auto compose_edge_handle_dragged_value(double anchor_value, double anchor_pointer,
                                                            double pointer) noexcept -> double {
    return anchor_value + pointer - anchor_pointer;
}

} // namespace wormedit::ui
