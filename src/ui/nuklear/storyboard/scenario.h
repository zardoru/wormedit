#pragma once

#include "ui/frontend/storyboard.h"

struct nk_context;
namespace wormedit::ui {
// Both preview surfaces borrow the same frame projection and emit the same
// captured action; no widget retains a choice or a pointer into the view.
void render_storyboard_scenario(nk_context*, const storyboard_view_t&, storyboard_action_t&,
    bool input_available);
} // namespace wormedit::ui
