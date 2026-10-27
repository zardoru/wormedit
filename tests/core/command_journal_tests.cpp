#include <wormedit/core/command_journal.h>
#include <wormedit/core/command.h>
#include <wormedit/core/autosave_loop.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include "../support/temporary_directory.h"
#include "../support/timing_links_fixture.h"
#include "../support/native_bms_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <iterator>
#include <future>
#include <latch>
#include <nlohmann/json.hpp>

using namespace wormedit;
namespace {
auto journal_song(std::uint64_t revision) -> song_document_t {
    return {.id = song_id_t{701}, .revision = revision,
        .charts = {{.id = chart_id_t{702}, .measures = {{.id = measure_id_t{703}}}}}};
}
auto journal_entry(std::uint64_t sequence, std::uint64_t before, std::uint64_t after) -> command_journal_entry_t {
    return {.sequence = sequence, .before_revision = before, .command = "Test edit",
        .snapshot = {.song = journal_song(after)}};
}
auto read_bytes(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}
void check_recovered(const command_journal_read_t& read, std::uint64_t revision, std::size_t records) {
    REQUIRE(read.snapshot);
    CHECK(read.snapshot->song.revision == revision);
    CHECK(read.accepted_records == records);
}
void append_entry(const command_journal_t& journal, const command_journal_entry_t& entry) {
    const auto appended = journal.append(entry);
    INFO((appended ? std::string{"ok"} : appended.error().message));
    REQUIRE(appended);
}
void check_valid_recovery(const diagnosed_t<command_journal_read_t,
    std::vector<native_project_storage_diagnostic_t>>& read, std::uint64_t revision, std::size_t records) {
    REQUIRE(read.ok());
    check_recovered(read.value, revision, records);
}
void check_stale_recovery_action(const native_project_recovery_action_outcome_t& stale) {
    CHECK_FALSE(stale.value.document);
    REQUIRE_FALSE(stale.diagnostics.empty());
    CHECK(stale.diagnostics.front().code == native_project_storage_diagnostic_code_t::recovery_candidate_changed);
}
void check_imported_recovery(const song_document_t& song) {
    REQUIRE_FALSE(song.audio_assets.empty());
    CHECK(song.audio_assets.front().source_path.is_absolute());
    REQUIRE_FALSE(song.charts.front().measures.front().notes.empty());
    CHECK(song.charts.front().lane_layouts.front().layout.lanes.size() == 4);
}
void check_retained_journal(const std::filesystem::path& journal, std::string_view before,
    const std::filesystem::path& backup, bool is_published) {
    CHECK(std::filesystem::exists(backup) == is_published);
    CHECK(read_bytes(journal) == before);
}
void check_checkpoint(const std::filesystem::path& path, std::string_view title, std::uint64_t sequence) {
    const auto backup = parse_native_project_recovery_snapshot(read_bytes(path));
    REQUIRE(backup.value);
    CHECK(backup.value->song.metadata.title == title);
    REQUIRE(backup.value->metadata.journal_position);
    CHECK(backup.value->metadata.journal_position->sequence == sequence);
}
void check_foreign_baseline_rejection(const command_journal_read_t& read) {
    auto foreign = journal_song(1);
    foreign.id = song_id_t{999};
    const auto replay = read.replay_on(foreign);
    CHECK_FALSE(replay.ok());
    CHECK(replay.value.id == foreign.id);
    REQUIRE_FALSE(replay.diagnostics.empty());
    CHECK(replay.diagnostics.front().code == diagnostic_code_t::invalid_binding);
}
void check_matching_metadata(const native_project_recovery_metadata_t& cached,
                             const native_project_recovery_metadata_t& fresh) {
    CHECK(cached.session_sequence == fresh.session_sequence);
    CHECK(cached.utc_write_time == fresh.utc_write_time);
    CHECK(cached.journal_position == fresh.journal_position);
    CHECK(cached.original_source_path == fresh.original_source_path);
    CHECK(cached.intended_project_path == fresh.intended_project_path);
    CHECK(cached.last_explicit_save_revision == fresh.last_explicit_save_revision);
}
void check_matching_chain(const command_journal_read_t& cached, const command_journal_read_t& fresh) {
    CHECK(cached.last_sequence == fresh.last_sequence);
    CHECK(cached.last_checksum == fresh.last_checksum);
    CHECK(cached.accepted_records == fresh.accepted_records);
    CHECK(cached.valid_bytes == fresh.valid_bytes);
    CHECK(cached.stopped_by_limit == fresh.stopped_by_limit);
}
void check_matching_recovery(const command_journal_read_t& cached,
    const diagnosed_t<command_journal_read_t, std::vector<native_project_storage_diagnostic_t>>& fresh) {
    REQUIRE(fresh.ok());
    REQUIRE(cached.snapshot);
    REQUIRE(fresh.value.snapshot);
    CHECK(write_native_project(cached.snapshot->song).value == write_native_project(fresh.value.snapshot->song).value);
    CHECK(cached.positions == fresh.value.positions);
    CHECK(cached.revisions == fresh.value.revisions);
    check_matching_metadata(cached.snapshot->metadata, fresh.value.snapshot->metadata);
    check_matching_chain(cached, fresh.value);
}
void repair_recovery_directory(const std::filesystem::path& blocked) {
    REQUIRE(std::filesystem::remove(blocked));
    REQUIRE(std::filesystem::create_directory(blocked));
}
}

TEST_CASE("Command journal replays full post-states and tolerates a torn final record", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "test.journal";
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    append_entry(journal, journal_entry(2, 2, 3));
    const auto read = journal.read();
    check_valid_recovery(read, 3, 2);
    const auto replay = read.value.replay_on(journal_song(1));
    REQUIRE(replay.ok());
    CHECK(replay.value.revision == 3);
    CHECK(read.value.replay_on(replay.value).value.revision == 3);
    check_foreign_baseline_rejection(read.value);
    auto bytes = read_bytes(path);
    bytes.resize(bytes.size() - 7);
    const auto torn = command_journal_read_t::from(bytes);
    CHECK_FALSE(torn.ok());
    check_recovered(torn.value, 2, 1);
}

