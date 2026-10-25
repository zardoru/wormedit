#include <atomic>
#include "sys/compose/views/recipe_form.h"
#include "core/text_util.h"
#include "sys/notifications/projection_notification_feedback.h"
#include "wormedit/core/numeric_text.h"

#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wormedit::sys {
namespace {

constexpr const char* malformed_form_message =
    "Values must be numbers or fractions such as 0.5 or 1/2, and counts whole numbers.";

auto parse_unsigned(const char* text) -> std::optional<std::uint64_t> {
    return wormedit::parse_integer<std::uint64_t>(text);
}

auto parse_signed_rational(const wormedit::ui::timing_recipe_rational_text_t& text)
    -> std::optional<signed_rational_t> {
    return wormedit::parse_exact_rational(text.text);
}

auto parse_unsigned_rational(const wormedit::ui::timing_recipe_rational_text_t& text)
    -> std::optional<rational_t> {
    const auto parsed = parse_signed_rational(text);
    if (!parsed || parsed->numerator < 0) return std::nullopt;
    return rational_t{static_cast<std::uint64_t>(parsed->numerator), parsed->denominator};
}

void set_text(wormedit::ui::timing_recipe_rational_text_t& target, signed_rational_t value) {
    (void)std::snprintf(target.text, sizeof(target.text), "%s",
                        wormedit::format_exact_rational(value).c_str());
}

void set_text(wormedit::ui::timing_recipe_rational_text_t& target, rational_t value) {
    if (value.numerator > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        (void)std::snprintf(target.text, sizeof(target.text), "%llu/%llu",
                            static_cast<unsigned long long>(value.numerator),
                            static_cast<unsigned long long>(value.denominator));
        return;
    }
    set_text(target, signed_rational_t{static_cast<std::int64_t>(value.numerator), value.denominator});
}

auto form_enums_valid(const wormedit::ui::timing_recipe_form_t& form) -> bool {
    return wormedit::is_known_sv_recipe_output(form.output) &&
           form.curve >= wormedit::sv_recipe_curve_t::constant &&
           form.curve <= wormedit::sv_recipe_curve_t::lua_expression &&
           form.sampling_mode >= wormedit::sv_recipe_sampling_mode_t::fixed &&
           form.sampling_mode <= wormedit::sv_recipe_sampling_mode_t::adaptive &&
           form.fixed_extent >= wormedit::ui::timing_recipe_fixed_extent_t::point_count &&
           form.fixed_extent <= wormedit::ui::timing_recipe_fixed_extent_t::step_rows;
}

auto set_parameter(signed_rational_t& target, const wormedit::ui::timing_recipe_rational_text_t& source) -> bool {
    const auto parsed = parse_signed_rational(source);
    if (parsed) target = *parsed;
    return parsed.has_value();
}

auto set_lua_parameters(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
    const auto seed = parse_unsigned(form.lua_seed);
    if (!seed) return false;
    auto descriptor = candidate.lua.value_or(sv_recipe_lua_t{});
    descriptor.entry_point = form.lua_entry_point;
    descriptor.seed = *seed;
    descriptor.mode = form.lua_mode;
    descriptor.source_reference = *form.lua_source_reference
        ? std::optional<std::filesystem::path>{text_util::path_from_utf8(form.lua_source_reference)} : std::nullopt;
    descriptor.parameters.clear();
    for (const auto& input : form.lua_parameters) {
        const auto value = parse_signed_rational(input.value);
        if (!value) return false;
        descriptor.parameters.push_back({.name = input.name, .value = *value});
    }
    descriptor.cycles.clear();
    if (form.lua_mode == sv_recipe_lua_mode_t::ordered)
        for (const auto count : form.lua_cycles)
            descriptor.cycles.push_back({.emission_count = count});
    candidate.lua = std::move(descriptor);
    return true;
}

auto set_curve_parameters(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
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
        return set_lua_parameters(candidate, form);
    }
    return false;
}

