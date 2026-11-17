#pragma once

// Test-only adapters: tests build the whole workspace_action_t, while the Compose dispatchers take
// their narrower windows. Production code builds the windows in sys/app/application.cpp.
#include "sys/compose/actions/compose_bga_actions.h"
#include "sys/compose/actions/compose_sound_actions.h"
#include "ui/frontend/actions.h"

namespace wormedit::sys {

[[nodiscard]] inline auto dispatch_sound_properties_action(
    const wormedit::ui::workspace_action_t& action, document_history_t& history,
    workspace_controller_t& workspace,
    notification_sink_t notifications = notification_sink_t::muted()) -> bool {
    const auto window = wormedit::ui::compose_window_of(action);
    return dispatch_sound_properties_action(window, history, workspace, notifications);
}

[[nodiscard]] inline auto dispatch_compose_sound_action(
    const wormedit::ui::workspace_action_t& action, document_history_t& history,
    workspace_controller_t& workspace, chart_id_t chart, application_command_state_t& application_state,
    notification_sink_t notifications = notification_sink_t::muted())
    -> std::optional<std::string> {
    const auto window = wormedit::ui::compose_window_of(action);
    return dispatch_compose_sound_action(window, history, workspace, chart, application_state,
                                         notifications);
}

[[nodiscard]] inline auto dispatch_compose_bga_action(
    const wormedit::ui::workspace_action_t& action, document_history_t& history,
    workspace_controller_t& workspace, chart_id_t chart,
    notification_sink_t notifications = notification_sink_t::muted()) -> std::optional<std::string> {
    const auto window = wormedit::ui::compose_window_of(action);
    return dispatch_compose_bga_action(window, history, workspace, chart, notifications);
}

} // namespace wormedit::sys
