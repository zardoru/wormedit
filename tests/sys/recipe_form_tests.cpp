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

void set(wormedit::ui::timing_recipe_rational_text_t& field, const char* numerator, const char* denominator) {
    std::snprintf(field.numerator, sizeof(field.numerator), "%s", numerator);
    std::snprintf(field.denominator, sizeof(field.denominator), "%s", denominator);
}

} // namespace

TEST_CASE("a recipe survives a trip through its form unchanged", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    const auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
    CHECK(std::strcmp(form.value_to.numerator, "-5") == 0);
    CHECK(std::strcmp(form.value_to.denominator, "2") == 0);
    CHECK(std::strcmp(form.fixed_point_count, "5") == 0);
    const auto back = recipe_from_form(form, recipe);
    REQUIRE(back.has_value());
    CHECK(*back == recipe);
}

TEST_CASE("form edits change only the curve, values and sampling", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
    form.curve = wormedit::sv_recipe_curve_t::linear;
    set(form.value_to, "7", "2");
    form.sampling_mode = wormedit::sv_recipe_sampling_mode_t::adaptive;
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
    auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
    form.output = wormedit::sv_recipe_output_t::speeds;
    set(form.transition_duration, "3", "2");
    form.transition_is_seconds = true;
    auto speeds = recipe_from_form(form, recipe);
    REQUIRE(speeds.has_value());
    CHECK(speeds->output == sv_recipe_output_t::speeds);
    CHECK(speeds->transition_duration == rational_t{3, 2});
    CHECK(speeds->transition_is_seconds == true);
    form.output = wormedit::sv_recipe_output_t::scrolls;
    const auto scrolls = recipe_from_form(form, *speeds);
    REQUIRE(scrolls.has_value());
    CHECK_FALSE(scrolls->transition_duration.has_value());
    CHECK_FALSE(scrolls->transition_is_seconds.has_value());
}

TEST_CASE("a step-rows sampling form switches the fixed extent", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
    form.fixed_extent = wormedit::ui::timing_recipe_fixed_extent_t::step_rows;
    std::snprintf(form.fixed_step_rows, sizeof(form.fixed_step_rows), "120");
    const auto edited = recipe_from_form(form, recipe);
    REQUIRE(edited.has_value());
    CHECK(edited->sampling.fixed_step_rows == std::optional<std::uint64_t>{120});
    CHECK_FALSE(edited->sampling.fixed_point_count.has_value());
}

TEST_CASE("a malformed form is refused", "[unit][sys][recipe-form]") {
    const auto recipe = sample_recipe();
    for (const auto* bad : {"", "x", "1.5", "99999999999999999999999"}) {
        auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
        set(form.value_from, bad, "1");
        CHECK_FALSE(recipe_from_form(form, recipe).has_value());
    }
    auto zero_denominator = wormedit::ui::timing_recipe_form_t::from(recipe);
    set(zero_denominator.shape, "1", "0");
    CHECK_FALSE(recipe_from_form(zero_denominator, recipe).has_value());
    auto bad_enum = wormedit::ui::timing_recipe_form_t::from(recipe);
    bad_enum.curve = static_cast<wormedit::sv_recipe_curve_t>(9);
    CHECK_FALSE(recipe_from_form(bad_enum, recipe).has_value());
    auto too_many = wormedit::ui::timing_recipe_form_t::from(recipe);
    std::snprintf(too_many.fixed_point_count, sizeof(too_many.fixed_point_count), "4294967296");
    CHECK_FALSE(recipe_from_form(too_many, recipe).has_value());
}

TEST_CASE("a Lua expression recipe keeps its source and parameters through the form",
          "[unit][sys][recipe-form]") {
    auto recipe = sample_recipe();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "return 1 + t";
    auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
    CHECK(form.curve == wormedit::sv_recipe_curve_t::lua_expression);
    form.sampling_mode = wormedit::sv_recipe_sampling_mode_t::adaptive;
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
    return document_history_t::from(song);
}

} // namespace

