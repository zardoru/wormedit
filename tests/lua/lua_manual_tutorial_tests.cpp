// The manual's Lua chapter tells readers to run these scripts against its tutorial project.
#include "../../src/scripting/lua_curve.h"
#include "../../src/scripting/lua_generator.h"
#include "../../src/scripting/lua_project.h"
#include <wormedit/core/sv_recipe.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
using namespace wormedit;
using namespace wormedit::scripting;
namespace {
const std::filesystem::path tutorial_dir{WORMEDIT_MANUAL_TUTORIAL_DIR};

auto read_file(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
auto tutorial_history() -> document_history_t {
    auto parsed = parse_native_project(read_file(tutorial_dir / "tutorial-empty.wormedit"));
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value);
    return document_history_t::from(std::move(*parsed.value));
}
auto tutorial_chart(const document_history_t& history) -> const chart_document_t& {
    const auto charts = history.document.read_charts();
    REQUIRE(charts.size() == 1);
    return *charts.front();
}
auto script_recipe(const document_history_t& history, const char* path) -> generator_recipe_t {
    generator_recipe_t recipe{.api_version = "1", .source_path = path, .seed = 1,
                              .chart = tutorial_chart(history).id};
    recipe.source = read_file(tutorial_dir / path);
    recipe.source_hash = native_project_content_checksum(recipe.source);
    return recipe;
}
auto run(const document_history_t& history, const generator_recipe_t& recipe) -> diagnosed_t<generator_output_t> {
    auto output = run_lua_generator(history.document, tutorial_chart(history), recipe, {},
                                    {.maximum_result_bytes = 8U * 1024U * 1024U});
    INFO((output.diagnostics.empty() ? "success" : output.diagnostics.front().message));
    REQUIRE(output.ok());
    return output;
}
void require_applies(document_history_t& history, const generator_recipe_t& recipe, generator_output_t output) {
    auto proposal = generator_preview_t::propose(history, recipe, std::move(output));
    INFO((proposal.diagnostics.empty() ? "success" : proposal.diagnostics.front().message));
    REQUIRE(proposal.value.has_value());
    REQUIRE(apply_generator(history, *proposal.value).applied);
}
auto distinct_lanes(const generator_output_t& output) -> std::set<lane_id_t> {
    std::set<lane_id_t> lanes;
    for (const auto& note : output.notes) lanes.insert(note.value.lane);
    return lanes;
}
auto repeats_a_lane(const generator_output_t& output) -> bool {
    for (std::size_t index = 1; index < output.notes.size(); ++index)
        if (output.notes[index].value.lane == output.notes[index - 1].value.lane) return true;
    return false;
}
auto wobble(sv_recipe_sampling_t sampling) -> sv_recipe_sample_result_t {
    const auto history = tutorial_history();
    auto chart = tutorial_chart(history);
    sv_recipe_t recipe{.id = recipe_id_t{900}, .name = "Wobble",
        .range = {{chart.measures[0].id, 0}, {chart.measures[2].id, 0}},
        .curve = sv_recipe_curve_t::lua_expression,
        .expression_source = read_file(tutorial_dir / "scripts/sv/wobble.lua")};
    recipe.lua = sv_recipe_lua_t{};
    recipe.sampling = sampling;
    lua_curve_backend_t backend;
    const sv_recipe_evaluation_scope_t scope{backend};
    return sample_sv_recipe(chart, recipe);
}
}

TEST_CASE("Manual first-note script places one note in a new layer", "[lua-manual]") {
    auto history = tutorial_history();
    const auto recipe = script_recipe(history, "scripts/first-note.lua");
    auto output = run(history, recipe);
    CHECK(output.value.notes.size() == 1);
    CHECK(output.value.layers.size() == 1);
    require_applies(history, recipe, std::move(output.value));
}

TEST_CASE("Manual stream script fills measures on playable lanes and follows its parameters", "[lua-manual]") {
    auto history = tutorial_history();
    auto recipe = script_recipe(history, "scripts/stream.lua");
    const auto defaults = run(history, recipe);
    CHECK(defaults.value.notes.size() == 8);
    recipe.parameters = {{"first", "2"}, {"count", "2"}, {"snap", "16"}};
    auto output = run(history, recipe);
    CHECK(output.value.notes.size() == 32);
    CHECK(distinct_lanes(output.value).size() > 1);
    CHECK_FALSE(distinct_lanes(output.value).contains(lane_id_t{108}));  // P1 scratch
    CHECK_FALSE(repeats_a_lane(output.value));
    const auto again = run(history, recipe);
    CHECK(again.value.notes == output.value.notes);
    require_applies(history, recipe, std::move(output.value));
}

TEST_CASE("Manual project.lua combines its included patterns into one preview", "[lua-manual]") {
    auto history = tutorial_history();
    auto recipe = script_recipe(history, "scripts/project.lua");
    const auto frozen = lua_project_source_t::from(history.document, tutorial_chart(history), recipe, {},
                                                   tutorial_dir);
    INFO((frozen.diagnostics.empty() ? "success" : frozen.diagnostics.front().message));
    REQUIRE(frozen.ok());
    recipe.source = frozen.value.source;
    recipe.source_hash = native_project_content_checksum(recipe.source);
    auto output = run(history, recipe);
    CHECK(output.value.notes.size() == 4 * 2 + 8 * 2);
    CHECK(output.value.layers.size() == 2);
    require_applies(history, recipe, std::move(output.value));
}

TEST_CASE("Manual easy.chart.lua creates a separate difficulty", "[lua-manual]") {
    auto history = tutorial_history();
    const auto recipe = script_recipe(history, "scripts/easy.chart.lua");
    auto output = run(history, recipe);
    const auto before = history.document.read_charts().size();
    require_applies(history, recipe, std::move(output.value));
    REQUIRE(history.document.read_charts().size() == before + 1);
    const auto& created = *history.document.read_charts().back();
    CHECK(created.measures.size() == 6);
    std::size_t notes{};
    for (const auto& measure : created.measures) notes += measure.notes.size();
    CHECK(notes == 41);  // the count the manual quotes from the preview
}

TEST_CASE("Manual wobble curve needs Grid points because Auto only checks midpoints", "[lua-manual]") {
    const auto automatic = wobble({.mode = sv_recipe_sampling_mode_t::adaptive, .fixed_point_count = std::nullopt});
    INFO((automatic.diagnostics.empty() ? "success" : automatic.diagnostics.front().message));
    REQUIRE(automatic);
    CHECK(automatic.points.size() == 2);  // every midpoint lands where the wave crosses 1
    const auto grid = wobble({.fixed_point_count = std::nullopt, .fixed_step_rows = 60});
    INFO((grid.diagnostics.empty() ? "success" : grid.diagnostics.front().message));
    REQUIRE(grid);
    CHECK(grid.points.size() > 30);
}
