#include "sys/notification_history_runtime.h"
#include "sys/application_command_dispatch.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

using namespace wormedit;
using namespace wormedit::sys;
using namespace std::chrono_literals;

namespace {
auto history_chart(std::uint64_t id) -> chart_document_t {
    chart_document_t chart;
    chart.id = chart_id_t{id};
    chart.measures.push_back({.id = measure_id_t{id}});
    chart.lane_layouts.push_back({.id = object_id_t{100 + id},
        .position = {measure_id_t{id}, 0}, .layout = {.profile_id = "test",
        .display_name = "Test", .lanes = {{.id = lane_id_t{id}, .name = "Key",
                                          .role = lane_role_t::playable}}}});
    chart.bpms.push_back({.id = object_id_t{200 + id}, .position = {measure_id_t{id}, 0},
                         .beats_per_minute = {120, 1}});
    chart.measures[0].notes.push_back({.id = object_id_t{300 + id},
        .position = {measure_id_t{id}, 17}, .lane = lane_id_t{id}});
    return chart;
}

auto history_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1, .charts = {history_chart(1), history_chart(2)}};
}

struct navigation_fixture_t {
    document_history_t history{make_document_history(history_song())};
    workspace_controller_t workspace;
    std::optional<editor_interaction_state_t> interaction;
    navigation_fixture_t() {
        auto binding = make_workspace_controller(history);
        REQUIRE(binding.workspace);
        workspace = *binding.workspace;
        auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
            {measure_id_t{1}, 0}, std::array<std::uint64_t, 1>{4}, 4);
        REQUIRE(setup.state);
        interaction = std::move(setup.state);
    }
};

auto post_target(notification_center_t& center, notification_target_t target) -> notification_id_t {
    center.post({notification_severity_t::warning, notification_source_t::document,
                 "Target", "Exact details", std::move(target)});
    center.drain_inbox(0ns);
    return center.borrowed_snapshot(notification_capacity).back().id;
}
}

TEST_CASE("history projection borrows newest bounded rows and clamps future times", "[sys][notifications]") {
    notification_center_t center{[] { return 10s; }};
    for (int index = 0; index < 260; ++index)
        center.post(notification_severity_t::info, notification_source_t::system,
                    "Message " + std::to_string(index));
    center.drain_inbox(10s);
    notification_history_runtime_t runtime;
    CHECK(runtime.project(center, history_song(), 20s).row_count == 0);
    runtime.visible = true;
    const auto view = runtime.project(center, history_song(), 20s);
    const auto entries = center.borrowed_snapshot(notification_capacity);
    REQUIRE(view.row_count == notification_capacity);
    CHECK(view.rows[0].message == entries.back().message.c_str());
    CHECK(view.rows[255].id == entries.front().id);
    CHECK(view.rows[0].age_seconds == 10);
    CHECK(runtime.project(center, history_song(), 0ns).rows[0].age_seconds == 0);
}

TEST_CASE("opening history marks existing entries read once and does not discard later unread",
          "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    const auto first = post_target(center, {});
    notification_history_runtime_t runtime;
    REQUIRE(runtime.apply({WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY, 0}, center));
    CHECK(find_notification(center, first)->read);
    center.post(notification_severity_t::error, notification_source_t::system, "Later");
    center.drain_inbox(1s);
    const auto later = center.borrowed_snapshot(notification_capacity).back().id;
    runtime.apply({WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY, 0}, center);
    CHECK_FALSE(find_notification(center, later)->read);
    CHECK(notification_copy_payload(center, first) == "Exact details");
    runtime.apply({WORM_UI_NOTIFICATION_ACTION_DISMISS, first}, center);
    CHECK_FALSE(notification_copy_payload(center, first));
    runtime.apply({WORM_UI_NOTIFICATION_ACTION_DISMISS_ALL, 0}, center);
    CHECK(center.borrowed_snapshot(notification_capacity).empty());
}

