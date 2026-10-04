#include "../ui/nuklear_event_harness.h"
#include "sys/metadata_drop_state.h"
#include "sys/project_import_feedback.h"
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
