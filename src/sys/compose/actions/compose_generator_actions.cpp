#include "sys/compose/actions/compose_actions_internal.h"
#include "sys/compose/generator_runtime.h"
namespace wormedit::sys::compose_action_detail {
namespace {
struct generator_action_t {
    WormUiComposeActionKind kind;
    auto operator==(const generator_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t> {
        if (auto blocked = context.require_no_gesture("Finish or cancel the current gesture first.")) return blocked;
        const auto& action = context.action;
        const auto& document = context.environment.document.history.document;
        if (action.compose_input_project_id != document.id || action.compose_input_document_revision != document.revision ||
            action.compose_input_chart_id != context.state.chart)
            return compose_action_outcome_t::blocked("Generator action is stale; review the current chart.");
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
auto feedback(document_command_result_t result) -> compose_action_outcome_t {
    return compose_action_outcome_t::from(std::move(result),
        compose_action_description_t{.rebind_failure = "Generator committed, but workspace rebinding failed.",
            .source = notification_source_t::edit});
}
auto request_generator(compose_action_context_t& context, WormUiComposeActionKind kind) -> compose_action_outcome_t {
    auto& state = context.environment.editor.commands.project.generator;
    const auto& form = context.action.compose_generator_form;
    auto& history = context.environment.document.history;
    if (!context.environment.resources) return compose_action_outcome_t::blocked("Generator worker is unavailable.");
    if (!state.runtime) state.runtime = std::make_shared<generator_runtime_t>(context.environment.resources->jobs.scheduler);
    auto selection = context.state.selected_objects;
    for (const auto id : context.state.selected_notes) {
        const editor_selection_object_t item{editor_selection_object_kind_t::note, id};
        if (std::ranges::find(selection, item) == selection.end()) selection.push_back(item);
    }
    return compose_action_outcome_t::from(state.runtime->request(history, context.state.chart, form, std::move(selection),
        context.environment.resources->asset_base, kind == WORM_UI_COMPOSE_ACTION_GENERATOR_CONFIRM),
        notification_severity_t::hint, notification_source_t::input);
}
auto generator_action_t::perform(compose_action_context_t& context) const -> compose_action_outcome_t {
    auto& state = context.environment.editor.commands.project.generator;
    const auto& form = context.action.compose_generator_form;
    auto& history = context.environment.document.history;
    if (kind == WORM_UI_COMPOSE_ACTION_GENERATOR_SELECT) {
        if (state.runtime) state.runtime->cancel();
        state.selected = form.recipe;
        return compose_action_outcome_t::from("Generator selected.", notification_severity_t::hint, notification_source_t::input);
    }
    if (kind == WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL) {
        if (state.runtime) state.runtime->cancel();
        return compose_action_outcome_t::from("Generator preview cancelled.", notification_severity_t::hint, notification_source_t::input);
    }
    if (kind == WORM_UI_COMPOSE_ACTION_GENERATOR_DETACH) {
        if (state.runtime) state.runtime->cancel();
        state.selected = {};
        return feedback(detach_generator(history, form.recipe));
    }
    if (kind == WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY) {
        if (!state.runtime) return compose_action_outcome_t::blocked("Run a generator before Apply.");
        const auto result = state.runtime->apply(history, form);
        if (result.applied && form.keep_recipe) {
            const auto& recipes = history.document.generator_recipes;
            if (!recipes.empty()) state.selected = form.recipe ? form.recipe : recipes.back().id;
        }
        return feedback(result);
    }
    return request_generator(context, kind);
}

constexpr auto routes = std::array{
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GENERATOR_SELECT,
        .operation = run_routed_compose_action<generator_action_t, WORM_UI_COMPOSE_ACTION_GENERATOR_SELECT>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GENERATOR_RUN,
        .operation = run_routed_compose_action<generator_action_t, WORM_UI_COMPOSE_ACTION_GENERATOR_RUN>, .captures_semantic_target = true, .rejects_locked_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GENERATOR_CONFIRM,
        .operation = run_routed_compose_action<generator_action_t, WORM_UI_COMPOSE_ACTION_GENERATOR_CONFIRM>, .captures_semantic_target = true, .rejects_locked_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY,
        .operation = run_routed_compose_action<generator_action_t, WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY>, .captures_semantic_target = true, .rejects_locked_target = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL,
        .operation = run_routed_compose_action<generator_action_t, WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_GENERATOR_DETACH,
        .operation = run_routed_compose_action<generator_action_t, WORM_UI_COMPOSE_ACTION_GENERATOR_DETACH>}};
static_assert(is_valid_route_module(routes));
}
constinit const std::span<const compose_action_route_t> compose_generator_routes{routes};
}
