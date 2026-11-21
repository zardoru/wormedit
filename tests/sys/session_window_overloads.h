#pragma once

// Test-only adapters: tests build the whole workspace_action_t, while the transport, recovery and
// menu-command checks take only the requests they read.
#include "sys/audio/transport_player_actions.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/project/recovery_notification_feedback.h"
#include "ui/frontend/actions.h"

namespace wormedit::sys {

[[nodiscard]] inline auto dispatch_transport_player_action(
    const wormedit::ui::workspace_action_t& action, application_command_state_t& application_state,
    notification_sink_t notifications = notification_sink_t::muted()) -> std::optional<std::string> {
    return dispatch_transport_player_action(action.kind, action.transport, action.player_preview_settings,
                                            application_state, notifications);
}

inline auto compose_menu_command_is_current(const wormedit::ui::workspace_action_t& action,
    const document_history_t& history, const workspace_controller_t& workspace) -> bool {
    return compose_menu_command_is_current(action.compose, action.input.shortcuts, action.stamp, history,
                                           workspace);
}

inline auto dispatch_recovery_ui_action(const wormedit::ui::workspace_action_t& action,
    native_project_recovery_controller_t& controller, document_history_t& history,
    workspace_controller_t& workspace,
    std::optional<native_project_document_version_t> scanned_primary_version,
    native_project_autosave_loop_t& autosave_loop, bool& recovered,
    const notification_sink_t& notifications) -> std::string {
    return dispatch_recovery_ui_action(action.kind, action.recovery, controller, history, workspace,
                                       scanned_primary_version, autosave_loop, recovered, notifications);
}

} // namespace wormedit::sys
