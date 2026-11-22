#include <wormedit/core/first_run.h>
#include <wormedit/core/preferences.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace wormedit;

TEST_CASE("first-run steps are offered in table order and remembered", "[core][first-run]") {
    first_run_preferences_t state;
    CHECK(next_first_run_step(state) == "updates");
    CHECK_FALSE(complete_first_run_step(state, "not-a-step"));
    CHECK(complete_first_run_step(state, "updates"));
    CHECK_FALSE(complete_first_run_step(state, "updates"));
    CHECK(next_first_run_step(state).empty());

    first_run_preferences_t browser;
    const std::array<std::string_view, 1> unavailable{"updates"};
    CHECK(next_first_run_step(browser, unavailable).empty());
}

TEST_CASE("first-run progress and the update opt-out survive a preferences round trip", "[core][first-run]") {
    auto preferences = program_preferences_t::with_defaults();
    CHECK(preferences.updates.check_on_startup);
    preferences.updates.check_on_startup = false;
    preferences.first_run.completed = {"updates", "step-from-a-newer-build"};
    const auto bytes = serialize_program_preferences(preferences);
    REQUIRE(bytes.has_value());
    const auto parsed = preferences_parse_effect_t::from(*bytes);
    REQUIRE(parsed.is_success());
    CHECK_FALSE(parsed.preferences.updates.check_on_startup);
    CHECK(parsed.preferences.first_run == preferences.first_run);
}

TEST_CASE("preferences without the new sections keep updates on and first run pending", "[core][first-run]") {
    const auto parsed = preferences_parse_effect_t::from(R"({"version":1})");
    REQUIRE(parsed.is_success());
    CHECK(parsed.preferences.updates.check_on_startup);
    CHECK(next_first_run_step(parsed.preferences.first_run) == "updates");
}
