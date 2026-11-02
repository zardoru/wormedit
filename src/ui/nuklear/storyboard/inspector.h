#pragma once

#include "ui/frontend/storyboard.h"
#include <SDL3/SDL_events.h>

struct nk_context;
namespace wormedit::ui {
void render_storyboard_inspector(nk_context*, const storyboard_view_t&, storyboard_action_t&, bool input_available);
void cancel_storyboard_inspector_input();
// Drain after input-loss cleanup even when the Storyboard tab is hidden.
[[nodiscard]] auto drain_storyboard_inspector_cancel(storyboard_action_t&) -> bool;
[[nodiscard]] auto capture_storyboard_inspector_key(nk_context*, const SDL_KeyboardEvent&) -> bool;
} // namespace wormedit::ui
