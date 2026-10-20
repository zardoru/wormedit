#pragma once

#include "sys/application_edit_inputs.h"

#include "sys/application_commands.h"
#include "sys/compose_view.h"

#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/notifications.h>

#include <optional>
#include <string>
#include <string_view>

namespace wormedit::sys {
struct notification_history_runtime_t;

// Clear chart-specific state while retaining application services and user settings.
void reset_application_project_edit_state(application_command_state_t& state);

// The command-kind owner defines which application commands require editable
// Compose notes. Registry state and refusal feedback share this policy.
[[nodiscard]] auto application_command_disabled_by_timing_focus(
    application_command_kind_t kind) noexcept -> bool;
void apply_timing_focus_command_policy(command_registry_t& registry,
                                       bool compose_notes_editable);

auto unavailable_application_command_message(const command_registry_t& registry,
    const std::optional<command_id_t>& id, const application_command_state_t& state) -> std::string;

auto invoke_notification_history_command(application_command_t command, bool auto_repeat,
    notification_history_runtime_t& runtime, notification_center_t& center)
    -> std::optional<wormedit::dispatch_outcome_t>;


auto reject_application_command(
    notification_sink_t& sink, std::string message, notification_severity_t severity,
    notification_source_t source,
    wormedit::diagnostic_code_t code = wormedit::diagnostic_code_t::unsupported_feature)
    -> wormedit::dispatch_outcome_t;



// Stamped Compose activations must be checked before refreshing/invoking the registry.
inline auto compose_menu_command_is_current(const wormedit::ui::workspace_action_t& action,
    const document_history_t& history, const workspace_controller_t& workspace) -> bool {
    return !action.compose_menu_command ||
        (action.shortcut_window_focused && !action.shortcut_ime_composition_active &&
         workspace.active_tab == workspace_tab_t::compose &&
         action.compose_input_project_id == history.document.id.value &&
         workspace.selected_chart && action.compose_input_chart_id == workspace.selected_chart->value &&
         action.compose_input_document_revision == history.document.revision);
}

// External-player, play/pause, play-from-start, and cancel/restore callbacks own
// publication at their typed producer bridge. Their returned text is retained for
// legacy callers; this dispatcher does not classify or repost that prose.
auto invoke_application_command(application_command_t command,
                                const command_dispatch_inputs_t& environment)
    -> wormedit::dispatch_outcome_t;

} // namespace wormedit::sys
