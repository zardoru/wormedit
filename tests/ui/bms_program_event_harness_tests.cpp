#include "nuklear_event_harness.h"
#include "sys/bms_program_runtime.h"
#include "sys/project/workspace_view.h"
#include "sys/compose/views/compose_view.h"
#include "sys/timing/timing_view.h"
#include "ui/nuklear/timing/timing_stage_editor.h"
#include "ui/nuklear/compose/compose_panel.h"
#include "ui/nuklear/compose/compose_state.h"
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <cstdlib>
#include <fstream>

namespace {
using namespace wormedit;
using namespace wormedit::sys;
using operation_t = ui::bms_program_operation_t;
constexpr chart_id_t difficulty{9000};
auto scoped_history() -> document_history_t {
    constexpr std::string_view source = "#TITLE Scope authoring\n#BPM 120\n#WAV01 common.wav\n#00011:01\n"
        "#RANDOM 2\n#IF 1\n#00112:01\n#ELSEIF 3\n#00213:01\n#00102:1.5\n#ENDIF\n#ENDRANDOM\n; editable comment\n";
    auto prepared = prepare_bms_program(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value.prepared);
    auto& input = *prepared.value.prepared;
    bms_program_evaluation_t evaluation;
    for (const auto& node : input.program.nodes())
        if (std::holds_alternative<bms_random_scope_t>(node.value)) evaluation.selections.push_back({.branch = node.id, .selected_value = 1});
    auto owner = bms_program_document_t::from(difficulty, std::move(input), std::move(evaluation));
    REQUIRE(owner.value);
    return document_history_t::from(song_document_t{.id = song_id_t{9001}, .prepared_bms_programs = {std::move(*owner.value)}});
}
struct scope_flow_t {
    document_history_t history;
    bms_program_runtime_t runtime;
    editor_interaction_state_t state;
    ui::bms_program_view_t view;
    explicit scope_flow_t(document_history_t initial = scoped_history()) : history(std::move(initial)) { project(); }
    void project() { runtime.synchronize(history, difficulty); runtime.refill(view, history); }
    auto action(operation_t operation, branch_id_t node = {}, std::string text = {}) const -> ui::bms_program_action_t {
        return {.project = history.document.id, .difficulty = difficulty, .revision = history.document.revision,
            .selected = view.selected, .operation = operation, .node = node, .text = std::move(text)};
    }
    auto apply(ui::bms_program_action_t request) -> document_command_result_t {
        auto result = runtime.apply(history, state, request); project(); return result;
    }
    auto node(ui::bms_program_node_kind_t kind) const -> branch_id_t {
        const auto row = std::ranges::find(view.rows, kind, &ui::bms_program_row_t::kind);
        REQUIRE(row != view.rows.end());
        return row->node;
    }
};
struct scope_projection_t {
    frontend_workspace_view_t frontend;
    compose_scene_cache_t scene;
    timing_scene_cache_t timing;
    application_command_state_t commands;
    std::optional<editor_interaction_state_t> interaction;
    void project(scope_flow_t& flow) {
        auto workspace = workspace_controller_t::from(flow.history);
        REQUIRE(workspace.value);
        workspace.value->active_tab = workspace_tab_t::compose;
        const auto source = workspace_view_t::from(*workspace.value, flow.history.document, workspace_tab_t::compose,
            nullptr, nullptr, nullptr, flow.runtime.content_chart());
        REQUIRE(source.value);
        const ui::keysound_dock_request_t dock{};
        frontend.refill(*source.value, flow.history.document, "Ready", {}, false, dock_sort_t::natural_name, dock);
        flow.runtime.refill(frontend.view.compose.bms_program, flow.history);
        populate_timing_view_for_compose(frontend, flow.history, *workspace.value, timing, 1280, 900,
            {.chart = difficulty, .supported_denominators = {4}, .active_denominator = 4}, nullptr);
        populate_compose_view(frontend, flow.history, *workspace.value, scene, 1280, 900, interaction, commands, nullptr);
    }
};
void check_scope_projection(const scope_projection_t& projection, std::size_t notes, std::size_t read_only) {
    const auto content = notes != 0;
    INFO(projection.frontend.view.compose.bms_program.target_path);
    CHECK(worm_ui_nuklear_timing_view_valid(&projection.frontend.view.timing));
    CHECK(worm_ui_nuklear_compose_view_valid(&projection.frontend.view.compose));
    CHECK(projection.frontend.view.timing.scene_available == content);
    CHECK(projection.frontend.view.compose.scene_available == content);
    CHECK(projection.frontend.view.compose.note_endpoints.size() == notes);
    CHECK(std::ranges::count(projection.frontend.view.compose.note_endpoints, true, &ui::compose_note_view_t::read_only) == read_only);
}
void check_scope_refusal(const document_command_result_t& result) {
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_binding);
}
auto click_label(nuklear_event_harness_t& harness, std::string_view text) -> ui::bms_program_action_t {
    std::optional<std::pair<float, float>> center;
    for (const auto& drawn : harness.drawn_text_bounds()) {
        auto label = std::string_view{drawn.text};
        while (label.starts_with(' ')) label.remove_prefix(1);
        if (label == text) center = std::pair{drawn.x + drawn.width * 0.5f, drawn.y + drawn.height * 0.5f};
    }
    REQUIRE(center);
    const auto clicks = test::click_at(harness, center->first, center->second);
    for (const auto& click : clicks)
        if (click.get().compose_action == WORM_UI_COMPOSE_ACTION_BMS_PROGRAM) return click.get().bms_program;
    FAIL("scope widget emitted no action");
    return {};
}
void bind_panel(nuklear_event_harness_t& harness, const scope_flow_t& flow) {
    auto& view = harness.view();
    view.project_id = flow.history.document.id; view.document_revision = flow.history.document.revision;
    view.selected_chart_id = difficulty;
    view.compose.project_id = flow.history.document.id; view.compose.chart_id = difficulty;
    view.compose.document_revision = flow.history.document.revision;
    view.compose.bms_program = flow.view;
}
void write_visual_fixture(const document_history_t& history) {
    const auto* path = std::getenv("WORMEDIT_BMS_SCOPE_FIXTURE");
    if (!path) return;
    const auto native = write_native_project(history.document);
    REQUIRE(native.value);
    std::ofstream output{path}; output << *native.value;
    REQUIRE(output.good());
}

}

