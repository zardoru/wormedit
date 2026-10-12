#pragma once

#include "sys/application_dispatch_context.h"

#include "sys/application_commands.h"
#include "sys/compose_view.h"

#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction.h>
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

auto invoke_notification_history_command(application_command_t command, bool auto_repeat,
    notification_history_runtime_t& runtime, notification_center_t& center)
    -> std::optional<wormedit::command_invocation_result_t>;


auto reject_application_command(
    notification_sink_t& sink, std::string message, notification_severity_t severity,
    notification_source_t source,
    wormedit::diagnostic_code_t code = wormedit::diagnostic_code_t::unsupported_feature)
    -> wormedit::command_invocation_result_t;

auto accept_editor_command(const wormedit::editor_interaction_result_t& result,
                           notification_sink_t& sink, std::string_view accepted,
                           notification_severity_t accepted_severity,
                           notification_source_t source) -> wormedit::command_invocation_result_t;

// Stamped Compose activations must be checked before refreshing/invoking the registry.
inline auto compose_menu_command_is_current(const WormUiWorkspaceAction& action,
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
                                application_dispatch_context_t& environment)
    -> wormedit::command_invocation_result_t;

} // namespace wormedit::sys
