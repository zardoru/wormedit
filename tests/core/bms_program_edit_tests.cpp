#include "../support/bms_program_fixture.h"
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/pause_warp_editor.h>
#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/hitsound_editor.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/core/time_shift.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/audio_offset_editor.h>
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

TEST_CASE("Scoped BPM commands use selected semantic Charts through the shared runner", "[bms-program-edit]") {
    auto history = history_from();
    const auto chosen = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = chosen.chart};
    const auto root = target(history).chart;
    const auto root_before = content(history, root);
    const auto result = apply_bpm_edit(history, insert_edit_t<bpm_edit_value_t>{.chart = difficulty,
        .value = {.position = {owner(history).prepared().measures.front(), 0},
            .settings = {.beats_per_minute = {180, 1}}}});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(content(history, root) == root_before);
    CHECK(std::holds_alternative<bms_native_content_t>(content(history, chosen.chart).back().value));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped timing commands cover native timing categories without a writable projection", "[bms-program-edit]") {
    auto history = history_from();
    const auto position = local_position_t{owner(history).prepared().measures.front(), 1};
    const auto family = GENERATE(0, 1, 2, 3, 4);
    document_command_result_t result;
    if (family == 0) result = apply_pause_warp_edit(history, insert_edit_t<pause_edit_value_t>{
        .chart = difficulty, .value = {.position = position,
            .settings = {.duration = {1, 4}, .source_unit = pause_unit_t::seconds, .kind = pause_kind_t::delay}}});
    if (family == 1) result = apply_scroll_speed_edit(history, insert_edit_t<scroll_edit_value_t>{
        .chart = difficulty, .value = {.position = position, .settings = {.multiplier = {-1, 2}}}});
    if (family == 2) result = apply_scroll_speed_edit(history, insert_edit_t<speed_edit_value_t>{
        .chart = difficulty, .value = {.position = position, .settings = {.multiplier = {2, 1}}}});
    if (family == 3) result = apply_marker_edit(history, insert_edit_t<kiai_edit_value_t>{
        .chart = difficulty, .value = {.position = position, .settings = {.enabled = true}}});
    if (family == 4) result = apply_marker_edit(history, insert_edit_t<bookmark_edit_value_t>{
        .chart = difficulty, .value = {.label = "Scoped bookmark", .position = position}});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(history.document.charts.empty());
    CHECK(history.undo.size() == 1);
    CHECK(std::get<bms_native_content_t>(content(history, target(history).chart).back().value).positions().size() == 1);
}

TEST_CASE("Scoped timing updates retain their original Chart after selection changes", "[bms-program-edit]") {
    auto history = history_from();
    const auto initial = target(history);
    const auto position = local_position_t{owner(history).prepared().measures.front(), 0};
    REQUIRE(apply_bpm_edit(history, insert_edit_t<bpm_edit_value_t>{.chart = difficulty,
        .value = {.position = position, .settings = {.beats_per_minute = {180, 1}}}}).applied);
    const auto id = content(history, initial.chart).back().id;
    history.bms_edit_target = {.difficulty = difficulty,
        .chart = target(history, node_id<bms_if_scope_t>(history)).chart};
    REQUIRE(apply_bpm_edit(history, update_edit_t<bpm_edit_value_t>{.chart = difficulty, .id = id,
        .value = {.position = position, .settings = {.beats_per_minute = {210, 1}}}}).applied);
    CHECK(content(history, initial.chart).back().id == id);
    CHECK(history.undo.size() == 2);
}

