#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/compose/compose_state.h"

struct nk_context;

// Release a retained Layers popup in its workspace window after a lifecycle change.
void worm_ui_nuklear_dismiss_layer_popup(struct nk_context* context);

void worm_ui_nuklear_render_compose_layers(struct nk_context* context,
                                           const wormedit::ui::compose_view_t* view,
                                           wormedit::ui::workspace_action_t* action,
                                           worm_ui_nuklear_compose_state_t* state);
