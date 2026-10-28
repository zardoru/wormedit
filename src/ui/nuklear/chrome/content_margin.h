#pragma once

#include <algorithm>

namespace wormedit::ui::layout {

// Form-style screens (Project, Metadata) share one centered column. It fills narrow windows
// up to a small gutter and stops growing at a readable width, so wide 16:9 and 16:10 windows
// keep labels, fields and table columns close together instead of stretching edge to edge.
inline constexpr float minimum_gutter = 32.0f;
inline constexpr float maximum_column_width = 1240.0f;
// Space between the column and the tab bar above it and the status bar below it.
inline constexpr float vertical_margin = 24.0f;

// Width of one side margin, in pixels, for a window of the given width.
inline auto form_side_margin(float window_width) -> float {
    if (!(window_width > 0.0f)) return minimum_gutter;
    return std::max(minimum_gutter, (window_width - maximum_column_width) * 0.5f);
}

} // namespace wormedit::ui::layout
