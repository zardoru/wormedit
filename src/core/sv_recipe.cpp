#include <wormedit/core/rational.h>
#include <wormedit/core/resource_file_copy.h>
#include "chart_object_ids.h"
#include "text_util.h"
#include "timing_arithmetic.h"
#include <wormedit/core/snap.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/span_manipulation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace wormedit {
namespace {
using wormedit::reject;
thread_local const sv_recipe_script_backend_t* script_backend{};
using recipe_evaluator_t = std::function<long double(std::uint64_t)>;

struct sv_recipe_edit_t {
    const sv_recipe_edit_action_t& action;
    [[nodiscard]] auto apply(document_edit_scope_t& scope) const
        -> diagnosed_t<std::optional<std::string>>;
};

constexpr long double maximum_parameter_magnitude = 1'000'000.0L;
constexpr long double maximum_curve_output = 1'000'000'000.0L;
constexpr std::uint64_t maximum_parameter_denominator = 1'000'000'000ULL;

void reject(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message) {
    diagnostics.push_back(error(code, std::move(message)));
}

using wormedit::to_long_double;

auto is_valid_bounded_rational(signed_rational_t value, long double maximum) -> bool {
    if (value.denominator == 0 || value.denominator > maximum_parameter_denominator) {
        return false;
    }
    const auto number = to_long_double(value);
    return std::isfinite(number) && std::abs(number) <= maximum;
}

auto multiply_divide_exact(std::uint64_t value, std::uint64_t numerator, std::uint64_t denominator)
    -> std::optional<std::uint64_t> {
    if (denominator == 0) {
        return std::nullopt;
    }
    auto left = value;
    auto right = numerator;
    auto divisor = denominator;
    const auto reduce_left = std::gcd(left, divisor);
    left /= reduce_left;
    divisor /= reduce_left;
    const auto reduce_right = std::gcd(right, divisor);
    right /= reduce_right;
    divisor /= reduce_right;
    if (divisor != 1 || (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right)) {
        return std::nullopt;
    }
    return left * right;
}

auto offset_for_fraction(std::uint64_t span, signed_rational_t fraction)
    -> std::optional<std::uint64_t> {
    if (fraction.numerator < 0 || fraction.denominator == 0) {
        return std::nullopt;
    }
    return multiply_divide_exact(span, static_cast<std::uint64_t>(fraction.numerator),
                                 fraction.denominator);
}

auto offset_for_fraction_sum(std::uint64_t span, signed_rational_t left, signed_rational_t right)
    -> std::optional<std::uint64_t> {
    if (left.numerator < 0 || right.numerator < 0 || left.denominator == 0 ||
        right.denominator == 0) {
        return std::nullopt;
    }
    auto left_numerator = static_cast<std::uint64_t>(left.numerator);
    auto right_numerator = static_cast<std::uint64_t>(right.numerator);
    auto left_denominator = left.denominator;
    auto right_denominator = right.denominator;
    const auto common = std::gcd(left_denominator, right_denominator);
    const auto left_factor = right_denominator / common;
    const auto right_factor = left_denominator / common;
    if ((left_factor != 0 &&
         left_numerator > std::numeric_limits<std::uint64_t>::max() / left_factor) ||
        (right_factor != 0 &&
         right_numerator > std::numeric_limits<std::uint64_t>::max() / right_factor) ||
        (left_denominator != 0 &&
         left_factor > std::numeric_limits<std::uint64_t>::max() / left_denominator)) {
        return std::nullopt;
    }
    left_numerator *= left_factor;
    right_numerator *= right_factor;
    if (right_numerator > std::numeric_limits<std::uint64_t>::max() - left_numerator) {
        return std::nullopt;
    }
    const auto denominator = left_denominator * left_factor;
    return multiply_divide_exact(span, left_numerator + right_numerator, denominator);
}

auto recipe_range(const chart_document_t& chart, const sv_recipe_t& recipe,
                  const measure_index_t& index)
    -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
    const auto begin = index.row_of(recipe.range.begin);
    const auto end = index.row_of(recipe.range.end, measure_chart_end_policy_t::allow);
    if (!begin || !end || *end <= *begin) {
        return std::nullopt;
    }
    (void)chart;
    return std::pair{*begin, *end};
}

auto curve_value(const sv_recipe_t& recipe, long double p) -> long double {
    const auto& values = recipe.parameters;
    switch (recipe.curve) {
    case sv_recipe_curve_t::constant:
        return to_long_double(values.value_from);
    case sv_recipe_curve_t::linear: {
        const auto from = to_long_double(values.value_from);
        return from + (to_long_double(values.value_to) - from) * p;
    }
    case sv_recipe_curve_t::exponential: {
        const auto from = to_long_double(values.value_from);
        const auto delta = to_long_double(values.value_to) - from;
        const auto shape = to_long_double(values.shape);
        if (std::abs(shape) < 1.0e-8L) {
            return from + delta * p;
        }
        return from + delta * std::expm1(shape * p) / std::expm1(shape);
    }
    case sv_recipe_curve_t::bump: {
        const auto begin = to_long_double(values.bump_start);
        const auto end = begin + to_long_double(values.bump_length);
        return p >= begin && p < end ? to_long_double(values.bump_x)
                                     : to_long_double(values.bump_y);
    }
    case sv_recipe_curve_t::lua_expression:
        break;  // Script values are materialized through the scoped backend.
    }
    return std::numeric_limits<long double>::quiet_NaN();
}

auto is_valid_curve_result(long double value, sv_recipe_output_t output) -> bool {
    return std::isfinite(value) && std::abs(value) <= maximum_curve_output &&
           (output != sv_recipe_output_t::speeds || value >= 0.0L);
}

auto recipe_position_for_row(const std::pair<std::uint64_t, std::uint64_t>& range,
                             std::uint64_t row) -> long double {
    return static_cast<long double>(row - range.first) /
           static_cast<long double>(range.second - range.first);
}

auto ensure_sample_point(std::map<std::uint64_t, long double>& points, std::uint64_t row,
                         const sv_recipe_t& recipe,
                         const std::pair<std::uint64_t, std::uint64_t>& range,
                         diagnostic_list_t& diagnostics,
                         const recipe_evaluator_t& evaluate = {}) -> bool {
    const auto value = evaluate ? evaluate(row) : curve_value(recipe, recipe_position_for_row(range, row));
    if (!diagnostics.empty()) return false;
    if (!is_valid_curve_result(value, recipe.output)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "SV recipe evaluates to a non-finite, oversized, or target-invalid value");
        return false;
    }
    points.insert_or_assign(row, value);
    return true;
}

auto base_events_for(const chart_document_t& chart, sv_recipe_output_t output,
                     const measure_index_t& index)
    -> std::vector<std::pair<std::uint64_t, long double>> {
    std::vector<std::tuple<std::uint64_t, std::size_t, std::size_t, long double>> ordered;
    if (output == sv_recipe_output_t::speeds) {
        ordered.reserve(chart.speeds.size());
        for (std::size_t i = 0; i < chart.speeds.size(); ++i) {
            const auto& event = chart.speeds[i];
            if (const auto row = index.row_of(event.position)) {
                ordered.emplace_back(*row, event.provenance.source_order, i,
                                     to_long_double(event.multiplier));
            }
        }
    } else {
        ordered.reserve(chart.scrolls.size());
        for (std::size_t i = 0; i < chart.scrolls.size(); ++i) {
            const auto& event = chart.scrolls[i];
            if (const auto row = index.row_of(event.position)) {
                ordered.emplace_back(*row, event.provenance.source_order, i,
                                     to_long_double(event.multiplier));
            }
        }
    }
    std::ranges::sort(ordered);
    std::vector<std::pair<std::uint64_t, long double>> result;
    result.reserve(ordered.size());
    for (const auto& [row, source_order, index_order, value] : ordered) {
        (void)source_order;
        (void)index_order;
        result.emplace_back(row, value);
    }
    return result;
}

auto base_value_at(const std::vector<std::pair<std::uint64_t, long double>>& events,
                   std::uint64_t row) -> long double {
    const auto found = std::upper_bound(
        events.begin(), events.end(), row,
        [](std::uint64_t sought, const auto& event) { return sought < event.first; });
    return found == events.begin() ? 1.0L : std::prev(found)->second;
}

auto recipe_by_id(chart_document_t& chart, recipe_id_t id) -> sv_recipe_t* {
    const auto found = std::ranges::find(chart.sv_recipes, id, &sv_recipe_t::id);
    return found == chart.sv_recipes.end() ? nullptr : &*found;
}

auto export_rational(double value) -> std::optional<signed_rational_t> {
    constexpr std::uint64_t denominator = 1'000'000'000ULL;
    const auto scaled = static_cast<long double>(value) * static_cast<long double>(denominator);
    if (!std::isfinite(scaled) ||
        scaled < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto numerator = static_cast<std::int64_t>(std::round(scaled));
    const auto magnitude = numerator < 0 ? static_cast<std::uint64_t>(-(numerator + 1)) + 1
                                         : static_cast<std::uint64_t>(numerator);
    const auto divisor = std::gcd(magnitude, denominator);
    return signed_rational_t{numerator / static_cast<std::int64_t>(divisor), denominator / divisor};
}

auto recipe_by_id(const chart_document_t& chart, recipe_id_t id) -> const sv_recipe_t* {
    const auto found = std::ranges::find(chart.sv_recipes, id, &sv_recipe_t::id);
    return found == chart.sv_recipes.end() ? nullptr : &*found;
}

auto is_valid_recipe_enum_domains(const sv_recipe_t& recipe, diagnostic_list_t& diagnostics) -> bool {
    const auto valid =
        is_known_sv_recipe_output(recipe.output) &&
        static_cast<unsigned int>(recipe.curve) <=
            static_cast<unsigned int>(sv_recipe_curve_t::lua_expression) &&
        static_cast<unsigned int>(recipe.sampling.mode) <=
            static_cast<unsigned int>(sv_recipe_sampling_mode_t::adaptive);
    if (!valid) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe contains an unknown output, curve, or sampling mode");
    }
    return valid;
}

auto is_valid_recipe_parameters(const sv_recipe_parameters_t& params) -> bool {
    return is_valid_bounded_rational(params.value_from, maximum_parameter_magnitude) &&
           is_valid_bounded_rational(params.value_to, maximum_parameter_magnitude) &&
           is_valid_bounded_rational(params.shape, maximum_parameter_magnitude) &&
           is_valid_bounded_rational(params.bump_start, maximum_parameter_magnitude) &&
           is_valid_bounded_rational(params.bump_length, maximum_parameter_magnitude) &&
           is_valid_bounded_rational(params.bump_x, maximum_parameter_magnitude) &&
           is_valid_bounded_rational(params.bump_y, maximum_parameter_magnitude);
}

void validate_bump_region(const sv_recipe_t& recipe, std::uint64_t span,
                          diagnostic_list_t& diagnostics) {
    if (recipe.curve != sv_recipe_curve_t::bump) {
        return;
    }
    const auto begin = to_long_double(recipe.parameters.bump_start);
    const auto length = to_long_double(recipe.parameters.bump_length);
    if (begin < 0.0L || length <= 0.0L || begin + length > 1.0L) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "bump region must be a positive subrange of normalized [0, 1]");
        return;
    }
    const auto begin_offset = offset_for_fraction(span, recipe.parameters.bump_start);
    if (!begin_offset || recipe.parameters.bump_start.numerator < 0 ||
        recipe.parameters.bump_length.numerator < 0) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "bump boundaries cannot be represented exactly on the chart grid");
        return;
    }
    if (!offset_for_fraction_sum(span, recipe.parameters.bump_start,
                                 recipe.parameters.bump_length)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "bump boundaries cannot be represented exactly on the chart grid");
    }
}

void validate_transition(const sv_recipe_t& recipe, diagnostic_list_t& diagnostics) {
    const auto& duration = recipe.transition_duration;
    if (recipe.output == sv_recipe_output_t::speeds) {
        if (!duration.has_value() || !recipe.transition_is_seconds.has_value() ||
            duration->denominator == 0 ||
            duration->numerator > maximum_parameter_denominator ||
            duration->denominator > maximum_parameter_denominator ||
            to_long_double(*duration) >
                maximum_parameter_magnitude) {
            reject(diagnostics, diagnostic_code_t::invalid_rational,
                   "SPEEDS recipe requires a bounded transition duration and beat/time mode");
        }
        return;
    }
    if (duration.has_value() || recipe.transition_is_seconds.has_value()) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "transition settings are only valid for SPEEDS recipes");
    }
}