TEST_CASE("Journal readers accept larger configured budgets and reject unsupported budgets", "[core][recovery][command-journal]") {
    const auto read = command_journal_read_t::from(command_journal_header, {.maximum_bytes = 96U * 1024U * 1024U});
    CHECK(read.ok());
    const auto unsupported = GENERATE(
        command_journal_limits_t{.maximum_bytes = command_journal_limits_t::maximum_supported_bytes + 1},
        command_journal_limits_t{.maximum_record_bytes = command_journal_limits_t::maximum_supported_record_bytes + 1},
        command_journal_limits_t{.maximum_records = command_journal_limits_t::maximum_supported_records + 1});
    const auto rejected = command_journal_read_t::from(command_journal_header, unsupported);
    CHECK_FALSE(rejected.ok());
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.front().code == native_project_storage_diagnostic_code_t::resource_limit);
}

TEST_CASE("Journal version two starts with a seed and keeps version one readable", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "version.journal";
    append_entry(command_journal_t{path}, journal_entry(1, 1, 2));
    const auto bytes = read_bytes(path);
    const auto header_end = bytes.find('\n');
    CHECK(nlohmann::json::parse(bytes.substr(0, header_end)).at("version") == 2);
    auto root = nlohmann::json::parse(bytes.substr(header_end + 1));
    REQUIRE(root.at("record").contains("kind"));
    CHECK(root.at("record").at("kind") == "seed");
    root["record"].erase("kind");
    root["record"].erase("payload_checksum");
    root["sha256"] = native_project_content_checksum(root.at("record").dump());
    const auto legacy = std::string{"{\"format\":\"wormedit-command-journal\",\"version\":1}\n"} + root.dump() + '\n';
    check_valid_recovery(command_journal_read_t::from(legacy), 2, 1);
}

TEST_CASE("A one-note journal delta stores accepted values by identity", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "notes.journal";
    command_journal_t journal{path};
    auto entry = journal_entry(1, 1, 2);
    auto& chart = entry.snapshot.song.charts.front();
    chart.lane_layouts.push_back({.id = object_id_t{500}, .position = {measure_id_t{703}, 0},
        .layout = {.lanes = {lane_definition_t::from(1, 0, stage_group_t::none)}}});
    for (std::uint64_t i = 0; i < 200; ++i)
        chart.measures.front().notes.push_back({.id = object_id_t{i + 1},
            .position = {measure_id_t{703}, i}, .lane = lane_id_t{1}});
    append_entry(journal, entry);
    const auto seed_end = read_bytes(path).size();
    ++entry.sequence;
    entry.before_revision = entry.snapshot.song.revision++;
    chart.measures.front().notes.front().label = "Accepted move";
    append_entry(journal, entry);
    const auto delta = read_bytes(path).substr(seed_end);
    CHECK(delta.size() <= 16 * 1024);
    CHECK(nlohmann::json::parse(delta).at("record").at("kind") == "delta");
    const auto recovered = journal.read();
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value.snapshot);
    CHECK(write_native_project(recovered.value.snapshot->song).value == write_native_project(entry.snapshot.song).value);
}

TEST_CASE("Command journal capacity failure retains the old file and checkpoint retains later edits", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "test.journal";
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    append_entry(journal, journal_entry(2, 2, 3));
    const auto before = read_bytes(path);
    command_journal_t capped{path, {.maximum_bytes = before.size()}};
    CHECK_FALSE(capped.append(journal_entry(3, 3, 4)));
    CHECK(read_bytes(path) == before);
    REQUIRE(journal.checkpoint(1));
    const auto read = journal.read();
    // The retained delta still depends on the seed through the checkpoint.
    check_valid_recovery(read, 3, 2);
    CHECK(read.value.last_sequence == 2);
}

TEST_CASE("Identical journal retry succeeds at capacity", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "retry.journal";
    command_journal_t journal{path, {.maximum_records = 1}};
    const auto entry = journal_entry(1, 1, 2);
    append_entry(journal, entry);
    const auto before = read_bytes(path);
    append_entry(journal, entry);
    CHECK(read_bytes(path) == before);
    check_valid_recovery(journal.read(), 2, 1);
}

TEST_CASE("Journal append retains the accepted immutable state and matches fresh recovery", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    command_journal_t journal{directory->path() / "cached.journal"};
    auto entry = journal_entry(7, 1, 2);
    const auto chart = entry.snapshot.song.charts.front();
    entry.snapshot.song = test::native_bms_fixture(entry.snapshot.song, chart);
    entry.snapshot.metadata.session_sequence = 999;
    entry.snapshot.metadata.original_source_path = "source/import.osu";
    entry.snapshot.metadata.intended_project_path = "projects/native.wormedit";
    entry.snapshot.metadata.last_explicit_save_revision = 1;
    entry.snapshot.metadata.utc_write_time += std::chrono::nanoseconds{123};
    entry.snapshot.metadata.journal_position = native_project_journal_position_t{.path = "journal", .sequence = 999};
    append_entry(journal, entry);
    REQUIRE(journal.accepted_state());
    const auto cached = *journal.accepted_state();
    REQUIRE(cached.snapshot);
    CHECK(cached.snapshot->song.prepared_bms_programs.front() == entry.snapshot.song.prepared_bms_programs.front());
    check_matching_recovery(cached, journal.read());
}

TEST_CASE("Journal payloads and native saves use the same compact JSON", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "compact.journal";
    const auto entry = journal_entry(1, 1, 2);
    append_entry(command_journal_t{path}, entry);
    const auto root = nlohmann::json::parse(read_bytes(path).substr(command_journal_header.size()));
    const auto payload = root.at("record").at("payload").get<std::string>();
    const auto saved = write_native_project(entry.snapshot.song);
    REQUIRE(saved.value);
    CHECK(payload == nlohmann::json::parse(payload).dump() + '\n');
    CHECK(payload == *saved.value);
    CHECK(parse_native_project(payload).ok());
}

TEST_CASE("Journal encode budget includes escaped payload and wrapper bytes", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto entry = journal_entry(1, 1, 2);
    entry.snapshot.song.metadata.title = std::string(512, '"');
    const auto payload = write_native_project(entry.snapshot.song);
    REQUIRE(payload.value);
    const auto path = directory->path() / "escaped.journal";
    const auto appended = command_journal_t{path, {.maximum_record_bytes = payload.value->size()}}.append(entry);
    REQUIRE_FALSE(appended);
    CHECK(appended.error().code == failure_code_t::capacity_exceeded);
    CHECK_FALSE(std::filesystem::exists(path));
}

