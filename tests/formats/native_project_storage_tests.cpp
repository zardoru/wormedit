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

void write_text(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

auto valid_song(std::uint64_t revision) -> wormedit::song_document_t {
    return {.id = wormedit::song_id_t{417}, .revision = revision};
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
    const auto initial = wormedit::create_native_project_file(primary, old_song);
    REQUIRE(initial.value);
    const auto old_bytes = read_text(primary);

    const auto updated = wormedit::create_native_project_file(primary, valid_song(3));
    REQUIRE(updated.value);
    const auto parsed = wormedit::parse_native_project(read_text(primary));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->id == wormedit::song_id_t{417});
    CHECK(parsed.value->revision == 3);
    CHECK_FALSE(read_text(primary) == old_bytes);

    auto invalid_song = valid_song(4);
    invalid_song.id = {};
    const auto rejected = wormedit::create_native_project_file(primary, invalid_song);
    CHECK_FALSE(rejected.value);
    CHECK(!rejected.ok());
    CHECK(read_text(primary) == *wormedit::write_native_project(valid_song(3)).value);
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
        REQUIRE(wormedit::create_native_project_file(primary, valid_song(index + 1),
                                                        std::move(options))
                    .value);
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
    const auto stored = wormedit::create_native_project_recovery_file(
        backup, song,
        {.original_source_path = directory.path / "imported.bmson",
         .intended_project_path = directory.path / "chart.wormedit",
         .last_explicit_save_revision = 9,
         .utc_write_time = time,
         .session_sequence = 4});
    REQUIRE(stored.value);

    const auto bytes = read_text(backup);
    const auto parsed = wormedit::parse_native_project_recovery_snapshot(bytes);
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->song.id == song.id);
    CHECK(parsed.value->song.revision == song.revision);
    CHECK(parsed.value->metadata.last_explicit_save_revision == 9);
    CHECK(parsed.value->metadata.session_sequence == 4);
    CHECK(parsed.value->metadata.original_source_path == directory.path / "imported.bmson");
    CHECK(parsed.value->metadata.utc_write_time == time);

    auto corrupted = bytes;
    const auto payload_position = corrupted.find("\"payload\":");
    REQUIRE(payload_position != std::string::npos);
    constexpr std::string_view payload_revision = R"(\"revision\": 11)";
    const auto revision_position = corrupted.find(payload_revision, payload_position);
    REQUIRE(revision_position != std::string::npos);
    corrupted[revision_position + payload_revision.size() - 2] = '2';
    const auto bad_checksum = wormedit::parse_native_project_recovery_snapshot(corrupted);
    CHECK_FALSE(bad_checksum.value.has_value());
    CHECK(!bad_checksum.ok());
    CHECK(std::ranges::any_of(bad_checksum.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::checksum_mismatch;
    }));

    auto inconsistent_save_json = nlohmann::json::parse(bytes);
    inconsistent_save_json["last_explicit_save_revision"] = 12;
    const auto inconsistent_save =
        wormedit::parse_native_project_recovery_snapshot(inconsistent_save_json.dump());
    CHECK_FALSE(inconsistent_save.value.has_value());
    CHECK(std::ranges::any_of(inconsistent_save.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::validation_failure;
    }));

    wormedit::native_project_limits_t narrow_limits;
    narrow_limits.maximum_source_bytes = 16;
    const auto oversized_payload =
        wormedit::parse_native_project_recovery_snapshot(bytes, narrow_limits);
    CHECK_FALSE(oversized_payload.value.has_value());
    CHECK(std::ranges::any_of(oversized_payload.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::resource_limit;
    }));

    const auto nested_input = wormedit::parse_native_project_recovery_snapshot("[[[[0]]]]");
    CHECK_FALSE(nested_input.value.has_value());
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
    REQUIRE(wormedit::create_native_project_file(primary, valid_song(6)).value);
    const auto original_primary = read_text(primary);
    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(7),
                                                             {.session_sequence = 1})
                .value);
    const auto original_backup = read_text(backup);

    const auto inconsistent_saved_revision = wormedit::create_native_project_recovery_file(
        backup, valid_song(8), {.last_explicit_save_revision = 9, .session_sequence = 2});
    CHECK_FALSE(inconsistent_saved_revision.value);
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
        const auto failed = wormedit::create_native_project_recovery_file(
            backup, valid_song(8), {.session_sequence = 2}, fails_at(stage));
        CHECK_FALSE(failed.value);
        CHECK(!failed.ok());
        CHECK(read_text(primary) == original_primary);
        CHECK(read_text(backup) == original_backup);
    }

    const auto preflight_failure = wormedit::create_native_project_recovery_file(
        backup, valid_song(8), {.session_sequence = 2},
        fails_at(wormedit::native_project_storage_stage_t::preflight_validate));
    CHECK_FALSE(preflight_failure.value);
    CHECK(read_text(primary) == original_primary);
    CHECK(read_text(backup) == original_backup);

    const auto post_rename_sync_warning = wormedit::create_native_project_recovery_file(
        backup, valid_song(9), {.session_sequence = 3},
        fails_at(wormedit::native_project_storage_stage_t::sync_destination_directory));
    CHECK(post_rename_sync_warning.value);
    CHECK(post_rename_sync_warning.ok());
    CHECK(std::ranges::any_of(post_rename_sync_warning.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.message.find("destination was replaced") != std::string::npos;
    }));
    const auto replaced_backup =
        wormedit::parse_native_project_recovery_snapshot(read_text(backup));
    REQUIRE(replaced_backup.value.has_value());
    CHECK(replaced_backup.value->song.revision == 9);
}

