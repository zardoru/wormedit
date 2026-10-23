#pragma once
#include "ui/frontend.h"
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/workspace.h>
#include <optional>
#include <string>
namespace wormedit::sys {
// A failure message leaves the workspace and interaction untouched.
[[nodiscard]] auto dispatch_bga_usage_action(const ui::workspace_action_t& action,
    const ui::bga_dock_request_t& request, document_history_t& history,
    workspace_controller_t& workspace, std::optional<editor_interaction_state_t>& interaction)
    -> std::optional<std::string>;
} // namespace wormedit::sys
