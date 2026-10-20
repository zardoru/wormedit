#pragma once

#include "ui/frontend.h"

struct nk_context;

[[nodiscard]] bool worm_ui_nuklear_bms_branch_panel_visible(const WormUiComposeView* view);
void worm_ui_nuklear_render_bms_branch_panel(struct nk_context* context,
                                             const WormUiComposeView* view,
                                             wormedit::ui::workspace_action_t* action);
