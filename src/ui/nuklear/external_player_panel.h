#pragma once
#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_external_player_panel(struct nk_context* context,
    const struct wormedit::ui::external_player_view_t* view, struct wormedit::ui::external_player_action_t* action);

void worm_ui_nuklear_reset_external_player_panel(void);
