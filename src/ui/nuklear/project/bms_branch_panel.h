#pragma once

#include "ui/frontend/compose_request.h"

struct nk_context;

[[nodiscard]] bool worm_ui_nuklear_bms_branch_panel_visible(const wormedit::ui::compose_view_t* view);
void worm_ui_nuklear_render_bms_branch_panel(struct nk_context* context,
                                             const wormedit::ui::compose_view_t* view,
                                             wormedit::ui::compose_request_t* action);
