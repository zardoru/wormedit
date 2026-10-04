#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/recipe_form.h"
#include <cstdio>

#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/notifications.h>
#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <source_location>

// Native drop hit testing is unrelated to the action dispatcher in this
// headless target; no rendered metadata pane can accept a file drop.
WormUiMetadataDropTarget worm_ui_frontend_metadata_drop_target(uint32_t, float, float) {
    return {};
}

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void check_action(bool value, std::source_location source = std::source_location::current()) {
    INFO("Compose action check at " << source.line());
    CHECK(value);
}
void require_action(bool value, std::source_location source = std::source_location::current()) {
    INFO("Compose action requirement at " << source.line());
    REQUIRE(value);
}
void check_action_false(bool value, std::source_location source = std::source_location::current()) {
    check_action(!value, source);
}
void require_action_false(bool value, std::source_location source = std::source_location::current()) {
    require_action(!value, source);
}
auto action_notification_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "notification-test", .display_name = "Notification test",
                   .lanes = {{.id = lane_id_t{1}, .name = "Key", .role = lane_role_t::playable}}}}};
    song.charts.push_back(std::move(chart));
    return song;
}

struct action_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    document_history_t history{make_document_history(action_notification_song())};
    workspace_controller_t workspace{*make_workspace_controller(history).workspace};
    editor_interaction_state_t state;
    application_command_state_t application;
    std::filesystem::path asset_base;
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{sink};
    runtime::output_group_export_t export_runtime{sink};

    action_notification_fixture_t() {
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart_id_t{1};
        state.chart = chart_id_t{1};
        state.active_lane = lane_id_t{1};
        state.active_snap_denominator = 4;
        state.supported_snap_denominators = {4};
        state.cursor = {measure_id_t{1}, 0};
    }
    auto action(WormUiComposeActionKind kind) const -> WormUiWorkspaceAction {
        WormUiWorkspaceAction value{};
        value.compose_action = kind;
        value.compose_input_chart_id = state.chart.value;
        value.compose_input_project_id = history.document.id.value;
        value.compose_input_document_revision = history.document.revision;
        return value;
    }
    auto dispatch(const WormUiWorkspaceAction& value) -> std::string {
        return dispatch_compose_view_action(value, history, workspace, state, 800, application,
            asset_base, scheduler, resource_copy, export_runtime, false, sink);
    }
    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{});
        return center.snapshot(256);
    }
};
} // namespace

TEST_CASE("Compose dispatcher posts a semantic edit once and preserves undo", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    require_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
            "Semantic layer added.");
    require_action(fixture.history.document.charts.front().semantic_layers.size() == 1);
    check_action(fixture.history.document.revision != revision);
    check_action(fixture.history.undo.size() == 1);
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
}

TEST_CASE("Compose dispatcher rejects stale actions without changing history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    --action.compose_input_document_revision;
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(action) == "Compose view action ignored because the chart or document revision changed.");
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
}

TEST_CASE("Compose NONE remains quiet even with an absent chart", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.workspace.selected_chart.reset();
    check_action(fixture.dispatch(WormUiWorkspaceAction{}) == "");
    check_action(fixture.entries().empty());
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose area navigation and validation keep typed source and unchanged history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    SECTION("clear is a hint") {
        fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR)) == "Selected area cleared.");
        check_action_false(fixture.state.retained_selection_range.has_value());
        const auto entries = fixture.entries();
        require_action(entries.empty());
        require_action(fixture.center.current_line() != nullptr);
        check_action(fixture.center.current_line()->severity == notification_severity_t::hint);
        check_action(fixture.center.current_line()->source == notification_source_t::input);
    }
    SECTION("invalid geometry is rejected") {
        auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET);
        action.compose_area_begin_row = 960;
        action.compose_area_end_row = 960;
        const auto revision = fixture.history.document.revision;
        check_action_false(fixture.dispatch(action).empty());
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::warning);
        check_action(fixture.history.document.revision == revision);
        check_action_false(fixture.state.retained_selection_range.has_value());
    }
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose BGA no-op remains producer-owned and posts info once", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA)) == "No unused BGA resources.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::info);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose bookmark seek callback owns its feedback", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .anchor = marker_anchor_t::beat, .position = local_position_t{measure_id_t{1}, 480}}};
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.transport_seek_posts_feedback = true;
    fixture.application.transport_seek = [&](std::uint64_t) {
        fixture.sink.post(notification_severity_t::hint, notification_source_t::audio, "Playback cursor moved.");
        return std::string{"Playback cursor moved."};
    };
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK);
    action.compose_bookmark_id = 4;
    check_action(fixture.dispatch(action) == "Playback cursor moved.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().count == 1);
    check_action(entries.front().source == notification_source_t::audio);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose pending gesture rejects an edit without changing history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.pending_hold = pending_hold_t{};
    const auto revision = fixture.history.document.revision;
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
          "Finish or cancel the active Compose gesture before adding a semantic layer.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::warning);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
    check_action(fixture.state.pending_hold.has_value());
}

