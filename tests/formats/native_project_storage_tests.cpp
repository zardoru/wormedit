#include <wormedit/formats/native_project_storage.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace std::chrono_literals;

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{1};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-native-project-storage-" +
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

auto valid_song(std::uint64_t revision) -> wormedit::song_document_t {
    return {.schema_version = 1, .id = wormedit::song_id_t{417}, .revision = revision};
}

auto fails_at(wormedit::native_project_storage_stage_t failed_stage)
    -> wormedit::native_project_storage_options_t {
    wormedit::native_project_storage_options_t options;
    options.allow_stage = [failed_stage](auto stage, const auto&) { return stage != failed_stage; };
    return options;
}

} // namespace

TEST_CASE("native project storage validates before atomically replacing a primary file",
          "[unit][format][native-project][storage]") {
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto old_song = valid_song(2);
    const auto initial = wormedit::publish_native_project_atomic(primary, old_song);
    REQUIRE(initial.published);
    const auto old_bytes = read_text(primary);

    const auto updated = wormedit::publish_native_project_atomic(primary, valid_song(3));
    REQUIRE(updated.published);
    const auto parsed = wormedit::parse_native_project(read_text(primary));
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->id == wormedit::song_id_t{417});
    CHECK(parsed.song->revision == 3);
    CHECK_FALSE(read_text(primary) == old_bytes);

    auto invalid_song = valid_song(4);
    invalid_song.id = {};
    const auto rejected = wormedit::publish_native_project_atomic(primary, invalid_song);
    CHECK_FALSE(rejected.published);
    CHECK(rejected.has_errors());
    CHECK(read_text(primary) == *wormedit::write_native_project(valid_song(3)).bytes);
}

TEST_CASE("temporary native-project files are unique and share the destination directory",
          "[unit][format][native-project][storage]") {
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    std::array<std::filesystem::path, 2> temporaries;
    for (std::size_t index = 0; index < temporaries.size(); ++index) {
        wormedit::native_project_storage_options_t options;
        options.allow_stage = [&temporaries, index](auto stage, const auto& path) {
            if (stage == wormedit::native_project_storage_stage_t::create_temporary) {
                temporaries[index] = path;
            }
            return true;
        };
        REQUIRE(wormedit::publish_native_project_atomic(primary, valid_song(index + 1),
                                                        std::move(options))
                    .published);
    }
    CHECK(temporaries[0].parent_path() == primary.parent_path());
    CHECK(temporaries[1].parent_path() == primary.parent_path());
    CHECK_FALSE(temporaries[0].empty());
    CHECK(temporaries[0] != temporaries[1]);
    CHECK_FALSE(std::filesystem::exists(temporaries[0]));
    CHECK_FALSE(std::filesystem::exists(temporaries[1]));
}

TEST_CASE("recovery snapshots carry a validated SHA-256 envelope and exact document identity",
          "[unit][format][native-project][recovery]") {
    temporary_directory_t directory;
    const auto backup = directory.path / "chart.wormedit.bak";
    const auto song = valid_song(11);
    const auto time = std::chrono::system_clock::time_point{123456789ms};
    const auto stored = wormedit::publish_native_project_recovery_atomic(
        backup, song,
        {.original_source_path = directory.path / "imported.bmson",
         .intended_project_path = directory.path / "chart.wormedit",
         .last_explicit_save_revision = 9,
         .utc_write_time = time,
         .session_sequence = 4});
    REQUIRE(stored.published);

    const auto bytes = read_text(backup);
    const auto parsed = wormedit::parse_native_project_recovery_snapshot(bytes);
    REQUIRE(parsed.snapshot.has_value());
    CHECK(parsed.snapshot->song.id == song.id);
    CHECK(parsed.snapshot->song.revision == song.revision);
    CHECK(parsed.snapshot->metadata.last_explicit_save_revision == 9);
    CHECK(parsed.snapshot->metadata.session_sequence == 4);
    CHECK(parsed.snapshot->metadata.original_source_path == directory.path / "imported.bmson");
    CHECK(parsed.snapshot->metadata.utc_write_time == time);

    auto corrupted = bytes;
    const auto payload_position = corrupted.find("\"payload\":");
    REQUIRE(payload_position != std::string::npos);
    constexpr std::string_view payload_revision = R"(\"revision\": 11)";
    const auto revision_position = corrupted.find(payload_revision, payload_position);
    REQUIRE(revision_position != std::string::npos);
    corrupted[revision_position + payload_revision.size() - 2] = '2';
    const auto bad_checksum = wormedit::parse_native_project_recovery_snapshot(corrupted);
    CHECK_FALSE(bad_checksum.snapshot.has_value());
    CHECK(bad_checksum.has_errors());
    CHECK(std::ranges::any_of(bad_checksum.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::checksum_mismatch;
    }));

    auto inconsistent_save_json = nlohmann::json::parse(bytes);
    inconsistent_save_json["last_explicit_save_revision"] = 12;
    const auto inconsistent_save =
        wormedit::parse_native_project_recovery_snapshot(inconsistent_save_json.dump());
    CHECK_FALSE(inconsistent_save.snapshot.has_value());
    CHECK(std::ranges::any_of(inconsistent_save.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::validation_failure;
    }));

    wormedit::native_project_limits_t narrow_limits;
    narrow_limits.maximum_source_bytes = 16;
    const auto oversized_payload =
        wormedit::parse_native_project_recovery_snapshot(bytes, narrow_limits);
    CHECK_FALSE(oversized_payload.snapshot.has_value());
    CHECK(std::ranges::any_of(oversized_payload.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::resource_limit;
    }));

    const auto nested_input = wormedit::parse_native_project_recovery_snapshot("[[[[0]]]]");
    CHECK_FALSE(nested_input.snapshot.has_value());
    CHECK(std::ranges::any_of(nested_input.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::resource_limit;
    }));
}

