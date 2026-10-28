#pragma once

#include "wormedit/nuklear_config.h"

namespace wormedit::ui::metrics {

// Control heights: compact rows and chips, default controls, primary actions.
inline constexpr float control_compact = 24.0f;
inline constexpr float control_default = 28.0f;
inline constexpr float control_primary = 32.0f;

// Corner radii.
inline constexpr float radius_control = 4.0f;
inline constexpr float radius_chip = 12.0f;
inline constexpr float radius_bar = 6.0f;
inline constexpr float radius_surface = 8.0f; // menus and dialogs

// Gaps between neighboring controls.
inline constexpr float gap_small = 4.0f;
inline constexpr float gap_medium = 8.0f;
inline constexpr float gap_large = 16.0f;

// Menu rows and separators share the control and gap scale.
inline constexpr float menu_item_height = control_compact;
inline constexpr float menu_separator_height = gap_medium;

// Font sizes. The editor's font is rasterised once, so these name the sizes that cards
// may request instead of scattering literals.
inline constexpr float font_small = 12.0f;
inline constexpr float font_body = 13.0f;
inline constexpr float font_label = 15.0f;
inline constexpr float font_display = 28.0f;

inline constexpr float checkbox_box = 16.0f;

// Left indent of an accordion section's body.
inline constexpr float section_indent = 14.0f;
// Level-two cards: body indent (also the right margin) and how far the card extends left of the content.
inline constexpr float card_indent = 8.0f;
inline constexpr float card_overhang = 4.0f;

// Shadow of one elevation level: `steps` stacked rounded rects, each one pixel larger, all
// shifted `offset_y` pixels down.
//   0 flat (inspector groups, bars)
//   1 menus, context menus, tooltips
//   2 dialogs and floating tools
struct elevation_t {
    int steps;
    float offset_y;
    unsigned char alpha;
};

inline constexpr elevation_t elevation_flat{0, 0.0f, 0};
inline constexpr elevation_t elevation_menu{8, 4.0f, 9};
inline constexpr elevation_t elevation_dialog{12, 6.0f, 12};

constexpr auto elevation(int level) -> elevation_t {
    return level >= 2 ? elevation_dialog : level == 1 ? elevation_menu : elevation_flat;
}

// How far a level's shadow reaches beyond the surface bounds on each side.
struct shadow_reach_t {
    float left, top, right, bottom;
};

constexpr auto shadow_reach(int level) -> shadow_reach_t {
    const auto shadow = elevation(level);
    if (shadow.steps == 0) return {};
    const auto steps = static_cast<float>(shadow.steps);
    const float up = steps - shadow.offset_y;
    return {steps, up > 0.0f ? up : 0.0f, steps, steps + shadow.offset_y};
}

// Colors the theme files do not carry yet (T-270 moves them there). Disabled colors derive
// from the theme instead: chrome::text_faint and chrome::disabled_fill.
inline constexpr nk_color destructive{223, 86, 59, 255};      // coral: destructive or conflict
inline constexpr nk_color destructive_text{255, 158, 134, 255};

} // namespace wormedit::ui::metrics