TEST_CASE("Captured scoped timing insertion retains its Chart when the session selection changes", "[bms-program-edit]") {
    auto history = history_from();
    const auto original = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = original.chart};
    const auto stamp = document_stamp_t::from(history);
    history.bms_edit_target.reset();
    document_command_transaction_t transaction(history, stamp, "Captured scoped timing");
    const auto result = apply_scroll_speed_edit(transaction, insert_edit_t<speed_edit_value_t>{
        .chart = difficulty, .value = {.position = {owner(history).prepared().measures.front(), 0},
            .settings = {.multiplier = {2, 1}}}});
    REQUIRE(result.applied);
    CHECK(std::holds_alternative<bms_native_content_t>(content(history, original.chart).back().value));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Native scoped events preserve exact fractions through projection grid changes", "[bms-program-edit]") {
    auto history = history_from();
    auto projection = owner(history).build_projection();
    REQUIRE(projection.value);
    auto geometry = projection.value->song().charts.front();
    geometry.divisions_per_standard_measure = 192;
    const auto measure = geometry.measures.front().id;
    const auto native = bms_native_content_t::from(warp_event_t{.id = object_id_t{99999},
        .span = {{measure, 1}, {measure, 3}}}, geometry);
    REQUIRE(native);
    geometry.divisions_per_standard_measure = 384;
    const auto rebuilt = native->build_event(object_id_t{50000}, geometry);
    REQUIRE(rebuilt);
    CHECK(std::get<warp_event_t>(*rebuilt).span == position_span_t{{measure, 2}, {measure, 6}});
    CHECK(std::get<warp_event_t>(*rebuilt).id == object_id_t{50000});
}

namespace {
auto scoped_state(document_history_t& history) -> editor_interaction_state_t {
    const auto* chart = history.document.read_chart(difficulty);
    REQUIRE(chart);
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = editor_interaction_state_t::from(history, difficulty,
        owner(history).prepared().lane_profile->layout.lanes.front().id,
        {chart->measures.front().id, 0}, snaps, 4);
    for (const auto& diagnostic : setup.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(setup);
    return std::move(*setup.state);
}
}
TEST_CASE("Actual scoped tap dispatch inserts into the selected Chart and supports Undo", "[bms-program-edit]") {
    auto history = history_from();
    auto state = scoped_state(history);
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto* chart = history.document.read_chart(difficulty);
    const auto result = dispatch_editor_action(history, state, place_tap_action_t{
        .position = {chart->measures.front().id, chart->divisions_per_standard_measure / 2}});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.accepted);
    CHECK(result.document_changed);
    CHECK(content(history, selected.chart).back().id == result.created_note);
    CHECK(undo_document_command(history));
}

TEST_CASE("Actual scoped hold dispatch keeps its captured Chart after selection changes", "[bms-program-edit]") {
    auto history = history_from();
    auto state = scoped_state(history);
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto* chart = history.document.read_chart(difficulty);
    const auto begin = local_position_t{chart->measures.front().id, chart->divisions_per_standard_measure / 4};
    const auto end = local_position_t{begin.measure, chart->divisions_per_standard_measure * 3 / 4};
    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{.position = begin}).accepted);
    history.bms_edit_target.reset();
    const auto result = dispatch_editor_action(history, state, commit_hold_action_t{.position = end});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.accepted);
    CHECK(content(history, selected.chart).back().id == result.created_note);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped resource placement creates notes BGM and BGA through their domain command", "[bms-program-edit]") {
    auto history = history_from();
    const auto kind = GENERATE(resource_placement_kind_t::sample_note,
        resource_placement_kind_t::bgm_event, resource_placement_kind_t::bga_background);
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto* chart = history.document.read_chart(difficulty);
    const auto role = kind == resource_placement_kind_t::bga_background ? resource_role_t::bga : resource_role_t::sample;
    const auto& resources = owner(history).prepared().resources;
    const auto resource = std::ranges::find_if(resources, [role](const auto& value) {
        return std::ranges::find(value.roles, role) != value.roles.end();
    });
    REQUIRE(resource != resources.end());
    const auto result = apply_resource_placement(history, {.chart = difficulty, .resource = resource->id,
        .position = {chart->measures.front().id, chart->divisions_per_standard_measure / 2},
        .lane = owner(history).prepared().lane_profile->layout.lanes.front().id, .kind = kind});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(std::holds_alternative<bms_native_content_t>(content(history, selected.chart).back().value));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped object edits preserve symbolic WAV resolution across alternatives", "[bms-program-edit]") {
    auto history = history_from("#BPM 120\n#00011:01\n#RANDOM 2\n#IF 1\n#WAV01 a.wav\n#ELSE\n#WAV01 b.wav\n#ENDIF\n#ENDRANDOM\n");
    const auto* chart = history.document.read_chart(difficulty);
    const auto id = chart->measures.front().notes.front().id;
    const auto original_sound = chart->measures.front().notes.front().sound;
    const std::array ids{id};
    REQUIRE(apply_hitsound_edit(history, difficulty, ids, {.field = hitsound_field_t::clap, .value = 1}).applied);
    const auto& value = std::get<bms_note_content_t>(std::ranges::find(content(history, target(history).chart), id, &bms_content_item_t::id)->value);
    REQUIRE(value.wav);
    CHECK(*value.wav == "01");
    auto alternative = owner(history).evaluation();
    alternative.selections.front().selected_value = 2;
    const auto projection = bms_program_projection_t::from(owner(history).prepared(), alternative);
    REQUIRE(projection.value);
    const auto& note = projection.value->song().charts.front().measures.front().notes.front();
    CHECK(note.sound != original_sound);
    CHECK(note.osu_hitsample->clap);
}

