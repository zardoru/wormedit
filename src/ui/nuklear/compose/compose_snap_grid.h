#pragma once
#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend.h"
struct nk_context;
void render_compose_snap_badge(nk_context* context, const wormedit::ui::compose_view_t& view,
                               wormedit::ui::compose_ui_window_t& action, bool available);
