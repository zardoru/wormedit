#include "../ui/nuklear_event_harness.h"
#include "sys/metadata_drop_state.h"
#include "sys/project_import_feedback.h"
#include "sys/application_startup_feedback.h"
#include "sys/workspace_edit_feedback.h"
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct drop_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    nuklear_event_harness_t frontend;
    metadata_drop_state_t drop;
    document_history_t history;
    workspace_controller_t workspace;

    drop_notification_fixture_t() {
        history.document.id = song_id_t{1};
        history.document.revision = 4;
        workspace.active_tab = workspace_tab_t::metadata;
        workspace.selected_chart = chart_id_t{1};
    }

    void handle(const char* path) {
        SDL_Event event{};
        event.type = SDL_EVENT_DROP_FILE;
        event.drop.data = path;
        drop.handle_file_drop(event, nullptr, workspace, history, {}, sink);
    }

    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
};

void check_drop_notification(bool value) { CHECK(value); }
void require_drop_notification(bool value) { REQUIRE(value); }
} // namespace

TEST_CASE("metadata rejected drop publishes once and clears stale staged paths", "[event-harness][notification][drop]") {
    drop_notification_fixture_t fixture;
    require_drop_notification(fixture.frontend.ready());
    fixture.drop.music_path = "/old.wav";
    fixture.drop.output_path = "old-output";
    fixture.handle("relative.wav");
    const auto entries = fixture.entries();
    require_drop_notification(entries.size() == 1);
    check_drop_notification(entries[0].severity == notification_severity_t::warning);
    check_drop_notification(entries[0].source == notification_source_t::import_);
    check_drop_notification(entries[0].message == "Drop an absolute local file path within the import limit.");
    check_drop_notification(fixture.drop.music_path.empty());
    check_drop_notification(fixture.drop.output_path.empty());
    SDL_Event idle{};
    fixture.drop.handle_file_drop(idle, nullptr, fixture.workspace, fixture.history, {}, fixture.sink);
    check_drop_notification(fixture.entries().size() == 1);
    check_drop_notification(fixture.history.document.revision == 4);
    check_drop_notification(fixture.history.undo.empty());
}

TEST_CASE("metadata accepted file staging and unrelated tabs emit no new notification", "[event-harness][notification][drop]") {
    drop_notification_fixture_t fixture;
    require_drop_notification(fixture.frontend.ready());
    fixture.handle("/tmp/metadata-notification-source.wav");
    check_drop_notification(fixture.drop.music_path == "/tmp/metadata-notification-source.wav");
    check_drop_notification(fixture.entries().empty());
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.handle("relative.wav");
    check_drop_notification(fixture.drop.music_path == "/tmp/metadata-notification-source.wav");
    check_drop_notification(fixture.entries().empty());
    check_drop_notification(fixture.history.document.revision == 4);
}

TEST_CASE("import warning feedback retains every diagnostic in one typed history entry", "[notification][import]") {
    notification_center_t center;
    notification_sink_t sink{center};
    const std::vector<std::string> warnings{"Quantized position from 1/960.", "Missing audio: 曲.wav"};
    sink.post(project_import_warning(warnings));
    center.drain_inbox(notification_time_t{0});
    const auto entries = center.snapshot(notification_capacity);
    require_drop_notification(entries.size() == 1);
    check_drop_notification(entries[0].severity == notification_severity_t::warning);
    check_drop_notification(entries[0].source == notification_source_t::import_);
    check_drop_notification(entries[0].message == "Chart imported with warning: Quantized position from 1/960. (2 resolution warnings total)");
    check_drop_notification(entries[0].detail == "Quantized position from 1/960.\nMissing audio: 曲.wav");
}

TEST_CASE("empty import warnings produce no notification", "[notification][import]") {
    notification_center_t center;
    notification_sink_t sink{center};
    sink.post(project_import_warning({}));
    center.drain_inbox(notification_time_t{0});
    check_drop_notification(center.snapshot(notification_capacity).empty());
}

TEST_CASE("startup context does not repeat a platform recovery failure", "[notification][recovery]") {
    const platform_recovery_directory_result_t unavailable{.path = {}, .error = "Platform recovery failure."};
    const auto empty = initial_application_feedback(false, {}, unavailable);
    check_drop_notification(empty.severity == notification_severity_t::info);
    check_drop_notification(empty.source == notification_source_t::recovery);
    check_drop_notification(empty.message == "No project loaded; recovery discovery unavailable.");
    const auto loaded = initial_application_feedback(true, "/project.wormedit", unavailable);
    check_drop_notification(loaded.severity == notification_severity_t::success);
    check_drop_notification(loaded.source == notification_source_t::document);
    check_drop_notification(loaded.message == "Opened native project: /project.wormedit");
    const auto failed = initial_application_feedback(false, "/project.wormedit", unavailable);
    check_drop_notification(failed.severity == notification_severity_t::error);
    check_drop_notification(failed.source == notification_source_t::document);
    check_drop_notification(failed.message == "Primary project was not loaded; review the completed recovery scan.");
}

TEST_CASE("workspace rebind retries without repeating the same revision failure", "[notification][workspace]") {
    notification_center_t center;
    notification_sink_t sink{center};
    document_history_t history;
    history.document.id = song_id_t{1};
    history.document.revision = 4;
    history.document.charts.push_back({.id = chart_id_t{1}});
    workspace_controller_t workspace;
    workspace.song = history.document.id;
    workspace.limits.maximum_charts = 0;
    workspace_edit_feedback_t feedback;
    feedback.rebind(workspace, history, sink);
    center.drain_inbox(notification_time_t{0});
    require_drop_notification(center.snapshot(notification_capacity).size() == 1);
    feedback.rebind(workspace, history, sink);
    center.drain_inbox(notification_time_t{3000});
    check_drop_notification(center.snapshot(notification_capacity).size() == 1);
    check_drop_notification(center.snapshot(notification_capacity)[0].count == 1);
    workspace.limits.maximum_charts = 1;
    feedback.rebind(workspace, history, sink);
    check_drop_notification(workspace.document_revision == history.document.revision);
    ++history.document.revision;
    workspace.limits.maximum_charts = 0;
    feedback.rebind(workspace, history, sink);
    center.drain_inbox(notification_time_t{6000});
    const auto entries = center.snapshot(notification_capacity);
    require_drop_notification(entries.size() == 1);
    check_drop_notification(entries[0].count == 2);
    check_drop_notification(entries[0].severity == notification_severity_t::error);
    check_drop_notification(entries[0].source == notification_source_t::document);
    check_drop_notification(history.document.revision == 5);
    check_drop_notification(history.undo.empty());
}
