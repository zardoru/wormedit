#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/sound_import_state.h"

struct nk_context;
struct SDL_Window;

auto worm_ui_nuklear_pending_sound_import() -> worm_ui_sound_import_state_t&;
bool worm_ui_nuklear_sound_import_active();
void worm_ui_nuklear_reset_sound_import();
void worm_ui_nuklear_sound_import_pick(SDL_Window* window, const WormUiWorkspaceView& view);
void worm_ui_nuklear_consume_sound_picker(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action);
void worm_ui_nuklear_render_pending_sound_import(nk_context* context,
    const WormUiWorkspaceView& view, WormUiWorkspaceAction& action, int width, int height);
