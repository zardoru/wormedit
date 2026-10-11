#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/compose_state.h"

struct nk_context;

void worm_ui_nuklear_render_compose_resources(struct nk_context* context,
                                              const WormUiComposeView* view,
                                              WormUiWorkspaceAction* action,
                                              worm_ui_nuklear_compose_state_t* state);

// Modal surfaces render after the workspace, never inside the inspector window.
void worm_ui_nuklear_open_export_dialog(const WormUiComposeView& view,
    worm_ui_nuklear_compose_state_t& state);
void worm_ui_nuklear_open_layer_creation(const WormUiComposeView& view,
    worm_ui_nuklear_compose_state_t& state);
void worm_ui_nuklear_render_compose_dialogs(nk_context* context, const WormUiComposeView& view,
    WormUiWorkspaceAction& action, worm_ui_nuklear_compose_state_t& state);
