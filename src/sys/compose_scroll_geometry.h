#pragma once

#include <wormedit/core/interaction.h>

namespace wormedit::sys {

// The first row settles one fifth of the viewport from the #000-facing edge.
inline auto compose_cursor_screen_anchor(double viewport_height, bool downscroll) -> double {
    return viewport_height * (downscroll ? 1.0 - editor_cursor_edge_margin_fraction
                                         : editor_cursor_edge_margin_fraction);
}

// The visual axis reverses in downscroll; horizontal BGM wheel input does not.
inline auto compose_wheel_scroll_delta(double delta_y, bool downscroll) -> double {
    return downscroll ? -delta_y : delta_y;
}

inline auto compose_visual_navigation_direction(bool downscroll, bool visual_up)
    -> interaction_direction_t {
    return downscroll == visual_up ? interaction_direction_t::forward
                                   : interaction_direction_t::backward;
}

// The one place a screen-space y (top-down, as the field is drawn) becomes a source-order y:
// downscroll mirrors the viewport. Every pointer-to-row path goes through these two helpers.
inline auto compose_source_y(double viewport_y, double viewport_height, bool downscroll)
    -> double {
    return downscroll ? viewport_height - viewport_y : viewport_y;
}

// Fraction of the viewport, in source order and clamped to [0, 1], under a screen-space y.
inline auto compose_source_fraction(double viewport_y, double viewport_height, bool downscroll)
    -> double {
    if (!(viewport_height > 0.0)) return 0.0;
    const auto source = compose_source_y(viewport_y, viewport_height, downscroll);
    return source <= 0.0 ? 0.0 : (source >= viewport_height ? 1.0 : source / viewport_height);
}

} // namespace wormedit::sys
