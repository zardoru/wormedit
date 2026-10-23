#pragma once

#include "sys/app/application_edit_inputs.h"

#include "sys/compose/actions/compose_action_outcome.h"
#include "sys/compose/input/compose_input.h"
#include "sys/metadata/metadata_asset_runtime.h"
#include "sys/metadata/metadata_drop_state.h"
#include "sys/resources/resource_job_state.h"

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {
auto apply_compose_hitsound(document_history_t& history, editor_interaction_state_t& state,
                            hitsound_change_t change, bool arm_only = false) -> compose_action_outcome_t;

auto dispatch_compose_view_action(const wormedit::ui::workspace_action_t& action,
                                  const compose_action_inputs_t& environment,
                                  wormedit::editor_interaction_state_t& state,
                                  double viewport_height, bool metadata_import_busy,
                                  bool post_notifications = true) -> std::string;

auto dispatch_compose_workspace_action(const wormedit::ui::workspace_action_t& action,
                                       const compose_action_inputs_t& environment,
                                       double viewport_height, bool metadata_import_busy) -> bool;

} // namespace wormedit::sys
