#pragma once
#include "ui/frontend.h"
namespace wormedit::ui {
enum class compose_project_stamp_t { include, preserve };
inline void stamp_compose_input(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
                                compose_project_stamp_t project) {
    if (project == compose_project_stamp_t::include)
        action.compose_input_project_id = wormedit::song_id_t{view.project_id.value};
    action.compose_input_chart_id = wormedit::chart_id_t{view.chart_id.value};
    action.compose_input_document_revision = view.document_revision;
}
inline auto raise_compose_action(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
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