TEST_CASE("Native scoped recipes retain an exact exclusive chart boundary", "[bms-program-edit]") {
    auto history = history_from();
    const auto* chart = history.document.read_chart(difficulty);
    const auto last = chart->measures.back().id;
    const auto native = bms_native_content_t::from(sv_recipe_t{.name = "Scoped recipe",
        .range = {{chart->measures.front().id, 0}, {last, chart->divisions_per_standard_measure}}}, *chart);
    REQUIRE(native);
    REQUIRE(insert(history, target(history), *native).applied);
    const auto projection = owner(history).build_projection();
    REQUIRE(projection.value);
    CHECK(projection.value->song().charts.front().sv_recipes.size() == 1);
    CHECK(projection.value->song().charts.front().sv_recipes.front().range.end.measure == last);
}

TEST_CASE("Native scoped sound channels retain child identities and exact event positions", "[bms-program-edit]") {
    auto history = history_from();
    const auto* chart = history.document.read_chart(difficulty);
    const auto at = local_position_t{chart->measures.front().id, chart->divisions_per_standard_measure / 2};
    const auto native = bms_native_content_t::from(sound_channel_t{.id = object_id_t{70000},
        .name = "Scoped stem", .source_clip = {.id = clip_id_t{70001}, .source = asset_id_t{70002}},
        .events = {{.id = object_id_t{70003}, .position = at, .lane = lane_id_t{1}}}}, *chart);
    REQUIRE(native);
    CHECK(native->declared_ids() == std::vector<object_id_t>{object_id_t{70001}, object_id_t{70003}});
    const auto event = native->build_event(object_id_t{70004}, *chart);
    REQUIRE(event);
    CHECK(std::get<sound_channel_t>(*event).events.front().position == at);
    CHECK(std::get<sound_channel_t>(*event).id == object_id_t{70004});
}

TEST_CASE("Scoped insertions reject a selected owner from another difficulty", "[bms-program-edit]") {
    auto history = history_from();
    history.bms_edit_target = {.difficulty = chart_id_t{99999}, .chart = target(history).chart};
    const auto before = history.document.prepared_bms_programs.front();
    const auto result = apply_bpm_edit(history, insert_edit_t<bpm_edit_value_t>{.chart = difficulty,
        .value = {.position = {owner(history).prepared().measures.front(), 0},
            .settings = {.beats_per_minute = {180, 1}}}});
    CHECK_FALSE(result.applied);
    CHECK(history.document.prepared_bms_programs.front() == before);
    CHECK(history.undo.empty());
}

