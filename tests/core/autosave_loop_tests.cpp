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
    return {.schema_version = 2,
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
    -> std::optional<wormedit::native_project_autosave_outcome_t> {
    for (auto attempt = 0; attempt < 2000; ++attempt) {
        if (auto result = loop.tick(document, true, now, target))
            return result;
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

void expect_autosave_interval(bool value) { CHECK(value); }
void require_autosave_interval(bool value) { REQUIRE(value); }

} // namespace

TEST_CASE("autosave target keeps saved projects adjacent and untitled sessions ID-stable",
          "[unit][core][autosave]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto source = directory.path / "chart.bms";
    const auto recovery = directory.path / "recovery";

    const auto adjacent =
        native_project_autosave_target_t::from(song_id_t{71}, true, primary, recovery, source);
    CHECK(adjacent.backup_path == native_project_adjacent_backup_path(primary));
    CHECK(adjacent.backup_path != primary);
    CHECK(adjacent.intended_project_path == primary);
    CHECK(adjacent.original_source_path == source);

    const auto untitled =
        native_project_autosave_target_t::from(song_id_t{71}, false, primary, recovery, source);
    CHECK(untitled.backup_path == recovery / "project-71.wormedit.bak");
    CHECK(untitled.intended_project_path.empty());

    const auto unavailable = native_project_autosave_target_t::from(song_id_t{71}, false, {}, {}, {});
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
    const auto target = native_project_autosave_target_t::from(document.id, false, {}, recovery);
    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(
        start, scheduler,
        native_project_document_version_t{.project_id = document.id, .revision = 1}, 4);

    CHECK_FALSE(loop.tick(document, true, start + 5min - 1ms, target));
    CHECK_FALSE(loop.tick(document, false, start + 5min, target));
    CHECK_FALSE(loop.is_busy());
    CHECK_FALSE(std::filesystem::exists(target.backup_path));

    CHECK_FALSE(loop.tick(document, true, start + 5min, target));
    const auto completed = wait_for_result(loop, document, start + 5min + 1ms, target);
    REQUIRE(completed.has_value());
    REQUIRE(completed->value.status == native_project_autosave_status_t::published);
    CHECK(completed->value.revision == document.revision);
    CHECK(completed->value.session_sequence == 5);
    CHECK(loop.session_sequence() == 5);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->song.id == document.id);
    CHECK(parsed.value->song.revision == document.revision);
    CHECK(parsed.value->metadata.last_explicit_save_revision == 1);
    CHECK(parsed.value->metadata.session_sequence == 5);
}

TEST_CASE("autosave loop resumes existing sequence and backs off after publication failures",
          "[unit][core][autosave]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(recovery);
    const auto document = song(71, 2);
    const auto target = native_project_autosave_target_t::from(document.id, false, {}, recovery);
    REQUIRE(create_native_project_recovery_file(target.backup_path, song(71, 1),
                                                   {.session_sequence = 8})
                .value);
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
    CHECK(failed->value.status == native_project_autosave_status_t::failed);
    CHECK(failed->value.session_sequence == 8);
    CHECK(read_text(target.backup_path) == previous_good_backup);

    CHECK_FALSE(loop.tick(document, true, start + 5min + 5s, target));
    CHECK_FALSE(loop.is_busy());
    CHECK(read_text(target.backup_path) == previous_good_backup);

    CHECK_FALSE(loop.tick(document, true, start + 5min + 11s, target));
    const auto retried = wait_for_result(loop, document, start + 5min + 12s, target);
    REQUIRE(retried.has_value());
    CHECK(retried->value.status == native_project_autosave_status_t::published);
    CHECK(retried->value.session_sequence == 9);
}

TEST_CASE("autosave loop permits one in-flight snapshot and refuses session replacement",
          "[unit][core][autosave]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(recovery);
    const auto document = song(71, 2);
    const auto target = native_project_autosave_target_t::from(document.id, false, {}, recovery);
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
    CHECK(loop.is_busy());
    CHECK(scheduler.outstanding_tasks() == 1);
    CHECK_FALSE(loop.tick(song(71, 3), true, start + 5min + 1s, target));
    CHECK(scheduler.outstanding_tasks() == 1);
    CHECK_FALSE(
        loop.reset_session(start + 6min, native_project_document_version_t{song_id_t{72}, 1}, 20));

    release.store(true, std::memory_order_release);
    for (auto attempt = 0; attempt < 2000 && scheduler.outstanding_tasks() != 0; ++attempt)
        std::this_thread::sleep_for(1ms);
    REQUIRE(scheduler.outstanding_tasks() == 0);
    CHECK(loop.is_busy()); // Completed, but still owned until the main thread collects it.
    loop.mark_explicit_save({.project_id = document.id, .revision = document.revision});
    CHECK_FALSE(
        loop.reset_session(start + 6min, native_project_document_version_t{song_id_t{72}, 1}, 20));
    const auto completed = loop.tick(document, true, start + 5min + 2s, target);
    REQUIRE(completed.has_value());
    CHECK(completed->value.status == native_project_autosave_status_t::published);
    CHECK(completed->value.revision == 2);
    CHECK_FALSE(loop.is_busy());
    CHECK(scheduler.outstanding_tasks() == 0);

    auto next_revision = song(71, 3);
    CHECK_FALSE(loop.tick(next_revision, true, start + 10min, target));
    const auto next = wait_for_result(loop, next_revision, start + 10min + 1s, target);
    REQUIRE(next.has_value());
    CHECK(next->value.status == native_project_autosave_status_t::published);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->metadata.last_explicit_save_revision == 2);

    const auto recovered = song(72, 5);
    const auto recovered_target =
        native_project_autosave_target_t::from(recovered.id, false, {}, recovery);
    REQUIRE(
        loop.reset_session(start + 11min, native_project_document_version_t{recovered.id, 4}, 20));
    CHECK_FALSE(loop.tick(recovered, true, start + 16min, recovered_target));
    const auto recovered_result =
        wait_for_result(loop, recovered, start + 16min + 1s, recovered_target);
    REQUIRE(recovered_result.has_value());
    CHECK(recovered_result->value.status == native_project_autosave_status_t::published);
    CHECK(recovered_result->value.session_sequence == 21);
    const auto recovered_snapshot =
        parse_native_project_recovery_snapshot(read_text(recovered_target.backup_path));
    REQUIRE(recovered_snapshot.value.has_value());
    CHECK(recovered_snapshot.value->metadata.last_explicit_save_revision == 4);
}

