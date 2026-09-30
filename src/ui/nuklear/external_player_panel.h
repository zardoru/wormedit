#pragma once
#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_external_player_panel(struct nk_context* context,
    const struct WormUiExternalPlayerView* view, struct WormUiExternalPlayerAction* action);

void worm_ui_nuklear_reset_external_player_panel(void);
