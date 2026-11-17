#pragma once

#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend.h"
#include <cstdio>

// A popup activation belongs to the chart/revision that drew it. Native/global
// menu commands remain unstamped and resolve their target at invocation time.
inline void emit_compose_menu_command(wormedit::ui::compose_ui_window_t& action,
    const wormedit::ui::compose_view_t& view, const char* command) {
    if (action.menu_command_id[0]) return;
    action.menu_command_id = command;
    action.compose.menu_command = true;
    action.stamp = wormedit::ui::request_stamp_t::of(view);
}
