#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_metadata_panel(struct nk_context* context,
                                           const WormUiMetadataView* view,
                                           WormUiWorkspaceAction* action, bool window_focused);
