#include "sys/transport_player_actions.h"
#include "sys/application_commands.h"
#include "sys/application_command_dispatch.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_semantic_layer_commands.h"

#include <wormedit/core/command_registry.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/semantic_layer_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace wormedit::sys;

#ifndef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Compose navigation cleanup preserves command results", "[unit][sys][commands]") {
    std::string status = "Semantic layer added.";
    // Focus loss can emit cleanup alongside a widget action or on later frames.
    for (int frame = 0; frame < 3; ++frame) {
        status = compose_input_status_with_precedence(status, "Compose navigation canceled.");
        CHECK(status == "Semantic layer added.");
    }
    status = compose_input_status_with_precedence(status, "");
    CHECK(status == "Semantic layer added.");

    status = "Semantic layer was not added.";
    status = compose_input_status_with_precedence(status, "Compose navigation canceled.");
    CHECK(status == "Semantic layer was not added.");

    status = compose_input_status_with_precedence(status, "Compose cursor moved.");
    CHECK(status == "Compose cursor moved.");
    CHECK(compose_input_status_with_precedence("", "Compose navigation canceled.") ==
          "Compose navigation canceled.");
}

TEST_CASE("application command IDs parse into typed commands", "[unit][sys][commands]") {
    CHECK(parse_application_command("workspace.options") ==
          application_command_t{.kind = application_command_kind_t::workspace_options});
    CHECK(parse_application_command("history.undo") ==
          application_command_t{.kind = application_command_kind_t::history_undo});
    CHECK(parse_application_command("workspace.open_compare_validate") ==
          application_command_t{.kind = application_command_kind_t::workspace_open_compare_validate});
    CHECK(parse_application_command("semantic_layer.remove") ==
          application_command_t{.kind = application_command_kind_t::semantic_layer_remove});
    CHECK((parse_application_command("semantic_layer.select_slot_12") ==
           application_command_t{.kind = application_command_kind_t::semantic_layer_select_slot,
                                 .semantic_layer_slot = 12}));
}

TEST_CASE("application command parser rejects unknown IDs and malformed slots",
          "[unit][sys][commands]") {
    CHECK_FALSE(parse_application_command("not.a.command"));
    CHECK_FALSE(parse_application_command("history.undo.extra"));

    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_0"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_-1"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_2x"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_1.0"));

    const auto overflow = std::to_string(std::numeric_limits<std::size_t>::max()) + "0";
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_" + overflow));
}

TEST_CASE("every built-in command ID has a typed representation", "[unit][sys][commands]") {
    const auto registry = wormedit::make_default_editor_command_registry(
        [](const wormedit::command_invocation_context_t&) {
            return wormedit::command_invocation_result_t{};
        });
    for (const auto& command : registry.commands) {
        INFO(command.id.value);
        CHECK(parse_application_command(command.id.value).has_value());
    }
}

#endif

#include "sys/compose_area_actions.h"
#include <algorithm>
#include <array>
#include <wormedit/formats/native_project.h>

namespace {
struct AreaFixture {
    wormedit::document_history_t history;
    wormedit::editor_interaction_state_t state;
    WormUiWorkspaceAction action{};
    AreaFixture() {
        using namespace wormedit;
        chart_document_t chart{
            .id = chart_id_t{7},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}},
            .lane_layouts = {{.id = object_id_t{100},
                              .position = {measure_id_t{1}, 0},
                              .layout = {.profile_id = "area-test",
                                         .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .bpms = {{.id = object_id_t{101},
                      .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}},
                     {.id = object_id_t{102},
                      .position = {measure_id_t{1}, 480},
                      .beats_per_minute = {150, 1}}}};
        chart.measures.front().notes = {
            {.id = object_id_t{1}, .position = {measure_id_t{1}, 121}, .lane = lane_id_t{1}},
            {.id = object_id_t{2}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{1}},
            {.id = object_id_t{3}, .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}}};
        history = make_document_history({.id = song_id_t{1}, .revision = 9, .charts = {chart}});
        constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
        auto setup = make_editor_interaction(history, chart_id_t{7}, lane_id_t{1},
                                             {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup);
        state = *setup.state;
        state.retained_selection_range =
            position_span_t{{measure_id_t{1}, 121}, {measure_id_t{1}, 480}};
        state.retained_canvas_selection_range = canvas_row_span_t{121, 480};
        state.selected_notes = {object_id_t{1}};
        state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}}};
        action.compose_input_chart_id = 7;
        action.compose_input_document_revision = history.document.revision;
        action.compose_area_begin_row = 121;
        action.compose_area_end_row = 620;
    }
    auto dispatch() -> std::string {
        const auto result = dispatch_compose_area_action(action, history, state);
        REQUIRE(result);
        return *result;
    }
};
} // namespace

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
namespace {
struct TimingInsertFixture {
    AreaFixture fixture;
    wormedit::workspace_controller_t workspace;
    std::optional<wormedit::editor_interaction_state_t> compose_state;
    std::optional<wormedit::timing_lane_pseudonote_preview_t> preview;
    application_command_state_t application_state;
    std::string status;
    wormedit::local_position_t cursor{wormedit::measure_id_t{1}, 720};

    TimingInsertFixture() {
        using namespace wormedit;
        auto& chart = fixture.history.document.charts.front();
        chart.scrolls.push_back({.id = object_id_t{103},
                                 .position = {measure_id_t{1}, 600},
                                 .multiplier = {3, 2}});
        const auto moved = dispatch_editor_action(
            fixture.history, fixture.state,
            jump_edit_cursor_action_t{cursor, fixture.history.document.revision});
        REQUIRE(moved.accepted);
        const auto workspace_result = make_workspace_controller(fixture.history);
        REQUIRE(workspace_result);
        workspace = *workspace_result.workspace;
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart.id;
        compose_state = fixture.state;
    }

