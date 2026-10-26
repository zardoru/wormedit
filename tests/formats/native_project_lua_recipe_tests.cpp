#include <wormedit/formats/native_project.h>
#include "formats/native_project_migrations.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace {
using namespace wormedit;
using json = nlohmann::json;

auto make_lua_recipe_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1}};
    chart_document_t chart{
        .id = chart_id_t{10},
        .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}},
    };
    chart.sv_recipes.push_back({
        .id = recipe_id_t{20},
        .name = "Lua rise",
        .range = {{measure_id_t{11}, 0}, {measure_id_t{12}, 0}},
        .curve = sv_recipe_curve_t::lua_expression,
        .expression_source = "return function(p, ctx) return 1 + p end",
        .lua = sv_recipe_lua_t{},
    });
    song.charts.push_back(std::move(chart));
    return song;
}

auto written_json(const song_document_t& song) -> json {
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    return json::parse(*written.value);
}
} // namespace

TEST_CASE("native Lua recipe descriptors retain frozen authoring inputs",
          "[unit][format][native-project][sv-recipe][lua]") {
    auto song = make_lua_recipe_song();
    song.midi_assets.push_back({.id = asset_id_t{30}, .source_path = "reference.mid",
                                .ticks_per_quarter = 480});
    auto& descriptor = *song.charts.front().sv_recipes.front().lua;
    descriptor.entry_point = "sample";
    descriptor.source_reference = "recipes/rise.lua";
    descriptor.source_hash = std::string(64, 'a');
    descriptor.parameters = {{.name = "height", .value = {-3, 2}}};
    descriptor.inputs = {{.asset = asset_id_t{30}, .content_hash = std::string(64, 'b')}};
    descriptor.seed = std::numeric_limits<std::uint64_t>::max();
    descriptor.mode = sv_recipe_lua_mode_t::ordered;
    descriptor.cycles = {{.emission_count = 1}, {.emission_count = 1}};
    const auto encoded = written_json(song);
    const auto& stored = encoded["document"]["charts"][0]["sv_recipes"][0];
    CHECK(encoded["document"]["schema_version"] == current_document_schema_version);
    CHECK(stored["lua"]["parameters"][0]["value"]["numerator"] == -3);
    CHECK_FALSE(stored.contains("points"));
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK(loaded.value->charts.front().sv_recipes.front() == song.charts.front().sv_recipes.front());
}

TEST_CASE("schema sixteen Lua recipes migrate to pure API one descriptors",
          "[unit][format][native-project][sv-recipe][lua][migration]") {
    const auto song = make_lua_recipe_song();
    auto encoded = written_json(song);
    auto& document = encoded["document"];
    document["schema_version"] = 16U;
    document["charts"][0]["sv_recipes"][0].erase("lua");
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    const auto& recipe = loaded.value->charts.front().sv_recipes.front();
    REQUIRE(recipe.lua);
    CHECK(*recipe.lua == sv_recipe_lua_t{});
    CHECK(recipe.expression_source == song.charts.front().sv_recipes.front().expression_source);
}

TEST_CASE("schema sixteen migration reaches scoped native Lua recipes",
          "[unit][format][native-project][sv-recipe][lua][migration]") {
    auto recipe = written_json(make_lua_recipe_song())["document"]["charts"][0]["sv_recipes"][0];
    recipe.erase("lua");
    json payload = {{"type", "sv_recipe"}, {"data", recipe}};
    json item = {{"value", {{"type", "native"}, {"data", {{"payload", payload}}}}}};
    json node = {{"value", {{"type", "chart"}, {"data", {{"items", json::array({item})}}}}}};
    json document = {{"schema_version", 16U}, {"charts", json::array()},
                     {"prepared_bms_programs", json::array({{{"nodes", json::array({node})}}})}};
    const auto migrated = native_project_internal::migrate_document(
        document, 17, native_project_internal::default_migrations());
    REQUIRE(migrated);
    const auto& descriptor = document["prepared_bms_programs"][0]["nodes"][0]
        ["value"]["data"]["items"][0]["value"]["data"]["payload"]["data"]["lua"];
    CHECK(descriptor["api_version"] == 1);
    CHECK(descriptor["mode"] == 0);
    CHECK(descriptor["cycles"].empty());
    CHECK(descriptor["source_reference"].is_null());
}

TEST_CASE("native Lua recipe descriptor numeric widths are checked before decoding",
          "[unit][format][native-project][sv-recipe][lua]") {
    auto encoded = written_json(make_lua_recipe_song());
    auto& descriptor = encoded["document"]["charts"][0]["sv_recipes"][0]["lua"];
    const auto oversized = std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1;
    SECTION("API version") { descriptor["api_version"] = oversized; }
    SECTION("cycle count") { descriptor["cycles"] = json::array({{{"emission_count", oversized}}}); }
    SECTION("mode") { descriptor["mode"] = 2U; }
    SECTION("fractional seed") { descriptor["seed"] = 0.5; }
    SECTION("negative seed") { descriptor["seed"] = -1; }
    CHECK_FALSE(parse_native_project(encoded.dump()).value);
}

