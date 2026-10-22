#pragma once
#include "ui/frontend.h"
struct nk_context;

void worm_ui_nuklear_render_tap_timing_panel(nk_context* context,
    const wormedit::ui::tap_timing_view_t& view, wormedit::ui::tap_timing_action_t& action);
