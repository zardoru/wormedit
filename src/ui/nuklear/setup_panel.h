#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_setup_panel(struct nk_context* context,
                                        const WormUiSetupView* view,
                                        WormUiWorkspaceAction* action);
