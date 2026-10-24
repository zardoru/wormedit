#include "../support/bms_program_fixture.h"
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/workspace.h>
#include <catch2/generators/catch_generators.hpp>

using namespace wormedit;
using namespace wormedit::test::bms_program;
namespace {
constexpr chart_id_t difficulty{9000};
constexpr auto source = "#WAV01 common.wav\n#BPM 120\n#00011:01\n"
    "#RANDOM 2\n#IF 1\n#00112:01\n#ELSEIF 3\n#00213:01\n#ENDIF\n#ENDRANDOM\n";
auto history_from(std::string_view text = source) -> document_history_t {
    auto input = prepared(text);
    bms_program_evaluation_t evaluation;
    for (const auto id : selectors(input.program)) evaluation.selections.push_back({id, 1});
    auto owner = bms_program_document_t::from(difficulty, std::move(input), std::move(evaluation));
    REQUIRE(owner.ok());
    REQUIRE(owner.value);
    return document_history_t::from(song_document_t{.id = song_id_t{9001},
        .prepared_bms_programs = {std::move(*owner.value)}});
}
auto owner(const document_history_t& history) -> const bms_program_document_t& {
    return *history.document.find_bms_program(difficulty);
}
template<class Value> auto node_id(const document_history_t& history) -> branch_id_t {
    for (const auto& node : owner(history).prepared().program.nodes())
        if (std::holds_alternative<Value>(node.value)) return node.id;
    FAIL("missing fixture node");
    return {};
}
auto chart_for(const document_history_t& history, branch_id_t selected) -> branch_id_t {
    const auto target = owner(history).prepared().program.target_chart(selected);
    REQUIRE(target);
    return *target;
}
auto notes(const chart_document_t& chart) -> std::vector<note_t> {
    std::vector<note_t> result;
    for (const auto& measure : chart.measures)
        result.insert(result.end(), measure.notes.begin(), measure.notes.end());
    return result;
}
auto authoring(const document_history_t& history, branch_id_t chart) -> bms_program_projection_t {
    auto view = owner(history).build_projection(chart);
    for (const auto& diagnostic : view.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(view.ok());
    REQUIRE(view.value);
    return std::move(*view.value);
}
void check_label_history(document_history_t& history, branch_id_t selected) {
    REQUIRE(undo_document_command(history));
    CHECK(history.bms_edit_target->chart == selected);
    REQUIRE(redo_document_command(history));
    CHECK(notes(*history.read_chart(difficulty)).back().label == "unreachable but editable");
}
void check_endpoint_warning(const bms_program_projection_diagnostics_t& diagnostics) {
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().severity == diagnostic_severity_t::warning);
    CHECK(diagnostics.front().code == failure_code_t::unavailable);
}

}

