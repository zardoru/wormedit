#include <wormedit/core/generator.h>
#include <wormedit/core/lane_profiles.h>
#include "../../src/core/id_ceiling.h"
#include "../support/native_bms_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>
namespace {
using namespace wormedit;
void check_generator(bool value, std::source_location at = std::source_location::current()) {
    INFO("Generator assertion at " << at.line()); CHECK(value);
}
void require_generator(bool value, std::source_location at = std::source_location::current()) {
    INFO("Generator requirement at " << at.line()); REQUIRE(value);
}
auto generator_history() -> document_history_t {
    auto profile = make_bms_7k_profile();
    chart_document_t chart{.id = chart_id_t{2}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{3}}}};
    chart.lane_layouts.push_back({.id = object_id_t{4}, .position = {measure_id_t{3}, 0}, .layout = profile.layout});
    chart.input_profile = profile.input;
    song_document_t song{.id = song_id_t{1}, .charts = {std::move(chart)}};
    return document_history_t::from(std::move(song));
}
auto generator_descriptor() -> generator_recipe_t {
    return {.api_version = "1", .source_path = "scripts/test.lua", .source_hash = std::string(64, 'a'),
        .source = "return {notes={}}", .chart = chart_id_t{2}};
}
auto generated_note(std::string key, std::uint32_t row, measure_id_t measure = measure_id_t{3}) -> generator_note_t {
    return {.key = std::move(key), .value = {.position = {measure, row}, .lane = lane_id_t{101}}};
}
auto preview_notes(document_history_t& history, generator_recipe_t recipe,
    std::vector<generator_note_t> notes) -> generator_proposal_t {
    auto result = generator_preview_t::propose(history, std::move(recipe), {.notes = std::move(notes)});
    require_generator(result.diagnostics.empty());
    require_generator(result.value.has_value());
    return std::move(*result.value);
}
}
TEST_CASE("generator Apply materializes stable keyed notes as one undoable command", "[generator]") {
    auto history = generator_history();
    auto preview = preview_notes(history, generator_descriptor(), {generated_note("a", 120)});
    const auto id = preview.payload.recipe.generated_notes.front().value.id;
    check_generator(history.undo.empty());
    require_generator(apply_generator(history, preview).applied);
    check_generator(history.undo.size() == 1);
    check_generator(history.document.generator_recipes.size() == 1);
    require_generator(undo_document_command(history));
    check_generator(history.document.generator_recipes.empty());
    require_generator(redo_document_command(history));
    auto next = preview_notes(history, history.document.generator_recipes.front(), {generated_note("a", 240), generated_note("b", 360)});
    check_generator(next.payload.notes.added == 1 && next.payload.notes.changed == 1);
    check_generator(next.payload.recipe.generated_notes.front().value.id == id);
    require_generator(apply_generator(history, next).applied);
    check_generator(history.undo.size() == 2);
}
TEST_CASE("generator regeneration rejects edited or deleted materializations and Detach keeps notes", "[generator]") {
    auto history = generator_history();
    auto preview = preview_notes(history, generator_descriptor(), {generated_note("a", 120)});
    require_generator(apply_generator(history, preview).applied);
    const auto recipe = history.document.generator_recipes.front();
    SECTION("edited") { history.document.charts.front().measures.front().notes.front().label = "manual"; }
    SECTION("deleted") { history.document.charts.front().measures.front().notes.clear(); }
    const auto attempted = generator_preview_t::propose(history, recipe, {.notes = {generated_note("a", 240)}});
    check_generator(!attempted.value && !attempted.diagnostics.empty());
    const auto notes = history.document.charts.front().measures.front().notes;
    require_generator(detach_generator(history, recipe.id).applied);
    check_generator(history.document.generator_recipes.empty());
    check_generator(history.document.charts.front().measures.front().notes == notes);
}
TEST_CASE("generator rejects collisions and stale proposals without changing unrelated notes", "[generator]") {
    auto history = generator_history();
    auto existing = generated_note("existing", 120).value; existing.id = object_id_t{80};
    history.document.charts.front().measures.front().notes.push_back(existing);
    const auto collided = generator_preview_t::propose(history, generator_descriptor(), {.notes = {generated_note("a", 120)}});
    check_generator(!collided.value && !collided.diagnostics.empty());
    auto preview = preview_notes(history, generator_descriptor(), {generated_note("a", 240)});
    ++history.document.revision;
    check_generator(!apply_generator(history, preview).applied);
    check_generator(history.document.charts.front().measures.front().notes == std::vector{existing});
    check_generator(history.undo.empty());
}
TEST_CASE("generator identity reservations survive deleted live output and Undo records", "[generator]") {
    auto history = generator_history();
    auto preview = preview_notes(history, generator_descriptor(), {generated_note("a", 120)});
    require_generator(apply_generator(history, preview).applied);
    const auto reserved = history.document.generator_recipes.front().generated_notes.front().value.id.value;
    history.document.charts.front().measures.front().notes.clear();
    history.undo.clear(); history.redo.clear();
    const auto ceiling = id_ceiling_t<object_id_t>::from(history.document);
    check_generator(ceiling.maximum >= reserved);
    const auto counted = id_counter_t<object_id_t>::from(history.document, object_id_t{reserved});
    check_generator(counted.count == 0);
    require_generator(detach_generator(history, history.document.generator_recipes.front().id).applied);
    const auto next = id_ceiling_t<object_id_t>::from(history).next();
    require_generator(next.has_value());
    check_generator(next->value > reserved);
}
TEST_CASE("generator layer regeneration preserves unrelated layer order and memberships", "[generator]") {
    auto history = generator_history();
    history.document.charts.front().semantic_layers.push_back({.id = layer_id_t{70}, .name = "User"});
    generator_output_t output{.notes = {generated_note("a", 120)},
        .layers = {{"generated", {.name = "Generated"}}}};
    output.notes.front().layers = {"generated"};
    auto first = generator_preview_t::propose(history, generator_descriptor(), output);
    require_generator(first.value.has_value());
    require_generator(apply_generator(history, *first.value).applied);
    const auto generated = history.document.generator_recipes.front().generated_layers.front().value.id;
    output.notes.front().value.position.row = 240;
    auto next = generator_preview_t::propose(history, history.document.generator_recipes.front(), output);
    require_generator(next.value.has_value());
    require_generator(apply_generator(history, *next.value).applied);
    const auto& layers = history.document.charts.front().semantic_layers;
    check_generator(layers.front().id == layer_id_t{70});
    check_generator(layers.back().id == generated);
}