namespace {
auto change_title(document_history_t& history, std::string title) -> document_command_result_t {
    auto document = history.document;
    document.metadata.title = std::move(title);
    return commit_document_command(history, std::move(document), "Set title");
}
auto checked_scan(const std::filesystem::path& primary) -> native_project_recovery_scan_outcome_t {
    auto scan = scan_native_project_recovery_candidates({.primary_path = primary});
    REQUIRE(scan.value.preferred_recovery_candidate);
    return scan;
}
void check_preferred_title(const native_project_recovery_scan_outcome_t& scan, std::string_view title) {
    REQUIRE(scan.value.preferred_recovery_candidate);
    const auto& candidate = scan.value.candidates[*scan.value.preferred_recovery_candidate];
    REQUIRE(candidate.document);
    CHECK(candidate.document->metadata.title == title);
}
void check_recover_undone_revision(const native_project_recovery_scan_outcome_t& scan) {
    const auto recovered = perform_native_project_recovery_action(scan.value.candidates,
        *scan.value.preferred_recovery_candidate, native_project_recovery_action_t::recover_into_session,
        {.expected_session = {.is_set = true, .version = native_project_document_version_t{
            .project_id = song_id_t{701}, .revision = 2}}});
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value.applied);
    REQUIRE(recovered.value.document);
    CHECK(recovered.value.document->revision == 1);
}
void check_invalid_tail(const std::string& bytes, native_project_storage_diagnostic_code_t code) {
    const auto read = command_journal_read_t::from(bytes);
    CHECK_FALSE(read.ok());
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == code);
    check_recovered(read.value, 2, 1);
}
auto altered_second_record(const std::string& bytes,
    const std::function<void(nlohmann::json&)>& change, bool checksum = true) -> std::string {
    const auto begin = bytes.find('\n', command_journal_header.size()) + 1;
    auto record = nlohmann::json::parse(bytes.substr(begin));
    change(record["record"]);
    if (checksum) record["sha256"] = native_project_content_checksum(record["record"].dump());
    return bytes.substr(0, begin) + record.dump() + '\n';
}
}

TEST_CASE("Shared publication observer sees accepted commands and Undo Redo but no detached preview", "[core][command-journal]") {
    auto history = document_history_t::from(journal_song(1));
    std::vector<std::uint64_t> published, barriers;
    history.publications.set([&](const song_document_t& document, std::uint64_t, std::string_view) {
        published.push_back(document.revision);
    }, [&](const song_document_t& before) { barriers.push_back(before.revision); });
    (void)change_title(history, "A");
    (void)change_title(history, "B");
    (void)undo_document_command(history);
    (void)redo_document_command(history);
    CHECK(published == std::vector<std::uint64_t>{2, 3, 2, 3});
    CHECK(barriers == std::vector<std::uint64_t>{1, 2, 3, 2});
    const auto preview = document_command_transaction_t::preview(history.document,
        [](document_history_t& isolated) { return change_title(isolated, "Preview"); });
    CHECK(preview.applied);
    CHECK(published.size() == 4);
    CHECK(barriers.size() == 4);
}

TEST_CASE("Observed edits are journaled on the worker and recovered by the existing scanner", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    auto history = document_history_t::from(journal_song(1));
    REQUIRE(create_native_project_file(primary, history.document).value);
    task_scheduler_t scheduler{1, 1};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, native_project_document_version_t{history.document.id, 1}};
    auto target = native_project_autosave_target_t::from(history.document.id, true, primary, {});
    target.asset_base = directory->path();
    loop.observe(history, target);
    REQUIRE(change_title(history, "A").applied);
    REQUIRE(change_title(history, "B").applied);
    loop.flush_journal();
    const auto scan = checked_scan(primary);
    check_preferred_title(scan, "B");
    const auto& candidate = scan.value.candidates[*scan.value.preferred_recovery_candidate];
    CHECK(candidate.is_command_journal);
    const auto read = command_journal_t{candidate.path}.read();
    check_recovered(read.value, 3, 2);
}

TEST_CASE("Successful autosave compacts its watermark and preserves a later publication", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    auto history = document_history_t::from(journal_song(1));
    REQUIRE(create_native_project_file(primary, history.document).value);
    task_scheduler_t scheduler{1, 1};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, native_project_document_version_t{history.document.id, 1}};
    const auto target = native_project_autosave_target_t::from(history.document.id, true, primary, {});
    loop.observe(history, target);
    (void)change_title(history, "Snapshot A");
    loop.flush_journal();
    (void)loop.tick(history.document, true, started + std::chrono::minutes{5}, target);
    (void)change_title(history, "Later B");
    loop.flush_journal();
    const auto scan = checked_scan(primary);
    check_preferred_title(scan, "Later B");
    const auto& candidate = scan.value.candidates[*scan.value.preferred_recovery_candidate];
    const auto read = command_journal_t{candidate.path}.read();
    check_recovered(read.value, 3, 1);
    CHECK(read.value.last_sequence == 2);
}

TEST_CASE("Command journal checksums cover command metadata and stop at the corrupt record", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "test.journal";
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    append_entry(journal, journal_entry(2, 2, 3));
    const auto damaged = altered_second_record(read_bytes(path), [](nlohmann::json& body) { body["command"] = "tampered"; }, false);
    check_invalid_tail(damaged, native_project_storage_diagnostic_code_t::checksum_mismatch);
}

TEST_CASE("Command journal rejects sequence gaps revision gaps and invalid full post-states", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "test.journal";
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    append_entry(journal, journal_entry(2, 2, 3));
    const auto original = read_bytes(path);
    std::string damaged;
    SECTION("sequence gap") { damaged = altered_second_record(original, [](nlohmann::json& body) { body["sequence"] = std::uint64_t{3}; }); }
    SECTION("revision gap") { damaged = altered_second_record(original, [](nlohmann::json& body) { body["before_revision"] = std::uint64_t{99}; }); }
    SECTION("invalid post-state") { damaged = altered_second_record(original, [](nlohmann::json& body) { body["payload"] = "{}"; }); }
    SECTION("project mismatch") {
        damaged = altered_second_record(original, [](nlohmann::json& body) {
            auto payload = nlohmann::json::parse(body["payload"].template get<std::string>());
            payload["document"]["id"] = std::uint64_t{999};
            body["payload"] = payload.dump();
        });
    }
    check_invalid_tail(damaged, native_project_storage_diagnostic_code_t::validation_failure);
}

