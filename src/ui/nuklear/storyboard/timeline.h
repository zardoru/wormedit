#pragma once

#include "ui/frontend/storyboard.h"
#include <SDL3/SDL_events.h>

struct nk_context;
namespace wormedit::ui {
struct shortcut_event_t;
void render_storyboard_timeline(nk_context*, const storyboard_view_t&, storyboard_action_t&, bool input_available);
void cancel_storyboard_timeline_input();
// Deliver terminal cancellation even when the timeline is no longer rendered.
[[nodiscard]] auto drain_storyboard_timeline_cancel(storyboard_action_t&) -> bool;
[[nodiscard]] auto capture_storyboard_timeline_key(nk_context*, const SDL_KeyboardEvent&) -> bool;
[[nodiscard]] auto storyboard_timeline_shortcut(const storyboard_view_t&,
    const shortcut_event_t&, storyboard_action_t&) -> bool;
} // namespace wormedit::ui
