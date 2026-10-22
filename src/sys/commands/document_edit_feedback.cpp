#include "sys/commands/document_edit_feedback.h"

#include <utility>

namespace wormedit::sys {

auto document_edit_feedback_t::from(dispatch_outcome_t rebound,
                                    const document_edit_description_t& description)
    -> document_edit_feedback_t {
    if (rebound.is_accepted(diagnostic_acceptance_policy_t::require_empty))
        return {.accepted = true, .message = description.success, .source = description.source};

    const bool use_diagnostic = description.is_message_diagnostic && !rebound.diagnostics.empty();
    const auto severity = use_diagnostic && description.is_severity_diagnostic
                              ? to_notification_severity(rebound.diagnostics.front().severity,
                                                         description.unknown_severity)
                              : notification_severity_t::error;
    return {.severity = description.failure_severity.value_or(severity),
            .message = description.rebind_failure,
            .is_message_diagnostic = description.is_message_diagnostic,
            .source = description.failure_source.value_or(description.source),
            .diagnostics = std::move(rebound.diagnostics)};
}

auto rebind_document_edit(workspace_controller_t& workspace, const document_history_t& history,
                          const document_edit_description_t& description)
    -> document_edit_feedback_t {
    auto rebound = rebind_workspace(workspace, history);
    return document_edit_feedback_t::from(std::move(rebound), description);
}

} // namespace wormedit::sys