    auto invoke(application_command_kind_t kind) {
        return invoke_application_command({.kind = kind}, fixture.history, workspace,
                                          compose_state, preview, application_state, status);
    }
};

void check_paint_cell(bool downscroll, bool snap) {
    using namespace wormedit;
    TimingInsertFixture fixture;
    auto& state = *fixture.compose_state;
    state.snap_enabled = snap;
    state.viewport_height_pixels = 600;
    state.viewport_scroll_pixels = 0;
    state.pixels_per_standard_measure = 960;
    const auto& chart = fixture.fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.fixture.history, state);
    const auto& view = std::get<editor_interaction_view_model_t>(inspected);
    const auto built = build_editor_hit_test_index(chart, fixture.fixture.history.document.revision);
    const auto& index = std::get<editor_hit_test_index_t>(built);
    const auto target = resolve_compose_paint_cell(chart, index, view, 50,
        downscroll ? 263 : 337, 42, 48, downscroll, edit_snap_denominator(chart, state));
    REQUIRE(target);
    CHECK(target->position.row == (snap ? 240 : 337));
    state.active_lane = target->lane;
    const auto placed = dispatch_editor_action(fixture.fixture.history, state,
        place_tap_action_t{target->position});
    REQUIRE(placed.document_changed);
    const auto& notes = fixture.fixture.history.document.charts.front().measures.front().notes;
    CHECK(std::ranges::any_of(notes, [&](const auto& note) { return note.position == target->position; }));
}

TEST_CASE("Compose paint ghost cell equals placement with snap and either scroll direction",
          "[sys][application_commands][paint-ghost]") {
    check_paint_cell(false, false);
    check_paint_cell(false, true);
    check_paint_cell(true, false);
    check_paint_cell(true, true);
}

TEST_CASE("Note kind shortcut activates paint and keeps it active when repeated", "[sys][application_commands][palette-repro]") {
    TimingInsertFixture fixture;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::edit_note_kind, .note_kind = 1},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.status);
    REQUIRE(result.accepted);
    CHECK(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::hold);
    CHECK(fixture.application_state.note_paint_mode_active);
    CHECK(invoke_application_command(
        {.kind = application_command_kind_t::edit_note_kind, .note_kind = 1},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.status).accepted);
    CHECK(fixture.application_state.note_paint_mode_active);
}

TEST_CASE("Select shortcut ends note paint without changing the armed kind", "[sys][application_commands]") {
    TimingInsertFixture fixture;
    fixture.application_state.note_paint_mode_active = true;
    fixture.compose_state->armed_note_kind = wormedit::note_kind_t::hold;
    REQUIRE(fixture.invoke(application_command_kind_t::edit_tool_select).accepted);
    CHECK_FALSE(fixture.application_state.note_paint_mode_active);
    CHECK(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::hold);
    fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
    CHECK_FALSE(fixture.invoke(application_command_kind_t::edit_tool_select).accepted);
}

TEST_CASE("Disallowed note kind leaves Select and the armed kind unchanged", "[sys][application_commands]") {
    TimingInsertFixture fixture;
    fixture.fixture.history.document.charts.front().source_family = wormedit::format_family_t::bms;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::edit_note_kind, .note_kind = 2},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.status);
    CHECK_FALSE(result.accepted);
    CHECK_FALSE(fixture.application_state.note_paint_mode_active);
    CHECK(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::tap);
}

auto bpm_preview_matches(const wormedit::timing_lane_pseudonote_preview_t& preview,
                         wormedit::local_position_t cursor) -> bool {
    using namespace wormedit;
    return preview.kind == timing_lane_kind_t::bpm && preview.position == cursor &&
           preview.initial_value_source == timing_lane_initial_value_source_t::effective_at_position &&
           std::get<timing_bpm_value_t>(preview.value).beats_per_minute == signed_rational_t{150, 1};
}

auto sv_preview_matches(const wormedit::timing_lane_pseudonote_preview_t& preview,
                        wormedit::local_position_t cursor) -> bool {
    using namespace wormedit;
    return preview.kind == timing_lane_kind_t::scroll && preview.position == cursor &&
           preview.initial_value_source == timing_lane_initial_value_source_t::effective_at_position &&
           std::get<timing_scroll_value_t>(preview.value).multiplier == signed_rational_t{3, 2};
}

auto history_unchanged(const wormedit::document_history_t& history, std::uint64_t revision) -> bool {
    return history.document.revision == revision && history.undo.empty();
}

auto bpm_stage_ready(const TimingInsertFixture& fixture,
                     const wormedit::command_invocation_result_t& result,
                     std::uint64_t revision) -> bool {
    return result.accepted && fixture.preview && bpm_preview_matches(*fixture.preview, fixture.cursor) &&
           history_unchanged(fixture.fixture.history, revision);
}

auto confirm_bpm_once(TimingInsertFixture& fixture, std::uint64_t revision) -> bool {
    if (!fixture.preview) return false;
    const auto committed = wormedit::confirm_timing_lane_pseudonote(
        fixture.fixture.history, *fixture.preview, false);
    return committed.applied && fixture.fixture.history.document.revision == revision + 1 &&
           fixture.fixture.history.undo.size() == 1;
}

auto sv_stage_ready(const TimingInsertFixture& fixture,
                    const wormedit::command_invocation_result_t& result,
                    std::uint64_t revision) -> bool {
    return result.accepted && fixture.preview && sv_preview_matches(*fixture.preview, fixture.cursor) &&
           history_unchanged(fixture.fixture.history, revision);
}
} // namespace

