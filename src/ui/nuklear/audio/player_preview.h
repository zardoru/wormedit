#pragma once

#include "ui/frontend/project.h"
#include "ui/frontend/timing_preview.h"

struct nk_context;
struct SDL_Renderer;

[[nodiscard]] int
worm_ui_nuklear_player_preview_view_valid(const wormedit::ui::player_preview_view_t* view);

void worm_ui_nuklear_render_player_preview(struct nk_context* context,
                                           struct SDL_Renderer* renderer,
                                           const wormedit::ui::player_preview_view_t* view,
                                           WormUiWorkspaceActionKind* kind, wormedit::player_preview_settings_t* settings_request, int window_width, int top_inset,
                                           int window_height,
                                           // Ctrl+wheel notches the frontend routed to the preview this frame.
                                           double wheel_zoom_steps);

// Releases backend-owned textures before the SDL renderer is destroyed.
void worm_ui_nuklear_shutdown_player_preview(void);
