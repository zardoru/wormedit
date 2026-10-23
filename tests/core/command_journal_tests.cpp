#include <wormedit/core/command_journal.h>
#include <wormedit/core/command.h>
#include <wormedit/core/autosave_loop.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include "../support/temporary_directory.h"
#include "../support/timing_links_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <iterator>
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
    INFO((appended ? std::string{"ok"} : appended.error()));
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
void check_foreign_baseline_rejection(const command_journal_read_t& read) {
    auto foreign = journal_song(1);
    foreign.id = song_id_t{999};
    const auto replay = read.replay_on(foreign);
    CHECK_FALSE(replay.ok());
    CHECK(replay.value.id == foreign.id);
    REQUIRE_FALSE(replay.diagnostics.empty());
    CHECK(replay.diagnostics.front().code == diagnostic_code_t::invalid_binding);
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
    check_valid_recovery(read, 3, 1);
    CHECK(read.value.last_sequence == 2);
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
    CHECK(journal.read().value.accepted_records == 1);
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
    const auto started = native_project_autosave_loop_t::clock_t::now();
    native_project_autosave_loop_t loop{started, scheduler, {}, 0, std::chrono::minutes{5},
        std::chrono::seconds{30}, notification_sink_t::muted(), {.maximum_bytes = 512}};
    const auto target = native_project_autosave_target_t::from(history.document.id, false, {}, directory->path());
    loop.observe(history, target);
    (void)change_title(history, "A");
    (void)change_title(history, "B");
    (void)change_title(history, "C");
    loop.flush_journal();
    (void)loop.tick(history.document, true, native_project_autosave_loop_t::clock_t::now(), target);
    loop.flush_journal();
    const auto backup = parse_native_project_recovery_snapshot(read_bytes(target.backup_path));
    REQUIRE(backup.value);
    CHECK(backup.value->song.metadata.title == "C");
    REQUIRE(backup.value->metadata.journal_position);
    CHECK(backup.value->metadata.journal_position->sequence == 3);
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
