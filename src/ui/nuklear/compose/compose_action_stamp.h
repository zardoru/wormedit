#pragma once
#include "ui/frontend.h"
namespace wormedit::ui {
enum class compose_project_stamp_t { include, preserve };
inline void stamp_compose_input(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
                                compose_project_stamp_t project) {
    const auto stamp = wormedit::ui::request_stamp_t::of(view);
    if (project == compose_project_stamp_t::include) action.stamp.project_id = stamp.project_id;
    action.stamp.chart_id = stamp.chart_id;
    action.stamp.document_revision = stamp.document_revision;
}
inline auto raise_compose_action(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
                                 WormUiComposeActionKind kind,
                                 compose_project_stamp_t project = compose_project_stamp_t::include)
    -> bool {
    if (action.compose.kind != WORM_UI_COMPOSE_ACTION_NONE)
        return false;
    action.compose.kind = kind;
    stamp_compose_input(action, view, project);
    return true;
}
} // namespace wormedit::ui
