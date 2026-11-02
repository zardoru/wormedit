#pragma once
#include "ui/frontend/storyboard.h"
struct nk_context;
struct SDL_Renderer;
struct SDL_KeyboardEvent;
namespace wormedit::ui {
void cancel_storyboard_canvas_pointer();
[[nodiscard]] auto drain_storyboard_canvas_cancel(storyboard_action_t&) -> bool;
[[nodiscard]] auto capture_storyboard_canvas_key(nk_context*, const SDL_KeyboardEvent&) -> bool;
void render_storyboard_canvas(nk_context*, SDL_Renderer*, const storyboard_view_t&,
    storyboard_action_t&, bool input_available);
} // namespace wormedit::ui