TEST_CASE("generator worker identity floor preserves scoped note and layer binding identities", "[generator][bms]") {
    auto flat = generator_history();
    auto history = document_history_t::from(test::native_bms_fixture(flat.document, flat.document.charts.front()));
    const auto measure = history.read_chart(chart_id_t{2})->measures.front().id;
    generator_output_t output{.notes = {generated_note("a", 120, measure)}, .layers = {{"layer", {.name = "Generated"}}}};
    output.notes.front().layers = {"layer"};
    auto preview = generator_preview_t::propose(history, generator_descriptor(), output, true, 10000);
    require_generator(preview.value.has_value());
    const auto note_id = preview.value->payload.recipe.generated_notes.front().value.id;
    const auto layer_id = preview.value->payload.recipe.generated_layers.front().value.id;
    check_generator(note_id.value > 10000 && layer_id.value > 10000);
    require_generator(apply_generator(history, *preview.value).applied);
    const auto* chart = history.read_chart(chart_id_t{2});
    require_generator(chart && chart->find_note(note_id));
    check_generator(chart->find_note(note_id)->semantic_layers == std::vector{layer_id});
    const auto& stored = history.document.generator_recipes.front();
    check_generator(stored.generated_notes.front().value == *chart->find_note(note_id));
    check_generator(stored.bms_scope.has_value() && stored.bms_chart.has_value());
    require_generator(undo_document_command(history));
    require_generator(redo_document_command(history));
    check_generator(history.read_chart(chart_id_t{2})->find_note(note_id) != nullptr);
}
TEST_CASE("generator rejects scoped owner retargeting and preserves unrelated semantic Charts", "[generator][bms]") {
    auto flat = generator_history();
    auto history = document_history_t::from(test::native_bms_fixture(flat.document, flat.document.charts.front()));
    const auto measure = history.read_chart(chart_id_t{2})->measures.front().id;
    auto preview = preview_notes(history, generator_descriptor(), {generated_note("a", 120, measure)});
    require_generator(apply_generator(history, preview).applied);
    const auto recipe = history.document.generator_recipes.front();
    const auto* owner = history.document.find_bms_program(chart_id_t{2});
    auto prepared = owner->prepared();
    const branch_id_t other{generator_identity_floor(history) + 1};
    const auto root = prepared.program.root();
    auto inserted = prepared.program.with_node(root, prepared.program.find(root)->children.back(),
        {.id = other, .value = bms_chart_content_t{}});
    require_generator(inserted.has_value());
    prepared.program = std::move(*inserted);
    auto replacement = bms_program_document_t::from(chart_id_t{2}, std::move(prepared));
    require_generator(replacement.value.has_value());
    history.document.prepared_bms_programs.front() = *replacement.value;
    history.bms_edit_target = bms_program_target_key_t{chart_id_t{2}, other};
    const auto rejected = generator_preview_t::propose(history, recipe, {.notes = {generated_note("a", 240, measure)}});
    check_generator(!rejected.value && !rejected.diagnostics.empty());
    check_generator(history.document.generator_recipes.front().bms_chart == recipe.bms_chart);
}
TEST_CASE("generator unchanged keys can regenerate at an exhausted identity ceiling", "[generator]") {
    auto history = generator_history();
    auto first = preview_notes(history, generator_descriptor(), {generated_note("a", 120)});
    require_generator(apply_generator(history, first).applied);
    auto historical = history.document;
    auto note = generated_note("history", 720).value; note.id = object_id_t{UINT64_MAX};
    historical.charts.front().measures.front().notes.push_back(note);
    history.redo.push_back({.before = historical, .after = historical});
    const auto recipe = history.document.generator_recipes.front();
    const auto unchanged = generator_preview_t::propose(history, recipe, {.notes = {generated_note("a", 120)}});
    check_generator(unchanged.value.has_value());
    const auto added = generator_preview_t::propose(history, recipe, {.notes = {generated_note("a", 120), generated_note("b", 240)}});
    check_generator(!added.value && !added.diagnostics.empty());
}

