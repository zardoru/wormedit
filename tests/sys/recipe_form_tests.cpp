#include "sys/recipe_form.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto sample_recipe() -> sv_recipe_t {
    sv_recipe_t recipe{.id = recipe_id_t{7},
                       .name = "rise",
                       .range = {{measure_id_t{1}, 0}, {measure_id_t{1}, 480}},
                       .output = sv_recipe_output_t::sv,
                       .curve = sv_recipe_curve_t::exponential,
                       .parameters = {.value_from = {3, 4},
                                      .value_to = {-5, 2},
                                      .shape = {1, 3},
                                      .bump_start = {1, 8},
                                      .bump_length = {1, 4},
                                      .bump_x = {3, 1},
                                      .bump_y = {1, 2}}};
    recipe.sampling.fixed_point_count = 5;
    return recipe;
}

void set(WormUiTimingRecipeRationalText& field, const char* numerator, const char* denominator) {
    std::snprintf(field.numerator, sizeof(field.numerator), "%s", numerator);
    std::snprintf(field.denominator, sizeof(field.denominator), "%s", denominator);
}

} // namespace

TEST_CASE("a recipe survives a trip through its form unchanged", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    const auto form = recipe_form_from_recipe(recipe);
    CHECK(std::strcmp(form.value_to.numerator, "-5") == 0);
    CHECK(std::strcmp(form.value_to.denominator, "2") == 0);
    CHECK(std::strcmp(form.fixed_point_count, "5") == 0);
    const auto back = recipe_from_form(form, recipe);
    REQUIRE(back.has_value());
    CHECK(*back == recipe);
}

TEST_CASE("form edits change only the curve, values and sampling", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    auto form = recipe_form_from_recipe(recipe);
    form.curve = WORM_UI_TIMING_RECIPE_CURVE_LINEAR;
    set(form.value_to, "7", "2");
    form.sampling_mode = WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE;
    set(form.tolerance, "1", "500");
    std::snprintf(form.adaptive_max_points, sizeof(form.adaptive_max_points), "64");
    const auto edited = recipe_from_form(form, recipe);
    REQUIRE(edited.has_value());
    CHECK(edited->curve == sv_recipe_curve_t::linear);
    CHECK(edited->parameters.value_to == signed_rational_t{7, 2});
    CHECK(edited->sampling.mode == sv_recipe_sampling_mode_t::adaptive);
    CHECK(edited->sampling.tolerance == signed_rational_t{1, 500});
    CHECK(edited->sampling.max_points == 64);
    CHECK_FALSE(edited->sampling.fixed_point_count.has_value());
    // Identity, name, range and enabled state are outside the form.
    CHECK(edited->id == recipe.id);
    CHECK(edited->name == recipe.name);
    CHECK(edited->range == recipe.range);
    CHECK(edited->enabled == recipe.enabled);
}

TEST_CASE("SPEEDS recipes carry a transition and other outputs drop it", "[unit][sys][recipe-form]") {
    auto recipe = sample_recipe();
    auto form = recipe_form_from_recipe(recipe);
    form.output = WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS;
    set(form.transition_duration, "3", "2");
    form.transition_is_seconds = true;
    auto speeds = recipe_from_form(form, recipe);
    REQUIRE(speeds.has_value());
    CHECK(speeds->output == sv_recipe_output_t::speeds);
    CHECK(speeds->transition_duration == rational_t{3, 2});
    CHECK(speeds->transition_is_seconds == true);
    form.output = WORM_UI_TIMING_RECIPE_OUTPUT_SCROLLS;
    const auto scrolls = recipe_from_form(form, *speeds);
    REQUIRE(scrolls.has_value());
    CHECK_FALSE(scrolls->transition_duration.has_value());
    CHECK_FALSE(scrolls->transition_is_seconds.has_value());
}

