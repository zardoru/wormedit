#include "../support/bms_program_fixture.h"
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/native_project.h>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>

using namespace wormedit;
using namespace wormedit::test::bms_program;

namespace {
constexpr auto prefix = "#TITLE Transfer\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n";
auto history_from(std::string_view body) -> document_history_t {
    const auto text = std::string(prefix) + std::string(body);
    auto parsed = parse_bms_bytes(bytes(text), {.lane_profile_id = "bme-7k-outside-scratch"});
    for (const auto& diagnostic : parsed.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.song);
    return document_history_t::from(std::move(*parsed.value.song));
}
auto owner(const document_history_t& history) -> const bms_program_document_t& {
    return *history.document.prepared_bms_programs.front();
}
auto chart_for(const document_history_t& history, int arm) -> branch_id_t {
    const auto& program = owner(history).prepared().program;
    if (arm == 0) return *program.target_chart(program.root());
    const auto node = std::ranges::find_if(program.nodes(), [&](const auto& node) {
        return arm == 1 ? std::holds_alternative<bms_if_scope_t>(node.value)
                        : std::holds_alternative<bms_else_scope_t>(node.value);
    });
    REQUIRE(node != program.nodes().end());
    return *program.target_chart(node->id);
}
auto content(const document_history_t& history, branch_id_t chart) -> const std::vector<bms_content_item_t>& {
    return std::get<bms_chart_content_t>(owner(history).prepared().program.find(chart)->value).items;
}
auto note_id(const document_history_t& history, branch_id_t chart) -> object_id_t {
    const auto* view = owner(history).read_authoring_chart(chart);
    REQUIRE(view);
    for (const auto& measure : view->measures)
        for (const auto& note : measure.notes)
            if (!view->is_bms_authoring_read_only(note.id)) return note.id;
    FAIL("fixture has no editable note");
    return {};
}
auto request_for(const document_history_t& history, int source, int destination,
    bms_note_transfer_kind_t kind = bms_note_transfer_kind_t::move) -> bms_note_transfer_command_t {
    const auto source_chart = chart_for(history, source);
    return {.stamp = chart_stamp_t::from(history, owner(history).difficulty()),
        .source_chart = source_chart, .destination_chart = chart_for(history, destination),
        .notes = {note_id(history, source_chart)}, .kind = kind};
}
void require_applied(const document_command_result_t& result) {
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
}
void set_preview(document_history_t& history, std::int64_t value) {
    const auto& program = owner(history).prepared().program;
    const auto selector = std::ranges::find_if(program.nodes(), [](const auto& node) {
        return std::holds_alternative<bms_random_scope_t>(node.value);
    });
    REQUIRE(selector != program.nodes().end());
    const auto result = apply_bms_program_preview_command(history, {
        .stamp = chart_stamp_t::from(history, owner(history).difficulty()), .node = selector->id, .value = value});
    REQUIRE(result.diagnostics.empty());
    history.undo.clear();
    history.redo.clear();
}
void check_refused(const document_history_t& history, const document_command_result_t& result,
    const std::shared_ptr<const bms_program_document_t>& original,
    diagnostic_code_t code = diagnostic_code_t::invalid_id) {
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == code);
    CHECK(history.document.prepared_bms_programs.front() == original);
    CHECK(history.undo.empty());
}
void check_history(document_history_t& history, const bms_program_t& before, const bms_program_t& after) {
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(owner(history).prepared().program == before);
    REQUIRE(redo_document_command(history));
    CHECK(owner(history).prepared().program == after);
}
void check_native_reload(const document_history_t& history) {
    const auto written = write_native_project(history.document);
    REQUIRE(written.value);
    const auto reopened = parse_native_project(*written.value);
    REQUIRE(reopened.value);
    CHECK(reopened.value->prepared_bms_programs.front()->prepared().program == owner(history).prepared().program);
}
void check_bms_export(const document_history_t& history) {
    const auto written = write_bms(history.document);
    REQUIRE(written.value);
    const auto reopened = parse_bms_bytes(bytes(*written.value), {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(reopened.value.song);
    const auto& actual = reopened.value.song->prepared_bms_programs.front()->prepared();
    const auto& expected = owner(history).prepared();
    CHECK(note_keys(projected(actual).song()) == note_keys(projected(expected).song()));
}
auto add_native_hold(document_history_t& history, branch_id_t chart) -> object_id_t {
    history.bms_edit_target = {.difficulty = owner(history).difficulty(), .chart = chart};
    const auto* view = history.read_chart(owner(history).difficulty());
    const auto measure = view->measures.front().id;
    const auto lane = view->lane_layouts.front().layout.lanes.back().id;
    note_t note{.position = {measure, 24}, .lane = lane, .kind = note_kind_t::hold,
        .end = local_position_t{measure, 72}, .sound = history.document.resources.front().id,
        .label = "Fresh hold"};
    document_command_transaction_t transaction(history);
    const std::array<bms_native_event_t, 1> additions{note};
    const auto staged = transaction.stage_bms_native_events(owner(history).difficulty(), {}, additions, "Author test hold");
    REQUIRE(staged.value);
    require_applied(transaction.commit("Author test hold"));
    return staged.value->front();
}
void check_copy(const std::vector<bms_content_item_t>& source, const std::vector<bms_content_item_t>& copied,
    std::uint64_t reserved) {
    REQUIRE(copied.size() == source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        CHECK(copied[index].id.value > reserved);
        CHECK(copied[index].id != source[index].id);
        CHECK(copied[index].value == source[index].value);
    }
}
}

TEST_CASE("Scoped note move preserves neighboring channel entries and targets an inactive Chart", "[bms-note-transfer]") {
    auto history = history_from("#00011:0101\n#RANDOM 2\n#IF 1\n#00012:01\n#ELSE\n#00013:01\n#ENDIF\n#ENDRANDOM\n");
    set_preview(history, 1);
    const auto request = request_for(history, 0, 2);
    const auto before = owner(history).prepared().program;
    const auto choices = owner(history).evaluation();
    const auto source = content(history, request.source_chart);
    const auto destination = content(history, request.destination_chart);
    const auto moved = *std::ranges::find(source, request.notes.front(), &bms_content_item_t::id);
    require_applied(apply_bms_note_transfer_command(history, request));
    auto expected_source = source;
    std::erase_if(expected_source, [&](const auto& item) { return item.id == moved.id; });
    CHECK(content(history, request.source_chart) == expected_source);
    auto expected_destination = destination;
    expected_destination.push_back(moved);
    CHECK(content(history, request.destination_chart) == expected_destination);
    CHECK(owner(history).evaluation() == choices);
    check_history(history, before, owner(history).prepared().program);
    check_native_reload(history);
    check_bms_export(history);
}

TEST_CASE("Scoped note copy retains imported taps and both hold contributions with fresh history-wide IDs", "[bms-note-transfer]") {
    const auto hold = GENERATE(false, true);
    const auto notes = hold ? "#00051:0102\n" : "#00011:01\n";
    auto history = history_from(std::string("#RANDOM 2\n#IF 1\n") + notes + "#ELSE\n#ENDIF\n#ENDRANDOM\n");
    set_preview(history, 1);
    const auto source_chart = chart_for(history, 1);
    const auto reserved = add_native_hold(history, source_chart).value;
    REQUIRE(undo_document_command(history));
    history.undo.clear();
    const auto source = content(history, source_chart);
    const auto before = owner(history).prepared().program;
    const auto request = request_for(history, 1, 2, bms_note_transfer_kind_t::copy);
    require_applied(apply_bms_note_transfer_command(history, request));
    CHECK(content(history, source_chart) == source);
    check_copy(source, content(history, request.destination_chart), reserved);
    check_history(history, before, owner(history).prepared().program);
    check_native_reload(history);
}

TEST_CASE("Scoped transfers preserve fresh authored holds and their annotations", "[bms-note-transfer]") {
    auto history = history_from("#00011:00\n#RANDOM 2\n#IF 1\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    const auto chart = chart_for(history, 1);
    const auto id = add_native_hold(history, chart);
    history.undo.clear();
    const auto source = content(history, chart);
    auto request = request_for(history, 1, 2, GENERATE(bms_note_transfer_kind_t::move, bms_note_transfer_kind_t::copy));
    request.notes = {id};
    require_applied(apply_bms_note_transfer_command(history, request));
    const auto& destination = content(history, request.destination_chart);
    REQUIRE(destination.size() == 1);
    CHECK(destination.front().value == source.front().value);
    CHECK(content(history, chart).empty() == (request.kind == bms_note_transfer_kind_t::move));
    check_native_reload(history);
    check_bms_export(history);
}

TEST_CASE("Scoped transfer refuses collisions hidden in another outcome atomically", "[bms-note-transfer]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00011:02\n#ENDIF\n#ENDRANDOM\n");
    const auto original = history.document.prepared_bms_programs.front();
    const auto request = request_for(history, 1, 2, GENERATE(bms_note_transfer_kind_t::move, bms_note_transfer_kind_t::copy));
    const auto result = apply_bms_note_transfer_command(history, request);
    check_refused(history, result, original, diagnostic_code_t::collision);
}

namespace {
void invalidate_request(bms_note_transfer_command_t& request, const document_history_t& history, int variant) {
    switch (variant) {
    case 0: request.notes.push_back(note_id(history, chart_for(history, 0))); break;
    case 1: request.notes.push_back(request.notes.front()); break;
    case 2: request.notes = {object_id_t{std::numeric_limits<std::uint64_t>::max()}}; break;
    case 3: request.destination_chart = owner(history).prepared().program.root(); break;
    case 4: request.destination_chart = request.source_chart; break;
    case 5: request.notes.clear(); break;
    case 6: request.kind = static_cast<bms_note_transfer_kind_t>(-1); break;
    }
}
}
TEST_CASE("Scoped transfer rejects ambiguous selections and invalid explicit Charts", "[bms-note-transfer]") {
    auto history = history_from("#00012:01\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    auto request = request_for(history, 1, 2);
    invalidate_request(request, history, GENERATE(0, 1, 2, 3, 4, 5, 6));
    const auto original = history.document.prepared_bms_programs.front();
    check_refused(history, apply_bms_note_transfer_command(history, request), original);
}

TEST_CASE("Scoped imported hold moves retain both endpoint identities and values", "[bms-note-transfer]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#00051:0102\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    set_preview(history, 1);
    const auto request = request_for(history, 1, 2);
    const auto source = content(history, request.source_chart);
    const auto before = owner(history).prepared().program;
    require_applied(apply_bms_note_transfer_command(history, request));
    CHECK(content(history, request.source_chart).empty());
    CHECK(content(history, request.destination_chart) == source);
    check_history(history, before, owner(history).prepared().program);
    check_native_reload(history);
    check_bms_export(history);
}

TEST_CASE("Scoped transfers refuse holds whose endpoints belong to different Charts", "[bms-note-transfer]") {
    // Explicit scoped fixture: its cross-owner pair needs a chosen playback
    // witness, while public import refuses its unpaired default evaluation.
    auto input = prepared("#WAV01 head.wav\n#BPM 120\n#00051:01\n#RANDOM 2\n#IF 1\n"
        "#00151:01\n#ELSE\n#00251:01\n#ENDIF\n#ENDRANDOM\n");
    bms_program_evaluation_t evaluation;
    for (const auto selector : selectors(input.program)) evaluation.selections.push_back({selector, 1});
    auto initial = bms_program_document_t::from(chart_id_t{9000}, std::move(input), std::move(evaluation));
    REQUIRE(initial.value);
    auto history = document_history_t::from(song_document_t{.id = song_id_t{9001},
        .prepared_bms_programs = {*initial.value}});
    const auto source = chart_for(history, 0);
    const auto& items = content(history, source);
    const auto head = std::ranges::find_if(items, [](const auto& item) {
        return std::holds_alternative<bms_hold_endpoint_t>(item.value);
    });
    REQUIRE(head != items.end());
    const bms_note_transfer_command_t request{.stamp = chart_stamp_t::from(history, owner(history).difficulty()),
        .source_chart = source, .destination_chart = chart_for(history, 1), .notes = {head->id}};
    const auto original = history.document.prepared_bms_programs.front();
    check_refused(history, apply_bms_note_transfer_command(history, request), original);
}

TEST_CASE("Inactive imported endpoint transfers require an explicit complete playback witness", "[bms-note-transfer]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#00012:01\n#ELSE\n#00151:0102\n#ENDIF\n#ENDRANDOM\n");
    set_preview(history, 1);
    const auto source = chart_for(history, 2);
    bms_note_transfer_command_t request{.stamp = chart_stamp_t::from(history, owner(history).difficulty()),
        .source_chart = source, .destination_chart = chart_for(history, 1),
        .notes = {content(history, source).front().id}, .kind = bms_note_transfer_kind_t::copy};
    const auto original = history.document.prepared_bms_programs.front();
    check_refused(history, apply_bms_note_transfer_command(history, request), original);
    set_preview(history, 2);
    request.stamp = chart_stamp_t::from(history, owner(history).difficulty());
    const auto choices = owner(history).evaluation();
    require_applied(apply_bms_note_transfer_command(history, request));
    CHECK(content(history, request.destination_chart).size() == 3);
    CHECK(owner(history).evaluation() == choices);
}

TEST_CASE("Scoped hold transfers refuse interleaving that would change another hold pair", "[bms-note-transfer]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#00051:01000100\n#ELSE\n#00051:00010002\n#ENDIF\n#ENDRANDOM\n");
    set_preview(history, 1);
    const auto original = history.document.prepared_bms_programs.front();
    const auto request = request_for(history, 1, 2, GENERATE(bms_note_transfer_kind_t::move, bms_note_transfer_kind_t::copy));
    check_refused(history, apply_bms_note_transfer_command(history, request), original,
        diagnostic_code_t::unsupported_feature);
}

TEST_CASE("Scoped hold transfers retain unrelated destination holds at other times", "[bms-note-transfer]") {
    auto history = history_from("#RANDOM 2\n#IF 1\n#00051:0102\n#ELSE\n#00151:0102\n#ENDIF\n#ENDRANDOM\n");
    set_preview(history, 1);
    const auto request = request_for(history, 1, 2, GENERATE(bms_note_transfer_kind_t::move, bms_note_transfer_kind_t::copy));
    const auto existing = content(history, request.destination_chart);
    require_applied(apply_bms_note_transfer_command(history, request));
    const auto& updated = content(history, request.destination_chart);
    REQUIRE(updated.size() == 4);
    CHECK(updated[0] == existing[0]);
    CHECK(updated[1] == existing[1]);
    check_native_reload(history);
}
