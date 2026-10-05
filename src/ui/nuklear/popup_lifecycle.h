#pragma once

#include "wormedit/nuklear_config.h"

#include <cstdint>

namespace wormedit::ui::chrome {

struct PopupContextStamp {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    std::uint64_t object_id{};
};

// Window fills saved while a menu surface hides them (see chrome::menu_surface_begin).
struct MenuSurface {
    nk_color background{};
    nk_style_item fixed_background{};
};

inline auto popup_context_matches(PopupContextStamp expected,
                                  PopupContextStamp current) -> bool {
    return expected.project_id == current.project_id &&
           expected.chart_id == current.chart_id && expected.revision == current.revision &&
           expected.object_id == current.object_id;
}

inline auto popup_outside_press(const nk_context* context, struct nk_rect screen_bounds) -> bool {
    if (!context) return false;
    const auto& mouse = context->input.mouse;
    const auto pressed = nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) ||
                         nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT);
    const auto coalesced_left = !mouse.buttons[NK_BUTTON_LEFT].down &&
                                mouse.buttons[NK_BUTTON_LEFT].clicked >= 2;
    const auto coalesced_right = !mouse.buttons[NK_BUTTON_RIGHT].down &&
                                 mouse.buttons[NK_BUTTON_RIGHT].clicked >= 2;
    return (pressed || coalesced_left || coalesced_right) &&
           !nk_input_is_mouse_hovering_rect(&context->input, screen_bounds);
}

inline auto popup_screen_bounds(nk_context* context, struct nk_rect local_bounds)
    -> struct nk_rect {
    if (!context) return local_bounds;
    const auto region = nk_window_get_content_region(context);
    return nk_rect(region.x + local_bounds.x, region.y + local_bounds.y,
                   local_bounds.w, local_bounds.h);
}

inline auto popup_dismiss_requested(const nk_context* context, struct nk_rect screen_bounds,
                                    bool was_open = true, bool include_escape = true) -> bool {
    if (!context) return false;
    const auto escape = include_escape &&
                        nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE);
    return escape || (was_open && popup_outside_press(context, screen_bounds));
}

inline void popup_arm_release(bool& release_pending) { release_pending = true; }

inline auto popup_update_release_pending(const nk_context* context, bool& release_pending)
    -> bool {
    if (!release_pending) return false;
    if (!context) return true;
    const auto& input = context->input;
    const auto mouse_down = input.mouse.buttons[NK_BUTTON_LEFT].down ||
                            input.mouse.buttons[NK_BUTTON_RIGHT].down;
    const auto mouse_edge = nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT) ||
                            nk_input_is_mouse_pressed(&input, NK_BUTTON_RIGHT) ||
                            nk_input_is_mouse_released(&input, NK_BUTTON_LEFT) ||
                            nk_input_is_mouse_released(&input, NK_BUTTON_RIGHT);
    const auto escape_edge = nk_input_is_key_pressed(&input, NK_KEY_TEXT_RESET_MODE) ||
                             nk_input_is_key_released(&input, NK_KEY_TEXT_RESET_MODE);
    const auto escape_down = input.keyboard.keys[NK_KEY_TEXT_RESET_MODE].down;
    if (mouse_down || mouse_edge || escape_down || escape_edge) return true;
    release_pending = false;
    return false;
}

inline auto popup_owns_input(bool open, bool release_pending) -> bool {
    return open || release_pending;
}

} // namespace wormedit::ui::chrome