TEST_CASE("a step-rows sampling form switches the fixed extent", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    auto form = recipe_form_from_recipe(recipe);
    form.fixed_extent = WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS;
    std::snprintf(form.fixed_step_rows, sizeof(form.fixed_step_rows), "120");
    const auto edited = recipe_from_form(form, recipe);
    REQUIRE(edited.has_value());
    CHECK(edited->sampling.fixed_step_rows == std::optional<std::uint64_t>{120});
    CHECK_FALSE(edited->sampling.fixed_point_count.has_value());
}

TEST_CASE("a malformed form is refused", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    for (const auto* bad : {"", "x", "1.5", "99999999999999999999999"}) {
        auto form = recipe_form_from_recipe(recipe);
        set(form.value_from, bad, "1");
        CHECK_FALSE(recipe_from_form(form, recipe).has_value());
    }
    auto zero_denominator = recipe_form_from_recipe(recipe);
    set(zero_denominator.shape, "1", "0");
    CHECK_FALSE(recipe_from_form(zero_denominator, recipe).has_value());
    auto bad_enum = recipe_form_from_recipe(recipe);
    bad_enum.curve = static_cast<WormUiTimingRecipeCurve>(9);
    CHECK_FALSE(recipe_from_form(bad_enum, recipe).has_value());
    auto too_many = recipe_form_from_recipe(recipe);
    std::snprintf(too_many.fixed_point_count, sizeof(too_many.fixed_point_count), "4294967296");
    CHECK_FALSE(recipe_from_form(too_many, recipe).has_value());
}

TEST_CASE("a Lua expression recipe keeps its source and parameters through the form",
          "[unit][sys][recipe-form]") {
    auto recipe = sample_recipe();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "return 1 + t";
    auto form = recipe_form_from_recipe(recipe);
    CHECK(form.curve == WORM_UI_TIMING_RECIPE_CURVE_LUA_EXPRESSION);
    form.sampling_mode = WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE;
    std::snprintf(form.adaptive_max_points, sizeof(form.adaptive_max_points), "32");
    const auto edited = recipe_from_form(form, recipe);
    REQUIRE(edited.has_value());
    CHECK(edited->curve == sv_recipe_curve_t::lua_expression);
    CHECK(edited->expression_source == "return 1 + t");
    CHECK(edited->parameters == recipe.parameters);
    CHECK(edited->sampling.max_points == 32);
}

namespace {

auto form_history() -> document_history_t {
    song_document_t song{.id = song_id_t{1}};
    chart_document_t chart{.id = chart_id_t{10}, .divisions_per_standard_measure = 960,
                           .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}}};
    auto recipe = sample_recipe();
    recipe.id = recipe_id_t{20};
    recipe.range = {{measure_id_t{11}, 0}, {measure_id_t{12}, 0}};
    recipe.curve = sv_recipe_curve_t::linear;
    recipe.parameters = {.value_from = {1, 1}, .value_to = {3, 1}};
    chart.sv_recipes.push_back(recipe);
    song.charts.push_back(chart);
    return make_document_history(song);
}

} // namespace

TEST_CASE("a form preview carries the candidate and the validator's message without writing",
          "[unit][sys][recipe-form]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = recipe_form_from_recipe(stored);
    set(form.value_to, "5", "1");
    const auto preview = propose_recipe_form(history.document, chart, stored, form);
    CHECK(preview.diagnostics.empty());
    CHECK(preview.stamp.chart == chart.id);
    CHECK(preview.stamp.document.revision == history.document.revision);
    CHECK(preview.candidate.parameters.value_to == signed_rational_t{5, 1});
    CHECK(stored.parameters.value_to == signed_rational_t{3, 1});
    CHECK(history.undo.empty());

    // A malformed value keeps the stored recipe and says why.
    set(form.value_to, "x", "1");
    const auto malformed = propose_recipe_form(history.document, chart, stored, form);
    CHECK_FALSE(malformed.diagnostics.empty());
    CHECK(malformed.candidate == stored);

    // Well-formed but out of range: the core validator's message, the candidate kept for editing.
    auto out_of_range = recipe_form_from_recipe(stored);
    out_of_range.curve = WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL;
    set(out_of_range.shape, "99", "1");
    const auto invalid = propose_recipe_form(history.document, chart, stored, out_of_range);
    CHECK_FALSE(invalid.diagnostics.empty());
    CHECK(invalid.candidate.curve == sv_recipe_curve_t::exponential);
}

