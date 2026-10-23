#include "../support/bms_program_fixture.h"
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/timing.h>
#include <wormedit/formats/native_project.h>
#include <catch2/generators/catch_generators.hpp>

using namespace wormedit;
using namespace wormedit::test::bms_program;
namespace {
constexpr auto source = "#WAV01 a.wav\n#BMP01 a.png\n#BPM 120\n#BPM01 150\n#STOP01 48\n#SCROLL01 .5\n#00001:01\n#00011:01\n"
    "#RANDOM 2\n#00214:01\n#IF 1\n#00112:01\n; split\n#00212:01\n"
    "#ELSE\n#WAV02 b.wav\n#00113:01\n#ENDIF\n#ENDRANDOM\n";
constexpr chart_id_t difficulty{9000};

auto history_from(std::string_view text = source) -> document_history_t {
    auto input = prepared(text);
    bms_program_evaluation_t evaluation;
    for (const auto id : selectors(input.program)) evaluation.selections.push_back({id, 1});
    auto owner = bms_program_document_t::from(difficulty, std::move(input), std::move(evaluation));
    REQUIRE(owner.value);
    REQUIRE(owner.ok());
    return document_history_t::from(song_document_t{.id = song_id_t{9001},
        .prepared_bms_programs = {std::move(*owner.value)}});
}
auto owner(const document_history_t& history) -> const bms_program_document_t& {
    return *history.document.prepared_bms_programs.front();
}
template<class Value> auto node_id(const document_history_t& history) -> branch_id_t {
    for (const auto& node : owner(history).prepared().program.nodes())
        if (std::holds_alternative<Value>(node.value)) return node.id;
    FAIL("fixture has no requested node");
    return {};
}
auto target(const document_history_t& history, std::optional<branch_id_t> selected = {})
    -> bms_program_edit_target_t {
    auto value = bms_program_edit_target_t::from(history.document, difficulty, selected);
    REQUIRE(value);
    return *value;
}
auto content(const document_history_t& history, branch_id_t chart)
    -> const std::vector<bms_content_item_t>& {
    return std::get<bms_chart_content_t>(owner(history).prepared().program.find(chart)->value).items;
}
auto note_value(const document_history_t& history) -> bms_content_value_t {
    return bms_note_content_t{.position = {.measure = owner(history).prepared().measures.front(),
        .fraction = {1, 2}}, .lane = owner(history).prepared().lane_profile->layout.lanes.front().id,
        .wav = "01"};
}
auto insert(document_history_t& history, bms_program_edit_target_t destination,
    bms_content_value_t value) -> document_command_result_t {
    document_command_transaction_t transaction(history);
    const auto id = transaction.next_bms_program_item_id();
    REQUIRE(id);
    auto result = transaction.finish_execution(transaction.stage(bms_program_content_edit_batch_t{
        .target = destination, .edits = {bms_program_content_insert_t{
            .item = {.id = *id, .value = std::move(value)}}}, .label = "Scoped insert"}));
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    return result;
}
} // namespace