TEST_CASE("Identical duplicate journal records replay once and compact completely", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "test.journal";
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    const auto first = read_bytes(path).substr(command_journal_header.size());
    append_entry(journal, journal_entry(2, 2, 3));
    const auto original = read_bytes(path);
    const auto duplicate = std::string{command_journal_header} + first + original.substr(command_journal_header.size());
    { std::ofstream output(path, std::ios::binary); output << duplicate; }
    const auto read = journal.read();
    REQUIRE(read.ok());
    check_recovered(read.value, 3, 2);
    REQUIRE(journal.checkpoint(1));
    CHECK(journal.read().value.accepted_records == 2);
}

TEST_CASE("Journal recovery actions validate the complete scanned bytes including a corrupt tail", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    REQUIRE(create_native_project_file(primary, journal_song(1)).value);
    const auto path = std::filesystem::path{primary.string() + ".bak.journal-test"};
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    { std::ofstream output(path, std::ios::app); output << "torn"; }
    const auto scan = checked_scan(primary);
    const auto index = *scan.value.preferred_recovery_candidate;
    const auto inspected = perform_native_project_recovery_action(scan.value.candidates, index,
        native_project_recovery_action_t::inspect_read_only);
    REQUIRE(inspected.value.document);
    CHECK(inspected.value.document->revision == 2);
    { std::ofstream output(path, std::ios::app); output << "changed"; }
    const auto stale = perform_native_project_recovery_action(scan.value.candidates, index,
        native_project_recovery_action_t::inspect_read_only);
    check_stale_recovery_action(stale);
}

TEST_CASE("Journaled Undo remains recoverable when its numeric revision is older than the primary", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    auto history = document_history_t::from(journal_song(1));
    (void)change_title(history, "Saved A");
    REQUIRE(create_native_project_file(primary, history.document).value);
    task_scheduler_t scheduler{1, 1};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, native_project_document_version_t{history.document.id, 2}};
    const auto target = native_project_autosave_target_t::from(history.document.id, true, primary, {});
    loop.observe(history, target);
    (void)change_title(history, "B");
    (void)undo_document_command(history);
    (void)undo_document_command(history);
    loop.flush_journal();
    const auto scan = checked_scan(primary);
    check_preferred_title(scan, "");
    const auto& candidate = scan.value.candidates[*scan.value.preferred_recovery_candidate];
    CHECK(candidate.version->revision == 1);
    CHECK(candidate.comparison == native_project_recovery_comparison_t::journaled_changes);
    check_recover_undone_revision(scan);
    (void)loop.tick(history.document, true, started + std::chrono::minutes{5}, target);
    loop.flush_journal();
    const auto checkpointed = checked_scan(primary);
    check_preferred_title(checkpointed, "");
    CHECK(checkpointed.value.candidates[*checkpointed.value.preferred_recovery_candidate].path == target.backup_path);
    check_recover_undone_revision(checkpointed);
}

TEST_CASE("Failed autosave retains the complete journal generation", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    auto history = document_history_t::from(journal_song(1));
    REQUIRE(create_native_project_file(primary, history.document).value);
    task_scheduler_t scheduler{1, 1};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, native_project_document_version_t{history.document.id, 1}};
    const auto target = native_project_autosave_target_t::from(history.document.id, true, primary, {});
    loop.observe(history, target);
    (void)change_title(history, "Protected A");
    loop.flush_journal();
    const auto before_scan = checked_scan(primary);
    const auto path = before_scan.value.candidates[*before_scan.value.preferred_recovery_candidate].path;
    const auto before = read_bytes(path);
    auto failed_stage = native_project_storage_stage_t::replace_destination;
    bool is_published{};
    SECTION("replacement failed") {}
    SECTION("directory sync is uncertain") {
        failed_stage = native_project_storage_stage_t::sync_destination_directory;
        is_published = true;
    }
    (void)loop.tick(history.document, true, started + std::chrono::minutes{5}, target,
        {.allow_stage = [failed_stage](native_project_storage_stage_t stage, const std::filesystem::path&) {
            return stage != failed_stage;
        }});
    loop.flush_journal();
    check_retained_journal(path, before, target.backup_path, is_published);
}

TEST_CASE("Journal capacity requests an early full checkpoint instead of losing earlier edits", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto history = document_history_t::from(journal_song(1));
    task_scheduler_t scheduler{1, 1};
    notification_center_t notifications;
    const auto payload = write_native_project(history.document);
    REQUIRE(payload.value);
    const auto limits = GENERATE_COPY(command_journal_limits_t{.maximum_bytes = 512},
        command_journal_limits_t{.maximum_record_bytes = 128},
        command_journal_limits_t{.maximum_record_bytes = payload.value->size() + 64});
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, {}, 0, std::chrono::minutes{5},
        std::chrono::seconds{30}, notification_sink_t{notifications}, limits};
    const auto target = native_project_autosave_target_t::from(history.document.id, false, {}, directory->path());
    loop.observe(history, target);
    (void)change_title(history, "A");
    (void)change_title(history, "B");
    (void)change_title(history, "C");
    loop.flush_journal();
    (void)loop.tick(history.document, true, native_project_autosave_loop_t::clock_t::now(), target);
    loop.flush_journal();
    check_checkpoint(target.backup_path, "C", 3);
    (void)notifications.drain_inbox({});
    CHECK(notifications.statistics().posted == 0);
}

TEST_CASE("Ordinary edits do not wait for an in-flight recovery write", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto history = document_history_t::from(journal_song(1));
    task_scheduler_t scheduler{1, 4};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler};
    const auto primary = directory->path() / "project.wormedit";
    const auto target = native_project_autosave_target_t::from(history.document.id, true, primary, {});
    loop.observe(history, target);
    std::latch worker_started{1}, release_worker{1};
    task_graph_t graph{1};
    REQUIRE(graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token) -> task_outcome_t {
        worker_started.count_down();
        release_worker.wait();
        return task_value_t{};
    }));
    auto submitted = scheduler.begin(std::move(graph));
    REQUIRE(submitted);
    worker_started.wait();
    auto edits = std::async(std::launch::async, [&] {
        const auto first = change_title(history, "A");
        const auto second = change_title(history, "B");
        return first.applied && second.applied;
    });
    // Release even on regression so a blocking implementation fails without hanging.
    const auto status = edits.wait_for(std::chrono::seconds{1});
    release_worker.count_down();
    CHECK(status == std::future_status::ready);
    CHECK(edits.get());
    loop.flush_journal();
    const auto scan = checked_scan(primary);
    check_preferred_title(scan, "B");
}

