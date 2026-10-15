#include "sys/compose_actions_internal.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {
auto edit_cursor_measure_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (const auto error = compose_measure_edit_rejection(
            context.state,
            context.environment.editor.commands.project.clipboard.paste_session.has_value()))
        return compose_action_outcome_t::blocked(*error);
    return std::nullopt;
}

auto edit_cursor_measure_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto revision = context.environment.document.history.document.revision;
    auto severity = notification_severity_t::warning;
    auto status = apply_cursor_measure_command(context.environment.document.history, context.state,
                                               command, &severity);
    const auto mutated = revision != context.environment.document.history.document.revision;
    auto outcome = compose_action_outcome_t::from(
        std::move(status), severity, notification_source_t::edit, mutated,
        mutated || severity == notification_severity_t::info);
    outcome.rebind_failure = "Measure edited, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    return outcome;
}

auto set_cursor_measure_meter_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing measures."))
        return std::move(*blocked);
    return std::nullopt;
}

auto set_cursor_measure_meter_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto revision = context.environment.document.history.document.revision;
    auto severity = notification_severity_t::warning;
    auto status =
        apply_cursor_measure_meter(context.environment.document.history, context.state,
                                   context.action.timing_meter_numerator_text,
                                   context.action.timing_meter_denominator_text, &severity);
    const auto mutated = revision != context.environment.document.history.document.revision;
    auto outcome = compose_action_outcome_t::from(
        std::move(status), severity, notification_source_t::edit, mutated,
        mutated || severity == notification_severity_t::info);
    outcome.rebind_failure = "Meter edited, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    return outcome;
}

auto set_measures_meter_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing measures."))
        return std::move(*blocked);
    return std::nullopt;
}

auto set_measures_meter_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto revision = context.environment.document.history.document.revision;
    auto severity = notification_severity_t::warning;
    const auto ids = std::span<const std::uint64_t>{context.action.compose_measure_ids,
                                                    context.action.compose_measure_id_count};
    auto status = apply_measures_meter(context.environment.document.history, context.state, ids,
                                       context.action.timing_meter_numerator_text,
                                       context.action.timing_meter_denominator_text, &severity);
    const auto mutated = revision != context.environment.document.history.document.revision;
    auto outcome = compose_action_outcome_t::from(
        std::move(status), severity, notification_source_t::edit, mutated,
        mutated || severity == notification_severity_t::info);
    outcome.rebind_failure = "Meter edited, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    return outcome;
}

auto move_compose_bar_line_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing measures."))
        return std::move(*blocked);
    if (context.action.compose_measure_id_count != 1 || !context.action.compose_measure_ids)
        return compose_action_outcome_t::blocked(
            "Bar line move ignored because the document changed.");
    return std::nullopt;
}

auto move_compose_bar_line_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto revision = context.environment.document.history.document.revision;
    auto severity = notification_severity_t::warning;
    const auto& action = context.action;
    auto status = apply_bar_line_move(
        context.environment.document.history, context.state, action.compose_measure_ids[0],
        {static_cast<std::uint32_t>(std::strtoul(action.timing_meter_numerator_text, nullptr, 10)),
         static_cast<std::uint32_t>(
             std::strtoul(action.timing_meter_denominator_text, nullptr, 10))},
        {action.compose_second_meter_numerator, action.compose_second_meter_denominator},
        &severity);
    const auto mutated = revision != context.environment.document.history.document.revision;
    auto outcome = compose_action_outcome_t::from(
        std::move(status), severity, notification_source_t::edit, mutated,
        mutated || severity == notification_severity_t::info);
    outcome.rebind_failure = "Bar line moved, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    return outcome;
}
} // namespace wormedit::sys::compose_action_detail
