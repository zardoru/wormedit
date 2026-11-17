#include "sys/app/action_router.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace wormedit::sys;

namespace {
struct test_environment_t {
    std::vector<std::string>& log;
};
using test_router_t = action_router_t<int, test_environment_t>;
using test_route_t = test_router_t::route_t;

auto route(std::string_view name, route_priority_t priority, route_status_t result, route_policy_t policy = {})
    -> test_route_t {
    return {name, priority, policy, [name, result](const int&, const test_environment_t& environment) {
                environment.log.emplace_back(name);
                return result;
            }};
}

auto order_of(const test_router_t& router) -> std::vector<std::string_view> {
    auto names = std::vector<std::string_view>{};
    for (const auto& entry : router.routes()) names.push_back(entry.name);
    return names;
}
}  // namespace

TEST_CASE("action router orders by priority, then by domain declaration order", "[action-router]") {
    const auto router = test_router_t::from(
        std::vector{route("a.domain", route_priority_t::domain, route_status_t::declined),
                    route("a.lifecycle", route_priority_t::lifecycle, route_status_t::declined)},
        std::vector{route("b.fallback", route_priority_t::fallback, route_status_t::declined),
                    route("b.domain", route_priority_t::domain, route_status_t::declined),
                    route("b.lifecycle", route_priority_t::lifecycle, route_status_t::declined)});
    CHECK(order_of(router) == std::vector<std::string_view>{"a.lifecycle", "b.lifecycle", "a.domain", "b.domain",
                                                             "b.fallback"});
    CHECK(router.has_unique_names());
}

TEST_CASE("action router stops at the first handled route", "[action-router]") {
    const auto router = test_router_t::from(
        std::vector{route("first", route_priority_t::domain, route_status_t::declined),
                    route("second", route_priority_t::domain, route_status_t::handled),
                    route("third", route_priority_t::domain, route_status_t::handled)});
    std::vector<std::string> log;
    CHECK(router.dispatch(0, {log}, {.session_loaded = true}) == route_status_t::handled);
    CHECK(log == std::vector<std::string>{"first", "second"});
}

TEST_CASE("action router reports declined when nothing claims the action", "[action-router]") {
    const auto router = test_router_t::from(
        std::vector{route("only", route_priority_t::domain, route_status_t::declined)});
    std::vector<std::string> log;
    CHECK(router.dispatch(0, {log}, {.session_loaded = true}) == route_status_t::declined);
    CHECK(log == std::vector<std::string>{"only"});
}

TEST_CASE("action router skips routes whose policy the frame does not admit", "[action-router]") {
    constexpr route_policy_t any_state{.needs_session = false, .yields_to_shortcut = false};
    const auto router = test_router_t::from(
        std::vector{route("lifecycle", route_priority_t::lifecycle, route_status_t::declined, any_state),
                    route("widget", route_priority_t::domain, route_status_t::declined)});
    std::vector<std::string> log;

    static_cast<void>(router.dispatch(0, {log}, {.session_loaded = false}));
    CHECK(log == std::vector<std::string>{"lifecycle"});

    log.clear();
    static_cast<void>(router.dispatch(0, {log}, {.session_loaded = true, .shortcut_handled = true}));
    CHECK(log == std::vector<std::string>{"lifecycle"});

    log.clear();
    static_cast<void>(router.dispatch(0, {log}, {.session_loaded = true}));
    CHECK(log == std::vector<std::string>{"lifecycle", "widget"});
}

TEST_CASE("action router flags duplicate route names", "[action-router]") {
    const auto router = test_router_t::from(
        std::vector{route("same", route_priority_t::domain, route_status_t::declined),
                    route("same", route_priority_t::fallback, route_status_t::declined)});
    CHECK_FALSE(router.has_unique_names());
}
