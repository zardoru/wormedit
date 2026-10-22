#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_metadata_panel(struct nk_context* context,
                                           const wormedit::ui::metadata_view_t* view,
                                           wormedit::ui::workspace_action_t* action, bool window_focused);
