#pragma once

#include "ui/frontend/actions.h"

struct nk_context;

void worm_ui_nuklear_render_compare_panel(nk_context* context,
                                          const wormedit::ui::compare_view_t* view,
                                          wormedit::ui::compare_request_t* compare,
                                          WormUiWorkspaceActionKind* kind);