TEST_CASE("Scoped metadata appends a typed declaration to the selected Chart", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto result = apply_metadata_edit(history, edit_format_metadata_field_action_t{
        .target = {.chart = difficulty, .family = format_family_t::bms},
        .name = "TITLE", .value = "Selected title"});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(std::get<bms_metadata_declaration_t>(content(history, selected.chart).back().value).field == bms_metadata_field_t::title);
    CHECK(owner(history).build_projection().value->song().metadata.title == "Selected title");
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped recipe creation uses the domain editor and selected semantic owner", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto* chart = history.document.read_chart(difficulty);
    const auto result = apply_sv_recipe_edit(history, insert_edit_t<sv_recipe_t>{.chart = difficulty,
        .value = {.name = "Selected recipe", .range = {{chart->measures.front().id, 0},
            {chart->measures.front().id, chart->divisions_per_standard_measure / 2}}}});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(std::holds_alternative<bms_native_content_t>(content(history, selected.chart).back().value));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped timing previews retain their captured insertion Chart", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    auto preview = timing_lane_pseudonote_preview_t::propose(history, difficulty,
        timing_lane_pseudonote_state_t{.kind = timing_lane_kind_t::speed,
            .position = {owner(history).prepared().measures.front(), 0},
            .value = timing_speed_value_t{.multiplier = {2, 1}}}, false);
    REQUIRE(preview.value);
    history.bms_edit_target.reset();
    const auto result = apply_proposal(history, preview, false);
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(std::holds_alternative<bms_native_content_t>(content(history, selected.chart).back().value));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped Kiai areas create both boundaries in the selected Chart", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto result = apply_marker_edit(history, edit_kiai_span_action_t{.chart = difficulty,
        .edit = {.rows = std::pair<std::uint64_t, std::uint64_t>{1, 2}}});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(owner(history).build_projection().value->song().charts.front().kiai.size() == 2);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped conversion retains object identity and semantic ownership", "[bms-program-edit]") {
    auto history = history_from();
    const auto* chart = history.document.read_chart(difficulty);
    const auto note = chart->measures.front().notes.front();
    const auto origin = bms_program_edit_target_t::from(history.document, difficulty, note.id);
    REQUIRE(origin);
    history.bms_edit_target = {.difficulty = difficulty,
        .chart = target(history, node_id<bms_if_scope_t>(history)).chart};
    const std::array ids{note.id};
    const auto moved = move_notes_to_bgm(history, difficulty, ids, std::nullopt);
    for (const auto& diagnostic : moved.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(moved.applied);
    CHECK(history.document.read_chart(difficulty)->find_bgm_event(note.id));
    CHECK(bms_program_edit_target_t::from(history.document, difficulty, note.id)->chart == origin->chart);
    REQUIRE(move_bgm_events_to_lane(history, difficulty, ids, note.lane).applied);
    CHECK(history.document.read_chart(difficulty)->find_note(note.id));
}

TEST_CASE("Scoped selection shifts and quantization use recorded owners", "[bms-program-edit]") {
    auto history = history_from();
    const auto* chart = history.document.read_chart(difficulty);
    const auto note = chart->measures.front().notes.front();
    const auto origin = bms_program_edit_target_t::from(history.document, difficulty, note.id);
    REQUIRE(origin);
    history.bms_edit_target = {.difficulty = difficulty,
        .chart = target(history, node_id<bms_if_scope_t>(history)).chart};
    REQUIRE(apply_shift_selection(history, {.chart = difficulty, .notes = {note.id}, .rows = 1}).command.applied);
    const auto result = apply_quantize_command(history, difficulty, {.divisions = 4,
        .allow_resolution_promotion = true, .selection = quantize_selection_t{.notes = {note.id}}});
    for (const auto& diagnostic : result.command.diagnostics) UNSCOPED_INFO(diagnostic.message);
    CHECK(result.command.diagnostics.empty());
    CHECK(bms_program_edit_target_t::from(history.document, difficulty, note.id)->chart == origin->chart);
}

