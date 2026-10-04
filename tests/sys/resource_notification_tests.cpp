#include "sys/metadata_asset_runtime.h"
#include "ui/frontend.h"

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

TEST_CASE("metadata copy inspection rejection is explicit and does not mutate document",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.compose_input_document_revision++;
    const auto revision = fixture.history.document.revision;
    const auto message = fixture.assets.start_copy_preview(action, fixture.history,
                                                           fixture.workspace, fixture.project);
    CHECK(message == "Metadata copy inspection expired after the chart or document changed.");
    const auto entries = fixture.entries();
    REQUIRE(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::warning,
                        notification_source_t::import_, message);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}

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
    std::optional<std::string> completed;
    for (int attempt = 0; attempt < 1000 && !completed; ++attempt) {
        completed = fixture.assets.poll(fixture.history, fixture.workspace, fixture.project);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
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