TEST_CASE("failed recovery publication preserves primary and last-known-good backup",
          "[unit][format][native-project][recovery][fault]") {
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    REQUIRE(wormedit::publish_native_project_atomic(primary, valid_song(6)).published);
    const auto original_primary = read_text(primary);
    REQUIRE(wormedit::publish_native_project_recovery_atomic(backup, valid_song(7),
                                                             {.session_sequence = 1})
                .published);
    const auto original_backup = read_text(backup);

    const auto inconsistent_saved_revision = wormedit::publish_native_project_recovery_atomic(
        backup, valid_song(8), {.last_explicit_save_revision = 9, .session_sequence = 2});
    CHECK_FALSE(inconsistent_saved_revision.published);
    CHECK(read_text(primary) == original_primary);
    CHECK(read_text(backup) == original_backup);

    constexpr std::array failure_stages{
        wormedit::native_project_storage_stage_t::serialize,
        wormedit::native_project_storage_stage_t::checksum,
        wormedit::native_project_storage_stage_t::create_temporary,
        wormedit::native_project_storage_stage_t::write_temporary,
        wormedit::native_project_storage_stage_t::flush_temporary,
        wormedit::native_project_storage_stage_t::close_temporary,
        wormedit::native_project_storage_stage_t::reopen_temporary,
        wormedit::native_project_storage_stage_t::validate_reopened,
        wormedit::native_project_storage_stage_t::replace_destination,
    };
    for (const auto stage : failure_stages) {
        const auto failed = wormedit::publish_native_project_recovery_atomic(
            backup, valid_song(8), {.session_sequence = 2}, fails_at(stage));
        CHECK_FALSE(failed.published);
        CHECK(failed.has_errors());
        CHECK(read_text(primary) == original_primary);
        CHECK(read_text(backup) == original_backup);
    }

    const auto preflight_failure = wormedit::publish_native_project_recovery_atomic(
        backup, valid_song(8), {.session_sequence = 2},
        fails_at(wormedit::native_project_storage_stage_t::preflight_validate));
    CHECK_FALSE(preflight_failure.published);
    CHECK(read_text(primary) == original_primary);
    CHECK(read_text(backup) == original_backup);

    const auto post_rename_sync_warning = wormedit::publish_native_project_recovery_atomic(
        backup, valid_song(9), {.session_sequence = 3},
        fails_at(wormedit::native_project_storage_stage_t::sync_destination_directory));
    CHECK(post_rename_sync_warning.published);
    CHECK_FALSE(post_rename_sync_warning.has_errors());
    CHECK(std::ranges::any_of(post_rename_sync_warning.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.message.find("destination was replaced") != std::string::npos;
    }));
    const auto replaced_backup =
        wormedit::parse_native_project_recovery_snapshot(read_text(backup));
    REQUIRE(replaced_backup.snapshot.has_value());
    CHECK(replaced_backup.snapshot->song.revision == 9);
}