TEST_CASE("migrated long UTF8 messages retain full history and clipboard payload",
          "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    const notification_sink_t sink{center};
    const std::string message = std::string(196, 'a') + "é" + std::string(700, 'b');
    SECTION("simple sink") {
        sink.post(notification_severity_t::error, notification_source_t::system, message);
    }
    SECTION("typed sink without explicit detail") {
        sink.post(notification_post_t{.severity = notification_severity_t::error,
            .source = notification_source_t::system, .message = message});
    }
    SECTION("diagnostic sink") {
        sink.post_diagnostics(notification_source_t::system,
            {{.severity = diagnostic_severity_t::error, .message = message}});
    }
    center.drain_inbox(0ns);
    notification_history_runtime_t runtime;
    runtime.visible = true;
    const auto view = runtime.project(center, history_song(), 0ns);
    REQUIRE(view.row_count == 1);
    CHECK(std::string_view{view.rows[0].detail} == message);
    CHECK(notification_copy_payload(center, view.rows[0].id) == message);
    CHECK(std::string_view{view.rows[0].message} == std::string(196, 'a') + "…");
}

TEST_CASE("history targets distinguish exact, missing and valid nonpositional objects",
          "[sys][notifications]") {
    auto song = history_song();
    song.charts[0].bookmarks.push_back({.id = object_id_t{400}, .label = "Audio",
                                      .anchor = marker_anchor_t::audio_time,
                                      .audio_milliseconds = 250});
    const auto note = resolve_notification_target(song, object_id_t{301});
    CHECK(note.availability == WORM_UI_NOTIFICATION_TARGET_POSITION);
    CHECK(note.position == local_position_t{measure_id_t{1}, 17});
    CHECK(resolve_notification_target(song, object_id_t{400}).availability ==
          WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL);
    CHECK(resolve_notification_target(song, object_id_t{999}).availability ==
          WORM_UI_NOTIFICATION_TARGET_MISSING);
    CHECK(resolve_notification_target(song, notification_chart_position_t{
        chart_id_t{1}, {measure_id_t{1}, 960}}).availability == WORM_UI_NOTIFICATION_TARGET_MISSING);
    CHECK(resolve_notification_target(song, {}).availability == WORM_UI_NOTIFICATION_NO_TARGET);
}

TEST_CASE("history Go to switches chart and jumps without snap rounding", "[sys][notifications]") {
    navigation_fixture_t fixture;
    auto& history = fixture.history;
    auto& workspace = fixture.workspace;
    auto& interaction = fixture.interaction;
    notification_center_t center{[] { return 0ns; }};
    const auto id = post_target(center, object_id_t{302});
    REQUIRE_FALSE(go_to_notification(center, id, history, workspace, interaction));
    CHECK(workspace.selected_chart == chart_id_t{2});
    CHECK(workspace.active_tab == workspace_tab_t::compose);
    REQUIRE(interaction);
    CHECK(interaction->cursor == local_position_t{measure_id_t{2}, 17});
    CHECK(history.document.revision == 1);
}

TEST_CASE("history deleted target rejects before changing workspace or editor", "[sys][notifications]") {
    auto history = make_document_history(history_song());
    auto binding = make_workspace_controller(history);
    REQUIRE(binding.workspace);
    auto workspace = *binding.workspace;
    notification_center_t center{[] { return 0ns; }};
    const auto id = post_target(center, object_id_t{302});
    history.document.charts[1].measures[0].notes.clear();
    std::optional<editor_interaction_state_t> interaction;
    const auto previous_chart = workspace.selected_chart;
    const auto previous_tab = workspace.active_tab;
    CHECK(go_to_notification(center, id, history, workspace, interaction) == "Target no longer exists");
    CHECK(workspace.selected_chart == previous_chart);
    CHECK(workspace.active_tab == previous_tab);
    CHECK_FALSE(interaction);
}