TEST_CASE("BPM insertion stages effective cursor BPM and commits only on confirmation",
          "[unit][sys][commands][timing]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;

    const auto result = fixture.invoke(application_command_kind_t::timing_insert_bpm);
    CHECK(bpm_stage_ready(fixture, result, revision));
    CHECK(confirm_bpm_once(fixture, revision));
}

TEST_CASE("SV insertion stages the effective cursor scroll multiplier",
          "[unit][sys][commands][timing]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;

    const auto result = fixture.invoke(application_command_kind_t::timing_insert_sv);
    CHECK(sv_stage_ready(fixture, result, revision));
}

TEST_CASE("timing insertion rejects locked edits and missing charts",
          "[unit][sys][commands][timing]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    fixture.application_state.timing_locked = true;
    CHECK_FALSE(fixture.invoke(application_command_kind_t::timing_insert_bpm).accepted);
    CHECK_FALSE(fixture.preview);
    fixture.application_state.timing_locked = false;
    fixture.workspace.selected_chart.reset();
    CHECK_FALSE(fixture.invoke(application_command_kind_t::timing_insert_sv).accepted);
    CHECK_FALSE(fixture.preview);
    CHECK(fixture.fixture.history.document.revision == revision);
}

namespace {
void check_layer_command(bool condition) { CHECK(condition); }
void require_layer_command(bool condition) { REQUIRE(condition); }
void seed_layer_commands(TimingInsertFixture& fixture, bool locked = false) {
    using namespace wormedit;
    auto& chart = fixture.fixture.history.document.charts.front();
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "First", .color = {32, 64, 128, 255}, .locked = locked},
        {.id = layer_id_t{8}, .name = "Second", .color = {64, 128, 32, 255}}};
    chart.measures.front().notes.front().semantic_layers = {layer_id_t{7}};
    fixture.compose_state->selected_notes = {object_id_t{1}, object_id_t{2}};
}
}

TEST_CASE("Application No layer command clears selection and disables other membership operations",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    seed_layer_commands(fixture);
    fixture.application_state.semantic_layer_target_none = true;
    require_layer_command(fixture.invoke(application_command_kind_t::semantic_layer_apply).accepted);
    check_layer_command(fixture.fixture.history.undo.size() == 1);
    check_layer_command(fixture.fixture.history.document.charts.front().measures.front().notes.front().semantic_layers.empty());
    check_layer_command(!fixture.invoke(application_command_kind_t::semantic_layer_remove).accepted);
    check_layer_command(fixture.status == "No layer only assigns");
    check_layer_command(!fixture.invoke(application_command_kind_t::semantic_layer_toggle).accepted);
    check_layer_command(fixture.fixture.history.undo.size() == 1);
    require_layer_command(fixture.invoke(application_command_kind_t::semantic_layer_clear_target).accepted);
    check_layer_command(!fixture.application_state.semantic_layer_target_none);
}

TEST_CASE("Application layer commands replace membership and reject locked sources without edits",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    for (const auto locked : {false, true}) {
        TimingInsertFixture fixture;
        seed_layer_commands(fixture, locked);
        fixture.application_state.semantic_layer_target = layer_id_t{8};
        const auto original = write_native_project(fixture.fixture.history.document).bytes;
        const auto result = fixture.invoke(application_command_kind_t::semantic_layer_apply);
        check_layer_command(result.accepted == !locked);
        if (locked) {
            check_layer_command(fixture.status.find("locked") != std::string::npos);
            check_layer_command(write_native_project(fixture.fixture.history.document).bytes == original);
        } else {
            const auto& notes = fixture.fixture.history.document.charts.front().measures.front().notes;
            check_layer_command(notes[0].semantic_layers == std::vector{layer_id_t{8}});
            check_layer_command(notes[1].semantic_layers == std::vector{layer_id_t{8}});
        }
        check_layer_command(fixture.application_state.semantic_layer_target == layer_id_t{8});
    }
}

TEST_CASE("Application numbered layer selection resets No layer target",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    seed_layer_commands(fixture);
    fixture.application_state.semantic_layer_target_none = true;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::semantic_layer_select_slot, .semantic_layer_slot = 1},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.status);
    require_layer_command(result.accepted);
    check_layer_command(!fixture.application_state.semantic_layer_target_none);
    check_layer_command(fixture.application_state.semantic_layer_target == layer_id_t{7});
}


#else
namespace {
void check_area_unchanged(const AreaFixture& fixture, const std::optional<std::string>& original) {
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.history.undo.empty());
    CHECK(fixture.state.selected_notes ==
          std::vector<wormedit::object_id_t>{wormedit::object_id_t{1}});
}
void check_original_area(const AreaFixture& fixture) {
    using namespace wormedit;
    CHECK(fixture.state.retained_selection_range ==
          position_span_t{{measure_id_t{1}, 121}, {measure_id_t{1}, 480}});
}
} // namespace

TEST_CASE("Compose area resizing snaps only the moving edge and leaves edits and selection intact",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    const auto selected = fixture.state.selected_objects;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{121, 720});
    check_area_unchanged(fixture, original);
    CHECK(fixture.state.selected_objects == selected);
}

TEST_CASE("Compose area edge crossing normalizes rows and cancellation restores unsnapped bounds",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    const auto selected = fixture.state.selected_objects;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    fixture.action.compose_area_begin_edge = true;
    fixture.action.compose_area_begin_row = 900;
    fixture.action.compose_area_end_row = 480;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{480, 960});
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE;
    fixture.action.compose_area_begin_row = 121;
    fixture.action.compose_area_end_row = 480;
    CHECK(fixture.dispatch() == "Area resize canceled.");
    check_original_area(fixture);
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.state.selected_objects == selected);
}

