#include "sys/recipe_form.h"
#include "sys/projection_notification_feedback.h"
#include "wormedit/core/numeric_text.h"

#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wormedit::sys {
namespace {

auto parse_unsigned(const char* text) -> std::optional<std::uint64_t> {
    return wormedit::parse_integer<std::uint64_t>(text);
}

auto parse_signed(const char* text) -> std::optional<std::int64_t> {
    return wormedit::parse_integer<std::int64_t>(text);
}

auto parse_signed_rational(const WormUiTimingRecipeRationalText& text)
    -> std::optional<signed_rational_t> {
    const auto numerator = parse_signed(text.numerator);
    const auto denominator = parse_unsigned(text.denominator);
    if (!numerator || !denominator || *denominator == 0) return std::nullopt;
    return signed_rational_t{*numerator, *denominator};
}

auto parse_unsigned_rational(const WormUiTimingRecipeRationalText& text)
    -> std::optional<rational_t> {
    const auto numerator = parse_unsigned(text.numerator);
    const auto denominator = parse_unsigned(text.denominator);
    if (!numerator || !denominator || *denominator == 0) return std::nullopt;
    return rational_t{*numerator, *denominator};
}

void set_text(WormUiTimingRecipeRationalText& target, signed_rational_t value) {
    (void)std::snprintf(target.numerator, sizeof(target.numerator), "%lld",
                        static_cast<long long>(value.numerator));
    (void)std::snprintf(target.denominator, sizeof(target.denominator), "%llu",
                        static_cast<unsigned long long>(value.denominator));
}

void set_text(WormUiTimingRecipeRationalText& target, rational_t value) {
    (void)std::snprintf(target.numerator, sizeof(target.numerator), "%llu",
                        static_cast<unsigned long long>(value.numerator));
    (void)std::snprintf(target.denominator, sizeof(target.denominator), "%llu",
                        static_cast<unsigned long long>(value.denominator));
}

auto form_enums_valid(const WormUiTimingRecipeForm& form) -> bool {
    return form.output >= WORM_UI_TIMING_RECIPE_OUTPUT_SV &&
           form.output <= WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS &&
           form.curve >= WORM_UI_TIMING_RECIPE_CURVE_CONSTANT &&
           form.curve <= WORM_UI_TIMING_RECIPE_CURVE_LUA_EXPRESSION &&
           form.sampling_mode >= WORM_UI_TIMING_RECIPE_SAMPLING_FIXED &&
           form.sampling_mode <= WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE &&
           form.fixed_extent >= WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT &&
           form.fixed_extent <= WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS;
}

auto set_parameter(signed_rational_t& target, const WormUiTimingRecipeRationalText& source) -> bool {
    const auto parsed = parse_signed_rational(source);
    if (parsed) target = *parsed;
    return parsed.has_value();
}

auto set_curve_parameters(sv_recipe_t& candidate, const WormUiTimingRecipeForm& form) -> bool {
    auto& values = candidate.parameters;
    switch (candidate.curve) {
    case sv_recipe_curve_t::constant:
        return set_parameter(values.value_from, form.value_from);
    case sv_recipe_curve_t::linear:
        return set_parameter(values.value_from, form.value_from) &&
               set_parameter(values.value_to, form.value_to);
    case sv_recipe_curve_t::exponential:
        return set_parameter(values.value_from, form.value_from) &&
               set_parameter(values.value_to, form.value_to) && set_parameter(values.shape, form.shape);
    case sv_recipe_curve_t::bump:
        return set_parameter(values.bump_start, form.bump_start) &&
               set_parameter(values.bump_length, form.bump_length) &&
               set_parameter(values.bump_x, form.bump_x) && set_parameter(values.bump_y, form.bump_y);
    case sv_recipe_curve_t::lua_expression:
        return true;  // the expression keeps its source and parameters
    }
    return false;
}

auto set_fixed_sampling(sv_recipe_t& candidate, const WormUiTimingRecipeForm& form) -> bool {
    if (form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT) {
        const auto count = parse_unsigned(form.fixed_point_count);
        if (!count || *count > std::numeric_limits<std::uint32_t>::max()) return false;
        candidate.sampling.fixed_point_count = static_cast<std::uint32_t>(*count);
        candidate.sampling.fixed_step_rows.reset();
        return true;
    }
    const auto rows = parse_unsigned(form.fixed_step_rows);
    if (!rows) return false;
    candidate.sampling.fixed_point_count.reset();
    candidate.sampling.fixed_step_rows = *rows;
    return true;
}

auto set_adaptive_sampling(sv_recipe_t& candidate, const WormUiTimingRecipeForm& form) -> bool {
    const auto tolerance = parse_signed_rational(form.tolerance);
    const auto max_points = parse_unsigned(form.adaptive_max_points);
    if (!tolerance || !max_points || *max_points > std::numeric_limits<std::uint32_t>::max())
        return false;
    candidate.sampling.fixed_point_count.reset();
    candidate.sampling.fixed_step_rows.reset();
    candidate.sampling.tolerance = *tolerance;
    candidate.sampling.max_points = static_cast<std::uint32_t>(*max_points);
    return true;
}

auto set_sampling(sv_recipe_t& candidate, const WormUiTimingRecipeForm& form) -> bool {
    candidate.sampling.mode = static_cast<sv_recipe_sampling_mode_t>(form.sampling_mode);
    return candidate.sampling.mode == sv_recipe_sampling_mode_t::fixed
               ? set_fixed_sampling(candidate, form)
               : set_adaptive_sampling(candidate, form);
}

auto set_transition(sv_recipe_t& candidate, const WormUiTimingRecipeForm& form) -> bool {
    if (candidate.output != sv_recipe_output_t::speeds) {
        candidate.transition_duration.reset();
        candidate.transition_is_seconds.reset();
        return true;
    }
    const auto transition = parse_unsigned_rational(form.transition_duration);
    if (!transition) return false;
    candidate.transition_duration = *transition;
    candidate.transition_is_seconds = form.transition_is_seconds;
    return true;
}

} // namespace