TEST_CASE("Scoped target defaults to Root and scopes resolve their first direct Chart", "[bms-program-edit]") {
    const auto history = history_from();
    const auto selected = GENERATE(0, 1, 2);
    const auto scope = selected == 0 ? owner(history).prepared().program.root() :
        selected == 1 ? node_id<bms_random_scope_t>(history) : node_id<bms_if_scope_t>(history);
    const auto resolved = target(history, selected == 0 ? std::nullopt : std::optional{scope});
    CHECK(resolved.scope == scope);
    CHECK(resolved.chart == owner(history).prepared().program.target_chart(scope));
}
TEST_CASE("Explicit alternate Chart retains its own identity", "[bms-program-edit]") {
    const auto history = history_from();
    const auto scope = owner(history).prepared().program.find(node_id<bms_if_scope_t>(history));
    const auto resolved = target(history, scope->children.back());
    CHECK(resolved.scope == scope->id);
    CHECK(resolved.chart == scope->children.back());
    CHECK(resolved.chart != owner(history).prepared().program.target_chart(scope->id));
}
TEST_CASE("Invalid hidden and non-body selections never fall back to Root", "[bms-program-edit]") {
    const auto history = history_from();
    const auto kind = GENERATE(0, 1, 2);
    const auto selected = kind == 0 ? branch_id_t{999999} : kind == 1 ?
        node_id<bms_else_scope_t>(history) : node_id<bms_text_t>(history);
    const auto resolved = bms_program_edit_target_t::from(history.document, difficulty, selected);
    REQUIRE_FALSE(resolved);
    CHECK(resolved.error().code == (kind == 1 ? failure_code_t::unavailable : failure_code_t::not_found));
}
TEST_CASE("Scoped insertion changes only the selected semantic owner", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = GENERATE(0, 1, 2, 3);
    auto destination = target(history);
    if (selected == 1) destination = target(history, node_id<bms_random_scope_t>(history));
    if (selected == 2) destination = target(history, node_id<bms_if_scope_t>(history));
    if (selected == 3) destination = target(history,
        owner(history).prepared().program.find(node_id<bms_if_scope_t>(history))->children.back());
    const auto before = owner(history).prepared().program;
    REQUIRE(insert(history, destination, note_value(history)));
    for (const auto& node : before.nodes()) {
        if (node.id == destination.chart) continue;
        CHECK(*owner(history).prepared().program.find(node.id) == node);
    }
    CHECK(content(history, destination.chart).size() ==
        std::get<bms_chart_content_t>(before.find(destination.chart)->value).items.size() + 1);
}
TEST_CASE("Scoped publication stores one Undo record and restores exact program snapshots", "[bms-program-edit]") {
    auto history = history_from();
    const auto before = history.document.prepared_bms_programs.front();
    REQUIRE(insert(history, target(history), note_value(history)));
    const auto after = history.document.prepared_bms_programs.front();
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.prepared_bms_programs.front() == before);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.prepared_bms_programs.front() == after);
}
TEST_CASE("A stale captured scoped target rejects without changing history", "[bms-program-edit]") {
    auto history = history_from();
    const auto captured = target(history);
    REQUIRE(insert(history, captured, note_value(history)));
    const auto committed = history.document.prepared_bms_programs.front();
    CHECK_FALSE(insert(history, captured, note_value(history)));
    CHECK(history.document.prepared_bms_programs.front() == committed);
    CHECK(history.undo.size() == 1);
}
TEST_CASE("Undo cannot recycle scoped content identities retained by Redo", "[bms-program-edit]") {
    auto history = history_from();
    REQUIRE(insert(history, target(history), note_value(history)));
    const auto retired = content(history, target(history).chart).back().id;
    REQUIRE(undo_document_command(history));
    document_command_transaction_t transaction(history);
    REQUIRE(transaction.next_bms_program_item_id());
    CHECK(transaction.next_bms_program_item_id()->value > retired.value);
}
TEST_CASE("A mismatched recorded owner poisons the complete scoped transaction", "[bms-program-edit]") {
    auto history = history_from();
    const auto destination = target(history);
    const auto other = target(history, node_id<bms_if_scope_t>(history));
    const auto item = content(history, other.chart).front();
    document_command_transaction_t transaction(history);
    const auto result = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_insert_t{.item = {.id = *transaction.next_bms_program_item_id(),
            .value = note_value(history)}}, bms_program_content_delete_t{.owner = {
                .scope = other.scope, .chart = other.chart, .item = item.id}}}, .label = "Reject batch"});
    CHECK_FALSE(result.ok());
    CHECK_FALSE(transaction.commit("Must not publish"));
    CHECK(history.undo.empty());
    CHECK(content(history, other.chart).front() == item);
}
TEST_CASE("An invalid projection rolls back typed and ordinary staged changes together", "[bms-program-edit]") {
    auto history = history_from();
    const auto before = history.document.prepared_bms_programs.front();
    document_command_transaction_t transaction(history);
    transaction.document().metadata.title = "must roll back";
    auto note = std::get<bms_note_content_t>(note_value(history));
    note.lane = lane_id_t{999999};
    const auto staged = transaction.stage(bms_program_content_edit_batch_t{.target = target(history),
        .edits = {bms_program_content_insert_t{.item = {.id = *transaction.next_bms_program_item_id(),
            .value = note}}}, .label = "Invalid lane"});
    REQUIRE(staged.ok());
    CHECK_FALSE(transaction.finish_execution(staged));
    CHECK(history.document.prepared_bms_programs.front() == before);
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}
TEST_CASE("Prepared difficulty refuses an independently writable chart mirror", "[bms-program-edit]") {
    auto history = history_from();
    auto proposed = history.document;
    proposed.charts.push_back(chart_document_t{.id = difficulty});
    const auto diagnostics = validate_document(proposed);
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == diagnostic_code_t::duplicate_id);
}

