#pragma once
#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend/common.h"
#include "ui/frontend/compose.h"
#include "ui/frontend/project.h"
#include "ui/frontend/timing_preview.h"
namespace wormedit::ui {
enum class compose_project_stamp_t { include, preserve };
inline void stamp_compose_input(request_stamp_t& request_stamp, const wormedit::ui::compose_view_t& view,
                                compose_project_stamp_t project) {
    const auto stamp = wormedit::ui::request_stamp_t::of(view);
    if (project == compose_project_stamp_t::include) request_stamp.project_id = stamp.project_id;
    request_stamp.chart_id = stamp.chart_id;
    request_stamp.document_revision = stamp.document_revision;
}
inline void stamp_compose_input(wormedit::ui::compose_ui_window_t& action, const wormedit::ui::compose_view_t& view,
                                compose_project_stamp_t project) {
    stamp_compose_input(action.stamp, view, project);
}
// Claims the Compose slot for this frame. A caller that only raises Compose requests passes the
// request and the stamp, not the whole window.
inline auto raise_compose_action(compose_request_t& compose, request_stamp_t& request_stamp,
                                 const wormedit::ui::compose_view_t& view, WormUiComposeActionKind kind,
                                 compose_project_stamp_t project = compose_project_stamp_t::include)
    -> bool {
    if (compose.kind != WORM_UI_COMPOSE_ACTION_NONE)
        return false;
    compose.kind = kind;
    stamp_compose_input(request_stamp, view, project);
    return true;
}
inline auto raise_compose_action(wormedit::ui::compose_ui_window_t& action, const wormedit::ui::compose_view_t& view,
                                 WormUiComposeActionKind kind,
                                 compose_project_stamp_t project = compose_project_stamp_t::include)
    -> bool {
    return raise_compose_action(action.compose, action.stamp, view, kind, project);
}
} // namespace wormedit::ui
