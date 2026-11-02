#pragma once

#include "ui/frontend/storyboard.h"

struct nk_context;
namespace wormedit::ui {
struct shortcut_event_t;
void cancel_storyboard_hierarchy_input();
void render_storyboard_hierarchy(nk_context*, const storyboard_view_t&, storyboard_action_t&,
                                 bool input_available);
// Called only at the existing focused/non-modal shortcut consumption boundary.
[[nodiscard]] auto storyboard_hierarchy_shortcut(const storyboard_view_t&, const shortcut_event_t&,
                                                storyboard_action_t&) -> bool;
} // namespace wormedit::ui