TEST_CASE("All modeled content families publish through the same scoped batch", "[bms-program-edit]") {
    auto history = history_from();
    const auto kind = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    const auto position = std::get<bms_note_content_t>(note_value(history)).position;
    bms_content_value_t value = note_value(history);
    const auto& input = owner(history).prepared();
    switch (kind) {
    case 0: break;
    case 1: value = bms_bgm_content_t{.position = position, .column = input.bgm_columns.front().id, .wav = "01", .structural_slot = 1}; break;
    case 2: value = bms_bga_content_t{.position = position, .bmp = "01"}; break;
    case 3: value = bms_direct_bpm_t{.position = position, .value = {180, 1}}; break;
    case 4: value = bms_timing_definition_t{.identifier = "02", .value = {160, 1}}; break;
    case 5: value = bms_timing_reference_t{.position = position, .identifier = "01"}; break;
    case 6: value = bms_measure_length_t{.measure = position.measure, .length = {3, 2}}; break;
    case 7: value = content(history, *input.program.target_chart(node_id<bms_else_scope_t>(history))).front().value; break;
    case 8: value = bms_metadata_declaration_t{.field = bms_metadata_field_t::title, .value = "Scoped title"}; break;
    case 9: value = bms_timing_reference_t{.position = position, .kind = bms_timing_definition_kind_t::stop, .identifier = "01"}; break;
    }
    const auto destination = target(history, node_id<bms_if_scope_t>(history));
    REQUIRE(insert(history, destination, value));
    CHECK(content(history, destination.chart).back().value == value);
}

TEST_CASE("A scoped hold publishes both authored endpoints as one command", "[bms-program-edit]") {
    auto history = history_from();
    document_command_transaction_t transaction(history);
    const auto id = transaction.next_bms_program_item_id(2);
    REQUIRE(id);
    const auto note = std::get<bms_note_content_t>(note_value(history));
    const auto destination = target(history, node_id<bms_if_scope_t>(history));
    const auto staged = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_insert_t{.item = {.id = *id,
            .value = bms_hold_endpoint_t{.position = note.position, .lane = note.lane, .wav = "01"}}},
            bms_program_content_insert_t{.item = {.id = object_id_t{id->value + 1},
            .value = bms_hold_endpoint_t{.position = {.measure = note.position.measure, .fraction = {3, 4}},
                .lane = note.lane, .wav = "01"}}}}, .label = "Scoped hold"});
    REQUIRE(transaction.finish_execution(staged));
    CHECK(history.undo.size() == 1);
    const auto projection = owner(history).build_projection();
    REQUIRE(projection.value);
    CHECK(projection.value->song().charts.front().find_note(*id)->end.has_value());
}

TEST_CASE("Updating typed content retains its recorded owner", "[bms-program-edit]") {
    auto history = history_from();
    const auto scope = node_id<bms_if_scope_t>(history);
    REQUIRE(insert(history, target(history, scope), note_value(history)));
    const auto destination = target(history, scope);
    const auto id = content(history, destination.chart).back().id;
    auto value = std::get<bms_note_content_t>(note_value(history));
    value.position.fraction = {3, 4};
    document_command_transaction_t transaction(history);
    const auto staged = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_update_t{.owner = {.scope = destination.scope,
            .chart = destination.chart, .item = id}, .value = value}}, .label = "Move scoped note"});
    REQUIRE(transaction.finish_execution(staged));
    CHECK(content(history, destination.chart).back().id == id);
    CHECK(content(history, destination.chart).back().value == bms_content_value_t{value});
}

