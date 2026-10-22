#pragma once

#include "ui/nuklear/compose/compose_panel_internal.h"

void process_compose_key_events(struct nk_context* context, const wormedit::ui::compose_view_t* view,
                                wormedit::ui::workspace_action_t* action, bool window_focused,
                                bool ime_composition_active);
