#include <wormedit/core/sv_recipe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace {
using namespace wormedit;
auto make_impulse_chart() -> chart_document_t {
    return {.id = chart_id_t{10}, .divisions_per_standard_measure = 4000,
        .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}},
        .bpms = {{.id = object_id_t{15}, .position = {measure_id_t{11}, 0}, .beats_per_minute = {120, 1}}}};
}
auto make_impulse_recipe() -> sv_recipe_t {
    return {.id = recipe_id_t{20}, .name = "Impulse",
        .range = {{measure_id_t{11}, 0}, {measure_id_t{12}, 0}},
        .curve = sv_recipe_curve_t::constant,
        .impulses = {{.at = {1, 4}, .area = {1, 4}}}};
}
auto point_at(std::span<const sv_recipe_point_t> points, std::uint64_t row) -> const sv_recipe_point_t& {
    const auto found = std::ranges::find(points, row, &sv_recipe_point_t::absolute_row);
    REQUIRE(found != points.end());
    return *found;
}
void check_unit_impulse_box(const sv_recipe_impulse_box_t& box) {
    CHECK(box.entry_row == 1000);
    CHECK(box.exit_row == 1002);
    CHECK(box.width_milliseconds == 1);
    CHECK(box.area == 0.25L);
    CHECK(box.realized_area == 0.25L);
    CHECK(box.area_error == 0);
}
void check_impulse_composition(const chart_document_t& chart, long double height,
                               std::optional<long double> after) {
    const auto compiled = sv_recipe_compiled_output_t::from(chart, sv_recipe_output_t::sv);
    REQUIRE(compiled.diagnostics.empty());
    CHECK(point_at(compiled.value.points, 1000).value == height);
    if (after) CHECK(point_at(compiled.value.points, 1002).value == *after);
}
} // namespace

TEST_CASE("SV structural impulse produces a finite right-owned box preserving signed normalized area",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.parameters.value_from = {0, 1};
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(prepared.diagnostics.empty());
    REQUIRE(prepared.value.impulse_boxes().size() == 1);
    check_unit_impulse_box(prepared.value.impulse_boxes().front());
    CHECK(prepared.value.value_at(999) == 0);
    CHECK(prepared.value.value_at(1000) == 500);
    CHECK(prepared.value.value_at(1002) == 0);
}

TEST_CASE("SV impulse width changes height while preserving area and exposed generated points",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.finite_box_width_milliseconds = {2, 1};
    recipe.impulses.front().area = {-1, 4};
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(prepared.diagnostics.empty());
    const auto& box = prepared.value.impulse_boxes().front();
    CHECK(box.exit_row == 1004);
    CHECK(box.area == -0.25L);
    CHECK(point_at(prepared.value.samples().points, 1000).value == -249);
    CHECK(point_at(prepared.value.samples().points, 1004).value == 1);
}

TEST_CASE("Coincident structural impulses add before box materialization",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.impulses.push_back({.at = {1, 4}, .area = {1, 8}});
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(prepared.diagnostics.empty());
    REQUIRE(prepared.value.impulse_boxes().size() == 1);
    CHECK(prepared.value.impulse_boxes().front().area == 0.375L);
    CHECK(prepared.value.value_at(1000) == 751);
}

TEST_CASE("Composed impulse boxes add across recipes without impulse cross products",
          "[unit][sv-recipe][impulse]") {
    auto chart = make_impulse_chart();
    auto first = make_impulse_recipe();
    auto second = first;
    second.id = recipe_id_t{21};
    second.impulses.front().area = {1, 8};
    chart.sv_recipes = {first, second};
    const auto compiled = sv_recipe_compiled_output_t::from(chart, sv_recipe_output_t::sv);
    REQUIRE(compiled.diagnostics.empty());
    CHECK(point_at(compiled.value.points, 1000).value == 751);
    CHECK(point_at(compiled.value.points, 1002).value == 1);
}

TEST_CASE("Coincident standalone impulse recipes add before external continuous weighting",
          "[unit][sv-recipe][impulse]") {
    auto chart = make_impulse_chart();
    auto first = make_impulse_recipe();
    first.parameters.value_from = {0, 1};
    auto second = first;
    second.id = recipe_id_t{21};
    second.impulses.front().area = {1, 8};
    chart.sv_recipes = {first, second};
    SECTION("standalone boxes add") {
        check_impulse_composition(chart, 750, 0);
    }
    SECTION("nonparticipating continuous factor still weights the sum") {
        auto factor = make_impulse_recipe();
        factor.id = recipe_id_t{22};
        factor.impulses.clear();
        factor.parameters.value_from = {2, 1};
        chart.sv_recipes.push_back(factor);
        check_impulse_composition(chart, 1500, 0);
    }
    SECTION("a noncoincident zero factor still applies") {
        chart.sv_recipes.back().impulses.front().at = {1, 2};
        check_impulse_composition(chart, 0, std::nullopt);
    }
}