TEST_CASE("notification command ignores repeats while closed and unrelated commands", "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    notification_history_runtime_t runtime;
    CHECK_FALSE(invoke_notification_history_command(
        {.kind = application_command_kind_t::history_undo}, false, runtime, center));
    const auto repeated = invoke_notification_history_command(
        {.kind = application_command_kind_t::view_notifications}, true, runtime, center);
    REQUIRE(repeated);
    CHECK(repeated->accepted);
    CHECK_FALSE(runtime.visible);
}

TEST_CASE("notification command marks read only on initial nonrepeated opening", "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    const auto first = post_target(center, {});
    notification_history_runtime_t runtime;
    const application_command_t command{.kind = application_command_kind_t::view_notifications};
    invoke_notification_history_command(command, false, runtime, center);
    CHECK(runtime.visible);
    CHECK(find_notification(center, first)->read);
    center.post(notification_severity_t::info, notification_source_t::system, "Later");
    center.drain_inbox(1s);
    const auto later = center.borrowed_snapshot(notification_capacity).back().id;
    invoke_notification_history_command(command, true, runtime, center);
    CHECK_FALSE(find_notification(center, later)->read);
    invoke_notification_history_command(command, false, runtime, center);
    CHECK_FALSE(find_notification(center, later)->read);
}

TEST_CASE("history availability cache invalidates document revision and identity", "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    post_target(center, object_id_t{301});
    notification_history_runtime_t runtime;
    runtime.visible = true;
    auto document = history_song();
    CHECK(runtime.project(center, document, 0ns).rows[0].target == WORM_UI_NOTIFICATION_TARGET_POSITION);
    document.charts[0].measures[0].notes.clear();
    ++document.revision;
    CHECK(runtime.project(center, document, 0ns).rows[0].target == WORM_UI_NOTIFICATION_TARGET_MISSING);
    auto replacement = history_song();
    replacement.revision = document.revision;
    CHECK(runtime.project(center, replacement, 0ns).rows[0].target == WORM_UI_NOTIFICATION_TARGET_POSITION);
    replacement.id = song_id_t{2};
    replacement.charts[0].measures[0].notes.clear();
    CHECK(runtime.project(center, replacement, 0ns).rows[0].target == WORM_UI_NOTIFICATION_TARGET_MISSING);
}

TEST_CASE("history availability cache follows row identity after eviction and insertion", "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    const auto existing = post_target(center, object_id_t{301});
    notification_history_runtime_t runtime;
    runtime.visible = true;
    const auto document = history_song();
    runtime.project(center, document, 0ns);
    center.dismiss(existing);
    post_target(center, object_id_t{999});
    CHECK(runtime.project(center, document, 0ns).rows[0].target == WORM_UI_NOTIFICATION_TARGET_MISSING);
    center.post({notification_severity_t::info, notification_source_t::system, "Other", {}, object_id_t{302}});
    center.drain_inbox(1s);
    const auto view = runtime.project(center, document, 1s);
    REQUIRE(view.row_count == 2);
    CHECK(view.rows[0].target == WORM_UI_NOTIFICATION_TARGET_POSITION);
    CHECK(view.rows[1].target == WORM_UI_NOTIFICATION_TARGET_MISSING);
}

TEST_CASE("history availability cache invalidates same-id same-revision reloads in place",
          "[sys][notifications]") {
    notification_center_t center{[] { return 0ns; }};
    post_target(center, object_id_t{301});
    notification_history_runtime_t runtime;
    runtime.visible = true;
    auto document = history_song();
    CHECK(runtime.project(center, document, 0ns, 1).rows[0].target == WORM_UI_NOTIFICATION_TARGET_POSITION);
    auto reloaded = history_song();
    reloaded.charts[0].measures[0].notes.clear();
    document = std::move(reloaded);
    CHECK(runtime.project(center, document, 0ns, 2).rows[0].target == WORM_UI_NOTIFICATION_TARGET_MISSING);
}
