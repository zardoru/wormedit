#pragma once

#include "ui/frontend.h"
#include <cstdio>

// A popup activation belongs to the chart/revision that drew it. Native/global
// menu commands remain unstamped and resolve their target at invocation time.
inline void emit_compose_menu_command(wormedit::ui::workspace_action_t& action,
    const WormUiComposeView& view, const char* command) {
    if (action.menu_command_id[0]) return;
    action.menu_command_id = command;
    action.compose_menu_command = true;
    action.compose_input_project_id = wormedit::song_id_t{view.project_id};
    action.compose_input_chart_id = wormedit::chart_id_t{view.chart_id};
    action.compose_input_document_revision = view.document_revision;
}
