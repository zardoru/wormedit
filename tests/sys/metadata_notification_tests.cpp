#include "sys/metadata_actions.h"
#include "ui/frontend.h"
#include <wormedit/core/chart_editor.h>
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct metadata_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    document_history_t history;
    workspace_controller_t workspace;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t assets;

    metadata_notification_fixture_t() {
        history = make_document_history(song_document_t{.id = song_id_t{1}, .revision = 4});
        REQUIRE(apply_new_chart_command(history, 4));
        history = make_document_history(std::move(history.document));
        const auto binding = make_workspace_controller(history);
        REQUIRE(binding);
        workspace = *binding.workspace;
    }

    auto dispatch(const WormUiWorkspaceAction& action) -> std::optional<std::string> {
        return dispatch_metadata_action(action, history, workspace, false, drop, assets,
                                        {}, scheduler, false, false, sink);
    }

    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
};

auto song_edit(const metadata_notification_fixture_t& fixture) -> WormUiWorkspaceAction {
    WormUiWorkspaceAction action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_SONG;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_title = "New title";
    action.metadata_artist = "New artist";
    return action;
}

void check_metadata_notification(bool value) { CHECK(value); }
void require_metadata_notification(bool value) { REQUIRE(value); }

void expect_edit_entry(const notification_entry_t& entry, notification_severity_t severity,
                       std::string_view message) {
    CHECK(entry.severity == severity);
    CHECK(entry.source == notification_source_t::edit);
    CHECK(entry.message == message);
}
} // namespace

TEST_CASE("metadata edit notifications preserve success and no-op outcomes", "[unit][sys][metadata-notification]") {
    metadata_notification_fixture_t fixture;
    auto action = song_edit(fixture);
    require_metadata_notification(fixture.dispatch(action) == "Metadata updated.");
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::success, "Metadata updated.");
    const auto revision = fixture.history.document.revision;
    action.metadata_document_revision = revision;
    require_metadata_notification(fixture.dispatch(action) == "No metadata changes.");
    entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::info, "No metadata changes.");
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.size() == 1);
}

TEST_CASE("metadata malformed and stale edits post once without mutation", "[unit][sys][metadata-notification]") {
    metadata_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto action = song_edit(fixture);
    action.metadata_title = nullptr;
    const auto invalid = fixture.dispatch(action);
    require_metadata_notification(static_cast<bool>(invalid));
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::warning, *invalid);
    action = song_edit(fixture);
    action.metadata_document_revision = revision - 1;
    const auto stale = fixture.dispatch(action);
    require_metadata_notification(static_cast<bool>(stale));
    entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::error, *stale);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
    check_metadata_notification(!fixture.dispatch(WormUiWorkspaceAction{}));
    check_metadata_notification(fixture.entries().size() == 2);
}
