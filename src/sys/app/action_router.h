#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::sys {

// The one way a UI action reaches its handler. A domain declares its routes in a list; the router
// merges the lists of every domain and tries them by priority. Routes of equal priority are tried
// in the order the domains were handed to the router, and within a domain in the order that domain
// declared them, so a tie is never resolved by accident.
//
// Lower values run first. Add a band here only when a route has to run before or after every route
// of another band; two routes that merely must not both fire share a band and the claim that
// matters is "handled".
enum class route_priority_t : std::uint8_t {
    lifecycle,  // quit, project open/close, recovery: valid with or without a loaded session
    frame,      // frame-level routes that may consume anything below (clipboard menu, reviewed edits)
    domain,     // ordinary per-domain actions
    fallback,   // last chance for an action nothing else claimed
};

enum class route_status_t : std::uint8_t {
    declined,  // not this route's action; the router tries the next one
    handled,   // consumed; no later route sees the action
};

// What a route needs from the frame before it may run. A route that fails its policy is skipped, so
// the rule is written on the route and not as a branch in front of the chain.
struct route_policy_t {
    bool needs_session{true};          // skipped on the landing view (no project loaded)
    bool yields_to_shortcut{true};     // skipped when a registry command already consumed this frame

    auto operator==(const route_policy_t&) const -> bool = default;
};

// The frame facts a policy is checked against.
struct route_gate_t {
    bool session_loaded{};
    bool shortcut_handled{};

    [[nodiscard]] auto admits(const route_policy_t& policy) const -> bool {
        return (session_loaded || !policy.needs_session) && (!shortcut_handled || !policy.yields_to_shortcut);
    }
};

// A route handler sees the action and the environment the router was dispatched with.
template <class Action, class Environment>
struct action_route_t {
    std::string_view name;
    route_priority_t priority{route_priority_t::domain};
    route_policy_t policy{};
    std::function<route_status_t(const Action&, const Environment&)> run;
};

template <class Action, class Environment>
class action_router_t {
public:
    using route_t = action_route_t<Action, Environment>;

    // Domains are passed in declaration order: that order breaks priority ties.
    template <class... Domains>
    static auto from(Domains... domains) -> action_router_t {
        action_router_t router;
        (router.append(std::move(domains)), ...);
        std::ranges::stable_sort(router.routes_, {}, &route_t::priority);
        return router;
    }

    auto dispatch(const Action& action, const Environment& environment, const route_gate_t& gate) const
        -> route_status_t {
        for (const auto& route : routes_) {
            if (!gate.admits(route.policy)) continue;
            if (route.run(action, environment) == route_status_t::handled) return route_status_t::handled;
        }
        return route_status_t::declined;
    }

    // Routes in the order they are tried.
    [[nodiscard]] auto routes() const -> const std::vector<route_t>& { return routes_; }

    [[nodiscard]] auto has_unique_names() const -> bool {
        auto names = std::vector<std::string_view>{};
        names.reserve(routes_.size());
        for (const auto& route : routes_) names.push_back(route.name);
        std::ranges::sort(names);
        return std::ranges::adjacent_find(names) == names.end();
    }

private:
    void append(std::vector<route_t> domain) {
        for (auto& route : domain) routes_.push_back(std::move(route));
    }

    std::vector<route_t> routes_;
};

} // namespace wormedit::sys
