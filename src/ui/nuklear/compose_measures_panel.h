#pragma once
#include "ui/frontend.h"
struct nk_context;
struct worm_ui_nuklear_compose_state_t;

// The Measures dock: one row per measure with its number, meter and length. Rows select like the
// Sounds dock (click, Ctrl, Shift); a row click moves the cursor there, and Apply sets the typed
// meter on every selected measure as one undo step.
void worm_ui_nuklear_render_compose_measures(nk_context*, const WormUiComposeView*,
    WormUiWorkspaceAction*, worm_ui_nuklear_compose_state_t*);
