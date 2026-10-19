#pragma once

#include "sys/application_edit_inputs.h"

#include "sys/compose_action_outcome.h"
#include "sys/compose_input.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/resource_job_state.h"

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

auto dispatch_compose_view_action(const WormUiWorkspaceAction& action,
                                  const compose_action_inputs_t& environment,
                                  wormedit::editor_interaction_state_t& state,
                                  double viewport_height, bool metadata_import_busy,
                                  bool post_notifications = true) -> std::string;

auto dispatch_compose_workspace_action(const WormUiWorkspaceAction& action,
                                       const compose_action_inputs_t& environment,
                                       double viewport_height, bool metadata_import_busy) -> bool;

} // namespace wormedit::sys