void validate_sampling(const sv_recipe_t& recipe, std::uint64_t span,
                       diagnostic_list_t& diagnostics) {
    const auto& sampling = recipe.sampling;
    if (sampling.mode == sv_recipe_sampling_mode_t::fixed) {
        if (sampling.fixed_point_count.has_value() == sampling.fixed_step_rows.has_value()) {
            reject(diagnostics, diagnostic_code_t::invalid_resolution,
                   "fixed SV sampling requires exactly one point count or row step");
        } else if (sampling.fixed_point_count.has_value()) {
            const auto count = *sampling.fixed_point_count;
            if (count < 2 || count > maximum_sv_recipe_sample_points ||
                span % static_cast<std::uint64_t>(count - 1) != 0) {
                reject(diagnostics, diagnostic_code_t::invalid_resolution,
                       "fixed SV point count must be 2..4096 and divide the exact interval");
            }
        } else if (*sampling.fixed_step_rows == 0 ||
                   *sampling.fixed_step_rows > maximum_parameter_denominator ||
                   span / *sampling.fixed_step_rows +
                           static_cast<std::uint64_t>(span % *sampling.fixed_step_rows != 0) + 1 >
                       maximum_sv_recipe_sample_points) {
            reject(diagnostics, diagnostic_code_t::invalid_resolution,
                   "fixed SV row step must be positive and bounded");
        }
    } else if (sampling.fixed_point_count.has_value() || sampling.fixed_step_rows.has_value() ||
               sampling.max_points < 2 ||
               sampling.max_points > maximum_sv_recipe_sample_points ||
               !is_valid_bounded_rational(sampling.tolerance, 1'000.0L) ||
               to_long_double(sampling.tolerance) <= 0.0L) {
        reject(diagnostics, diagnostic_code_t::invalid_resolution,
               "adaptive SV sampling requires tolerance > 0 and a point cap in 2..4096");
    }
    if (sampling.max_points < 2 || sampling.max_points > maximum_sv_recipe_sample_points) {
        reject(diagnostics, diagnostic_code_t::invalid_resolution,
               "SV recipe point cap must be in 2..4096");
    }
}

constexpr std::size_t maximum_expression_source_bytes = 16 * 1024;

// The expression source belongs to the lua_expression curve alone, bounded and free of NUL bytes.
void validate_expression_source(const sv_recipe_t& recipe, diagnostic_list_t& diagnostics) {
    const bool is_expression = recipe.curve == sv_recipe_curve_t::lua_expression;
    if (!is_expression && !recipe.expression_source.empty()) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "only a Lua expression SV recipe may carry expression source");
    }
    if (is_expression && (recipe.expression_source.empty() ||
                          !text_util::is_valid_bounded_text(recipe.expression_source, maximum_expression_source_bytes))) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "a Lua expression SV recipe needs 1 to 16384 non-NUL bytes of source");
    }
}

auto is_lua_identifier(std::string_view name) -> bool {
    const auto initial = [](char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
    };
    return !name.empty() && name.size() <= 128 && initial(name.front()) &&
        std::ranges::all_of(name, [&](char byte) {
            return initial(byte) || (byte >= '0' && byte <= '9');
        });
}

auto is_lua_content_hash(std::string_view hash) -> bool {
    return hash.empty() || text_util::is_sha256_digest(hash);
}

auto is_lua_source_reference(const std::filesystem::path& path) -> bool {
    const auto text = text_util::path_utf8(path);
    return !text.empty() && text_util::is_valid_bounded_text(text, 4096) &&
        text.find('\\') == std::string::npos && text.find(':') == std::string::npos &&
        export_asset_directory(path).has_value();
}

void validate_lua_parameters(const sv_recipe_lua_t& lua, diagnostic_list_t& diagnostics) {
    std::set<std::string_view> names;
    if (lua.parameters.size() > 256 || lua.inputs.size() > 256) {
        reject(diagnostics, diagnostic_code_t::invalid_id, "Lua recipes allow at most 256 parameters and inputs");
        return;
    }
    for (const auto& parameter : lua.parameters) {
        if (!is_lua_identifier(parameter.name) || !names.insert(parameter.name).second ||
            !is_valid_bounded_rational(parameter.value, maximum_parameter_magnitude)) {
            reject(diagnostics, diagnostic_code_t::invalid_rational,
                "Lua parameters need unique identifiers and bounded rational values");
        }
    }
    std::set<std::uint64_t> assets;
    for (const auto& input : lua.inputs) {
        if (!input.asset || !assets.insert(input.asset.value).second ||
            input.content_hash.empty() || !is_lua_content_hash(input.content_hash)) {
            reject(diagnostics, diagnostic_code_t::invalid_id,
                "Lua inputs need unique nonzero asset IDs and 64 hexadecimal hash characters");
        }
    }
}

auto fixed_emission_count(const sv_recipe_t& recipe, std::uint64_t span) -> std::uint64_t {
    if (recipe.sampling.fixed_point_count) return *recipe.sampling.fixed_point_count;
    const auto step = *recipe.sampling.fixed_step_rows;
    return span / step + static_cast<std::uint64_t>(span % step != 0) + 1;
}

void validate_lua_cycles(const sv_recipe_t& recipe, std::uint64_t span,
    diagnostic_list_t& diagnostics) {
    const auto& lua = *recipe.lua;
    if (lua.mode == sv_recipe_lua_mode_t::pure) {
        if (!lua.cycles.empty()) reject(diagnostics, diagnostic_code_t::invalid_resolution,
            "pure Lua recipes cannot declare ordered cycles");
        return;
    }
    if (recipe.sampling.mode != sv_recipe_sampling_mode_t::fixed || lua.cycles.empty() ||
        lua.cycles.size() > maximum_sv_recipe_sample_points) {
        reject(diagnostics, diagnostic_code_t::invalid_resolution,
            "ordered Lua recipes need fixed sampling and a bounded nonempty cycle plan");
        return;
    }
    std::uint64_t count{};
    for (const auto& cycle : lua.cycles) {
        if (cycle.emission_count == 0 || cycle.emission_count > maximum_sv_recipe_sample_points) {
            reject(diagnostics, diagnostic_code_t::invalid_resolution, "Lua cycles need 1 to 4096 emissions");
            return;
        }
        count += cycle.emission_count;
    }
    if (count != fixed_emission_count(recipe, span)) reject(diagnostics,
        diagnostic_code_t::invalid_resolution, "Lua cycle emissions must exactly partition the fixed schedule");
}

void validate_lua_descriptor(const sv_recipe_t& recipe, std::uint64_t span,
    diagnostic_list_t& diagnostics) {
    if (!recipe.lua) return;
    const auto& lua = *recipe.lua;
    if (recipe.curve != sv_recipe_curve_t::lua_expression || lua.api_version != 1 ||
        static_cast<unsigned int>(lua.mode) > static_cast<unsigned int>(sv_recipe_lua_mode_t::ordered)) {
        reject(diagnostics, diagnostic_code_t::invalid_id, "Lua descriptor needs a Lua recipe, API version 1 and a valid execution mode");
        return;
    }
    if ((!lua.entry_point.empty() && !is_lua_identifier(lua.entry_point)) ||
        !is_lua_content_hash(lua.source_hash) ||
        (lua.source_reference && !is_lua_source_reference(*lua.source_reference))) {
        reject(diagnostics, diagnostic_code_t::invalid_id, "Lua entry point, source reference or source hash is invalid");
    }
    validate_lua_parameters(lua, diagnostics);
    if (diagnostics.empty()) validate_lua_cycles(recipe, span, diagnostics);
}

void validate_impulses(const sv_recipe_t& recipe, std::uint64_t span, diagnostic_list_t& diagnostics) {
    if (recipe.impulses.size() > maximum_sv_recipe_impulses) {
        reject(diagnostics, diagnostic_code_t::invalid_resolution, "SV recipes allow at most 256 structural impulses");
        return;
    }
    if (!recipe.impulses.empty() && recipe.output == sv_recipe_output_t::speeds &&
        recipe.transition_duration && recipe.transition_duration->numerator != 0)
        reject(diagnostics, diagnostic_code_t::invalid_rational,
            "SV impulse boxes require instantaneous SPEEDS transitions");
    const auto width = recipe.finite_box_width_milliseconds;
    if (width.numerator == 0 || width.numerator > maximum_parameter_denominator ||
        width.denominator == 0 || width.denominator > maximum_parameter_denominator) {
        reject(diagnostics, diagnostic_code_t::invalid_rational, "SV impulse box width must be a bounded positive exact millisecond rational");
    }
    for (const auto& impulse : recipe.impulses) {
        const auto offset = offset_for_fraction(span, impulse.at);
        if (!offset || *offset >= span || !is_valid_bounded_rational(impulse.area, maximum_parameter_magnitude))
            reject(diagnostics, diagnostic_code_t::invalid_rational,
                "SV impulse needs an exact grid position in [0,1) and a bounded signed normalized-p area");
    }
}

void validate_curve_outputs(const sv_recipe_t& recipe, diagnostic_list_t& diagnostics) {
    const auto& params = recipe.parameters;
    if (recipe.curve == sv_recipe_curve_t::constant &&
        !is_valid_curve_result(to_long_double(params.value_from), recipe.output)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "constant SV recipe value is invalid for its output family");
    }
    if ((recipe.curve == sv_recipe_curve_t::linear ||
         recipe.curve == sv_recipe_curve_t::exponential) &&
        (!is_valid_curve_result(to_long_double(params.value_from), recipe.output) ||
         !is_valid_curve_result(to_long_double(params.value_to), recipe.output))) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "SV recipe endpoints are invalid for its output family");
    }
    if (recipe.curve == sv_recipe_curve_t::bump &&
        (!is_valid_curve_result(to_long_double(params.bump_x), recipe.output) ||
         !is_valid_curve_result(to_long_double(params.bump_y), recipe.output))) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "bump values are invalid for its output family");
    }
}

} // namespace

auto sv_recipe_postprocess_t::label() const -> std::string_view {
    switch (kind) {
    case sv_recipe_postprocess_kind_t::multiply: return "Multiply";
    case sv_recipe_postprocess_kind_t::normalize:
        switch (mode) {
        case sv_recipe_normalization_t::preserve_displacement: return "Normalize displacement";
        case sv_recipe_normalization_t::preserve_distance: return "Normalize distance";
        case sv_recipe_normalization_t::preserve_average: return "Normalize average";
        }
        break;
    case sv_recipe_postprocess_kind_t::balance_bump:
        return solve == sv_recipe_bump_solve_t::x ? "Balance bump: solve X" : "Balance bump: solve Y";
    }
    return {};
}

namespace {
auto validate_postprocess(const sv_recipe_t& recipe) -> diagnostic_list_t {
    if (recipe.postprocess.size() > maximum_sv_recipe_postprocess_stages)
        return {error(diagnostic_code_t::invalid_resolution, "SV postprocess stage cap exceeded")};
    for (const auto& stage : recipe.postprocess) {
        if (static_cast<unsigned>(stage.kind) > static_cast<unsigned>(sv_recipe_postprocess_kind_t::balance_bump) ||
            static_cast<unsigned>(stage.mode) > static_cast<unsigned>(sv_recipe_normalization_t::preserve_average) ||
            static_cast<unsigned>(stage.solve) > static_cast<unsigned>(sv_recipe_bump_solve_t::x) ||
            !is_valid_bounded_rational(stage.value, maximum_parameter_magnitude))
            return {error(diagnostic_code_t::invalid_rational, "Invalid SV postprocess stage")};
        if (stage.kind == sv_recipe_postprocess_kind_t::balance_bump &&
            (recipe.curve != sv_recipe_curve_t::bump || to_long_double(recipe.parameters.bump_length) >= 1))
            return {error(diagnostic_code_t::invalid_rational, "Balancing requires a finite bump and a nonempty remainder")};
        if (stage.kind == sv_recipe_postprocess_kind_t::normalize &&
            stage.mode == sv_recipe_normalization_t::preserve_distance && stage.value.numerator < 0)
            return {error(diagnostic_code_t::invalid_rational, "Distance normalization needs a nonnegative target")};
    }
    return {};
}
}

auto validate_sv_recipe(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (!recipe.id) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe must have a non-zero stable ID");
    }
    if (recipe.name.empty() || !text_util::is_valid_bounded_text(recipe.name, 256)) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe name must contain 1 to 256 non-NUL bytes");
    }
    if (!is_valid_recipe_enum_domains(recipe, diagnostics)) {
        return diagnostics;
    }

    const auto index_result = measure_index_t::from(chart);
    if (!index_result) {
        return index_result.error();
    }
    const auto& index = *index_result;
    const auto range = recipe_range(chart, recipe, index);
    if (!range) {
        reject(diagnostics, diagnostic_code_t::invalid_hold,
               "SV recipe range must be an increasing interval inside the chart grid");
        return diagnostics;
    }
    const auto span = range->second - range->first;
    if (!is_valid_recipe_parameters(recipe.parameters)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "SV recipe parameters require bounded finite rational values");
        return diagnostics;
    }
    if (recipe.curve == sv_recipe_curve_t::exponential &&
        std::abs(to_long_double(recipe.parameters.shape)) > 32.0L) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "exponential SV recipe shape must be between -32 and 32");
    }
    validate_bump_region(recipe, span, diagnostics);
    validate_expression_source(recipe, diagnostics);
    validate_transition(recipe, diagnostics);
    validate_sampling(recipe, span, diagnostics);
    validate_curve_outputs(recipe, diagnostics);
    validate_lua_descriptor(recipe, span, diagnostics);
    validate_impulses(recipe, span, diagnostics);
    const auto stages = validate_postprocess(recipe);
    diagnostics.insert(diagnostics.end(), stages.begin(), stages.end());
    return diagnostics;
}