TEST_CASE("recovery sequence resumes only from valid artifacts for the same project",
          "[unit][core][autosave]") {
    using namespace wormedit;
    native_project_recovery_scan_outcome_t scan;
    scan.value.candidates = {
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
    const auto target = native_project_autosave_target_t::from(document.id, false, {}, recovery);
    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(start, scheduler, {}, 0, 5min, 10s);

    native_project_storage_options_t throws_in_worker;
    throws_in_worker.allow_stage = [](auto, const auto&) -> bool {
        throw std::runtime_error("injected worker failure");
    };
    CHECK_FALSE(loop.tick(document, true, start + 5min, target, std::move(throws_in_worker)));
    REQUIRE(loop.is_busy());
    loop.mark_explicit_save({.project_id = document.id, .revision = document.revision});
    const auto failed = wait_for_result(loop, document, start + 5min + 1ms, target);
    REQUIRE(failed.has_value());
    CHECK(failed->value.status == native_project_autosave_status_t::failed);

    auto next_revision = song(71, 3);
    CHECK_FALSE(loop.tick(next_revision, true, start + 5min + 11s, target));
    const auto recovered = wait_for_result(loop, next_revision, start + 5min + 12s, target);
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->value.status == native_project_autosave_status_t::published);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->metadata.last_explicit_save_revision == document.revision);
}

TEST_CASE("autosave interval changes preserve successful cadence and survive pending workers", "[unit][core][autosave]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    auto document = song(71, 2);
    const auto target = native_project_autosave_target_t::from(document.id, false, {}, directory.path);
    const auto start = clock_t::time_point{};
    task_scheduler_t scheduler(1, 1);
    native_project_autosave_loop_t loop(start, scheduler, {}, 4);
    require_autosave_interval(loop.set_interval(1min));
    expect_autosave_interval(!loop.set_interval(0min));
    expect_autosave_interval(!loop.tick(document, true, start + 1min - 1ms, target));
    expect_autosave_interval(!loop.tick(document, true, start + 1min, target));
    require_autosave_interval(loop.is_busy());
    require_autosave_interval(loop.set_interval(10min));
    const auto first = wait_for_result(loop, document, start + 1min, target);
    require_autosave_interval(first.has_value());
    require_autosave_interval(first->value.status == native_project_autosave_status_t::published);
    expect_autosave_interval(loop.session_sequence() == 5);
    ++document.revision;
    expect_autosave_interval(!loop.tick(document, true, start + 6min, target));
    expect_autosave_interval(!loop.is_busy());
    expect_autosave_interval(!loop.tick(document, true, start + 11min, target));
    const auto second = wait_for_result(loop, document, start + 11min, target);
    require_autosave_interval(second.has_value());
    require_autosave_interval(second->value.status == native_project_autosave_status_t::published);
    expect_autosave_interval(loop.session_sequence() == 6);
    require_autosave_interval(loop.set_interval(1min));
    expect_autosave_interval(!loop.tick(document, true, start + 20min, target));
    expect_autosave_interval(!loop.is_busy());
}

