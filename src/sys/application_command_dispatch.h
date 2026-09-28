#pragma once

#include "sys/application_commands.h"
#include "sys/compose_view.h"

#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <optional>
#include <string>
#include <string_view>

namespace wormedit::sys {

auto reject_application_command(
    std::string& status, std::string message,
    wormedit::diagnostic_code_t code = wormedit::diagnostic_code_t::unsupported_feature)
    -> wormedit::command_invocation_result_t;

auto accept_editor_command(const wormedit::editor_interaction_result_t& result, std::string& status,
                           std::string_view accepted) -> wormedit::command_invocation_result_t;

auto invoke_application_command(
    application_command_t command, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_state,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    application_command_state_t& application_state, std::string& status)
    -> wormedit::command_invocation_result_t;

} // namespace wormedit::sys