TEST_CASE(
    "startup recovery scan validates artifacts and compares identity and revision before time",
    "[unit][format][native-project][recovery][scan]") {
    using candidate_kind_t = wormedit::native_project_recovery_candidate_kind_t;
    using candidate_status_t = wormedit::native_project_recovery_candidate_status_t;
    using comparison_t = wormedit::native_project_recovery_comparison_t;
    using action_t = wormedit::native_project_recovery_action_t;
    using quarantine_t = wormedit::native_project_recovery_quarantine_t;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    const auto missing_entry = directory.path / "untitled-session.bak";
    const auto write_recovery = [](const auto& path, const auto& song, auto time,
                                   std::uint64_t sequence = 1) {
        return wormedit::create_native_project_recovery_file(
            path, song, {.utc_write_time = time, .session_sequence = sequence});
    };
    const auto find_candidate = [](const auto& scan, const auto& path) -> const auto& {
        return *std::ranges::find_if(scan.value.candidates,
                                     [&](const auto& candidate) { return candidate.path == path; });
    };

    REQUIRE(wormedit::create_native_project_file(primary, valid_song(5)).value);
    REQUIRE(write_recovery(backup, valid_song(4), std::chrono::system_clock::time_point{999999h})
                .value);
    auto scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary,
         .caller_supplied_entries = {missing_entry},
         .discover_stale_temporaries = false});
    const auto& primary_candidate = find_candidate(scan, primary);
    const auto& missing_backup = find_candidate(scan, missing_entry);
    CHECK(primary_candidate.kind == candidate_kind_t::primary);
    CHECK(primary_candidate.status == candidate_status_t::valid);
    CHECK(missing_backup.status == candidate_status_t::missing);
    CHECK(find_candidate(scan, backup).comparison == comparison_t::older_revision);
    CHECK(find_candidate(scan, backup).recommended_action == action_t::discard);

    REQUIRE(write_recovery(missing_entry, valid_song(3), std::chrono::system_clock::time_point{2ms})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary,
         .caller_supplied_entries = {missing_entry},
         .discover_stale_temporaries = false});
    CHECK(find_candidate(scan, missing_entry).kind == candidate_kind_t::caller_supplied_entry);
    CHECK(find_candidate(scan, missing_entry).status == candidate_status_t::valid);
    CHECK(find_candidate(scan, missing_entry).available_actions.size() == 4);

    REQUIRE(write_recovery(backup, valid_song(5), std::chrono::system_clock::time_point{999999h})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    CHECK(find_candidate(scan, backup).comparison == comparison_t::equal_revision);
    CHECK(find_candidate(scan, backup).recommended_action == action_t::discard);

    REQUIRE(write_recovery(backup, valid_song(6), std::chrono::system_clock::time_point{1ms})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    const auto& newer_backup = find_candidate(scan, backup);
    CHECK(newer_backup.comparison == comparison_t::newer_revision);
    CHECK(newer_backup.recommended_action == action_t::inspect_read_only);
    REQUIRE(scan.value.preferred_recovery_candidate.has_value());
    CHECK(scan.value.candidates[*scan.value.preferred_recovery_candidate].path == backup);
    CHECK(newer_backup.available_actions.size() == 4);

    auto unrelated_song = valid_song(99);
    unrelated_song.id = wormedit::song_id_t{9001};
    REQUIRE(write_recovery(backup, unrelated_song, std::chrono::system_clock::time_point{999999h})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    CHECK(find_candidate(scan, backup).comparison == comparison_t::different_project);
    CHECK_FALSE(scan.value.preferred_recovery_candidate.has_value());

    write_text(backup, "{ corrupt recovery data");
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    CHECK(find_candidate(scan, backup).status == candidate_status_t::invalid);
    CHECK(find_candidate(scan, backup).quarantine == quarantine_t::recommend_quarantine_corrupt);

    REQUIRE(write_recovery(backup, valid_song(8), std::chrono::system_clock::time_point{2ms})
                .value);
    std::filesystem::remove(primary);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    CHECK(find_candidate(scan, primary).status == candidate_status_t::missing);
    CHECK(find_candidate(scan, backup).comparison == comparison_t::no_valid_primary);
    CHECK(find_candidate(scan, backup).recommended_action == action_t::recover_into_session);

    write_text(primary, "not a native project");
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    CHECK(find_candidate(scan, primary).status == candidate_status_t::invalid);
    CHECK(find_candidate(scan, backup).recommended_action == action_t::recover_into_session);
}

TEST_CASE("startup scan validates stale temporaries and never lets their timestamp beat a "
          "completed backup",
          "[unit][format][native-project][recovery][scan]") {
    using candidate_kind_t = wormedit::native_project_recovery_candidate_kind_t;
    using candidate_status_t = wormedit::native_project_recovery_candidate_status_t;
    using quarantine_t = wormedit::native_project_recovery_quarantine_t;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    auto primary_temporary = primary;
    primary_temporary += ".wormedit-tmp-interrupted-primary";
    auto temporary = backup;
    temporary += ".wormedit-tmp-leftover";
    REQUIRE(wormedit::create_native_project_file(primary, valid_song(10)).value);
    REQUIRE(wormedit::create_native_project_file(primary_temporary, valid_song(10)).value);
    REQUIRE(
        wormedit::create_native_project_recovery_file(
            backup, valid_song(11),
            {.utc_write_time = std::chrono::system_clock::time_point{1ms}, .session_sequence = 1})
            .value);
    REQUIRE(wormedit::create_native_project_recovery_file(
                temporary, valid_song(11),
                {.utc_write_time = std::chrono::system_clock::time_point{999999h},
                 .session_sequence = 100})
                .value);

    const auto scan = wormedit::scan_native_project_recovery_candidates({.primary_path = primary});
    const auto completed = std::ranges::find_if(
        scan.value.candidates, [&](const auto& candidate) { return candidate.path == backup; });
    const auto stale = std::ranges::find_if(
        scan.value.candidates, [&](const auto& candidate) { return candidate.path == temporary; });
    const auto stale_primary = std::ranges::find_if(scan.value.candidates, [&](const auto& candidate) {
        return candidate.path == primary_temporary;
    });
    REQUIRE(completed != scan.value.candidates.end());
    REQUIRE(stale != scan.value.candidates.end());
    REQUIRE(stale_primary != scan.value.candidates.end());
    CHECK(stale->kind == candidate_kind_t::stale_recovery_temporary);
    CHECK(stale->status == candidate_status_t::valid);
    CHECK(stale->quarantine == quarantine_t::recommend_quarantine_superseded_temporary);
    CHECK(stale_primary->kind == candidate_kind_t::stale_primary_temporary);
    CHECK(stale_primary->status == candidate_status_t::valid);
    CHECK(stale_primary->quarantine == quarantine_t::recommend_quarantine_superseded_temporary);
    REQUIRE(scan.value.preferred_recovery_candidate.has_value());
    CHECK(scan.value.candidates[*scan.value.preferred_recovery_candidate].path == backup);
    CHECK(std::filesystem::exists(temporary));

    REQUIRE(
        wormedit::create_native_project_recovery_file(
            temporary, valid_song(12),
            {.utc_write_time = std::chrono::system_clock::time_point{1ms}, .session_sequence = 2})
            .value);
    const auto newer_temporary_scan =
        wormedit::scan_native_project_recovery_candidates({.primary_path = primary});
    REQUIRE(newer_temporary_scan.value.preferred_recovery_candidate.has_value());
    CHECK(
        newer_temporary_scan.value.candidates[*newer_temporary_scan.value.preferred_recovery_candidate].path ==
        temporary);

    REQUIRE(wormedit::create_native_project_file(primary_temporary, valid_song(13)).value);
    const auto newer_primary_temporary_scan =
        wormedit::scan_native_project_recovery_candidates({.primary_path = primary});
    REQUIRE(newer_primary_temporary_scan.value.preferred_recovery_candidate.has_value());
    CHECK(newer_primary_temporary_scan
              .value.candidates[*newer_primary_temporary_scan.value.preferred_recovery_candidate]
              .path == primary_temporary);
}

TEST_CASE("stale temporary quarantine uses matching project identity and completed state",
          "[unit][format][native-project][recovery][scan]") {
    using candidate_kind_t = wormedit::native_project_recovery_candidate_kind_t;
    using quarantine_t = wormedit::native_project_recovery_quarantine_t;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto unrelated_entry = directory.path / "untitled.bak";
    auto stale_primary = primary;
    stale_primary += ".wormedit-tmp-interrupted";
    auto stale_recovery = unrelated_entry;
    stale_recovery += ".wormedit-tmp-interrupted";

    REQUIRE(wormedit::create_native_project_file(primary, valid_song(10)).value);
    REQUIRE(wormedit::create_native_project_file(stale_primary, valid_song(10)).value);
    auto unrelated_song = valid_song(99);
    unrelated_song.id = wormedit::song_id_t{9001};
    REQUIRE(wormedit::create_native_project_recovery_file(
                unrelated_entry, unrelated_song,
                {.utc_write_time = std::chrono::system_clock::time_point{999999h}})
                .value);
    REQUIRE(wormedit::create_native_project_recovery_file(
                stale_recovery, valid_song(11),
                {.utc_write_time = std::chrono::system_clock::time_point{1ms}})
                .value);

    auto scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .caller_supplied_entries = {unrelated_entry}});
    const auto find_candidate = [&](const auto& path) -> const auto& {
        return *std::ranges::find_if(scan.value.candidates,
                                     [&](const auto& candidate) { return candidate.path == path; });
    };
    CHECK(find_candidate(stale_primary).kind == candidate_kind_t::stale_primary_temporary);
    CHECK(find_candidate(stale_primary).quarantine ==
          quarantine_t::recommend_quarantine_superseded_temporary);
    CHECK(find_candidate(stale_recovery).kind == candidate_kind_t::stale_recovery_temporary);
    CHECK(find_candidate(stale_recovery).quarantine == quarantine_t::retain);

    REQUIRE(wormedit::create_native_project_recovery_file(
                unrelated_entry, valid_song(11),
                {.utc_write_time = std::chrono::system_clock::time_point{2ms}})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .caller_supplied_entries = {unrelated_entry}});
    CHECK(find_candidate(stale_recovery).quarantine ==
          quarantine_t::recommend_quarantine_superseded_temporary);
}

