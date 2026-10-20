#pragma once
#include "ui/frontend.h"
struct nk_context;
void render_compose_snap_badge(nk_context* context, const WormUiComposeView& view,
                               wormedit::ui::workspace_action_t& action, bool available);
