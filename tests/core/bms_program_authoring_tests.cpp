#include "../support/bms_program_fixture.h"
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/native_project.h>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <nlohmann/json.hpp>

using namespace wormedit;
using namespace wormedit::test::bms_program;
namespace {
constexpr chart_id_t difficulty{9000};
auto history_from(std::string_view source = "") -> document_history_t {
    auto owner = bms_program_document_t::from(difficulty, prepared(source));
    REQUIRE(owner.value);
    return document_history_t::from(song_document_t{.id = song_id_t{9001},
        .prepared_bms_programs = {std::move(*owner.value)}});
}
auto owner(const document_history_t& history) -> const bms_program_document_t& {
    return *history.document.find_bms_program(difficulty);
}
auto program(const document_history_t& history) -> const bms_program_t& { return owner(history).prepared().program; }
auto snapshot(const document_history_t& history) -> std::string {
    const auto written = write_native_project(history.document);
    REQUIRE(written.ok());
    REQUIRE(written.value);
    return *written.value;
}
void check_history_round_trip(document_history_t& history, const bms_program_t& before,
    const bms_program_t& after) {
    REQUIRE(undo_document_command(history));
    CHECK(program(history) == before);
    REQUIRE(redo_document_command(history));
    CHECK(program(history) == after);
}
void check_refused(const document_history_t& history, const document_command_result_t& result,
    diagnostic_code_t code, const std::string& original) {
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == code);
    CHECK(snapshot(history) == original);
}
auto insert(document_history_t& history, branch_id_t parent, bms_program_insert_value_t value,
    std::optional<branch_id_t> after = {}) -> branch_id_t {
    const auto previous = program(history);
    const auto result = apply_bms_program_insert_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .parent = parent, .after = after, .value = std::move(value)});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    const auto* containing = program(history).find(parent);
    const auto anchor = after ? std::ranges::find(containing->children, *after) + 1 : containing->children.begin();
    REQUIRE(anchor != containing->children.end());
    const auto id = *anchor;
    REQUIRE_FALSE(previous.find(id));
    for (const auto& node : previous.nodes()) if (node.id != parent) CHECK(*program(history).find(node.id) == node);
    return id;
}
auto write(const document_history_t& history) -> std::string {
    for (const auto& node : program(history).nodes())
        if (node.is_scope()) REQUIRE(program(history).target_chart(node.id));
    auto result = write_bms_program(owner(history).prepared());
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.ok());
    REQUIRE(result.value);
    const auto imported = prepare_bms_program(bytes(*result.value), {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(imported.ok());
    REQUIRE(imported.value.prepared);
    return *result.value;
}
} // namespace