TEST_CASE("recovery actions revalidate scan identity and save-as never replaces a destination",
          "[unit][format][native-project][recovery][actions]") {
    using action_t = wormedit::native_project_recovery_action_t;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    const auto save_as = directory.path / "recovered-copy.wormedit";
    REQUIRE(wormedit::create_native_project_file(primary, valid_song(5)).value);
    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(6),
                                                             {.session_sequence = 2})
                .value);
    const auto primary_before = read_text(primary);
    const auto scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    const auto candidate = std::ranges::find_if(
        scan.value.candidates, [&](const auto& item) { return item.path == backup; });
    REQUIRE(candidate != scan.value.candidates.end());
    const auto candidate_index = static_cast<std::size_t>(candidate - scan.value.candidates.begin());
    CHECK(candidate->scanned_size_bytes.has_value());
    CHECK(candidate->scanned_sha256.size() == 64);

    const auto inspected = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::inspect_read_only);
    REQUIRE(inspected.value.applied);
    REQUIRE(inspected.value.document.has_value());
    CHECK((inspected.value.version == wormedit::native_project_document_version_t{
                                    .project_id = valid_song(1).id, .revision = 6}));

    const auto unguarded_recovery = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::recover_into_session);
    CHECK_FALSE(unguarded_recovery.value.applied);

    wormedit::native_project_recovery_action_options_t recovery_options;
    recovery_options.expected_session.is_set = true;
    recovery_options.expected_session.version =
        wormedit::native_project_document_version_t{.project_id = valid_song(1).id, .revision = 5};
    const auto recovered = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::recover_into_session, recovery_options);
    REQUIRE(recovered.value.applied);
    REQUIRE(recovered.value.document.has_value());
    CHECK(recovered.value.document->revision == 6);
    CHECK(read_text(primary) == primary_before);

    recovery_options.expected_session.version->revision = 4;
    const auto stale_session = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::recover_into_session, recovery_options);
    CHECK_FALSE(stale_session.value.applied);
    CHECK(!stale_session.ok());

    wormedit::native_project_recovery_action_options_t limited_options;
    limited_options.maximum_candidate_file_bytes = 1;
    const auto limited_read = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::inspect_read_only, limited_options);
    CHECK_FALSE(limited_read.value.applied);
    CHECK(std::ranges::any_of(limited_read.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::resource_limit;
    }));

    wormedit::native_project_recovery_action_options_t save_options;
    save_options.save_as_destination = save_as;
    const auto saved_as = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::save_as_new_project, save_options);
    REQUIRE(saved_as.value.applied);
    const auto reopened = wormedit::parse_native_project(read_text(save_as));
    REQUIRE(reopened.value.has_value());
    CHECK(reopened.value->revision == 6);
    CHECK(read_text(primary) == primary_before);

    const auto existing_destination = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::save_as_new_project, save_options);
    CHECK_FALSE(existing_destination.value.applied);
    CHECK(std::ranges::any_of(existing_destination.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::recovery_target_exists;
    }));

    save_options.save_as_destination = primary;
    const auto primary_target = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::save_as_new_project, save_options);
    CHECK_FALSE(primary_target.value.applied);
    CHECK(read_text(primary) == primary_before);

    const auto raced_destination = directory.path / "appeared-during-save-as.wormedit";
    save_options.save_as_destination = raced_destination;
    save_options.allow_stage = [&](auto stage, const auto&) {
        if (stage == wormedit::native_project_recovery_action_stage_t::before_save_as_publish) {
            write_text(raced_destination, "preexisting concurrent file");
        }
        return true;
    };
    const auto save_as_race = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::save_as_new_project, save_options);
    CHECK_FALSE(save_as_race.value.applied);
    CHECK(read_text(raced_destination) == "preexisting concurrent file");
    CHECK(read_text(primary) == primary_before);
}

