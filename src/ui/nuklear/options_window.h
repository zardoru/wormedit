#pragma once
#include "ui/frontend.h"
struct nk_context;
using WormUiOptionsPageRenderer = void (*)(nk_context*, const WormUiOptionsView*, WormUiOptionsAction*);
// Page registration changes presentation only; preferences are owned by the runtime.
bool worm_ui_nuklear_register_options_page(WormUiOptionsCategory, WormUiOptionsPageRenderer);
bool worm_ui_nuklear_options_view_valid(const WormUiOptionsView*);
void worm_ui_nuklear_render_options_window(nk_context*, const WormUiOptionsView*, WormUiOptionsAction*,
    wormedit::ui::lane_keys_action_t* lane_keys = nullptr);

// Cancel header movement without discarding the retained tool position/size.
void worm_ui_nuklear_cancel_options_drag();