TEST_CASE("Scoped layer definitions and object membership have separate appropriate owners", "[bms-program-edit]") {
    auto history = history_from();
    REQUIRE(apply_semantic_layer_edit(history, insert_edit_t<semantic_layer_edit_value_t>{.chart = difficulty,
        .value = {.name = "Prepared layer", .color = {.red = 1.0, .alpha = 1.0}}}).applied);
    const auto* chart = history.document.read_chart(difficulty);
    REQUIRE(chart->semantic_layers.size() == 1);
    const auto layer = chart->semantic_layers.front().id;
    const auto note = chart->measures.front().notes.front().id;
    REQUIRE(apply_semantic_layer_edit(history, semantic_layer_membership_edit_t{.chart = difficulty,
        .layer = layer, .notes = {note}}).applied);
    CHECK(history.document.read_chart(difficulty)->find_note(note)->semantic_layers == std::vector{layer});
    CHECK(owner(history).prepared().semantic_layers.front().id == layer);
}

TEST_CASE("Scoped offset edits create a typed declaration and retain it after selection changes", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    REQUIRE(apply_audio_offset_edit(history, difficulty, {1, 4}).applied);
    history.bms_edit_target.reset();
    REQUIRE(apply_audio_offset_edit(history, difficulty, {1, 2}).applied);
    CHECK(history.document.read_chart(difficulty)->beat_zero_audio_offset_seconds == signed_rational_t{1, 2});
    CHECK(std::get<bms_metadata_declaration_t>(content(history, selected.chart).back().value).field == bms_metadata_field_t::offset);
}

TEST_CASE("Scoped gesture captures reject another project even at the same revision", "[bms-program-edit]") {
    auto history = history_from();
    auto state = scoped_state(history);
    const auto* chart = history.document.read_chart(difficulty);
    const auto begin = local_position_t{chart->measures.front().id, chart->divisions_per_standard_measure / 4};
    const auto end = local_position_t{begin.measure, chart->divisions_per_standard_measure * 3 / 4};
    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{.position = begin}).accepted);
    const auto before = history.document.prepared_bms_programs.front();
    history.document.id = song_id_t{99999};
    const auto result = dispatch_editor_action(history, state, commit_hold_action_t{.position = end});
    CHECK_FALSE(result.accepted);
    CHECK(history.document.prepared_bms_programs.front() == before);
    CHECK(history.undo.empty());
}

TEST_CASE("Moving scoped timing references preserves alternative definition values", "[bms-program-edit]") {
    auto history = history_from("#BPM 120\n#00108:01\n#RANDOM 2\n#IF 1\n#BPM01 180\n#ELSE\n#BPM01 240\n#ENDIF\n#ENDRANDOM\n");
    const auto* chart = history.document.read_chart(difficulty);
    const auto event = *std::ranges::find(chart->bpms, signed_rational_t{180, 1}, &bpm_event_t::beats_per_minute);
    REQUIRE(apply_bpm_edit(history, update_edit_t<bpm_edit_value_t>{.chart = difficulty, .id = event.id,
        .value = {.position = {event.position.measure, 1}, .settings = {.beats_per_minute = event.beats_per_minute}}}).applied);
    const auto& items = content(history, target(history).chart);
    CHECK(std::holds_alternative<bms_timing_reference_t>(std::ranges::find(items, event.id, &bms_content_item_t::id)->value));
    auto evaluation = owner(history).evaluation();
    evaluation.selections.front().selected_value = 2;
    const auto projection = bms_program_projection_t::from(owner(history).prepared(), evaluation);
    REQUIRE(projection.value);
    CHECK(std::ranges::find(projection.value->song().charts.front().bpms, event.id, &bpm_event_t::id)->beats_per_minute == signed_rational_t{240, 1});
}

