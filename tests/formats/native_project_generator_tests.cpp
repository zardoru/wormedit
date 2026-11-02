#include <wormedit/core/osbx_edit.h>
#include "../support/bms_program_fixture.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <utility>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/generator.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>

namespace {
using namespace wormedit;
using json = nlohmann::json;
auto generator_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1},
        .charts = {{.id = chart_id_t{10}, .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}}}}};
    song.midi_assets.push_back({.id = asset_id_t{30}, .source_path = "input.mid", .ticks_per_quarter = 480});
    const std::string source = "return {notes={},layers={}}";
    song.generator_recipes.push_back({.id = recipe_id_t{40}, .api_version = "1",
        .source_path = "scripts/generate.lua", .input_assets = {{.asset = asset_id_t{30}, .content_hash = std::string(64, 'a')}},
        .parameters = {{.name = "density", .serialized_value = R"({"num":3,"den":2})"}},
        .seed = UINT64_MAX, .source_hash = native_project_content_checksum(source), .source = source,
        .chart = chart_id_t{10}, .generated_notes = {{.key = "one", .value = {.id = object_id_t{90},
            .position = {measure_id_t{11}, 120}, .lane = lane_id_t{1}}}},
        .generated_layers = {{.key = "generated", .value = {.id = layer_id_t{91}, .name = "Generated"}}}});
    return song;
}
auto generator_json() -> json {
    const auto written = write_native_project(generator_song());
    REQUIRE(written.value);
    return json::parse(*written.value);
}
void check_frozen_metadata(const generator_recipe_t& actual, const generator_recipe_t& expected) {
    CHECK(actual == expected);
    CHECK(actual.seed == UINT64_MAX);
    REQUIRE(actual.generated_notes.size() == 1);
    CHECK(actual.generated_notes.front().value.id == object_id_t{90});
}
void check_legacy_generator_source(const generator_recipe_t& recipe) {
    CHECK(recipe.source.empty());
    CHECK_FALSE(recipe.chart);
    CHECK(recipe.generated_notes.empty());
    CHECK(recipe.source_hash == "sha256:legacy");
}
void check_legacy_generator_inputs(const generator_recipe_t& recipe) {
    REQUIRE(recipe.input_assets.size() == 1);
    CHECK(recipe.input_assets.front().asset == asset_id_t{30});
    CHECK(recipe.input_assets.front().content_hash.empty());
}
auto inactive_generator_song() -> song_document_t {
    auto prepared = test::bms_program::prepared(test::bms_program::scoped_source);
    bms_program_evaluation_t evaluation;
    for (const auto selector : test::bms_program::selectors(prepared.program))
        evaluation.selections.push_back({selector, 1});
    auto owner = bms_program_document_t::from(chart_id_t{9000}, std::move(prepared), std::move(evaluation));
    REQUIRE(owner.value);
    song_document_t song{.id = song_id_t{9001}, .prepared_bms_programs = {*owner.value}};
    const auto& nodes = song.prepared_bms_programs.front()->prepared().program.nodes();
    const auto inactive = std::ranges::find_if(nodes, [](const auto& node) {
        return std::holds_alternative<bms_else_if_scope_t>(node.value);
    });
    REQUIRE(inactive != nodes.end());
    const auto target = bms_program_edit_target_t::for_authoring(song, chart_id_t{9000}, inactive->id);
    REQUIRE(target);
    const std::string source = "return {notes={},layers={}}";
    song.generator_recipes.push_back({.id = recipe_id_t{9002}, .api_version = "1",
        .source_hash = native_project_content_checksum(source), .source = source,
        .chart = chart_id_t{9000}, .bms_scope = target->scope, .bms_chart = target->chart});
    return song;
}
void apply_storyboard_output(document_history_t& history, const generator_recipe_t& recipe,
    generator_output_t output) {
    auto proposal = generator_preview_t::propose(history, recipe, std::move(output));
    REQUIRE(proposal.value);
    REQUIRE(apply_generator(history, *proposal.value).applied);
}
auto reopen_storyboard_project(const song_document_t& song) -> document_history_t {
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    const auto loaded = parse_native_project(*written.value);
    INFO((loaded.diagnostics.empty() ? "success" : loaded.diagnostics.front().message));
    REQUIRE(loaded.value);
    CHECK(loaded.value->storyboards == song.storyboards);
    CHECK(loaded.value->generator_recipes == song.generator_recipes);
    return document_history_t::from(*loaded.value);
}
void check_storyboard_state(const song_document_t& actual, const song_document_t& expected) {
    CHECK(actual.storyboards == expected.storyboards);
    CHECK(actual.generator_recipes == expected.generator_recipes);
    CHECK(actual.revision == expected.revision);
}
void check_storyboard_detach(document_history_t& history, recipe_id_t recipe) {
    const auto scene = history.document.storyboards.front().osbx;
    REQUIRE(detach_generator(history, recipe).applied);
    CHECK_FALSE(history.document.storyboards.front().owning_generator_recipe);
    CHECK(history.document.storyboards.front().osbx == scene);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.storyboards.front().owning_generator_recipe == recipe);
}
void check_forged_storyboard_identity(document_history_t& history, generator_proposal_t proposal) {
    proposal.payload.recipe.generated_storyboards.front().storyboard = storyboard_id_t{proposal.payload.recipe.id.value};
    const auto before = history.document;
    CHECK_FALSE(apply_generator(history, proposal).applied);
    check_storyboard_state(history.document, before);
    CHECK(history.undo.empty());
}
void check_deleted_storyboard_witness(document_history_t& history, const generator_proposal_t& proposal) {
    REQUIRE(apply_generator(history, proposal).applied);
    const auto stored = history.document.generator_recipes.back();
    history.document.storyboards.clear();
    ++history.document.revision;
    const auto before = history.document;
    const auto attempted = generator_preview_t::propose(history, stored, {});
    CHECK_FALSE(attempted.value);
    REQUIRE_FALSE(attempted.diagnostics.empty());
    CHECK(attempted.diagnostics.front().code == diagnostic_code_t::invalid_binding);
    check_storyboard_state(history.document, before);
}
} // namespace