TEST_CASE("Authoring an unreachable arm includes common content without changing playback", "[bms-program-authoring-view]") {
    const auto history = history_from();
    const auto before = owner(history).build_projection();
    REQUIRE(before.value);
    const auto selected = chart_for(history, node_id<bms_else_if_scope_t>(history));
    const auto view = authoring(history, selected);
    CHECK(note_keys(view.song()) != note_keys(before.value->song()));
    CHECK(notes(view.song().charts.front()).size() == 2);
    CHECK(view.song().charts.front().bms_authoring_chart == selected);
    const auto after = owner(history).build_projection();
    REQUIRE(after.value);
    CHECK(note_keys(after.value->song()) == note_keys(before.value->song()));
}
TEST_CASE("Authoring marks inherited notes read-only until their Chart is selected", "[bms-program-authoring-view]") {
    const auto history = history_from();
    const auto root = chart_for(history, owner(history).prepared().program.root());
    const auto selected = chart_for(history, node_id<bms_else_if_scope_t>(history));
    const auto view = authoring(history, selected);
    const auto visible = notes(view.song().charts.front());
    REQUIRE(visible.size() == 2);
    CHECK(view.song().charts.front().is_bms_authoring_read_only(visible.front().id));
    CHECK_FALSE(view.song().charts.front().is_bms_authoring_read_only(visible.back().id));
    const auto inherited = bms_program_edit_target_t::for_authoring(history.document, difficulty, selected, visible.front().id);
    REQUIRE_FALSE(inherited);
    CHECK(inherited.error().code == failure_code_t::unavailable);
    CHECK(bms_program_edit_target_t::for_authoring(history.document, difficulty, root, visible.front().id));
}
TEST_CASE("Interactive reads use the exact authoring Chart while document reads keep playback", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto selected = chart_for(history, node_id<bms_else_if_scope_t>(history));
    const auto playback = notes(*history.document.read_chart(difficulty));
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty, .chart = selected};
    const auto* view = history.read_chart(difficulty);
    REQUIRE(view);
    CHECK(view->bms_authoring_chart == selected);
    CHECK(notes(*history.document.read_chart(difficulty)) == playback);
    document_command_transaction_t transaction(history);
    REQUIRE(transaction.read_chart(difficulty));
    CHECK(transaction.read_chart(difficulty)->bms_authoring_chart == selected);
}
TEST_CASE("Inactive owner edits publish semantic data and retain the target through Undo", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto selected = chart_for(history, node_id<bms_else_if_scope_t>(history));
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty, .chart = selected};
    const auto visible = notes(*history.read_chart(difficulty));
    REQUIRE(visible.size() == 2);
    const auto result = apply_note_label_edit(history, {.chart = difficulty,
        .notes = {visible.back().id}, .label = "unreachable but editable"});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(notes(*history.read_chart(difficulty)).back().label == "unreachable but editable");
    CHECK(notes(*history.document.read_chart(difficulty)).back().label != "unreachable but editable");
    check_label_history(history, selected);
}
TEST_CASE("Read-only inherited edits reject atomically", "[bms-program-authoring-view]") {
    auto history = history_from();
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty,
        .chart = chart_for(history, node_id<bms_else_if_scope_t>(history))};
    const auto snapshot = history.document.prepared_bms_programs.front();
    const auto id = notes(*history.read_chart(difficulty)).front().id;
    const auto result = apply_note_label_edit(history, {.chart = difficulty, .notes = {id}, .label = "no"});
    CHECK_FALSE(result.applied);
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(history.document.prepared_bms_programs.front() == snapshot);
    CHECK(history.undo.empty());
}
TEST_CASE("Partial cross-scope holds remain read-only endpoints with a warning", "[bms-program-authoring-view]") {
    const auto history = history_from("#BPM 120\n#WAV01 a.wav\n#00051:01\n#RANDOM 2\n#IF 1\n#00151:01\n#ELSE\n#00251:01\n#ENDIF\n#ENDRANDOM\n");
    const auto root = chart_for(history, owner(history).prepared().program.root());
    const auto view = owner(history).build_projection(root);
    REQUIRE(view.value);
    CHECK(view.ok());
    const auto visible = notes(view.value->song().charts.front());
    REQUIRE(visible.size() == 1);
    CHECK_FALSE(visible.front().end);
    CHECK(view.value->song().charts.front().is_bms_authoring_read_only(visible.front().id));
    check_endpoint_warning(view.diagnostics);
}
TEST_CASE("Authoring never invents hold pairs for an inactive branch", "[bms-program-authoring-view]") {
    const auto history = history_from("#BPM 120\n#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#00051:01\n#00151:01\n#ELSE\n#00251:01\n#00351:01\n#ENDIF\n#ENDRANDOM\n");
    const auto selected = chart_for(history, node_id<bms_else_scope_t>(history));
    const auto view = authoring(history, selected);
    const auto visible = notes(view.song().charts.front());
    REQUIRE(visible.size() == 2);
    for (const auto& note : visible) {
        CHECK_FALSE(note.end);
        CHECK(view.song().charts.front().is_bms_authoring_read_only(note.id));
    }
}
TEST_CASE("An unavailable authoring target never falls back to playback", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto selected = GENERATE(branch_id_t{}, branch_id_t{999999});
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty, .chart = selected};
    CHECK(history.read_chart(difficulty) == nullptr);
    const auto target = bms_program_edit_target_t::for_authoring(history.document, difficulty, selected);
    REQUIRE_FALSE(target);
    CHECK(target.error().code == failure_code_t::not_found);
}

