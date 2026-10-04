#pragma once
#include "ui/frontend.h"
#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>
#include <optional>
#include <span>
#include <string>
namespace wormedit::sys {
[[nodiscard]] auto import_bga_files(document_history_t& history, workspace_controller_t& workspace,
                                   chart_id_t chart, std::span<const std::string> paths,
                                   std::optional<notification_sink_t> notifications = {}) -> std::string;
[[nodiscard]] auto dispatch_compose_bga_action(const WormUiWorkspaceAction& action,
    document_history_t& history, workspace_controller_t& workspace, chart_id_t chart,
    std::optional<notification_sink_t> notifications = {})
    -> std::optional<std::string>;
}
