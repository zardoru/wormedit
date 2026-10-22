#pragma once
#include "sys/compose/actions/compose_action_outcome.h"
#include "ui/frontend.h"
#include <optional>
#include <span>
#include <string>
#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>
namespace wormedit::sys {

[[nodiscard]] auto
import_bga_files(document_history_t& history, workspace_controller_t& workspace, chart_id_t chart,
                 std::span<const std::string> paths,
                 notification_sink_t notifications = notification_sink_t::muted()) -> std::string;
[[nodiscard]] auto
dispatch_compose_bga_action(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                            workspace_controller_t& workspace, chart_id_t chart,
                            notification_sink_t notifications = notification_sink_t::muted())
    -> std::optional<std::string>;
} // namespace wormedit::sys
