#include "../support/stale_action_assertions.h"
#include <catch2/generators/catch_generators.hpp>
#include "sys/metadata_asset_runtime.h"
#include "sys/resource_job_state.h"
#include "ui/frontend.h"

#include <wormedit/core/resource_file_copy.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct resource_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{scheduler, sink};
    document_history_t history;
    workspace_controller_t workspace;
    std::filesystem::path root = unique_directory();
    std::filesystem::path source = root / "source.wav";
    std::filesystem::path project = root / "project";
    runtime::metadata_asset_t assets{sink};

    static auto unique_directory() -> std::filesystem::path {
        static std::atomic<std::uint64_t> sequence{};
        return std::filesystem::temp_directory_path() /
            ("wormedit-resource-notification-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence.fetch_add(1)));
    }

    resource_notification_fixture_t() {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(project);
        std::ofstream{source} << "test audio bytes";
        history.document.id = song_id_t{1};
        history.document.charts.push_back({.id = chart_id_t{1}});
        workspace.active_tab = workspace_tab_t::metadata;
        workspace.selected_chart = chart_id_t{1};
    }
    ~resource_notification_fixture_t() { std::filesystem::remove_all(root); }

    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
    auto inspect_action() const -> WormUiWorkspaceAction {
        WormUiWorkspaceAction action{};
        action.compose_input_chart_id = 1;
        action.compose_input_document_revision = history.document.revision;
        action.compose_resource_asset_id = 1;
        action.metadata_inspect_import = true;
        std::snprintf(action.compose_import_path, sizeof(action.compose_import_path), "%s",
                      source.c_str());
        return action;
    }
    auto wait_for_import() -> std::optional<std::string> {
        std::optional<std::string> completed;
        for (int attempt = 0; attempt < 1000 && !completed; ++attempt) {
            completed = assets.poll(history, workspace, project);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return completed;
    }
    void wait_for_preview() {
        for (int attempt = 0; attempt < 200 && assets.copy_preview_pending(); ++attempt) {
            (void)assets.poll(history, workspace, project);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        (void)assets.poll(history, workspace, project);
    }
};

void expect_notification(const notification_entry_t& entry, notification_severity_t severity,
                         notification_source_t source, std::string_view message) {
    CHECK(entry.severity == severity);
    CHECK(entry.source == source);
    CHECK(entry.message == message);
}

void check_resource_notification(bool value) { CHECK(value); }
void require_resource_notification(bool value) { REQUIRE(value); }

auto wait_for_resource_result(resource_notification_fixture_t& fixture)
    -> std::optional<std::string> {
    std::optional<std::string> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = fixture.resource_copy.poll(fixture.history, fixture.workspace);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return result;
}

auto wait_for_export_result(runtime::output_group_export_t& exports,
                            const document_history_t& history) -> std::optional<std::string> {
    std::optional<std::string> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = exports.poll(history);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return result;
}

auto failed_copy_worker(const resource_copy_request_t&, std::stop_token)
    -> resource_copy_job_t::work_outcome_t {
    return std::unexpected(wormedit::error_t<task_error_code_t>{task_error_code_t::task_failed,
                                                                "Injected copy worker failure."});
}
auto cancelled_copy_worker(const resource_copy_request_t&, std::stop_token)
    -> resource_copy_job_t::work_outcome_t {
    return resource_copy_job_t::work_outcome_t{
        std::in_place, std::unexpected(wormedit::error_t<project_asset_copy_status_t>{
                           project_asset_copy_status_t::cancelled, {}})};
}

auto make_export_task(runtime::output_group_export_t& runtime,
                      output_group_export_task_result_t result) -> pending_output_group_export_t {
    task_graph_t graph{1};
    const auto value = std::make_shared<const output_group_export_task_result_t>(std::move(result));
    const auto added = graph.create_task({}, [value](std::span<const task_result_t>, std::stop_token) {
        return task_result_t::success(task_value_t::from(value));
    });
    REQUIRE(added);
    auto submitted = runtime.scheduler.begin(std::move(graph));
    REQUIRE(submitted);
    return {.graph = std::move(submitted.value()), .task = *added,
            .song = song_id_t{1}, .revision = 0};
}
} // namespace

TEST_CASE("metadata copy inspection posts start and completion once", "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    REQUIRE(fixture.assets.start_copy_preview(action, fixture.history, fixture.workspace,
                                              fixture.project) ==
            "Copy destination inspection started.");
    auto entries = fixture.entries();
    REQUIRE(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info,
                        notification_source_t::import_, "Copy destination inspection started.");

    fixture.wait_for_preview();
    entries = fixture.entries();
    REQUIRE(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::info,
                        notification_source_t::import_,
                        "Destination is free; copy will create it.");
    (void)fixture.assets.poll(fixture.history, fixture.workspace, fixture.project);
    CHECK(fixture.entries().size() == 2);
}

