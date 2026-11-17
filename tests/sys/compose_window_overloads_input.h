#pragma once

// Test-only adapters: tests build the whole workspace_action_t, while the Compose dispatchers take
// their narrower windows. Production code builds the windows in sys/app/application.cpp.
#include "sys/compose/input/compose_input.h"
#include "ui/frontend/actions.h"

namespace wormedit::sys {

inline void refresh_compose_editor_width(wormedit::ui::workspace_action_t& action,
                                         const wormedit::document_history_t& history,
                                         const wormedit::workspace_controller_t& workspace,
                                         application_command_state_t& application) {
    const auto window = wormedit::ui::compose_input_window_of(action);
    refresh_compose_editor_width(window, history, workspace, application);
}

inline auto dispatch_compose_bgm_mix_inputs(
    wormedit::ui::workspace_action_t& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, application_command_state_t& application,
    const wormedit::notification_sink_t* notifications = nullptr) -> std::string {
    auto window = wormedit::ui::compose_input_window_of(action);
    return dispatch_compose_bgm_mix_inputs(window, history, workspace, application, notifications);
}

inline auto dispatch_compose_input_action(wormedit::ui::workspace_action_t& action,
                                          const compose_input_inputs_t& environment,
                                          wormedit::editor_interaction_state_t& state,
                                          double viewport_delta_seconds, bool post_notifications = true)
    -> std::string {
    auto window = wormedit::ui::compose_input_window_of(action);
    return dispatch_compose_input_action(window, environment, state, viewport_delta_seconds,
                                         post_notifications);
}

} // namespace wormedit::sys
