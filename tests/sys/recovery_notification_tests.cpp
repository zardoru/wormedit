#include "sys/recovery_notification_feedback.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
using namespace wormedit;
using namespace wormedit::sys;
namespace {
void expect_recovery(bool value) { REQUIRE(value); }
struct recovery_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 16};
    native_project_autosave_loop_t autosave{native_project_autosave_loop_t::clock_t::now(), scheduler};
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("wormedit-recovery-notification-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    document_history_t history = document_history_t::from({.schema_version = 2, .id = song_id_t{417}, .revision = 5});
    workspace_controller_t workspace;
    native_project_recovery_controller_t controller;
    bool recovered{};
    recovery_fixture_t() {
        std::filesystem::create_directories(directory);
        const auto binding = workspace_controller_t::from(history);
        expect_recovery(static_cast<bool>(binding));
        workspace = *binding.workspace;
        const auto primary = directory / "chart.wormedit";
        expect_recovery(publish_native_project_atomic(primary, history.document).published);
        auto backup = history.document;
        backup.revision = 6;
        expect_recovery(publish_native_project_recovery_atomic(native_project_adjacent_backup_path(primary),
            backup, {.session_sequence = 2}).published);
        (void)controller.replace_scan(scan_native_project_recovery_candidates(
            {.primary_path = primary, .discover_stale_temporaries = false}));
    }
    ~recovery_fixture_t() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    auto action(WormUiWorkspaceActionKind kind) -> WormUiWorkspaceAction {
        const auto target = controller.view().rows.at(1).target;
        WormUiWorkspaceAction value{};
        value.kind = kind;
        value.recovery_scan_generation = target.scan_generation;
        value.recovery_row_index = target.row_index;
        value.recovery_candidate_index = target.candidate_index;
        return value;
    }
    auto dispatch(const WormUiWorkspaceAction& action) -> std::string {
        return dispatch_recovery_ui_action(action, controller, history, workspace,
            native_project_document_version_t{song_id_t{417}, 5}, autosave, recovered, sink);
    }
    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
};
}
TEST_CASE("Recovery inspection is read-only info and loading is explicit success", "[unit][sys][recovery-notification]") {
    recovery_fixture_t fixture;
    expect_recovery(fixture.dispatch(fixture.action(WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT)) ==
        "Inspected recovery candidate read-only:  (revision 6).");
    expect_recovery(fixture.history.document.revision == 5);
    auto entries = fixture.entries();
    expect_recovery(entries.size() == 1);
    expect_recovery(entries[0].severity == notification_severity_t::info);
    expect_recovery(entries[0].source == notification_source_t::recovery);
    expect_recovery(fixture.dispatch(fixture.action(WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER)) ==
        "Recovery loaded into the current session. Review it and save explicitly.");
    expect_recovery(fixture.history.document.revision == 6);
    expect_recovery(fixture.recovered);
    entries = fixture.entries();
    expect_recovery(entries.size() == 2);
    expect_recovery(entries[0].severity == notification_severity_t::success);
}
TEST_CASE("Recovery validation warns and stale controller diagnostics retain actual severity", "[unit][sys][recovery-notification]") {
    recovery_fixture_t fixture;
    expect_recovery(fixture.dispatch(fixture.action(WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS)) ==
        "Choose a new project path before using Save as.");
    auto entries = fixture.entries();
    expect_recovery(entries.size() == 1);
    expect_recovery(entries[0].severity == notification_severity_t::warning);
    auto stale = fixture.action(WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT);
    stale.recovery_scan_generation -= 1;
    expect_recovery(fixture.dispatch(stale) == "recovery action belongs to a stale scan generation");
    entries = fixture.entries();
    expect_recovery(entries.size() == 2);
    expect_recovery(entries[0].severity == notification_severity_t::error);
    expect_recovery(entries[0].source == notification_source_t::recovery);
    expect_recovery(fixture.history.document.revision == 5);
    expect_recovery(!fixture.recovered);
}
TEST_CASE("Recovery scan diagnostics post once per actual generation", "[unit][sys][recovery-notification]") {
    recovery_fixture_t fixture;
    const auto broken = fixture.directory / "broken.wormedit";
    { std::ofstream file(broken); file << "broken recovery bytes"; }
    auto scan = [&] { return scan_native_project_recovery_candidates(
        {.primary_path = broken, .discover_stale_temporaries = false}); };
    (void)fixture.controller.replace_scan(scan());
    expect_recovery(!fixture.controller.view().diagnostics.empty());
    const auto diagnostic = fixture.controller.view().diagnostics.front();
    recovery_scan_notification_publisher_t publisher;
    publisher.publish(fixture.controller.view(), fixture.sink);
    const auto first = fixture.entries();
    expect_recovery(!first.empty());
    expect_recovery(first[0].source == notification_source_t::recovery);
    expect_recovery(first.back().message == diagnostic.message + " (" + diagnostic.path.generic_string() + ")");
    const auto posted = fixture.center.statistics().posted;
    publisher.publish(fixture.controller.view(), fixture.sink);
    expect_recovery(fixture.entries().size() == first.size());
    expect_recovery(fixture.center.statistics().posted == posted);
    (void)fixture.controller.replace_scan(scan());
    publisher.publish(fixture.controller.view(), fixture.sink);
    (void)fixture.entries();
    expect_recovery(fixture.center.statistics().posted == posted * 2);
    expect_recovery(fixture.center.snapshot(notification_capacity).front().count == 2);
}