namespace {

auto add_bump_sample_points(std::map<std::uint64_t, long double>& points,
                           const sv_recipe_t& recipe,
                           const std::pair<std::uint64_t, std::uint64_t>& range,
                           diagnostic_list_t& diagnostics) -> bool {
    if (recipe.curve != sv_recipe_curve_t::bump) {
        return true;
    }
    const auto span = range.second - range.first;
    const auto begin = offset_for_fraction(span, recipe.parameters.bump_start);
    const auto end = offset_for_fraction_sum(span, recipe.parameters.bump_start,
                                             recipe.parameters.bump_length);
    return begin.has_value() && end.has_value() &&
           ensure_sample_point(points, range.first + *begin, recipe, range, diagnostics) &&
           ensure_sample_point(points, range.first + *end, recipe, range, diagnostics);
}

auto add_fixed_sample_points(std::map<std::uint64_t, long double>& points,
                             const sv_recipe_t& recipe,
                             const std::pair<std::uint64_t, std::uint64_t>& range,
                             diagnostic_list_t& diagnostics,
                             const recipe_evaluator_t& evaluate = {}) -> bool {
    const auto span = range.second - range.first;
    if (recipe.sampling.fixed_point_count.has_value()) {
        const auto count = static_cast<std::uint64_t>(*recipe.sampling.fixed_point_count);
        const auto step = span / (count - 1);
        for (std::uint64_t i = 0; i < count; ++i) {
            if (!ensure_sample_point(points, range.first + i * step, recipe, range, diagnostics, evaluate)) {
                return false;
            }
        }
        return true;
    }
    const auto step = *recipe.sampling.fixed_step_rows;
    for (auto row = range.first; row < range.second;) {
        if (!ensure_sample_point(points, row, recipe, range, diagnostics, evaluate)) {
            return false;
        }
        if (range.second - row <= step) {
            break;
        }
        row += step;
    }
    return ensure_sample_point(points, range.second, recipe, range, diagnostics, evaluate);
}

struct adaptive_sampling_context_t {
    const sv_recipe_t& recipe;
    const std::pair<std::uint64_t, std::uint64_t>& range;
    std::size_t maximum_points{};
    long double tolerance{};
    std::map<std::uint64_t, long double>& sampled;
    diagnostic_list_t& diagnostics;
    bool& tolerance_met;
    const recipe_evaluator_t& evaluate;
};

auto refine_adaptive_interval(adaptive_sampling_context_t& context, std::uint64_t left,
                              std::uint64_t right, std::size_t depth) -> void {
    if (right - left <= 1 || depth > 64 || !context.diagnostics.empty()) {
        return;
    }
    const auto middle = left + (right - left) / 2;
    const auto actual = context.evaluate ? context.evaluate(middle) : curve_value(
        context.recipe, recipe_position_for_row(context.range, middle));
    if (!context.diagnostics.empty()) return;
    const auto left_value = context.sampled.at(left);
    const auto right_value = context.sampled.at(right);
    const auto ratio = static_cast<long double>(middle - left) /
                       static_cast<long double>(right - left);
    const auto expected = left_value + (right_value - left_value) * ratio;
    if (!is_valid_curve_result(actual, context.recipe.output)) {
        reject(context.diagnostics, diagnostic_code_t::invalid_rational,
               "adaptive SV recipe evaluates to an invalid value");
        return;
    }
    if (std::abs(actual - expected) <= context.tolerance) {
        return;
    }
    if (context.sampled.size() >= context.maximum_points) {
        context.tolerance_met = false;
        return;
    }
    context.sampled.emplace(middle, actual);
    refine_adaptive_interval(context, left, middle, depth + 1);
    refine_adaptive_interval(context, middle, right, depth + 1);
}

auto add_adaptive_sample_points(std::map<std::uint64_t, long double>& sampled,
                                const sv_recipe_t& recipe,
                                const std::pair<std::uint64_t, std::uint64_t>& range,
                                sv_recipe_sample_outcome_t& result,
                                const recipe_evaluator_t& evaluate = {}) -> bool {
    if (!ensure_sample_point(sampled, range.first, recipe, range, result.diagnostics, evaluate) ||
        !ensure_sample_point(sampled, range.second, recipe, range, result.diagnostics, evaluate)) {
        return false;
    }
    const auto maximum = static_cast<std::size_t>(recipe.sampling.max_points);
    if (sampled.size() > maximum) {
        reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
               "adaptive SV point cap is smaller than the required structural points");
        return false;
    }
    adaptive_sampling_context_t context{.recipe = recipe,
                                        .range = range,
                                        .maximum_points = maximum,
                                        .tolerance = to_long_double(recipe.sampling.tolerance),
                                        .sampled = sampled,
                                        .diagnostics = result.diagnostics,
                                        .tolerance_met = result.value.tolerance_met,
                                        .evaluate = evaluate};
    std::vector<std::uint64_t> mandatory;
    mandatory.reserve(sampled.size());
    for (const auto& [row, value] : sampled) {
        (void)value;
        mandatory.push_back(row);
    }
    for (std::size_t index = 1; index < mandatory.size(); ++index) {
        refine_adaptive_interval(context, mandatory[index - 1], mandatory[index], 0);
    }
    return result.diagnostics.empty();
}

struct script_emissions_t {
    std::vector<sv_recipe_emission_t> emissions;
    bool tolerance_met{true};
};

struct script_sampling_context_t {
    const sv_recipe_t& recipe;
    sv_recipe_script_session_t& session;
    const std::pair<std::uint64_t, std::uint64_t>& range;
    const std::vector<std::pair<std::uint64_t, long double>>& baseline;
    diagnostic_list_t& diagnostics;
    std::uint32_t emission_index{}, cycle_index{}, cycle_emission_index{};

    auto evaluate(std::uint64_t row) -> long double {
        sv_recipe_script_query_t query{
            .p = recipe_position_for_row(range, row),
            .baseline = base_value_at(baseline, row), .absolute_row = row};
        if (recipe.lua && recipe.lua->mode == sv_recipe_lua_mode_t::ordered) {
            query.cycle_index = cycle_index + 1;
            query.emission_index = ++emission_index;
            query.cycle_emission_index = ++cycle_emission_index;
            query.cycle_count = static_cast<std::uint32_t>(recipe.lua->cycles.size());
            query.emission_count = static_cast<std::uint32_t>(fixed_emission_count(recipe, range.second - range.first));
            if (cycle_emission_index == recipe.lua->cycles[cycle_index].emission_count) {
                ++cycle_index;
                cycle_emission_index = 0;
            }
        }
        auto outcome = session.evaluate(query);
        diagnostics.insert(diagnostics.end(), outcome.diagnostics.begin(), outcome.diagnostics.end());
        return outcome.value;
    }
};

auto sample_script_emissions(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> diagnosed_t<script_emissions_t> {
    diagnosed_t<script_emissions_t> result;
    result.diagnostics = validate_sv_recipe(chart, recipe);
    if (!result.diagnostics.empty()) return result;
    const auto* backend = sv_recipe_evaluation_scope_t::current();
    if (!backend) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
        "Lua SV recipes require an explicit evaluation scope")}};
    auto started = backend->start(chart, recipe);
    if (!started.diagnostics.empty()) return {.diagnostics = std::move(started.diagnostics)};
    if (!started.value) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
        "Lua SV recipe backend did not start a session")}};
    const auto index = measure_index_t::from(chart);
    const auto range = *recipe_range(chart, recipe, *index);
    const auto baseline = base_events_for(chart, recipe.output, *index);
    sv_recipe_sample_outcome_t sampled;
    script_sampling_context_t context{recipe, *started.value, range, baseline, sampled.diagnostics};
    const recipe_evaluator_t evaluate = [&](std::uint64_t row) { return context.evaluate(row); };
    std::map<std::uint64_t, long double> points;
    if (recipe.sampling.mode == sv_recipe_sampling_mode_t::fixed)
        (void)add_fixed_sample_points(points, recipe, range, sampled.diagnostics, evaluate);
    else
        (void)add_adaptive_sample_points(points, recipe, range, sampled, evaluate);
    result.diagnostics = std::move(sampled.diagnostics);
    auto finished = started.value->finish();
    result.diagnostics.insert(result.diagnostics.end(), finished.begin(), finished.end());
    if (!result.diagnostics.empty()) return result;
    for (const auto& [row, value] : points) result.value.emissions.push_back({row, value});
    result.value.tolerance_met = sampled.value.tolerance_met;
    return result;
}

auto materialize_sample_points(const chart_document_t&, const sv_recipe_t& recipe,
                               const measure_index_t& index,
                               const std::pair<std::uint64_t, std::uint64_t>& range,
                               std::map<std::uint64_t, long double>& sampled,
                               sv_recipe_sample_outcome_t& result) -> bool {
    if (sampled.size() > maximum_sv_recipe_sample_points) {
        reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
               "SV recipe structural and sampled points exceed the point cap");
        return false;
    }
    if (range.second == index.total_rows) {
        sampled.erase(index.total_rows);
        if (index.total_rows == 0 ||
            !ensure_sample_point(sampled, index.total_rows - 1, recipe, range,
                                 result.diagnostics)) {
            return false;
        }
    }
    result.value.points.reserve(sampled.size());
    for (const auto& [row, value] : sampled) {
        const auto position = index.position_at(row);
        if (!position) {
            reject(result.diagnostics, diagnostic_code_t::row_out_of_range,
                   "sampled SV recipe point is outside chart geometry");
            result.value.points.clear();
            return false;
        }
        result.value.points.push_back({.absolute_row = row,
                                 .position = *position,
                                 .value = static_cast<double>(value),
                                 .transition_duration =
                                     recipe.transition_duration.value_or(rational_t{}),
                                 .transition_is_seconds =
                                     recipe.transition_is_seconds.value_or(false)});
    }
    return true;
}

} // namespace

