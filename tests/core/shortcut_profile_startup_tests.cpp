#include <wormedit/core/shortcut_profile_startup.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{1};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-shortcut-startup-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path);
    }

    ~temporary_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto sample_shortcut(wormedit::shortcut_key_t key) -> wormedit::shortcut_chord_t {
    using namespace wormedit;
    return {.key = key,
            .modifiers = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control)};
}

auto sample_registry() -> wormedit::command_registry_t {
    using namespace wormedit;
    command_registry_t registry;
    registry.commands.push_back(
        {.definition = {.id = {"edit.sample"},
                        .default_shortcuts = {sample_shortcut(shortcut_key_t::a)}},
         .current_shortcuts = {sample_shortcut(shortcut_key_t::a)}});
    return registry;
}

} // namespace

TEST_CASE("shortcut startup leaves a missing profile silent and preserves defaults",
          "[unit][core][shortcut-profile-startup]") {
    using namespace wormedit;
    temporary_directory_t directory;
    auto registry = sample_registry();

    const auto result =
        load_shortcut_profile_for_startup(directory.path / "shortcuts.json", registry);

    CHECK(result.status == shortcut_profile_startup_status_t::missing);
    CHECK(result.diagnostics.empty());
    REQUIRE(registry.commands.size() == 1);
    CHECK(registry.commands.front().current_shortcuts ==
          std::vector<shortcut_chord_t>{sample_shortcut(shortcut_key_t::a)});
}

TEST_CASE("shortcut startup applies a valid user profile to the registry",
          "[unit][core][shortcut-profile-startup]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto path = directory.path / "shortcuts.json";
    const shortcut_profile_t profile{
        .id = "custom-layout",
        .bindings = {
            {.command = {"edit.sample"}, .shortcuts = {sample_shortcut(shortcut_key_t::b)}}}};
    REQUIRE(save_shortcut_profile_atomic(path, profile).status ==
            shortcut_profile_save_status_t::published);
    auto registry = sample_registry();

    const auto result = load_shortcut_profile_for_startup(path, registry);

    CHECK(result.status == shortcut_profile_startup_status_t::applied);
    CHECK(result.diagnostics.empty());
    REQUIRE(registry.commands.size() == 1);
    CHECK(registry.commands.front().current_shortcuts == profile.bindings.front().shortcuts);
}

TEST_CASE("rejected startup profiles do not partially change the command registry",
          "[unit][core][shortcut-profile-startup][negative]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto path = directory.path / "shortcuts.json";
    const shortcut_profile_t profile{
        .id = "custom-layout",
        .bindings = {
            {.command = {"edit.missing"}, .shortcuts = {sample_shortcut(shortcut_key_t::b)}}}};
    REQUIRE(save_shortcut_profile_atomic(path, profile).status ==
            shortcut_profile_save_status_t::published);
    auto registry = sample_registry();
    const auto before = registry.commands;

    const auto result = load_shortcut_profile_for_startup(path, registry);

    CHECK(result.status == shortcut_profile_startup_status_t::rejected);
    CHECK_FALSE(result.diagnostics.empty());
    REQUIRE(registry.commands.size() == before.size());
    CHECK(registry.commands.front().current_shortcuts == before.front().current_shortcuts);
}

TEST_CASE("startup diagnostics have bounded count and message size",
          "[unit][core][shortcut-profile-startup][bounds]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto path = directory.path / "shortcuts.json";
    REQUIRE(save_shortcut_profile_atomic(path, {.id = "empty-profile"}).status ==
            shortcut_profile_save_status_t::published);

    command_registry_t registry;
    const auto shared = sample_shortcut(shortcut_key_t::a);
    for (std::size_t index = 0; index < 6; ++index) {
        auto id = "edit.sample" + std::to_string(index);
        registry.commands.push_back(
            {.definition = {.id = {std::move(id)}, .default_shortcuts = {shared}},
             .current_shortcuts = {shared}});
    }

    const auto result = load_shortcut_profile_for_startup(path, registry);

    CHECK(result.status == shortcut_profile_startup_status_t::applied);
    REQUIRE(result.diagnostics.size() == maximum_shortcut_profile_startup_diagnostics);
    CHECK(result.diagnostics.back().message ==
          "Additional shortcut profile diagnostics were omitted.");
    for (const auto& diagnostic : result.diagnostics) {
        CHECK(diagnostic.message.size() <= maximum_shortcut_profile_startup_diagnostic_bytes);
    }
}
