#pragma once

// Test-only adapters: tests build the whole workspace_action_t, while the Compose dispatchers take
// their narrower windows. Production code builds the windows in sys/app/application.cpp.
#include "sys/compose/actions/compose_actions.h"
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

} // namespace wormedit::sys
