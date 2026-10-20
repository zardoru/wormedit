#pragma once

#include "ui/frontend.h"

struct nk_context;
struct SDL_Renderer;

[[nodiscard]] int
worm_ui_nuklear_player_preview_view_valid(const wormedit::ui::player_preview_view_t* view);

void worm_ui_nuklear_render_player_preview(struct nk_context* context,
                                           struct SDL_Renderer* renderer,
                                           const wormedit::ui::player_preview_view_t* view,
                                           wormedit::ui::workspace_action_t* action, int window_width,
                                           int window_height);

// Releases backend-owned textures before the SDL renderer is destroyed.
void worm_ui_nuklear_shutdown_player_preview(void);