namespace {
auto drain_notifications(wormedit::notification_center_t& center)
    -> std::vector<wormedit::notification_entry_t> {
    center.drain_inbox(wormedit::notification_time_t{0});
    return center.snapshot(wormedit::notification_capacity);
}

struct autosave_notification_fixture_t {
    using clock_t = wormedit::native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    wormedit::song_document_t document{song(83, 4)};
    std::filesystem::path recovery{directory.path / "recovery"};
    wormedit::native_project_autosave_target_t target{
        wormedit::native_project_autosave_target_t::from(document.id, false, {}, recovery)};
    clock_t::time_point start{};
    wormedit::notification_center_t center;
    wormedit::task_scheduler_t scheduler{1, 1};
    wormedit::notification_sink_t sink{center};
    wormedit::native_project_autosave_loop_t loop{start, scheduler, {}, 0, 5min, 10s, sink};

    autosave_notification_fixture_t() { std::filesystem::create_directories(recovery); }

    auto fail_replace_options() const -> wormedit::native_project_storage_options_t {
        wormedit::native_project_storage_options_t options;
        options.allow_stage = [](auto stage, const auto&) {
            return stage != wormedit::native_project_storage_stage_t::replace_destination;
        };
        return options;
    }
};

auto first_failure_posts_once(autosave_notification_fixture_t& fixture) -> bool {
    if (fixture.loop.tick(fixture.document, true, fixture.start + 5min, fixture.target,
                          fixture.fail_replace_options()))
        return false;
    const auto failed = wait_for_result(fixture.loop, fixture.document,
                                        fixture.start + 5min + 1ms, fixture.target);
    if (!failed || failed->value.status != wormedit::native_project_autosave_status_t::failed)
        return false;
    const auto entries = drain_notifications(fixture.center);
    if (entries.size() != 1 || entries[0].message != "Autosave failed: " + failed->diagnostics.front().message +
            " (" + failed->diagnostics.front().path.generic_string() + ")" ||
        entries[0].severity != wormedit::notification_severity_t::error ||
        entries[0].source != wormedit::notification_source_t::recovery)
        return false;
    if (fixture.loop.tick(fixture.document, true, fixture.start + 5min + 5s, fixture.target))
        return false;
    return drain_notifications(fixture.center).size() == 1;
}

auto retry_merges_then_success_stays_quiet(autosave_notification_fixture_t& fixture) -> bool {
    if (fixture.loop.tick(fixture.document, true, fixture.start + 5min, fixture.target,
                          fixture.fail_replace_options()))
        return false;
    if (!wait_for_result(fixture.loop, fixture.document, fixture.start + 5min + 1ms,
                         fixture.target))
        return false;
    (void)drain_notifications(fixture.center);
    if (fixture.loop.tick(fixture.document, true, fixture.start + 5min + 11s, fixture.target,
                          fixture.fail_replace_options()))
        return false;
    const auto retried_failure = wait_for_result(fixture.loop, fixture.document,
        fixture.start + 5min + 12s, fixture.target);
    if (!retried_failure || retried_failure->value.status != wormedit::native_project_autosave_status_t::failed)
        return false;
    const auto entries = drain_notifications(fixture.center);
    if (entries.size() != 1 || entries[0].count != 2)
        return false;
    if (fixture.loop.tick(fixture.document, true, fixture.start + 5min + 23s, fixture.target))
        return false;
    const auto recovered = wait_for_result(fixture.loop, fixture.document,
        fixture.start + 5min + 24s, fixture.target);
    if (!recovered || recovered->value.status != wormedit::native_project_autosave_status_t::published)
        return false;
    const auto after_success = drain_notifications(fixture.center);
    return after_success.size() == 1 && after_success[0].count == 2;
}

auto stopped_queue_failure_is_single_and_missing_target_is_quiet() -> bool {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    const auto document = song(84, 2);
    const auto start = clock_t::time_point{};
    notification_center_t center;
    task_scheduler_t scheduler(1, 1);
    scheduler.shutdown();
    notification_sink_t sink{center};
    native_project_autosave_loop_t loop(start, scheduler, {}, 0, 5min, 10s, sink);
    const native_project_autosave_target_t target{.backup_path = "stopped/project.bak"};
    const auto failed = loop.tick(document, true, start + 5min, target);
    if (!failed || failed->value.status != native_project_autosave_status_t::failed)
        return false;
    auto entries = drain_notifications(center);
    if (entries.size() != 1 || entries[0].message != "Autosave failed: " + failed->diagnostics.front().message +
            " (" + failed->diagnostics.front().path.generic_string() + ")" ||
        entries[0].severity != notification_severity_t::error ||
        entries[0].source != notification_source_t::recovery)
        return false;
    if (loop.tick(document, true, start + 5min + 1s, target) ||
        drain_notifications(center).size() != 1)
        return false;
    const native_project_autosave_target_t missing{};
    return !loop.tick(document, true, start + 6min, missing) &&
           drain_notifications(center).size() == 1;
}

auto throwing_notification_clock_does_not_change_failed_result() -> bool {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto document = song(85, 3);
    const auto target = native_project_autosave_target_t::from(document.id, false, {}, directory.path);
    const auto start = clock_t::time_point{};
    notification_center_t center{[]() -> notification_time_t {
        throw std::runtime_error{"injected notification clock failure"};
    }};
    task_scheduler_t scheduler(1, 1);
    notification_sink_t sink{center};
    native_project_autosave_loop_t loop(start, scheduler, {}, 0, 5min, 10s, sink);
    native_project_storage_options_t fail_replace;
    fail_replace.allow_stage = [](auto stage, const auto&) {
        return stage != native_project_storage_stage_t::replace_destination;
    };
    if (loop.tick(document, true, start + 5min, target, std::move(fail_replace)))
        return false;
    const auto failed = wait_for_result(loop, document, start + 5min + 1ms, target);
    if (!failed || failed->value.status != native_project_autosave_status_t::failed)
        return false;
    const auto quiet_poll = loop.tick(document, true, start + 5min + 5s, target);
    return !quiet_poll && !loop.is_busy();
}
} // namespace

