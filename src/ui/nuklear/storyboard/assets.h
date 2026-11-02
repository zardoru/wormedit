#pragma once

#include "ui/frontend/storyboard.h"

struct nk_context;
struct SDL_Renderer;
namespace wormedit { struct storyboard_canvas_geometry_t; }
namespace wormedit::ui {
void cancel_storyboard_assets_input();
[[nodiscard]] auto storyboard_assets_drag_active() -> bool;
void render_storyboard_assets(nk_context*, SDL_Renderer*, const storyboard_view_t&, storyboard_action_t&, bool input_available);
void clear_storyboard_assets_demand(const storyboard_view_t&, storyboard_action_t&);
// Canvas owns its actual geometry and offers the drop before selection/gizmos.
[[nodiscard]] auto handle_storyboard_asset_drop(nk_context*, const storyboard_view_t&,
    const storyboard_canvas_geometry_t&, storyboard_action_t&, bool input_available) -> bool;
} // namespace wormedit::ui