TEST_CASE("schema sixteen non-Lua recipes migrate without a Lua descriptor",
          "[unit][format][native-project][sv-recipe][lua][migration]") {
    auto song = make_lua_recipe_song();
    auto& recipe = song.charts.front().sv_recipes.front();
    recipe.curve = sv_recipe_curve_t::linear;
    recipe.expression_source.clear();
    recipe.lua.reset();
    auto encoded = written_json(song);
    encoded["document"]["schema_version"] = 16U;
    encoded["document"]["charts"][0]["sv_recipes"][0].erase("lua");
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK_FALSE(loaded.value->charts.front().sv_recipes.front().lua);
}

TEST_CASE("native Lua inputs resolve against every asset family and reject dangling IDs",
          "[unit][format][native-project][sv-recipe][lua]") {
    auto song = make_lua_recipe_song();
    song.audio_assets.push_back({.id = asset_id_t{30}, .source_path = "audio.ogg"});
    song.visual_assets.push_back({.id = asset_id_t{31}, .kind = asset_kind_t::image,
                                 .source_path = "image.png"});
    song.visual_assets.push_back({.id = asset_id_t{32}, .kind = asset_kind_t::video,
                                 .source_path = "video.mp4"});
    song.midi_assets.push_back({.id = asset_id_t{33}, .source_path = "reference.mid",
                                .ticks_per_quarter = 480});
    auto& descriptor = *song.charts.front().sv_recipes.front().lua;
    const auto hash = std::string(64, 'a');
    descriptor.inputs = {{asset_id_t{30}, hash}, {asset_id_t{31}, hash},
                         {asset_id_t{32}, hash}, {asset_id_t{33}, hash}};
    const auto encoded = written_json(song);
    CHECK(parse_native_project(encoded.dump()).value.has_value());
    auto dangling = encoded;
    dangling["document"]["charts"][0]["sv_recipes"][0]["lua"]["inputs"][0]["asset"] = 999U;
    CHECK_FALSE(parse_native_project(dangling.dump()).value);
    descriptor.inputs.front().asset = asset_id_t{999};
    const auto diagnostics = validate_document(song);
    CHECK(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::dangling_asset;
    }));
    CHECK_FALSE(write_native_project(song).value);
}

namespace {
void check_shared_recipe(bool value) { CHECK(value); }
void require_shared_recipe(bool value) { REQUIRE(value); }
} // namespace

TEST_CASE("Native shared recipe definitions persist once and restore each placement", "[unit][format][native-project][sv-recipe]") {
    auto song = make_lua_recipe_song();
    const auto definition = sv_recipe_definition_t::from(recipe_id_t{20}, song.charts.front().sv_recipes.front());
    song.sv_recipe_definitions.push_back(definition);
    song.charts.front().sv_recipes.front() = definition.make_placement(recipe_id_t{20},
        {{measure_id_t{11}, 0}, {measure_id_t{12}, 0}}, true);
    song.charts.front().sv_recipes.push_back(definition.make_placement(recipe_id_t{21},
        {{measure_id_t{12}, 0}, {measure_id_t{12}, 120}}, false));
    const auto encoded = written_json(song);
    check_shared_recipe(encoded["document"]["sv_recipe_definitions"].size() == 1);
    check_shared_recipe(!encoded["document"]["charts"][0]["sv_recipes"][0].contains("expression_source"));
    const auto loaded = parse_native_project(encoded.dump());
    require_shared_recipe(loaded.value.has_value());
    check_shared_recipe(loaded.value->sv_recipe_definitions == song.sv_recipe_definitions);
    check_shared_recipe(loaded.value->charts.front().sv_recipes == song.charts.front().sv_recipes);
    auto broken = encoded;
    broken["document"]["sv_recipe_definitions"] = json::array();
    check_shared_recipe(!parse_native_project(broken.dump()).value);
}

TEST_CASE("Schema22 recipes migrate without changing their independent definitions", "[unit][format][native-project][migration][sv-recipe]") {
    const auto song = make_lua_recipe_song();
    auto encoded = written_json(song);
    encoded["document"]["schema_version"] = 22;
    encoded["document"].erase("sv_recipe_definitions");
    const auto loaded = parse_native_project(encoded.dump());
    require_shared_recipe(loaded.value.has_value());
    check_shared_recipe(loaded.value->sv_recipe_definitions.empty());
    check_shared_recipe(loaded.value->charts.front().sv_recipes == song.charts.front().sv_recipes);
}

TEST_CASE("Additive schema23 files without SV definitions retain independent recipes", "[unit][format][native-project][sv-recipe]") {
    const auto song = make_lua_recipe_song();
    auto encoded = written_json(song);
    encoded["document"].erase("sv_recipe_definitions");
    const auto loaded = parse_native_project(encoded.dump());
    require_shared_recipe(loaded.value.has_value());
    check_shared_recipe(loaded.value->sv_recipe_definitions.empty());
    check_shared_recipe(loaded.value->charts.front().sv_recipes == song.charts.front().sv_recipes);
}

TEST_CASE("Schema22 migration adds SV definitions even without generator metadata",
          "[unit][format][native-project][sv-recipe][migration]") {
    json document = {{"schema_version", 22U}};
    const auto migrated = native_project_internal::migrate_document(
        document, 23, native_project_internal::default_migrations());
    REQUIRE(migrated);
    CHECK(document.at("sv_recipe_definitions") == json::array());
}