auto recipe_form_from_recipe(const sv_recipe_t& recipe) -> WormUiTimingRecipeForm {
    WormUiTimingRecipeForm form{};
    form.output = static_cast<WormUiTimingRecipeOutput>(recipe.output);
    form.curve = static_cast<WormUiTimingRecipeCurve>(recipe.curve);
    form.sampling_mode = static_cast<WormUiTimingRecipeSamplingMode>(recipe.sampling.mode);
    form.fixed_extent = recipe.sampling.fixed_step_rows ? WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS
                                                        : WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
    set_text(form.value_from, recipe.parameters.value_from);
    set_text(form.value_to, recipe.parameters.value_to);
    set_text(form.shape, recipe.parameters.shape);
    set_text(form.bump_start, recipe.parameters.bump_start);
    set_text(form.bump_length, recipe.parameters.bump_length);
    set_text(form.bump_x, recipe.parameters.bump_x);
    set_text(form.bump_y, recipe.parameters.bump_y);
    set_text(form.tolerance, recipe.sampling.tolerance);
    set_text(form.transition_duration, recipe.transition_duration.value_or(rational_t{}));
    form.transition_is_seconds = recipe.transition_is_seconds.value_or(false);
    (void)std::snprintf(form.fixed_point_count, sizeof(form.fixed_point_count), "%u",
                        recipe.sampling.fixed_point_count.value_or(2));
    (void)std::snprintf(form.fixed_step_rows, sizeof(form.fixed_step_rows), "%llu",
                        static_cast<unsigned long long>(recipe.sampling.fixed_step_rows.value_or(1)));
    (void)std::snprintf(form.adaptive_max_points, sizeof(form.adaptive_max_points), "%u",
                        recipe.sampling.max_points);
    return form;
}

auto recipe_from_form(const WormUiTimingRecipeForm& form, const sv_recipe_t& current)
    -> std::optional<sv_recipe_t> {
    if (!form_enums_valid(form)) return std::nullopt;
    auto candidate = current;
    candidate.output = static_cast<sv_recipe_output_t>(form.output);
    candidate.curve = static_cast<sv_recipe_curve_t>(form.curve);
    if (candidate.curve != sv_recipe_curve_t::lua_expression) candidate.expression_source.clear();
    if (!set_curve_parameters(candidate, form) || !set_sampling(candidate, form) ||
        !set_transition(candidate, form))
        return std::nullopt;
    return candidate;
}

auto preview_recipe_form(const chart_document_t& chart, std::uint64_t revision,
                         const sv_recipe_t& recipe, const WormUiTimingRecipeForm& form)
    -> recipe_form_preview_t {
    recipe_form_preview_t preview{.chart = chart.id, .revision = revision, .candidate = recipe};
    const auto candidate = recipe_from_form(form, recipe);
    if (!candidate) {
        preview.error = "Values must be exact fractions and counts must be whole numbers.";
        preview.severity = notification_severity_t::warning;
        return preview;
    }
    preview.candidate = *candidate;
    const auto diagnostics = validate_sv_recipe(chart, *candidate);
    if (!diagnostics.empty()) {
        preview.error = diagnostics.front().message;
        preview.severity = projection_diagnostic_severity(diagnostics.front().severity);
    }
    return preview;
}

auto apply_recipe_form(document_history_t& history, chart_id_t chart_id, recipe_id_t recipe_id,
                       const WormUiTimingRecipeForm& form, bool& applied,
                       notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    applied = false;
    const auto chart = history.document.find_chart(chart_id);
    if (chart == nullptr) return "The chart is no longer available.";
    const auto recipe = std::ranges::find(chart->sv_recipes, recipe_id, &sv_recipe_t::id);
    if (recipe == chart->sv_recipes.end()) return "The SV recipe no longer exists.";
    const auto candidate = recipe_from_form(form, *recipe);
    if (!candidate) return "Values must be exact fractions and counts must be whole numbers.";
    const auto result = apply_sv_recipe_edit(history, history.document.revision,
                                             update_sv_recipe_action_t{chart_id, recipe_id, *candidate});
    applied = result.applied;
    if (result.applied) {
        set_feedback_severity(severity, notification_severity_t::success);
        return "SV recipe updated.";
    }
    set_feedback_severity(severity, result.diagnostics.empty() ? notification_severity_t::info
        : projection_diagnostic_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? "SV recipe is unchanged." : result.diagnostics.front().message;
}

} // namespace wormedit::sys
