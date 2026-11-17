#pragma once

// Test-only adapters: tests build the whole workspace_action_t, while the Compose dispatchers take
// their narrower windows. Production code builds the windows explicitly in sys/app/application.cpp.
#include "sys/compose/actions/compose_actions.h"
#include "sys/compose/actions/compose_bga_actions.h"
#include "sys/compose/actions/compose_sound_actions.h"
#include "sys/compose/input/compose_input.h"
#include "ui/frontend/actions.h"

namespace wormedit::sys {

inline auto dispatch_compose_view_action(const wormedit::ui::workspace_action_t& action,
                                         const compose_action_inputs_t& environment,
                                         wormedit::editor_interaction_state_t& state,
                                         double viewport_height, bool metadata_import_busy,
                                         bool post_notifications = true) -> std::string {
    const auto window = wormedit::ui::compose_window_of(action);
    return dispatch_compose_view_action(window, environment, state, viewport_height,
                                        metadata_import_busy, post_notifications);
}

inline auto dispatch_compose_workspace_action(const wormedit::ui::workspace_action_t& action,
                                              const compose_action_inputs_t& environment,
                                              double viewport_height, bool metadata_import_busy)
    -> bool {
    const auto window = wormedit::ui::compose_window_of(action);
    return dispatch_compose_workspace_action(window, environment, viewport_height,
                                             metadata_import_busy);
}

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