auto sv_recipe_samples_t::from(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> sv_recipe_sample_outcome_t {
    sv_recipe_sample_outcome_t result{};
    result.diagnostics = validate_sv_recipe(chart, recipe);
    if (!result.diagnostics.empty()) {
        return result;
    }
    if (recipe.curve == sv_recipe_curve_t::lua_expression) {
        reject(result.diagnostics, diagnostic_code_t::unsupported_feature,
               "raw Lua SV inspection requires the prepared recipe owner");
        return result;
    }
    const auto index_result = measure_index_t::from(chart);
    const auto& index = *index_result;
    const auto range = *recipe_range(chart, recipe, index);
    std::map<std::uint64_t, long double> sampled;
    if (!add_bump_sample_points(sampled, recipe, range, result.diagnostics)) {
        return result;
    }
    if (recipe.sampling.mode == sv_recipe_sampling_mode_t::fixed) {
        if (!add_fixed_sample_points(sampled, recipe, range, result.diagnostics)) {
            return result;
        }
    } else if (!add_adaptive_sample_points(sampled, recipe, range, result)) {
        return result;
    }
    (void)materialize_sample_points(chart, recipe, index, range, sampled, result);
    return result;
}

auto sv_recipe_sample_result_t::from(sv_recipe_sample_outcome_t&& outcome) -> sv_recipe_sample_result_t {
    return {.points = std::move(outcome.value.points), .diagnostics = std::move(outcome.diagnostics),
        .tolerance_met = outcome.value.tolerance_met};
}
auto sample_sv_recipe(const chart_document_t& chart, const sv_recipe_t& recipe) -> sv_recipe_sample_result_t {
    auto prepared = sv_recipe_prepared_t::from(chart, recipe);
    return sv_recipe_sample_result_t::from({.value = prepared.value.samples(), .diagnostics = std::move(prepared.diagnostics)});
}

namespace {

auto validate_recipe_emissions(std::span<const sv_recipe_emission_t> emissions,
    const std::pair<std::uint64_t, std::uint64_t>& range, sv_recipe_output_t output)
    -> diagnostic_list_t {
    if (emissions.empty() || emissions.size() > maximum_sv_recipe_sample_points)
        return {error(diagnostic_code_t::invalid_resolution,
                      "SV emissions require 1 to 4096 bounded points")};
    if (emissions.front().absolute_row != range.first)
        return {error(diagnostic_code_t::row_out_of_range,
                      "SV emissions must cover the recipe's first row")};
    std::optional<std::uint64_t> previous;
    for (const auto& emission : emissions) {
        if (emission.absolute_row < range.first || emission.absolute_row > range.second ||
            (previous && emission.absolute_row <= *previous))
            return {error(diagnostic_code_t::row_out_of_range,
                          "SV emission rows must be strictly increasing inside the recipe range")};
        if (!is_valid_curve_result(emission.value, output))
            return {error(diagnostic_code_t::invalid_rational,
                          "SV emission value is non-finite, oversized, or target-invalid")};
        previous = emission.absolute_row;
    }
    return {};
}

auto impulse_width_error(const timing_index_t& timing, std::uint64_t row) -> diagnostic_t {
    const auto first = timing.seconds_at(row);
    const auto next = timing.seconds_at(row + 1);
    std::string minimum = "unavailable";
    if (first.seconds && next.seconds) {
        const auto seconds = timing_arithmetic::subtract_exact(*next.seconds, *first.seconds);
        if (seconds && seconds->numerator > 0) {
            const auto divisor = std::gcd(seconds->denominator, std::uint64_t{1000});
            const auto numerator = checked_math::checked_multiply(
                static_cast<std::uint64_t>(seconds->numerator), 1000 / divisor);
            if (numerator)
                minimum = std::to_string(*numerator) + "/" + std::to_string(seconds->denominator / divisor);
        }
    }
    return error(diagnostic_code_t::invalid_resolution,
        "SV impulse width must map exactly to non-warped chart rows; minimum grid width here is " +
        minimum +
        " ms. Promote chart resolution or choose an exact representable width; no rounding is applied.");
}

auto impulse_exit_row(const timing_index_t& timing, const sv_recipe_t& recipe,
    std::uint64_t entry, std::uint64_t range_end) -> std::optional<std::uint64_t> {
    const auto start = timing.seconds_at(entry);
    if (!start.seconds || start.inside_warp) return std::nullopt;
    const auto width = recipe.finite_box_width_milliseconds;
    const auto end_time = timing_arithmetic::add_exact(*start.seconds,
        {static_cast<std::int64_t>(width.numerator), width.denominator * 1000});
    if (!end_time) return std::nullopt;
    const auto mapped = timing.row_at(*end_time, chart_end_policy_t::clamp_to_end);
    if (!mapped.absolute_row || mapped.inside_warp || mapped.absolute_row->numerator < 0 ||
        mapped.absolute_row->denominator == 0) return std::nullopt;
    const auto row = *mapped.absolute_row;
    if (static_cast<std::uint64_t>(row.numerator) % row.denominator != 0) return std::nullopt;
    const auto exit = static_cast<std::uint64_t>(row.numerator) / row.denominator;
    if (exit <= entry || exit > range_end) return std::nullopt;
    const auto actual_end = timing.seconds_at(exit);
    if (!actual_end.seconds || timing_arithmetic::try_normalize(*actual_end.seconds) !=
        timing_arithmetic::try_normalize(*end_time)) return std::nullopt;
    if (std::ranges::any_of(timing.segments, [&](const auto& segment) {
        return segment.warped && segment.begin_row < exit && segment.end_row > entry;
    })) return std::nullopt;
    return exit;
}

auto realized_impulse_area(long double height, std::uint64_t width, std::uint64_t span) -> long double {
    return static_cast<long double>(static_cast<double>(height)) * static_cast<long double>(width) /
        static_cast<long double>(span);
}

auto build_impulse_boxes(const chart_document_t& chart, const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range)
    -> diagnosed_t<std::vector<sv_recipe_impulse_box_t>> {
    if (recipe.impulses.empty()) return {};
    const auto timing = timing_index_t::from(chart);
    if (!timing.value) return {.diagnostics = {error(diagnostic_code_t::invalid_resolution,
        "SV impulse time boxes need a valid exact timing map and an initial BPM")}};
    std::map<std::uint64_t, signed_rational_t> areas;
    const auto span = range.second - range.first;
    for (const auto& impulse : recipe.impulses) {
        const auto row = range.first + *offset_for_fraction(span, impulse.at);
        const auto area = timing_arithmetic::add_exact(areas[row], impulse.area);
        if (!area) return {.diagnostics = {error(diagnostic_code_t::invalid_rational, "Coincident SV impulse area overflows")}};
        areas[row] = *area;
    }
    diagnosed_t<std::vector<sv_recipe_impulse_box_t>> result;
    for (const auto& [entry, authored_area] : areas) {
        const auto exit = impulse_exit_row(*timing.value, recipe, entry, range.second);
        if (!exit) return {.diagnostics = {impulse_width_error(*timing.value, entry)}};
        const auto area = to_long_double(authored_area);
        const auto height = area * static_cast<long double>(span) / static_cast<long double>(*exit - entry);
        result.value.push_back({.entry_row = entry, .exit_row = *exit, .raw_area = area,
            .area = area, .realized_area = realized_impulse_area(height, *exit - entry, span),
            .width_milliseconds = to_long_double(recipe.finite_box_width_milliseconds), .height = height});
        result.value.back().area_error = result.value.back().realized_area - area;
    }
    return result;
}

auto build_builtin_impulse_emissions(const chart_document_t& chart, const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range, const sv_recipe_samples_t& sampled)
    -> diagnosed_t<std::vector<sv_recipe_emission_t>> {
    const auto boxes = build_impulse_boxes(chart, recipe, range);
    if (!boxes.diagnostics.empty()) return {.diagnostics = boxes.diagnostics};
    std::set<std::uint64_t> rows;
    for (const auto& point : sampled.points) rows.insert(point.absolute_row);
    for (const auto& box : boxes.value) {
        rows.insert(box.entry_row);
        rows.insert(box.exit_row - 1);
        rows.insert(box.exit_row);
    }
    diagnosed_t<std::vector<sv_recipe_emission_t>> result;
    for (const auto row : rows) result.value.push_back({.absolute_row = row,
        .value = curve_value(recipe, recipe_position_for_row(range, row))});
    return result;
}

auto impulse_height_at(std::span<const sv_recipe_impulse_box_t> boxes, std::uint64_t row) -> long double {
    long double value{};
    for (const auto& box : boxes)
        if (row >= box.entry_row && row < box.exit_row) value += box.height;
    return value;
}

void add_impulse_inspection_rows(std::map<std::uint64_t, long double>& points,
    std::span<const sv_recipe_emission_t> emissions, std::span<const sv_recipe_impulse_box_t> boxes,
    std::uint64_t total_rows) {
    for (const auto& box : boxes) {
        for (const auto row : {box.entry_row, box.exit_row}) {
            if (row >= total_rows) continue;
            const auto after = std::upper_bound(emissions.begin(), emissions.end(), row,
                [](std::uint64_t sought, const auto& point) { return sought < point.absolute_row; });
            points.try_emplace(row, std::prev(after)->value);
        }
    }
}

auto sample_recipe_emissions(const measure_index_t& index, const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range,
    std::span<const sv_recipe_emission_t> emissions,
    std::span<const sv_recipe_impulse_box_t> boxes = {}) -> sv_recipe_sample_outcome_t {
    std::map<std::uint64_t, long double> points;
    for (const auto& emission : emissions)
        if (emission.absolute_row < index.total_rows)
            points.emplace(emission.absolute_row, emission.value);
    const auto last = range.second == index.total_rows ? index.total_rows - 1 : range.second;
    if (!points.contains(last)) points.emplace(last, points.rbegin()->second);
    add_impulse_inspection_rows(points, emissions, boxes, index.total_rows);
    if (points.size() > maximum_sv_recipe_sample_points)
        return {.diagnostics = {error(diagnostic_code_t::invalid_resolution,
            "SV emission inspection boundaries exceed the point cap")}};
    sv_recipe_sample_outcome_t result{};
    result.value.points.reserve(points.size());
    for (const auto& [row, value] : points) {
        const auto position = index.position_at(row);
        if (!position)
            return {.diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                          "SV emission point is outside chart geometry")}};
        const auto combined = value + impulse_height_at(boxes, row);
        if (!is_valid_curve_result(combined, recipe.output)) return {.diagnostics = {
            error(diagnostic_code_t::invalid_rational, "SV impulse box output is non-finite, oversized or target-forbidden")}};
        result.value.points.push_back({.absolute_row = row, .position = *position,
            .value = static_cast<double>(combined),
            .transition_duration = recipe.transition_duration.value_or(rational_t{}),
            .transition_is_seconds = recipe.transition_is_seconds.value_or(false)});
    }
    return result;
}

auto emission_value_at(std::span<const sv_recipe_emission_t> emissions, std::uint64_t row) -> long double {
    const auto after = std::upper_bound(emissions.begin(), emissions.end(), row,
        [](std::uint64_t sought, const auto& point) { return sought < point.absolute_row; });
    return std::prev(after)->value;
}

auto is_bump_region(const sv_recipe_t& recipe, const std::pair<std::uint64_t, std::uint64_t>& range,
    std::uint64_t row) -> bool {
    const auto p = recipe_position_for_row(range, row);
    const auto begin = to_long_double(recipe.parameters.bump_start);
    return p >= begin && p < begin + to_long_double(recipe.parameters.bump_length);
}

struct postprocess_integrals_t {
    std::array<long double, 2> baseline{}, processed{}, impulses{};
};
auto postprocess_integrals(const chart_document_t& chart, const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range,
    std::span<const sv_recipe_emission_t> emissions, std::span<const sv_recipe_impulse_box_t> boxes,
    bool distance) -> postprocess_integrals_t {
    const auto index = measure_index_t::from(chart);
    const auto base = base_events_for(chart, recipe.output, *index);
    std::set<std::uint64_t> rows{range.first, range.second};
    for (const auto& point : emissions) rows.insert(point.absolute_row);
    for (const auto& [row, value] : base) {
        (void)value;
        if (row > range.first && row < range.second) rows.insert(row);
    }
    postprocess_integrals_t result;
    for (auto row = rows.begin(), next = std::next(row); next != rows.end(); ++row, ++next) {
        const auto region = is_bump_region(recipe, range, *row) ? 0U : 1U;
        const auto baseline = base_value_at(base, *row);
        const auto weighted = baseline * emission_value_at(emissions, *row);
        const auto length = static_cast<long double>(*next - *row);
        result.baseline[region] += (distance ? std::abs(baseline) : baseline) * length;
        result.processed[region] += (distance ? std::abs(weighted) : weighted) * length;
    }
    const auto span = static_cast<long double>(range.second - range.first);
    for (const auto& box : boxes) {
        const auto region = is_bump_region(recipe, range, box.entry_row) ? 0U : 1U;
        const auto weighted = box.area * span * base_value_at(base, box.entry_row);
        result.impulses[region] += distance ? std::abs(weighted) : weighted;
        result.processed[region] += distance ? std::abs(weighted) : weighted;
    }
    return result;
}

auto postprocess_factor(const chart_document_t& chart, const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range,
    std::span<const sv_recipe_emission_t> emissions, std::span<const sv_recipe_impulse_box_t> boxes,
    const sv_recipe_postprocess_t& stage) -> std::optional<long double> {
    const auto target = to_long_double(stage.value);
    if (stage.kind == sv_recipe_postprocess_kind_t::multiply) return target;
    const auto integrals = postprocess_integrals(chart, recipe, range, emissions, boxes,
        stage.kind == sv_recipe_postprocess_kind_t::normalize && stage.mode == sv_recipe_normalization_t::preserve_distance);
    const auto total = integrals.baseline[0] + integrals.baseline[1];
    const auto solved = stage.solve == sv_recipe_bump_solve_t::x ? 0U : 1U;
    const auto denominator = stage.kind == sv_recipe_postprocess_kind_t::normalize
        ? integrals.processed[0] + integrals.processed[1] : integrals.baseline[solved];
    if (!std::isfinite(denominator) || denominator == 0) return {};
    return stage.kind == sv_recipe_postprocess_kind_t::normalize ? total * target / denominator
        : (total * target - integrals.processed[1U - solved] - integrals.impulses[solved]) / denominator;
}

void scale_impulse_boxes(std::vector<sv_recipe_impulse_box_t>& boxes, long double factor, std::uint64_t span) {
    for (auto& box : boxes) {
        box.area *= factor;
        box.height *= factor;
        box.realized_area = realized_impulse_area(box.height, box.exit_row - box.entry_row, span);
        box.area_error = box.realized_area - box.area;
    }
}