TEST_CASE("Durable journal flush checkpoints the latest edit after record capacity", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto history = document_history_t::from(journal_song(1));
    task_scheduler_t scheduler{1, 4};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, {}, 0, std::chrono::minutes{5},
        std::chrono::seconds{30}, notification_sink_t::muted(), {.maximum_records = 1}};
    const auto target = native_project_autosave_target_t::from(history.document.id, false, {}, directory->path());
    loop.observe(history, target);
    REQUIRE(change_title(history, "A").applied);
    REQUIRE(loop.flush_journal(history.document));
    REQUIRE(change_title(history, "B").applied);
    REQUIRE(loop.flush_journal(history.document));
    check_checkpoint(target.backup_path, "B", 2);
}

TEST_CASE("Shutdown drains queued journal recovery before stopping its scheduler", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto history = document_history_t::from(journal_song(1));
    task_scheduler_t scheduler{1, 4};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, {}, 0, std::chrono::minutes{5},
        std::chrono::seconds{30}, notification_sink_t::muted(), {.maximum_records = 1}};
    const auto target = native_project_autosave_target_t::from(history.document.id, false, {}, directory->path());
    loop.observe(history, target);
    REQUIRE(change_title(history, "A").applied);
    REQUIRE(change_title(history, "B").applied);
    REQUIRE(loop.flush_journal(history.document));
    scheduler.shutdown();
    CHECK(scheduler.outstanding_tasks() == 0);
    CHECK_FALSE(loop.is_busy());
    check_checkpoint(target.backup_path, "B", 2);
}

TEST_CASE("Durable journal flush reports storage failure and retries the current checkpoint immediately", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto blocked = directory->path() / "blocked";
    std::ofstream(blocked) << "not a directory";
    auto history = document_history_t::from(journal_song(1));
    task_scheduler_t scheduler{1, 4};
    native_project_autosave_loop_t loop{native_project_autosave_loop_t::clock_t::now(), scheduler};
    const auto target = native_project_autosave_target_t::from(history.document.id, false, {}, blocked);
    loop.observe(history, target);
    REQUIRE(change_title(history, "A").applied);
    REQUIRE(change_title(history, "B").applied);
    CHECK_FALSE(loop.flush_journal(history.document));
    repair_recovery_directory(blocked);
    REQUIRE(loop.flush_journal(history.document));
    check_checkpoint(target.backup_path, "B", 2);
}

TEST_CASE("Invalid journal snapshots still warn and request recovery", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    task_scheduler_t scheduler{1, 1};
    notification_center_t notifications;
    command_journal_runtime_t journal{scheduler, notification_sink_t{notifications}};
    auto song = journal_song(2);
    song.schema_version = 0;
    const auto target = native_project_autosave_target_t::from(song.id, false, {}, directory->path());
    journal.configure(song.id, target);
    journal.record(song, 1, "Invalid edit");
    journal.drain();
    CHECK(journal.requires_checkpoint());
    (void)notifications.drain_inbox({});
    CHECK(notifications.statistics().posted == 1);
}

TEST_CASE("A cached journal writer refuses to bury an unknown torn tail", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "test.journal";
    command_journal_t journal{path};
    append_entry(journal, journal_entry(1, 1, 2));
    { std::ofstream output(path, std::ios::app); output << "torn"; }
    const auto before = read_bytes(path);
    CHECK_FALSE(journal.append(journal_entry(2, 2, 3)));
    CHECK(read_bytes(path) == before);
    check_recovered(journal.read().value, 2, 1);
}

TEST_CASE("A corrupt journal never blocks loading the valid backup", "[core][recovery][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    REQUIRE(create_native_project_file(primary, journal_song(1)).value);
    auto snapshot = journal_song(2);
    snapshot.metadata.title = "Safe backup";
    REQUIRE(create_native_project_recovery_file(native_project_adjacent_backup_path(primary), snapshot, {}).value);
    { std::ofstream output(primary.string() + ".bak.journal-bad"); output << command_journal_header << "bad\n"; }
    const auto before = read_bytes(primary);
    const auto scan = checked_scan(primary);
    check_preferred_title(scan, "Safe backup");
    CHECK(read_bytes(primary) == before);
}

TEST_CASE("Untitled imported sessions journal complete native state with anchored assets", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const std::string source = "osu file format v14\n[General]\nMode:3\nAudioFilename:song.ogg\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,1,0,100,1,0\n"
        "[HitObjects]\n64,192,500,1,0,0:0:0:0:\n";
    auto imported = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE(imported.value);
    auto history = document_history_t::from(std::move(*imported.value), 128, false);
    const std::string script = "return {notes={},layers={}}";
    history.document.generator_recipes.push_back(
        {.id = recipe_id_t{9000},
         .api_version = "1",
         .source_path = "scripts/generate.lua",
         .source_hash = native_project_content_checksum(script),
         .source = script,
         .chart = history.document.charts.front().id});
    task_scheduler_t scheduler{1, 1};
    native_project_autosave_loop_t loop{native_project_autosave_loop_t::clock_t::now(), scheduler};
    auto target = native_project_autosave_target_t::from(history.document.id, false, {}, directory->path());
    target.asset_base = directory->path();
    loop.observe(history, target);
    (void)change_title(history, "Recovered import");
    loop.flush_journal();
    const auto scan = scan_native_project_recovery_candidates({.caller_supplied_directories = {directory->path()}});
    check_preferred_title(scan, "Recovered import");
    check_imported_recovery(*scan.value.candidates[*scan.value.preferred_recovery_candidate].document);
    CHECK(scan.value.candidates[*scan.value.preferred_recovery_candidate]
              .document->generator_recipes == history.document.generator_recipes);
}

