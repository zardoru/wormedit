#include "sys/compose_actions_internal.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {
auto recipe_form_target(compose_action_context_t& context) -> std::optional<recipe_form_target_t> {
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    if (chart == context.environment.document.history.document.charts.end() ||
        chart->id.value != context.action.compose_input_chart_id)
        return std::nullopt;
    const auto recipe = std::ranges::find(
        chart->sv_recipes, recipe_id_t{context.action.compose_recipe_id}, &sv_recipe_t::id);
    if (recipe == chart->sv_recipes.end())
        return std::nullopt;
    return recipe_form_target_t{&*chart, &*recipe};
}

compose_action_outcome_t propose_recipe_form_action(compose_action_context_t& context) {
    const auto target = recipe_form_target(context);
    if (!target)
        return compose_action_outcome_t::blocked(
            "SV recipe form ignored because the recipe or the document changed.");
    auto preview = wormedit::sys::propose_recipe_form(context.environment.document.history.document,
                                                      *target->chart, *target->recipe,
                                                      context.action.compose_recipe_form);
    const auto status = preview.diagnostics.empty() ? std::string{"SV recipe preview updated."}
                                                    : std::string{preview.message()};
    const auto severity =
        preview.diagnostics.empty()
            ? notification_severity_t::hint
            : projection_diagnostic_severity(preview.diagnostics.front().severity);
    const auto source =
        preview.diagnostics.empty() ? notification_source_t::input : notification_source_t::edit;
    context.environment.editor.commands.project.timing_recipe.recipe_preview = std::move(preview);
    return compose_action_outcome_t::from(status, severity, source);
}

compose_action_outcome_t apply_recipe_form_action(compose_action_context_t& context) {
    const auto target = recipe_form_target(context);
    if (!target)
        return compose_action_outcome_t::blocked(
            "SV recipe form ignored because the recipe or the document changed.");
    if (context.environment.editor.commands.project.timing_recipe.timing_locked)
        return compose_action_outcome_t::blocked("Timing is locked.");
    bool applied = false;
    auto severity = notification_severity_t::warning;
    auto status = wormedit::sys::apply_recipe_form(
        context.environment.document.history, target->chart->id, target->recipe->id,
        context.action.compose_recipe_form, applied, &severity);
    if (applied)
        context.environment.editor.commands.project.timing_recipe.recipe_preview.reset();
    auto outcome = compose_action_outcome_t::from(
        std::move(status), severity, notification_source_t::edit, applied,
        applied || severity == notification_severity_t::info);
    outcome.rebind_failure = "SV recipe updated, but workspace rebinding failed.";
    outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
    return outcome;
}
} // namespace wormedit::sys::compose_action_detail