auto transform_recipe_batch(const chart_document_t& chart, const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range,
    std::vector<sv_recipe_emission_t>& emissions, std::vector<sv_recipe_impulse_box_t>& boxes) -> diagnostic_list_t {
    for (const auto& stage : recipe.postprocess) {
        const auto factor = postprocess_factor(chart, recipe, range, emissions, boxes, stage);
        if (!factor)
            return {error(diagnostic_code_t::invalid_rational, "SV postprocess denominator is zero or non-finite")};
        if (stage.kind != sv_recipe_postprocess_kind_t::balance_bump)
            scale_impulse_boxes(boxes, *factor, range.second - range.first);
        for (auto& point : emissions) {
            if (stage.kind != sv_recipe_postprocess_kind_t::balance_bump) point.value *= *factor;
            else if (is_bump_region(recipe, range, point.absolute_row) == (stage.solve == sv_recipe_bump_solve_t::x)) point.value = *factor;
            if (!is_valid_curve_result(point.value, recipe.output))
                return {error(diagnostic_code_t::invalid_rational, "SV postprocess output is non-finite, oversized, or target-forbidden")};
        }
    }
    return {};
}

auto add_bump_emission_boundaries(const sv_recipe_t& recipe,
    const std::pair<std::uint64_t, std::uint64_t>& range,
    std::vector<sv_recipe_emission_t>& emissions) -> diagnostic_list_t {
    if (recipe.curve != sv_recipe_curve_t::bump || recipe.postprocess.empty()) return {};
    const auto span = range.second - range.first;
    const std::array offsets{*offset_for_fraction(span, recipe.parameters.bump_start),
        *offset_for_fraction_sum(span, recipe.parameters.bump_start, recipe.parameters.bump_length)};
    for (const auto offset : offsets) {
        const auto row = range.first + offset;
        const auto found = std::lower_bound(emissions.begin(), emissions.end(), row,
            [](const auto& point, std::uint64_t sought) { return point.absolute_row < sought; });
        if (found == emissions.end() || found->absolute_row != row) {
            const auto value = emission_value_at(emissions, row);
            emissions.insert(found, {.absolute_row = row, .value = value});
        }
    }
    if (emissions.size() > maximum_sv_recipe_sample_points)
        return {error(diagnostic_code_t::invalid_resolution, "SV bump boundaries exceed the emission cap")};
    return {};
}

} // namespace

namespace {
thread_local const sv_recipe_prepared_scope_t* prepared_scope{};
}

sv_recipe_evaluation_scope_t::sv_recipe_evaluation_scope_t(const sv_recipe_script_backend_t& backend)
    : previous_(std::exchange(script_backend, &backend)) {}
sv_recipe_evaluation_scope_t::~sv_recipe_evaluation_scope_t() { script_backend = previous_; }
auto sv_recipe_evaluation_scope_t::current() -> const sv_recipe_script_backend_t* { return script_backend; }

sv_recipe_prepared_scope_t::sv_recipe_prepared_scope_t(std::span<const sv_recipe_prepared_t> prepared)
    : prepared_(prepared), previous_(std::exchange(prepared_scope, this)) {}
sv_recipe_prepared_scope_t::~sv_recipe_prepared_scope_t() { prepared_scope = previous_; }
auto sv_recipe_prepared_scope_t::current() -> const sv_recipe_prepared_scope_t* { return prepared_scope; }
auto sv_recipe_prepared_scope_t::find(const chart_document_t& chart, const sv_recipe_t& recipe) const
    -> const sv_recipe_prepared_t* {
    const auto index = measure_index_t::from(chart);
    if (!index) return nullptr;
    const auto found = std::ranges::find_if(prepared_, [&](const auto& prepared) {
        return prepared.recipe() == recipe && prepared.has_matching_geometry(chart, *index);
    });
    return found == prepared_.end() ? nullptr : &*found;
}

auto sv_recipe_prepared_t::from(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> diagnosed_t<sv_recipe_prepared_t> {
    if (const auto* scope = sv_recipe_prepared_scope_t::current()) {
        if (const auto* prepared = scope->find(chart, recipe)) return {.value = *prepared};
        if (recipe.curve == sv_recipe_curve_t::lua_expression) return {.value = sv_recipe_prepared_t{}, .diagnostics = {
            error(diagnostic_code_t::invalid_binding, "Lua recipe has no matching prepared snapshot")}};
    }
    if (recipe.curve == sv_recipe_curve_t::lua_expression) {
        auto sampled = sample_script_emissions(chart, recipe);
        if (!sampled.diagnostics.empty()) return {.value = sv_recipe_prepared_t{}, .diagnostics = std::move(sampled.diagnostics)};
        auto prepared = from_emissions(chart, recipe, sampled.value.emissions);
        prepared.value.samples_.tolerance_met = sampled.value.tolerance_met;
        prepared.value.raw_samples_.tolerance_met = sampled.value.tolerance_met;
        return prepared;
    }
    return from_builtin_samples(chart, recipe, sv_recipe_samples_t::from(chart, recipe));
}

auto sv_recipe_prepared_t::from_builtin_samples(const chart_document_t& chart, const sv_recipe_t& recipe,
    sv_recipe_sample_outcome_t sampled) -> diagnosed_t<sv_recipe_prepared_t> {
    if (sampled.diagnostics.empty() && (!recipe.postprocess.empty() || !recipe.impulses.empty())) {
        const auto index = measure_index_t::from(chart);
        const auto range = *recipe_range(chart, recipe, *index);
        diagnosed_t<std::vector<sv_recipe_emission_t>> emissions;
        if (!recipe.impulses.empty()) emissions = build_builtin_impulse_emissions(chart, recipe, range, sampled.value);
        else {
            for (const auto& point : sampled.value.points)
                emissions.value.push_back({.absolute_row = point.absolute_row,
                    .value = curve_value(recipe, recipe_position_for_row(range, point.absolute_row))});
        }
        if (!emissions.diagnostics.empty()) return {.value = sv_recipe_prepared_t{}, .diagnostics = std::move(emissions.diagnostics)};
        auto result = from_emissions(chart, recipe, emissions.value);
        result.value.uses_analytic_curve_ = recipe.postprocess.empty();
        if (recipe.impulses.empty()) result.value.raw_samples_ = std::move(sampled.value);
        result.value.samples_.tolerance_met = sampled.value.tolerance_met;
        result.value.raw_samples_.tolerance_met = sampled.value.tolerance_met;
        return result;
    }
    sv_recipe_prepared_t prepared;
    prepared.samples_ = std::move(sampled.value);
    if (sampled.diagnostics.empty()) {
        const auto index = measure_index_t::from(chart);
        prepared.recipe_ = recipe;
        prepared.chart_ = chart.id;
        prepared.divisions_ = chart.divisions_per_standard_measure;
        prepared.range_ = *recipe_range(chart, recipe, *index);
    }
    return {.value = std::move(prepared), .diagnostics = std::move(sampled.diagnostics)};
}

auto sv_recipe_prepared_t::from_emissions(const chart_document_t& chart,
    const sv_recipe_t& recipe, std::span<const sv_recipe_emission_t> emissions)
    -> diagnosed_t<sv_recipe_prepared_t> {
    sv_recipe_prepared_t prepared;
    auto diagnostics = validate_sv_recipe(chart, recipe);
    if (!diagnostics.empty())
        return {.value = std::move(prepared), .diagnostics = std::move(diagnostics)};
    const auto index = measure_index_t::from(chart);
    const auto range = *recipe_range(chart, recipe, *index);
    diagnostics = validate_recipe_emissions(emissions, range, recipe.output);
    if (!diagnostics.empty())
        return {.value = std::move(prepared), .diagnostics = std::move(diagnostics)};
    auto boxes = build_impulse_boxes(chart, recipe, range);
    if (!boxes.diagnostics.empty()) return {.value = std::move(prepared), .diagnostics = std::move(boxes.diagnostics)};
    if (!recipe.postprocess.empty()) {
        auto raw = sample_recipe_emissions(*index, recipe, range, emissions, boxes.value);
        if (!raw.diagnostics.empty()) return {.value = std::move(prepared), .diagnostics = std::move(raw.diagnostics)};
        prepared.raw_samples_ = std::move(raw.value);
    }
    std::vector<sv_recipe_emission_t> processed{emissions.begin(), emissions.end()};
    diagnostics = add_bump_emission_boundaries(recipe, range, processed);
    if (diagnostics.empty()) diagnostics = transform_recipe_batch(chart, recipe, range, processed, boxes.value);
    if (!diagnostics.empty())
        return {.value = std::move(prepared), .diagnostics = std::move(diagnostics)};
    auto sampled = sample_recipe_emissions(*index, recipe, range, processed, boxes.value);
    prepared.samples_ = std::move(sampled.value);
    if (sampled.diagnostics.empty()) {
        prepared.recipe_ = recipe;
        prepared.chart_ = chart.id;
        prepared.divisions_ = chart.divisions_per_standard_measure;
        prepared.range_ = range;
        prepared.emissions_ = std::move(processed);
        prepared.impulse_boxes_ = std::move(boxes.value);
    }
    return {.value = std::move(prepared), .diagnostics = std::move(sampled.diagnostics)};
}

auto sv_recipe_prepared_t::value_at(std::uint64_t row) const -> std::optional<long double> {
    const auto continuous = continuous_value_at(row);
    return continuous ? std::optional{*continuous + impulse_height_at(impulse_boxes_, row)} : std::nullopt;
}

auto sv_recipe_prepared_t::has_impulse_at(std::uint64_t entry_row) const -> bool {
    return std::ranges::any_of(impulse_boxes_, [entry_row](const auto& box) {
        return box.entry_row == entry_row;
    });
}

auto sv_recipe_prepared_t::continuous_value_at(std::uint64_t row) const -> std::optional<long double> {
    if (row < range_.first || row >= range_.second) return std::nullopt;
    if (emissions_.empty() || uses_analytic_curve_)
        return curve_value(recipe_, recipe_position_for_row(range_, row));
    const auto after = std::upper_bound(emissions_.begin(), emissions_.end(), row,
        [](std::uint64_t sought, const auto& emission) { return sought < emission.absolute_row; });
    return std::prev(after)->value;
}

auto sv_recipe_prepared_t::has_matching_geometry(const chart_document_t& chart,
    const measure_index_t& index) const -> bool {
    return range_.first < range_.second && chart.id == chart_ &&
        chart.divisions_per_standard_measure == divisions_ &&
        index.row_of(recipe_.range.begin) == range_.first &&
        index.row_of(recipe_.range.end, measure_chart_end_policy_t::allow) == range_.second &&
        std::ranges::all_of(samples_.points, [&](const auto& point) {
            return index.row_of(point.position) == point.absolute_row;
        });
}

namespace {

auto append_recipe_sample(const chart_document_t& chart, const sv_recipe_t& recipe,
                          std::vector<sv_recipe_prepared_t>& samples,
                          diagnostic_list_t& diagnostics,
                          const sv_recipe_prepared_t* prepared = nullptr) -> bool {
    auto sample = prepared ? diagnosed_t<sv_recipe_prepared_t>{.value = *prepared}
                           : sv_recipe_prepared_t::from(chart, recipe);
    if (!sample.diagnostics.empty()) {
        diagnostics.insert(diagnostics.end(), sample.diagnostics.begin(), sample.diagnostics.end());
        return false;
    }
    samples.push_back(std::move(sample.value));
    return true;
}

auto collect_recipe_samples(const chart_document_t& chart, sv_recipe_output_t output,
                           const sv_recipe_t* candidate,
                           std::optional<recipe_id_t> replacing_recipe,
                           diagnostic_list_t& diagnostics,
                           const sv_recipe_prepared_t* prepared_candidate)
    -> std::vector<sv_recipe_prepared_t> {
    std::vector<sv_recipe_prepared_t> samples;
    for (const auto& stored : chart.sv_recipes) {
        const auto* recipe = replacing_recipe && stored.id == *replacing_recipe
                                 ? candidate
                                 : &stored;
        if (recipe == nullptr || !recipe->enabled || recipe->output != output) {
            continue;
        }
        (void)append_recipe_sample(chart, *recipe, samples, diagnostics,
                                  recipe == candidate ? prepared_candidate : nullptr);
    }
    if (candidate != nullptr && !replacing_recipe && candidate->enabled &&
        candidate->output == output) {
        (void)append_recipe_sample(chart, *candidate, samples, diagnostics, prepared_candidate);
    }
    return samples;
}

auto has_overlapping_speed_transition(const chart_document_t& chart,
                                     const measure_index_t& index,
                                     std::span<const sv_recipe_prepared_t> recipes) -> bool {
    std::uint64_t last_recipe_end{};
    for (const auto& recipe : recipes)
        last_recipe_end = std::max(last_recipe_end, recipe.range().second);
    return std::ranges::any_of(chart.speeds, [&](const auto& event) {
        const auto row = index.row_of(event.position);
        return event.transition_beats.numerator != 0 && row && *row < last_recipe_end;
    });
}

void add_authored_event_rows(const std::vector<std::pair<std::uint64_t, long double>>& events,
                             std::span<const sv_recipe_prepared_t> recipes,
                             std::set<std::uint64_t>& rows) {
    for (const auto& [row, value] : events) {
        (void)value;
        if (std::ranges::any_of(recipes, [&](const auto& recipe) {
                return row >= recipe.range().first && row < recipe.range().second;
            })) {
            rows.insert(row);
        }
    }
}

struct recipe_composition_context_t {
    sv_recipe_output_t output{};
    const measure_index_t& index;
    std::span<const sv_recipe_prepared_t> recipes;
    const std::vector<std::pair<std::uint64_t, long double>>& base_events;
};

auto composed_impulse_height(const recipe_composition_context_t& context, std::uint64_t row) -> long double {
    long double height{};
    for (const auto& source : context.recipes) {
        for (const auto& box : source.impulse_boxes()) {
            if (row < box.entry_row || row >= box.exit_row) continue;
            auto weighted = box.height * base_value_at(context.base_events, box.entry_row);
            for (const auto& other : context.recipes) {
                if (other.has_impulse_at(box.entry_row)) continue;
                weighted *= other.continuous_value_at(box.entry_row).value_or(1);
            }
            height += weighted;
        }
    }
    return height;
}

auto compose_recipe_point(const recipe_composition_context_t& context, std::uint64_t row,
                         diagnostic_list_t& diagnostics)
    -> std::optional<sv_recipe_point_t> {
    long double value = base_value_at(context.base_events, row);
    const sv_recipe_t* transition_source{};
    for (const auto& recipe : context.recipes) {
        const auto prepared_value = recipe.continuous_value_at(row);
        if (!prepared_value) continue;
        value *= *prepared_value;
        if (context.output == sv_recipe_output_t::speeds) {
            transition_source = &recipe.recipe();
        }
    }
    value += composed_impulse_height(context, row);
    if (!is_valid_curve_result(value, context.output)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "composed SV recipe value is non-finite, oversized, or target-invalid");
        return std::nullopt;
    }
    const auto position = context.index.position_at(row);
    if (!position) {
        reject(diagnostics, diagnostic_code_t::row_out_of_range,
               "compiled SV point is outside chart geometry");
        return std::nullopt;
    }
    auto point = sv_recipe_point_t{
        .absolute_row = row, .position = *position, .value = static_cast<double>(value)};
    if (transition_source != nullptr) {
        point.transition_duration = *transition_source->transition_duration;
        point.transition_is_seconds = *transition_source->transition_is_seconds;
    }
    return point;
}

auto compose_recipe_points(const recipe_composition_context_t& context,
                           const std::set<std::uint64_t>& rows,
                           sv_recipe_compilation_outcome_t& result) -> bool {
    result.value.points.reserve(rows.size());
    for (const auto row : rows) {
        auto point = compose_recipe_point(context, row, result.diagnostics);
        if (!point.has_value()) {
            result.value.points.clear();
            return false;
        }
        result.value.points.push_back(std::move(*point));
    }
    return true;
}

auto compile_sv_recipes_impl(const chart_document_t& chart, sv_recipe_output_t output,
                             const sv_recipe_t* candidate,
                             std::optional<recipe_id_t> replacing_recipe,
                             const sv_recipe_prepared_t* prepared_candidate = nullptr)
    -> sv_recipe_compilation_outcome_t {
    sv_recipe_compilation_outcome_t result{.value = {.output = output}};
    if (!is_known_sv_recipe_output(output)) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV compilation requested an unknown output family");
        return result;
    }
    if (replacing_recipe && candidate == nullptr) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe replacement preview has no candidate");
        return result;
    }
    if (replacing_recipe &&
        std::ranges::count(chart.sv_recipes, *replacing_recipe, &sv_recipe_t::id) != 1) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe replacement preview refers to a missing or ambiguous recipe");
        return result;
    }
    const auto index_result = measure_index_t::from(chart);
    if (!index_result) {
        result.diagnostics = index_result.error();
        return result;
    }
    auto samples = collect_recipe_samples(chart, output, candidate, replacing_recipe,
                                          result.diagnostics, prepared_candidate);
    if (!result.diagnostics.empty() || samples.empty()) {
        return result;
    }
    return sv_recipe_compiled_output_t::from(chart, output, samples);
}

} // namespace