TEST_CASE("Empty Root supports ordered Chart siblings and one-step Undo Redo", "[bms-program-authoring]") {
    auto history = history_from();
    const auto before = program(history);
    const auto root = before.root();
    const auto original = *before.target_chart(root);
    const auto first = insert(history, root, bms_chart_content_t{});
    const auto after_first = program(history);
    const auto last = insert(history, root, bms_chart_content_t{}, original);
    CHECK(program(history).find(root)->children == std::vector<branch_id_t>{first, original, last});
    CHECK(program(history).target_chart(root) == first);
    const auto final = program(history);
    check_history_round_trip(history, after_first, final);
    REQUIRE(undo_document_command(history));
    REQUIRE(undo_document_command(history));
    CHECK(program(history) == before);
}
TEST_CASE("Typed nested RANDOM and IF chains emit automatic markers", "[bms-program-authoring]") {
    auto history = history_from("; untouched\n#WAV01 a.wav\n#00011:01\n");
    const auto random = insert(history, program(history).root(), bms_random_scope_t{.range = 3});
    const auto arm = insert(history, random, bms_if_scope_t{.value = 1});
    const auto alternate = insert(history, random, bms_else_if_scope_t{.previous_arm = arm, .value = 2}, arm);
    const auto fallback = insert(history, random, bms_else_scope_t{.previous_arm = alternate}, alternate);
    insert(history, fallback, bms_random_scope_t{.range = 2});
    insert(history, arm, bms_text_t{.bytes = "; inactive comment\n", .encoding = text_encoding_t::utf8});
    const auto output = write(history);
    CHECK(output.find("#RANDOM 3\n#IF 1") != std::string::npos);
    CHECK(output.find("#ELSEIF 2") != std::string::npos);
    CHECK(output.find("#ENDIF") != std::string::npos);
    CHECK(output.find("; inactive comment") != std::string::npos);
}
TEST_CASE("Fixed settings are childless and SKIP belongs to an anchored case", "[bms-program-authoring]") {
    auto history = history_from();
    const auto root = program(history).root();
    const auto selector = insert(history, root, bms_switch_scope_t{.value = 2});
    const auto arm = insert(history, selector, bms_case_scope_t{.value = 1});
    insert(history, selector, bms_case_scope_t{}, arm);
    const auto setting = insert(history, root, bms_set_switch_t{.value = 1});
    CHECK(program(history).find(setting)->children.empty());
    const auto random = insert(history, root, bms_random_scope_t{.range = 2}, selector);
    const auto fixed = insert(history, root, bms_set_random_t{.value = 1}, selector);
    CHECK(program(history).find(fixed)->children.empty());
    insert(history, random, bms_if_scope_t{.value = 1});
    const auto child = *program(history).target_chart(arm);
    const auto before = program(history);
    auto result = apply_bms_program_skip_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .case_node = arm, .skip = {.scope = arm, .after_child = child}});
    REQUIRE(result.applied);
    const auto after = program(history);
    const auto output = write(history);
    CHECK(output.find("#SKIP") != std::string::npos);
    CHECK(output.find("#SETSWITCH 1") < output.find("#SWITCH 2"));
    check_history_round_trip(history, before, after);
}
TEST_CASE("Opaque Text insertion and editing preserve order bytes and history", "[bms-program-authoring]") {
    auto history = history_from();
    const auto root = program(history).root();
    const auto text = insert(history, root, bms_text_t{.bytes = "; first\r\n#UNKNOWN abc\r\n", .encoding = text_encoding_t::utf8});
    const auto before = program(history);
    const auto result = apply_bms_program_text_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .node = text, .text = {.bytes = "; edited\n", .encoding = text_encoding_t::ascii}});
    REQUIRE(result.applied);
    CHECK(write(history).find("; edited\n") != std::string::npos);
    REQUIRE(undo_document_command(history));
    CHECK(program(history) == before);
    REQUIRE(redo_document_command(history));
    CHECK(std::get<bms_text_t>(program(history).find(text)->value).bytes == "; edited\n");
}
TEST_CASE("Recognized statements and invalid encodings cannot enter Text", "[bms-program-authoring]") {
    auto history = history_from();
    const auto payload = GENERATE("#WAV01 x.wav", "#BMP01 x.png", "#BPM 120", "#BPM01 150", "#STOP01 48",
        "#SCROLL01 1", "#TITLE x", "#00011:01", "#00002:1.5", "#RANDOM 2", "#SKIP", "#ENDIF", "#SETSWITCH 1", "\xff");
    const auto before = snapshot(history);
    auto result = apply_bms_program_insert_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .parent = program(history).root(), .value = bms_text_t{.bytes = payload, .encoding = text_encoding_t::utf8}});
    check_refused(history, result, diagnostic_code_t::invalid_id, before);
    CHECK(history.undo.empty());
}
TEST_CASE("Invalid placement anchors and limits reject atomically", "[bms-program-authoring]") {
    auto history = history_from();
    const auto root = program(history).root();
    const auto chart = *program(history).target_chart(root);
    const auto kind = GENERATE(0, 1, 2, 3, 4, 5, 6);
    bms_program_insert_command_t request{.stamp = chart_stamp_t::from(history, difficulty), .parent = root,
        .value = bms_random_scope_t{.range = 1}};
    if (kind == 0) request.parent = chart;
    if (kind == 1) request.after = branch_id_t{99999};
    if (kind == 2) request.value = bms_if_scope_t{.value = 1};
    if (kind == 3) request.value = bms_case_scope_t{.value = 1};
    if (kind == 4) request.limits.maximum_nodes = program(history).nodes().size() + 1;
    if (kind == 5) request.limits.maximum_depth = 2;
    if (kind == 6) request.value = bms_random_scope_t{.range = 0};
    const auto before = snapshot(history);
    const auto result = apply_bms_program_insert_command(history, request);
    check_refused(history, result, diagnostic_code_t::invalid_id, before);
    CHECK(history.undo.empty());
}
TEST_CASE("Stale structural captures cannot publish and Redo IDs are reserved", "[bms-program-authoring]") {
    auto history = history_from();
    const auto root = program(history).root();
    const auto stamp = chart_stamp_t::from(history, difficulty);
    const auto first = insert(history, root, bms_random_scope_t{.range = 1});
    auto result = apply_bms_program_insert_command(history, {.stamp = stamp, .parent = root, .value = bms_chart_content_t{}});
    CHECK_FALSE(result.applied);
    REQUIRE(undo_document_command(history));
    const auto second = insert(history, root, bms_random_scope_t{.range = 1});
    CHECK(second.value > first.value + 1);
    CHECK(history.redo.empty());
}
TEST_CASE("Text edits and typed SKIP reject wrong owners and foreign anchors", "[bms-program-authoring]") {
    auto history = history_from();
    const auto root = program(history).root();
    const auto text = insert(history, root, bms_text_t{.bytes = "; kept\n", .encoding = text_encoding_t::ascii});
    const auto before = snapshot(history);
    auto result = apply_bms_program_text_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .node = text, .text = {.bytes = "#TITLE hidden\n", .encoding = text_encoding_t::utf8}});
    check_refused(history, result, diagnostic_code_t::invalid_id, before);
    result = apply_bms_program_text_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .node = root, .text = {.bytes = "; wrong owner\n", .encoding = text_encoding_t::ascii}});
    check_refused(history, result, diagnostic_code_t::invalid_id, before);
    result = apply_bms_program_skip_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .case_node = root, .skip = {.scope = root}});
    check_refused(history, result, diagnostic_code_t::invalid_id, before);
    const auto selector = insert(history, root, bms_switch_scope_t{.value = 2});
    const auto arm = insert(history, selector, bms_case_scope_t{.value = 1});
    const auto original_snapshot = snapshot(history);
    result = apply_bms_program_skip_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .case_node = arm, .skip = {.scope = arm, .after_child = text}});
    check_refused(history, result, diagnostic_code_t::invalid_id, original_snapshot);
}
TEST_CASE("Insertion cannot split an existing IF continuation chain", "[bms-program-authoring]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    branch_id_t random, first;
    for (const auto& node : program(history).nodes()) {
        if (std::holds_alternative<bms_random_scope_t>(node.value)) random = node.id;
        if (std::holds_alternative<bms_if_scope_t>(node.value)) first = node.id;
    }
    const auto original_snapshot = snapshot(history);
    const auto result = apply_bms_program_insert_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .parent = random, .after = first, .value = bms_chart_content_t{}});
    CHECK_FALSE(result.applied);
    CHECK(snapshot(history) == original_snapshot);
}
TEST_CASE("Authored Text budgets and program evaluation caps reject without history", "[bms-program-authoring]") {
    auto history = history_from();
    const auto mode = GENERATE(0, 1, 2);
    bms_program_insert_command_t request{.stamp = chart_stamp_t::from(history, difficulty),
        .parent = program(history).root(), .value = bms_text_t{.bytes = "; comment\n", .encoding = text_encoding_t::ascii}};
    if (mode == 0) request.limits.maximum_text_bytes = 2;
    if (mode == 1) request.value = bms_text_t{.bytes = std::string(bms_text_t::maximum_line_bytes + 1, ';'), .encoding = text_encoding_t::ascii};
    if (mode == 2) request.value = bms_random_scope_t{.range = 257};
    const auto original_snapshot = snapshot(history);
    const auto result = apply_bms_program_insert_command(history, std::move(request));
    check_refused(history, result, (mode == 2 ? diagnostic_code_t::unsupported_feature : diagnostic_code_t::invalid_id), original_snapshot);
    CHECK(history.undo.empty());
}
TEST_CASE("Authored node identities and Text survive native persistence", "[bms-program-authoring]") {
    auto history = history_from();
    const auto random = insert(history, program(history).root(), bms_random_scope_t{.range = 2});
    const auto arm = insert(history, random, bms_if_scope_t{.value = 1});
    insert(history, arm, bms_text_t{.bytes = "; hidden\r\n#UNKNOWN a\r\n", .encoding = text_encoding_t::ascii});
    const auto serialized = write_native_project(history.document);
    REQUIRE(serialized.ok());
    REQUIRE(serialized.value);
    const auto read = parse_native_project(*serialized.value);
    REQUIRE(read.ok());
    REQUIRE(read.value);
    CHECK(read.value->find_bms_program(difficulty)->prepared().program == program(history));
}
TEST_CASE("Hidden fixed-selector settings validate their implicit outcomes", "[bms-program-authoring]") {
    const auto use_switch = GENERATE(false, true);
    auto history = history_from(use_switch ?
        "#RANDOM 2\n#IF 1\n#SWITCH 2\n#CASE 1\n#ENDSW\n#ENDIF\n#ENDRANDOM\n" :
        "#RANDOM 2\n#IF 1\n#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n#ENDIF\n#ENDRANDOM\n");
    branch_id_t first_arm;
    for (const auto& node : program(history).nodes())
        if (std::holds_alternative<bms_if_scope_t>(node.value) && !first_arm) first_arm = node.id;
    REQUIRE(first_arm);
    const auto original_snapshot = snapshot(history);
    const auto result = apply_bms_program_insert_command(history, {.stamp = chart_stamp_t::from(history, difficulty),
        .parent = first_arm, .value = use_switch ? bms_program_insert_value_t{bms_set_switch_t{.value = 999}} :
            bms_program_insert_value_t{bms_set_random_t{.value = 999}}});
    check_refused(history, result, diagnostic_code_t::invalid_binding, original_snapshot);
    CHECK(history.undo.empty());
}

