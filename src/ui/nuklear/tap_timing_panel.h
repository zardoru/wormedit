#pragma once
#include "ui/frontend.h"
struct nk_context;

void worm_ui_nuklear_render_tap_timing_panel(nk_context* context,
    const WormUiTapTimingView& view, WormUiTapTimingAction& action);
