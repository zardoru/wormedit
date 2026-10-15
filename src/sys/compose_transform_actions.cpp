#include "sys/compose_actions_internal.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {
auto edit_selected_note_labels_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing note labels."))
        return std::move(*blocked);
    if (context.state.selected_notes.empty())
        return compose_action_outcome_t::blocked(
            "Select one or more notes before editing their label.");
    return std::nullopt;
}

auto edit_selected_note_labels_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto label = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
                           ? std::optional<std::string>{context.action.compose_note_label}
                           : std::nullopt;
    auto applied = apply_note_label_edit(
        context.environment.document.history,
        {.chart = context.state.chart, .notes = context.state.selected_notes, .label = label});
    return compose_action_outcome_t::from(std::move(applied), compose_action_description_t{.success = label ? "Note label applied." : "Note labels cleared.", .unchanged = label ? "Selected notes already have that label." : "Selected notes already have no label.", .rebind_failure = "Note label changed, but workspace rebinding failed."});
}

auto search_selected_note_labels_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before searching labels."))
        return std::move(*blocked);
    return std::nullopt;
}

auto search_selected_note_labels_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto step =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL
            ? label_search_step_t::next
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL
            ? label_search_step_t::previous
            : label_search_step_t::first;
    auto severity = notification_severity_t::warning;
    auto status = run_label_search(context.environment.document.history, context.state,
                                   context.action.compose_note_label, step, &severity);
    return compose_action_outcome_t::from(std::move(status), severity,
                                          notification_source_t::input);
}

auto transform_selected_notes_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before transforming notes."))
        return std::move(*blocked);
    return std::nullopt;
}

auto transform_selected_notes_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto revision = context.environment.document.history.document.revision;
    auto severity = notification_severity_t::warning;
    auto status = apply_selection_transform(context.environment.document.history, context.state,
                                            transform, &severity);
    const auto mutated = revision != context.environment.document.history.document.revision;
    auto outcome = compose_action_outcome_t::from(
        std::move(status), severity, notification_source_t::edit, mutated,
        mutated || severity == notification_severity_t::info);
    outcome.rebind_failure = "Transform applied, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    return outcome;
}
} // namespace wormedit::sys::compose_action_detail
