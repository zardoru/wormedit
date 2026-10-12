#pragma once

#include "sys/application_dispatch_context.h"

#include "sys/compose_input.h"
#include "sys/metadata_drop_state.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/resource_job_state.h"

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

// Shared inspector/registry quantize path; preview is session state, Apply records the usual report.
auto invoke_compose_quantize_command(bool toggle_preview, wormedit::document_history_t& history,
    wormedit::editor_interaction_state_t& state, application_command_state_t& application,
    std::optional<wormedit::notification_sink_t> notifications = {}) -> bool;

auto dispatch_compose_view_action(const WormUiWorkspaceAction& action,
                                  application_dispatch_context_t& environment,
                                  wormedit::editor_interaction_state_t& state,
                                  double viewport_height, bool metadata_import_busy,
                                  bool post_notifications = true) -> std::string;

auto dispatch_compose_workspace_action(const WormUiWorkspaceAction& action,
                                       application_dispatch_context_t& environment,
                                       double viewport_height, bool metadata_import_busy) -> bool;

} // namespace wormedit::sys
