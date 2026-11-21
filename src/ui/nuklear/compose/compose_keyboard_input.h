#pragma once

#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/nuklear/compose/compose_panel_internal.h"

void process_compose_key_events(struct nk_context* context, const wormedit::ui::compose_view_t* view,
                                wormedit::ui::compose_ui_window_t* action, bool window_focused,
                                bool ime_composition_active);
void process_compose_preview_navigation_keys(struct nk_context* context,
                                             const wormedit::ui::compose_view_t* view,
                                             wormedit::ui::compose_ui_window_t* action,
                                             bool window_focused, bool ime_composition_active);