TEST_CASE("Compose area actions reject stale sources and extreme rows atomically",
          "[unit][sys][commands]") {
    AreaFixture fixture;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    const auto range = fixture.state.retained_selection_range;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    SECTION("stale revision") {
        ++fixture.action.compose_input_document_revision;
    }
    SECTION("other chart") {
        ++fixture.action.compose_input_chart_id;
    }
    SECTION("overflow row") {
        fixture.action.compose_area_end_row = std::numeric_limits<std::uint64_t>::max();
    }
    CHECK(fixture.dispatch() != "Selected area resized.");
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.state.retained_selection_range == range);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Compose area quantization changes selected notes only", "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
    CHECK(fixture.dispatch() == "Selected notes quantized.");
    const auto& chart = fixture.history.document.charts.front();
    CHECK(chart.measures.front().notes[0].position.row == 240);
    CHECK(chart.measures.front().notes[1].position.row == 480);
    CHECK(chart.measures.front().notes[2].position.row == 720);
    CHECK(chart.bpms[1].position.row == 480);
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose area deletion preserves notes and timing at the ending boundary",
          "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch() == "Selected area contents deleted.");
    const auto& chart = fixture.history.document.charts.front();
    REQUIRE(chart.measures.front().notes.size() == 2);
    CHECK(chart.measures.front().notes.front().id == wormedit::object_id_t{2});
    CHECK(chart.measures.front().notes.back().id == wormedit::object_id_t{3});
    REQUIRE(chart.bpms.size() == 2);
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose area resizing retains virtual canvas rows without materializing measures",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    fixture.history.document.charts.front().source_family = format_family_t::bms;
    fixture.state.retained_selection_range.reset();
    fixture.state.retained_canvas_selection_range = canvas_row_span_t{2400, 4800};
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    fixture.action.compose_area_virtual = true;
    fixture.action.compose_area_begin_row = 2400;
    fixture.action.compose_area_end_row = 5000;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{2400, 5040});
    CHECK_FALSE(fixture.state.retained_selection_range);
    check_area_unchanged(fixture, original);
    fixture.action.compose_area_end_row = 960000;
    CHECK(fixture.dispatch() != "Selected area resized.");
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
}

TEST_CASE("Compose area deletion rejects unsupported timing atomically", "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.history.document.charts.front().bpms[1].position.row = 240;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    CHECK_FALSE(compose_area_delete_supported(fixture.history, fixture.state));
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch().find("notes and BGM only") != std::string::npos);
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Compose area deletion removes interior BGM and preserves the ending boundary",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.resize(1);
    chart.bgm_source_lanes.push_back({.id = object_id_t{300}, .name = "BGM"});
    chart.measures.front().bgm_events = {{.id = object_id_t{200},
                                          .position = {measure_id_t{1}, 240},
                                          .source_lane = object_id_t{300}},
                                         {.id = object_id_t{201},
                                          .position = {measure_id_t{1}, 480},
                                          .source_lane = object_id_t{300}},
                                         {.id = object_id_t{202},
                                          .position = {measure_id_t{1}, 720},
                                          .source_lane = object_id_t{300}}};
    REQUIRE(compose_area_delete_supported(fixture.history, fixture.state));
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    CHECK(compose_area_delete_supported(chart, index, fixture.state));
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch() == "Selected area contents deleted.");
    const auto& events = fixture.history.document.charts.front().measures.front().bgm_events;
    REQUIRE(events.size() == 2);
    CHECK(events.front().id == object_id_t{201});
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose area edges follow absolute active grid across odd measure lengths",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition = {{5, 8}, meter_t{5, 8}};
    chart.measures.front().notes.back().position.row = 520;
    fixture.state.retained_selection_range =
        position_span_t{{measure_id_t{2}, 17}, {measure_id_t{2}, 480}};
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    fixture.action.compose_area_begin_row = 617;
    fixture.action.compose_area_end_row = 1100;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{617, 1200});
    REQUIRE(fixture.state.retained_selection_range);
    CHECK(fixture.state.retained_selection_range->begin == local_position_t{measure_id_t{2}, 17});
    CHECK(fixture.state.retained_selection_range->end == local_position_t{measure_id_t{2}, 600});
}

TEST_CASE("Compose area quantization rejects ambiguous identities without edits", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.back().id = wormedit::object_id_t{1};
    const auto revision = fixture.history.document.revision;
    const auto position = chart.measures.front().notes.front().position;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
    CHECK(fixture.dispatch().find("identities overlap") != std::string::npos);
    CHECK(fixture.history.document.revision == revision);
    CHECK(chart.measures.front().notes.front().position == position);
    CHECK(chart.bpms.back().id == wormedit::object_id_t{1});
    CHECK(fixture.history.undo.empty());
}


TEST_CASE("Transport rates accept exact preferences and reject boundaries", "[unit][sys][commands]") {
    application_command_state_t state;
    wormedit::rational_t requested{1, 1};
    int calls = 0;
    state.transport_set_playback_rate = [&](wormedit::rational_t rate) {
        requested = rate;
        ++calls;
        return true;
    };
    CHECK_FALSE(step_transport_rate(state, true));
    CHECK(calls == 0);
    for (std::size_t index = 8; index > 0; --index) {
        REQUIRE(step_transport_rate(state, false));
        CHECK(state.transport_rate_index == index - 1);
        CHECK(requested == wormedit::audio::playback_rates[index - 1]);
    }
    CHECK_FALSE(step_transport_rate(state, false));
    CHECK(calls == 8);
    CHECK_FALSE(set_transport_rate(state, 9));
    state.transport_set_playback_rate = [](wormedit::rational_t) { return false; };
    CHECK_FALSE(step_transport_rate(state, true));
    CHECK(state.transport_rate_index == 0);
    CHECK_FALSE(state.transport_keep_pitch_available);
    CHECK_FALSE(state.transport_keep_pitch);
    REQUIRE(parse_application_command("transport.rate_slower"));
    REQUIRE(parse_application_command("transport.rate_faster"));
    CHECK(parse_application_command("transport.rate_slower")->kind == application_command_kind_t::transport_rate_slower);
    CHECK(parse_application_command("transport.rate_faster")->kind == application_command_kind_t::transport_rate_faster);
}

