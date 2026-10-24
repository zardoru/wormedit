#include "sys/compose/views/recipe_form.h"
#include "core/text_util.h"
#include "sys/compose/recipe_evaluation_runtime.h"
#include "../support/temporary_directory.h"
#include <wormedit/formats/native_project_storage.h>
#include <fstream>
#include <latch>

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

TEST_CASE("ordinary recipe form edits preserve the stored postprocess order", "[unit][sys][recipe-form][postprocess]") {
    auto recipe = sample_recipe();
    recipe.postprocess = {{.kind = sv_recipe_postprocess_kind_t::normalize}, {.value = {2, 1}}};
    auto form = wormedit::ui::timing_recipe_form_t::from(recipe);
    set(form.value_from, "7", "4");
    const auto candidate = recipe_from_form(form, recipe);
    REQUIRE(candidate);
    CHECK(candidate->postprocess == recipe.postprocess);
}

namespace {
void check_lua_form_edit(const std::optional<sv_recipe_t>& edited, const sv_recipe_t& original) {
    REQUIRE(edited);
    CHECK(edited->lua->parameters.front().value == signed_rational_t{-7, 3});
    CHECK(edited->lua->seed == UINT64_MAX);
    CHECK(edited->lua->inputs == original.lua->inputs);
    CHECK(edited->expression_source == original.expression_source);
}
void check_builtin_switch(const ui::timing_recipe_form_t& form, const sv_recipe_t& original) {
    const auto edited = recipe_from_form(form, original);
    REQUIRE(edited);
    CHECK_FALSE(edited->lua);
    CHECK(edited->expression_source.empty());
}
auto require_prepared_preview(const std::optional<recipe_form_outcome_t>& preview)
    -> const recipe_form_report_t& {
    REQUIRE(preview);
    REQUIRE(preview->value);
    REQUIRE(preview->report());
    REQUIRE(preview->report()->prepared.size() == 1);
    CHECK_FALSE(preview->report()->composed_points.empty());
    REQUIRE(preview->report()->requested_form);
    return *preview->report();
}
void check_frozen_module(const recipe_form_report_t& report, const std::string& source,
    const ui::timing_recipe_form_t& requested) {
    REQUIRE(report.candidate.lua);
    CHECK(report.candidate.expression_source == source);
    CHECK(report.candidate.lua->source_reference == std::filesystem::path{"curve.lua"});
    CHECK(report.candidate.lua->source_hash == native_project_content_checksum(source));
    REQUIRE(report.requested_form);
    CHECK(*report.requested_form == requested);
}
#if defined(WORMEDIT_HAS_LUA)
void check_loaded_recipe_apply(document_history_t& history, const recipe_form_outcome_t& preview) {
    const auto original = history.document.charts.front().sv_recipes;
    CHECK(sv_recipe_evaluation_scope_t::current() == nullptr);
    CHECK(sv_recipe_prepared_scope_t::current() == nullptr);
    const auto result = apply_recipe_form(history, preview);
    REQUIRE(result.applied);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes == original);
}
#endif
} // namespace

TEST_CASE("Lua form preserves frozen source and hidden input hashes while editing settings", "[unit][sys][recipe-form]") {
    auto recipe = sample_recipe();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "function curve(p) return p end";
    recipe.lua = sv_recipe_lua_t{.entry_point = "curve", .source_reference = text_util::path_from_utf8("scripts/曲線.lua"),
        .source_hash = "frozen", .parameters = {{"gain", {3, 2}}},
        .inputs = {{asset_id_t{4}, "input-hash"}}, .seed = 92,
        .mode = sv_recipe_lua_mode_t::ordered, .cycles = {{3}, {2}}};
    auto form = ui::timing_recipe_form_t::from(recipe);
    CHECK(recipe_from_form(form, recipe) == recipe);
    set(form.lua_parameters.front().value, "-7", "3");
    std::snprintf(form.lua_seed, sizeof(form.lua_seed), "18446744073709551615");
    auto edited = recipe_from_form(form, recipe);
    check_lua_form_edit(edited, recipe);
    set(form.lua_parameters.front().value, "1", "0");
    CHECK_FALSE(recipe_from_form(form, recipe));
    form.lua_mode = sv_recipe_lua_mode_t::pure;
    set(form.lua_parameters.front().value, "1", "1");
    edited = recipe_from_form(form, recipe);
    REQUIRE(edited);
    CHECK(edited->lua->cycles.empty());
    form.curve = sv_recipe_curve_t::linear;
    check_builtin_switch(form, recipe);
}

TEST_CASE("recipe evaluation publishes prepared results and retains last good geometry on failure", "[unit][sys][recipe-form][recipe-runtime]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& recipe = chart.sv_recipes.front();
    task_scheduler_t scheduler{1, 4};
    recipe_evaluation_runtime_t runtime{scheduler};
    std::optional<recipe_form_outcome_t> preview;
    auto form = ui::timing_recipe_form_t::from(recipe);
    runtime.request(history.document, chart, recipe, form, {});
    runtime.wait(history.document, chart.id, recipe.id, preview);
    const auto good = require_prepared_preview(preview).prepared;
    set(form.value_to, "invalid", "1");
    runtime.request(history.document, chart, recipe, form, {});
    runtime.wait(history.document, chart.id, recipe.id, preview);
    CHECK_FALSE(preview->value);
    CHECK_FALSE(preview->diagnostics.empty());
    CHECK(preview->report()->prepared == good);
}