TEST_CASE("applying a form updates the recipe in one undo step and rejects bad input unchanged",
          "[unit][sys][recipe-form]") {
    auto history = form_history();
    const auto stored = history.document.charts.front().sv_recipes.front();
    auto form = recipe_form_from_recipe(stored);
    form.curve = WORM_UI_TIMING_RECIPE_CURVE_BUMP;
    bool applied = false;
    CHECK(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, form, applied) ==
          "SV recipe updated.");
    CHECK(applied);
    CHECK(history.document.charts.front().sv_recipes.front().curve == sv_recipe_curve_t::bump);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes.front() == stored);

    const auto revision = history.document.revision;
    auto bad = recipe_form_from_recipe(stored);
    set(bad.value_from, "", "1");
    CHECK_FALSE(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, bad, applied) == "SV recipe updated.");
    CHECK_FALSE(applied);
    auto invalid = recipe_form_from_recipe(stored);
    invalid.sampling_mode = WORM_UI_TIMING_RECIPE_SAMPLING_FIXED;
    std::snprintf(invalid.fixed_point_count, sizeof(invalid.fixed_point_count), "1");  // needs 2..4096
    apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, invalid, applied);
    CHECK_FALSE(applied);
    CHECK(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{99}, form, applied) ==
          "The SV recipe no longer exists.");
    CHECK(apply_recipe_form(history, chart_id_t{77}, recipe_id_t{20}, form, applied) ==
          "The chart is no longer available.");
    CHECK(history.document.revision == revision);
    // Re-applying what is already stored is unchanged, not an error and not a revision.
    const auto same = recipe_form_from_recipe(history.document.charts.front().sv_recipes.front());
    CHECK(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, same, applied) ==
          "SV recipe is unchanged.");
    CHECK_FALSE(applied);
    CHECK(history.document.revision == revision);
}

TEST_CASE("captured recipe form applies once with revision-only identity",
          "[sys][recipe-form][stamp]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    auto form = recipe_form_from_recipe(chart.sv_recipes.front());
    set(form.value_to, "7", "1");
    const auto proposal = propose_recipe_form(history.document, chart, chart.sv_recipes.front(), form);
    REQUIRE(proposal.is_valid());
    history.document.id = song_id_t{99};
    REQUIRE(apply_recipe_form(history, proposal));
    CHECK(history.undo.size() == 1);
    const auto stale = apply_recipe_form(history, proposal);
    CHECK_FALSE(stale);
    REQUIRE_FALSE(stale.diagnostics.empty());
    CHECK(stale.diagnostics.front().message == stale_action_message("SV recipe form"));
}

namespace {
void check_failed_recipe_candidate(const recipe_form_preview_t& proposal, const sv_recipe_t& stored) {
    CHECK(proposal.diagnostics.front().severity == diagnostic_severity_t::warning);
    CHECK(proposal.candidate == stored);
}
} // namespace

TEST_CASE("failed recipe form owns warning diagnostics and retains the stored candidate",
          "[sys][recipe-form][stamp]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = recipe_form_from_recipe(stored);
    set(form.value_to, "x", "1");
    const auto proposal = propose_recipe_form(history.document, chart, stored, form);
    REQUIRE_FALSE(proposal.is_valid());
    REQUIRE_FALSE(proposal.diagnostics.empty());
    check_failed_recipe_candidate(proposal, stored);
    const auto result = apply_recipe_form(history, proposal);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message == proposal.message());
    CHECK(history.undo.empty());
}
