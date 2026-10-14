#include <wormedit/core/shortcut_profile_storage.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{1};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-shortcut-profile-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path);
    }

    ~temporary_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto valid_profile() -> wormedit::shortcut_profile_t {
    using namespace wormedit;
    return {.id = "custom-layout",
            .bindings = {{.command = {"edit.sample"},
                          .shortcuts = {{.key = shortcut_key_t::a,
                                         .modifiers = static_cast<shortcut_modifiers_t>(
                                             shortcut_modifier_t::control)}}}}};
}

void check_rejected_profile_preserves_file(const std::filesystem::path& path,
                                          const wormedit::shortcut_profile_t& profile) {
    const auto before = read_text(path);
    auto invalid = profile;
    invalid.id = "not a valid ID";
    const auto rejected = wormedit::create_shortcut_profile_file(path, invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == wormedit::shortcut_profile_save_status_t::invalid_profile);
    CHECK(read_text(path) == before);
}

} // namespace

TEST_CASE("shortcut profile storage round trips user preferences atomically",
          "[unit][core][shortcut-profile-storage]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto path = directory.path / "shortcuts.json";

    const auto missing = open_shortcut_profile(path);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().value.code == shortcut_profile_load_status_t::missing);
    const auto profile = valid_profile();
    const auto saved = create_shortcut_profile_file(path, profile);
    REQUIRE(saved);
    CHECK(open_shortcut_profile(path) == profile);

    check_rejected_profile_preserves_file(path, profile);
}

TEST_CASE("shortcut profile storage distinguishes malformed, oversized, and unreadable files",
          "[unit][core][shortcut-profile-storage][bounds]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto path = directory.path / "shortcuts.json";

    {
        std::ofstream stream(path, std::ios::binary);
        stream << "{bad json";
    }
    const auto malformed = open_shortcut_profile(path);
    REQUIRE_FALSE(malformed);
    CHECK(malformed.error().value.code == shortcut_profile_load_status_t::invalid);

    std::filesystem::resize_file(path, maximum_shortcut_profile_bytes + 1);
    const auto oversized = open_shortcut_profile(path);
    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().value.code == shortcut_profile_load_status_t::resource_limit);

    const auto directory_load = open_shortcut_profile(directory.path);
    REQUIRE_FALSE(directory_load);
    CHECK(directory_load.error().value.code == shortcut_profile_load_status_t::io_error);
}
