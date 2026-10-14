#include "sys/compose_actions_internal.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {
compose_action_outcome_t edit_selected_note_labels(compose_action_context_t& context) {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing note labels."))
        return std::move(*blocked);
    if (context.state.selected_notes.empty())
        return compose_action_outcome_t::blocked(
            "Select one or more notes before editing their label.");
    const auto label = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
                           ? std::optional<std::string>{context.action.compose_note_label}
                           : std::nullopt;
    auto applied = apply_note_label_edit(
        context.environment.document.history,
        {.chart = context.state.chart, .notes = context.state.selected_notes, .label = label});
    return compose_action_outcome_t::from(
        std::move(applied), label ? "Note label applied." : "Note labels cleared.",
        label ? "Selected notes already have that label." : "Selected notes already have no label.",
        "Note label changed, but workspace rebinding failed.");
}

compose_action_outcome_t search_selected_note_labels(compose_action_context_t& context) {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before searching labels."))
        return std::move(*blocked);
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

auto selection_transform_for(WormUiComposeActionKind kind) -> std::optional<selection_transform_t> {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR:
        return selection_transform_t::mirror;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT:
        return selection_transform_t::rotate_left;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT:
        return selection_transform_t::rotate_right;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER:
        return selection_transform_t::shift_earlier;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER:
        return selection_transform_t::shift_later;
    default:
        return std::nullopt;
    }
}

compose_action_outcome_t transform_selected_notes(compose_action_context_t& context,
                                                  selection_transform_t transform) {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before transforming notes."))
        return std::move(*blocked);
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