TEST_CASE("Compose area recipe mutation records one success and one undo", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE)) ==
          "SV recipe added over the selected area.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    check_action(fixture.history.undo.size() == 1);
    require_action(fixture.history.document.charts.front().sv_recipes.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().sv_recipes.empty());
}

TEST_CASE("Compose area quantize preserves actual core rejection severity", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.selected_notes = {object_id_t{999}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE);
    action.compose_quantize_divisions = 4;
    const auto revision = fixture.history.document.revision;
    const auto message = fixture.dispatch(action);
    require_action_false(message.empty());
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().message == message);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose mutation keeps rebind failure visible with document severity", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.workspace.song = song_id_t{99};
    check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER)) ==
          "workspace cannot be rebound to a different song identity");
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::document);
    check_action(entries.back().severity == notification_severity_t::success);
    check_action(entries.back().source == notification_source_t::edit);
    check_action(entries.back().message == "Semantic layer added.");
    check_action(entries.front().count == 1);
    check_action(entries.back().count == 1);
    check_action(fixture.history.document.charts.front().semantic_layers.size() == 1);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().semantic_layers.empty());
}

TEST_CASE("Compose measure navigation posts a hint without editing", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.cursor = {measure_id_t{1}, 480};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE);
    action.compose_measure_number = 1;
    const auto message = fixture.dispatch(action);
    INFO(message);
    check_action(message == "Compose cursor moved to the requested measure.");
    const auto entries = fixture.entries();
    require_action(entries.empty());
    require_action(fixture.center.current_line() != nullptr);
    check_action(fixture.center.current_line()->severity == notification_severity_t::hint);
    check_action(fixture.center.current_line()->source == notification_source_t::input);
    check_action((fixture.state.cursor == local_position_t{measure_id_t{1}, 0}));
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose NONE area-clear request retains its quiet controller behavior", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.state.retained_selection_range = position_span_t{{measure_id_t{1}, 0}, {measure_id_t{1}, 480}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_NONE);
    action.compose_area_clear_requested = true;
    check_action(fixture.dispatch(action).empty());
    check_action_false(fixture.state.retained_selection_range.has_value());
    check_action(fixture.entries().empty());
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose resource dispatcher retains exactly one start and terminal notification", "[compose-action-notifications]") {
    const auto directory = std::filesystem::temp_directory_path() /
        ("wormedit-compose-resource-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup_t {
        std::filesystem::path directory;
        ~cleanup_t() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } cleanup{directory};
    action_notification_fixture_t fixture;
    fixture.asset_base = directory / "project";
    std::filesystem::create_directories(fixture.asset_base);
    const auto source = directory / "source.wav";
    std::ofstream{source} << "actual resource copy bytes";
    fixture.history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = source});
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT);
    action.compose_resource_asset_id = 1;
    require_action(fixture.dispatch(action) == "Resource copy started in the background.");
    std::optional<std::string> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = fixture.resource_copy.poll(fixture.history, fixture.workspace);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    require_action(result.has_value());
    require_action_false(fixture.resource_copy.poll(fixture.history, fixture.workspace).has_value());
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::import_);
    check_action(entries.back().severity == notification_severity_t::info);
    check_action(entries.back().message == "Resource copy started in the background.");
    check_action(entries.front().count == 1);
    check_action(entries.back().count == 1);
    check_action(fixture.history.undo.size() == 1);
    check_action(std::filesystem::is_regular_file(fixture.asset_base / fixture.history.document.audio_assets.front().source_path));
}

