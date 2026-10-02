#pragma once

#include <algorithm>

namespace wormedit::ui::layout {

// Form-style screens (Metadata, Setup) read better as a centered column than edge to edge.
// Both side margins together take about half the window at 16:9 and nothing beyond a small
// gutter at 4:3, interpolating linearly in between (16:10 lands near a third).
inline constexpr float narrow_aspect = 4.0f / 3.0f;
inline constexpr float wide_aspect = 16.0f / 9.0f;
inline constexpr float narrow_margin_share = 0.05f;
inline constexpr float wide_margin_share = 0.50f;
inline constexpr float minimum_gutter = 12.0f;
inline constexpr float minimum_column_width = 420.0f;

// Width of one side margin, in pixels, for a window of the given size.
inline auto form_side_margin(float window_width, float window_height) -> float {
    if (window_width <= 0.0f || window_height <= 0.0f) return minimum_gutter;
    const float aspect = std::clamp(window_width / window_height, narrow_aspect, wide_aspect);
    const float t = (aspect - narrow_aspect) / (wide_aspect - narrow_aspect);
    const float share = narrow_margin_share + (wide_margin_share - narrow_margin_share) * t;
    const float margin = window_width * share * 0.5f;
    const float widest = std::max(minimum_gutter, (window_width - minimum_column_width) * 0.5f);
    return std::clamp(margin, minimum_gutter, widest);
}

} // namespace wormedit::ui::layout