TEST_CASE("recovery into an empty session validates a recovery with no primary file",
          "[unit][format][native-project][recovery][actions]") {
    temporary_directory_t directory;
    const auto primary = directory.path / "missing-primary.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(2),
                                                             {.session_sequence = 1})
                .value);
    const auto scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    const auto candidate = std::ranges::find_if(
        scan.value.candidates, [&](const auto& item) { return item.path == backup; });
    REQUIRE(candidate != scan.value.candidates.end());
    const auto candidate_index = static_cast<std::size_t>(candidate - scan.value.candidates.begin());
    wormedit::native_project_recovery_action_options_t options;
    options.expected_session.is_set = true;
    const auto recovered = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, wormedit::native_project_recovery_action_t::recover_into_session,
        options);
    REQUIRE(recovered.value.applied);
    REQUIRE(recovered.value.document.has_value());
    CHECK(recovered.value.document->revision == 2);
    CHECK_FALSE(std::filesystem::exists(primary));
}

TEST_CASE("platform recovery directories are scanned deterministically for an empty session",
          "[unit][format][native-project][recovery][scan][directories]") {
    using candidate_kind_t = wormedit::native_project_recovery_candidate_kind_t;
    temporary_directory_t directory;
    const auto recovery_directory = directory.path / "recovery";
    const auto nested_directory = recovery_directory / "nested";
    std::filesystem::create_directories(nested_directory);
    const auto first = recovery_directory / "a-project.bak";
    const auto second = recovery_directory / "z-project.bak";
    const auto interrupted = recovery_directory / "middle-project.bak.wormedit-tmp-interrupted";
    const auto quarantined =
        recovery_directory / "newest-project.bak.wormedit-quarantine-123-456-1";
    const auto nested = nested_directory / "ignored.bak";
    REQUIRE(wormedit::create_native_project_recovery_file(first, valid_song(2), {}).value);
    REQUIRE(wormedit::create_native_project_recovery_file(second, valid_song(4), {}).value);
    REQUIRE(
        wormedit::create_native_project_recovery_file(interrupted, valid_song(3), {}).value);
    REQUIRE(wormedit::create_native_project_recovery_file(quarantined, valid_song(100), {})
                .value);
    REQUIRE(wormedit::create_native_project_recovery_file(nested, valid_song(99), {}).value);

    const auto scan = wormedit::scan_native_project_recovery_candidates(
        {.caller_supplied_directories = {recovery_directory}, .discover_stale_temporaries = false});
    REQUIRE(scan.value.candidates.size() == 3);
    CHECK(scan.value.candidates[0].path == first);
    CHECK(scan.value.candidates[1].path == interrupted);
    CHECK(scan.value.candidates[2].path == second);
    CHECK(scan.value.candidates[0].kind == candidate_kind_t::caller_supplied_entry);
    CHECK(scan.value.candidates[1].kind == candidate_kind_t::stale_recovery_temporary);
    CHECK(scan.value.candidates[2].kind == candidate_kind_t::caller_supplied_entry);
    CHECK(std::ranges::none_of(scan.value.candidates,
                               [&](const auto& candidate) { return candidate.path == nested; }));
    CHECK(std::ranges::none_of(
        scan.value.candidates, [&](const auto& candidate) { return candidate.path == quarantined; }));
    REQUIRE(scan.value.preferred_recovery_candidate.has_value());
    CHECK(scan.value.candidates[*scan.value.preferred_recovery_candidate].path == second);

    wormedit::native_project_recovery_action_options_t options;
    options.expected_session.is_set = true;
    const auto recovered = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, *scan.value.preferred_recovery_candidate,
        wormedit::native_project_recovery_action_t::recover_into_session, options);
    REQUIRE(recovered.value.applied);
    REQUIRE(recovered.value.document.has_value());
    CHECK(recovered.value.document->revision == 4);

    options.expected_session.version =
        wormedit::native_project_document_version_t{.project_id = valid_song(1).id, .revision = 1};
    const auto nonempty_session = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, *scan.value.preferred_recovery_candidate,
        wormedit::native_project_recovery_action_t::recover_into_session, options);
    CHECK_FALSE(nonempty_session.value.applied);
    CHECK(!nonempty_session.ok());

    const auto limited_scan = wormedit::scan_native_project_recovery_candidates(
        {.caller_supplied_directories = {recovery_directory}, .discover_stale_temporaries = false},
        {.maximum_candidates = 2});
    CHECK(limited_scan.value.candidates.size() == 2);
    CHECK(std::ranges::any_of(limited_scan.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::resource_limit;
    }));

    const auto oversized_directory = directory.path / "oversized-recovery";
    std::filesystem::create_directories(oversized_directory);
    for (std::size_t index = 0; index < 65; ++index) {
        write_text(oversized_directory / ("candidate-" + std::to_string(index)), "invalid");
    }
    const auto oversized_scan = wormedit::scan_native_project_recovery_candidates(
        {.caller_supplied_directories = {oversized_directory}, .discover_stale_temporaries = false},
        {.maximum_candidates = 1});
    CHECK(oversized_scan.value.candidates.empty());
    CHECK(std::ranges::any_of(oversized_scan.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::native_project_storage_diagnostic_code_t::resource_limit;
    }));
}

