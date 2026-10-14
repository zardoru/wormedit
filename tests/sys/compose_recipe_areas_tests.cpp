#include "sys/compose_recipe_areas.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto base_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
                      {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}};
    return chart;
}

auto linear_recipe(std::uint64_t id, local_position_t begin, local_position_t end,
                   signed_rational_t from, signed_rational_t to) -> sv_recipe_t {
    sv_recipe_t recipe{.id = recipe_id_t{id},
                       .name = "rise",
                       .range = {begin, end},
                       .output = sv_recipe_output_t::sv,
                       .curve = sv_recipe_curve_t::linear,
                       .parameters = {.value_from = from, .value_to = to}};
    recipe.sampling.fixed_point_count = 3;
    return recipe;
}

auto project(const chart_document_t& chart, std::size_t limit = 16) -> recipe_areas_t {
    const auto measures = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(measures));
    return build_recipe_areas_view(chart, std::get<measure_index_t>(measures), limit);
}

auto y_of(std::uint64_t row) { return static_cast<double>(row); }

} // namespace

TEST_CASE("recipe areas project each stored recipe with its sampled values", "[unit][sys][recipe-areas]") {
    auto chart = base_chart();
    chart.sv_recipes.push_back(linear_recipe(10, {measure_id_t{1}, 0}, {measure_id_t{2}, 0}, {1, 1}, {3, 1}));
    const auto result = project(chart);
    REQUIRE(result.areas.size() == 1);
    const auto& area = result.areas[0];
    CHECK(area.recipe == recipe_id_t{10});
    CHECK(area.evaluable);
    CHECK(area.begin_row == 0);
    CHECK(area.end_row == 960);
    REQUIRE(area.points.size() == 3);
    CHECK(area.points[0].value == Catch::Approx(1.0));
    CHECK(area.points[2].value == Catch::Approx(3.0));
    CHECK_FALSE(result.truncated);
}

TEST_CASE("a recipe ending at the chart end and a Lua expression recipe both get areas",
          "[unit][sys][recipe-areas]") {
    auto chart = base_chart();
    chart.sv_recipes.push_back(
        linear_recipe(10, {measure_id_t{2}, 0}, {measure_id_t{2}, 960}, {1, 1}, {2, 1}));
    auto expression = linear_recipe(11, {measure_id_t{1}, 0}, {measure_id_t{1}, 480}, {1, 1}, {1, 1});
    expression.curve = sv_recipe_curve_t::lua_expression;
    expression.expression_source = "return 1";
    chart.sv_recipes.push_back(expression);
    const auto result = project(chart);
    REQUIRE(result.areas.size() == 2);
    CHECK(result.areas[0].end_row == 1920);
    CHECK(result.areas[0].evaluable);
    CHECK_FALSE(result.areas[1].evaluable);
    CHECK(result.areas[1].points.empty());
    CHECK(result.areas[1].curve == sv_recipe_curve_t::lua_expression);
}

TEST_CASE("recipe areas skip invalid ranges and are bounded", "[unit][sys][recipe-areas]") {
    auto chart = base_chart();
    chart.sv_recipes.push_back(linear_recipe(10, {measure_id_t{1}, 0}, {measure_id_t{1}, 240}, {1, 1}, {2, 1}));
    chart.sv_recipes.push_back(linear_recipe(11, {measure_id_t{1}, 480}, {measure_id_t{1}, 240}, {1, 1}, {2, 1}));
    chart.sv_recipes.push_back(linear_recipe(12, {measure_id_t{1}, 500}, {measure_id_t{1}, 600}, {1, 1}, {2, 1}));
    const auto all = project(chart);
    CHECK(all.areas.size() == 2);
    CHECK(all.truncated);
    const auto cut = project(chart, 1);
    CHECK(cut.areas.size() == 1);
    CHECK(cut.truncated);
}

TEST_CASE("recipe graphs are continuous lines normalized to the visible window",
          "[unit][sys][recipe-areas]") {
    auto chart = base_chart();
    chart.sv_recipes.push_back(linear_recipe(10, {measure_id_t{1}, 0}, {measure_id_t{2}, 0}, {1, 1}, {3, 1}));
    const auto area = project(chart).areas.front();

    const auto full = build_recipe_graph(area, 0, 960, y_of);
    CHECK(full.minimum == Catch::Approx(1.0));
    CHECK(full.maximum == Catch::Approx(3.0));
    REQUIRE(full.points.size() == 3);
    CHECK(full.points[0].x == Catch::Approx(0.0));
    CHECK(full.points[1].x == Catch::Approx(0.5));
    CHECK(full.points[2].x == Catch::Approx(1.0));
    CHECK(full.points[1].y == 480.0);

    // A window onto the second half interpolates at its edge and renormalizes to what is visible.
    const auto half = build_recipe_graph(area, 480, 960, y_of);
    CHECK(half.minimum == Catch::Approx(2.0));
    CHECK(half.maximum == Catch::Approx(3.0));
    REQUIRE(half.points.size() == 2);
    CHECK(half.points.front().x == Catch::Approx(0.0));
    CHECK(half.points.back().x == Catch::Approx(1.0));

    CHECK(build_recipe_graph(area, 2000, 3000, y_of).points.empty());
    auto flat = area;
    for (auto& point : flat.points) point.value = 2.0;
    const auto centered = build_recipe_graph(flat, 0, 960, y_of);
    CHECK(centered.points.front().x == 0.5);
    auto unevaluable = area;
    unevaluable.evaluable = false;
    CHECK(build_recipe_graph(unevaluable, 0, 960, y_of).points.empty());
}