namespace {
void check_note_kind_command_parser(std::string_view name, unsigned kind) {
    const auto id = "edit.note_kind_" + std::string(name);
    const auto command = parse_application_command(id);
    REQUIRE(command);
    CHECK(command->kind == application_command_kind_t::edit_note_kind);
    CHECK(command->note_kind == kind);
}
void check_note_kind_command_shortcut(const wormedit::command_registry_t& registry,
                                      std::string_view name, std::size_t index) {
    using namespace wormedit;
    const auto id = "edit.note_kind_" + std::string(name);
    const auto registration = std::ranges::find_if(registry.commands,
        [&](const auto& entry) { return entry.id.value == id; });
    REQUIRE(registration != registry.commands.end());
    REQUIRE(registration->default_shortcuts.size() == 1);
    CHECK(registration->default_shortcuts.front().key ==
        static_cast<shortcut_key_t>(static_cast<unsigned>(shortcut_key_t::digit_2) + index));
    CHECK(registration->default_shortcuts.front().modifiers ==
        static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt));
}
} // namespace

TEST_CASE("Note kind commands preserve lane and layer shortcut chords", "[unit][sys][commands]") {
    using namespace wormedit;
    const auto registry = make_default_editor_command_registry(
        [](const command_invocation_context_t&) { return command_invocation_result_t{}; });
    const std::array names{"tap", "hold", "mine", "invisible", "lift", "fake", "roll"};
    const std::array kinds{0U, 1U, 4U, 3U, 5U, 6U, 2U};
    for (std::size_t index = 0; index < names.size(); ++index) {
        check_note_kind_command_parser(names[index], kinds[index]);
        check_note_kind_command_shortcut(registry, names[index], index);
    }
    CHECK_FALSE(parse_application_command("edit.note_kind_invalid"));
    CHECK_FALSE(parse_application_command("edit.note_kind_tap.extra"));
}

TEST_CASE("Minimap transport gestures preserve playing paused and stopped policies", "[unit][sys][commands]") {
    application_command_state_t state;
    bool playing = true, paused = false;
    std::uint64_t resume = 0;
    int seeks = 0, stops = 0;
    state.transport_preview_playing = [&] { return playing; };
    state.transport_preview_paused = [&] { return paused; };
    state.transport_seek = [&](std::uint64_t frame) { ++seeks; resume = frame; return "seek"; };
    state.transport_stop = [&] { ++stops; paused = false; resume = 0; };
    CHECK(navigate_compose_minimap_transport(state, 100, false, false) == "seek");
    CHECK(seeks == 1);
    CHECK(resume == 100);
    CHECK(navigate_compose_minimap_transport(state, 200, true, false).empty());
    CHECK(seeks == 1);
    playing = false; paused = true;
    CHECK(navigate_compose_minimap_transport(state, 200, false, false).empty());
    CHECK(stops == 1);
    CHECK_FALSE(paused);
    CHECK(resume == 0);
    paused = true;
    CHECK(navigate_compose_minimap_transport(state, 300, false, true) == "seek");
    CHECK(paused);
    CHECK(resume == 300);
    state.minimap_click_moves_paused_playhead = true;
    CHECK(navigate_compose_minimap_transport(state, 400, false, false) == "seek");
    CHECK(resume == 400);
    CHECK(navigate_compose_minimap_transport(state, 500, false, true).empty());
    CHECK(stops == 2);
    const int before = seeks;
    CHECK(navigate_compose_minimap_transport(state, 600, false, false).empty());
    CHECK(seeks == before);
    playing = true;
    state.transport_seek = [](std::uint64_t) { return "seek failed"; };
    CHECK(navigate_compose_minimap_transport(state, 600, false, false) == "seek failed");
    CHECK(playing);
}

TEST_CASE("Minimap mapping converts rates offsets snaps and chart endpoints without document edits", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.beat_zero_audio_offset_seconds = {1, 1};
    const auto timing = wormedit::build_timing_index(chart);
    const auto measures = wormedit::build_measure_index(chart);
    REQUIRE(timing);
    REQUIRE(std::holds_alternative<wormedit::measure_index_t>(measures));
    const auto before = wormedit::write_native_project(fixture.history.document).bytes;
    const auto early = compose_minimap_target(0, 48001, 48000, 44100, *timing.index, 4);
    REQUIRE(early);
    CHECK(early->frame == 0);
    CHECK(early->snapped_row == 0);
    const auto at_zero = compose_minimap_target(1, 48001, 48000, 44100, *timing.index, 4);
    REQUIRE(at_zero);
    CHECK(at_zero->frame == 44100);
    CHECK(at_zero->snapped_row == 0);
    const auto target = compose_minimap_target(1, 72001, 48000, 44100, *timing.index, 4);
    REQUIRE(target);
    CHECK(target->frame == 66150);
    CHECK(target->snapped_row == 240);
    REQUIRE(move_compose_minimap_cursor(fixture.history, fixture.state, std::get<wormedit::measure_index_t>(measures), *timing.index, *target));
    CHECK((fixture.state.cursor == wormedit::local_position_t{wormedit::measure_id_t{1}, 240}));
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == before);
    const auto clamped = compose_minimap_target(1, 72001, 48000, 44100, *timing.index, 4, 44100);
    REQUIRE(clamped);
    CHECK(clamped->frame == 44100);
    CHECK(clamped->snapped_row == 0);
    CHECK_FALSE(compose_minimap_target(1.1, 72001, 48000, 44100, *timing.index, 4));
}

