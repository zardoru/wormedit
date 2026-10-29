#pragma once
#include "sys/compose/actions/compose_action_context.h"
#include "sys/metadata/metadata_commands.h"
#include "sys/resources/resource_action_context.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace wormedit::sys::compose_action_detail {
enum class resource_route_policy_t { sound, bga_action, sound_layer_edit };

// Each constexpr module table owns its enum -> operation and boundary policy facts.
// find() uses one immutable index derived from those tables before input dispatch.
struct compose_action_route_t {
    WormUiComposeActionKind kind{};
    compose_action_outcome_t (*operation)(compose_action_context_t&){};
    bool bypasses_area_clear{};
    bool is_workspace_only{};
    bool requires_resources{};
    bool captures_semantic_target{};
    bool rejects_locked_target{};
    compose_action_outcome_t (*resource_operation)(const resource_action_context_t&,
                                                   application_command_state_t*){};
    bool is_standalone_only{};
    auto operator==(const compose_action_route_t&) const -> bool = default;
    static auto find(WormUiComposeActionKind kind) -> const compose_action_route_t*;
    static auto find(WormUiComposeActionKind kind, std::span<const compose_action_route_t> module)
        -> const compose_action_route_t* {
        const auto found = std::ranges::find(module, kind, &compose_action_route_t::kind);
        return found == module.end() ? nullptr : &*found;
    }
    template <class Action, notification_source_t Source,
              resource_route_policy_t Policy = resource_route_policy_t::sound>
    static constexpr auto from(WormUiComposeActionKind kind, bool is_standalone_only = false)
        -> compose_action_route_t;
};

inline constexpr std::size_t compose_action_kind_count = WORM_UI_COMPOSE_ACTION_COUNT;

// A module table names only real action kinds, each once. Every module checks itself with
// static_assert; the combined index still checks for the same kind in two modules at startup.
template <std::size_t Count>
constexpr auto is_valid_route_module(const std::array<compose_action_route_t, Count>& routes) -> bool {
    for (std::size_t at = 0; at < Count; ++at) {
        if (static_cast<std::size_t>(routes[at].kind) >= compose_action_kind_count) return false;
        for (std::size_t later = at + 1; later < Count; ++later)
            if (routes[at].kind == routes[later].kind) return false;
    }
    return true;
}

template <class Action, auto... Parameters>
auto run_routed_compose_action(compose_action_context_t& context) -> compose_action_outcome_t {
    if constexpr (requires { Action::from(context, Parameters...); })
        return run_compose_action(Action::from(context, Parameters...), context);
    else if constexpr (requires { Action::from(Parameters...); })
        return run_compose_action(Action::from(Parameters...), context);
    else
        return run_compose_action(Action{Parameters...}, context);
}

// Standalone resource APIs and the Compose boundary share these stamp/source rules.
// The Compose entry additionally preserves its no-gesture guard before the resource boundary.
template <class Action, notification_source_t Source, resource_route_policy_t Policy>
auto run_routed_native_resource_action(const resource_action_context_t& context,
                                       application_command_state_t* application)
    -> compose_action_outcome_t {
    if constexpr (Policy == resource_route_policy_t::bga_action) {
        if (context.document.history.document.revision !=
            context.action.compose_input_document_revision)
            return compose_action_outcome_t::blocked(stale_action_message("BGA action"), Source);
    }
    if constexpr (Policy == resource_route_policy_t::sound_layer_edit) {
        if (auto rejected = metadata_command_detail::stale_rejection(
                context.document.history, context.action.compose_input_document_revision,
                "Sound layer edit"))
            return compose_action_outcome_t::from(rejected->diagnostics.front(),
                                                  notification_source_t::edit,
                                                  notification_severity_t::info);
    }
    compose_action_outcome_t outcome;
    if constexpr (requires { Action::from(application); })
        outcome = run_compose_action(Action::from(application), context);
    else
        outcome = run_compose_action(Action{}, context);
    if constexpr (Policy != resource_route_policy_t::bga_action)
        outcome.source = Source;
    return outcome;
}

template <class Action, notification_source_t Source, resource_route_policy_t Policy>
auto run_routed_resource_action(compose_action_context_t& context) -> compose_action_outcome_t {
    if (auto blocked = context.require_no_gesture(
            Policy == resource_route_policy_t::bga_action
                ? "Finish or cancel the active Compose gesture before editing BGA."
                : "Finish or cancel the active Compose gesture before editing sounds.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    const resource_action_context_t resource{context.action, context.environment.document,
                                             context.state.chart};
    return run_routed_native_resource_action<Action, Source, Policy>(
        resource, &context.environment.editor.commands);
}

template <class Action, notification_source_t Source, resource_route_policy_t Policy>
constexpr auto compose_action_route_t::from(WormUiComposeActionKind kind, bool is_standalone_only)
    -> compose_action_route_t {
    return {.kind = kind,
            .operation = run_routed_resource_action<Action, Source, Policy>,
            .resource_operation = run_routed_native_resource_action<Action, Source, Policy>,
            .is_standalone_only = is_standalone_only};
}

extern const std::span<const compose_action_route_t> compose_local_routes;
extern const std::span<const compose_action_route_t> compose_resource_routes;
extern const std::span<const compose_action_route_t> compose_marker_routes;
extern const std::span<const compose_action_route_t> compose_generator_routes;
extern const std::span<const compose_action_route_t> compose_bookmark_routes;
extern const std::span<const compose_action_route_t> compose_area_routes;
extern const std::span<const compose_action_route_t> compose_bga_routes;
extern const std::span<const compose_action_route_t> compose_sound_routes;
auto process_compose_area_boundary(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t>;
extern const std::span<const compose_action_route_t> compose_quantize_routes;
} // namespace wormedit::sys::compose_action_detail