TEST_CASE("New journal generations continue recovery chronology across revision rewinds", "[core][recovery][autosave][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    auto history = document_history_t::from(journal_song(1));
    REQUIRE(create_native_project_file(primary, history.document).value);
    const auto old_path = std::filesystem::path{primary.string() + ".bak.journal-old"};
    auto old = journal_entry(50, 1, 10);
    old.snapshot.song.metadata.title = "Older branch";
    old.snapshot.metadata.last_explicit_save_revision = 1;
    old.snapshot.metadata.journal_position = native_project_journal_position_t{.path = old_path, .sequence = 50};
    append_entry(command_journal_t{old_path}, old);
    task_scheduler_t scheduler{1, 1};
    native_project_autosave_loop_t loop{native_project_autosave_loop_t::clock_t::now(), scheduler,
        native_project_document_version_t{history.document.id, 1}, 50};
    const auto target = native_project_autosave_target_t::from(history.document.id, true, primary, {});
    loop.observe(history, target);
    (void)change_title(history, "New branch");
    loop.flush_journal();
    const auto scan = checked_scan(primary);
    check_preferred_title(scan, "New branch");
    CHECK(std::filesystem::exists(old_path));
}

TEST_CASE("Journal wrapper nesting is bounded before native payload decoding", "[core][recovery][command-journal]") {
    const auto bytes = std::string{command_journal_header} + std::string(10, '[') + "0" + std::string(10, ']') + '\n';
    const auto read = command_journal_read_t::from(bytes);
    CHECK_FALSE(read.ok());
    CHECK(read.value.stopped_by_limit);
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == native_project_storage_diagnostic_code_t::resource_limit);
}
namespace {
auto run_link_journal_edits(test::timing_links_fixture_t& fixture, native_project_autosave_loop_t& loop,
    const native_project_autosave_target_t& target, native_project_autosave_loop_t::clock_t::time_point started,
    bool checkpoint) -> song_document_t {
    auto baseline = fixture.history.document;
    REQUIRE(fixture.set_bpm(fixture.source_id, {150, 1}).applied);
    if (checkpoint) {
        loop.flush_journal();
        baseline = fixture.history.document;
        (void)loop.tick(fixture.history.document, true, started + std::chrono::minutes{5}, target);
    }
    REQUIRE(fixture.set_bpm(fixture.target_id, {160, 1}).applied);
    REQUIRE(fixture.relink().applied);
    REQUIRE(undo_document_command(fixture.history));
    return baseline;
}
void check_torn_link_journal(const std::filesystem::path& path, const song_document_t& before_undo, std::size_t records) {
    auto bytes = read_bytes(path);
    REQUIRE(bytes.size() > 10);
    bytes.resize(bytes.size() - 10);
    const auto read = command_journal_read_t::from(bytes);
    REQUIRE_FALSE(read.ok());
    check_recovered(read.value, before_undo.revision, records);
    CHECK(write_native_project(read.value.snapshot->song).value == write_native_project(before_undo).value);
}
}
TEST_CASE("Journal recovery retains linked source updates local overrides Relink and Undo across autosave cursors", "[core][recovery][timing-links]") {
    const auto checkpoint = GENERATE(false, true);
    auto fixture = test::timing_links_fixture_t::from();
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "linked.wormedit";
    REQUIRE(create_native_project_file(primary, fixture.history.document).value);
    task_scheduler_t scheduler{1, 1};
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler,
        native_project_document_version_t{fixture.history.document.id, fixture.history.document.revision}};
    auto target = native_project_autosave_target_t::from(fixture.history.document.id, true, primary, {});
    target.asset_base = directory->path();
    loop.observe(fixture.history, target);
    const auto baseline = run_link_journal_edits(fixture, loop, target, started, checkpoint);
    loop.flush_journal();
    const auto scan = checked_scan(primary);
    const auto& candidate = scan.value.candidates[*scan.value.preferred_recovery_candidate];
    CHECK(candidate.is_command_journal);
    const auto read = command_journal_t{candidate.path}.read();
    const std::size_t records = checkpoint ? 3 : 4;
    check_valid_recovery(read, fixture.history.document.revision, records);
    const auto replay = read.value.replay_on(baseline);
    REQUIRE(replay.ok());
    CHECK(write_native_project(replay.value).value == write_native_project(fixture.history.document).value);
    check_torn_link_journal(candidate.path, fixture.history.redo.back().after, records - 1);
}

namespace {
auto delta_entry(std::uint64_t sequence) -> command_journal_entry_t {
    auto entry = journal_entry(sequence, sequence, sequence + 1);
    // Keep the conservative seed-size lower bound comfortably above small deltas
    // for the full 128-record replay budget.
    entry.snapshot.song.metadata.artist = std::string(4096, 'a');
    entry.snapshot.song.metadata.title = std::to_string(sequence);
    return entry;
}
auto journal_lines(std::string_view bytes) -> std::vector<nlohmann::json> {
    std::vector<nlohmann::json> result;
    auto begin = bytes.find('\n') + 1;
    while (begin < bytes.size()) {
        const auto end = bytes.find('\n', begin);
        REQUIRE(end != std::string_view::npos);
        result.push_back(nlohmann::json::parse(bytes.substr(begin, end - begin)));
        begin = end + 1;
    }
    return result;
}
auto encode_lines(const std::vector<nlohmann::json>& lines) -> std::string {
    std::string result{command_journal_header};
    for (auto line : lines) {
        line["sha256"] = native_project_content_checksum(line.at("record").dump());
        result += line.dump() + '\n';
    }
    return result;
}
void check_delta_base(const nlohmann::json& previous, const nlohmann::json& next) {
    CHECK(next.at("record").at("kind") == "delta");
    CHECK(next.at("record").at("base_sequence") == previous.at("record").at("sequence"));
    CHECK(next.at("record").at("base_checksum") == previous.at("sha256"));
}
void check_delta_chain(const std::vector<nlohmann::json>& lines) {
    REQUIRE(lines.size() == 3);
    CHECK(lines.front().at("record").at("kind") == "seed");
    for (std::size_t i = 1; i < lines.size(); ++i) check_delta_base(lines[i - 1], lines[i]);
}

void check_torn_prefix(std::string_view bytes, std::size_t cut, std::size_t record) {
    INFO(cut);
    const auto read = command_journal_read_t::from(bytes.substr(0, cut));
    CHECK_FALSE(read.ok());
    CHECK(read.value.accepted_records == record);
    CHECK(read.value.snapshot.has_value() == (record > 0));
    if (record) CHECK(read.value.snapshot->song.metadata.title == std::to_string(record));
}
void check_latest_seed(const command_journal_t& journal, const std::filesystem::path& path, std::uint64_t last) {
    const auto lines = journal_lines(read_bytes(path));
    CHECK(lines.back().at("record").at("kind") == "seed");
    const auto recovered = journal.read();
    REQUIRE(recovered.ok());
    CHECK(recovered.value.last_sequence == last);
    REQUIRE(recovered.value.snapshot);
    CHECK(recovered.value.snapshot->song.revision == last + 1);
}
void check_compacted_segment(const std::filesystem::path& path) {
    const auto lines = journal_lines(read_bytes(path));
    REQUIRE(lines.size() == 2);
    CHECK(lines.front().at("record").at("sequence") == 4);
    CHECK(lines.front().at("record").at("kind") == "seed");
    CHECK(lines.back().at("record").at("kind") == "delta");
}

}