auto sv_recipe_compilation_t::from(sv_recipe_compilation_outcome_t&& outcome) -> sv_recipe_compilation_t {
    return {.output = outcome.value.output, .points = std::move(outcome.value.points),
        .diagnostics = std::move(outcome.diagnostics)};
}
auto sv_recipe_compilation_t::into_outcome() && -> sv_recipe_compilation_outcome_t {
    return {.value = {.output = output, .points = std::move(points)}, .diagnostics = std::move(diagnostics)};
}
auto sv_recipe_compiled_output_t::from(const chart_document_t& chart, sv_recipe_output_t output)
    -> sv_recipe_compilation_outcome_t {
    return compile_sv_recipes_impl(chart, output, nullptr, std::nullopt);
}

auto sv_recipe_compiled_output_t::from(const chart_document_t& chart, sv_recipe_output_t output,
    std::span<const sv_recipe_prepared_t> recipes) -> sv_recipe_compilation_outcome_t {
    sv_recipe_compilation_outcome_t result{.value = {.output = output}};
    if (!is_known_sv_recipe_output(output)) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV compilation requested an unknown output family");
        return result;
    }
    const auto built_index = measure_index_t::from(chart);
    if (!built_index) {
        result.diagnostics = built_index.error();
        return result;
    }
    const auto& index = *built_index;
    std::set<std::uint64_t> rows;
    for (const auto& recipe : recipes) {
        if (recipe.recipe().output != output || !recipe.recipe().enabled ||
            !recipe.has_matching_geometry(chart, index)) {
            reject(result.diagnostics, diagnostic_code_t::invalid_binding,
                   "Prepared SV recipe does not match the chart geometry or active output family");
            return result;
        }
        for (const auto& point : recipe.samples().points) rows.insert(point.absolute_row);
    }
    if (recipes.empty()) return result;
    if (output == sv_recipe_output_t::speeds &&
        has_overlapping_speed_transition(chart, index, recipes)) {
        reject(result.diagnostics, diagnostic_code_t::invalid_rational,
               "SV recipe composition over a potentially overlapping authored SPEEDS "
               "transition is not yet supported");
        return result;
    }
    const auto base_events = base_events_for(chart, output, index);
    add_authored_event_rows(base_events, recipes, rows);
    const recipe_composition_context_t context{
        .output = output, .index = index, .recipes = recipes, .base_events = base_events};
    (void)compose_recipe_points(context, rows, result);
    return result;
}
auto compile_sv_recipes(const chart_document_t& chart, sv_recipe_output_t output) -> sv_recipe_compilation_t {
    return sv_recipe_compilation_t::from(sv_recipe_compiled_output_t::from(chart, output));
}

auto sv_recipe_draft_t::validate_target(const song_document_t& document, chart_id_t selected,
    proposal_operation_t) const -> result_t<const chart_document_t*, diagnostic_t> {
    const auto* chart = document.read_chart(selected);
    if (!chart)
        return std::unexpected(error(diagnostic_code_t::invalid_id,
            "SV recipe draft refers to an unknown chart"));
    if (existing_recipe && std::ranges::count(chart->sv_recipes, *existing_recipe, &sv_recipe_t::id) != 1)
        return std::unexpected(error(diagnostic_code_t::invalid_id,
            "SV recipe draft target is missing or ambiguous"));
    return chart;
}

auto sv_recipe_draft_t::preview_candidate() const -> sv_recipe_t {
    auto preview = candidate;
    preview.id = existing_recipe.value_or(recipe_id_t{1});
    return preview;
}

auto sv_recipe_draft_t::propose(const document_history_t& history, chart_id_t chart_id,
                           sv_recipe_t candidate, bool timing_locked) -> proposal_outcome_t<sv_recipe_draft_t> {
    proposal_outcome_t<sv_recipe_draft_t> outcome;
    auto pending = proposal_t<sv_recipe_draft_t>::from(chart_stamp_t::from(history, chart_id),
        sv_recipe_draft_t{.candidate = std::move(candidate)});
    auto& draft = pending.payload;
    if (timing_locked) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                     std::string(begin_locked_message)));
        return outcome;
    }
    const auto* chart = history.read_chart(chart_id);
    if (chart == nullptr || !chart_id) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                     "SV recipe draft refers to an unknown chart"));
        return outcome;
    }
    if (chart->sv_recipes.size() >= maximum_sv_recipes_per_chart) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                     "SV recipe insertion would exceed the per-chart cap"));
        return outcome;
    }
    // Stable IDs are assigned by the insert command only after the draft is
    // accepted. A caller-provided ID must not make a new draft look like an
    // update or reserve a durable ID during preview.
    draft.candidate.id = {};
    outcome.value = std::move(pending);
    return outcome;
}

auto sv_recipe_draft_t::propose(const document_history_t& history, chart_id_t chart_id,
                                    recipe_id_t recipe_id, bool timing_locked)
    -> proposal_outcome_t<sv_recipe_draft_t> {
    proposal_outcome_t<sv_recipe_draft_t> outcome;
    auto pending = proposal_t<sv_recipe_draft_t>::from(chart_stamp_t::from(history, chart_id),
        sv_recipe_draft_t{.existing_recipe = recipe_id});
    auto& draft = pending.payload;
    if (timing_locked) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                     std::string(begin_locked_message)));
        return outcome;
    }
    const auto* chart = history.read_chart(chart_id);
    if (chart == nullptr || !chart_id || !recipe_id) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                     "SV recipe draft refers to an unknown chart or recipe"));
        return outcome;
    }
    const auto* recipe = recipe_by_id(*chart, recipe_id);
    if (recipe == nullptr ||
        std::ranges::count(chart->sv_recipes, recipe_id, &sv_recipe_t::id) != 1) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                     "SV recipe draft target is missing or ambiguous"));
        return outcome;
    }
    draft.candidate = *recipe;
    outcome.value = std::move(pending);
    return outcome;
}

auto sv_recipe_draft_t::revise(proposal_revision_scope_t<chart_stamp_t>&,
    sv_recipe_t candidate) -> bool {
    auto& draft = *this;
    candidate.id = draft.existing_recipe.value_or(recipe_id_t{});
    draft.candidate = std::move(candidate);
    return true;
}

auto sv_recipe_draft_t::revise(proposal_revision_scope_t<chart_stamp_t>& scope,
    position_span_t pointer_hits, snap_grid_t snap)
    -> bool {
    auto& draft = *this;
    auto& diagnostics = scope.diagnostics();
    const auto* chart = scope.target();
    auto built_index = measure_index_t::from(*chart);
    if (!built_index) {
        diagnostics = std::move(built_index.error());
        return false;
    }
    const auto& index = *built_index;
    const auto snapped_begin = snap_span_position(*chart, index, pointer_hits.begin, snap);
    const auto snapped_end = snap_span_position(*chart, index, pointer_hits.end, snap);
    const auto begin_row = snapped_begin
        ? index.row_of(*snapped_begin, measure_chart_end_policy_t::allow) : std::nullopt;
    const auto end_row = snapped_end
        ? index.row_of(*snapped_end, measure_chart_end_policy_t::allow) : std::nullopt;
    if (!begin_row || !end_row) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                     "SV recipe draft range cannot be represented on the active grid"));
        return false;
    }
    if (*begin_row == *end_row) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                     "SV recipe draft range must have a nonzero width"));
        return false;
    }
    const auto first_row = std::min(*begin_row, *end_row);
    const auto last_row = std::max(*begin_row, *end_row);
    const auto begin_position = index.position_at(first_row);
    const auto end_position = index.position_at(last_row, measure_chart_end_policy_t::allow);
    if (!begin_position || !end_position) {
        diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                     "SV recipe draft range exceeds the chart grid"));
        return false;
    }
    draft.candidate.range = {*begin_position, *end_position};
    return true;
}

auto preview_sv_recipe_draft(const document_history_t& history, const proposal_t<sv_recipe_draft_t>& pending,
    std::span<const diagnostic_t> recent_diagnostics) -> sv_recipe_draft_preview_t {
    const auto& draft = pending.payload;
    sv_recipe_draft_preview_t preview{.effective_output = {.output = draft.candidate.output}};
    auto target = pending.inspect(history, recent_diagnostics);
    if (!target) {
        preview.diagnostics = std::move(target.error());
        return preview;
    }
    const auto* chart = *target;

    const auto candidate = draft.preview_candidate();
    const auto prepared = sv_recipe_prepared_t::from(*chart, candidate);
    preview.candidate_points = sv_recipe_sample_result_t::from(
        {.value = prepared.value.samples(), .diagnostics = prepared.diagnostics});
    if (!preview.candidate_points) {
        preview.diagnostics = preview.candidate_points.diagnostics;
        return preview;
    }
    preview.effective_output = sv_recipe_compilation_t::from(
        compile_sv_recipes_impl(*chart, candidate.output, &candidate, draft.existing_recipe,
                               &prepared.value));
    preview.diagnostics = preview.effective_output.diagnostics;
    return preview;
}