TEST_CASE("a form preview carries the candidate and the validator's message without writing",
          "[unit][sys][recipe-form]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(stored);
    set(form.value_to, "5", "1");
    const auto preview = recipe_form_preview_t::propose(history.document, chart, stored, form);
    CHECK(preview.diagnostics.empty());
    CHECK(preview.captured_stamp().chart == chart.id);
    CHECK(preview.captured_stamp().document.revision == history.document.revision);
    CHECK(preview.report()->candidate.parameters.value_to == signed_rational_t{5, 1});
    CHECK(stored.parameters.value_to == signed_rational_t{3, 1});
    CHECK(history.undo.empty());

    // A malformed value keeps the stored recipe and says why.
    set(form.value_to, "x", "1");
    const auto malformed = recipe_form_preview_t::propose(history.document, chart, stored, form);
    CHECK_FALSE(malformed.diagnostics.empty());
    CHECK(malformed.report()->candidate == stored);

    // Well-formed but out of range: the core validator's message, the candidate kept for editing.
    auto out_of_range = wormedit::ui::timing_recipe_form_t::from(stored);
    out_of_range.curve = wormedit::sv_recipe_curve_t::exponential;
    set(out_of_range.shape, "99", "1");
    const auto invalid = recipe_form_preview_t::propose(history.document, chart, stored, out_of_range);
    CHECK_FALSE(invalid.diagnostics.empty());
    CHECK(invalid.report()->candidate.curve == sv_recipe_curve_t::exponential);
}

TEST_CASE("applying a form updates the recipe in one undo step and rejects bad input unchanged",
          "[unit][sys][recipe-form]") {
    auto history = form_history();
    const auto stored = history.document.charts.front().sv_recipes.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(stored);
    form.curve = wormedit::sv_recipe_curve_t::bump;
    bool applied = false;
    CHECK(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, form, applied) ==
          "SV recipe updated.");
    CHECK(applied);
    CHECK(history.document.charts.front().sv_recipes.front().curve == sv_recipe_curve_t::bump);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes.front() == stored);

    const auto revision = history.document.revision;
    auto bad = wormedit::ui::timing_recipe_form_t::from(stored);
    set(bad.value_from, "", "1");
    CHECK_FALSE(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, bad, applied) == "SV recipe updated.");
    CHECK_FALSE(applied);
    auto invalid = wormedit::ui::timing_recipe_form_t::from(stored);
    invalid.sampling_mode = wormedit::sv_recipe_sampling_mode_t::fixed;
    std::snprintf(invalid.fixed_point_count, sizeof(invalid.fixed_point_count), "1");  // needs 2..4096
    CHECK_FALSE(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, invalid, applied) == "SV recipe updated.");
    CHECK_FALSE(applied);
    CHECK(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{99}, form, applied) ==
          "The SV recipe no longer exists.");
    CHECK(apply_recipe_form(history, chart_id_t{77}, recipe_id_t{20}, form, applied) ==
          "The chart is no longer available.");
    CHECK(history.document.revision == revision);
    // Re-applying what is already stored is unchanged, not an error and not a revision.
    const auto same = wormedit::ui::timing_recipe_form_t::from(history.document.charts.front().sv_recipes.front());
    CHECK(apply_recipe_form(history, chart_id_t{10}, recipe_id_t{20}, same, applied) ==
          "SV recipe is unchanged.");
    CHECK_FALSE(applied);
    CHECK(history.document.revision == revision);
}

TEST_CASE("captured recipe form applies once with revision-only identity",
          "[sys][recipe-form][stamp]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(chart.sv_recipes.front());
    set(form.value_to, "7", "1");
    const auto proposal = recipe_form_preview_t::propose(history.document, chart, chart.sv_recipes.front(), form);
    REQUIRE(proposal.value.has_value());
    history.document.id = song_id_t{99};
    REQUIRE(apply_recipe_form(history, proposal));
    CHECK(history.undo.size() == 1);
    const auto stale = apply_recipe_form(history, proposal);
    CHECK_FALSE(stale);
    REQUIRE_FALSE(stale.diagnostics.empty());
    CHECK(stale.diagnostics.front().message == stale_action_message("SV recipe form"));
}

