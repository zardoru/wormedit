#pragma once

#include "sys/compose_action_context.h"
#include "sys/compose_action_routes.h"
#include "sys/compose_area_actions.h"

namespace wormedit::sys {
// Standalone area tests retain their boundary; production borrows the existing dispatch context.
inline auto
dispatch_compose_area_action(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                             editor_interaction_state_t& state,
                             notification_sink_t notifications = notification_sink_t::muted(),
                             bool timing_locked = false) -> std::optional<std::string> {
    const auto kind = action.compose_action;
    const auto* route = compose_action_detail::compose_action_route_t::find(
        kind, compose_action_detail::compose_area_routes);
    if (!action.compose_area_clear_requested && !route)
        return std::nullopt;
    if (compose_area_pause_conversion(kind) &&
        history.document.id != action.compose_input_project_id) {
        auto message = stale_action_message("Area conversion");
        notifications.post_feedback(notification_severity_t::warning, notification_source_t::edit,
                                    message);
        return message;
    }
    if (state.chart != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        auto message = stale_action_message("Area action");
        notifications.post_feedback(notification_severity_t::warning, notification_source_t::edit,
                                    message);
        return message;
    }
    workspace_controller_t workspace;
    workspace.song = history.document.id;
    workspace.selected_chart = state.chart;
    workspace.active_tab = workspace_tab_t::compose;
    application_command_state_t commands;
    commands.project.timing_recipe.timing_locked = timing_locked;
    std::optional<editor_interaction_state_t> retained;
    const compose_action_inputs_t environment{
        {history, workspace, notifications}, {commands, retained}, {}};
    compose_action_detail::compose_action_context_t context{
        environment, action, state, 0.0, false, editor_field_scroll_direction_t::up, notifications};
    if (auto blocked = compose_action_detail::process_compose_area_boundary(context))
        return compose_action_detail::finish_compose_action(context, std::move(*blocked));
    if (!route)
        return std::nullopt;
    return compose_action_detail::finish_compose_action(context, route->operation(context));
}
} // namespace wormedit::sys
