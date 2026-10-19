#pragma once

#include "ui/frontend.h"

struct nk_context;

void worm_ui_nuklear_render_setup_panel(struct nk_context* context,
                                        const WormUiSetupView* view,
                                        WormUiWorkspaceAction* action);

// Modal chart actions share frontend shortcut ownership and expire with the view.
bool worm_ui_nuklear_setup_popup_owns_input();
void worm_ui_nuklear_sync_setup_popups(nk_context* context, const WormUiSetupView* view);
void worm_ui_nuklear_retire_setup_popups(nk_context* context);
void worm_ui_nuklear_render_setup_dialogs(nk_context* context, const WormUiSetupView& view,
                                         WormUiWorkspaceAction& action);

// Abandon an in-progress row drag on native focus loss.
void worm_ui_nuklear_cancel_setup_drag();
void worm_ui_nuklear_set_project_directory(const char* path);