TEST_CASE("Journal deltas chain to the preceding sequence and checksum and retry identically", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "chain.journal";
    command_journal_t journal{path, {.maximum_records = 3}};
    for (std::uint64_t i = 1; i <= 2; ++i) append_entry(journal, delta_entry(i));
    const auto last = delta_entry(3);
    append_entry(journal, last);
    const auto bytes = read_bytes(path);
    const auto lines = journal_lines(bytes);
    check_delta_chain(lines);
    append_entry(journal, last);
    CHECK(read_bytes(path) == bytes);
    auto duplicate = bytes + lines.back().dump() + '\n';
    check_valid_recovery(command_journal_read_t::from(duplicate), 4, 3);
}

TEST_CASE("Journal recovery stops before wrong-base gaps and conflicting duplicate deltas", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "corrupt-chain.journal";
    command_journal_t journal{path};
    for (std::uint64_t i = 1; i <= 3; ++i) append_entry(journal, delta_entry(i));
    auto lines = journal_lines(read_bytes(path));
    auto& body = lines.back()["record"];
    SECTION("wrong base checksum") { body["base_checksum"] = std::string(64, '0'); }
    SECTION("wrong base sequence") { body["base_sequence"] = 1U; }
    SECTION("gap") { body["sequence"] = 4U; }
    SECTION("conflicting duplicate") { body["sequence"] = 2U; }
    const auto read = command_journal_read_t::from(encode_lines(lines));
    CHECK_FALSE(read.ok());
    check_recovered(read.value, 3, 2);
}

TEST_CASE("Journal truncation at each record boundary and within seeds and deltas retains its prefix", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "truncation.journal";
    command_journal_t journal{path};
    for (std::uint64_t i = 1; i <= 4; ++i) append_entry(journal, delta_entry(i));
    const auto bytes = read_bytes(path);
    std::size_t begin = command_journal_header.size();
    for (std::size_t record = 0; record < 4; ++record) {
        const auto end = bytes.find('\n', begin) + 1;
        for (const auto cut : {begin + 1, begin + (end - begin) / 2, end - 1}) {
            check_torn_prefix(bytes, cut, record);
        }
        const auto complete = command_journal_read_t::from(std::string_view{bytes}.substr(0, end));
        check_valid_recovery(complete, record + 2, record + 1);
        begin = end;
    }
}

TEST_CASE("Native validation failure after deltas recovers the latest complete seed", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "invalid-graph.journal";
    command_journal_t journal{path};
    for (std::uint64_t i = 1; i <= 3; ++i) append_entry(journal, delta_entry(i));
    auto lines = journal_lines(read_bytes(path));
    auto payload = nlohmann::json::parse(lines.back()["record"]["payload"].get<std::string>());
    payload["document"]["fields"]["charts"] = {{"key", "id"}, {"update", {{{"id", 702U},
        {"patch", {{"fields", {{"source_family", {{"set", 9999U}}}}}}}}}}};
    lines.back()["record"]["payload"] = payload.dump();
    const auto recovered = command_journal_read_t::from(encode_lines(lines));
    CHECK_FALSE(recovered.ok());
    check_recovered(recovered.value, 2, 1);
    REQUIRE(recovered.value.snapshot);
    CHECK(recovered.value.snapshot->song.metadata.title == "1");
}

TEST_CASE("Failed and skipped journal publications reseed", "[core][recovery][command-journal][delta]") {
    const auto failed = GENERATE(false, true);
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "reseed.journal";
    command_journal_t journal{path};
    append_entry(journal, delta_entry(1));
    if (failed) {
        auto invalid = delta_entry(2);
        invalid.snapshot.song.schema_version = 0;
        CHECK_FALSE(journal.append(invalid));
    }
    const std::uint64_t next = failed ? 2 : 3;
    append_entry(journal, delta_entry(next));
    check_latest_seed(journal, path, next);
}

TEST_CASE("A journal delta exceeding half a seed stores a full seed", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "large-delta.journal";
    command_journal_t journal{path};
    append_entry(journal, delta_entry(1));
    auto large = delta_entry(2);
    large.snapshot.song.metadata.title = std::string(20'000, 'z');
    append_entry(journal, large);
    check_latest_seed(journal, path, 2);
}

TEST_CASE("Journal seeds bound replay to 128 deltas", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "bounded-replay.journal";
    command_journal_t journal{path};
    for (std::uint64_t i = 1; i <= 130; ++i) append_entry(journal, delta_entry(i));
    const auto lines = journal_lines(read_bytes(path));
    CHECK(lines[128].at("record").at("kind") == "delta");
    check_latest_seed(journal, path, 130);
}

TEST_CASE("Journal compaction retains the newest required seed and drops older segments", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "compact-segments.journal";
    command_journal_t journal{path};
    append_entry(journal, delta_entry(1));
    append_entry(journal, delta_entry(2));
    append_entry(journal, delta_entry(4)); // A gap starts a fresh seed.
    append_entry(journal, delta_entry(5));
    REQUIRE(journal.checkpoint(4));
    check_compacted_segment(path);
    check_valid_recovery(journal.read(), 6, 2);
    REQUIRE(journal.checkpoint(5));
    CHECK_FALSE(std::filesystem::exists(path));
    append_entry(journal, delta_entry(6));
    CHECK(journal_lines(read_bytes(path)).front().at("record").at("kind") == "seed");
}

namespace {
void run_overflow_edits(document_history_t& history, task_scheduler_t& scheduler) {
    std::latch started{1}, release{1};
    task_graph_t graph{1};
    REQUIRE(graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token) -> task_outcome_t {
        started.count_down();
        release.wait();
        return task_value_t{};
    }));
    auto submitted = scheduler.begin(std::move(graph));
    REQUIRE(submitted);
    started.wait();
    bool applied = true;
    for (unsigned i = 0; i < 10; ++i) applied &= change_title(history, std::to_string(i)).applied;
    release.count_down();
    REQUIRE(applied);
}
auto checkpoint_overflow(native_project_autosave_loop_t& loop, const document_history_t& history,
    const native_project_autosave_target_t& target) -> std::filesystem::path {
    REQUIRE(loop.flush_journal(history.document));
    check_checkpoint(target.backup_path, "9", 10);
    const auto backup = parse_native_project_recovery_snapshot(read_bytes(target.backup_path));
    REQUIRE(backup.value);
    REQUIRE(backup.value->metadata.journal_position);
    return backup.value->metadata.journal_position->path;
}
void check_overflow_seed(const std::filesystem::path& path) {
    const auto lines = journal_lines(read_bytes(path));
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().at("record").at("kind") == "seed");
    const auto recovered = command_journal_t{path}.read();
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value.snapshot);
    CHECK(recovered.value.snapshot->song.metadata.title == "After checkpoint");
}
}