TEST_CASE("Cross-scope holds refuse an implicit partial-owner edit", "[bms-program-edit]") {
    auto history = history_from("#WAV01 a.wav\n#00051:01\n#RANDOM 2\n#IF 1\n#00151:01\n#ELSE\n#00251:01\n#ENDIF\n#ENDRANDOM\n");
    const auto destination = target(history);
    const auto id = content(history, destination.chart).back().id;
    document_command_transaction_t transaction(history);
    const auto staged = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_delete_t{.owner = {.scope = destination.scope,
            .chart = destination.chart, .item = id}}}, .label = "Reject partial hold"});
    CHECK_FALSE(staged.ok());
    CHECK_FALSE(transaction.commit("Must not publish"));
    CHECK(history.undo.empty());
}

TEST_CASE("Undeclared references reject the entire scoped publication", "[bms-program-edit]") {
    auto history = history_from();
    auto value = std::get<bms_note_content_t>(note_value(history));
    value.wav = "ZZ";
    const auto result = insert(history, target(history), value);
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::dangling_resource);
    CHECK(history.undo.empty());
}

TEST_CASE("Root edits validate collisions in currently inactive outcomes", "[bms-program-edit]") {
    auto history = history_from();
    auto value = std::get<bms_note_content_t>(note_value(history));
    value.position = {.measure = owner(history).prepared().measures[1], .fraction = {0, 1}};
    const auto& lanes = owner(history).prepared().lane_profile->layout.lanes;
    const auto lane = std::ranges::find_if(lanes,
        [](const auto& entry) { return entry.bms_channels.visible == "13"; });
    REQUIRE(lane != lanes.end());
    value.lane = lane->id; // BMS 13 is present only in ELSE.
    const auto result = insert(history, target(history), value);
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::collision);
    CHECK(history.undo.empty());
}

TEST_CASE("Unprovable evaluation budgets refuse publication atomically", "[bms-program-edit]") {
    auto history = history_from("#WAV01 a.wav\n#00011:01\n#RANDOM 257\n#IF 1\n#00112:01\n#ENDIF\n#ENDRANDOM\n");
    const auto result = insert(history, target(history), note_value(history));
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
    CHECK(history.undo.empty());
}

TEST_CASE("Deleting scoped content removes only the named authored item", "[bms-program-edit]") {
    auto history = history_from();
    const auto destination = target(history, node_id<bms_if_scope_t>(history));
    const auto item = content(history, destination.chart).front();
    const auto before = owner(history).prepared().program;
    document_command_transaction_t transaction(history);
    const auto staged = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_delete_t{.owner = {.scope = destination.scope,
            .chart = destination.chart, .item = item.id}}}, .label = "Delete scoped note"});
    REQUIRE(transaction.finish_execution(staged));
    CHECK(content(history, destination.chart).empty());
    for (const auto& node : before.nodes()) {
        if (node.id == destination.chart) continue;
        CHECK(*owner(history).prepared().program.find(node.id) == node);
    }
}

TEST_CASE("Scoped batches with no net semantic change do not create Undo", "[bms-program-edit]") {
    auto history = history_from();
    const auto destination = target(history);
    document_command_transaction_t transaction(history);
    const auto id = *transaction.next_bms_program_item_id();
    const auto staged = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_insert_t{.item = {.id = id, .value = note_value(history)}},
            bms_program_content_delete_t{.owner = {.scope = destination.scope,
                .chart = destination.chart, .item = id}}}, .label = "No net edit"});
    REQUIRE(staged.value);
    CHECK_FALSE(staged.value->changed);
    CHECK_FALSE(transaction.finish_execution(staged));
    CHECK(history.undo.empty());
}