TEST_CASE("Scalar node edits retain structural identity and publish once", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto arm = node_id<bms_else_if_scope_t>(history);
    const auto before = *owner(history).prepared().program.find(arm);
    const auto result = apply_bms_program_parameter_command(history,
        {.stamp = chart_stamp_t::from(history, difficulty), .node = arm, .value = 4});
    REQUIRE(result.applied);
    const auto& after = *owner(history).prepared().program.find(arm);
    CHECK(after.children == before.children);
    CHECK(std::get<bms_else_if_scope_t>(after.value).value == 4);
    CHECK(history.undo.size() == 1);
}
TEST_CASE("Playback preview edits do not retarget the authoring node", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto chart = chart_for(history, node_id<bms_else_if_scope_t>(history));
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty, .chart = chart};
    const auto before = owner(history).prepared().program;
    REQUIRE(apply_bms_program_preview_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .node = node_id<bms_random_scope_t>(history), .value = 2}).applied);
    CHECK(owner(history).prepared().program == before);
    CHECK(history.bms_edit_target->chart == chart);
    CHECK(owner(history).evaluation().selections.front().selected_value == 2);
}
TEST_CASE("Invalid playback choices reject without changing the program or history", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto snapshot = history.document.prepared_bms_programs.front();
    const auto result = apply_bms_program_preview_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .node = node_id<bms_random_scope_t>(history), .value = 3});
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    CHECK(history.document.prepared_bms_programs.front() == snapshot);
    CHECK(history.undo.empty());
}
TEST_CASE("Prepared native difficulties bind into the workspace without a writable mirror", "[bms-program-authoring-view]") {
    const auto history = history_from();
    auto workspace = workspace_controller_t::from(history);
    REQUIRE(workspace.value);
    CHECK(workspace.value->selected_chart == difficulty);
    const auto chart = chart_for(history, node_id<bms_else_if_scope_t>(history));
    const auto view = workspace_view_t::from(*workspace.value, history.document, workspace_tab_t::compose,
        nullptr, nullptr, nullptr, chart);
    REQUIRE(view.value);
    CHECK(std::get<workspace_compose_view_t>(view.value->tab_view).note_count == 2);
    CHECK(history.document.charts.empty());
}
TEST_CASE("A newly constructed empty Root supports an authoring view without retained source", "[bms-program-authoring-view]") {
    auto input = prepared("#BPM 120\n");
    auto program = bms_program_t::from_root(branch_id_t{100}, branch_id_t{101});
    REQUIRE(program);
    input.program = std::move(*program);
    const auto view = bms_program_projection_t::for_authoring(input, branch_id_t{101});
    REQUIRE(view.value);
    CHECK(notes(view.value->song().charts.front()).empty());
    CHECK_FALSE(view.value->song().charts.front().bms_control_tree);
    CHECK(view.value->song().charts.front().bms_authoring_chart == branch_id_t{101});
}
TEST_CASE("Captured transactions read their original authoring context after session selection changes", "[bms-program-authoring-view]") {
    auto history = history_from();
    const auto selected = chart_for(history, node_id<bms_else_if_scope_t>(history));
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty, .chart = selected};
    const auto stamp = document_stamp_t::from(history);
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty,
        .chart = chart_for(history, owner(history).prepared().program.root())};
    document_command_transaction_t transaction(history, stamp, "Captured authoring read");
    REQUIRE(transaction.source_chart(difficulty));
    CHECK(transaction.source_chart(difficulty)->bms_authoring_chart == selected);
    REQUIRE(transaction.read_chart(difficulty));
    CHECK(transaction.read_chart(difficulty)->bms_authoring_chart == selected);
    CHECK(history.read_chart(difficulty)->bms_authoring_chart != selected);
}
