#include <wormedit/core/startup_session.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>

namespace {

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{1};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-startup-session-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path);
    }

    ~temporary_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto song(std::uint64_t id, std::uint64_t revision) -> wormedit::song_document_t {
    return {.schema_version = 1,
            .id = wormedit::song_id_t{id},
            .revision = revision,
            .metadata = {.title = "project-" + std::to_string(id)}};
}

} // namespace

TEST_CASE("startup without paths keeps the caller's unsaved fallback and skips scanning",
          "[unit][core][startup-session]") {
    const auto result = wormedit::prepare_native_project_startup(song(1, 1));
    CHECK_FALSE(result.recovery_scan_configured);
    CHECK_FALSE(result.loaded_primary);
    CHECK_FALSE(result.primary_version.has_value());
    CHECK(result.recovery_scan.candidates.empty());
    CHECK(result.document.id == wormedit::song_id_t{1});
}

TEST_CASE("startup loads a validated primary and retains newer recovery choices",
          "[unit][core][startup-session]") {
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    REQUIRE(wormedit::publish_native_project_atomic(primary, song(7, 3)).published);
    REQUIRE(wormedit::publish_native_project_recovery_atomic(
                backup, song(7, 5),
                {.intended_project_path = primary, .last_explicit_save_revision = 3})
                .published);

    const auto result =
        wormedit::prepare_native_project_startup(song(1, 1), {.primary_path = primary});
    REQUIRE(result.loaded_primary);
    REQUIRE(result.primary_version.has_value());
    CHECK(result.document.id == wormedit::song_id_t{7});
    CHECK(result.document.revision == 3);
    CHECK(*result.primary_version ==
          wormedit::native_project_document_version_t{wormedit::song_id_t{7}, 3});
    REQUIRE(result.recovery_scan.preferred_recovery_candidate.has_value());
}

TEST_CASE("startup keeps the fallback when the primary is missing and exposes its backup",
          "[unit][core][startup-session]") {
    temporary_directory_t directory;
    const auto primary = directory.path / "missing.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    REQUIRE(wormedit::publish_native_project_recovery_atomic(backup, song(9, 4),
                                                             {.intended_project_path = primary})
                .published);

    const auto result =
        wormedit::prepare_native_project_startup(song(1, 1), {.primary_path = primary});
    CHECK(result.recovery_scan_configured);
    CHECK_FALSE(result.loaded_primary);
    CHECK_FALSE(result.primary_version.has_value());
    CHECK(result.document.id == wormedit::song_id_t{1});
    REQUIRE(result.recovery_scan.preferred_recovery_candidate.has_value());
}

TEST_CASE("startup discovers an untitled recovery from a caller-owned platform directory",
          "[unit][core][startup-session]") {
    temporary_directory_t directory;
    const auto recovery_directory = directory.path / "recovery";
    std::filesystem::create_directories(recovery_directory);
    const auto backup = recovery_directory / "project-9.wormedit.bak";
    REQUIRE(wormedit::publish_native_project_recovery_atomic(backup, song(9, 4), {}).published);

    const auto result = wormedit::prepare_native_project_startup(
        song(1, 1), {.recovery_directories = {recovery_directory}});
    CHECK(result.recovery_scan_configured);
    CHECK_FALSE(result.loaded_primary);
    CHECK_FALSE(result.primary_version.has_value());
    CHECK(result.document.id == wormedit::song_id_t{1});
    REQUIRE(result.recovery_scan.preferred_recovery_candidate.has_value());
    CHECK(
        result.recovery_scan.candidates[*result.recovery_scan.preferred_recovery_candidate].path ==
        backup);
}
