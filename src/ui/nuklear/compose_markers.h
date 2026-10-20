#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_reset_compose_markers(void);
void worm_ui_nuklear_render_compose_markers(struct nk_context* context,
                                            const WormUiComposeView* view,
                                            wormedit::ui::workspace_action_t* action);