TEST_CASE("Minimap audio-tail starts are bound to chart revision and cursor", "[unit][sys][commands]") {
    AreaFixture fixture;
    application_command_state_t application;
    const auto timing = wormedit::build_timing_index(fixture.history.document.charts.front());
    REQUIRE(timing);
    const auto target = compose_minimap_target(1, 441001, 44100, 44100, *timing.index, 4);
    REQUIRE(target);
    REQUIRE(target->absolute_row == timing.index->total_rows);
    SECTION("stopped and canceled pause retain the exact frame") {
        bool paused = true;
        application.transport_preview_paused = [&] { return paused; };
        application.transport_stop = [&] { paused = false; };
        (void)navigate_compose_minimap_transport(application, target->frame, false, false);
        REQUIRE_FALSE(paused);
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK(compose_minimap_start_frame(application, fixture.history, &fixture.state) == target->frame);
    }
    SECTION("normal cursor movement discards the binding") {
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        fixture.state.cursor.row = 240;
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
        fixture.state.cursor.row = 0;
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    }
    SECTION("revision changes discard the binding") {
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        ++fixture.history.document.revision;
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    }
    SECTION("chart changes discard the binding") {
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        fixture.state.chart = wormedit::chart_id_t{8};
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    }
    SECTION("playing and paused exact seeks require no next-start binding") {
        application.transport_preview_playing = [] { return true; };
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK_FALSE(application.minimap_start_frame);
        application.transport_preview_playing = [] { return false; };
        application.transport_preview_paused = [] { return true; };
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK_FALSE(application.minimap_start_frame);
    }
    SECTION("in-range clicks use their active snapped cursor") {
        const auto in_range = compose_minimap_target(0.1, 441001, 44100, 44100, *timing.index, 4);
        REQUIRE(in_range);
        retain_compose_minimap_start(application, fixture.history, fixture.state, *in_range, *timing.index);
        CHECK_FALSE(application.minimap_start_frame);
    }
}

TEST_CASE("Minimap view scrolling preserves floor snap and authored BMS tail cursor", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    SECTION("off-grid row floors instead of nearest rounding") {
        const auto timing = wormedit::build_timing_index(chart);
        const auto measures = wormedit::build_measure_index(chart);
        REQUIRE(timing);
        REQUIRE(std::holds_alternative<wormedit::measure_index_t>(measures));
        compose_minimap_target_t target{36750, 400, 240};
        REQUIRE(move_compose_minimap_cursor(fixture.history, fixture.state,
            std::get<wormedit::measure_index_t>(measures), *timing.index, target));
        CHECK(fixture.state.cursor.row == 240);
        CHECK_FALSE(fixture.state.virtual_bms_cursor_row);
    }
    SECTION("audio tail retains an authored cursor on BMS") {
        chart.source_family = wormedit::format_family_t::bms;
        const auto timing = wormedit::build_timing_index(chart);
        const auto measures = wormedit::build_measure_index(chart);
        REQUIRE(timing);
        REQUIRE(std::holds_alternative<wormedit::measure_index_t>(measures));
        const auto target = compose_minimap_target(1, 441001, 44100, 44100, *timing.index, 4);
        REQUIRE(target);
        REQUIRE(move_compose_minimap_cursor(fixture.history, fixture.state,
            std::get<wormedit::measure_index_t>(measures), *timing.index, *target));
        CHECK_FALSE(fixture.state.virtual_bms_cursor_row);
        CHECK((fixture.state.cursor == wormedit::local_position_t{wormedit::measure_id_t{2}, 720}));
        application_command_state_t application;
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK(compose_minimap_start_frame(application, fixture.history, &fixture.state) == 441000);
    }
}

namespace { void expect_keep_pitch(bool value) { CHECK(value); } }

TEST_CASE("Keep pitch dispatch accepts backend preferences without changing documents", "[unit][sys][commands][keep-pitch]") {
    application_command_state_t state;
    wormedit::stepmania_export_policy_t policy;
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH;
    action.transport_keep_pitch = true;
    expect_keep_pitch(dispatch_transport_player_action(action, state, policy) == "Keep pitch is unavailable.");
    state.transport_keep_pitch_available = true;
    bool requested{};
    state.transport_set_keep_pitch = [&](bool value) { requested = value; return true; };
    expect_keep_pitch(dispatch_transport_player_action(action, state, policy) == "Keep pitch enabled.");
    expect_keep_pitch(requested);
    expect_keep_pitch(state.transport_keep_pitch);
    state.transport_set_keep_pitch = [](bool) { return false; };
    action.transport_keep_pitch = false;
    expect_keep_pitch(dispatch_transport_player_action(action, state, policy) == "Keep pitch is unavailable.");
    expect_keep_pitch(state.transport_keep_pitch);
}

TEST_CASE("Tap timing commands parse independently of document editing", "[unit][sys][commands]") {
    CHECK(parse_application_command("timing.tap_assistant") ==
          application_command_t{.kind = application_command_kind_t::timing_tap_assistant});
    CHECK(parse_application_command("timing.tap") ==
          application_command_t{.kind = application_command_kind_t::timing_tap});
}