TEST_CASE("native generator recipes retain frozen inputs and historical materialization witnesses",
          "[unit][format][native-project][generator]") {
    const auto encoded = generator_json();
    CHECK(encoded["document"]["schema_version"] == current_document_schema_version);
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    REQUIRE(loaded.value->generator_recipes.size() == 1);
    check_frozen_metadata(loaded.value->generator_recipes.front(), generator_song().generator_recipes.front());
}

TEST_CASE("native schema23 additions migrate and decode together without losing frozen inputs",
          "[unit][format][native-project][generator][sv-recipe][migration]") {
    auto encoded = generator_json();
    encoded["document"]["schema_version"] = GENERATE(22U, 23U);
    encoded["document"].erase("sv_recipe_definitions");
    encoded["document"]["generator_recipes"][0].erase("generated_charts");
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK(loaded.value->schema_version == current_document_schema_version);
    CHECK(loaded.value->sv_recipe_definitions.empty());
    CHECK(loaded.value->generator_recipes.front().generated_charts.empty());
    check_frozen_metadata(loaded.value->generator_recipes.front(),
                          generator_song().generator_recipes.front());
}

TEST_CASE("schema eighteen generator metadata migrates without evaluating or losing legacy fields",
          "[unit][format][native-project][generator][migration]") {
    auto encoded = generator_json();
    auto& document = encoded["document"];
    document["schema_version"] = 18U;
    auto& recipe = document["generator_recipes"][0];
    for (const auto* field : {"source", "chart", "bms_scope", "bms_chart", "generated_notes", "generated_layers"})
        recipe.erase(field);
    recipe["input_assets"] = json::array({30U});
    recipe["source_hash"] = "sha256:legacy";
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    const auto& migrated = loaded.value->generator_recipes.front();
    check_legacy_generator_source(migrated);
    check_legacy_generator_inputs(migrated);
}

