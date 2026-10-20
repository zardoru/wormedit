#pragma once

#include "ui/nuklear/compose_state.h"

// The BMSON tab of the Sounds dock: BGM object and marker actions, razor mode, the staged-marker
// commit/cancel pair, the selected-slice controls and the staged-region audition list. Actions and
// payloads are the ones the inspector emitted before these controls moved here.
void worm_ui_nuklear_render_compose_bmson_tab(struct nk_context* context,
                                              const WormUiComposeView* view,
                                              wormedit::ui::workspace_action_t* action,
                                              worm_ui_nuklear_compose_state_t* state);