TEST_CASE("recovery actions reject changed artifacts and quarantine by recoverable move",
          "[unit][format][native-project][recovery][actions][faults]") {
    using action_t = wormedit::native_project_recovery_action_t;
    using diagnostic_code_t = wormedit::native_project_storage_diagnostic_code_t;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = wormedit::native_project_adjacent_backup_path(primary);
    REQUIRE(wormedit::create_native_project_file(primary, valid_song(4)).value);
    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(3),
                                                             {.session_sequence = 1})
                .value);
    auto scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    auto candidate = std::ranges::find_if(scan.value.candidates,
                                          [&](const auto& item) { return item.path == backup; });
    REQUIRE(candidate != scan.value.candidates.end());
    const auto candidate_index = static_cast<std::size_t>(candidate - scan.value.candidates.begin());

    wormedit::native_project_recovery_action_options_t raced_options;
    raced_options.allow_stage = [&](auto stage, const auto&) {
        if (stage == wormedit::native_project_recovery_action_stage_t::before_mutation) {
            write_text(backup, "replacement after scan");
        }
        return true;
    };
    const auto changed = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, candidate_index, action_t::discard, raced_options);
    CHECK_FALSE(changed.value.applied);
    CHECK(read_text(backup) == "replacement after scan");
    CHECK(std::ranges::any_of(changed.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::recovery_candidate_changed;
    }));

    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(3),
                                                             {.session_sequence = 2})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    candidate = std::ranges::find_if(scan.value.candidates,
                                     [&](const auto& item) { return item.path == backup; });
    REQUIRE(candidate != scan.value.candidates.end());
    const auto primary_race_index = static_cast<std::size_t>(candidate - scan.value.candidates.begin());
    raced_options.allow_stage = [&](auto stage, const auto&) {
        if (stage == wormedit::native_project_recovery_action_stage_t::before_mutation) {
            REQUIRE(wormedit::create_native_project_file(primary, valid_song(5)).value);
        }
        return true;
    };
    const auto changed_primary = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, primary_race_index, action_t::discard, raced_options);
    CHECK_FALSE(changed_primary.value.applied);
    CHECK(std::filesystem::exists(backup));
    CHECK(std::ranges::any_of(changed_primary.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::recovery_candidate_changed;
    }));

    REQUIRE(wormedit::create_native_project_file(primary, valid_song(4)).value);
    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(3),
                                                             {.session_sequence = 2})
                .value);
    write_text(backup, "corrupt recovery payload");
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    candidate = std::ranges::find_if(scan.value.candidates,
                                     [&](const auto& item) { return item.path == backup; });
    REQUIRE(candidate != scan.value.candidates.end());
    CHECK(candidate->quarantine ==
          wormedit::native_project_recovery_quarantine_t::recommend_quarantine_corrupt);
    CHECK(std::ranges::find(candidate->available_actions, action_t::quarantine) !=
          candidate->available_actions.end());
    const auto corrupt_index = static_cast<std::size_t>(candidate - scan.value.candidates.begin());
    const auto quarantined_bytes = read_text(backup);
    const auto quarantined =
        wormedit::perform_native_project_recovery_action(scan.value.candidates, corrupt_index, action_t::quarantine);
    REQUIRE(quarantined.value.applied);
    REQUIRE(quarantined.value.quarantined_path.has_value());
    CHECK_FALSE(std::filesystem::exists(backup));
    CHECK(read_text(*quarantined.value.quarantined_path) == quarantined_bytes);

    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(3),
                                                             {.session_sequence = 3})
                .value);
    scan = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
    candidate = std::ranges::find_if(scan.value.candidates,
                                     [&](const auto& item) { return item.path == backup; });
    REQUIRE(candidate != scan.value.candidates.end());
    const auto valid_index = static_cast<std::size_t>(candidate - scan.value.candidates.begin());
    wormedit::native_project_recovery_action_options_t after_move_options;
    after_move_options.allow_stage = [&](auto stage, const auto& path) {
        if (stage == wormedit::native_project_recovery_action_stage_t::after_quarantine_move) {
            write_text(path, "changed during move");
        }
        return true;
    };
    const auto failed_move = wormedit::perform_native_project_recovery_action(
        scan.value.candidates, valid_index, action_t::discard, after_move_options);
    CHECK_FALSE(failed_move.value.applied);
    CHECK(std::filesystem::exists(backup));
    CHECK(read_text(backup) == "changed during move");
    CHECK_FALSE(failed_move.value.quarantined_path.has_value());
}