TEST_CASE("native load preserves generator source without implicitly materializing its outputs",
          "[unit][format][native-project][generator]") {
    auto encoded = generator_json();
    encoded["document"]["generator_recipes"][0]["source"] = "error('must never execute while loading')";
    encoded["document"]["generator_recipes"][0]["source_hash"] =
        native_project_content_checksum("error('must never execute while loading')");
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK(loaded.value->charts.front().measures.front().notes.empty());
    CHECK(loaded.value->charts.front().semantic_layers.empty());
    CHECK(loaded.value->generator_recipes.front().source.find("must never execute") != std::string::npos);
}

TEST_CASE("native generator input identity and numeric widths reject malformed persisted data",
          "[unit][format][native-project][generator]") {
    auto encoded = generator_json();
    auto& recipe = encoded["document"]["generator_recipes"][0];
    SECTION("missing input asset") { recipe["input_assets"][0]["asset"] = 999U; }
    SECTION("negative seed") { recipe["seed"] = -1; }
    SECTION("fractional row") { recipe["generated_notes"][0]["value"]["position"]["row"] = 0.5; }
    SECTION("oversized color") { recipe["generated_layers"][0]["value"]["color"]["red"] = 256U; }
    SECTION("missing target chart") { recipe["chart"] = 999U; }
    CHECK_FALSE(parse_native_project(encoded.dump()).value);
}

TEST_CASE("native generator bindings preserve inactive BMS authoring owners",
          "[unit][format][native-project][generator][bms]") {
    const auto song = inactive_generator_song();
    const auto& recipe = song.generator_recipes.front();
    CHECK_FALSE(bms_program_edit_target_t::from(song, recipe.chart, recipe.bms_scope));
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    const auto loaded = parse_native_project(*written.value);
    REQUIRE(loaded.value);
    CHECK(loaded.value->generator_recipes.front() == recipe);
    CHECK(bms_program_edit_target_t::for_authoring(*loaded.value, recipe.chart, *recipe.bms_scope));
}

TEST_CASE("native generator bindings reject impossible saved BMS scope and Chart pairs",
          "[unit][format][native-project][generator][bms]") {
    const auto song = inactive_generator_song();
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    auto encoded = json::parse(*written.value);
    auto& recipe = encoded["document"]["generator_recipes"][0];
    SECTION("Chart used as its containing scope") { recipe["bms_scope"] = recipe["bms_chart"]; }
    SECTION("Chart belongs to another scope") {
        const auto& program = song.prepared_bms_programs.front()->prepared().program;
        const auto root_chart = program.target_chart(program.root());
        REQUIRE(root_chart);
        recipe["bms_chart"] = root_chart->value;
    }
    CHECK_FALSE(parse_native_project(encoded.dump()).value);
}

TEST_CASE("native generator targets retain an explicit later Chart within the same scope",
          "[unit][format][native-project][generator][bms]") {
    auto song = inactive_generator_song();
    const auto& owner = song.prepared_bms_programs.front();
    auto prepared = owner->prepared();
    const auto root = prepared.program.root();
    const auto* root_node = prepared.program.find(root);
    REQUIRE(root_node);
    const auto extended = prepared.program.with_node(root, root_node->children.back(),
        {.id = branch_id_t{10000}, .value = bms_chart_content_t{}});
    REQUIRE(extended);
    prepared.program = *extended;
    const auto replacement = bms_program_document_t::from(owner->difficulty(), std::move(prepared), owner->evaluation());
    REQUIRE(replacement.value);
    song.prepared_bms_programs.front() = *replacement.value;
    auto& recipe = song.generator_recipes.front();
    recipe.bms_scope = root;
    recipe.bms_chart = branch_id_t{10000};
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    const auto loaded = parse_native_project(*written.value);
    REQUIRE(loaded.value);
    CHECK(loaded.value->generator_recipes.front() == recipe);
}

