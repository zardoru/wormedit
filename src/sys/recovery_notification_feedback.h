#pragma once
#include "ui/frontend.h"
#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/notifications.h>
namespace wormedit::sys {
auto dispatch_recovery_ui_action(const WormUiWorkspaceAction& action,
    native_project_recovery_controller_t& controller, document_history_t& history,
    workspace_controller_t& workspace,
    std::optional<native_project_document_version_t> scanned_primary_version,
    native_project_autosave_loop_t& autosave_loop, bool& recovered,
    const notification_sink_t& notifications) -> std::string;
class recovery_scan_notification_publisher_t {
    std::optional<std::uint64_t> generation_;
public:
    auto publish(const native_project_recovery_controller_view_t& view,
                 const notification_sink_t& notifications) -> void;
};
}