TEST_CASE("generator constructors reject chosen recipe IDs and Apply rejects forged materialization IDs", "[generator]") {
    auto history = generator_history();
    auto descriptor = generator_descriptor(); descriptor.id = recipe_id_t{91};
    check_generator(!generator_preview_t::propose(history, descriptor, {}).value);
    auto first = preview_notes(history, generator_descriptor(), {generated_note("a", 120)});
    auto invalid = first; invalid.payload.recipe.generated_notes.front().value.id = object_id_t{1};
    check_generator(!apply_generator(history, invalid).applied);
    require_generator(apply_generator(history, first).applied);
    const auto recipe = history.document.generator_recipes.front();
    auto next = preview_notes(history, recipe, {generated_note("a", 240)});
    next.payload.recipe.generated_notes.front().value.id = object_id_t{generator_identity_floor(history) + 100};
    check_generator(!apply_generator(history, next).applied);
    check_generator(history.undo.size() == 1);
}

TEST_CASE("generator layer definitions use normal editor name validation", "[generator]") {
    auto history = generator_history();
    std::string name;
    SECTION("empty") { name = ""; }
    SECTION("oversize") { name = std::string(4097, 'x'); }
    SECTION("NUL") { name = std::string{"bad\0name", 8}; }
    SECTION("invalid UTF8") { name = std::string(1, static_cast<char>(0xff)); }
    const auto result = generator_preview_t::propose(history, generator_descriptor(),
        {.layers = {{"layer", {.name = std::move(name)}}}});
    check_generator(!result.value && !result.diagnostics.empty());
    check_generator(history.undo.empty() && history.document.charts.front().semantic_layers.empty());
}
