#pragma once

#include "ui/frontend.h"

struct nk_context;

namespace wormedit::ui::nuklear {

// Input & shortcuts page renderer; registered as the page for WORM_UI_OPTIONS_INPUT.
// Rows and capture state come from the view, which the sys layer projects per frame.
void render_shortcuts_page(nk_context* context, const wormedit::ui::options_view_t* view,
                           wormedit::ui::options_action_t* action);
void reset_shortcuts_filter();

} // namespace wormedit::ui::nuklear