TEST_CASE("recipe evaluation discards stale revisions and cancellation", "[unit][sys][recipe-form][recipe-runtime]") {
    auto history = form_history();
    const auto chart = history.document.charts.front();
    const auto recipe = chart.sv_recipes.front();
    task_scheduler_t scheduler{1, 4};
    recipe_evaluation_runtime_t runtime{scheduler};
    std::optional<recipe_form_outcome_t> preview;
    const auto form = ui::timing_recipe_form_t::from(recipe);
    runtime.request(history.document, chart, recipe, form, {});
    ++history.document.revision;
    runtime.wait(history.document, chart.id, recipe.id, preview);
    CHECK_FALSE(preview);
    runtime.request(history.document, chart, recipe, form, {});
    runtime.cancel();
    runtime.wait(history.document, chart.id, recipe.id, preview);
    CHECK_FALSE(preview);
}

TEST_CASE("module Reload freezes bounded source and records the exact requested form", "[unit][sys][recipe-form][recipe-runtime]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const std::string source = "function(p,ctx) return 1+p end";
    { std::ofstream file{directory->path() / "curve.lua"}; file << source; }
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& recipe = chart.sv_recipes.front();
    auto form = ui::timing_recipe_form_t::from(recipe);
    form.curve = sv_recipe_curve_t::lua_expression;
    form.lua_entry_point[0] = '\0';
    std::snprintf(form.lua_source_reference, sizeof(form.lua_source_reference), "%s",
        (directory->path() / "curve.lua").string().c_str());
    form.lua_reload = true;
    task_scheduler_t scheduler{1, 4};
    recipe_evaluation_runtime_t runtime{scheduler};
    std::optional<recipe_form_outcome_t> preview;
    runtime.request(history.document, chart, recipe, form, directory->path());
    runtime.wait(history.document, chart.id, recipe.id, preview);
    REQUIRE(preview);
    REQUIRE(preview->report());
    const auto& report = *preview->report();
    form.lua_reload = false;
    check_frozen_module(report, source, form);
    std::snprintf(form.lua_source_reference, sizeof(form.lua_source_reference), "different.lua");
    CHECK(*report.requested_form != form);
#if defined(WORMEDIT_HAS_LUA)
    static_cast<void>(require_prepared_preview(preview));
    check_loaded_recipe_apply(history, *preview);
#else
    CHECK_FALSE(preview->value);
    CHECK_FALSE(preview->diagnostics.empty());
#endif
}

namespace {
using blocker_job_t = async_job_t<std::latch*, int, int>;
auto hold_recipe_worker(std::latch* const& release, std::stop_token) -> blocker_job_t::work_outcome_t {
    release->wait();
    return 1;
}
}
TEST_CASE("recipe evaluation retries the newest draft after shared scheduler admission fails", "[unit][sys][recipe-form][recipe-runtime]") {
    auto history = form_history();
    const auto& chart = history.document.charts.front();
    const auto& recipe = chart.sv_recipes.front();
    task_scheduler_t scheduler{1, 1};
    std::latch release{1};
    blocker_job_t blocker{scheduler, 1, "invalid", "full", "missing"};
    REQUIRE(blocker.request([&] { return &release; }, 1, hold_recipe_worker));
    recipe_evaluation_runtime_t runtime{scheduler};
    std::optional<recipe_form_outcome_t> preview;
    auto form = ui::timing_recipe_form_t::from(recipe);
    runtime.request(history.document, chart, recipe, form, {});
    set(form.value_to, "7", "1");
    runtime.request(history.document, chart, recipe, form, {});
    CHECK(runtime.is_busy());
    release.count_down();
    REQUIRE(blocker.wait());
    runtime.poll(history.document, chart.id, recipe.id, preview);
    runtime.wait(history.document, chart.id, recipe.id, preview);
    const auto& report = require_prepared_preview(preview);
    CHECK(report.candidate.parameters.value_to == signed_rational_t{7, 1});
    CHECK(*report.requested_form == form);
}

TEST_CASE("recipe form refill retains Lua vector storage across curve changes", "[unit][sys][recipe-form]") {
    auto recipe = sample_recipe();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.lua = sv_recipe_lua_t{.entry_point = "curve", .parameters = {{"gain", {3, 2}}},
        .mode = sv_recipe_lua_mode_t::ordered, .cycles = {{3}, {2}}};
    auto form = ui::timing_recipe_form_t::from(recipe);
    const auto expected = form;
    const auto* parameters = form.lua_parameters.data();
    const auto* cycles = form.lua_cycles.data();
    const auto parameter_capacity = form.lua_parameters.capacity();
    const auto cycle_capacity = form.lua_cycles.capacity();
    form.refill(sample_recipe());
    CHECK(form == ui::timing_recipe_form_t::from(sample_recipe()));
    CHECK(form.lua_parameters.capacity() == parameter_capacity);
    CHECK(form.lua_cycles.capacity() == cycle_capacity);
    form.refill(recipe);
    CHECK(form == expected);
    CHECK(form.lua_parameters.data() == parameters);
    CHECK(form.lua_cycles.data() == cycles);
}

