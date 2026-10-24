#include "sys/compose/actions/compose_actions_internal.h"
#include "sys/notifications/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {
auto recipe_form_target(compose_action_context_t& context) -> std::optional<recipe_form_target_t> {
    const auto* chart =
        context.environment.document.history.read_chart(context.state.chart);
    if (!chart || chart->id.value != context.action.compose_input_chart_id.value)
        return std::nullopt;
    const auto recipe = std::ranges::find(
        chart->sv_recipes, context.action.compose_recipe_id, &sv_recipe_t::id);
    if (recipe == chart->sv_recipes.end())
        return std::nullopt;
    return recipe_form_target_t{&*chart, &*recipe};
}

auto propose_recipe_form_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    target = recipe_form_target(context);
    if (!target)
        return compose_action_outcome_t::blocked(
            "SV recipe form ignored because the recipe or the document changed.");
    return std::nullopt;
}

auto propose_recipe_form_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& state = context.environment.editor.commands.project.timing_recipe;
    const bool needs_worker = context.action.compose_recipe_form.curve == sv_recipe_curve_t::lua_expression ||
        std::ranges::any_of(target->chart->sv_recipes, [](const auto& recipe) {
            return recipe.enabled && recipe.curve == sv_recipe_curve_t::lua_expression;
        });
    if (needs_worker) {
        if (!context.environment.resources)
            return compose_action_outcome_t::blocked("Lua recipe evaluation is unavailable in this context.");
        const auto& resources = *context.environment.resources;
        if (!state.evaluation)
            state.evaluation = std::make_shared<recipe_evaluation_runtime_t>(resources.jobs.scheduler);
        const auto* baseline = target->recipe;
        if (state.recipe_preview && state.recipe_preview->captured_stamp() ==
            chart_stamp_t::from(context.environment.document.history.document, target->chart->id) &&
            state.recipe_preview->report() && state.recipe_preview->report()->candidate.id == baseline->id)
            baseline = &state.recipe_preview->report()->candidate;
        return compose_action_outcome_t::from(state.evaluation->request(
            context.environment.document.history.document, *target->chart, *baseline,
            context.action.compose_recipe_form, resources.asset_base), notification_severity_t::hint,
            notification_source_t::input);
    }
    if (state.evaluation) state.evaluation->cancel();
    auto& previous = context.environment.editor.commands.project.timing_recipe.recipe_preview;
    auto preview = recipe_form_preview_t::revise(
        previous ? std::move(*previous) : recipe_form_outcome_t{},
        context.environment.document.history.document, *target->chart, *target->recipe,
        context.action.compose_recipe_form);
    const auto status = preview.diagnostics.empty() ? std::string{"SV recipe preview updated."}
                                                    : preview.diagnostics.front().message;
    const auto severity =
        preview.diagnostics.empty()
            ? notification_severity_t::hint
            : to_notification_severity(preview.diagnostics.front().severity);
    const auto source =
        preview.diagnostics.empty() ? notification_source_t::input : notification_source_t::edit;
    context.environment.editor.commands.project.timing_recipe.recipe_preview = std::move(preview);
    return compose_action_outcome_t::from(status, severity, source);
}

auto apply_recipe_form_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    target = recipe_form_target(context);
    if (!target)
        return compose_action_outcome_t::blocked(
            "SV recipe form ignored because the recipe or the document changed.");
    if (context.environment.editor.commands.project.timing_recipe.timing_locked)
        return compose_action_outcome_t::blocked("Timing is locked.");
    return std::nullopt;
}

auto apply_recipe_form_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& state = context.environment.editor.commands.project.timing_recipe;
    const bool needs_worker = context.action.compose_recipe_form.curve == sv_recipe_curve_t::lua_expression ||
        std::ranges::any_of(target->chart->sv_recipes, [](const auto& recipe) {
            return recipe.enabled && recipe.curve == sv_recipe_curve_t::lua_expression;
        });
    if (needs_worker) {
        const auto* report = state.recipe_preview ? state.recipe_preview->report() : nullptr;
        auto form = context.action.compose_recipe_form;
        form.lua_reload = false;
        if (!state.recipe_preview || !state.recipe_preview->value || !report || report->prepared.empty() ||
            !report->requested_form || form != *report->requested_form ||
            (state.evaluation && state.evaluation->is_busy()))
            return compose_action_outcome_t::blocked("Wait for a successful preview of these settings before Apply.");
        const auto result = apply_recipe_form(context.environment.document.history, *state.recipe_preview);
        auto outcome = compose_action_outcome_t::from(
            result.applied ? "SV recipe updated." : result.diagnostics.empty() ? "SV recipe is unchanged." : result.diagnostics.front().message,
            result.applied ? notification_severity_t::success : notification_severity_t::warning,
            notification_source_t::edit, result.applied, result.applied);
        if (result.applied) state.recipe_preview.reset();
        return outcome;
    }
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
auto clear_recipe_form_preview_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto clear_recipe_form_preview_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& state = context.environment.editor.commands.project.timing_recipe;
    if (state.evaluation) state.evaluation->cancel();
    state.recipe_preview.reset();
    return compose_action_outcome_t::from("SV recipe preview cleared.",
                                          notification_severity_t::hint,
                                          notification_source_t::input);
}
} // namespace wormedit::sys::compose_action_detail