TEST_CASE("metadata inspection retains advisory feedback after invalidation and cancel",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    require_resource_notification(fixture.assets.start_copy_preview(
                                      action, fixture.history, fixture.workspace,
                                      fixture.project) == "Copy destination inspection started.");
    fixture.assets.clear_copy_preview_key();
    ++fixture.history.document.revision;
    fixture.assets.cancel();
    fixture.wait_for_preview();
    check_resource_notification(!fixture.assets.copy_preview_pending());
    check_resource_notification(!fixture.assets.copy_preview_key());
    const auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    check_resource_notification(entries[0].source == notification_source_t::import_);
    check_resource_notification(entries[0].severity == notification_severity_t::info);
    check_resource_notification(!entries[0].message.empty());
    (void)fixture.assets.poll(fixture.history, fixture.workspace, fixture.project);
    check_resource_notification(fixture.entries().size() == 2);
}

TEST_CASE("metadata copy inspection rejection is explicit and does not mutate document",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.compose_input_document_revision++;
    const auto revision = fixture.history.document.revision;
    const auto message = fixture.assets.start_copy_preview(action, fixture.history,
                                                           fixture.workspace, fixture.project);
    test::check_stale_action_message(message, "Metadata copy inspection");
    const auto entries = fixture.entries();
    REQUIRE(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::warning,
                        notification_source_t::import_, message);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("metadata import completion rejects its captured project and revision",
          "[unit][sys][resource-notification][metadata-completion-stamp]") {
    const auto replace_project = GENERATE(false, true);
    resource_notification_fixture_t fixture;
    fixture.source = fixture.root / "source.ogg";
    std::filesystem::copy_file(std::filesystem::path{__FILE__}.parent_path().parent_path() / "fixtures" / "audio" / "keysound-c4.ogg",
                               fixture.source);
    auto action = fixture.inspect_action();
    action.metadata_import_copy_project = true;
    (void)fixture.assets.start_import_copy(action, fixture.history, fixture.workspace,
        fixture.project, fixture.scheduler, false, false);
    require_resource_notification(fixture.assets.import_pending());
    if (replace_project) fixture.history.document.id = song_id_t{2};
    else ++fixture.history.document.revision;
    const auto message = fixture.wait_for_import();
    require_resource_notification(message.has_value());
    test::check_stale_action_message(*message, "Metadata audio copy linking");
    check_resource_notification(!std::filesystem::exists(fixture.project / "assets" / "1-source.ogg"));
    check_resource_notification(std::filesystem::exists(fixture.source));
    check_resource_notification(fixture.history.document.audio_assets.empty());
    check_resource_notification(fixture.history.undo.empty());
}

TEST_CASE("metadata import-copy rejects stale start before inspecting audio",
          "[unit][sys][resource-notification][metadata-copy-stamp]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.metadata_import_copy_project = true;
    ++action.compose_input_document_revision;
    const auto message = fixture.assets.start_import_copy(action, fixture.history, fixture.workspace,
        fixture.project, fixture.scheduler, false, false);
    test::check_stale_action_message(message, "Metadata import-copy");
    check_resource_notification(!fixture.assets.import_pending());
    check_resource_notification(fixture.history.undo.empty());
}
#endif

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("metadata import decode failure posts once and never links invalid audio",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.metadata_import_copy_project = true;
    const auto revision = fixture.history.document.revision;
    const auto started = fixture.assets.start_import_copy(action, fixture.history, fixture.workspace,
        fixture.project, fixture.scheduler, false, false);
    require_resource_notification(fixture.assets.import_pending());
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::import_, started);
    const auto completed = fixture.wait_for_import();
    require_resource_notification(static_cast<bool>(completed));
    entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::error, notification_source_t::import_, *completed);
    check_resource_notification(!fixture.assets.import_pending());
    check_resource_notification(!fixture.assets.poll(fixture.history, fixture.workspace, fixture.project));
    check_resource_notification(fixture.entries().size() == 2);
    check_resource_notification(fixture.history.document.audio_assets.empty());
    check_resource_notification(fixture.history.document.revision == revision);
    check_resource_notification(fixture.history.undo.empty());
}
#endif