TEST_CASE("Effective impulse area freezes baseline and other continuous factors at its position",
          "[unit][sv-recipe][impulse]") {
    auto chart = make_impulse_chart();
    chart.scrolls = {{.id = object_id_t{30}, .position = {measure_id_t{11}, 0}, .multiplier = {2, 1}},
                     {.id = object_id_t{31}, .position = {measure_id_t{11}, 1001}, .multiplier = {3, 1}}};
    auto recipe = make_impulse_recipe();
    auto factor = recipe;
    factor.id = recipe_id_t{21};
    factor.impulses.clear();
    factor.parameters.value_from = {4, 1};
    chart.sv_recipes = {recipe, factor};
    const auto compiled = sv_recipe_compiled_output_t::from(chart, sv_recipe_output_t::sv);
    REQUIRE(compiled.diagnostics.empty());
    CHECK(point_at(compiled.value.points, 1000).value == 4008);
    CHECK(point_at(compiled.value.points, 1001).value == 4012);
    CHECK(point_at(compiled.value.points, 1002).value == 12);
}

TEST_CASE("Impulse multiply and normalization transform structural area with the continuous batch",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.postprocess = {{.kind = sv_recipe_postprocess_kind_t::multiply, .value = {2, 1}},
                          {.kind = sv_recipe_postprocess_kind_t::normalize}};
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(prepared.diagnostics.empty());
    const auto& box = prepared.value.impulse_boxes().front();
    CHECK(box.raw_area == 0.25L);
    CHECK(static_cast<double>(box.area) == Catch::Approx(0.2));
    CHECK(static_cast<double>(box.realized_area) == Catch::Approx(0.2));
    CHECK(point_at(prepared.value.raw_samples().points, 1000).value == 501);
    CHECK(point_at(prepared.value.samples().points, 1000).value == Catch::Approx(400.8));
}

TEST_CASE("Impulse boxes reject unrepresentable times boundaries and target-forbidden output",
          "[unit][sv-recipe][impulse]") {
    const auto scenario = GENERATE(0, 1, 2, 3, 4, 5, 6);
    auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    switch (scenario) {
    case 0: recipe.impulses.front().at = {1, 1}; break;
    case 1: recipe.impulses.front().at = {1, 3}; break;
    case 2: recipe.finite_box_width_milliseconds = {1, 4}; break;
    case 3: recipe.finite_box_width_milliseconds = {3, 4}; break;
    case 4: recipe.impulses.front().at = {3999, 4000}; break;
    case 5: chart.bpms.clear(); break;
    case 6:
        recipe.output = sv_recipe_output_t::speeds;
        recipe.transition_duration = rational_t{};
        recipe.transition_is_seconds = false;
        recipe.impulses.front().area = {-1, 4};
        break;
    }
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    CHECK_FALSE(prepared.diagnostics.empty());
    CHECK(prepared.value.samples().points.empty());
}

TEST_CASE("Impulse minimum-width rejection reports resolution guidance",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.finite_box_width_milliseconds = {1, 4};
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE_FALSE(prepared.diagnostics.empty());
    CHECK(prepared.diagnostics.front().code == diagnostic_code_t::invalid_resolution);
    CHECK(prepared.diagnostics.front().message.find("1/2 ms") != std::string::npos);
}

TEST_CASE("Impulse export bake carries generated entry and exit events and leaves source parametric",
          "[unit][sv-recipe][impulse][export]") {
    auto chart = make_impulse_chart();
    chart.sv_recipes = {make_impulse_recipe()};
    const auto baked = bake_sv_recipes_for_export(chart);
    REQUIRE(baked.diagnostics.empty());
    REQUIRE(baked.value);
    CHECK(baked.value->sv_recipes.empty());
    CHECK(chart.sv_recipes.front().impulses.size() == 1);
    const auto entry = std::ranges::find(baked.value->scrolls,
        local_position_t{measure_id_t{11}, 1000}, &scroll_event_t::position);
    REQUIRE(entry != baked.value->scrolls.end());
    CHECK((entry->multiplier == signed_rational_t{501, 1}));
}

namespace {
struct impulse_script_counts_t { std::size_t queries{}, finishes{}; };
class impulse_script_session_t final : public sv_recipe_script_session_t {
  public:
    explicit impulse_script_session_t(impulse_script_counts_t& counts) : counts_(counts) {}
    auto evaluate(const sv_recipe_script_query_t&) -> diagnosed_t<long double> override {
        ++counts_.queries;
        return {.value = 1};
    }
    auto finish() -> diagnostic_list_t override { ++counts_.finishes; return {}; }
  private:
    impulse_script_counts_t& counts_;
};
class impulse_script_backend_t final : public sv_recipe_script_backend_t {
  public:
    explicit impulse_script_backend_t(impulse_script_counts_t& counts) : counts_(counts) {}
    auto start(const chart_document_t&, const sv_recipe_t&) const
        -> diagnosed_t<std::unique_ptr<sv_recipe_script_session_t>> override {
        return {.value = std::make_unique<impulse_script_session_t>(counts_)};
    }
  private:
    impulse_script_counts_t& counts_;
};
} // namespace

