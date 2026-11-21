#pragma once

#include <span>
#include <wormedit/core/resource_assignment.h>

namespace wormedit {

// Publishes a set of verified replacement paths as one undoable edit.
[[nodiscard]] auto apply_resource_asset_relinks(document_history_t& history,
    std::span<const relink_resource_asset_action_t> actions) -> document_command_result_t;

} // namespace wormedit