TEST_CASE("native storyboard bindings retain witnesses and support regeneration after reopen",
          "[unit][format][native-project][osbx-generator]") {
    auto history = document_history_t::from(generator_song());
    const std::string source = "storyboard 'intro'; group 'root'";
    generator_recipe_t recipe{.api_version = "1", .source_hash = native_project_content_checksum(source),
        .source = source, .chart = chart_id_t{10}};
    const osbx_document_t generated{.nodes = {{.id = "root"}}};
    apply_storyboard_output(history, recipe, {.storyboards = {{.key = "intro", .value = generated}}});
    const auto id = history.document.storyboards.front().id;
    auto reopened = reopen_storyboard_project(history.document);
    recipe = reopened.document.generator_recipes.back();
    auto changed = generated; changed.canvas.width = 800;
    const auto proposal = generator_preview_t::propose(reopened, recipe, {.storyboards = {{.key = "intro", .value = changed}}});
    REQUIRE(proposal.value);
    CHECK(proposal.value->payload.storyboards.changed == 1);
    REQUIRE(apply_generator(reopened, *proposal.value).applied);
    CHECK(reopened.document.storyboards.front().id == id);
    CHECK(reopened.document.storyboards.front().osbx == changed);
}

TEST_CASE("existing schema29 snapshots default absent storyboard bindings safely",
          "[unit][format][native-project][osbx-generator]") {
    auto encoded = generator_json();
    for (auto& recipe : encoded["document"]["generator_recipes"]) recipe.erase("generated_storyboards");
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK(loaded.value->generator_recipes.front().generated_storyboards.empty());
}

TEST_CASE("storyboard generator omissions and detach respect scene ownership",
          "[unit][format][native-project][osbx-generator]") {
    auto history = document_history_t::from(generator_song());
    const std::string source = "storyboard 'intro'";
    generator_recipe_t recipe{.api_version = "1", .source_hash = native_project_content_checksum(source),
        .source = source, .chart = chart_id_t{10}};
    apply_storyboard_output(history, recipe, {.storyboards = {{.key = "intro"}, {.key = "outro"}}});
    recipe = history.document.generator_recipes.back();
    const auto proposal = generator_preview_t::propose(history, recipe, {.storyboards = {{.key = "intro"}}});
    REQUIRE(proposal.value);
    CHECK(proposal.value->payload.storyboards.removed == 1);
    REQUIRE(apply_generator(history, *proposal.value).applied);
    REQUIRE(history.document.storyboards.size() == 1);
    check_storyboard_detach(history, recipe.id);
}

TEST_CASE("unretained storyboard generation leaves editable scenes without dangling ownership",
          "[unit][format][native-project][osbx-generator]") {
    auto history = document_history_t::from(generator_song());
    const std::string source = "storyboard 'intro'";
    const generator_recipe_t recipe{.api_version = "1", .source_hash = native_project_content_checksum(source),
        .source = source, .chart = chart_id_t{10}};
    const auto proposal = generator_preview_t::propose(history, recipe, {.storyboards = {{.key = "intro"}}}, false);
    REQUIRE(proposal.value);
    REQUIRE(apply_generator(history, *proposal.value).applied);
    REQUIRE(history.document.storyboards.size() == 1);
    CHECK_FALSE(history.document.storyboards.front().owning_generator_recipe);
    CHECK(history.document.generator_recipes.size() == 1);
}

TEST_CASE("storyboard regeneration rejects deleted witnesses and forged identities atomically",
          "[unit][format][native-project][osbx-generator]") {
    auto history = document_history_t::from(generator_song());
    const std::string source = "storyboard 'intro'";
    const generator_recipe_t recipe{.api_version = "1", .source_hash = native_project_content_checksum(source),
        .source = source, .chart = chart_id_t{10}};
    auto proposal = generator_preview_t::propose(history, recipe, {.storyboards = {{.key = "intro"}}});
    REQUIRE(proposal.value);
    SECTION("recipe identity cannot be reused for the scene") {
        check_forged_storyboard_identity(history, *proposal.value);
    }
    SECTION("deleted scene cannot be recreated implicitly") {
        check_deleted_storyboard_witness(history, *proposal.value);
    }
}