TEST_CASE("startup recovery scan enforces candidate, file-byte, and diagnostic caps",
          "[unit][format][native-project][recovery][scan][limits]") {
    using candidate_status_t = wormedit::native_project_recovery_candidate_status_t;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto first = directory.path / "first.bak";
    const auto second = directory.path / "second.bak";
    REQUIRE(wormedit::create_native_project_file(primary, valid_song(1)).value);
    write_text(first, "bad one");
    write_text(second, "bad two");

    wormedit::native_project_recovery_scan_options_t candidate_options;
    candidate_options.maximum_candidates = 1;
    candidate_options.maximum_diagnostics = 1;
    const auto candidate_limited = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false}, candidate_options);
    CHECK(candidate_limited.value.candidates.size() == 1);
    CHECK(candidate_limited.diagnostics.size() == 1);

    wormedit::native_project_recovery_scan_options_t byte_options;
    byte_options.maximum_candidate_file_bytes = 1;
    byte_options.maximum_total_bytes = 1;
    const auto byte_limited = wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false}, byte_options);
    CHECK(byte_limited.value.candidates.front().status == candidate_status_t::resource_limited);
    CHECK(byte_limited.diagnostics.size() == 1);

    wormedit::native_project_recovery_scan_options_t diagnostic_options;
    diagnostic_options.maximum_candidates = 4;
    diagnostic_options.maximum_diagnostics = 1;
    const auto diagnostics_limited = wormedit::scan_native_project_recovery_candidates(
        {.caller_supplied_entries = {first, second}, .discover_stale_temporaries = false},
        diagnostic_options);
    CHECK(diagnostics_limited.value.candidates.size() == 2);
    CHECK(diagnostics_limited.diagnostics.size() == 1);
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
        autosave.create_autosave(valid_song(1), start + 5min, recovery_path);
    CHECK(clean_at_deadline.value.status == wormedit::native_project_autosave_status_t::not_due);

    auto song = valid_song(2);
    const auto before_deadline = autosave.create_autosave(song, start + 5min - 1ms, recovery_path,
                                                         directory.path / "source.bms");
    CHECK(before_deadline.value.status == wormedit::native_project_autosave_status_t::not_due);
    const auto at_deadline =
        autosave.create_autosave(song, start + 5min, recovery_path, directory.path / "source.bms");
    REQUIRE(at_deadline.value.status == wormedit::native_project_autosave_status_t::published);
    CHECK(at_deadline.value.session_sequence == 1);
    CHECK(std::filesystem::exists(recovery_path));
    const auto parsed = wormedit::parse_native_project_recovery_snapshot(read_text(recovery_path));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->metadata.original_source_path == directory.path / "source.bms");

    const auto same_revision = autosave.create_autosave(song, start + 10min, recovery_path);
    CHECK(same_revision.value.status == wormedit::native_project_autosave_status_t::not_due);
    song.revision = 3;
    const auto next_revision = autosave.create_autosave(song, start + 10min, recovery_path);
    REQUIRE(next_revision.value.status == wormedit::native_project_autosave_status_t::published);
    CHECK(next_revision.value.session_sequence == 2);

    autosave.mark_explicit_save({.project_id = song.id, .revision = song.revision});
    const auto now_clean = autosave.create_autosave(song, start + 15min, recovery_path);
    CHECK(now_clean.value.status == wormedit::native_project_autosave_status_t::not_due);
}

