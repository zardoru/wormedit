#pragma once

#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit {
class document_history_t;
class workspace_controller_t;
class task_scheduler_t;
}

struct WormUiWorkspaceAction;

namespace wormedit::sys {

// Returns a status only when one of the metadata actions was handled.
auto dispatch_metadata_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    bool resource_copy_busy, bool export_busy) -> std::optional<std::string>;

} // namespace wormedit::sys