namespace {
template<class Value> auto nodes_of(const document_history_t& history) -> std::vector<branch_id_t> {
    std::vector<branch_id_t> ids;
    for (const auto& node : program(history).nodes()) if (std::holds_alternative<Value>(node.value)) ids.push_back(node.id);
    return ids;
}
auto apply_move(document_history_t& history, bms_program_move_t move) -> bms_program_move_report_t {
    const auto before = program(history);
    auto proposed = bms_program_move_edit_t::propose(history.document, difficulty, move);
    for (const auto& diagnostic : proposed.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(proposed.value);
    const auto report = proposed.value->payload.report;
    const auto result = apply_proposal(history, *proposed.value);
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    for (const auto& node : before.nodes()) CHECK(program(history).find(node.id)->value == node.value);
    return report;
}
auto selected_move_history(std::string_view source) -> document_history_t {
    auto input = prepared(source);
    bms_program_evaluation_t evaluation;
    for (const auto id : selectors(input.program)) evaluation.selections.push_back({.branch = id, .selected_value = 1});
    auto selected = bms_program_document_t::from(difficulty, std::move(input), std::move(evaluation));
    REQUIRE(selected.value);
    return document_history_t::from(song_document_t{.id = song_id_t{9001},
        .prepared_bms_programs = {std::move(*selected.value)}});
}
void check_move_refused(document_history_t& history, bms_program_move_t move,
    diagnostic_code_t expected = diagnostic_code_t::invalid_id) {
    const auto original = snapshot(history);
    const auto undo_count = history.undo.size();
    const auto proposed = bms_program_move_edit_t::propose(history.document, difficulty, move);
    CHECK_FALSE(proposed.value);
    REQUIRE_FALSE(proposed.diagnostics.empty());
    CHECK(proposed.diagnostics.front().code == expected);
    CHECK(snapshot(history) == original);
    CHECK(history.undo.size() == undo_count);
}
} // namespace

TEST_CASE("Scoped moves reorder Chart and Text siblings without rebasing content", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#00011:01\n; ordered\n#00112:01\n");
    const auto root = program(history).root();
    const auto charts = nodes_of<bms_chart_content_t>(history);
    const auto text = nodes_of<bms_text_t>(history).front();
    const auto before = program(history);
    const auto report = apply_move(history, {.node = charts.back(), .parent = root});
    CHECK(report.subtree == std::vector<branch_id_t>{charts.back()});
    CHECK(report.potentially_affected_charts.size() == charts.size());
    CHECK_FALSE(report.potentially_affected_definitions.empty());
    const auto after = program(history);
    check_history_round_trip(history, before, after);
    apply_move(history, {.node = text, .parent = root, .after = charts.back()});
    CHECK(program(history).find(root)->children[1] == text);
    CHECK(write(history).find("; ordered") != std::string::npos);
}
TEST_CASE("Scoped move preparation reports a changed preview without publishing", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#00011:01\n; boundary\n#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n");
    const auto chart = *program(history).target_chart(program(history).root());
    const auto arm = nodes_of<bms_if_scope_t>(history).front();
    insert(history, program(history).root(), bms_chart_content_t{}, nodes_of<bms_random_scope_t>(history).front());
    const auto original = snapshot(history);
    const auto undo_count = history.undo.size();
    const auto proposed = bms_program_move_edit_t::propose(history.document, difficulty, {.node = chart, .parent = arm});
    REQUIRE(proposed.value);
    CHECK(snapshot(history) == original);
    CHECK(history.undo.size() == undo_count);
    const auto& report = proposed.value->payload.report;
    CHECK(std::ranges::find(report.before_active_charts, chart) != report.before_active_charts.end());
    CHECK(std::ranges::find(report.after_active_charts, chart) == report.after_active_charts.end());
}
TEST_CASE("Scoped moves carry nested controls comments holds and scoped declarations", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#WAV02 b.wav\n; travels\n#00051:0202\n#ENDIF\n#ENDRANDOM\n");
    const auto root = program(history).root();
    const auto moving = nodes_of<bms_random_scope_t>(history).front();
    const auto destination = insert(history, root, bms_random_scope_t{.range = 2}, moving);
    const auto before = program(history);
    const auto report = apply_move(history, {.node = moving, .parent = destination});
    CHECK(report.subtree.size() > 4);
    CHECK(program(history).parent(moving) == destination);
    CHECK(write(history).find("; travels") != std::string::npos);
    const auto after = program(history);
    check_history_round_trip(history, before, after);
    const auto reloaded = parse_native_project(snapshot(history));
    REQUIRE(reloaded.value);
    CHECK(reloaded.value->find_bms_program(difficulty)->prepared().program == after);
}
TEST_CASE("Scoped moves reject cycles Root leaf parents foreign anchors and the last Chart", "[bms-program-authoring]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n");
    const auto root = program(history).root();
    const auto random = nodes_of<bms_random_scope_t>(history).front();
    const auto arm = nodes_of<bms_if_scope_t>(history).front();
    const auto chart = *program(history).target_chart(root);
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5);
    bms_program_move_t move{.node = random, .parent = root};
    if (mode == 0) move.node = root;
    if (mode == 1) move.parent = arm;
    if (mode == 2) move.parent = chart;
    if (mode == 3) move.after = *program(history).target_chart(arm);
    if (mode == 4) move = {.node = chart, .parent = arm};
    if (mode == 5) move.after = random;
    check_move_refused(history, move);
}
TEST_CASE("Scoped moves never rebind IF CASE or anchored SKIP", "[bms-program-authoring]") {
    const auto mode = GENERATE(0, 1, 2);
    auto history = history_from(mode == 0 ?
        "#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n#RANDOM 2\n#ENDRANDOM\n" :
        "#SWITCH 2\n#CASE 1\n; anchor\n#SKIP\n#ENDSW\n#SWITCH 2\n#DEF\n#ENDSW\n");
    if (mode == 0) {
        check_move_refused(history, {.node = nodes_of<bms_if_scope_t>(history).front(),
            .parent = nodes_of<bms_random_scope_t>(history).back()});
    } else if (mode == 1) {
        check_move_refused(history, {.node = nodes_of<bms_case_scope_t>(history).front(),
            .parent = nodes_of<bms_switch_scope_t>(history).back()});
    } else {
        check_move_refused(history, {.node = nodes_of<bms_text_t>(history).front(),
            .parent = program(history).root()});
    }
}
TEST_CASE("Scoped moves preserve IF continuation adjacency", "[bms-program-authoring]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    check_move_refused(history, {.node = nodes_of<bms_if_scope_t>(history).front(),
        .parent = nodes_of<bms_random_scope_t>(history).front(),
        .after = nodes_of<bms_else_scope_t>(history).front()});
}
TEST_CASE("Scoped moves reject unresolved references in hidden outcomes", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n; boundary\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00012:01\n#ENDIF\n#ENDRANDOM\n");
    const auto declaration = *program(history).target_chart(program(history).root());
    insert(history, program(history).root(), bms_chart_content_t{}, nodes_of<bms_random_scope_t>(history).front());
    check_move_refused(history, {.node = declaration, .parent = nodes_of<bms_if_scope_t>(history).front()},
        diagnostic_code_t::dangling_resource);
}
TEST_CASE("Scoped moves refuse unproven fixed-selector rebinding and allow Text order", "[bms-program-authoring]") {
    auto history = history_from("#SETRANDOM 1\n; movable\n#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n");
    const auto root = program(history).root();
    const auto random = nodes_of<bms_random_scope_t>(history).front();
    const auto fixed = nodes_of<bms_set_random_t>(history).front();
    check_move_refused(history, {.node = fixed, .parent = root, .after = random});
    const auto text = nodes_of<bms_text_t>(history).front();
    apply_move(history, {.node = fixed, .parent = root, .after = text});
    apply_move(history, {.node = text, .parent = root, .after = random});
    const auto& children = program(history).find(root)->children;
    CHECK(*(std::ranges::find(children, random) + 1) == text);
}
TEST_CASE("Scoped move proposals expire after another edit and unchanged order has no Undo", "[bms-program-authoring]") {
    auto history = history_from("; movable\n");
    const auto root = program(history).root();
    const auto chart = *program(history).target_chart(root);
    const auto text = nodes_of<bms_text_t>(history).front();
    auto proposed = bms_program_move_edit_t::propose(history.document, difficulty, {.node = text, .parent = root, .after = chart});
    REQUIRE(proposed.value);
    insert(history, root, bms_chart_content_t{});
    const auto original = snapshot(history);
    check_refused(history, apply_proposal(history, *proposed.value), diagnostic_code_t::invalid_id, original);
    const auto undo_count = history.undo.size();
    bms_program_move_proposal_t unchanged{.stamp = chart_stamp_t::from(history, difficulty),
        .payload = {.move = {.node = chart, .parent = root, .after = text}}};
    CHECK_FALSE(apply_proposal(history, unchanged).applied);
    CHECK(history.undo.size() == undo_count);
}
TEST_CASE("Scoped moves reject hidden collisions and broken cross-owner holds", "[bms-program-authoring]") {
    const auto hold = GENERATE(false, true);
    auto history = selected_move_history(hold ?
        "#WAV01 a.wav\n#00051:01\n#RANDOM 2\n#IF 1\n#00151:01\n#ELSE\n#00151:01\n#ENDIF\n#ENDRANDOM\n" :
        "#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00011:01\n#ENDIF\n#ENDRANDOM\n");
    const auto arm = nodes_of<bms_if_scope_t>(history).front();
    const auto moving = *program(history).target_chart(arm);
    insert(history, arm, bms_chart_content_t{}, moving);
    check_move_refused(history, {.node = moving, .parent = program(history).root()},
        hold ? diagnostic_code_t::invalid_binding : diagnostic_code_t::collision);
}
TEST_CASE("Scoped moves respect the resulting graph depth limit", "[bms-program-authoring]") {
    auto history = history_from("#RANDOM 2\n#ENDRANDOM\n#RANDOM 2\n#ENDRANDOM\n");
    const auto randoms = nodes_of<bms_random_scope_t>(history);
    const auto original = snapshot(history);
    const auto proposal = bms_program_move_edit_t::propose(history.document, difficulty,
        {.node = randoms.front(), .parent = randoms.back()}, {.maximum_depth = 3});
    CHECK_FALSE(proposal.value);
    REQUIRE_FALSE(proposal.diagnostics.empty());
    CHECK(proposal.diagnostics.front().code == diagnostic_code_t::invalid_id);
    CHECK(snapshot(history) == original);
}
TEST_CASE("Scoped CASE subtree reorder retains anchored SKIP and authored output order", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#SWITCH 2\n#CASE 1\n#00011:01\n#SKIP\n#CASE 2\n#00112:01\n#SKIP\n#DEF\n#ENDSW\n");
    const auto selector = nodes_of<bms_switch_scope_t>(history).front();
    const auto cases = nodes_of<bms_case_scope_t>(history);
    const auto before = program(history);
    apply_move(history, {.node = cases[1], .parent = selector});
    const auto output = write(history);
    CHECK(output.find("#CASE 2") < output.find("#CASE 1"));
    const auto after = program(history);
    check_history_round_trip(history, before, after);
}