TEST_CASE("Annotating imported scoped holds retains both endpoint identities and WAV values", "[bms-program-edit]") {
    auto history = history_from("#WAV01 a.wav\n#WAV02 b.wav\n#BPM 120\n#00051:0102\n");
    const auto id = history.document.read_chart(difficulty)->measures.front().notes.front().id;
    const auto endpoint_count = content(history, target(history).chart).size();
    const std::array ids{id};
    REQUIRE(apply_hitsound_edit(history, difficulty, ids, {.field = hitsound_field_t::clap, .value = 1}).applied);
    const auto& items = content(history, target(history).chart);
    CHECK(items.size() == endpoint_count);
    std::vector<std::string> values;
    for (const auto& item : items)
        if (const auto* endpoint = std::get_if<bms_hold_endpoint_t>(&item.value)) values.push_back(endpoint->wav);
    CHECK(values == std::vector<std::string>{"01", "02"});
    CHECK(history.document.read_chart(difficulty)->find_note(id)->osu_hitsample->clap);
}

TEST_CASE("Actual scoped note drags retain the recorded owner after selection changes", "[bms-program-edit]") {
    auto history = history_from();
    auto state = scoped_state(history);
    const auto* chart = history.document.read_chart(difficulty);
    const auto note = chart->measures.front().notes.front();
    state.active_lane = note.lane;
    const auto origin = bms_program_edit_target_t::from(history.document, difficulty, note.id);
    REQUIRE(origin);
    REQUIRE(dispatch_editor_action(history, state, begin_note_move_gesture_action_t{.reference_note = note.id}).accepted);
    history.bms_edit_target = {.difficulty = difficulty,
        .chart = target(history, node_id<bms_if_scope_t>(history)).chart};
    const auto result = dispatch_editor_action(history, state, update_note_move_gesture_action_t{
        .reference_position = {note.position.measure, chart->divisions_per_standard_measure / 2},
        .reference_lane = note.lane, .operation = proposal_operation_t::apply});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.accepted);
    CHECK(bms_program_edit_target_t::from(history.document, difficulty, note.id)->chart == origin->chart);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Scoped recipe drafts capture their insertion Chart", "[bms-program-edit]") {
    auto history = history_from();
    const auto selected = target(history, node_id<bms_if_scope_t>(history));
    history.bms_edit_target = {.difficulty = difficulty, .chart = selected.chart};
    const auto* chart = history.document.read_chart(difficulty);
    auto draft = sv_recipe_draft_t::propose(history, difficulty, sv_recipe_t{.name = "Captured recipe",
        .range = {{chart->measures.front().id, 0},
            {chart->measures.front().id, chart->divisions_per_standard_measure / 2}}}, false);
    REQUIRE(draft.value);
    history.bms_edit_target.reset();
    const auto result = apply_proposal(history, *draft.value);
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    CHECK(std::holds_alternative<bms_native_content_t>(content(history, selected.chart).back().value));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Root scoped proposals reject a foreign legacy project at the same revision", "[bms-program-edit]") {
    auto history = history_from();
    auto preview = timing_lane_pseudonote_preview_t::propose(history, difficulty,
        timing_lane_pseudonote_state_t{.kind = timing_lane_kind_t::speed,
            .position = {owner(history).prepared().measures.front(), 0},
            .value = timing_speed_value_t{.multiplier = {2, 1}}}, false);
    REQUIRE(preview.value);
    auto foreign = history.document;
    foreign.id = song_id_t{99999};
    foreign.charts = {*history.document.read_chart(difficulty)};
    foreign.resources = owner(history).prepared().resources;
    foreign.audio_assets = owner(history).prepared().audio_assets;
    foreign.visual_assets = owner(history).prepared().visual_assets;
    foreign.prepared_bms_programs.clear();
    history = document_history_t::from(std::move(foreign));
    const auto result = apply_proposal(history, preview, false);
    CHECK_FALSE(result.applied);
    CHECK(history.document.charts.front().speeds.empty());
    CHECK(history.undo.empty());
}