TEST_CASE("Queue overflow checkpoints the skipped publications and the next edit starts a seed", "[core][recovery][autosave][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto history = document_history_t::from(journal_song(1));
    task_scheduler_t scheduler{1, 4};
    native_project_autosave_loop_t loop{native_project_autosave_loop_t::clock_t::now(), scheduler};
    const auto target = native_project_autosave_target_t::from(history.document.id, false, {}, directory->path());
    loop.observe(history, target);
    run_overflow_edits(history, scheduler);
    const auto path = checkpoint_overflow(loop, history, target);
    REQUIRE(change_title(history, "After checkpoint").applied);
    REQUIRE(loop.flush_journal(history.document));
    check_overflow_seed(path);
}

TEST_CASE("Idempotent journal records still validate their wrapper shape", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "duplicate-wrapper.journal";
    append_entry(command_journal_t{path}, delta_entry(1));
    const auto bytes = read_bytes(path);
    auto duplicate = journal_lines(bytes).front();
    duplicate["unknown"] = true;
    const auto read = command_journal_read_t::from(bytes + duplicate.dump() + '\n');
    CHECK_FALSE(read.ok());
    check_recovered(read.value, 2, 1);
}

TEST_CASE("An older-schema seed is never reused as an unmigrated delta base", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "old-schema.journal";
    command_journal_t journal{path};
    auto first = delta_entry(1);
    first.snapshot.song.schema_version = current_document_schema_version - 1;
    append_entry(journal, first);
    append_entry(journal, first);
    append_entry(journal, delta_entry(2));
    const auto lines = journal_lines(read_bytes(path));
    REQUIRE(lines.size() == 2);
    CHECK(lines.back().at("record").at("kind") == "seed");
    check_valid_recovery(journal.read(), 3, 2);
}

TEST_CASE("Version one journals remain discoverable and recoverable through the scanner", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto primary = directory->path() / "project.wormedit";
    REQUIRE(create_native_project_file(primary, journal_song(1)).value);
    const auto path = std::filesystem::path{primary.string() + ".bak.journal-v1"};
    append_entry(command_journal_t{path}, journal_entry(1, 1, 2));
    auto line = journal_lines(read_bytes(path)).front();
    line["record"].erase("kind");
    line["record"].erase("payload_checksum");
    line["sha256"] = native_project_content_checksum(line.at("record").dump());
    { std::ofstream output(path, std::ios::binary); output << command_journal_v1_header << line.dump() << '\n'; }
    const auto scan = checked_scan(primary);
    const auto index = *scan.value.preferred_recovery_candidate;
    CHECK(scan.value.candidates[index].is_command_journal);
    const auto inspected = perform_native_project_recovery_action(scan.value.candidates, index,
        native_project_recovery_action_t::inspect_read_only);
    REQUIRE(inspected.value.document);
    CHECK(inspected.value.document->revision == 2);
}

TEST_CASE("Journal replay validates the final delta state before publishing a snapshot", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "final-validation.journal";
    command_journal_t journal{path};
    for (std::uint64_t i = 1; i <= 3; ++i) append_entry(journal, delta_entry(i));
    auto lines = journal_lines(read_bytes(path));
    auto patch = nlohmann::json::parse(lines[1]["record"]["payload"].get<std::string>());
    patch["document"]["fields"]["charts"] = {{"key", "id"}, {"update", {{{"id", 702U},
        {"patch", {{"fields", {{"source_family", {{"set", 9999U}}}}}}}}}}};
    lines[1]["record"]["payload"] = patch.dump();
    lines[1]["sha256"] = native_project_content_checksum(lines[1]["record"].dump());
    lines[2]["record"]["base_checksum"] = lines[1]["sha256"];
    patch = nlohmann::json::parse(lines[2]["record"]["payload"].get<std::string>());
    patch["document"]["fields"]["charts"] = {{"key", "id"}, {"update", {{{"id", 702U},
        {"patch", {{"fields", {{"source_family", {{"set", static_cast<unsigned int>(format_family_t::native_project)}}}}}}}}}}};
    lines[2]["record"]["payload"] = patch.dump();
    const auto recovered = command_journal_read_t::from(encode_lines(lines));
    REQUIRE(recovered.ok());
    check_recovered(recovered.value, 4, 3);
    REQUIRE(recovered.value.snapshot);
    CHECK(recovered.value.snapshot->song.metadata.title == "3");
}

TEST_CASE("Reopened migrated journal seeds reseed before accepting another publication", "[core][recovery][command-journal][delta]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "reopened-old-seed.journal";
    append_entry(command_journal_t{path}, delta_entry(1));
    auto lines = journal_lines(read_bytes(path));
    auto payload = nlohmann::json::parse(lines.front()["record"]["payload"].get<std::string>());
    payload["document"]["schema_version"] = current_document_schema_version - 1;
    lines.front()["record"]["payload"] = payload.dump();
    lines.front()["record"]["payload_checksum"] = native_project_content_checksum(payload.dump());
    { std::ofstream output(path, std::ios::binary); output << encode_lines(lines); }
    command_journal_t reopened{path};
    append_entry(reopened, delta_entry(2));
    check_latest_seed(reopened, path, 2);
}
