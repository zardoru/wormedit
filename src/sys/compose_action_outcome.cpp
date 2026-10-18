#include "sys/compose_action_context.h"
#include "sys/document_edit_feedback.h"
#include "sys/compose_audition_requests.h"
#include <cassert>
#include <utility>

namespace wormedit::sys {
auto compose_action_outcome_t::blocked(std::string message, notification_source_t source,
                                       compose_feedback_delivery_t delivery)
    -> compose_action_outcome_t {
    auto outcome = from(std::move(message), notification_severity_t::warning, source, false, false);
    outcome.delivery = delivery;
    return outcome;
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

auto compose_action_outcome_t::from(std::string message, notification_severity_t severity,
                                    notification_source_t source,
                                    compose_feedback_delivery_t delivery)
    -> compose_action_outcome_t {
    auto outcome = from(std::move(message), severity, source);
    outcome.delivery = delivery;
    return outcome;
}

auto compose_action_outcome_t::from(std::string_view message, notification_severity_t severity,
                                    notification_source_t source,
                                    compose_message_lifetime_t lifetime)
    -> compose_action_outcome_t {
    if (lifetime == compose_message_lifetime_t::owned)
        return from(std::string{message}, severity, source);
    return {.severity = severity, .source = source, .borrowed_message = message};
}

auto compose_action_outcome_t::from(std::string message, notification_severity_t severity,
                                    notification_source_t source, diagnostic_code_t code)
    -> compose_action_outcome_t {
    return {.severity = severity,
            .source = source,
            .accepted = false,
            .diagnostics = {error(code, std::move(message))},
            .delivery = compose_feedback_delivery_t::diagnostic_message};
}

auto compose_action_outcome_t::from(const editor_interaction_result_t& result,
                                    std::string_view accepted,
                                    notification_severity_t accepted_severity,
                                    notification_source_t source) -> compose_action_outcome_t {
    auto outcome = from(result.accepted ? accepted : "Compose action was rejected.",
                        result.accepted ? accepted_severity : notification_severity_t::warning,
                        source, compose_message_lifetime_t::borrowed);
    outcome.accepted = result.accepted;
    outcome.diagnostics = result.diagnostics;
    if (result.accepted) outcome.audition = result.audition;
    if (!result.accepted && !outcome.diagnostics.empty()) {
        outcome.severity = to_notification_severity(outcome.diagnostics.front().severity);
        outcome.delivery = compose_feedback_delivery_t::diagnostic_message;
    }
    return outcome;
}

auto compose_action_outcome_t::from(document_command_result_t&& command,
                                    const compose_action_description_t& description,
                                    compose_message_lifetime_t lifetime)
    -> compose_action_outcome_t {
    const auto is_success = command.applied;
    return from(std::move(command), is_success, description, lifetime);
}

void compose_action_outcome_t::apply_description(compose_action_outcome_t& outcome,
                                                 const compose_action_description_t& description) {
    outcome.source = !outcome.accepted && description.rejected_source ? *description.rejected_source
                                                                      : description.source;
    outcome.rebind_failure = description.rebind_failure;
    outcome.rebind_failure_policy = description.rebind_failure_policy;
    outcome.rebind_feedback_policy = description.rebind_feedback_policy;
    outcome.unknown_severity = description.unknown_severity;
}

auto compose_action_outcome_t::from(document_command_result_t&& command, bool is_success,
                                    const compose_action_description_t& description,
                                    compose_message_lifetime_t lifetime)
    -> compose_action_outcome_t {
    auto outcome =
        from({}, is_success ? notification_severity_t::success : description.unchanged_severity,
             description.source, command.applied,
             is_success || (description.is_unchanged_accepted && command.diagnostics.empty()));
    apply_description(outcome, description);
    outcome.diagnostics = std::move(command.diagnostics);
    if (!outcome.diagnostics.empty() &&
        (description.diagnostic_policy == compose_diagnostic_feedback_policy_t::first ||
         !is_success)) {
        if (lifetime == compose_message_lifetime_t::borrowed)
            outcome.delivery = compose_feedback_delivery_t::diagnostic_message;
        else
            outcome.message = outcome.diagnostics.front().message;
        outcome.severity = to_notification_severity(outcome.diagnostics.front().severity,
                                                    description.unknown_severity);
    } else {
        if (lifetime == compose_message_lifetime_t::borrowed)
            outcome.borrowed_message = is_success ? description.success : description.unchanged;
        else
            outcome.message = is_success ? description.success : description.unchanged;
    }
    return outcome;
}

auto compose_action_outcome_t::from(document_command_result_t&& command,
                                    const compose_action_description_t& description,
                                    std::string&& success) -> compose_action_outcome_t {
    auto outcome = from(std::move(command), description);
    if (outcome.mutated &&
        (outcome.diagnostics.empty() ||
         description.diagnostic_policy == compose_diagnostic_feedback_policy_t::rejected_only))
        outcome.message = std::move(success);
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

auto compose_action_outcome_t::from(editor_interaction_result_t&& interaction,
                                    std::string_view accepted, notification_source_t source,
                                    compose_interaction_message_policy_t message_policy)
    -> compose_action_outcome_t {
    auto message =
        interaction.accepted || message_policy == compose_interaction_message_policy_t::supplied
            ? std::string{accepted}
        : interaction.diagnostics.empty() ? "Compose action was rejected."
                                          : interaction.diagnostics.front().message;
    const auto severity =
        interaction.accepted
            ? interaction.document_changed ? notification_severity_t::success
                                           : notification_severity_t::hint
            : to_notification_severity(interaction.diagnostics, notification_severity_t::warning);
    auto outcome = from(std::move(message), severity,
                        interaction.document_changed ? notification_source_t::edit : source,
                        interaction.document_changed, interaction.accepted);
    outcome.rebind_failure = "Compose edit applied, but workspace rebinding failed.";
    outcome.diagnostics = std::move(interaction.diagnostics);
    if (interaction.accepted) outcome.audition = std::move(interaction.audition);
    return outcome;
}

namespace {
void post_compose_outcome(const notification_sink_t& feedback,
                          const compose_action_outcome_t& outcome) {
    if (outcome.delivery == compose_feedback_delivery_t::none)
        return;
    if (outcome.delivery == compose_feedback_delivery_t::transient_hint) {
        feedback.post_transient_hint(outcome.source, outcome.message_text());
        return;
    }
    if (outcome.delivery == compose_feedback_delivery_t::first_diagnostic &&
        !outcome.diagnostics.empty())
        feedback.post_diagnostic(outcome.source, outcome.diagnostics.front());
    else if (outcome.delivery == compose_feedback_delivery_t::notification)
        feedback.post(outcome.severity, outcome.source, outcome.message_text());
    else if (outcome.delivery == compose_feedback_delivery_t::diagnostic_message &&
             !outcome.diagnostics.empty())
        feedback.post_feedback(outcome.severity, outcome.source,
                               outcome.diagnostics.front().message);
    else
        feedback.post_feedback(outcome.severity, outcome.source, outcome.message_text());
}
void post_compose_mutation_before_failure(const notification_sink_t& feedback,
                                          const compose_action_outcome_t& outcome) {
    if (outcome.rebind_feedback_policy != compose_rebind_feedback_policy_t::edit_then_document)
        return;
    if (outcome.rebind_success_message.empty())
        post_compose_outcome(feedback, outcome);
    else
        feedback.post_feedback(outcome.severity, outcome.source, outcome.rebind_success_message);
}

void retain_compose_rebind_failure(compose_action_outcome_t& outcome,
                                   document_edit_feedback_t&& rebound) {
    outcome.accepted = false;
    outcome.has_rebind_failed = true;
    outcome.borrowed_message = {};
    outcome.delivery = compose_feedback_delivery_t::message;
    outcome.message = rebound.message_text();
    outcome.severity = rebound.severity;
    outcome.source = rebound.source;
    outcome.diagnostics = std::move(rebound.diagnostics);
}

auto finish_compose_outcome(const document_edit_inputs_t& document,
                            const notification_sink_t& feedback, compose_action_outcome_t outcome,
                            application_command_state_t* application) -> compose_action_outcome_t {
    // Admission precedes rebind: an accepted input already queued its audition
    // before a later rebind failure in the old pipeline. Playback stays scoped.
    if (document.auditions && outcome.accepted && outcome.audition) {
        document.auditions->submit(std::move(*outcome.audition));
        outcome.audition.reset();
    }
    const bool posted_before_rebind =
        outcome.feedback_order == compose_feedback_order_t::before_rebind;
    if (posted_before_rebind)
        post_compose_outcome(feedback, outcome);
    if (outcome.mutated || outcome.is_workspace_refresh_required) {
        auto rebound = rebind_document_edit(
            document.workspace, document.history,
            {.rebind_failure = outcome.rebind_failure,
             .is_message_diagnostic = outcome.rebind_failure_policy ==
                                      compose_rebind_failure_policy_t::diagnostic_or_message,
             .is_severity_diagnostic = outcome.is_rebind_severity_diagnostic,
             .unknown_severity = outcome.unknown_severity,
             .failure_severity = outcome.rebind_failure_severity,
             .source = outcome.source,
             .failure_source = outcome.rebind_failure_source.value_or(
                 outcome.rebind_feedback_policy == compose_rebind_feedback_policy_t::action_only
                     ? outcome.source : notification_source_t::document)});
        if (!rebound.accepted) {
            if (!posted_before_rebind)
                post_compose_mutation_before_failure(feedback, outcome);
            auto retained_message = outcome.is_message_retained_on_rebind_failure
                                        ? std::move(outcome.message)
                                        : std::string{};
            retain_compose_rebind_failure(outcome, std::move(rebound));
            post_compose_outcome(feedback, outcome);
            if (outcome.is_message_retained_on_rebind_failure)
                outcome.message = std::move(retained_message);
            return outcome;
        }
    }
    if (application && outcome.clear_semantic_layer_paint_after_rebind)
        application->project.compose_gesture.semantic_layer_paint_mode_active = false;
    if (outcome.asset_selection)
        static_cast<void>(apply_workspace_action(document.workspace, document.history.document,
                                                 *outcome.asset_selection));
    if (!posted_before_rebind)
        post_compose_outcome(feedback, outcome);
    return outcome;
}
} // namespace

auto finish_compose_action(const document_edit_inputs_t& document,
                           const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t {
    return finish_compose_outcome(document, feedback, std::move(outcome), nullptr);
}

auto finish_compose_action(const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t {
    assert(!outcome.mutated && !outcome.is_workspace_refresh_required);
    post_compose_outcome(feedback, outcome);
    return outcome;
}

auto finish_application_command(const notification_sink_t& feedback,
                                compose_action_outcome_t outcome,
                                const document_edit_inputs_t* document)
    -> dispatch_outcome_t {
    auto finished = document ? finish_compose_action(*document, feedback, std::move(outcome))
                             : finish_compose_action(feedback, std::move(outcome));
    if (finished.has_rebind_failed) {
        finished.diagnostics.resize(1);
        finished.diagnostics.front() =
            error(diagnostic_code_t::invalid_id, std::move(finished.message));
    } else if (!finished.are_command_diagnostics_retained) {
        finished.diagnostics.clear();
    }
    return {.value = finished.accepted, .diagnostics = std::move(finished.diagnostics)};
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
    if ((state).is_pending())
        return compose_action_outcome_t::blocked(std::string{message});
    return std::nullopt;
}

void retain_bms_statement_feedback(application_command_state_t& application,
                                   std::optional<wormedit::chart_id_t> chart,
                                   std::uint64_t revision, const std::string& message) {
    if (chart) {
        application.project.feedback.bms_branch_statement_feedback =
            application_command_state_t::bms_branch_statement_feedback_t{*chart, revision, message};
    } else {
        application.project.feedback.bms_branch_statement_feedback.reset();
    }
}

auto finish_compose_action(compose_action_context_t& context, compose_action_outcome_t outcome)
    -> std::string {
    auto finished =
        finish_compose_outcome(context.environment.document, context.notifications,
                               std::move(outcome), &context.environment.editor.commands);
    if (finished.has_bms_statement_feedback)
        retain_bms_statement_feedback(context.environment.editor.commands,
                                      context.environment.document.workspace.selected_chart,
                                      context.environment.document.history.document.revision,
                                      finished.message);
    return std::move(finished.message);
}
} // namespace compose_action_detail
} // namespace wormedit::sys
