#pragma once

#include "ui/frontend.h"

struct nk_context;

[[nodiscard]] int
worm_ui_nuklear_player_preview_view_valid(const WormUiPlayerPreviewView* view);

void worm_ui_nuklear_render_player_preview(struct nk_context* context,
                                           const WormUiPlayerPreviewView* view,
                                           WormUiWorkspaceAction* action, int window_width,
                                           int window_height);
