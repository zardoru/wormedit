#include <wormedit/core/autosave_loop.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{1};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-autosave-loop-" +
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

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto wait_for_result(wormedit::native_project_autosave_loop_t& loop,
                     const wormedit::song_document_t& document,
                     wormedit::native_project_autosave_loop_t::clock_t::time_point now,
                     const wormedit::native_project_autosave_target_t& target)
    -> std::optional<wormedit::native_project_autosave_result_t> {
    for (auto attempt = 0; attempt < 2000; ++attempt) {
        if (auto result = loop.tick(document, true, now, target))
            return result;
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("autosave target keeps saved projects adjacent and untitled sessions ID-stable",
          "[unit][core][autosave]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto source = directory.path / "chart.bms";
    const auto recovery = directory.path / "recovery";

    const auto adjacent =
        make_native_project_autosave_target(song_id_t{71}, true, primary, recovery, source);
    CHECK(adjacent.backup_path == native_project_adjacent_backup_path(primary));
    CHECK(adjacent.backup_path != primary);
    CHECK(adjacent.intended_project_path == primary);
    CHECK(adjacent.original_source_path == source);

    const auto untitled =
        make_native_project_autosave_target(song_id_t{71}, false, primary, recovery, source);
    CHECK(untitled.backup_path == recovery / "project-71.wormedit.bak");
    CHECK(untitled.intended_project_path.empty());

    const auto unavailable = make_native_project_autosave_target(song_id_t{71}, false, {}, {}, {});
    CHECK(unavailable.backup_path.empty());
}

TEST_CASE(
    "autosave loop honors dirty state and publishes a monotonic new revision off the frame loop",
    "[unit][core][autosave]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(recovery);
    const auto document = song(71, 2);
    const auto target = make_native_project_autosave_target(document.id, false, {}, recovery);
    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(
        start, scheduler,
        native_project_document_version_t{.project_id = document.id, .revision = 1}, 4);

    CHECK_FALSE(loop.tick(document, true, start + 5min - 1ms, target));
    CHECK_FALSE(loop.tick(document, false, start + 5min, target));
    CHECK_FALSE(loop.busy());
    CHECK_FALSE(std::filesystem::exists(target.backup_path));

    CHECK_FALSE(loop.tick(document, true, start + 5min, target));
    const auto completed = wait_for_result(loop, document, start + 5min + 1ms, target);
    REQUIRE(completed.has_value());
    REQUIRE(completed->status == native_project_autosave_status_t::published);
    CHECK(completed->revision == document.revision);
    CHECK(completed->session_sequence == 5);
    CHECK(loop.session_sequence() == 5);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    REQUIRE(parsed.snapshot.has_value());
    CHECK(parsed.snapshot->song.id == document.id);
    CHECK(parsed.snapshot->song.revision == document.revision);
    CHECK(parsed.snapshot->metadata.last_explicit_save_revision == 1);
    CHECK(parsed.snapshot->metadata.session_sequence == 5);
}

TEST_CASE("autosave loop resumes existing sequence and backs off after publication failures",
          "[unit][core][autosave]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(recovery);
    const auto document = song(71, 2);
    const auto target = make_native_project_autosave_target(document.id, false, {}, recovery);
    REQUIRE(publish_native_project_recovery_atomic(target.backup_path, song(71, 1),
                                                   {.session_sequence = 8})
                .published);
    const auto previous_good_backup = read_text(target.backup_path);
    const auto scan =
        scan_native_project_recovery_candidates({.caller_supplied_directories = {recovery}});
    CHECK(native_project_recovery_session_sequence(scan, document.id) == 8);

    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(start, scheduler, {},
                                        native_project_recovery_session_sequence(scan, document.id),
                                        5min, 10s);
    native_project_storage_options_t fail_replace;
    fail_replace.allow_stage = [](auto stage, const auto&) {
        return stage != native_project_storage_stage_t::replace_destination;
    };
    CHECK_FALSE(loop.tick(document, true, start + 5min, target, std::move(fail_replace)));
    const auto failed = wait_for_result(loop, document, start + 5min + 1ms, target);
    REQUIRE(failed.has_value());
    CHECK(failed->status == native_project_autosave_status_t::failed);
    CHECK(failed->session_sequence == 8);
    CHECK(read_text(target.backup_path) == previous_good_backup);

    CHECK_FALSE(loop.tick(document, true, start + 5min + 5s, target));
    CHECK_FALSE(loop.busy());
    CHECK(read_text(target.backup_path) == previous_good_backup);

    CHECK_FALSE(loop.tick(document, true, start + 5min + 11s, target));
    const auto retried = wait_for_result(loop, document, start + 5min + 12s, target);
    REQUIRE(retried.has_value());
    CHECK(retried->status == native_project_autosave_status_t::published);
    CHECK(retried->session_sequence == 9);
}

TEST_CASE("autosave loop permits one in-flight snapshot and refuses session replacement",
          "[unit][core][autosave]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(recovery);
    const auto document = song(71, 2);
    const auto target = make_native_project_autosave_target(document.id, false, {}, recovery);
    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(start, scheduler,
                                        native_project_document_version_t{document.id, 1});

    std::atomic_bool entered{};
    std::atomic_bool release{};
    std::atomic_bool gate_claimed{};
    native_project_storage_options_t gated;
    gated.allow_stage = [&](auto, const auto&) {
        bool expected = false;
        if (gate_claimed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            entered.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(1ms);
        }
        return true;
    };
    CHECK_FALSE(loop.tick(document, true, start + 5min, target, std::move(gated)));
    for (auto attempt = 0; attempt < 2000 && !entered.load(std::memory_order_acquire); ++attempt)
        std::this_thread::sleep_for(1ms);
    REQUIRE(entered.load(std::memory_order_acquire));
    CHECK(loop.busy());
    CHECK(scheduler.outstanding_tasks() == 1);
    CHECK_FALSE(loop.tick(song(71, 3), true, start + 5min + 1s, target));
    CHECK(scheduler.outstanding_tasks() == 1);
    CHECK_FALSE(
        loop.reset_session(start + 6min, native_project_document_version_t{song_id_t{72}, 1}, 20));

    release.store(true, std::memory_order_release);
    for (auto attempt = 0; attempt < 2000 && scheduler.outstanding_tasks() != 0; ++attempt)
        std::this_thread::sleep_for(1ms);
    REQUIRE(scheduler.outstanding_tasks() == 0);
    CHECK(loop.busy()); // Completed, but still owned until the main thread collects it.
    loop.mark_explicit_save({.project_id = document.id, .revision = document.revision});
    CHECK_FALSE(
        loop.reset_session(start + 6min, native_project_document_version_t{song_id_t{72}, 1}, 20));
    const auto completed = loop.tick(document, true, start + 5min + 2s, target);
    REQUIRE(completed.has_value());
    CHECK(completed->status == native_project_autosave_status_t::published);
    CHECK(completed->revision == 2);
    CHECK_FALSE(loop.busy());
    CHECK(scheduler.outstanding_tasks() == 0);

    auto next_revision = song(71, 3);
    CHECK_FALSE(loop.tick(next_revision, true, start + 10min, target));
    const auto next = wait_for_result(loop, next_revision, start + 10min + 1s, target);
    REQUIRE(next.has_value());
    CHECK(next->status == native_project_autosave_status_t::published);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    REQUIRE(parsed.snapshot.has_value());
    CHECK(parsed.snapshot->metadata.last_explicit_save_revision == 2);

    const auto recovered = song(72, 5);
    const auto recovered_target =
        make_native_project_autosave_target(recovered.id, false, {}, recovery);
    REQUIRE(
        loop.reset_session(start + 11min, native_project_document_version_t{recovered.id, 4}, 20));
    CHECK_FALSE(loop.tick(recovered, true, start + 16min, recovered_target));
    const auto recovered_result =
        wait_for_result(loop, recovered, start + 16min + 1s, recovered_target);
    REQUIRE(recovered_result.has_value());
    CHECK(recovered_result->status == native_project_autosave_status_t::published);
    CHECK(recovered_result->session_sequence == 21);
    const auto recovered_snapshot =
        parse_native_project_recovery_snapshot(read_text(recovered_target.backup_path));
    REQUIRE(recovered_snapshot.snapshot.has_value());
    CHECK(recovered_snapshot.snapshot->metadata.last_explicit_save_revision == 4);
}

TEST_CASE("recovery sequence resumes only from valid artifacts for the same project",
          "[unit][core][autosave]") {
    using namespace wormedit;
    native_project_recovery_scan_result_t scan;
    scan.candidates = {
        {.status = native_project_recovery_candidate_status_t::valid,
         .version = native_project_document_version_t{song_id_t{71}, 2},
         .recovery_metadata = native_project_recovery_metadata_t{.session_sequence = 12}},
        {.status = native_project_recovery_candidate_status_t::invalid,
         .version = native_project_document_version_t{song_id_t{71}, 3},
         .recovery_metadata = native_project_recovery_metadata_t{.session_sequence = 99}},
        {.status = native_project_recovery_candidate_status_t::valid,
         .version = native_project_document_version_t{song_id_t{72}, 9},
         .recovery_metadata = native_project_recovery_metadata_t{.session_sequence = 50}},
    };
    CHECK(native_project_recovery_session_sequence(scan, song_id_t{71}) == 12);
    CHECK(native_project_recovery_session_sequence(scan, song_id_t{73}) == 0);
}

TEST_CASE("autosave worker errors retain an explicit-save baseline deferred during execution",
          "[unit][core][autosave][fault]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(recovery);
    const auto document = song(71, 2);
    const auto target = make_native_project_autosave_target(document.id, false, {}, recovery);
    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(start, scheduler, {}, 0, 5min, 10s);

    native_project_storage_options_t throws_in_worker;
    throws_in_worker.allow_stage = [](auto, const auto&) -> bool {
        throw std::runtime_error("injected worker failure");
    };
    CHECK_FALSE(loop.tick(document, true, start + 5min, target, std::move(throws_in_worker)));
    REQUIRE(loop.busy());
    loop.mark_explicit_save({.project_id = document.id, .revision = document.revision});
    const auto failed = wait_for_result(loop, document, start + 5min + 1ms, target);
    REQUIRE(failed.has_value());
    CHECK(failed->status == native_project_autosave_status_t::failed);

    auto next_revision = song(71, 3);
    CHECK_FALSE(loop.tick(next_revision, true, start + 5min + 11s, target));
    const auto recovered = wait_for_result(loop, next_revision, start + 5min + 12s, target);
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->status == native_project_autosave_status_t::published);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    REQUIRE(parsed.snapshot.has_value());
    CHECK(parsed.snapshot->metadata.last_explicit_save_revision == document.revision);
}
