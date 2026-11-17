#pragma once
#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend/common.h"
#include "ui/frontend/compose.h"
#include "ui/frontend/project.h"
#include "ui/frontend/timing_preview.h"
struct nk_context;
struct worm_ui_nuklear_compose_state_t;
void worm_ui_nuklear_render_compose_bga(nk_context*, const wormedit::ui::compose_view_t*,
    wormedit::ui::compose_ui_window_t*, worm_ui_nuklear_compose_state_t*);
void worm_ui_nuklear_bga_set_renderer(SDL_Renderer* renderer);
void worm_ui_nuklear_render_bga_stack_picker(nk_context*, const wormedit::ui::compose_view_t*,
    wormedit::ui::compose_ui_window_t*, worm_ui_nuklear_compose_state_t*);
