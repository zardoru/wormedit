#pragma once
#include "ui/frontend.h"
struct nk_context;
void worm_ui_nuklear_render_timing_settings_panel(nk_context* context,
    const WormUiWorkspaceView& view, WormUiWorkspaceAction& action);
