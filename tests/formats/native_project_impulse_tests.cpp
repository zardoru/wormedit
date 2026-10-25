#include <wormedit/formats/native_project.h>
#include "formats/native_project_migrations.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {
using namespace wormedit;
using json = nlohmann::json;
auto impulse_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{10},
        .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}}};
    chart.sv_recipes.push_back({.id = recipe_id_t{20}, .name = "Pulse",
        .range = {{measure_id_t{11}, 0}, {measure_id_t{12}, 0}},
        .impulses = {{.at = {1, 4}, .area = {-3, 2}}, {.at = {1, 4}, .area = {1, 2}}},
        .finite_box_width_milliseconds = {5, 2}});
    return {.id = song_id_t{1}, .charts = {chart}};
}
auto impulse_json() -> json {
    const auto written = write_native_project(impulse_song());
    REQUIRE(written.value);
    return json::parse(*written.value);
}
} // namespace

TEST_CASE("native recipes retain structural impulses and exact box width without sampled clouds",
          "[unit][format][native-project][sv-recipe][impulse]") {
    const auto encoded = impulse_json();
    const auto& recipe = encoded["document"]["charts"][0]["sv_recipes"][0];
    CHECK(recipe["impulses"].size() == 2);
    CHECK_FALSE(recipe.contains("points"));
    CHECK_FALSE(recipe.contains("impulse_boxes"));
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK(loaded.value->charts.front().sv_recipes.front() == impulse_song().charts.front().sv_recipes.front());
}

TEST_CASE("schema seventeen recipes gain empty impulses and one millisecond boxes",
          "[unit][format][native-project][sv-recipe][impulse][migration]") {
    auto encoded = impulse_json();
    auto& document = encoded["document"];
    document["schema_version"] = 17U;
    auto& recipe = document["charts"][0]["sv_recipes"][0];
    recipe.erase("impulses");
    recipe.erase("finite_box_width_milliseconds");
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    const auto& migrated = loaded.value->charts.front().sv_recipes.front();
    CHECK(migrated.impulses.empty());
    CHECK((migrated.finite_box_width_milliseconds == rational_t{1, 1}));
}

TEST_CASE("schema eighteen migration reaches scoped native impulse recipe payloads",
          "[unit][format][native-project][sv-recipe][impulse][migration]") {
    auto recipe = impulse_json()["document"]["charts"][0]["sv_recipes"][0];
    recipe.erase("impulses");
    recipe.erase("finite_box_width_milliseconds");
    json payload = {{"type", "sv_recipe"}, {"data", recipe}};
    json item = {{"value", {{"type", "native"}, {"data", {{"payload", payload}}}}}};
    json node = {{"value", {{"type", "chart"}, {"data", {{"items", json::array({item})}}}}}};
    json document = {{"schema_version", 17U}, {"charts", json::array()},
        {"prepared_bms_programs", json::array({{{"nodes", json::array({node})}}})}};
    const auto migrated = native_project_internal::migrate_document(
        document, 18, native_project_internal::default_migrations());
    REQUIRE(migrated);
    const auto& stored = document["prepared_bms_programs"][0]["nodes"][0]["value"]["data"]
        ["items"][0]["value"]["data"]["payload"]["data"];
    CHECK(stored["impulses"].empty());
    CHECK(stored["finite_box_width_milliseconds"]["numerator"] == 1);
}

TEST_CASE("native impulse declarations reject invalid rational domains",
          "[unit][format][native-project][sv-recipe][impulse]") {
    auto encoded = impulse_json();
    auto& recipe = encoded["document"]["charts"][0]["sv_recipes"][0];
    SECTION("exclusive-end impulse") { recipe["impulses"][0]["at"] = {{"numerator", 1}, {"denominator", 1U}}; }
    SECTION("zero area denominator") { recipe["impulses"][0]["area"]["denominator"] = 0U; }
    SECTION("zero width") { recipe["finite_box_width_milliseconds"]["numerator"] = 0U; }
    SECTION("negative width") { recipe["finite_box_width_milliseconds"]["numerator"] = -1; }
    CHECK_FALSE(parse_native_project(encoded.dump()).value);
}

TEST_CASE("schema twenty scrolls recipes load as SV recipes in charts and BMS scopes",
          "[unit][format][native-project][sv-recipe][migration]") {
    auto encoded = impulse_json();
    auto& document = encoded["document"];
    document["schema_version"] = 20U;
    document["charts"][0]["sv_recipes"][0]["output"] = 1;
    const auto loaded = parse_native_project(encoded.dump());
    REQUIRE(loaded.value);
    CHECK(loaded.value->charts.front().sv_recipes.front().output == sv_recipe_output_t::sv);

    auto recipe = impulse_json()["document"]["charts"][0]["sv_recipes"][0];
    recipe["output"] = 1;
    json payload = {{"type", "sv_recipe"}, {"data", recipe}};
    json item = {{"value", {{"type", "native"}, {"data", {{"payload", payload}}}}}};
    json node = {{"value", {{"type", "chart"}, {"data", {{"items", json::array({item})}}}}}};
    json scoped = {{"schema_version", 20U}, {"charts", json::array()},
        {"prepared_bms_programs", json::array({{{"nodes", json::array({node})}}})}};
    REQUIRE(native_project_internal::migrate_document(scoped, 21, native_project_internal::default_migrations()));
    CHECK(scoped["prepared_bms_programs"][0]["nodes"][0]["value"]["data"]["items"][0]["value"]["data"]
              ["payload"]["data"]["output"] == 0);
}

TEST_CASE("current native recipes reject the retired scrolls output value",
          "[unit][format][native-project][sv-recipe]") {
    auto encoded = impulse_json();
    encoded["document"]["charts"][0]["sv_recipes"][0]["output"] = 1;
    CHECK_FALSE(parse_native_project(encoded.dump()).value);
}
