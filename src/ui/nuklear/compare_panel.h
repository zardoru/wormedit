#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_compare_panel(nk_context* context,
                                          const wormedit::ui::compare_view_t* view,
                                          wormedit::ui::workspace_action_t* action);
