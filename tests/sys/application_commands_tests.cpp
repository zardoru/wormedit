#include "sys/transport_player_actions.h"
#include "sys/application_commands.h"
#include "sys/compose_minimap_navigation.h"

#include <wormedit/core/command_registry.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace wormedit::sys;

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

TEST_CASE("Compose area deletion includes note endpoints while preserving outside contents",
          "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.history.document.charts.front().bpms.resize(1);
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch() == "Selected area contents deleted.");
    const auto& chart = fixture.history.document.charts.front();
    REQUIRE(chart.measures.front().notes.size() == 1);
    CHECK(chart.measures.front().notes.front().id == wormedit::object_id_t{3});
    REQUIRE(chart.bpms.size() == 1);
    CHECK(chart.bpms.front().id == wormedit::object_id_t{101});
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
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    CHECK_FALSE(compose_area_delete_supported(fixture.history, fixture.state));
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch().find("notes and BGM only") != std::string::npos);
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Compose area deletion removes BGM at the inclusive tail and keeps outside BGM",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.resize(1);
    chart.bgm_source_lanes.push_back({.id = object_id_t{300}, .name = "BGM"});
    chart.measures.front().bgm_events = {{.id = object_id_t{201},
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
    REQUIRE(events.size() == 1);
    CHECK(events.front().id == object_id_t{202});
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
        static_cast<shortcut_key_t>(static_cast<unsigned>(shortcut_key_t::digit_1) + index));
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
