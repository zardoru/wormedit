#pragma once
#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_external_players_page(struct nk_context* context,
    const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action);