TEST_CASE("Unchanged imported unresolved references remain recoverable during unrelated edits", "[bms-program-edit]") {
    auto history = history_from("#WAV01 a.wav\n#00011:ZZ\n");
    const auto result = insert(history, target(history), note_value(history));
    CHECK(result);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("A later failed stage cannot publish an earlier successful scoped stage", "[bms-program-edit]") {
    auto history = history_from();
    const auto destination = target(history);
    document_command_transaction_t transaction(history);
    const auto first = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_insert_t{.item = {.id = *transaction.next_bms_program_item_id(),
            .value = note_value(history)}}}, .label = "First stage"});
    REQUIRE(first.ok());
    const auto second = transaction.stage(bms_program_content_edit_batch_t{.target = destination,
        .edits = {bms_program_content_delete_t{.owner = {.scope = destination.scope,
            .chart = destination.chart, .item = object_id_t{999999}}}}, .label = "Failed second stage"});
    CHECK_FALSE(second.ok());
    CHECK_FALSE(transaction.finish_execution(first));
    CHECK(history.undo.empty());
}

TEST_CASE("Existing-object target resolution follows its owner rather than Root selection", "[bms-program-edit]") {
    const auto history = history_from();
    const auto destination = target(history, node_id<bms_if_scope_t>(history));
    const auto id = content(history, destination.chart).front().id;
    const auto resolved = bms_program_edit_target_t::from(history.document, difficulty, id);
    REQUIRE(resolved);
    CHECK(*resolved == destination);
    CHECK(resolved->chart != target(history).chart);
}

TEST_CASE("Existing hidden items cannot acquire a visible ancestor target", "[bms-program-edit]") {
    const auto history = history_from();
    const auto chart = *owner(history).prepared().program.target_chart(node_id<bms_else_scope_t>(history));
    const auto id = content(history, chart).front().id;
    const auto resolved = bms_program_edit_target_t::from(history.document, difficulty, id);
    REQUIRE_FALSE(resolved);
    CHECK(resolved.error().code == failure_code_t::unavailable);
}

TEST_CASE("The legacy native codec refuses prepared owners rather than dropping them", "[bms-program-edit]") {
    const auto history = history_from();
    const auto written = write_native_project(history.document);
    CHECK_FALSE(written.value);
    REQUIRE_FALSE(written.diagnostics.empty());
    CHECK(written.diagnostics.front().code == native_project_diagnostic_code_t::unsupported_version);
}

TEST_CASE("Appended scoped timing follows program order without rewriting authored provenance", "[bms-program-edit]") {
    auto history = history_from();
    const auto destination = target(history);
    REQUIRE(insert(history, destination, bms_direct_bpm_t{
        .position = {.measure = owner(history).prepared().measures.front(), .fraction = {0, 1}},
        .value = {180, 1}}));
    const auto projection = owner(history).build_projection();
    REQUIRE(projection.value);
    const auto& chart = projection.value->song().charts.front();
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto one_beat = timing.value->seconds_at(chart.divisions_per_standard_measure / 4);
    REQUIRE(one_beat);
    CHECK(one_beat.seconds == signed_rational_t{1, 3});
    CHECK(content(history, destination.chart).back().provenance.source_order == 0);
}

TEST_CASE("Alternative SWITCH validation reaches DEF when zero and one are named cases", "[bms-program-edit]") {
    auto history = history_from("#WAV01 a.wav\n#BPM 120\n#SWITCH 2\n#CASE 0\n#00112:01\n#SKIP\n"
        "#CASE 1\n#00113:01\n#SKIP\n#DEF\n#00114:01\n#ENDSW\n");
    auto value = std::get<bms_note_content_t>(note_value(history));
    const auto& lanes = owner(history).prepared().lane_profile->layout.lanes;
    const auto lane = std::ranges::find_if(lanes,
        [](const auto& entry) { return entry.bms_channels.visible == "14"; });
    REQUIRE(lane != lanes.end());
    value.lane = lane->id;
    value.position = {.measure = owner(history).prepared().measures[1], .fraction = {0, 1}};
    const auto result = insert(history, target(history), value);
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::collision);
    CHECK(history.undo.empty());
}
