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

} // namespace wormedit::sys