namespace {
void check_failed_recipe_candidate(const recipe_form_outcome_t& proposal, const sv_recipe_t& stored) {
    CHECK(proposal.diagnostics.front().severity == diagnostic_severity_t::warning);
    CHECK(proposal.report()->candidate == stored);
}
} // namespace

TEST_CASE("failed recipe form owns warning diagnostics and retains the stored candidate",
          "[sys][recipe-form][stamp]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(stored);
    set(form.value_to, "x", "1");
    const auto proposal = recipe_form_preview_t::propose(history.document, chart, stored, form);
    REQUIRE_FALSE(proposal.value.has_value());
    REQUIRE_FALSE(proposal.diagnostics.empty());
    check_failed_recipe_candidate(proposal, stored);
    const auto result = apply_recipe_form(history, proposal);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message == proposal.diagnostics.front().message);
    CHECK(history.undo.empty());
}

TEST_CASE("Rejected recipe form revisions retain the stored candidate and reuse syntax feedback",
          "[sys][recipe-form][proposal-shape]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(stored);
    set(form.value_to, "x", "1");
    auto outcome = recipe_form_preview_t::propose(history.document, chart, stored, form);
    const auto* feedback = outcome.diagnostics.data();
    outcome = recipe_form_preview_t::revise(std::move(outcome), history.document, chart, stored, form);
    CHECK_FALSE(outcome.value);
    CHECK(outcome.diagnostics.data() == feedback);
    CHECK(outcome.report()->candidate == stored);
    CHECK(history.undo.empty());
}

TEST_CASE("A repaired recipe form starts with the current stored recipe",
          "[sys][recipe-form][proposal-shape]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(stored);
    set(form.value_to, "9", "1");
    auto outcome = recipe_form_preview_t::propose(history.document, chart, stored, form);
    set(form.value_to, "x", "1");
    outcome = recipe_form_preview_t::revise(std::move(outcome), history.document, chart, stored, form);
    CHECK_FALSE(outcome.value);
    CHECK(outcome.report()->candidate == stored);
    set(form.value_to, "7", "1");
    outcome = recipe_form_preview_t::revise(std::move(outcome), history.document, chart, stored, form);
    REQUIRE(outcome.value);
    CHECK(outcome.value->payload.report.candidate.parameters.value_to == signed_rational_t{7, 1});
    CHECK(history.undo.empty());
}

TEST_CASE("Recipe form diagnostics still reject before a stale captured stamp",
          "[sys][recipe-form][proposal-shape]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& stored = chart.sv_recipes.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(stored);
    set(form.value_to, "x", "1");
    const auto outcome = recipe_form_preview_t::propose(history.document, chart, stored, form);
    ++history.document.revision;
    const auto result = apply_recipe_form(history, outcome);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics == outcome.diagnostics);
    CHECK(history.undo.empty());
}

TEST_CASE("Recipe editor retention keeps payload storage independent of diagnostics",
          "[sys][recipe-form][proposal-shape]") {
    recipe_form_proposal_t captured{.stamp = {.chart = chart_id_t{2}},
        .payload = {.report = {.candidate = {.name = std::string(512, 'r')}}}};
    recipe_form_outcome_t outcome{std::move(captured)};
    const auto* storage = outcome.report()->candidate.name.data();
    const auto stamp = outcome.captured_stamp();
    outcome.complete(false);
    CHECK_FALSE(outcome.value);
    outcome.diagnostics = {error(diagnostic_code_t::invalid_id, "Replacement feedback.")};
    CHECK(outcome.report()->candidate.name.data() == storage);
    CHECK(outcome.captured_stamp() == stamp);
    outcome.complete(true);
    REQUIRE(outcome.value);
    CHECK(outcome.value->payload.report.candidate.name.data() == storage);
    CHECK(outcome.report() == &outcome.value->payload.report);
}

TEST_CASE("An empty recipe editor cannot fabricate an applicable proposal",
          "[sys][recipe-form][proposal-shape]") {
    recipe_form_outcome_t outcome;
    outcome.complete(true);
    CHECK_FALSE(outcome.value);
    CHECK(outcome.report() == nullptr);
    CHECK(outcome.captured_stamp() == chart_stamp_t{});
}