namespace {
constexpr auto deletion_source = "#WAV01 a.wav\n#RANDOM 2\n#IF 1\n; delete text\n#WAV02 b.wav\n#00011:02\n#ENDIF\n#ENDRANDOM\n; keep\n#00112:01\n";
void check_bms_target(const document_history_t& history, branch_id_t expected) {
    REQUIRE(history.bms_edit_target);
    CHECK(history.bms_edit_target->chart == expected);
    CHECK(program(history).find(expected));
}
auto prepare_delete(const document_history_t& history, branch_id_t node) -> bms_program_delete_proposal_t {
    const auto original = snapshot(history);
    const auto undo_count = history.undo.size();
    const auto target = history.bms_edit_target;
    auto prepared = bms_program_delete_edit_t::propose(history.document, difficulty, node);
    for (const auto& diagnostic : prepared.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(prepared.value);
    CHECK(snapshot(history) == original);
    CHECK(history.undo.size() == undo_count);
    CHECK(history.bms_edit_target == target);
    return std::move(*prepared.value);
}
auto catalog_snapshot(const document_history_t& history) -> nlohmann::json {
    const auto encoded = nlohmann::json::parse(snapshot(history));
    const auto& document = encoded.at("document");
    const auto& scoped = document.at("prepared_bms_programs").at(0);
    nlohmann::json result;
    for (const auto* field : {"resources", "audio_assets", "visual_assets"}) {
        result["shared"][field] = document.at(field);
        result["scoped"][field] = scoped.at(field);
    }
    return result;
}
void check_deleted_subtree(const bms_program_t& before, const bms_program_t& after,
    const bms_program_subtree_t& summary) {
    auto expected = std::vector<bms_program_node_t>(before.nodes().begin(), before.nodes().end());
    std::erase_if(expected, [&](const auto& node) {
        return std::ranges::find(summary.nodes, node.id) != summary.nodes.end();
    });
    const auto parent = std::ranges::find(expected, *summary.parent, &bms_program_node_t::id);
    REQUIRE(parent != expected.end());
    std::erase(parent->children, summary.root);
    CHECK(std::ranges::equal(expected, after.nodes()));
}
void check_delete_refused(const document_history_t& history, branch_id_t node,
    diagnostic_code_t code = diagnostic_code_t::invalid_id) {
    const auto original = snapshot(history);
    const auto target = history.bms_edit_target;
    const auto proposed = bms_program_delete_edit_t::propose(history.document, difficulty, node);
    CHECK_FALSE(proposed.value);
    REQUIRE_FALSE(proposed.diagnostics.empty());
    CHECK(proposed.diagnostics.front().code == code);
    CHECK(snapshot(history) == original);
    CHECK(history.bms_edit_target == target);
}
} // namespace

TEST_CASE("Scoped deletion previews complete typed counts without publishing", "[bms-program-authoring]") {
    const auto history = selected_move_history(deletion_source);
    const auto random = nodes_of<bms_random_scope_t>(history).front();
    const auto proposed = prepare_delete(history, random);
    const auto& summary = proposed.payload.summary();
    CHECK(summary.root == random);
    CHECK(summary.parent == program(history).root());
    CHECK(summary.nodes.size() == 5);
    const auto counts = std::array{summary.scopes, summary.charts, summary.texts,
        summary.settings, summary.content_items, summary.text_bytes};
    CHECK(counts == std::array<std::size_t, 6>{2, 2, 1, 0, 2, 14});
}
TEST_CASE("Confirmed scoped deletion removes exactly its subtree and retains assets", "[bms-program-authoring]") {
    auto history = selected_move_history(deletion_source);
    const auto before = program(history);
    const auto catalog = snapshot(history);
    auto proposed = prepare_delete(history, nodes_of<bms_random_scope_t>(history).front());
    const auto pools = catalog_snapshot(history);
    const auto result = apply_proposal(history, proposed, bms_program_delete_decision_t::confirm);
    REQUIRE(result.applied);
    check_deleted_subtree(before, program(history), proposed.payload.summary());
    CHECK(catalog_snapshot(history) == pools);
    CHECK(owner(history).evaluation().selections.empty());
    const auto after = program(history);
    check_history_round_trip(history, before, after);
    REQUIRE(undo_document_command(history));
    CHECK(snapshot(history) == catalog);
}
TEST_CASE("Canceling scoped deletion changes neither document target nor history", "[bms-program-authoring]") {
    auto history = selected_move_history(deletion_source);
    const auto random = nodes_of<bms_random_scope_t>(history).front();
    history.bms_edit_target = {.difficulty = difficulty, .chart = *program(history).target_chart(random)};
    auto proposed = prepare_delete(history, random);
    const auto original = snapshot(history);
    const auto selected = history.bms_edit_target;
    const auto undo_count = history.undo.size();
    CHECK_FALSE(apply_proposal(history, proposed, bms_program_delete_decision_t::cancel).applied);
    CHECK(snapshot(history) == original);
    CHECK(history.bms_edit_target == selected);
    CHECK(history.undo.size() == undo_count);
}
TEST_CASE("Scoped deletion repairs a deleted target through Commit Undo and Redo", "[bms-program-authoring]") {
    auto history = selected_move_history(deletion_source);
    const auto random = nodes_of<bms_random_scope_t>(history).front();
    const auto selected = *program(history).target_chart(nodes_of<bms_if_scope_t>(history).front());
    const auto fallback = *program(history).target_chart(program(history).root());
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected};
    auto proposed = prepare_delete(history, random);
    REQUIRE(apply_proposal(history, proposed, bms_program_delete_decision_t::confirm).applied);
    check_bms_target(history, fallback);
    REQUIRE(undo_document_command(history));
    CHECK(program(history).find(selected));
    check_bms_target(history, fallback);
    history.bms_edit_target->chart = selected;
    REQUIRE(redo_document_command(history));
    check_bms_target(history, fallback);
}
TEST_CASE("Scoped deletion retains unrelated targets and chooses the first surviving Chart", "[bms-program-authoring]") {
    auto history = history_from("; split\n");
    const auto root = program(history).root();
    const auto surviving = *program(history).target_chart(root);
    const auto removed = insert(history, root, bms_chart_content_t{}, nodes_of<bms_text_t>(history).front());
    const auto mode = GENERATE(0, 1, 2);
    history.bms_edit_target = {.difficulty = difficulty, .chart = mode == 0 ? surviving : mode == 1 ? removed : branch_id_t{99999}};
    auto proposed = prepare_delete(history, removed);
    REQUIRE(apply_proposal(history, proposed, bms_program_delete_decision_t::confirm).applied);
    const auto expected = mode == 2 ? std::nullopt : std::optional{bms_program_target_key_t{
        .difficulty = difficulty, .chart = surviving}};
    CHECK(history.bms_edit_target == expected);
    CHECK(program(history).target_chart(root) == surviving);
}
TEST_CASE("Scoped deletion refuses Root missing nodes and the last surviving Chart", "[bms-program-authoring]") {
    auto history = history_from();
    const auto mode = GENERATE(0, 1, 2);
    const auto root = program(history).root();
    const auto node = mode == 0 ? root : mode == 1 ? *program(history).target_chart(root) : branch_id_t{99999};
    history.bms_edit_target = {.difficulty = difficulty, .chart = *program(history).target_chart(root)};
    check_delete_refused(history, node);
}
TEST_CASE("Scoped deletion refuses dangling IF continuations and SKIP anchors", "[bms-program-authoring]") {
    const auto skip = GENERATE(false, true);
    auto history = history_from(skip ? "#SWITCH 2\n#CASE 1\n; anchored\n#SKIP\n#ENDSW\n" :
        "#RANDOM 2\n#IF 1\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    const auto node = skip ? nodes_of<bms_text_t>(history).front() : nodes_of<bms_if_scope_t>(history).front();
    check_delete_refused(history, node);
}
TEST_CASE("Scoped deletion refuses hidden unresolved references atomically", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n; split\n#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#ENDRANDOM\n");
    const auto root = program(history).root();
    const auto declarations = *program(history).target_chart(root);
    insert(history, root, bms_chart_content_t{}, nodes_of<bms_random_scope_t>(history).front());
    check_delete_refused(history, declarations, diagnostic_code_t::dangling_resource);
}
TEST_CASE("Scoped deletion refuses a partial cross-owner hold without selection repair", "[bms-program-authoring]") {
    auto history = selected_move_history("#WAV01 a.wav\n#00051:01\n#RANDOM 1\n#IF 1\n#00151:01\n#ENDIF\n#ENDRANDOM\n");
    const auto arm = nodes_of<bms_if_scope_t>(history).front();
    const auto tail = *program(history).target_chart(arm);
    insert(history, arm, bms_chart_content_t{}, tail);
    history.bms_edit_target = {.difficulty = difficulty, .chart = tail};
    check_delete_refused(history, tail);
}
TEST_CASE("Scoped deletion confirmation rejects stale and forged stamps", "[bms-program-authoring]") {
    auto history = selected_move_history(deletion_source);
    auto proposed = prepare_delete(history, nodes_of<bms_random_scope_t>(history).front());
    insert(history, program(history).root(), bms_chart_content_t{});
    const auto original = snapshot(history);
    check_refused(history, apply_proposal(history, proposed, bms_program_delete_decision_t::confirm),
        diagnostic_code_t::invalid_id, original);
    proposed.stamp = chart_stamp_t::from(history, difficulty);
    check_refused(history, apply_proposal(history, proposed, bms_program_delete_decision_t::confirm),
        diagnostic_code_t::invalid_id, original);
}
TEST_CASE("Scoped deletion of Text and fixed settings is exact and persists natively", "[bms-program-authoring]") {
    auto history = history_from("#SETRANDOM 1\n; comment\n#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n");
    const auto setting = GENERATE(false, true);
    const auto node = setting ? nodes_of<bms_set_random_t>(history).front() : nodes_of<bms_text_t>(history).front();
    auto proposed = prepare_delete(history, node);
    const auto before = program(history);
    REQUIRE(apply_proposal(history, proposed, bms_program_delete_decision_t::confirm).applied);
    check_deleted_subtree(before, program(history), proposed.payload.summary());
    const auto parsed = parse_native_project(snapshot(history));
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value);
    CHECK(parsed.value->find_bms_program(difficulty)->prepared().program == program(history));
}
TEST_CASE("Scoped deletion removes a whole CASE with its typed SKIP behavior", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#SWITCH 2\n#CASE 1\n#00011:01\n#SKIP\n#CASE 2\n#00112:01\n#SKIP\n#DEF\n#ENDSW\n");
    const auto before = program(history);
    auto proposed = prepare_delete(history, nodes_of<bms_case_scope_t>(history).front());
    CHECK(proposed.payload.summary().content_items == 2);
    REQUIRE(apply_proposal(history, proposed, bms_program_delete_decision_t::confirm).applied);
    check_deleted_subtree(before, program(history), proposed.payload.summary());
    CHECK(write(history).find("#CASE 1") == std::string::npos);
    const auto after = program(history);
    check_history_round_trip(history, before, after);
}

