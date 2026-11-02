#pragma once
#include "ui/frontend.h"
struct nk_context;
struct worm_ui_nuklear_compose_state_t;
void worm_ui_nuklear_render_compose_bga(nk_context*, const wormedit::ui::compose_view_t*,
    wormedit::ui::workspace_action_t*, worm_ui_nuklear_compose_state_t*);
void worm_ui_nuklear_bga_set_renderer(SDL_Renderer* renderer);
void worm_ui_nuklear_render_bga_stack_picker(nk_context*, const wormedit::ui::compose_view_t*,
    wormedit::ui::workspace_action_t*, worm_ui_nuklear_compose_state_t*);
void worm_ui_nuklear_reset_image_picker();
namespace wormedit::ui {
struct storyboard_action_t;
struct storyboard_view_t;
[[nodiscard]] auto open_storyboard_image_picker(const storyboard_action_t& captured) -> bool;
[[nodiscard]] auto consume_storyboard_image_picker(const storyboard_view_t&, storyboard_action_t&) -> bool;
}
