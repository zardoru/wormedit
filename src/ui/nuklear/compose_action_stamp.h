#pragma once
#include "ui/frontend.h"
namespace wormedit::ui {
enum class compose_project_stamp_t { include, preserve };
inline void stamp_compose_input(wormedit::ui::workspace_action_t& action, const WormUiComposeView& view,
                                compose_project_stamp_t project) {
    if (project == compose_project_stamp_t::include)
        action.compose_input_project_id = view.project_id;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
}
inline auto raise_compose_action(wormedit::ui::workspace_action_t& action, const WormUiComposeView& view,
                                 WormUiComposeActionKind kind,
                                 compose_project_stamp_t project = compose_project_stamp_t::include)
    -> bool {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return false;
    action.compose_action = kind;
    stamp_compose_input(action, view, project);
    return true;
}
} // namespace wormedit::ui
