#pragma once

#include "wormedit/nuklear_config.h"

namespace wormedit::ui::gallery {

// The hidden UI gallery draws every chrome component in every state. Start the editor with
// WORMEDIT_UI_GALLERY=1 to see it over the workspace.
[[nodiscard]] auto requested() -> bool;

struct state_t {
    int segmented{1};
    bool segmented_focus{};
    int chip{0};
    int check_cycle{};
    int selection_actions_clicks{};
};

// Draws the gallery as a full-window page plus a level-2 dialog and a level-1 menu near the
// window edge. `width` and `height` are the render target size.
void render(nk_context* context, state_t& state, int width, int height);

} // namespace wormedit::ui::gallery