namespace {
auto prepare_unwrap(const document_history_t& history, branch_id_t node) -> bms_program_unwrap_proposal_t {
    const auto original = snapshot(history);
    auto proposed = bms_program_unwrap_edit_t::propose(history.document, difficulty, node);
    for (const auto& diagnostic : proposed.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(proposed.value);
    CHECK(snapshot(history) == original);
    return std::move(*proposed.value);
}
void check_unwrap_refused(const document_history_t& history, branch_id_t node,
    diagnostic_code_t expected = diagnostic_code_t::invalid_id) {
    const auto original = snapshot(history);
    const auto result = bms_program_unwrap_edit_t::propose(history.document, difficulty, node);
    CHECK_FALSE(result.value);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == expected);
    CHECK(snapshot(history) == original);
}
void check_unwrapped_nodes(const bms_program_t& before, const bms_program_t& after,
    const bms_program_unwrap_report_t& report) {
    const auto removed = report.contents.root;
    REQUIRE_FALSE(after.find(removed));
    CHECK(after.nodes().size() + 1 == before.nodes().size());
    for (const auto& node : before.nodes()) {
        if (node.id == removed || node.id == report.contents.parent) continue;
        auto expected = node;
        if (std::ranges::find(report.unconditional_branches, node.id) != report.unconditional_branches.end())
            expected.value = bms_bare_scope_t{};
        CHECK(*after.find(node.id) == expected);
    }
    const auto parent = *report.contents.parent;
    auto children = before.find(parent)->children;
    const auto index = std::ranges::find(children, removed) - children.begin();
    children.erase(children.begin() + index);
    const auto& promoted = before.find(removed)->children;
    children.insert(children.begin() + index, promoted.begin(), promoted.end());
    CHECK(after.find(parent)->children == children);
}
void check_unwrap_reload(const document_history_t& history) {
    const auto loaded = parse_native_project(snapshot(history));
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value);
    CHECK(loaded.value->schema_version == current_document_schema_version);
    CHECK(loaded.value->find_bms_program(difficulty)->prepared().program == program(history));
}
void check_bare_export_equivalence(const document_history_t& history, const std::string& output) {
    const auto flattened = write_bms_program(owner(history).prepared(),
        {.format = {.branch_policy = bms_branch_policy_t::flatten_active}});
    REQUIRE(flattened.ok());
    CHECK(flattened.value == output);
    const auto imported = prepared(output);
    const auto projected = bms_program_projection_t::from(imported);
    REQUIRE(projected.value);
    CHECK(note_keys(projected.value->song()).size() == 2);
}
constexpr auto unwrap_source = "#WAV01 a.wav\n#RANDOM 2\n#IF 1\n; first\n#00011:01\n#ELSE\n; second\n#00111:01\n#ENDIF\n#ENDRANDOM\n";
}
TEST_CASE("Selector unwrap requires explicit unconditional conversion and preserves every body", "[bms-program-authoring]") {
    auto history = selected_move_history(unwrap_source);
    const auto selector = nodes_of<bms_random_scope_t>(history).front();
    const auto before = program(history);
    const auto catalogs = catalog_snapshot(history);
    auto proposed = prepare_unwrap(history, selector);
    REQUIRE(proposed.payload.report().unconditional_branches.size() == 2);
    const auto original = snapshot(history);
    check_refused(history, apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_promotion),
        diagnostic_code_t::invalid_id, original);
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    check_unwrapped_nodes(before, program(history), proposed.payload.report());
    CHECK(catalog_snapshot(history) == catalogs);
    CHECK(owner(history).evaluation().selections.empty());
    CHECK(note_keys(owner(history).build_projection().value->song()).size() == 2);
    const auto after = program(history);
    check_history_round_trip(history, before, after);
    check_unwrap_reload(history);
}
TEST_CASE("Unwrapped selector exports all bare bodies and Text in order", "[bms-program-authoring]") {
    auto history = history_from(unwrap_source);
    auto proposed = prepare_unwrap(history, nodes_of<bms_random_scope_t>(history).front());
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    const auto output = write(history);
    CHECK(output.find("#RANDOM") == std::string::npos);
    CHECK(output.find("#IF") == std::string::npos);
    CHECK(output.find("; first") < output.find("; second"));
    check_bare_export_equivalence(history, output);
}
TEST_CASE("Unwrap Cancel preserves graph target choices and Undo", "[bms-program-authoring]") {
    auto history = selected_move_history(unwrap_source);
    const auto selector = nodes_of<bms_random_scope_t>(history).front();
    history.bms_edit_target = {.difficulty = difficulty, .chart = *program(history).target_chart(selector)};
    const auto original = snapshot(history);
    const auto target = history.bms_edit_target;
    const auto count = history.undo.size();
    auto proposed = prepare_unwrap(history, selector);
    CHECK_FALSE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::cancel).applied);
    CHECK(snapshot(history) == original);
    CHECK(history.bms_edit_target == target);
    CHECK(history.undo.size() == count);
}
TEST_CASE("Unwrap rejects stale or retargeted confirmation", "[bms-program-authoring]") {
    auto history = history_from(unwrap_source);
    auto proposed = prepare_unwrap(history, nodes_of<bms_random_scope_t>(history).front());
    insert(history, program(history).root(), bms_chart_content_t{});
    const auto original = snapshot(history);
    check_refused(history, apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion),
        diagnostic_code_t::invalid_id, original);
    proposed.stamp = chart_stamp_t::from(history, difficulty);
    check_refused(history, apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion),
        diagnostic_code_t::invalid_id, original);
}
TEST_CASE("Unwrapping an IF promotes its children and makes following arms bare", "[bms-program-authoring]") {
    auto history = history_from(unwrap_source);
    const auto before = program(history);
    auto proposed = prepare_unwrap(history, nodes_of<bms_if_scope_t>(history).front());
    REQUIRE(proposed.payload.report().unconditional_branches.size() == 1);
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    check_unwrapped_nodes(before, program(history), proposed.payload.report());
    const auto after = program(history);
    check_history_round_trip(history, before, after);
    CHECK(write(history).find("#ELSE") == std::string::npos);
}
TEST_CASE("Ordinary body unwrap preserves selector bindings and target identity", "[bms-program-authoring]") {
    auto history = selected_move_history("#WAV01 a.wav\n#RANDOM 1\n#IF 1\n; nested\n#00011:01\n#ENDIF\n#ENDRANDOM\n");
    const auto arm = nodes_of<bms_if_scope_t>(history).front();
    const auto chart = *program(history).target_chart(arm);
    history.bms_edit_target = {.difficulty = difficulty, .chart = chart};
    const auto before = program(history);
    auto proposed = prepare_unwrap(history, arm);
    CHECK(proposed.payload.report().unconditional_branches.empty());
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_promotion).applied);
    check_unwrapped_nodes(before, program(history), proposed.payload.report());
    check_bms_target(history, chart);
    const auto after = program(history);
    check_history_round_trip(history, before, after);
}
TEST_CASE("Selector unwrap converts only its own dependents including nested IF scopes", "[bms-program-authoring]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#IF 2\n#ENDIF\n#RANDOM 3\n#IF 3\n#ENDIF\n#ENDRANDOM\n#ENDIF\n#ENDRANDOM\n");
    const auto selectors = nodes_of<bms_random_scope_t>(history);
    const auto inner = selectors.back();
    const auto before = program(history);
    auto proposed = prepare_unwrap(history, selectors.front());
    CHECK(proposed.payload.report().unconditional_branches.size() == 2);
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    check_unwrapped_nodes(before, program(history), proposed.payload.report());
    CHECK(*program(history).find(inner) == *before.find(inner));
    CHECK(nodes_of<bms_if_scope_t>(history).size() == 1);
    CHECK(write(history).find("#RANDOM 3") != std::string::npos);
}
TEST_CASE("Inner selector unwrap never rebinds its arms to an outer RANDOM", "[bms-program-authoring]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#RANDOM 3\n#IF 3\n#ELSE\n#ENDIF\n#ENDRANDOM\n#ENDIF\n#ENDRANDOM\n");
    const auto selectors = nodes_of<bms_random_scope_t>(history);
    auto proposed = prepare_unwrap(history, selectors.back());
    const auto outer_arm = nodes_of<bms_if_scope_t>(history).front();
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    CHECK(program(history).conditional_selector(outer_arm) == selectors.front());
    for (const auto id : proposed.payload.report().unconditional_branches)
        CHECK_FALSE(program(history).conditional_selector(id));
    CHECK(nodes_of<bms_bare_scope_t>(history).size() == 2);
}
TEST_CASE("SWITCH unwrap makes every CASE and DEF bare and reports removed SKIP behavior", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#SWITCH 2\n#CASE 1\n#00011:01\n#SKIP\n#DEF\n#00111:01\n#ENDSW\n");
    const auto before = program(history);
    auto proposed = prepare_unwrap(history, nodes_of<bms_switch_scope_t>(history).front());
    CHECK(proposed.payload.report().removed_skips == 1);
    CHECK(proposed.payload.report().unconditional_branches.size() == 2);
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    check_unwrapped_nodes(before, program(history), proposed.payload.report());
    CHECK(note_keys(owner(history).build_projection().value->song()).size() == 2);
    CHECK(write(history).find("#SKIP") == std::string::npos);
    check_unwrap_reload(history);
    const auto after = program(history);
    check_history_round_trip(history, before, after);
}
TEST_CASE("Bare scopes can subsequently be unwrapped without merging Chart owners", "[bms-program-authoring]") {
    auto history = history_from(unwrap_source);
    auto selector = prepare_unwrap(history, nodes_of<bms_random_scope_t>(history).front());
    REQUIRE(apply_proposal(history, selector, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    auto bare = prepare_unwrap(history, nodes_of<bms_bare_scope_t>(history).front());
    const auto before = program(history);
    REQUIRE(apply_proposal(history, bare, bms_program_unwrap_decision_t::confirm_promotion).applied);
    check_unwrapped_nodes(before, program(history), bare.payload.report());
    CHECK(note_keys(owner(history).build_projection().value->song()).size() == 2);
    check_unwrap_reload(history);
}
TEST_CASE("Unwrap refuses Root leaves missing targets fixed selectors and insufficient limits", "[bms-program-authoring]") {
    const auto mode = GENERATE(0, 1, 2, 3, 4);
    auto history = history_from(mode == 3 ? "#SETRANDOM 1\n#RANDOM 2\n#IF 1\n#ENDIF\n#ENDRANDOM\n" : unwrap_source);
    const auto root = program(history).root();
    if (mode == 0) check_unwrap_refused(history, root);
    if (mode == 1) check_unwrap_refused(history, *program(history).target_chart(root));
    if (mode == 2) check_unwrap_refused(history, branch_id_t{99999});
    if (mode == 3) check_unwrap_refused(history, nodes_of<bms_random_scope_t>(history).front());
    if (mode == 4) {
        auto proposed = bms_program_unwrap_edit_t::propose(history.document, difficulty,
            nodes_of<bms_random_scope_t>(history).front(), {.maximum_nodes = 1});
        CHECK_FALSE(proposed.value);
    }
}
TEST_CASE("Unwrap rejects newly simultaneous branch collisions without dropping content", "[bms-program-authoring]") {
    auto history = history_from("#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00011:01\n#ENDIF\n#ENDRANDOM\n");
    check_unwrap_refused(history, nodes_of<bms_random_scope_t>(history).front(), diagnostic_code_t::collision);
}
TEST_CASE("Native schema 14 migrates unchanged scoped programs to schema 15", "[bms-program-authoring]") {
    auto history = history_from(unwrap_source);
    auto document = nlohmann::json::parse(snapshot(history));
    document["document"]["schema_version"] = 14;
    const auto loaded = parse_native_project(document.dump());
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value);
    CHECK(loaded.value->schema_version == 15);
    CHECK(loaded.value->find_bms_program(difficulty)->prepared().program == program(history));
}
TEST_CASE("Unwrap preserves external lexical SKIP anchors when promoting a nested body", "[bms-program-authoring]") {
    auto history = history_from("#SWITCH 2\n#CASE 1\n#RANDOM 1\n#IF 1\n; nested\n#SKIP\n#ENDIF\n#ENDRANDOM\n#DEF\n#ENDSW\n");
    const auto arm = nodes_of<bms_if_scope_t>(history).front();
    const auto parent = *program(history).parent(arm);
    const auto before = program(history);
    auto proposed = prepare_unwrap(history, arm);
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_promotion).applied);
    const auto cases = nodes_of<bms_case_scope_t>(history);
    const auto& original_skip = std::get<bms_case_scope_t>(before.find(cases.front())->value).skips.front();
    const auto& skips = std::get<bms_case_scope_t>(program(history).find(cases.front())->value).skips;
    REQUIRE(skips.size() == 1);
    CHECK(skips.front().scope == parent);
    CHECK(skips.front().after_child == original_skip.after_child);
    CHECK(write(history).find("#SKIP") != std::string::npos);
    const auto after = program(history);
    check_history_round_trip(history, before, after);
}
TEST_CASE("Direct CASE unwrap refuses retained switch entry gating", "[bms-program-authoring]") {
    auto history = history_from("#SWITCH 2\n#CASE 1\n#DEF\n#ENDSW\n");
    check_unwrap_refused(history, nodes_of<bms_case_scope_t>(history).front());
}
TEST_CASE("Unwrap checks collisions in unrelated inactive selector outcomes", "[bms-program-authoring]") {
    auto history = selected_move_history("#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#ENDRANDOM\n"
        "#RANDOM 2\n#IF 1\n#00111:01\n#ELSE\n#00011:01\n#ENDIF\n#ENDRANDOM\n");
    check_unwrap_refused(history, nodes_of<bms_random_scope_t>(history).front(), diagnostic_code_t::collision);
}
TEST_CASE("Unwrap retains a case SKIP immediately after the promoted wrapper", "[bms-program-authoring]") {
    auto history = history_from("#SWITCH 2\n#CASE 1\n#RANDOM 1\n#IF 1\n; nested\n#ENDIF\n#ENDRANDOM\n#SKIP\n#DEF\n#ENDSW\n");
    const auto wrapper = nodes_of<bms_random_scope_t>(history).front();
    const auto last_child = program(history).find(wrapper)->children.back();
    const auto containing = *program(history).parent(wrapper);
    auto proposed = prepare_unwrap(history, wrapper);
    REQUIRE(apply_proposal(history, proposed, bms_program_unwrap_decision_t::confirm_unconditional_conversion).applied);
    const auto& skips = std::get<bms_case_scope_t>(program(history).find(containing)->value).skips;
    REQUIRE(skips.size() == 1);
    CHECK(skips.front().scope == containing);
    CHECK(skips.front().after_child == last_child);
    CHECK(write(history).find("#SKIP") != std::string::npos);
    check_unwrap_reload(history);
}