TEST_CASE("native storyboard witnesses reserve deleted output identities after save load", "[unit][format][native-project][osbx-generator]") {
    auto song = generator_song();
    song.generator_recipes.front().generated_storyboards.push_back({.key = "missing", .storyboard = storyboard_id_t{10000}});
    const auto history = reopen_storyboard_project(song);
    CHECK(history.document.storyboards.empty());
    CHECK(history.undo.empty());
    CHECK(generator_identity_floor(history) >= 10000);
    const auto proposal = generator_preview_t::propose(history,
        {.api_version = "1", .source_hash = native_project_content_checksum("return {notes={}}"),
         .source = "return {notes={}}", .chart = chart_id_t{10}}, {.storyboards = {{.key = "new"}}});
    REQUIRE(proposal.value);
    CHECK(proposal.value->payload.recipe.generated_storyboards.front().storyboard.value > 10000);
}


namespace {
auto persisted_detach_history() -> document_history_t {
    auto history = document_history_t::from(generator_song());
    const std::string source = "storyboard 'intro'";
    generator_recipe_t recipe{.api_version = "1", .source_path = "scripts/story.lua",
        .source_hash = native_project_content_checksum(source), .source = source, .chart = chart_id_t{10}};
    const osbx_document_t generated{.nodes = {{.id = "root", .lifetime = osbx_lifetime_t{.begin_ms = 10, .end_ms = 300}}}};
    apply_storyboard_output(history, recipe, {.storyboards = {{.key = "intro", .value = generated}, {.key = "outro"}}});
    const auto& board = history.document.storyboards.front();
    REQUIRE(apply_osbx_edit(history, {.storyboard = board.id,
        .change = osbx_detach_storyboard_t{.expected_owner = *board.owning_generator_recipe}}).applied);
    return reopen_storyboard_project(history.document);
}
void check_persisted_detach(const document_history_t& history) {
    CHECK_FALSE(history.document.storyboards.front().owning_generator_recipe);
    REQUIRE(history.document.storyboards.front().osbx);
    const osbx_lifetime_t lifetime{.begin_ms = 10, .end_ms = 300};
    CHECK(history.document.storyboards.front().osbx->nodes.front().lifetime == lifetime);
    CHECK(history.document.generator_recipes.back().generated_storyboards.size() == 1);
}
} // namespace

TEST_CASE("native per-storyboard detach persists ownership separation and preserves scene lifetime", "[unit][format][native-project][storyboard-detach]") {
    const auto history = persisted_detach_history();
    check_persisted_detach(history);
}

TEST_CASE("native reopened detached storyboard survives same-key regeneration with a fresh identity", "[unit][format][native-project][storyboard-detach]") {
    auto history = persisted_detach_history();
    REQUIRE(apply_osbx_edit(history, {.storyboard = history.document.storyboards.front().id,
        .change = osbx_add_node_t{}}).applied);
    const auto detached = history.document.storyboards.front();
    const auto recipe = history.document.generator_recipes.back();
    const auto proposal = generator_preview_t::propose(history, recipe,
        {.storyboards = {{.key = "intro"}, {.key = "outro"}}});
    REQUIRE(proposal.value);
    const auto generated = proposal.value->payload.recipe.generated_storyboards.front().storyboard;
    CHECK(generated.value > detached.id.value);
    REQUIRE(apply_generator(history, *proposal.value).applied);
    CHECK(history.document.storyboards.front() == detached);
    const auto reopened = reopen_storyboard_project(history.document);
    CHECK(reopened.document.storyboards.front() == detached);
}