TEST_CASE("Flat imported BMS opens with every note editable in its initial scope",
          "[bms-scope][flat-bms]") {
    constexpr std::string_view source = "#TITLE Flat\n\n#WAV01 a.wav\n"
        "; notes\n#00011:01\n\n#00112:01\n\n#UNKNOWN keep\n#00216:01\n";
    auto input = prepare_bms_program(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(input.value.prepared);
    auto owner = bms_program_document_t::from(difficulty, std::move(*input.value.prepared));
    REQUIRE(owner.value);
    scope_flow_t flow{document_history_t::from(song_document_t{.id = song_id_t{9001},
        .prepared_bms_programs = {std::move(*owner.value)}})};
    scope_projection_t projection;
    projection.project(flow);
    check_scope_projection(projection, 3, 0);
    CHECK(flow.view.selected == flow.history.document.find_bms_program(difficulty)->prepared().program.root());
    CHECK(projection.frontend.view.compose.note_endpoints[0].key_color == lane_key_color_t::white);
    CHECK(projection.frontend.view.compose.note_endpoints[1].key_color == lane_key_color_t::black);
    CHECK(projection.frontend.view.compose.note_endpoints[2].key_color == lane_key_color_t::scratch);
}
TEST_CASE("Scope selection is independent of playback and clears retained object selection", "[bms-scope]") {
    scope_flow_t flow;
    const auto evaluation = flow.history.document.find_bms_program(difficulty)->evaluation();
    flow.state.selected_notes = {object_id_t{123}};
    flow.state.selected_canvas_measure = 0;
    const auto arm = flow.node(ui::bms_program_node_kind_t::else_if);
    CHECK_FALSE(flow.apply(flow.action(operation_t::select, arm)).applied);
    CHECK(flow.view.selected == arm);
    CHECK(flow.history.document.find_bms_program(difficulty)->evaluation() == evaluation);
    CHECK(flow.state.selected_notes.empty());
    CHECK_FALSE(flow.state.selected_canvas_measure);
    CHECK(flow.history.undo.empty());
}
TEST_CASE("A scope draft is recoverable after rejection and reload explicitly discards it", "[bms-scope]") {
    scope_flow_t flow;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::random)));
    (void)flow.apply(flow.action(operation_t::draft_parameter, {}, "zero?"));
    const auto result = flow.apply(flow.action(operation_t::save));
    check_scope_refusal(result);
    CHECK(flow.view.parameter == "zero?");
    (void)flow.apply(flow.action(operation_t::reload));
    CHECK(flow.view.parameter == "2");
    CHECK(flow.history.undo.empty());
}
TEST_CASE("Delete confirmation cancels or applies the exact prepared subtree", "[bms-scope]") {
    scope_flow_t flow;
    const auto arm = flow.node(ui::bms_program_node_kind_t::else_if);
    (void)flow.apply(flow.action(operation_t::select, arm));
    (void)flow.apply(flow.action(operation_t::prepare_delete));
    REQUIRE_FALSE(flow.view.confirmation.empty());
    const auto confirms = GENERATE(false, true);
    const auto result = flow.apply(flow.action(confirms ? *flow.view.confirmation_operation : operation_t::cancel));
    CHECK(result.applied == confirms);
    const auto& program = flow.history.document.find_bms_program(difficulty)->prepared().program;
    CHECK((program.find(arm) == nullptr) == confirms);
}
TEST_CASE("Changing the selected node invalidates an earlier confirmation action", "[bms-scope]") {
    scope_flow_t flow;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::else_if)));
    (void)flow.apply(flow.action(operation_t::prepare_delete));
    REQUIRE(flow.view.confirmation_operation);
    const auto captured = flow.action(*flow.view.confirmation_operation);
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::text)));
    const auto result = flow.apply(captured);
    check_scope_refusal(result);
    CHECK(flow.history.undo.empty());
}
TEST_CASE("A valid authoring node stays selected through parameter Undo and Redo", "[bms-scope]") {
    scope_flow_t flow;
    const auto arm = flow.node(ui::bms_program_node_kind_t::else_if);
    (void)flow.apply(flow.action(operation_t::select, arm));
    (void)flow.apply(flow.action(operation_t::draft_parameter, {}, "4"));
    REQUIRE(flow.apply(flow.action(operation_t::save)).applied);
    REQUIRE(undo_document_command(flow.history)); flow.project();
    CHECK(flow.view.selected == arm);
    CHECK(flow.view.parameter == "3");
    REQUIRE(redo_document_command(flow.history)); flow.project();
    CHECK(flow.view.parameter == "4");
}
TEST_CASE("Scope panel renders retained unreachable arms at normal and compact window sizes", "[bms-scope]") {
    const auto width = GENERATE(1280, 800);
    scope_flow_t flow;
    nuklear_event_harness_t harness{width, 700};
    REQUIRE(harness.ready());
    // A compact window has no room for the right dock beside the branch panel, so hide it.
    if (width < 1000)
        for (const auto panel : {dock_panel_t::inspector, dock_panel_t::sounds})
            worm_ui_nuklear_compose_state().dock_layout.layout.show(panel, false);
    bind_panel(harness, flow);
    (void)harness.frame();
    // Reachability is a state dot on the row; the row text is the node label.
    CHECK(harness.drawn_text().find("RANDOM 2") != std::string::npos);
    CHECK(harness.drawn_text().find("ELSEIF 3") != std::string::npos);
    write_visual_fixture(flow.history);
}
TEST_CASE("A tree click captures the original node and never changes playback choices", "[bms-scope]") {
    scope_flow_t flow;
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready()); bind_panel(harness, flow); (void)harness.frame();
    const auto request = click_label(harness, "ELSEIF 3");
    CHECK(request.operation == operation_t::select);
    CHECK(request.node == flow.node(ui::bms_program_node_kind_t::else_if));
    (void)flow.apply(request);
    CHECK(flow.view.selected == request.node);
    CHECK(flow.history.undo.empty());
}
TEST_CASE("An alternate Chart is an explicit target and Undo repairs its deletion", "[bms-scope]") {
    scope_flow_t flow;
    const auto arm = flow.node(ui::bms_program_node_kind_t::else_if);
    (void)flow.apply(flow.action(operation_t::select, arm));
    const auto first = flow.view.content_chart;
    auto add = flow.action(operation_t::add); add.kind = ui::bms_program_node_kind_t::chart;
    REQUIRE(flow.apply(add).applied);
    const auto& program = flow.history.document.find_bms_program(difficulty)->prepared().program;
    const auto second = program.find(arm)->children.back();
    REQUIRE(second != *first);
    (void)flow.apply(flow.action(operation_t::select, second));
    CHECK(flow.view.content_chart == second);
    REQUIRE(undo_document_command(flow.history)); flow.project();
    CHECK(flow.view.content_chart == first);
}
TEST_CASE("Unwrap exposes an explicit always-applying conversion and supports Cancel", "[bms-scope]") {
    scope_flow_t flow;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::random)));
    (void)flow.apply(flow.action(operation_t::prepare_unwrap));
    CHECK(flow.view.unconditional_conversion);
    CHECK_FALSE(flow.view.confirmation.empty());
    (void)flow.apply(flow.action(operation_t::cancel));
    CHECK(flow.view.confirmation.empty());
    CHECK(flow.history.undo.empty());
}
TEST_CASE("Scope parameter text capture does not emit chart keyboard input", "[bms-scope]") {
    scope_flow_t flow;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::random)));
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready()); bind_panel(harness, flow); (void)harness.frame();
    const auto field = harness.drawn_text_center("2");
    REQUIRE(field);
    (void)test::click_at(harness, field->first, field->second);
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; key.key.key = SDLK_4;
    key.key.scancode = SDL_SCANCODE_4; key.key.down = true;
    SDL_Event text{}; text.type = SDL_EVENT_TEXT_INPUT; text.text.text = "4";
    const std::array events{key, text};
    const auto typed = harness.frame(events);
    CHECK(typed.get().bms_program.operation == operation_t::draft_parameter);
    CHECK(typed.get().bms_program.text.find('4') != std::string::npos);
    CHECK(typed.get().compose_inputs.empty());
}
TEST_CASE("Saving Text keeps content editing unavailable for that selected node", "[bms-scope]") {
    scope_flow_t flow;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::text)));
    (void)flow.apply(flow.action(operation_t::draft_text, {}, "; revised comment\n"));
    REQUIRE(flow.apply(flow.action(operation_t::save)).applied);
    CHECK_FALSE(flow.view.content_chart);
    CHECK(flow.history.read_chart(difficulty) == nullptr);
    CHECK(flow.view.text == "; revised comment\n");
}
TEST_CASE("A scope follows its first direct Chart when the children are reordered", "[bms-scope]") {
    scope_flow_t flow;
    const auto arm = flow.node(ui::bms_program_node_kind_t::else_if);
    (void)flow.apply(flow.action(operation_t::select, arm));
    auto add = flow.action(operation_t::add); add.kind = ui::bms_program_node_kind_t::chart;
    REQUIRE(flow.apply(add).applied);
    const auto& program = flow.history.document.find_bms_program(difficulty)->prepared().program;
    const auto second = program.find(arm)->children.back();
    auto move = bms_program_move_edit_t::propose(flow.history.document, difficulty,
        {.node = second, .parent = arm});
    REQUIRE(move.value);
    REQUIRE(apply_proposal(flow.history, *move.value).applied);
    flow.project();
    CHECK(flow.view.content_chart == second);
    CHECK(flow.history.bms_edit_target->chart == second);
}
TEST_CASE("Real scope projections keep timing valid and invalidate geometry when the target changes", "[bms-scope]") {
    scope_flow_t flow;
    scope_projection_t projection;
    projection.project(flow);
    check_scope_projection(projection, 1, 0);
    const auto common_rows = projection.frontend.view.timing.total_rows;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::text)));
    projection.project(flow);
    check_scope_projection(projection, 0, 0);
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::else_if)));
    projection.project(flow);
    check_scope_projection(projection, 2, 1);
    CHECK(projection.frontend.view.timing.total_rows > common_rows);
}
TEST_CASE("Delete confirmation cannot authorize a later Unwrap conversion", "[bms-scope]") {
    scope_flow_t flow;
    (void)flow.apply(flow.action(operation_t::select, flow.node(ui::bms_program_node_kind_t::random)));
    (void)flow.apply(flow.action(operation_t::prepare_delete));
    REQUIRE(flow.view.confirmation_operation);
    const auto deletion = flow.action(*flow.view.confirmation_operation);
    (void)flow.apply(flow.action(operation_t::prepare_unwrap));
    CHECK(flow.view.confirmation_operation == operation_t::confirm_unconditional);
    const auto rejected = flow.apply(deletion);
    check_scope_refusal(rejected);
    CHECK(flow.history.undo.empty());
    REQUIRE(flow.apply(flow.action(operation_t::confirm_unconditional)).applied);
}
