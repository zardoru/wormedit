#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/compose_state.h"

struct nk_context;

// The Compose keysound listbox: a sticky toolbar and search field above a
// virtualized, multi-selectable list of the catalog's samples (iBMSC/BMSE
// style). Selecting a row arms it for painting; the layer strip assigns
// semantic layers to the selected group; files dropped on the panel are added.
// Call inside the panel's group; it fills the group's remaining height.
void worm_ui_nuklear_render_compose_sounds(struct nk_context* context,
                                           const WormUiComposeView* view,
                                           wormedit::ui::workspace_action_t* action,
                                           worm_ui_nuklear_compose_state_t* state);
