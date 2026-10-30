#pragma once

#include "sys/app/application_edit_inputs.h"

#include "sys/metadata/metadata_asset_runtime.h"
#include "sys/metadata/metadata_drop_state.h"

#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/notifications.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {
struct document_history_t;
struct workspace_controller_t;
class task_scheduler_t;
}

namespace wormedit::ui { struct workspace_action_t; }

namespace wormedit::sys {

// Built-in profiles of the chart's key-count family, in catalog order. Empty when the chart's
// first layout is not a built-in BMS-family profile, so no key-count selector is offered.
[[nodiscard]] auto lane_profile_family_targets(const wormedit::chart_document_t& chart)
    -> std::vector<wormedit::lane_input_profile_t>;

// Returns text only when a metadata action was handled. Edit outcomes post here;
// copy/import start and completion outcomes are owned by the configured runtime.
auto dispatch_metadata_action(const wormedit::ui::workspace_action_t& action,
                              const metadata_action_inputs_t& environment, bool timing_locked,
                              bool file_jobs_busy,
                              bool post_notifications = true) -> std::optional<std::string>;

} // namespace wormedit::sys