TEST_CASE("Compose workspace boundary publishes branch validation without mutation", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t metadata{fixture.sink};
    std::optional<editor_interaction_state_t> interaction{fixture.state};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE);
    auto expected = notification_severity_t::warning;
    SECTION("missing selected control is validation") { action.compose_branch_id = 0; }
    SECTION("core rejection keeps its severity") {
        action.compose_branch_id = 999;
        expected = notification_severity_t::error;
    }
    const auto revision = fixture.history.document.revision;
    check_action(dispatch_compose_workspace_action(action, fixture.history, fixture.workspace,
        interaction, 800, fixture.application, {}, fixture.scheduler, fixture.resource_copy,
        fixture.export_runtime, false, drop, metadata, fixture.sink));
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == expected);
    check_action(entries.front().source == notification_source_t::format);
    check_action(entries.front().count == 1);
    check_action(fixture.history.document.revision == revision);
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose measure helpers publish their actual edit and validation outcomes", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    SECTION("insert mutation") {
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER)) == "Measure inserted.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::success);
        check_action(entries.front().source == notification_source_t::edit);
        check_action(fixture.history.undo.size() == 1);
        check_action(fixture.history.document.charts.front().measures.size() == 2);
        require_action(undo_document_command(fixture.history));
        check_action(fixture.history.document.charts.front().measures.size() == 1);
    }
    SECTION("invalid meter") {
        const auto revision = fixture.history.document.revision;
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER)) ==
            "A meter needs a non-zero numerator and denominator.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::warning);
        check_action(entries.front().source == notification_source_t::edit);
        check_action(fixture.history.document.revision == revision);
        check_action(fixture.history.undo.empty());
    }
}

TEST_CASE("Compose transform helpers retain success and rejection metadata", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    SECTION("shift mutation") {
        fixture.history.document.charts.front().measures.front().notes = {{.id = object_id_t{4},
            .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}}};
        fixture.state.selected_notes = {object_id_t{4}};
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER)) ==
            "Selection shifted by one snap step.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::success);
        check_action(entries.front().source == notification_source_t::edit);
        check_action(fixture.history.document.charts.front().measures.front().notes.front().position.row == 480);
        require_action(undo_document_command(fixture.history));
        check_action(fixture.history.document.charts.front().measures.front().notes.front().position.row == 240);
    }
    SECTION("no selection") {
        check_action(fixture.dispatch(fixture.action(WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR)) ==
            "Select notes before transforming them.");
        const auto entries = fixture.entries();
        require_action(entries.size() == 1);
        check_action(entries.front().severity == notification_severity_t::warning);
        check_action(fixture.history.undo.empty());
    }
}

TEST_CASE("Compose label search navigates with a hint and preserves document history", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().measures.front().notes = {{.id = object_id_t{4},
        .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}, .label = "Chorus"}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL);
    std::snprintf(action.compose_note_label, sizeof(action.compose_note_label), "%s", "Chorus");
    check_action(fixture.dispatch(action) == "Selected 1 note(s) with that label.");
    const auto entries = fixture.entries();
    require_action(entries.empty());
    require_action(fixture.center.current_line() != nullptr);
    check_action(fixture.center.current_line()->severity == notification_severity_t::hint);
    check_action(fixture.center.current_line()->source == notification_source_t::input);
    check_action(fixture.state.cursor.row == 240);
    check_action(fixture.state.selected_notes == std::vector{object_id_t{4}});
    check_action(fixture.history.undo.empty());
}