auto sv_recipe_draft_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    const proposal_application_input_t&) -> diagnosed_t<std::optional<std::string>> {
    auto& draft = *this;
    const auto& stamp = scope.captured_stamp();
    auto candidate = draft.candidate;
    candidate.id = draft.existing_recipe.value_or(recipe_id_t{});
    const sv_recipe_edit_action_t action =
        draft.existing_recipe
            ? sv_recipe_edit_action_t{wormedit::update_edit_t<wormedit::sv_recipe_t>{
                  .chart = stamp.chart,
                  .id = *draft.existing_recipe,
                  .value = std::move(candidate)}}
            : sv_recipe_edit_action_t{wormedit::insert_edit_t<wormedit::sv_recipe_t>{
                  .chart = stamp.chart, .value = std::move(candidate)}};
    return sv_recipe_edit_t{.action = action}.apply(scope);
}

namespace {

auto active_recipe_ranges(const chart_document_t& chart, sv_recipe_output_t output,
                          const measure_index_t& index)
    -> std::vector<std::pair<std::uint64_t, std::uint64_t>> {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    for (const auto& recipe : chart.sv_recipes) {
        if (recipe.enabled && recipe.output == output) {
            if (const auto range = recipe_range(chart, recipe, index)) {
                ranges.push_back(*range);
            }
        }
    }
    return ranges;
}

auto is_row_in_ranges(std::uint64_t row,
                   const std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges) -> bool {
    return std::ranges::any_of(
        ranges, [row](const auto& range) { return row >= range.first && row <= range.second; });
}

auto allocate_transient_id(std::uint64_t& next_id) -> std::optional<object_id_t> {
    if (next_id == 0) {
        return std::nullopt;
    }
    const auto allocated = object_id_t{next_id};
    next_id = next_id == std::numeric_limits<std::uint64_t>::max() ? 0 : next_id + 1;
    return allocated;
}

auto compiled_points_for_export(chart_document_t& chart, sv_recipe_output_t output,
                                diagnostic_list_t& diagnostics)
    -> std::optional<std::vector<sv_recipe_point_t>> {
    const auto compiled = sv_recipe_compiled_output_t::from(chart, output);
    if (!compiled.diagnostics.empty()) {
        diagnostics.insert(diagnostics.end(), compiled.diagnostics.begin(),
                           compiled.diagnostics.end());
        return std::nullopt;
    }
    return compiled.value.points;
}

auto bake_scroll_events(chart_document_t& chart, const measure_index_t& index,
                        const std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges,
                        std::uint64_t& next_id, diagnostic_list_t& diagnostics) -> bool {
    if (ranges.empty()) {
        return true;
    }
    const auto points = compiled_points_for_export(chart, sv_recipe_output_t::sv, diagnostics);
    if (!points.has_value()) {
        return false;
    }
    std::erase_if(chart.scrolls, [&](const auto& event) {
        const auto row = index.row_of(event.position);
        return row && is_row_in_ranges(*row, ranges);
    });
    for (std::size_t order = 0; order < points->size(); ++order) {
        const auto& point = (*points)[order];
        const auto value = export_rational(point.value);
        const auto id = allocate_transient_id(next_id);
        if (!value || !id) {
            reject(diagnostics, diagnostic_code_t::invalid_rational,
                   "SV export bake cannot represent a sampled scroll value");
            return false;
        }
        chart.scrolls.push_back({.id = *id,
                                 .position = point.position,
                                 .multiplier = *value,
                                 .provenance = {.family = format_family_t::native_project,
                                                .profile = "sv-recipe-export",
                                                .source_field = "SCROLLS",
                                                .source_order = order}});
    }
    return true;
}

auto bake_speed_events(chart_document_t& chart, const measure_index_t& index,
                       const std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges,
                       std::uint64_t& next_id, diagnostic_list_t& diagnostics) -> bool {
    if (ranges.empty()) {
        return true;
    }
    const auto points = compiled_points_for_export(chart, sv_recipe_output_t::speeds, diagnostics);
    if (!points.has_value()) {
        return false;
    }
    std::erase_if(chart.speeds, [&](const auto& event) {
        const auto row = index.row_of(event.position);
        return row && is_row_in_ranges(*row, ranges);
    });
    for (std::size_t order = 0; order < points->size(); ++order) {
        const auto& point = (*points)[order];
        const auto value = export_rational(point.value);
        const auto id = allocate_transient_id(next_id);
        if (!value || !id) {
            reject(diagnostics, diagnostic_code_t::invalid_rational,
                   "SV export bake cannot represent a sampled speed value");
            return false;
        }
        chart.speeds.push_back({.id = *id,
                                .position = point.position,
                                .multiplier = *value,
                                .transition_beats = point.transition_duration,
                                .transition_is_seconds = point.transition_is_seconds,
                                .provenance = {.family = format_family_t::native_project,
                                               .profile = "sv-recipe-export",
                                               .source_field = "SPEEDS",
                                               .source_order = order}});
    }
    return true;
}

} // namespace

auto bake_sv_recipes_for_export(const chart_document_t& chart) -> sv_recipe_export_bake_outcome_t {
    sv_recipe_export_bake_outcome_t result{};
    auto baked = chart;
    if (std::ranges::none_of(baked.sv_recipes, &sv_recipe_t::enabled)) {
        baked.sv_recipes.clear();
        result.value = std::move(baked);
        return result;
    }
    for (const auto& recipe : baked.sv_recipes) {
        if (recipe.enabled) {
            const auto diagnostics = validate_sv_recipe(baked, recipe);
            result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
        }
    }
    if (!result.diagnostics.empty()) {
        return result;
    }
    const auto built_index = measure_index_t::from(baked);
    if (!built_index) {
        result.diagnostics = built_index.error();
        return result;
    }
    const auto& index = *built_index;
    const auto first_id = next_chart_object_id(baked, std::numeric_limits<std::size_t>::max()).id;
    if (!first_id) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV export bake cannot allocate transient event IDs");
        return result;
    }
    auto next_id = first_id->value;
    const auto scroll_ranges = active_recipe_ranges(baked, sv_recipe_output_t::sv, index);
    const auto speed_ranges = active_recipe_ranges(baked, sv_recipe_output_t::speeds, index);
    if (!bake_scroll_events(baked, index, scroll_ranges, next_id, result.diagnostics) ||
        !bake_speed_events(baked, index, speed_ranges, next_id, result.diagnostics)) {
        return result;
    }
    baked.sv_recipes.clear();
    result.value = std::move(baked);
    return result;
}

auto sv_recipe_range_drag_t::propose(const document_history_t& history, chart_id_t chart_id,
    recipe_id_t recipe_id, sv_recipe_range_handle_t handle, local_position_t pointer_position,
    snap_grid_t snap, bool timing_locked)
    -> proposal_outcome_t<sv_recipe_range_drag_t> {
    proposal_outcome_t<sv_recipe_range_drag_t> outcome;
    proposal_t<sv_recipe_range_drag_t> pending{
        .stamp = chart_stamp_t::from(history, chart_id),
        .payload = {.recipe = recipe_id, .handle = handle,
                    .snap = snap}};
    auto& drag = pending.payload;
    if (timing_locked) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
            "SV recipe range dragging is unavailable while timing is locked"));
        return outcome;
    }
    const auto* chart = history.read_chart(chart_id);
    if (!chart || !recipe_id) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "SV recipe range drag refers to an unknown chart or recipe"));
        return outcome;
    }
    const auto* recipe = recipe_by_id(*chart, recipe_id);
    if (!recipe || std::ranges::count(chart->sv_recipes, recipe_id, &sv_recipe_t::id) != 1) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "SV recipe range drag refers to a missing or ambiguous recipe"));
        return outcome;
    }
    auto built_index = measure_index_t::from(*chart);
    if (!built_index) {
        outcome.diagnostics = std::move(built_index.error());
        return outcome;
    }
    const auto& index = *built_index;
    const auto snapped = snap_span_position(*chart, index, pointer_position, snap);
    if (!snapped) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
            "SV recipe range drag pointer cannot be represented on the active grid"));
        return outcome;
    }
    drag.original_range = recipe->range;
    drag.preview_range = recipe->range;
    drag.snapped_pointer_anchor = *snapped;
    outcome.value = proposal_t<sv_recipe_range_drag_t>::from(pending.stamp, std::move(drag));
    return outcome;
}

namespace {
auto prepare_sv_recipe_range(const chart_document_t& chart, const sv_recipe_t& recipe,
    const measure_index_t& index, const sv_recipe_range_drag_t& drag,
    local_position_t pointer_position) -> diagnosed_t<std::optional<position_span_t>> {
    diagnosed_t<std::optional<position_span_t>> outcome;
    const auto snapped = snap_span_position(chart, index, pointer_position, drag.snap);
    const auto original_begin = index.row_of(drag.original_range.begin);
    const auto original_end = index.row_of(drag.original_range.end, measure_chart_end_policy_t::allow);
    const auto anchor = index.row_of(drag.snapped_pointer_anchor, measure_chart_end_policy_t::allow);
    const auto target = snapped
        ? index.row_of(*snapped, measure_chart_end_policy_t::allow) : std::nullopt;
    if (!snapped || !original_begin || !original_end || !anchor || !target) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
            "SV recipe range drag is outside the chart grid"));
        return outcome;
    }
    const auto handle = drag.handle == sv_recipe_range_handle_t::begin ? span_handle_t::begin
                        : drag.handle == sv_recipe_range_handle_t::end ? span_handle_t::end
                                                                      : span_handle_t::body;
    const auto manipulated = manipulate_span({*original_begin, *original_end}, handle, *anchor,
                                             *target, index.total_rows);
    if (!manipulated) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
            "SV recipe range drag cannot produce a valid interval"));
        return outcome;
    }
    const auto begin_position = index.position_at(manipulated->begin);
    const auto end_position = index.position_at(manipulated->end, measure_chart_end_policy_t::allow);
    if (!begin_position || !end_position) {
        outcome.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
            "SV recipe range drag exceeds the chart grid"));
        return outcome;
    }
    auto replacement = recipe;
    replacement.range = {*begin_position, *end_position};
    outcome.diagnostics = validate_sv_recipe(chart, replacement);
    if (outcome.diagnostics.empty()) outcome.value = replacement.range;
    return outcome;
}
} // namespace

auto sv_recipe_range_drag_t::validate_target(const song_document_t& document, chart_id_t selected,
    proposal_operation_t operation) const -> result_t<const chart_document_t*, diagnostic_t> {
    // The numeric editor owns Apply target validation after the captured no-op check.
    if (operation == proposal_operation_t::apply) return nullptr;
    const auto* chart = document.read_chart(selected);
    if (!chart)
        return std::unexpected(error(diagnostic_code_t::invalid_id, std::string(invalid_target_message)));
    return chart;
}

auto sv_recipe_range_drag_t::is_unchanged() const noexcept -> bool {
    return preview_range == original_range;
}

auto sv_recipe_range_drag_t::revise(proposal_revision_scope_t<chart_stamp_t>& scope,
    local_position_t pointer_position) -> bool {
    auto& diagnostics = scope.diagnostics();
    const auto* chart = scope.target();
    const auto* target_recipe = recipe_by_id(*chart, recipe);
    if (!target_recipe) {
        scope.invalidate_target();
        diagnostics.push_back(error(diagnostic_code_t::invalid_id, std::string(invalid_target_message)));
        return false;
    }
    auto built_index = measure_index_t::from(*chart);
    if (!built_index) {
        diagnostics = std::move(built_index.error());
        scope.invalidate_target();
        return false;
    }
    auto prepared = prepare_sv_recipe_range(*chart, *target_recipe,
        *built_index, *this, pointer_position);
    diagnostics = std::move(prepared.diagnostics);
    if (!prepared.value) return false;
    preview_range = *prepared.value;
    return true;
}

auto sv_recipe_range_drag_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    const proposal_application_input_t&) -> diagnosed_t<std::optional<std::string>> {
    const sv_recipe_edit_action_t action =
        move_sv_recipe_range_action_t{scope.captured_stamp().chart, recipe, preview_range};
    return sv_recipe_edit_t{.action = action}.apply(scope);
}

namespace {

struct sv_recipe_edit_context_t {
    document_command_transaction_t& transaction;
    std::string label;
    bool changed{};

    template <typename Edit> void operator()(const Edit& edit);

};

auto editable_recipe_chart(sv_recipe_edit_context_t& context, chart_id_t id)
    -> const chart_document_t* {
    const auto chart = context.transaction.read_chart(id);
    if (!id || chart == nullptr) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe edit refers to an unknown chart");
        return nullptr;
    }
    if (chart->sv_recipes.size() > maximum_sv_recipes_per_chart) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the SV recipe cap");
        return nullptr;
    }
    return &*chart;
}

