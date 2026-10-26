#include "../support/bms_program_fixture.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <utility>
#include <wormedit/core/bms_program_edit.h>
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