TEST_CASE("autosave waits five minutes and writes only new dirty revisions",
          "[unit][format][native-project][autosave]") {
    using clock_t = wormedit::native_project_autosave_coordinator_t::clock_t;
    temporary_directory_t directory;
    const auto start = clock_t::time_point{};
    const auto recovery_path = directory.path / "caller-selected-recovery" / "session-417.bak";
    std::filesystem::create_directories(recovery_path.parent_path());
    wormedit::native_project_autosave_coordinator_t autosave(
        start, wormedit::native_project_document_version_t{wormedit::song_id_t{417}, 1});

    const auto clean_at_deadline =
        autosave.maybe_autosave(valid_song(1), start + 5min, recovery_path);
    CHECK(clean_at_deadline.status == wormedit::native_project_autosave_status_t::not_due);

    auto song = valid_song(2);
    const auto before_deadline = autosave.maybe_autosave(song, start + 5min - 1ms, recovery_path,
                                                         directory.path / "source.bms");
    CHECK(before_deadline.status == wormedit::native_project_autosave_status_t::not_due);
    const auto at_deadline =
        autosave.maybe_autosave(song, start + 5min, recovery_path, directory.path / "source.bms");
    REQUIRE(at_deadline.status == wormedit::native_project_autosave_status_t::published);
    CHECK(at_deadline.session_sequence == 1);
    CHECK(std::filesystem::exists(recovery_path));
    const auto parsed = wormedit::parse_native_project_recovery_snapshot(read_text(recovery_path));
    REQUIRE(parsed.snapshot.has_value());
    CHECK(parsed.snapshot->metadata.original_source_path == directory.path / "source.bms");

    const auto same_revision = autosave.maybe_autosave(song, start + 10min, recovery_path);
    CHECK(same_revision.status == wormedit::native_project_autosave_status_t::not_due);
    song.revision = 3;
    const auto next_revision = autosave.maybe_autosave(song, start + 10min, recovery_path);
    REQUIRE(next_revision.status == wormedit::native_project_autosave_status_t::published);
    CHECK(next_revision.session_sequence == 2);

    autosave.mark_explicit_save({.project_id = song.id, .revision = song.revision});
    const auto now_clean = autosave.maybe_autosave(song, start + 15min, recovery_path);
    CHECK(now_clean.status == wormedit::native_project_autosave_status_t::not_due);
}

TEST_CASE("autosave retries a failed revision without advancing its cadence or sequence",
          "[unit][format][native-project][autosave][fault]") {
    using clock_t = wormedit::native_project_autosave_coordinator_t::clock_t;
    temporary_directory_t directory;
    const auto start = clock_t::time_point{};
    const auto backup = directory.path / "recovery.bak";
    REQUIRE(wormedit::publish_native_project_recovery_atomic(backup, valid_song(4),
                                                             {.session_sequence = 3})
                .published);
    const auto original_backup = read_text(backup);
    wormedit::native_project_autosave_coordinator_t autosave(start, {}, 5min, 3);

    const auto failed = autosave.maybe_autosave(
        valid_song(5), start + 5min, backup, {}, {},
        fails_at(wormedit::native_project_storage_stage_t::replace_destination));
    CHECK(failed.status == wormedit::native_project_autosave_status_t::failed);
    CHECK(autosave.session_sequence() == 3);
    CHECK(read_text(backup) == original_backup);

    const auto retried = autosave.maybe_autosave(valid_song(5), start + 5min, backup);
    REQUIRE(retried.status == wormedit::native_project_autosave_status_t::published);
    CHECK(retried.session_sequence == 4);
}

TEST_CASE("autosave rejects nonpositive intervals explicitly",
          "[unit][format][native-project][autosave][negative]") {
    using clock_t = wormedit::native_project_autosave_coordinator_t::clock_t;
    const auto start = clock_t::time_point{};
    const auto backup = std::filesystem::path{"unused-recovery.bak"};
    const std::array intervals{clock_t::duration::zero(),
                               std::chrono::duration_cast<clock_t::duration>(-1ms)};
    for (const auto interval : intervals) {
        wormedit::native_project_autosave_coordinator_t autosave(start, {}, interval);
        const auto result = autosave.maybe_autosave(valid_song(1), start + 1h, backup);
        CHECK(result.status == wormedit::native_project_autosave_status_t::failed);
        CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code ==
                   wormedit::native_project_storage_diagnostic_code_t::invalid_interval;
        }));
    }
}