auto editable_recipe(sv_recipe_edit_context_t& context, const chart_document_t& chart,
                     recipe_id_t id) -> const sv_recipe_t* {
    if (!id) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe edit uses the reserved zero ID");
        return nullptr;
    }
    auto* recipe = recipe_by_id(chart, id);
    if (recipe == nullptr ||
        std::ranges::count(chart.sv_recipes, id, &sv_recipe_t::id) != 1) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe edit refers to a missing or ambiguous recipe");
        return nullptr;
    }
    return recipe;
}

auto append_recipe_validation(document_command_result_t& context,
                              const chart_document_t& chart,
                              const sv_recipe_t& recipe) -> bool {
    auto validation = validate_sv_recipe(chart, recipe);
    if (validation.empty())
        return true;
    context.diagnostics.insert(context.diagnostics.end(), validation.begin(),
                                      validation.end());
    return false;
}

void apply_recipe_operation(sv_recipe_edit_context_t& context,
                            const wormedit::insert_edit_t<wormedit::sv_recipe_t>& edit) {
    const auto* chart = editable_recipe_chart(context, edit.chart);
    if (!chart) return;
    if (chart->sv_recipes.size() >= maximum_sv_recipes_per_chart) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_resolution,
               "SV recipe insertion would exceed the per-chart cap");
        return;
    }
    auto recipe = edit.value;
    if (!recipe.id) {
        const auto id = context.transaction.next_recipe_id();
        if (!id) {
            reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_id,
                   "SV recipe ID sequence is exhausted");
            return;
        }
        recipe.id = *id;
    } else if (context.transaction.current_document().find_sv_recipe_definition(recipe.id) ||
               id_counter_t<recipe_id_t>::from(context.transaction.current_document(), recipe.id, 1).count != 0) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::duplicate_id,
               "SV recipe ID is already used or reserved by a shared definition");
        return;
    }
    if (!append_recipe_validation(context.transaction.outcome(), *chart, recipe)) return;
    auto* target = context.transaction.document().find_chart(edit.chart);
    target->sv_recipes.push_back(std::move(recipe));
    context.label = "Insert SV recipe";
    context.changed = true;
}

void apply_recipe_operation(sv_recipe_edit_context_t& context, chart_document_t& chart,
                            const duplicate_sv_recipe_action_t& edit) {
    const auto* source = recipe_by_id(chart, edit.id);
    const auto index = static_cast<std::size_t>(source - chart.sv_recipes.data());
    auto copy = *source;
    copy.id = {};
    copy.definition = {};
    apply_recipe_operation(context, insert_edit_t<sv_recipe_t>{.chart = edit.chart, .value = copy});
    if (!context.changed) return;
    auto& recipes = chart.sv_recipes;
    // Insert beside the source while retaining all existing recipes' relative order.
    std::rotate(recipes.begin() + static_cast<std::ptrdiff_t>(index + 1),
                recipes.end() - 1, recipes.end());
    context.label = "Duplicate SV recipe";
}

void apply_recipe_operation(sv_recipe_edit_context_t& context,
                            const wormedit::update_edit_t<wormedit::sv_recipe_t>& edit) {
    const auto* chart = editable_recipe_chart(context, edit.chart);
    if (!chart) return;
    const auto* recipe = editable_recipe(context, *chart, edit.id);
    if (!recipe) return;
    auto replacement = edit.value;
    replacement.id = edit.id;
    if (!append_recipe_validation(context.transaction.outcome(), *chart, replacement)) return;
    context.changed = *recipe != replacement;
    auto* target = context.transaction.document().find_chart(edit.chart);
    if (context.changed) {
        *recipe_by_id(*target, edit.id) = std::move(replacement);
        context.label = "Update SV recipe";
    }
}

void apply_recipe_operation(sv_recipe_edit_context_t& context, chart_document_t& chart,
                            const wormedit::delete_edit_t<wormedit::sv_recipe_t>& edit) {
    if (!recipe_by_id(chart, edit.id))
        return;
    std::erase_if(chart.sv_recipes, [&](const auto& item) { return item.id == edit.id; });
    context.label = "Delete SV recipe";
    context.changed = true;
}

void apply_recipe_operation(sv_recipe_edit_context_t& context,
                            chart_document_t& chart,
                            const reorder_sv_recipe_action_t& edit) {
    auto* recipe = recipe_by_id(chart, edit.id);
    if (!recipe)
        return;
    if (edit.new_index >= chart.sv_recipes.size()) {
        reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_resolution,
               "SV recipe reorder index is outside the recipe list");
        return;
    }
    const auto old_index = static_cast<std::size_t>(recipe - chart.sv_recipes.data());
    if (old_index == edit.new_index)
        return;
    auto moved = std::move(chart.sv_recipes[old_index]);
    chart.sv_recipes.erase(chart.sv_recipes.begin() + static_cast<std::ptrdiff_t>(old_index));
    chart.sv_recipes.insert(chart.sv_recipes.begin() +
                                static_cast<std::ptrdiff_t>(edit.new_index),
                            std::move(moved));
    context.label = "Reorder SV recipes";
    context.changed = true;
}

void apply_recipe_operation(sv_recipe_edit_context_t& context,
                            chart_document_t& chart,
                            const set_sv_recipe_enabled_action_t& edit) {
    auto* recipe = recipe_by_id(chart, edit.id);
    if (!recipe)
        return;
    context.changed = recipe->enabled != edit.enabled;
    if (context.changed) {
        recipe->enabled = edit.enabled;
        context.label = edit.enabled ? "Enable SV recipe" : "Disable SV recipe";
    }
}

void apply_recipe_operation(sv_recipe_edit_context_t& context,
                            const move_sv_recipe_range_action_t& edit) {
    const auto* chart = editable_recipe_chart(context, edit.chart);
    if (!chart) return;
    const auto* recipe = editable_recipe(context, *chart, edit.id);
    if (!recipe) return;
    auto replacement = *recipe;
    replacement.range = edit.range;
    if (!append_recipe_validation(context.transaction.outcome(), *chart, replacement)) return;
    context.changed = replacement.range != recipe->range;
    auto* target = context.transaction.document().find_chart(edit.chart);
    if (context.changed) {
        recipe_by_id(*target, edit.id)->range = edit.range;
        context.label = "Move SV recipe range";
    }
}

// Marks events that came from baking a recipe so they read as materialized, and moves their source
// order after the chart's existing events so ordering at one row stays deterministic.
template <typename Event>
void mark_materialized_events(std::vector<Event>& events, std::size_t first_order) {
    for (auto& event : events) {
        if (event.provenance.profile != "sv-recipe-export") continue;
        event.provenance.profile = "sv-recipe-materialized";
        event.provenance.source_order += first_order;
    }
}

template <typename Event>
auto next_event_source_order(const std::vector<Event>& events) -> std::size_t {
    std::size_t next = 0;
    for (const auto& event : events) next = std::max(next, event.provenance.source_order + 1);
    return next;
}

void apply_recipe_operation(sv_recipe_edit_context_t& context,
                            chart_document_t& chart,
                            const materialize_sv_recipe_action_t& edit) {
    const auto* recipe = recipe_by_id(chart, edit.id);
    if (!recipe)
        return;
    // Bake a detached copy holding only this recipe, then adopt its events.
    auto scratch = chart;
    scratch.sv_recipes = {*recipe};
    scratch.sv_recipes.front().enabled = true;
    auto baked = bake_sv_recipes_for_export(scratch);
    if (!baked.value || !baked.diagnostics.empty()) {
        context.transaction.outcome().diagnostics.insert(context.transaction.outcome().diagnostics.end(),
                                          baked.diagnostics.begin(), baked.diagnostics.end());
        if (context.transaction.outcome().diagnostics.empty())
            reject(context.transaction.outcome().diagnostics, diagnostic_code_t::invalid_id,
                   "SV recipe could not be materialized");
        return;
    }
    mark_materialized_events(baked.value->scrolls, next_event_source_order(chart.scrolls));
    mark_materialized_events(baked.value->speeds, next_event_source_order(chart.speeds));
    chart.scrolls = std::move(baked.value->scrolls);
    chart.speeds = std::move(baked.value->speeds);
    std::erase_if(chart.sv_recipes, [&](const auto& item) { return item.id == edit.id; });
    context.label = "Materialize SV recipe";
    context.changed = true;
}

template <typename Edit>
void apply_recipe_operation(sv_recipe_edit_context_t& context, const Edit& edit) {
    const auto* source = editable_recipe_chart(context, edit.chart);
    if (!source || !editable_recipe(context, *source, edit.id)) return;
    auto* chart = context.transaction.document().find_chart(edit.chart);
    apply_recipe_operation(context, *chart, edit);
}

template <typename Edit>
void sv_recipe_edit_context_t::operator()(const Edit& edit) {
    apply_recipe_operation(*this, edit);
}


struct scoped_recipe_editor_t {
    document_command_transaction_t& transaction;

    auto change(chart_id_t chart, std::optional<object_id_t> existing, sv_recipe_t value)
        -> attempt_t<document_edit_effect_t> {
        const auto next = transaction.next_bms_program_item_id();
        value.id = recipe_id_t{existing ? existing->value : next.value_or(object_id_t{}).value};
        return transaction.stage_bms_native_edit(chart, existing,
            [&](chart_document_t& target) -> attempt_t<bms_native_edit_effect_t> {
                if (!existing && target.sv_recipes.size() >= maximum_sv_recipes_per_chart)
                    return {.diagnostics = {error(diagnostic_code_t::invalid_resolution, "Selected chart exceeds the SV recipe cap.")}};
                auto diagnostics = validate_sv_recipe(target, value);
                if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
                attempt_t<bms_native_edit_effect_t> result{};
                auto& edited = result.value.emplace();
                edited.effect.changed = true;
                edited.effect.command_label = "Edit SV recipe";
                edited.event = value;
                return result;
            });
    }
    auto operator()(const insert_edit_t<sv_recipe_t>& edit) -> attempt_t<document_edit_effect_t> {
        return change(edit.chart, {}, edit.value);
    }
    auto operator()(const update_edit_t<sv_recipe_t>& edit) -> attempt_t<document_edit_effect_t> {
        return change(edit.chart, object_id_t{edit.id.value}, edit.value);
    }
    auto operator()(const delete_edit_t<sv_recipe_t>& edit) -> attempt_t<document_edit_effect_t> {
        return transaction.stage_bms_native_edit(edit.chart, object_id_t{edit.id.value},
            [](chart_document_t&) -> attempt_t<bms_native_edit_effect_t> {
                return {.value = bms_native_edit_effect_t{.effect = {.changed = true,
                    .command_label = "Delete SV recipe"}}};
            });
    }
    template<class Edit> auto operator()(const Edit& edit) -> attempt_t<document_edit_effect_t> {
        if constexpr (std::same_as<Edit, move_sv_recipe_range_action_t> ||
            std::same_as<Edit, set_sv_recipe_enabled_action_t>) {
            const auto* chart = transaction.read_chart(edit.chart);
            const auto* source = chart ? recipe_by_id(*chart, edit.id) : nullptr;
            if (!source) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped recipe is unavailable.")}};
            auto value = *source;
            if constexpr (std::same_as<Edit, move_sv_recipe_range_action_t>) value.range = edit.range;
            else value.enabled = edit.enabled;
            return change(edit.chart, object_id_t{edit.id.value}, std::move(value));
        } else return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
            "Scoped recipe reorder, duplication and materialization require an explicit semantic operation.")}};
    }
};

auto sv_recipe_edit_t::apply(document_edit_scope_t& scope) const
    -> diagnosed_t<std::optional<std::string>> {
    auto& transaction = scope.transaction();
    auto& result = transaction.outcome();
    const auto chart = std::visit([](const auto& edit) { return edit.chart; }, action);
    if (transaction.has_bms_program(chart)) {
        auto staged = std::visit(scoped_recipe_editor_t{transaction}, action);
        if (!staged.value || !staged.ok()) return {.diagnostics = std::move(staged.diagnostics)};
        if (!staged.value->changed) return {};
        return {.value = std::move(staged.value->command_label)};
    }
    sv_recipe_edit_context_t context{.transaction = transaction, .label = {}, .changed = false};
    std::visit(context, action);
    if (!result.diagnostics.empty() || !context.changed) return {};
    return {.value = std::move(context.label)};
}

} // namespace

// Temporary: removed by T-637; native_chart_edit_action.h dispatches into existing transactions.
auto apply_sv_recipe_edit(document_command_transaction_t& transaction,
                          const sv_recipe_edit_action_t& action) -> document_command_result_t {
    document_edit_scope_t scope{transaction};
    return apply_proposal_edit(scope, sv_recipe_edit_t{.action = action});
}

auto apply_sv_recipe_edit(document_history_t& history,
                          const sv_recipe_edit_action_t& action) -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    document_edit_scope_t scope{transaction};
    return apply_proposal_edit(scope, sv_recipe_edit_t{.action = action});
}

} // namespace wormedit