TEST_CASE("notification history has a stable typed application command", "[unit][sys][notifications]") {
    CHECK(parse_application_command("view.notifications") ==
          application_command_t{.kind = application_command_kind_t::view_notifications});
    CHECK_FALSE(parse_application_command("view.notifications.extra"));
}

namespace { void expect_play_stop(bool value) { CHECK(value); } }

TEST_CASE("Plain Space transport starts or hard stops without pausing", "[unit][sys][commands]") {
    application_command_state_t state;
    bool playing{};
    bool paused{};
    int starts{};
    int stops{};
    state.transport_preview_playing = [&] { return playing; };
    state.transport_play_pause = [&] {
        if (playing) paused = true;
        else { ++starts; playing = true; paused = false; }
        return std::string{"started"};
    };
    state.transport_stop = [&] { ++stops; playing = false; paused = false; };
    expect_play_stop(play_stop_transport(state) == "started");
    expect_play_stop(starts == 1);
    expect_play_stop(play_stop_transport(state) == "Audio preview stopped.");
    expect_play_stop(stops == 1);
    expect_play_stop(!playing);
    expect_play_stop(!paused);
    paused = true;
    expect_play_stop(play_stop_transport(state) == "started");
    expect_play_stop(starts == 2);
    expect_play_stop(!paused);
    state.transport_stop = {};
    expect_play_stop(!play_stop_transport(state));
    expect_play_stop(playing);
}

TEST_CASE("bookmark commands parse to their application kinds", "[unit][sys][commands]") {
    using kind = application_command_kind_t;
    const auto previous = parse_application_command("navigation.previous_bookmark");
    const auto next = parse_application_command("navigation.next_bookmark");
    const auto add = parse_application_command("markers.add_bookmark");
    REQUIRE(previous);
    REQUIRE(next);
    REQUIRE(add);
    CHECK(previous->kind == kind::navigation_previous_bookmark);
    CHECK(next->kind == kind::navigation_next_bookmark);
    CHECK(add->kind == kind::markers_add_bookmark);
}

namespace {
void check_layer_lock(bool condition) { CHECK(condition); }
void require_layer_lock(bool condition) { REQUIRE(condition); }
} // namespace

TEST_CASE("Layer row lock commands round trip through shared history and undo",
          "[unit][sys][commands][application_commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    fixture.history.document.charts.front().semantic_layers = {
        {.id = layer_id_t{7}, .name = "Layer", .color = {32, 64, 128, 255}},
        {.id = layer_id_t{9}, .name = "Other", .color = {64, 128, 32, 255}}};
    const auto update = [&](bool locked) {
        return apply_semantic_layer_edit(fixture.history, fixture.history.document.revision,
            update_semantic_layer_action_t{chart_id_t{7}, layer_id_t{9}, "Other",
                                           {64.0 / 255, 128.0 / 255, 32.0 / 255, 1}, true, locked});
    };
    require_layer_lock(update(true).applied);
    check_layer_lock(!fixture.history.document.charts.front().semantic_layers.front().locked);
    check_layer_lock(fixture.history.document.charts.front().semantic_layers.back().locked);
    require_layer_lock(update(false).applied);
    check_layer_lock(!fixture.history.document.charts.front().semantic_layers.back().locked);
    require_layer_lock(undo_document_command(fixture.history));
    check_layer_lock(fixture.history.document.charts.front().semantic_layers.back().locked);
    require_layer_lock(undo_document_command(fixture.history));
    check_layer_lock(!fixture.history.document.charts.front().semantic_layers.back().locked);
}


namespace {
void seed_membership_fixture(AreaFixture& fixture, bool locked = false) {
    using namespace wormedit;
    auto& chart = fixture.history.document.charts.front();
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "First", .color = {32, 64, 128, 255}, .locked = locked},
        {.id = layer_id_t{8}, .name = "Second", .color = {64, 128, 32, 255}}};
    chart.measures.front().notes[0].semantic_layers = {layer_id_t{7}};
    chart.measures.front().notes[1].semantic_layers = {layer_id_t{8}};
    fixture.state.selected_notes = {object_id_t{1}, object_id_t{2}};
}
}

namespace {
void check_membership(bool condition) { CHECK(condition); }
void require_membership(bool condition) { REQUIRE(condition); }
}

TEST_CASE("Compose No layer assigns mixed selection in one step and binds target after success",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    const auto original = fixture.history.document;
    bool none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    const auto result = apply_compose_layer_assignment(fixture.history, fixture.state, 9, true, {}, none, target);
    require_membership(static_cast<bool>(result));
    check_membership(static_cast<bool>(none));
    check_membership(static_cast<bool>(!(target)));
    check_membership(static_cast<bool>(fixture.history.undo.size() == 1));
    check_membership(static_cast<bool>(fixture.state.selected_notes.size() == 2));
    for (const auto& note : fixture.history.document.charts.front().measures.front().notes)
        check_membership(static_cast<bool>(note.semantic_layers.empty()));
    require_membership(static_cast<bool>(undo_document_command(fixture.history)));
    check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
}

TEST_CASE("Compose rejected clear and replacement preserve target and selection",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture, true);
    const auto original = fixture.history.document;
    const auto selection = fixture.state.selected_notes;
    bool none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    for (const auto clear : {true, false}) {
        const auto result = apply_compose_layer_assignment(fixture.history, fixture.state, 9,
            clear, layer_id_t{8}, none, target);
        check_membership(static_cast<bool>(!(result)));
        check_membership(static_cast<bool>(!(result.diagnostics.empty())));
        check_membership(static_cast<bool>(!(none)));
        check_membership(static_cast<bool>(target == layer_id_t{7}));
        check_membership(static_cast<bool>(fixture.state.selected_notes == selection));
        check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
        check_membership(static_cast<bool>(fixture.history.undo.empty()));
    }
}

