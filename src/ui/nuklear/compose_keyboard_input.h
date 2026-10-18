#pragma once

#include "ui/nuklear/compose_panel_internal.h"

void process_compose_key_events(struct nk_context* context, const WormUiComposeView* view,
                                WormUiWorkspaceAction* action, bool window_focused,
                                bool ime_composition_active);