TEST_CASE("autosave retries a failed revision without advancing its cadence or sequence",
          "[unit][format][native-project][autosave][fault]") {
    using clock_t = wormedit::native_project_autosave_coordinator_t::clock_t;
    temporary_directory_t directory;
    const auto start = clock_t::time_point{};
    const auto backup = directory.path / "recovery.bak";
    REQUIRE(wormedit::create_native_project_recovery_file(backup, valid_song(4),
                                                             {.session_sequence = 3})
                .value);
    const auto original_backup = read_text(backup);
    wormedit::native_project_autosave_coordinator_t autosave(start, {}, 5min, 3);

    const auto failed = autosave.create_autosave(
        valid_song(5), start + 5min, backup, {}, {},
        fails_at(wormedit::native_project_storage_stage_t::replace_destination));
    CHECK(failed.value.status == wormedit::native_project_autosave_status_t::failed);
    CHECK(autosave.session_sequence() == 3);
    CHECK(read_text(backup) == original_backup);

    const auto retried = autosave.create_autosave(valid_song(5), start + 5min, backup);
    REQUIRE(retried.value.status == wormedit::native_project_autosave_status_t::published);
    CHECK(retried.value.session_sequence == 4);
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
        const auto result = autosave.create_autosave(valid_song(1), start + 1h, backup);
        CHECK(result.value.status == wormedit::native_project_autosave_status_t::failed);
        CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code ==
                   wormedit::native_project_storage_diagnostic_code_t::invalid_interval;
        }));
    }
}

TEST_CASE("schema-4 autosave preserves quantization presets through recovery",
          "[unit][format][native-project][autosave][recovery]") {
    using clock_t = wormedit::native_project_autosave_coordinator_t::clock_t;
    temporary_directory_t directory;
    const auto recovery_path = directory.path / "presets.bak";
    auto song = valid_song(2);
    song.quantization_presets = {
        {.name = "Triplets", .divisions = 12,
         .space = wormedit::quantization_space_t::authored_measure,
         .tie = wormedit::quantization_tie_t::later,
         .holds = wormedit::quantization_hold_t::start_only},
        {.name = "Sixteenths", .divisions = 16}};
    wormedit::native_project_autosave_coordinator_t autosave(clock_t::time_point{});
    const auto saved = autosave.create_autosave(song, clock_t::time_point{} + 5min, recovery_path);
    REQUIRE(saved.value.status == wormedit::native_project_autosave_status_t::published);
    const auto parsed = wormedit::parse_native_project_recovery_snapshot(read_text(recovery_path));
    REQUIRE(parsed.value);
    CHECK(parsed.value->song.schema_version == 7);
    CHECK(parsed.value->song.quantization_presets == song.quantization_presets);
}

TEST_CASE("recovery envelope accepts supported schema tags and rejects unknown versions",
          "[unit][format][native-project][recovery][negative]") {
    temporary_directory_t directory;
    const auto recovery_path = directory.path / "schemas.bak";
    REQUIRE(wormedit::create_native_project_recovery_file(
                recovery_path, valid_song(2), {.session_sequence = 1}).value);
    auto envelope = nlohmann::json::parse(read_text(recovery_path));
    for (const auto schema : std::array<std::uint64_t, 8>{0, 1, 2, 3, 4, 5, 6, 7}) {
        INFO(schema);
        envelope["schema_version"] = schema;
        const auto parsed = wormedit::parse_native_project_recovery_snapshot(envelope.dump());
        CHECK(parsed.value.has_value() == (schema >= 1 && schema <= 7));
    }
}

TEST_CASE("schema 6 autosave recovery preserves a BGM tail",
          "[unit][format][native-project][autosave][recovery]") {
    using clock_t = wormedit::native_project_autosave_coordinator_t::clock_t;
    temporary_directory_t directory;
    auto song = valid_song(7);
    song.charts = {{.id = wormedit::chart_id_t{1},
        .measures = {{.id = wormedit::measure_id_t{1},
            .bgm_events = {{.id = wormedit::object_id_t{2},
                .position = {wormedit::measure_id_t{1}, 0},
                .source_lane = wormedit::object_id_t{3},
                .end = wormedit::local_position_t{wormedit::measure_id_t{1}, 1}}}}},
        .bgm_source_lanes = {{.id = wormedit::object_id_t{3}, .name = "BGM"}}}};
    wormedit::native_project_autosave_coordinator_t autosave(clock_t::time_point{});
    const auto path = directory.path / "schema6.bak";
    REQUIRE(autosave.create_autosave(song, clock_t::time_point{} + 5min, path).value.status ==
            wormedit::native_project_autosave_status_t::published);
    const auto recovered =
        wormedit::parse_native_project_recovery_snapshot(read_text(path));
    REQUIRE(recovered.value);
    REQUIRE(recovered.value->song.schema_version == 7);
    CHECK(recovered.value->song.charts.front().measures.front().bgm_events.front().end ==
          wormedit::local_position_t{wormedit::measure_id_t{1}, 1});
}

