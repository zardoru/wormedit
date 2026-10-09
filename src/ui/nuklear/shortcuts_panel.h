#pragma once

#include "ui/frontend.h"

struct nk_context;

namespace wormedit::ui::nuklear {

// Input & shortcuts page renderer; registered as the page for WORM_UI_OPTIONS_INPUT.
// Rows and capture state come from the view, which the sys layer projects per frame.
void render_shortcuts_page(nk_context* context, const WormUiOptionsView* view,
                           WormUiOptionsAction* action);

} // namespace wormedit::ui::nuklear
