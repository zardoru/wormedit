#pragma once
#include "ui/frontend.h"
struct nk_context;
void worm_ui_nuklear_render_timing_settings_panel(nk_context* context,
    const wormedit::ui::workspace_view_t& view, wormedit::ui::workspace_action_t& action);