TEST_CASE("autosave terminal failure posts once and quiet poll does not repeat it",
          "[unit][core][autosave][notifications]") {
    autosave_notification_fixture_t fixture;
    CHECK(first_failure_posts_once(fixture));
}

TEST_CASE("autosave retry failure merges and successful retry adds no failure", "[unit][core][autosave][notifications]") {
    autosave_notification_fixture_t fixture;
    CHECK(retry_merges_then_success_stays_quiet(fixture));
}

TEST_CASE("autosave queue failure posts once and missing destination stays quiet", "[unit][core][autosave][notifications]") {
    CHECK(stopped_queue_failure_is_single_and_missing_target_is_quiet());
}

TEST_CASE("autosave worker failure remains contained when notification clock throws", "[unit][core][autosave][notifications]") {
    CHECK(throwing_notification_clock_does_not_change_failed_result());
}

TEST_CASE("Recovery snapshots anchor imported assets without changing the live document",
          "[unit][core][autosave][scratchpad-recovery]") {
    using namespace wormedit;
    using clock_t = native_project_autosave_loop_t::clock_t;
    temporary_directory_t directory;
    const auto source = directory.path / "original-chart";
    const auto recovery = directory.path / "recovery";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(recovery);
    std::ofstream(source / "stem.wav") << "fixture";
    auto document = song(71, 2);
    document.schema_version = current_document_schema_version;
    document.charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}}};
    const std::string script = "return {notes={},layers={}}";
    document.generator_recipes.push_back({.id = recipe_id_t{5},
                                          .api_version = "1",
                                          .source_path = "scripts/generate.lua",
                                          .source_hash = native_project_content_checksum(script),
                                          .source = script,
                                          .chart = chart_id_t{2}});
    document.audio_assets = {{.id = asset_id_t{1}, .source_path = "stem.wav",
                              .waveform_cache_path = "stem.waveform"}};
    auto target = native_project_autosave_target_t::from(document.id, false, {}, recovery);
    target.asset_base = source;
    task_scheduler_t scheduler(1, 1);
    const auto start = clock_t::time_point{};
    native_project_autosave_loop_t loop(start, scheduler);
    (void)loop.tick(document, true, start + 5min, target);
    const auto completed = wait_for_result(loop, document, start + 5min + 1ms, target);
    require_autosave_interval(completed.has_value());
    require_autosave_interval(completed->value.status == native_project_autosave_status_t::published);
    const auto parsed = parse_native_project_recovery_snapshot(read_text(target.backup_path));
    require_autosave_interval(parsed.value.has_value());
    const auto& asset = parsed.value->song.audio_assets.front();
    expect_autosave_interval(asset.source_path == source / "stem.wav");
    expect_autosave_interval(std::filesystem::is_regular_file(asset.source_path));
    expect_autosave_interval(asset.waveform_cache_path == source / "stem.waveform");
    expect_autosave_interval(document.audio_assets.front().source_path == "stem.wav");
    expect_autosave_interval(document.audio_assets.front().waveform_cache_path == "stem.waveform");
    expect_autosave_interval(parsed.value->song.generator_recipes == document.generator_recipes);
}