auto set_fixed_sampling(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
    if (form.fixed_extent == wormedit::ui::timing_recipe_fixed_extent_t::point_count) {
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

auto set_adaptive_sampling(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
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

auto set_sampling(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
    candidate.sampling.mode = static_cast<sv_recipe_sampling_mode_t>(form.sampling_mode);
    return candidate.sampling.mode == sv_recipe_sampling_mode_t::fixed
               ? set_fixed_sampling(candidate, form)
               : set_adaptive_sampling(candidate, form);
}

auto set_transition(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
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

auto set_postprocess(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
    candidate.postprocess.clear();
    for (const auto& stage : form.postprocess) {
        const auto value = parse_signed_rational(stage.value);
        if (!value) return false;
        candidate.postprocess.push_back(
            {.kind = stage.kind, .mode = stage.mode, .solve = stage.solve, .value = *value});
    }
    return true;
}

auto set_impulses(sv_recipe_t& candidate, const wormedit::ui::timing_recipe_form_t& form) -> bool {
    const auto width = parse_unsigned_rational(form.finite_box_width_milliseconds);
    if (!width) return false;
    candidate.finite_box_width_milliseconds = *width;
    candidate.impulses.clear();
    for (const auto& impulse : form.impulses) {
        const auto at = parse_signed_rational(impulse.at);
        const auto area = parse_signed_rational(impulse.area);
        if (!at || !area) return false;
        candidate.impulses.push_back({.at = *at, .area = *area});
    }
    return true;
}

} // namespace


auto recipe_from_form(const wormedit::ui::timing_recipe_form_t& form, const sv_recipe_t& current)
    -> std::optional<sv_recipe_t> {
    if (!form_enums_valid(form)) return std::nullopt;
    auto candidate = current;
    candidate.output = static_cast<sv_recipe_output_t>(form.output);
    candidate.curve = static_cast<sv_recipe_curve_t>(form.curve);
    if (candidate.curve != sv_recipe_curve_t::lua_expression) {
        candidate.expression_source.clear();
        candidate.lua.reset();
    }
    if (!set_curve_parameters(candidate, form) || !set_sampling(candidate, form) ||
        !set_transition(candidate, form) || !set_impulses(candidate, form) ||
        !set_postprocess(candidate, form))
        return std::nullopt;
    return candidate;
}

auto recipe_form_outcome_t::next_publication_generation() -> std::uint64_t {
    static std::atomic<std::uint64_t> next{1};
    auto generation = next.fetch_add(1, std::memory_order_relaxed);
    if (!generation) generation = next.fetch_add(1, std::memory_order_relaxed);
    return generation;
}

auto recipe_form_preview_t::propose(const song_document_t& document,
    const chart_document_t& chart, const sv_recipe_t& recipe, const wormedit::ui::timing_recipe_form_t& form)
    -> recipe_form_outcome_t {
    return revise({}, document, chart, recipe, form);
}

auto recipe_form_preview_t::revise(recipe_form_outcome_t previous,
    const song_document_t& document, const chart_document_t& chart,
    const sv_recipe_t& recipe, const wormedit::ui::timing_recipe_form_t& form) -> recipe_form_outcome_t {
    auto candidate = recipe_from_form(form, recipe);
    recipe_form_report_t report{.candidate = candidate ? std::move(*candidate) : recipe,
        .has_valid_form = candidate.has_value()};
    previous.diagnostics.clear();
    if (!candidate) {
        previous.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
            .code = diagnostic_code_t::invalid_rational,
            .message = malformed_form_message});
    } else {
        previous.diagnostics = validate_sv_recipe(chart, report.candidate);
    }
    previous.value = recipe_form_proposal_t{
        .stamp = chart_stamp_t::from(document, chart.id), .payload = {.report = std::move(report)}};
    previous.complete(bool(chart.id) && previous.diagnostics.empty());
    return previous;
}

auto apply_recipe_form(document_history_t& history, const recipe_form_outcome_t& proposal)
    -> document_command_result_t {
    if (!proposal.diagnostics.empty())
        return document_command_transaction_t::rejection(history.document.revision, proposal.diagnostics);
    if (!proposal.value)
        return document_command_transaction_t::rejection(history.document.revision, {});
    const auto& pending = *proposal.value;
    auto transaction = pending.transaction(history, "SV recipe form",
        document_identity_policy_t::revision_only);
    if (!transaction.is_ready()) return transaction.finish_without_commit();
    const auto& candidate = pending.payload.report.candidate;
    const sv_recipe_prepared_scope_t prepared_scope{pending.payload.report.prepared};
    return apply_sv_recipe_edit(
        transaction, wormedit::update_edit_t<wormedit::sv_recipe_t>{.chart = pending.stamp.chart,
                                                                    .id = candidate.id,
                                                                    .value = candidate});
}

auto apply_recipe_form(document_history_t& history, chart_id_t chart_id, recipe_id_t recipe_id,
                       const wormedit::ui::timing_recipe_form_t& form, bool& applied,
                       notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    applied = false;
    const auto chart = history.read_chart(chart_id);
    if (chart == nullptr) return "The chart is no longer available.";
    const auto recipe = std::ranges::find(chart->sv_recipes, recipe_id, &sv_recipe_t::id);
    if (recipe == chart->sv_recipes.end()) return "The SV recipe no longer exists.";
    auto candidate = recipe_from_form(form, *recipe);
    if (!candidate) return malformed_form_message;
    // This synchronous path leaves semantic validation to the native command, once.
    const recipe_form_outcome_t proposal{recipe_form_proposal_t{
        .stamp = chart_stamp_t::from(history.document, chart_id),
        .payload = {.report = {.candidate = std::move(*candidate)}}}};
    const auto result = apply_recipe_form(history, proposal);
    applied = result.applied;
    if (result.applied) {
        set_feedback_severity(severity, notification_severity_t::success);
        return "SV recipe updated.";
    }
    set_feedback_severity(severity,
                          result.diagnostics.empty()
                              ? notification_severity_t::info
                              : to_notification_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? "SV recipe is unchanged." : result.diagnostics.front().message;
}

} // namespace wormedit::sys

auto wormedit::ui::timing_recipe_form_t::from(const wormedit::sv_recipe_t& recipe)
    -> timing_recipe_form_t {
    timing_recipe_form_t form;
    form.refill(recipe);
    return form;
}

void wormedit::ui::timing_recipe_form_t::refill(const wormedit::sv_recipe_t& recipe) {
    using namespace wormedit::sys;
    auto parameters = std::move(lua_parameters);
    auto cycles = std::move(lua_cycles);
    auto retained_impulses = std::move(impulses);
    auto retained_postprocess = std::move(postprocess);
    *this = {};
    lua_parameters = std::move(parameters);
    lua_cycles = std::move(cycles);
    impulses = std::move(retained_impulses);
    postprocess = std::move(retained_postprocess);
    lua_parameters.clear();
    lua_cycles.clear();
    impulses.clear();
    postprocess.clear();
    auto& form = *this;
    // An absent descriptor uses the inline-function API defaults too.
    form.lua_entry_point[0] = '\0';
    if (recipe.lua) {
        const auto& lua = *recipe.lua;
        if (lua.source_reference)
            (void)std::snprintf(form.lua_source_reference, sizeof(form.lua_source_reference), "%s",
                text_util::path_utf8(*lua.source_reference).c_str());
        (void)std::snprintf(form.lua_entry_point, sizeof(form.lua_entry_point), "%s", lua.entry_point.c_str());
        (void)std::snprintf(form.lua_seed, sizeof(form.lua_seed), "%llu", static_cast<unsigned long long>(lua.seed));
        form.lua_mode = lua.mode;
        for (const auto& parameter : lua.parameters) {
            timing_recipe_lua_parameter_form_t field;
            (void)std::snprintf(field.name, sizeof(field.name), "%s", parameter.name.c_str());
            set_text(field.value, parameter.value);
            form.lua_parameters.push_back(field);
        }
        for (const auto& cycle : lua.cycles) form.lua_cycles.push_back(cycle.emission_count);
    }
    form.output = recipe.output;
    form.curve = recipe.curve;
    set_text(form.finite_box_width_milliseconds, recipe.finite_box_width_milliseconds);
    for (const auto& impulse : recipe.impulses) {
        timing_recipe_impulse_form_t field;
        set_text(field.at, impulse.at);
        set_text(field.area, impulse.area);
        form.impulses.push_back(field);
    }
    for (const auto& stage : recipe.postprocess) {
        timing_recipe_postprocess_form_t field{.kind = stage.kind, .mode = stage.mode, .solve = stage.solve};
        set_text(field.value, stage.value);
        form.postprocess.push_back(field);
    }
    form.sampling_mode = recipe.sampling.mode;
    form.fixed_extent = recipe.sampling.fixed_step_rows ? wormedit::ui::timing_recipe_fixed_extent_t::step_rows
                                                        : wormedit::ui::timing_recipe_fixed_extent_t::point_count;
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
}
