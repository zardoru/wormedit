#include "sys/compose_action_context.h"
#include <utility>

namespace wormedit::sys {
auto compose_action_outcome_t::blocked(std::string message, notification_source_t source)
    -> compose_action_outcome_t {
    return from(std::move(message), notification_severity_t::warning, source, false, false);
}

auto compose_action_outcome_t::from(std::string message, notification_severity_t severity,
                                    notification_source_t source, bool mutated, bool accepted)
    -> compose_action_outcome_t {
    return {.message = std::move(message),
            .severity = severity,
            .source = source,
            .mutated = mutated,
            .accepted = accepted};
}

auto compose_action_outcome_t::from(const diagnostic_t& diagnostic, notification_source_t source)
    -> compose_action_outcome_t {
    return from(diagnostic.message, wormedit::to_notification_severity(diagnostic.severity), source,
                false, false);
}

auto compose_action_outcome_t::from(document_command_result_t&& command, std::string_view success,
                                    std::string_view unchanged, std::string_view rebind_failure,
                                    notification_source_t source,
                                    compose_diagnostic_feedback_policy_t diagnostic_policy)
    -> compose_action_outcome_t {
    auto outcome =
        from({}, command.applied ? notification_severity_t::success : notification_severity_t::info,
             source, command.applied, command.applied || command.diagnostics.empty());
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(command.diagnostics);
    if (!outcome.diagnostics.empty() &&
        (diagnostic_policy == compose_diagnostic_feedback_policy_t::first || !command.applied)) {
        outcome.message = outcome.diagnostics.front().message;
        outcome.severity = to_notification_severity(outcome.diagnostics.front().severity);
    } else {
        outcome.message = command.applied ? success : unchanged;
    }
    return outcome;
}

auto compose_action_outcome_t::from(diagnostic_list_t&& diagnostics, notification_source_t source,
                                    compose_feedback_delivery_t delivery)
    -> compose_action_outcome_t {
    auto outcome = diagnostics.empty() ? blocked({}, source) : from(diagnostics.front(), source);
    outcome.diagnostics = std::move(diagnostics);
    outcome.delivery = delivery;
    return outcome;
}

namespace {
void post_compose_outcome(const notification_sink_t& feedback,
                          const compose_action_outcome_t& outcome) {
    if (outcome.delivery == compose_feedback_delivery_t::first_diagnostic &&
        !outcome.diagnostics.empty())
        feedback.post_diagnostic(outcome.source, outcome.diagnostics.front());
    else
        feedback.post_feedback(outcome.severity, outcome.source, outcome.message);
}
} // namespace

auto finish_compose_action(const document_edit_inputs_t& document,
                           const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t {
    if (outcome.mutated) {
        auto rebound = rebind_workspace(document.workspace, document.history);
        if (!rebound) {
            if (outcome.publish_before_rebind_failure)
                post_compose_outcome(feedback, outcome);
            outcome.accepted = false;
            outcome.source = notification_source_t::document;
            outcome.delivery = compose_feedback_delivery_t::message;
            if (outcome.rebind_failure_policy ==
                    compose_rebind_failure_policy_t::diagnostic_or_message &&
                !rebound.diagnostics.empty()) {
                outcome.message = rebound.diagnostics.front().message;
                outcome.severity = to_notification_severity(rebound.diagnostics.front().severity);
            } else {
                outcome.message = outcome.rebind_failure;
                outcome.severity = notification_severity_t::error;
            }
            outcome.diagnostics = std::move(rebound.diagnostics);
            post_compose_outcome(feedback, outcome);
            return outcome;
        }
    }
    post_compose_outcome(feedback, outcome);
    return outcome;
}

namespace compose_action_detail {
auto compose_action_context_t::require_resources() const
    -> std::optional<compose_action_outcome_t> {
    if (!environment.resources)
        return compose_action_outcome_t::blocked("Resource services are unavailable.");
    return std::nullopt;
}

auto compose_action_context_t::require_no_gesture(std::string_view message) const
    -> std::optional<compose_action_outcome_t> {
    if (wormedit::has_pending_editor_gesture(state))
        return compose_action_outcome_t::blocked(std::string{message});
    return std::nullopt;
}

auto finish_compose_action(compose_action_context_t& context, compose_action_outcome_t outcome)
    -> std::string {
    auto finished = wormedit::sys::finish_compose_action(context.environment.document,
                                                         context.notifications, std::move(outcome));
    return std::move(finished.message);
}
} // namespace compose_action_detail
} // namespace wormedit::sys