TEST_CASE("resource copy worker failure posts once and leaves the document unchanged",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t::from(fixture.history.document),
                    .source_path = fixture.source,
                    .export_copy = false}},
        failed_copy_worker)));
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::import_,
                        "Resource copy started in the background.");

    const auto completed = wait_for_resource_result(fixture);
    require_resource_notification(completed == "Injected copy worker failure.");
    entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::error,
                        notification_source_t::import_, *completed);
    check_resource_notification(!fixture.resource_copy.poll(fixture.history, fixture.workspace));
    check_resource_notification(fixture.entries().size() == 2);
    check_resource_notification(fixture.history.document.revision == revision);
    check_resource_notification(fixture.history.undo.empty());
}

TEST_CASE("resource copy stale completion warns once without changing document",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t{.project = fixture.history.document.id,
                                                 .revision = revision - 1},
                    .source_path = fixture.source,
                    .export_copy = false},
         .project_directory = fixture.project})));
    const auto completed = wait_for_resource_result(fixture);
    require_resource_notification(completed.has_value());
    test::check_stale_action_message(*completed, "Resource copy relink");
    const auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::warning,
                        notification_source_t::import_, *completed);
    check_resource_notification(fixture.history.document.revision == revision);
    check_resource_notification(fixture.history.document.audio_assets.empty());
    check_resource_notification(fixture.history.undo.empty());
}

TEST_CASE("output export publishes typed success and terminal failure once",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    runtime::output_group_export_t exports{fixture.sink};
    auto pending = make_export_task(exports, {.published = true,
        .message = "Output group published from the selected revision.",
        .severity = notification_severity_t::success});
    require_resource_notification(exports.start(std::move(pending)));
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info,
                        notification_source_t::export_,
                        "Output group conversion started in the background.");
    const auto completed = wait_for_export_result(exports, fixture.history);
    require_resource_notification(completed == "Output group published from the selected revision.");
    entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::success,
                        notification_source_t::export_, *completed);
    check_resource_notification(!exports.poll(fixture.history));
    check_resource_notification(fixture.entries().size() == 2);

    pending = make_export_task(exports, {.message = "Injected publication failure.",
        .severity = notification_severity_t::error});
    require_resource_notification(exports.start(std::move(pending)));
    (void)wait_for_export_result(exports, fixture.history);
    entries = fixture.entries();
    require_resource_notification(entries.size() == 4);
    expect_notification(entries[0], notification_severity_t::error,
                        notification_source_t::export_, "Injected publication failure.");
}

TEST_CASE("resource copy links a real successful copy and canceled fallback remains visible", "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    fixture.workspace.song = fixture.history.document.id;
    fixture.history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = fixture.source});
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t::from(fixture.history.document),
                    .source_path = fixture.source},
         .project_directory = fixture.project})));
    const auto completed = wait_for_resource_result(fixture);
    require_resource_notification(completed == "Resource copied into the project and linked.");
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::success, notification_source_t::import_, *completed);
    check_resource_notification(fixture.history.document.audio_assets[0].source_path ==
                                "assets/1-source.wav");
    check_resource_notification(fixture.history.undo.size() == 1);
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t::from(fixture.history.document)}},
        cancelled_copy_worker)));
    require_resource_notification(wait_for_resource_result(fixture) == "Resource copy failed.");
    entries = fixture.entries();
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::import_, "Resource copy failed.");
    check_resource_notification(fixture.history.undo.size() == 1);
}