TEST_CASE("Compose recipe form preview and edit preserve typed helper outcomes", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto& recipes = fixture.history.document.charts.front().sv_recipes;
    recipes.push_back({.id = recipe_id_t{1}, .name = "Recipe",
        .range = {{measure_id_t{1}, 0}, {measure_id_t{1}, 480}}, .output = sv_recipe_output_t::sv,
        .curve = sv_recipe_curve_t::linear,
        .parameters = {.value_from = {1, 1}, .value_to = {2, 1}}});
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM);
    action.compose_recipe_id = 1;
    action.compose_recipe_form = recipe_form_from_recipe(recipes.front());
    auto expected = notification_severity_t::hint;
    auto source = notification_source_t::input;
    SECTION("valid preview") {
        check_action(fixture.dispatch(action) == "SV recipe preview updated.");
        check_action(fixture.history.undo.empty());
    }
    SECTION("invalid syntax") {
        std::snprintf(action.compose_recipe_form.value_to.numerator, sizeof(action.compose_recipe_form.value_to.numerator), "%s", "bad");
        check_action(fixture.dispatch(action) == "Values must be exact fractions and counts must be whole numbers.");
        expected = notification_severity_t::warning;
        source = notification_source_t::edit;
        check_action(fixture.history.undo.empty());
    }
    SECTION("committed form") {
        action.compose_action = WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM;
        std::snprintf(action.compose_recipe_form.value_to.numerator, sizeof(action.compose_recipe_form.value_to.numerator), "%s", "3");
        check_action(fixture.dispatch(action) == "SV recipe updated.");
        expected = notification_severity_t::success;
        source = notification_source_t::edit;
        check_action(fixture.history.undo.size() == 1);
        require_action(undo_document_command(fixture.history));
        check_action((fixture.history.document.charts.front().sv_recipes.front().parameters.value_to == signed_rational_t{2, 1}));
    }
    const auto entries = fixture.entries();
    if (expected == notification_severity_t::hint) {
        require_action(entries.empty());
        require_action(fixture.center.current_line() != nullptr);
        check_action(fixture.center.current_line()->severity == expected);
        check_action(fixture.center.current_line()->source == source);
    } else {
        require_action(entries.size() == 1);
        check_action(entries.front().severity == expected);
        check_action(entries.front().source == source);
    }
}

TEST_CASE("Compose bookmark rename separates no-op from validation without changing legacy flags", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .label = "Intro", .anchor = marker_anchor_t::beat,
        .position = local_position_t{measure_id_t{1}, 240}}};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK);
    action.compose_bookmark_id = 4;
    std::snprintf(action.compose_marker_label, sizeof(action.compose_marker_label), "%s", "Intro");
    auto severity = notification_severity_t::info;
    SECTION("unchanged label") { check_action(fixture.dispatch(action) == "Bookmark label was unchanged."); }
    SECTION("missing bookmark") {
        action.compose_bookmark_id = 999;
        check_action(fixture.dispatch(action) == "That bookmark is no longer available.");
        severity = notification_severity_t::warning;
    }
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == severity);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(fixture.history.undo.empty());
    check_action(fixture.history.document.charts.front().bookmarks.front().label == "Intro");
}

TEST_CASE("Compose bookmark rename mutation precedes its failed rebind notification", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    fixture.history.document.charts.front().bookmarks = {{.id = object_id_t{4},
        .label = "Intro", .anchor = marker_anchor_t::beat,
        .position = local_position_t{measure_id_t{1}, 240}}};
    fixture.workspace.song = song_id_t{99};
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK);
    action.compose_bookmark_id = 4;
    std::snprintf(action.compose_marker_label, sizeof(action.compose_marker_label), "%s", "Outro");
    check_action(fixture.dispatch(action) == "workspace cannot be rebound to a different song identity");
    const auto entries = fixture.entries();
    require_action(entries.size() == 2);
    check_action(entries.front().severity == notification_severity_t::error);
    check_action(entries.front().source == notification_source_t::document);
    check_action(entries.back().message == "Bookmark renamed.");
    check_action(entries.back().severity == notification_severity_t::success);
    check_action(entries.back().source == notification_source_t::edit);
    check_action(entries.front().count == 1);
    check_action(entries.back().count == 1);
    check_action(fixture.history.undo.size() == 1);
    check_action(fixture.history.document.charts.front().bookmarks.front().label == "Outro");
    require_action(undo_document_command(fixture.history));
    check_action(fixture.history.document.charts.front().bookmarks.front().label == "Intro");
}

TEST_CASE("Compose audio offset is a document edit with one success notification", "[compose-action-notifications]") {
    action_notification_fixture_t fixture;
    auto action = fixture.action(WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET);
    action.compose_audio_offset_numerator = 1;
    action.compose_audio_offset_denominator = 4;
    check_action(fixture.dispatch(action) == "Audio offset changed.");
    const auto entries = fixture.entries();
    require_action(entries.size() == 1);
    check_action(entries.front().severity == notification_severity_t::success);
    check_action(entries.front().source == notification_source_t::edit);
    check_action(fixture.history.undo.size() == 1);
    require_action(undo_document_command(fixture.history));
}
