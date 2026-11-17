#pragma once
#include "ui/frontend/compose_request.h"
#include "ui/frontend/workspace.h"
struct nk_context;
void worm_ui_nuklear_render_timing_settings_panel(nk_context* context,
    const wormedit::ui::workspace_view_t& view, wormedit::ui::timing_settings_action_t& settings,
    wormedit::ui::compose_request_t& compose);
