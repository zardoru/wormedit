#pragma once

#include "sys/compose_input.h"
#include "sys/metadata_drop_state.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/resource_job_state.h"

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

auto dispatch_compose_view_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime,
    runtime::output_group_export_t& export_runtime, bool metadata_import_busy) -> std::string;

auto dispatch_compose_workspace_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base,
    wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime,
    runtime::output_group_export_t& export_runtime, bool metadata_import_busy,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    std::string& status) -> bool;

} // namespace wormedit::sys