TEST_CASE("legacy inline Lua forms keep the default empty entry point", "[unit][sys][recipe-form]") {
    auto history = form_history();
    auto recipe = history.document.charts.front().sv_recipes.front();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "function(p) return 1+p end";
    recipe.lua.reset();
    const auto form = ui::timing_recipe_form_t::from(recipe);
    CHECK(form.lua_entry_point[0] == '\0');
    const auto rebuilt = recipe_from_form(form, recipe);
    REQUIRE(rebuilt);
    REQUIRE(rebuilt->lua);
    CHECK(rebuilt->lua->entry_point.empty());
}

TEST_CASE("frozen Lua source evaluates without implicitly reloading a file", "[unit][sys][recipe-form][recipe-runtime]") {
    auto history = form_history();
    auto& chart = history.document.charts.front();
    auto& recipe = chart.sv_recipes.front();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "function(p) return 1+p end";
    recipe.lua.reset();
    auto form = ui::timing_recipe_form_t::from(recipe);
    SECTION("legacy recipe without a descriptor") {}
    SECTION("clearing the reload origin retains frozen source") {
        recipe.lua = sv_recipe_lua_t{.source_reference = "missing.lua"};
        form = ui::timing_recipe_form_t::from(recipe);
        form.lua_source_reference[0] = '\0';
    }
    task_scheduler_t scheduler{1, 4};
    recipe_evaluation_runtime_t runtime{scheduler};
    std::optional<recipe_form_outcome_t> preview;
    runtime.request(history.document, chart, recipe, form, {});
    runtime.wait(history.document, chart.id, recipe.id, preview);
    REQUIRE(preview);
#if defined(WORMEDIT_HAS_LUA)
    REQUIRE(preview->value);
    CHECK_FALSE(preview->report()->prepared.empty());
#else
    REQUIRE_FALSE(preview->diagnostics.empty());
    CHECK(preview->diagnostics.front().code == diagnostic_code_t::unsupported_feature);
#endif
}

TEST_CASE("SV impulse forms preserve exact placement area width and vector storage", "[unit][sys][recipe-form][impulse]") {
    auto recipe = sample_recipe();
    recipe.impulses = {{.at = {1, 4}, .area = {-3, 2}}};
    recipe.finite_box_width_milliseconds = {25, 12};
    auto form = ui::timing_recipe_form_t::from(recipe);
    const auto* storage = form.impulses.data();
    const auto edited = recipe_from_form(form, recipe);
    REQUIRE(edited);
    CHECK(edited->impulses == recipe.impulses);
    CHECK(edited->finite_box_width_milliseconds == recipe.finite_box_width_milliseconds);
    form.refill(sample_recipe());
    form.refill(recipe);
    CHECK(form.impulses.data() == storage);
    set(form.impulses.front().area, "-5", "3");
    const auto revised = recipe_from_form(form, recipe);
    REQUIRE(revised);
    CHECK(revised->impulses.front().area == signed_rational_t{-5, 3});
}

namespace {
void check_impulse_worker_box(const recipe_form_outcome_t& preview) {
    REQUIRE(preview.report()->prepared.size() == 1);
    const auto boxes = preview.report()->prepared.front().impulse_boxes();
    REQUIRE(boxes.size() == 1);
    CHECK(boxes.front().entry_row == 1000);
    CHECK(boxes.front().exit_row == 1002);
}
}

TEST_CASE("SV impulse worker preview provides immutable box details for one undoable Apply", "[unit][sys][recipe-form][impulse]") {
    auto history = form_history();
    auto& chart = history.document.charts.front();
    chart.divisions_per_standard_measure = 4000;
    chart.bpms.push_back({.id = object_id_t{100}, .position = {measure_id_t{11}, 0}, .beats_per_minute = {120, 1}});
    const auto& recipe = chart.sv_recipes.front();
    auto form = ui::timing_recipe_form_t::from(recipe);
    ui::timing_recipe_impulse_form_t impulse;
    set(impulse.at, "1", "4");
    set(impulse.area, "1", "4");
    form.impulses.push_back(impulse);
    task_scheduler_t scheduler{1, 4};
    recipe_evaluation_runtime_t runtime{scheduler};
    std::optional<recipe_form_outcome_t> preview;
    runtime.request(history.document, chart, recipe, form, {});
    runtime.wait(history.document, chart.id, recipe.id, preview);
    REQUIRE(preview);
    REQUIRE(preview->value);
    check_impulse_worker_box(*preview);
    const auto applied = apply_recipe_form(history, *preview);
    CHECK(applied.applied);
    CHECK(history.undo.size() == 1);
}
