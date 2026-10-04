#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_compare_panel(nk_context* context,
                                          const WormUiCompareView* view,
                                          WormUiWorkspaceAction* action);