TEST_CASE("Lua impulse boxing postprocess and composition consume an immutable batch without more queries",
          "[unit][sv-recipe][impulse][lua]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "function(p) return 1 end";
    recipe.postprocess = {{.kind = sv_recipe_postprocess_kind_t::multiply, .value = {2, 1}}};
    impulse_script_counts_t counts;
    const impulse_script_backend_t backend(counts);
    const sv_recipe_evaluation_scope_t scope(backend);
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(prepared.diagnostics.empty());
    const auto compiled = sv_recipe_compiled_output_t::from(chart, recipe.output, std::span{&prepared.value, 1});
    REQUIRE(compiled.diagnostics.empty());
    CHECK(counts.queries == 2);
    CHECK(counts.finishes == 1);
    CHECK(point_at(compiled.value.points, 1000).value == 1002);
}

namespace {
void check_analytic_impulse_base(const sv_recipe_prepared_t& original, const sv_recipe_prepared_t& impulse) {
    CHECK(impulse.continuous_value_at(137) == original.value_at(137));
    CHECK(impulse.continuous_value_at(1001) == original.value_at(1001));
}
} // namespace

TEST_CASE("Builtin impulses retain analytic factors at authored and other recipe boundaries",
          "[unit][sv-recipe][impulse]") {
    auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.curve = sv_recipe_curve_t::linear;
    recipe.parameters.value_to = {2, 1};
    auto unmodified = recipe;
    unmodified.impulses.clear();
    const auto original = sv_recipe_prepared_t::from(chart, unmodified);
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(original.diagnostics.empty());
    REQUIRE(prepared.diagnostics.empty());
    check_analytic_impulse_base(original.value, prepared.value);
    chart.scrolls = {{.id = object_id_t{30}, .position = {measure_id_t{11}, 137}, .multiplier = {2, 1}}};
    auto other = make_impulse_recipe();
    other.id = recipe_id_t{21};
    other.impulses.clear();
    other.range.begin.row = 211;
    other.parameters.value_from = {3, 1};
    chart.sv_recipes = {recipe, other};
    const auto compiled = sv_recipe_compiled_output_t::from(chart, sv_recipe_output_t::sv);
    REQUIRE(compiled.diagnostics.empty());
    CHECK(point_at(compiled.value.points, 137).value == Catch::Approx(2 * static_cast<double>(*original.value.value_at(137))));
    CHECK(point_at(compiled.value.points, 211).value == Catch::Approx(6 * static_cast<double>(*original.value.value_at(211))));
}

TEST_CASE("Explicit impulse emission batches and postprocessed builtins retain held continuous values",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.curve = sv_recipe_curve_t::linear;
    recipe.parameters.value_to = {2, 1};
    const std::array emissions{sv_recipe_emission_t{0, 1}, sv_recipe_emission_t{4000, 2}};
    const auto explicit_batch = sv_recipe_prepared_t::from_emissions(chart, recipe, emissions);
    REQUIRE(explicit_batch.diagnostics.empty());
    CHECK(explicit_batch.value.continuous_value_at(137) == 1);
    recipe.postprocess = {{.kind = sv_recipe_postprocess_kind_t::multiply, .value = {2, 1}}};
    const auto processed = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(processed.diagnostics.empty());
    CHECK(processed.value.continuous_value_at(137) == 2);
}

TEST_CASE("Impulse-aware bump balancing solves the continuous remainder and retains structural area",
          "[unit][sv-recipe][impulse]") {
    const auto chart = make_impulse_chart();
    auto recipe = make_impulse_recipe();
    recipe.curve = sv_recipe_curve_t::bump;
    recipe.postprocess = {{.kind = sv_recipe_postprocess_kind_t::balance_bump,
                           .solve = sv_recipe_bump_solve_t::y}};
    const auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    REQUIRE(prepared.diagnostics.empty());
    CHECK(prepared.value.continuous_value_at(3000) == -0.5L);
    CHECK(prepared.value.impulse_boxes().front().raw_area == 0.25L);
    CHECK(prepared.value.impulse_boxes().front().area == 0.25L);
    CHECK(prepared.value.value_at(1000) == 502);
    CHECK(prepared.value.value_at(3000) == -0.5L);
}

TEST_CASE("SV impulse minimum width diagnostics give an exact editable millisecond fraction",
          "[unit][sv-recipe][impulse]") {
    auto chart = make_impulse_chart();
    chart.divisions_per_standard_measure = 960;
    const auto prepared = sv_recipe_prepared_t::from(chart, make_impulse_recipe());
    REQUIRE_FALSE(prepared.diagnostics.empty());
    CHECK(prepared.diagnostics.front().message.find("25/12 ms") != std::string::npos);
}
