#pragma once

#include "ui/frontend.h"

struct nk_context;

// Category buses for live playback. Emits the complete next mix; never edits the document.
void worm_ui_nuklear_render_compose_mixer(struct nk_context* context,
                                          const wormedit::ui::compose_view_t* view,
                                          wormedit::ui::workspace_action_t* action);
