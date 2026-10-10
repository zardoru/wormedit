#pragma once

#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"

#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/notifications.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {
class document_history_t;
class workspace_controller_t;
class task_scheduler_t;
}

struct WormUiWorkspaceAction;

namespace wormedit::sys {

// Built-in profiles of the chart's key-count family, in catalog order. Empty when the chart's
// first layout is not a built-in BMS-family profile, so no key-count selector is offered.
[[nodiscard]] auto lane_profile_family_targets(const wormedit::chart_document_t& chart)
    -> std::vector<wormedit::lane_input_profile_t>;

// Returns text only when a metadata action was handled. Edit outcomes post here;
// copy/import start and completion outcomes are owned by the configured runtime.
auto dispatch_metadata_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    bool resource_copy_busy, bool export_busy,
    std::optional<notification_sink_t> notifications = {}) -> std::optional<std::string>;

} // namespace wormedit::sys