TEST_CASE("native publication outcomes keep warning severity independent of publication", "[unit][native-storage][results]") {
    using namespace wormedit;
    native_project_storage_outcome_t outcome{.value = false,
        .diagnostics = {{.severity = diagnostic_severity_t::warning, .message = "warning", .path = "marker"}}};
    CHECK(outcome.ok());
    const auto legacy = std::move(outcome);
    CHECK_FALSE(legacy.value);
    CHECK(legacy.ok());
    CHECK(legacy.diagnostics.front().path == "marker");
}

TEST_CASE("native recovery parse outcomes retain warnings without a snapshot", "[unit][native-storage][results]") {
    using namespace wormedit;
    native_project_recovery_parse_outcome_t outcome{.diagnostics = {{.severity = diagnostic_severity_t::warning, .message = "warning"}}};
    CHECK(outcome.ok());
    const auto legacy = std::move(outcome);
    CHECK_FALSE(legacy.value);
    CHECK(legacy.ok());
    CHECK(legacy.diagnostics.front().message == "warning");
}

TEST_CASE("native primary byte publication retains the publication flag", "[unit][native-storage][results]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto path = directory.path / "marker";
    auto outcome = create_native_bytes_file(path, "payload");
    REQUIRE(outcome.ok());
    CHECK(outcome.value);
    CHECK(read_text(path) == "payload");
    const auto legacy = std::move(outcome);
    CHECK(legacy.value);
    CHECK(legacy.ok());
}

TEST_CASE("native shared recovery scan retains candidate selection beside warnings", "[unit][native-storage][results]") {
    using namespace wormedit;
    native_project_recovery_scan_outcome_t outcome{.value = {.candidates = {{.path = "candidate"}}, .preferred_recovery_candidate = 0},
        .diagnostics = {{.severity = diagnostic_severity_t::warning, .message = "warning"}}};
    CHECK(outcome.ok());
    const auto legacy = std::move(outcome);
    REQUIRE(legacy.value.candidates.size() == 1);
    CHECK(legacy.value.candidates.front().path == "candidate");
    CHECK(legacy.value.preferred_recovery_candidate == 0);
    CHECK(legacy.diagnostics.front().message == "warning");
}

TEST_CASE("native shared recovery action retains failure metadata", "[unit][native-storage][results]") {
    using namespace wormedit;
    const native_project_document_version_t version{song_id_t{17}, 23};
    const std::filesystem::path path{"quarantine"};
    native_project_recovery_action_outcome_t outcome{.value = {.version = version, .quarantined_path = path},
        .diagnostics = {{.severity = diagnostic_severity_t::error, .message = "error"}}};
    CHECK_FALSE(outcome.ok());
    const auto legacy = std::move(outcome);
    CHECK_FALSE(legacy.value.applied);
    CHECK(legacy.value.version == version);
    CHECK(legacy.value.quarantined_path == path);
    CHECK(!legacy.ok());
}

TEST_CASE("native shared autosave retains quiet status and counters beside warnings", "[unit][native-storage][results]") {
    using namespace wormedit;
    native_project_autosave_outcome_t outcome{.value = {.revision = 31, .session_sequence = 7},
        .diagnostics = {{.severity = diagnostic_severity_t::warning, .message = "warning"}}};
    CHECK(outcome.ok());
    const auto legacy = std::move(outcome);
    CHECK(legacy.value.status == native_project_autosave_status_t::not_due);
    CHECK(legacy.value.revision == 31);
    CHECK(legacy.value.session_sequence == 7);
    CHECK(legacy.diagnostics.front().message == "warning");
}

TEST_CASE("native primary recovery scan handles an empty request", "[unit][native-storage][results]") {
    using namespace wormedit;
    auto outcome = scan_native_project_recovery_candidates(native_project_recovery_scan_request_t{});
    REQUIRE(outcome.ok());
    CHECK(outcome.value.candidates.empty());
    CHECK_FALSE(outcome.value.preferred_recovery_candidate);
    const auto legacy = std::move(outcome);
    CHECK(legacy.value.candidates.empty());
}

TEST_CASE("native primary autosave rejects invalid intervals before publication", "[unit][native-storage][results]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto now = std::chrono::steady_clock::now();
    native_project_autosave_coordinator_t coordinator{now, {}, std::chrono::steady_clock::duration::zero(), 7};
    const auto path = directory.path / "backup";
    const auto outcome = coordinator.create_autosave(valid_song(21), now, path);
    REQUIRE_FALSE(outcome.ok());
    CHECK(outcome.value == native_project_autosave_payload_t{.status = native_project_autosave_status_t::failed, .revision = 21, .session_sequence = 7});
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK(coordinator.session_sequence() == 7);
    CHECK(outcome.diagnostics.front().code == native_project_storage_diagnostic_code_t::invalid_interval);
}