TEST_CASE("Compose No layer paint clears mixed memberships once and undo restores each source",
          "[unit][sys][application_commands][semantic-layer][paint]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    const auto original = fixture.history.document;
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, true, {}, object_id_t{1}, true)));
    require_membership(static_cast<bool>(dispatch_editor_action(fixture.history, fixture.state,
        update_semantic_layer_paint_gesture_action_t{object_id_t{2}, true})));
    require_membership(static_cast<bool>(finish_compose_layer_paint(fixture.history, fixture.state, {})));
    check_membership(static_cast<bool>(fixture.history.undo.size() == 1));
    check_membership(static_cast<bool>(!(fixture.state.pending_semantic_layer_paint_gesture)));
    check_membership(static_cast<bool>(fixture.state.selected_notes.size() == 2));
    for (const auto& note : fixture.history.document.charts.front().measures.front().notes)
        check_membership(static_cast<bool>(note.semantic_layers.empty()));
    require_membership(static_cast<bool>(undo_document_command(fixture.history)));
    check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
}

TEST_CASE("Compose No layer paint rejects locked membership and exposes status diagnostic",
          "[unit][sys][application_commands][semantic-layer][paint]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture, true);
    const auto original = fixture.history.document;
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, true, {}, object_id_t{2}, true)));
    require_membership(static_cast<bool>(dispatch_editor_action(fixture.history, fixture.state,
        update_semantic_layer_paint_gesture_action_t{object_id_t{1}, true})));
    const auto result = finish_compose_layer_paint(fixture.history, fixture.state, {});
    check_membership(static_cast<bool>(!(result)));
    require_membership(static_cast<bool>(!(result.diagnostics.empty())));
    check_membership(static_cast<bool>(result.diagnostics.front().message.find("locked") != std::string::npos));
    check_membership(static_cast<bool>(!(fixture.state.pending_semantic_layer_paint_gesture)));
    check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
    check_membership(static_cast<bool>(fixture.history.undo.empty()));
    check_membership(static_cast<bool>(fixture.state.selected_notes.size() == 2));
}

TEST_CASE("Compose ordinary layer paint replaces other sources while toggling its own target",
          "[unit][sys][application_commands][semantic-layer][paint]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, false, layer_id_t{8}, object_id_t{1}, true)));
    require_membership(static_cast<bool>(finish_compose_layer_paint(fixture.history, fixture.state, object_id_t{2})));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    check_membership(static_cast<bool>(notes[0].semantic_layers == std::vector{layer_id_t{8}}));
    check_membership(static_cast<bool>(notes[1].semantic_layers.empty()));
    check_membership(static_cast<bool>(fixture.history.undo.size() == 1));
}


TEST_CASE("Compose No layer no-op still binds target and releases an unassigned paint stroke",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    AreaFixture fixture;
    bool none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    const auto result = apply_compose_layer_assignment(fixture.history, fixture.state, 9, true, {}, none, target);
    check_membership(!result.applied && result.diagnostics.empty());
    check_membership(none && !target);
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, true, {}, object_id_t{1}, true)));
    require_membership(static_cast<bool>(finish_compose_layer_paint(fixture.history, fixture.state, {})));
    check_membership(!fixture.state.pending_semantic_layer_paint_gesture);
    check_membership(fixture.history.undo.empty());
}

#endif // WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY

#ifndef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Note context selects only an unselected hit before assigning one layer",
          "[unit][sys][application_commands][note-menu]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    fixture.state.selected_notes = {object_id_t{1}};
    require_membership(static_cast<bool>(select_compose_note_context(fixture.history, fixture.state, 9, object_id_t{2})));
    check_membership(fixture.state.selected_notes == std::vector{object_id_t{2}});
    bool none = false;
    std::optional<layer_id_t> target;
    require_membership(static_cast<bool>(apply_compose_layer_assignment(fixture.history,
        fixture.state, 9, false, layer_id_t{7}, none, target)));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    check_membership(notes[0].semantic_layers == std::vector{layer_id_t{7}});
    check_membership(notes[1].semantic_layers == std::vector{layer_id_t{7}});
    check_membership(fixture.history.undo.size() == 1);
    require_membership(undo_document_command(fixture.history));
    check_membership(fixture.history.document.charts.front().measures.front().notes[1].semantic_layers ==
                     std::vector{layer_id_t{8}});
}

TEST_CASE("Note context preserves selected groups and clearing removes mixed layers once",
          "[unit][sys][application_commands][note-menu]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    require_membership(static_cast<bool>(select_compose_note_context(fixture.history, fixture.state, 9, object_id_t{2})));
    check_membership(fixture.state.selected_notes == std::vector{object_id_t{1}, object_id_t{2}});
    bool none = false;
    std::optional<layer_id_t> target{layer_id_t{8}};
    require_membership(static_cast<bool>(apply_compose_layer_assignment(fixture.history,
        fixture.state, 9, true, {}, none, target)));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    check_membership(notes[0].semantic_layers.empty());
    check_membership(notes[1].semantic_layers.empty());
    check_membership(fixture.history.undo.size() == 1);
}

TEST_CASE("Note context rejects stale and unknown hits without replacing selection",
          "[unit][sys][application_commands][note-menu]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    const auto selection = fixture.state.selected_notes;
    check_membership(!select_compose_note_context(fixture.history, fixture.state, 8, object_id_t{3}));
    check_membership(!select_compose_note_context(fixture.history, fixture.state, 9, object_id_t{999}));
    check_membership(fixture.state.selected_notes == selection);
    check_membership(fixture.history.undo.empty());
}

#endif
