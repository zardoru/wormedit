#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

constexpr long double maximum_parameter_magnitude = 1'000'000.0L;
constexpr long double maximum_curve_output = 1'000'000'000.0L;
constexpr std::uint64_t maximum_parameter_denominator = 1'000'000'000ULL;

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

void reject(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message) {
    diagnostics.push_back(error(code, std::move(message)));
}

auto rational_number(signed_rational_t value) -> long double {
    if (value.denominator == 0) {
        return std::numeric_limits<long double>::quiet_NaN();
    }
    return static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
}

auto valid_bounded_rational(signed_rational_t value, long double maximum) -> bool {
    if (value.denominator == 0 || value.denominator > maximum_parameter_denominator) {
        return false;
    }
    const auto number = rational_number(value);
    return std::isfinite(number) && std::abs(number) <= maximum;
}

auto position_at_row(const measure_index_t& index, std::uint64_t row, bool allow_chart_end = false)
    -> std::optional<local_position_t> {
    if (allow_chart_end && row == index.total_rows && !index.entries.empty()) {
        const auto& last = index.entries.back();
        return local_position_t{.measure = last.id, .row = last.row_capacity};
    }
    const auto found = std::ranges::find_if(index.entries, [row](const auto& entry) {
        return row >= entry.absolute_begin_row &&
               row - entry.absolute_begin_row < entry.row_capacity;
    });
    if (found == index.entries.end()) {
        return std::nullopt;
    }
    return local_position_t{.measure = found->id, .row = row - found->absolute_begin_row};
}

auto position_row(const measure_index_t& index, local_position_t position,
                  bool allow_chart_end = false) -> std::optional<std::uint64_t> {
    if (const auto row = absolute_row(index, position)) {
        return row;
    }
    if (allow_chart_end && !index.entries.empty()) {
        const auto& last = index.entries.back();
        if (position.measure == last.id && position.row == last.row_capacity) {
            return index.total_rows;
        }
    }
    return std::nullopt;
}

auto snapped_position(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t target, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto absolute = position_row(index, target, true);
    if (!absolute || denominator == 0 || chart.divisions_per_standard_measure % denominator != 0) {
        return std::nullopt;
    }
    const auto step = chart.divisions_per_standard_measure / denominator;
    if (step == 0) {
        return std::nullopt;
    }
    if (*absolute == index.total_rows) {
        return position_at_row(index, index.total_rows, true);
    }
    const auto remainder = *absolute % step;
    auto snapped = *absolute - remainder;
    const auto distance_up = step - remainder;
    if (remainder > distance_up &&
        distance_up <= std::numeric_limits<std::uint64_t>::max() - *absolute) {
        snapped = *absolute + distance_up;
    }
    if (snapped > index.total_rows) {
        snapped = index.total_rows;
    }
    const auto distance_to_snapped =
        snapped >= *absolute ? snapped - *absolute : *absolute - snapped;
    const auto distance_to_chart_end = index.total_rows - *absolute;
    if (distance_to_chart_end < distance_to_snapped) {
        snapped = index.total_rows;
    }
    return position_at_row(index, snapped, true);
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
    const auto begin = position_row(index, recipe.range.begin);
    const auto end = position_row(index, recipe.range.end, true);
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
        return rational_number(values.value_from);
    case sv_recipe_curve_t::linear: {
        const auto from = rational_number(values.value_from);
        return from + (rational_number(values.value_to) - from) * p;
    }
    case sv_recipe_curve_t::exponential: {
        const auto from = rational_number(values.value_from);
        const auto delta = rational_number(values.value_to) - from;
        const auto shape = rational_number(values.shape);
        if (std::abs(shape) < 1.0e-8L) {
            return from + delta * p;
        }
        return from + delta * std::expm1(shape * p) / std::expm1(shape);
    }
    case sv_recipe_curve_t::bump: {
        const auto begin = rational_number(values.bump_start);
        const auto end = begin + rational_number(values.bump_length);
        return p >= begin && p < end ? rational_number(values.bump_x)
                                     : rational_number(values.bump_y);
    }
    }
    return std::numeric_limits<long double>::quiet_NaN();
}

auto valid_curve_result(long double value, sv_recipe_output_t output) -> bool {
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
                         diagnostic_list_t& diagnostics) -> bool {
    const auto value = curve_value(recipe, recipe_position_for_row(range, row));
    if (!valid_curve_result(value, recipe.output)) {
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
            if (const auto row = absolute_row(index, event.position)) {
                ordered.emplace_back(*row, event.provenance.source_order, i,
                                     rational_number(event.multiplier));
            }
        }
    } else {
        ordered.reserve(chart.scrolls.size());
        for (std::size_t i = 0; i < chart.scrolls.size(); ++i) {
            const auto& event = chart.scrolls[i];
            if (const auto row = absolute_row(index, event.position)) {
                ordered.emplace_back(*row, event.provenance.source_order, i,
                                     rational_number(event.multiplier));
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

auto globally_used_recipe_id(const song_document_t& song, recipe_id_t id) -> bool {
    if (std::ranges::any_of(song.generator_recipes,
                            [id](const auto& recipe) { return recipe.id == id; })) {
        return true;
    }
    return std::ranges::any_of(song.charts, [id](const auto& chart) {
        return std::ranges::any_of(chart.sv_recipes,
                                   [id](const auto& recipe) { return recipe.id == id; });
    });
}

auto next_recipe_id(const song_document_t& song) -> std::optional<recipe_id_t> {
    std::uint64_t maximum{};
    for (const auto& recipe : song.generator_recipes) {
        maximum = std::max(maximum, recipe.id.value);
    }
    for (const auto& chart : song.charts) {
        for (const auto& recipe : chart.sv_recipes) {
            maximum = std::max(maximum, recipe.id.value);
        }
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return recipe_id_t{maximum + 1};
}

auto next_chart_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    std::uint64_t maximum{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) {
            maximum = std::max(maximum, value.id.value);
        }
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) {
        scan(channel.events);
    }
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return object_id_t{maximum + 1};
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

auto chart_by_id(const song_document_t& song, chart_id_t id) -> const chart_document_t* {
    const auto found = std::ranges::find(song.charts, id, &chart_document_t::id);
    return found == song.charts.end() ? nullptr : &*found;
}

auto recipe_by_id(const chart_document_t& chart, recipe_id_t id) -> const sv_recipe_t* {
    const auto found = std::ranges::find(chart.sv_recipes, id, &sv_recipe_t::id);
    return found == chart.sv_recipes.end() ? nullptr : &*found;
}

void reject_drag(sv_recipe_range_drag_t& drag, diagnostic_code_t code, std::string message,
                 bool terminal = false) {
    drag.diagnostics.clear();
    reject(drag.diagnostics, code, std::move(message));
    if (terminal) {
        drag.status = sv_recipe_range_drag_status_t::rejected;
    }
}

} // namespace

auto validate_sv_recipe(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (!recipe.id) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe must have a non-zero stable ID");
    }
    if (recipe.name.empty() || recipe.name.size() > 256 ||
        recipe.name.find('\0') != std::string::npos) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe name must contain 1 to 256 non-NUL bytes");
    }
    if (static_cast<unsigned int>(recipe.output) >
            static_cast<unsigned int>(sv_recipe_output_t::speeds) ||
        static_cast<unsigned int>(recipe.curve) >
            static_cast<unsigned int>(sv_recipe_curve_t::bump) ||
        static_cast<unsigned int>(recipe.sampling.mode) >
            static_cast<unsigned int>(sv_recipe_sampling_mode_t::adaptive)) {
        reject(diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe contains an unknown output, curve, or sampling mode");
        return diagnostics;
    }

    const auto index_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(index_result)) {
        diagnostics = std::get<diagnostic_list_t>(index_result);
        return diagnostics;
    }
    const auto& index = std::get<measure_index_t>(index_result);
    const auto range = recipe_range(chart, recipe, index);
    if (!range) {
        reject(diagnostics, diagnostic_code_t::invalid_hold,
               "SV recipe range must be an increasing interval inside the chart grid");
        return diagnostics;
    }
    const auto span = range->second - range->first;
    const auto& params = recipe.parameters;
    const auto valid_value = [&](signed_rational_t value) {
        return valid_bounded_rational(value, maximum_parameter_magnitude);
    };
    if (!valid_value(params.value_from) || !valid_value(params.value_to) ||
        !valid_value(params.shape) || !valid_value(params.bump_start) ||
        !valid_value(params.bump_length) || !valid_value(params.bump_x) ||
        !valid_value(params.bump_y)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "SV recipe parameters require bounded finite rational values");
        return diagnostics;
    }
    if (recipe.curve == sv_recipe_curve_t::exponential &&
        std::abs(rational_number(params.shape)) > 32.0L) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "exponential SV recipe shape must be between -32 and 32");
    }
    if (recipe.curve == sv_recipe_curve_t::bump) {
        const auto begin = rational_number(params.bump_start);
        const auto length = rational_number(params.bump_length);
        if (begin < 0.0L || length <= 0.0L || begin + length > 1.0L) {
            reject(diagnostics, diagnostic_code_t::invalid_rational,
                   "bump region must be a positive subrange of normalized [0, 1]");
        } else {
            const auto begin_offset = offset_for_fraction(span, params.bump_start);
            if (!begin_offset || params.bump_start.numerator < 0 ||
                params.bump_length.numerator < 0) {
                reject(diagnostics, diagnostic_code_t::invalid_rational,
                       "bump boundaries cannot be represented exactly on the chart grid");
            } else {
                const auto end_offset =
                    offset_for_fraction_sum(span, params.bump_start, params.bump_length);
                if (!end_offset) {
                    reject(diagnostics, diagnostic_code_t::invalid_rational,
                           "bump boundaries cannot be represented exactly on the chart grid");
                }
            }
        }
    }
    if (recipe.output == sv_recipe_output_t::speeds &&
        (!recipe.transition_duration.has_value() || !recipe.transition_is_seconds.has_value() ||
         recipe.transition_duration->denominator == 0 ||
         recipe.transition_duration->numerator > maximum_parameter_denominator ||
         recipe.transition_duration->denominator > maximum_parameter_denominator ||
         static_cast<long double>(recipe.transition_duration->numerator) /
                 static_cast<long double>(recipe.transition_duration->denominator) >
             maximum_parameter_magnitude)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "SPEEDS recipe requires a bounded transition duration and beat/time mode");
    } else if (recipe.output != sv_recipe_output_t::speeds &&
               (recipe.transition_duration.has_value() ||
                recipe.transition_is_seconds.has_value())) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "transition settings are only valid for SPEEDS recipes");
    }

    const auto& sampling = recipe.sampling;
    if (sampling.mode == sv_recipe_sampling_mode_t::fixed) {
        if (sampling.fixed_point_count.has_value() == sampling.fixed_step_rows.has_value()) {
            reject(diagnostics, diagnostic_code_t::invalid_resolution,
                   "fixed SV sampling requires exactly one point count or row step");
        } else if (sampling.fixed_point_count.has_value()) {
            const auto count = *sampling.fixed_point_count;
            if (count < 2 || count > maximum_sv_recipe_sample_points ||
                (span % static_cast<std::uint64_t>(count - 1)) != 0) {
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
    } else {
        if (sampling.fixed_point_count.has_value() || sampling.fixed_step_rows.has_value() ||
            sampling.max_points < 2 || sampling.max_points > maximum_sv_recipe_sample_points ||
            !valid_bounded_rational(sampling.tolerance, 1'000.0L) ||
            rational_number(sampling.tolerance) <= 0.0L) {
            reject(diagnostics, diagnostic_code_t::invalid_resolution,
                   "adaptive SV sampling requires tolerance > 0 and a point cap in 2..4096");
        }
    }
    if (sampling.max_points < 2 || sampling.max_points > maximum_sv_recipe_sample_points) {
        reject(diagnostics, diagnostic_code_t::invalid_resolution,
               "SV recipe point cap must be in 2..4096");
    }
    if (recipe.curve == sv_recipe_curve_t::constant &&
        !valid_curve_result(rational_number(params.value_from), recipe.output)) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "constant SV recipe value is invalid for its output family");
    }
    if ((recipe.curve == sv_recipe_curve_t::linear ||
         recipe.curve == sv_recipe_curve_t::exponential) &&
        (!valid_curve_result(rational_number(params.value_from), recipe.output) ||
         !valid_curve_result(rational_number(params.value_to), recipe.output))) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "SV recipe endpoints are invalid for its output family");
    }
    if (recipe.curve == sv_recipe_curve_t::bump &&
        (!valid_curve_result(rational_number(params.bump_x), recipe.output) ||
         !valid_curve_result(rational_number(params.bump_y), recipe.output))) {
        reject(diagnostics, diagnostic_code_t::invalid_rational,
               "bump values are invalid for its output family");
    }
    return diagnostics;
}

auto sample_sv_recipe(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> sv_recipe_sample_result_t {
    sv_recipe_sample_result_t result;
    result.diagnostics = validate_sv_recipe(chart, recipe);
    if (!result.diagnostics.empty()) {
        return result;
    }
    const auto index_result = build_measure_index(chart);
    const auto& index = std::get<measure_index_t>(index_result);
    const auto range = *recipe_range(chart, recipe, index);
    const auto span = range.second - range.first;
    std::map<std::uint64_t, long double> sampled;
    if (recipe.curve == sv_recipe_curve_t::bump) {
        const auto begin = offset_for_fraction(span, recipe.parameters.bump_start);
        const auto end = offset_for_fraction_sum(span, recipe.parameters.bump_start,
                                                 recipe.parameters.bump_length);
        if (!begin || !end ||
            !ensure_sample_point(sampled, range.first + *begin, recipe, range,
                                 result.diagnostics) ||
            !ensure_sample_point(sampled, range.first + *end, recipe, range, result.diagnostics)) {
            return result;
        }
    }

    if (recipe.sampling.mode == sv_recipe_sampling_mode_t::fixed) {
        if (recipe.sampling.fixed_point_count.has_value()) {
            const auto count = static_cast<std::uint64_t>(*recipe.sampling.fixed_point_count);
            const auto step = span / (count - 1);
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto row = range.first + i * step;
                if (!ensure_sample_point(sampled, row, recipe, range, result.diagnostics)) {
                    return result;
                }
            }
        } else {
            const auto step = *recipe.sampling.fixed_step_rows;
            for (auto row = range.first; row < range.second;) {
                if (!ensure_sample_point(sampled, row, recipe, range, result.diagnostics)) {
                    return result;
                }
                if (range.second - row <= step) {
                    break;
                }
                row += step;
            }
            if (!ensure_sample_point(sampled, range.second, recipe, range, result.diagnostics)) {
                return result;
            }
        }
    } else {
        (void)ensure_sample_point(sampled, range.first, recipe, range, result.diagnostics);
        (void)ensure_sample_point(sampled, range.second, recipe, range, result.diagnostics);
        if (!result.diagnostics.empty()) {
            return result;
        }
        const auto tolerance = rational_number(recipe.sampling.tolerance);
        const auto max_points = static_cast<std::size_t>(recipe.sampling.max_points);
        const auto refine = [&](auto&& self, std::uint64_t left, std::uint64_t right,
                                std::size_t depth) -> void {
            if (right - left <= 1 || depth > 64 || !result.diagnostics.empty()) {
                return;
            }
            const auto middle = left + (right - left) / 2;
            const auto actual = curve_value(recipe, recipe_position_for_row(range, middle));
            const auto left_value = sampled.at(left);
            const auto right_value = sampled.at(right);
            const auto ratio =
                static_cast<long double>(middle - left) / static_cast<long double>(right - left);
            const auto expected = left_value + (right_value - left_value) * ratio;
            const auto error_value = std::abs(actual - expected);
            if (!valid_curve_result(actual, recipe.output)) {
                reject(result.diagnostics, diagnostic_code_t::invalid_rational,
                       "adaptive SV recipe evaluates to an invalid value");
                return;
            }
            if (error_value <= tolerance) {
                return;
            }
            if (sampled.size() >= max_points) {
                result.tolerance_met = false;
                return;
            }
            sampled.emplace(middle, actual);
            self(self, left, middle, depth + 1);
            self(self, middle, right, depth + 1);
        };
        std::vector<std::uint64_t> mandatory;
        mandatory.reserve(sampled.size());
        for (const auto& [row, value] : sampled) {
            (void)value;
            mandatory.push_back(row);
        }
        if (mandatory.size() > max_points) {
            reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
                   "adaptive SV point cap is smaller than the required structural points");
            return result;
        }
        for (std::size_t i = 1; i < mandatory.size(); ++i) {
            refine(refine, mandatory[i - 1], mandatory[i], 0);
        }
        if (!result.diagnostics.empty()) {
            return result;
        }
    }
    if (sampled.size() > maximum_sv_recipe_sample_points) {
        reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
               "SV recipe structural and sampled points exceed the point cap");
        return result;
    }

    // The chart boundary is a valid exclusive recipe endpoint even though it
    // is not an event position. Keep the sampler's mathematical endpoint for
    // refinement, then emit the final in-chart row instead of inventing an
    // out-of-chart scroll event.
    if (range.second == index.total_rows) {
        sampled.erase(index.total_rows);
        if (index.total_rows == 0 || !ensure_sample_point(sampled, index.total_rows - 1, recipe,
                                                          range, result.diagnostics)) {
            return result;
        }
    }

    result.points.reserve(sampled.size());
    for (const auto& [row, value] : sampled) {
        const auto position = position_at_row(index, row);
        if (!position) {
            reject(result.diagnostics, diagnostic_code_t::row_out_of_range,
                   "sampled SV recipe point is outside chart geometry");
            result.points.clear();
            return result;
        }
        result.points.push_back(
            {.absolute_row = row,
             .position = *position,
             .value = static_cast<double>(value),
             .transition_duration = recipe.transition_duration.value_or(rational_t{}),
             .transition_is_seconds = recipe.transition_is_seconds.value_or(false)});
    }
    return result;
}

namespace {

auto compile_sv_recipes_impl(const chart_document_t& chart, sv_recipe_output_t output,
                             const sv_recipe_t* candidate,
                             std::optional<recipe_id_t> replacing_recipe)
    -> sv_recipe_compilation_t {
    sv_recipe_compilation_t result{.output = output};
    if (static_cast<unsigned int>(output) > static_cast<unsigned int>(sv_recipe_output_t::speeds)) {
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
    const auto index_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(index_result)) {
        result.diagnostics = std::get<diagnostic_list_t>(index_result);
        return result;
    }
    const auto& index = std::get<measure_index_t>(index_result);
    std::vector<const sv_recipe_t*> recipes;
    std::set<std::uint64_t> rows;
    for (const auto& stored_recipe : chart.sv_recipes) {
        const auto* recipe = &stored_recipe;
        if (replacing_recipe && stored_recipe.id == *replacing_recipe) {
            recipe = candidate;
        }
        if (!recipe->enabled || recipe->output != output) {
            continue;
        }
        const auto sample = sample_sv_recipe(chart, *recipe);
        if (!sample) {
            result.diagnostics.insert(result.diagnostics.end(), sample.diagnostics.begin(),
                                      sample.diagnostics.end());
            continue;
        }
        recipes.push_back(recipe);
        for (const auto& point : sample.points) {
            rows.insert(point.absolute_row);
        }
    }
    if (candidate != nullptr && !replacing_recipe && candidate->enabled &&
        candidate->output == output) {
        const auto sample = sample_sv_recipe(chart, *candidate);
        if (!sample) {
            result.diagnostics.insert(result.diagnostics.end(), sample.diagnostics.begin(),
                                      sample.diagnostics.end());
        } else {
            recipes.push_back(candidate);
            for (const auto& point : sample.points) {
                rows.insert(point.absolute_row);
            }
        }
    }
    if (!result.diagnostics.empty() || recipes.empty()) {
        return result;
    }
    if (output == sv_recipe_output_t::speeds) {
        std::uint64_t last_recipe_end{};
        for (const auto* recipe : recipes) {
            if (const auto range = recipe_range(chart, *recipe, index)) {
                last_recipe_end = std::max(last_recipe_end, range->second);
            }
        }
        const auto potentially_overlapping_transition =
            std::ranges::any_of(chart.speeds, [&](const auto& event) {
                const auto row = absolute_row(index, event.position);
                return event.transition_beats.numerator != 0 && row && *row < last_recipe_end;
            });
        if (potentially_overlapping_transition) {
            reject(result.diagnostics, diagnostic_code_t::invalid_rational,
                   "SV recipe composition over a potentially overlapping authored SPEEDS "
                   "transition is not yet supported");
            return result;
        }
    }

    const auto base_events = base_events_for(chart, output, index);
    for (const auto& [row, value] : base_events) {
        (void)value;
        const auto affects_row = std::ranges::any_of(recipes, [&](const auto* recipe) {
            const auto range = recipe_range(chart, *recipe, index);
            return range && row >= range->first && row < range->second;
        });
        if (affects_row) {
            rows.insert(row);
        }
    }

    result.points.reserve(rows.size());
    for (const auto row : rows) {
        long double value = base_value_at(base_events, row);
        const sv_recipe_t* transition_source{};
        for (const auto* recipe : recipes) {
            const auto range = recipe_range(chart, *recipe, index);
            if (!range || row < range->first || row >= range->second) {
                continue;
            }
            const auto multiplier = curve_value(*recipe, recipe_position_for_row(*range, row));
            value *= multiplier;
            if (output == sv_recipe_output_t::speeds) {
                transition_source = recipe;
            }
        }
        if (!valid_curve_result(value, output)) {
            reject(result.diagnostics, diagnostic_code_t::invalid_rational,
                   "composed SV recipe value is non-finite, oversized, or target-invalid");
            result.points.clear();
            return result;
        }
        const auto position = position_at_row(index, row);
        if (!position) {
            reject(result.diagnostics, diagnostic_code_t::row_out_of_range,
                   "compiled SV point is outside chart geometry");
            result.points.clear();
            return result;
        }
        auto point = sv_recipe_point_t{
            .absolute_row = row, .position = *position, .value = static_cast<double>(value)};
        if (transition_source != nullptr) {
            point.transition_duration = *transition_source->transition_duration;
            point.transition_is_seconds = *transition_source->transition_is_seconds;
        }
        result.points.push_back(std::move(point));
    }
    return result;
}

} // namespace

auto compile_sv_recipes(const chart_document_t& chart, sv_recipe_output_t output)
    -> sv_recipe_compilation_t {
    return compile_sv_recipes_impl(chart, output, nullptr, std::nullopt);
}

namespace {

void reject_draft(sv_recipe_draft_t& draft, diagnostic_code_t code, std::string message,
                  bool terminal = false) {
    draft.diagnostics.clear();
    reject(draft.diagnostics, code, std::move(message));
    if (terminal) {
        draft.status = sv_recipe_draft_status_t::rejected;
    }
}

auto draft_chart_for(const document_history_t& history, const sv_recipe_draft_t& draft)
    -> const chart_document_t* {
    return chart_by_id(history.document, draft.chart);
}

auto draft_is_current(const document_history_t& history, sv_recipe_draft_t& draft,
                      bool timing_locked) -> const chart_document_t* {
    if (draft.status != sv_recipe_draft_status_t::editing) {
        return nullptr;
    }
    if (timing_locked || history.document.revision != draft.document_revision) {
        reject_draft(draft, diagnostic_code_t::invalid_binding,
                     timing_locked ? "SV recipe draft was cancelled by timing lock"
                                   : "SV recipe draft document revision is stale");
        draft.status = sv_recipe_draft_status_t::cancelled;
        return nullptr;
    }
    const auto* chart = draft_chart_for(history, draft);
    if (chart == nullptr) {
        reject_draft(draft, diagnostic_code_t::invalid_id,
                     "SV recipe draft refers to an unknown chart");
        draft.status = sv_recipe_draft_status_t::cancelled;
        return nullptr;
    }
    if (draft.existing_recipe &&
        std::ranges::count(chart->sv_recipes, *draft.existing_recipe, &sv_recipe_t::id) != 1) {
        reject_draft(draft, diagnostic_code_t::invalid_id,
                     "SV recipe draft target is missing or ambiguous");
        draft.status = sv_recipe_draft_status_t::cancelled;
        return nullptr;
    }
    return chart;
}

auto preview_candidate(const sv_recipe_draft_t& draft) -> sv_recipe_t {
    auto candidate = draft.candidate;
    candidate.id = draft.existing_recipe.value_or(recipe_id_t{1});
    return candidate;
}

} // namespace

auto begin_sv_recipe_draft(const document_history_t& history, chart_id_t chart_id,
                           sv_recipe_t candidate, bool timing_locked) -> sv_recipe_draft_t {
    sv_recipe_draft_t draft{.status = sv_recipe_draft_status_t::editing,
                            .chart = chart_id,
                            .candidate = std::move(candidate),
                            .document_revision = history.document.revision};
    if (timing_locked) {
        reject_draft(draft, diagnostic_code_t::invalid_binding,
                     "SV recipe editing is unavailable while timing is locked", true);
        return draft;
    }
    const auto* chart = chart_by_id(history.document, chart_id);
    if (chart == nullptr || !chart_id) {
        reject_draft(draft, diagnostic_code_t::invalid_id,
                     "SV recipe draft refers to an unknown chart", true);
        return draft;
    }
    if (chart->sv_recipes.size() >= maximum_sv_recipes_per_chart) {
        reject_draft(draft, diagnostic_code_t::invalid_resolution,
                     "SV recipe insertion would exceed the per-chart cap", true);
        return draft;
    }
    // Stable IDs are assigned by the insert command only after the draft is
    // accepted. A caller-provided ID must not make a new draft look like an
    // update or reserve a durable ID during preview.
    draft.candidate.id = {};
    return draft;
}

auto begin_existing_sv_recipe_draft(const document_history_t& history, chart_id_t chart_id,
                                    recipe_id_t recipe_id, bool timing_locked)
    -> sv_recipe_draft_t {
    sv_recipe_draft_t draft{.status = sv_recipe_draft_status_t::editing,
                            .chart = chart_id,
                            .existing_recipe = recipe_id,
                            .document_revision = history.document.revision};
    if (timing_locked) {
        reject_draft(draft, diagnostic_code_t::invalid_binding,
                     "SV recipe editing is unavailable while timing is locked", true);
        return draft;
    }
    const auto* chart = chart_by_id(history.document, chart_id);
    if (chart == nullptr || !chart_id || !recipe_id) {
        reject_draft(draft, diagnostic_code_t::invalid_id,
                     "SV recipe draft refers to an unknown chart or recipe", true);
        return draft;
    }
    const auto* recipe = recipe_by_id(*chart, recipe_id);
    if (recipe == nullptr ||
        std::ranges::count(chart->sv_recipes, recipe_id, &sv_recipe_t::id) != 1) {
        reject_draft(draft, diagnostic_code_t::invalid_id,
                     "SV recipe draft target is missing or ambiguous", true);
        return draft;
    }
    draft.candidate = *recipe;
    return draft;
}

auto update_sv_recipe_draft(const document_history_t& history, sv_recipe_draft_t& draft,
                            sv_recipe_t candidate, bool timing_locked) -> bool {
    if (draft_is_current(history, draft, timing_locked) == nullptr) {
        return false;
    }
    candidate.id = draft.existing_recipe.value_or(recipe_id_t{});
    draft.candidate = std::move(candidate);
    draft.diagnostics.clear();
    return true;
}

auto set_sv_recipe_draft_range(const document_history_t& history, sv_recipe_draft_t& draft,
                               local_position_t begin_hit, local_position_t end_hit,
                               std::uint64_t active_snap_denominator, bool timing_locked) -> bool {
    const auto* chart = draft_is_current(history, draft, timing_locked);
    if (chart == nullptr) {
        return false;
    }
    const auto built_index = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(built_index)) {
        draft.diagnostics = std::get<diagnostic_list_t>(built_index);
        return false;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto snapped_begin = snapped_position(*chart, index, begin_hit, active_snap_denominator);
    const auto snapped_end = snapped_position(*chart, index, end_hit, active_snap_denominator);
    const auto begin_row = snapped_begin ? position_row(index, *snapped_begin, true) : std::nullopt;
    const auto end_row = snapped_end ? position_row(index, *snapped_end, true) : std::nullopt;
    if (!begin_row || !end_row) {
        reject_draft(draft, diagnostic_code_t::invalid_resolution,
                     "SV recipe draft range cannot be represented on the active grid");
        return false;
    }
    if (*begin_row == *end_row) {
        reject_draft(draft, diagnostic_code_t::invalid_hold,
                     "SV recipe draft range must have a nonzero width");
        return false;
    }
    const auto first_row = std::min(*begin_row, *end_row);
    const auto last_row = std::max(*begin_row, *end_row);
    const auto begin_position = position_at_row(index, first_row);
    const auto end_position = position_at_row(index, last_row, true);
    if (!begin_position || !end_position) {
        reject_draft(draft, diagnostic_code_t::row_out_of_range,
                     "SV recipe draft range exceeds the chart grid");
        return false;
    }
    draft.candidate.range = {*begin_position, *end_position};
    draft.diagnostics.clear();
    return true;
}

auto preview_sv_recipe_draft(const document_history_t& history, const sv_recipe_draft_t& draft)
    -> sv_recipe_draft_preview_t {
    sv_recipe_draft_preview_t preview{.effective_output = {.output = draft.candidate.output}};
    if (draft.status != sv_recipe_draft_status_t::editing) {
        preview.diagnostics = draft.diagnostics;
        if (preview.diagnostics.empty()) {
            reject(preview.diagnostics, diagnostic_code_t::invalid_binding,
                   "SV recipe draft is not active");
        }
        return preview;
    }
    if (history.document.revision != draft.document_revision) {
        reject(preview.diagnostics, diagnostic_code_t::invalid_binding,
               "SV recipe draft document revision is stale");
        return preview;
    }
    const auto* chart = draft_chart_for(history, draft);
    if (chart == nullptr) {
        reject(preview.diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe draft refers to an unknown chart");
        return preview;
    }
    if (draft.existing_recipe &&
        std::ranges::count(chart->sv_recipes, *draft.existing_recipe, &sv_recipe_t::id) != 1) {
        reject(preview.diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe draft target is missing or ambiguous");
        return preview;
    }

    const auto candidate = preview_candidate(draft);
    preview.candidate_points = sample_sv_recipe(*chart, candidate);
    if (!preview.candidate_points) {
        preview.diagnostics = preview.candidate_points.diagnostics;
        return preview;
    }
    preview.effective_output =
        compile_sv_recipes_impl(*chart, candidate.output, &candidate, draft.existing_recipe);
    preview.diagnostics = preview.effective_output.diagnostics;
    return preview;
}

auto apply_sv_recipe_draft(document_history_t& history, sv_recipe_draft_t& draft,
                           bool timing_locked) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (draft_is_current(history, draft, timing_locked) == nullptr) {
        result.diagnostics = draft.diagnostics;
        return result;
    }
    auto candidate = draft.candidate;
    if (draft.existing_recipe) {
        candidate.id = *draft.existing_recipe;
        result = apply_sv_recipe_edit(
            history, draft.document_revision,
            update_sv_recipe_action_t{draft.chart, *draft.existing_recipe, std::move(candidate)});
    } else {
        candidate.id = {};
        result = apply_sv_recipe_edit(history, draft.document_revision,
                                      insert_sv_recipe_action_t{draft.chart, std::move(candidate)});
    }
    draft.diagnostics = result.diagnostics;
    if (result.diagnostics.empty()) {
        draft.status = sv_recipe_draft_status_t::applied;
    }
    return result;
}

void cancel_sv_recipe_draft(sv_recipe_draft_t& draft) noexcept {
    if (draft.status == sv_recipe_draft_status_t::editing) {
        draft.status = sv_recipe_draft_status_t::cancelled;
        draft.diagnostics.clear();
    }
}

auto bake_sv_recipes_for_export(const chart_document_t& chart) -> sv_recipe_export_bake_result_t {
    sv_recipe_export_bake_result_t result;
    auto baked = chart;
    if (std::ranges::none_of(baked.sv_recipes, [](const auto& recipe) { return recipe.enabled; })) {
        baked.sv_recipes.clear();
        result.chart = std::move(baked);
        return result;
    }
    for (auto& recipe : baked.sv_recipes) {
        if (recipe.output == sv_recipe_output_t::sv) {
            recipe.output = sv_recipe_output_t::scrolls;
        }
    }
    for (const auto& recipe : baked.sv_recipes) {
        if (!recipe.enabled) {
            continue;
        }
        const auto validation = validate_sv_recipe(baked, recipe);
        if (!validation.empty()) {
            result.diagnostics.insert(result.diagnostics.end(), validation.begin(),
                                      validation.end());
        }
    }
    if (!result.diagnostics.empty()) {
        return result;
    }
    const auto built_index = build_measure_index(baked);
    if (!std::holds_alternative<measure_index_t>(built_index)) {
        result.diagnostics = std::get<diagnostic_list_t>(built_index);
        return result;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto first_id = next_chart_object_id(baked);
    if (!first_id) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV export bake cannot allocate transient event IDs");
        return result;
    }
    std::uint64_t next_id = first_id->value;
    const auto allocate_id = [&]() -> std::optional<object_id_t> {
        if (next_id == 0) {
            return std::nullopt;
        }
        const auto allocated = object_id_t{next_id};
        next_id = next_id == std::numeric_limits<std::uint64_t>::max() ? 0 : next_id + 1;
        return allocated;
    };
    const auto active_ranges = [&](sv_recipe_output_t output) {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        for (const auto& recipe : baked.sv_recipes) {
            if (recipe.enabled && recipe.output == output) {
                if (const auto range = recipe_range(baked, recipe, index)) {
                    ranges.push_back(*range);
                }
            }
        }
        return ranges;
    };
    const auto affected = [](std::uint64_t row, const auto& ranges) {
        return std::ranges::any_of(
            ranges, [row](const auto& range) { return row >= range.first && row <= range.second; });
    };

    const auto scroll_ranges = active_ranges(sv_recipe_output_t::scrolls);
    if (!scroll_ranges.empty()) {
        const auto compiled = compile_sv_recipes(baked, sv_recipe_output_t::scrolls);
        if (!compiled) {
            result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(),
                                      compiled.diagnostics.end());
            return result;
        }
        std::erase_if(baked.scrolls, [&](const auto& event) {
            const auto row = absolute_row(index, event.position);
            return row && affected(*row, scroll_ranges);
        });
        for (std::size_t point_index = 0; point_index < compiled.points.size(); ++point_index) {
            const auto value = export_rational(compiled.points[point_index].value);
            const auto event_id = allocate_id();
            if (!value || !event_id) {
                reject(result.diagnostics, diagnostic_code_t::invalid_rational,
                       "SV export bake cannot represent a sampled scroll value");
                return result;
            }
            baked.scrolls.push_back({
                .id = *event_id,
                .position = compiled.points[point_index].position,
                .multiplier = *value,
                .provenance = {.family = format_family_t::native_project,
                               .profile = "sv-recipe-export",
                               .source_field = "SCROLLS",
                               .source_order = point_index},
            });
        }
    }

    const auto speed_ranges = active_ranges(sv_recipe_output_t::speeds);
    if (!speed_ranges.empty()) {
        const auto compiled = compile_sv_recipes(baked, sv_recipe_output_t::speeds);
        if (!compiled) {
            result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(),
                                      compiled.diagnostics.end());
            return result;
        }
        std::erase_if(baked.speeds, [&](const auto& event) {
            const auto row = absolute_row(index, event.position);
            return row && affected(*row, speed_ranges);
        });
        for (std::size_t point_index = 0; point_index < compiled.points.size(); ++point_index) {
            const auto& point = compiled.points[point_index];
            const auto value = export_rational(point.value);
            const auto event_id = allocate_id();
            if (!value || !event_id) {
                reject(result.diagnostics, diagnostic_code_t::invalid_rational,
                       "SV export bake cannot represent a sampled speed value");
                return result;
            }
            baked.speeds.push_back({
                .id = *event_id,
                .position = point.position,
                .multiplier = *value,
                .transition_beats = point.transition_duration,
                .transition_is_seconds = point.transition_is_seconds,
                .provenance = {.family = format_family_t::native_project,
                               .profile = "sv-recipe-export",
                               .source_field = "SPEEDS",
                               .source_order = point_index},
            });
        }
    }
    baked.sv_recipes.clear();
    result.chart = std::move(baked);
    return result;
}

auto begin_sv_recipe_range_drag(const document_history_t& history, chart_id_t chart_id,
                                recipe_id_t recipe_id, sv_recipe_range_handle_t handle,
                                local_position_t pointer_position,
                                std::uint64_t active_snap_denominator, bool timing_locked)
    -> sv_recipe_range_drag_t {
    sv_recipe_range_drag_t drag{.chart = chart_id,
                                .recipe = recipe_id,
                                .handle = handle,
                                .active_snap_denominator = active_snap_denominator,
                                .document_revision = history.document.revision};
    if (timing_locked) {
        reject_drag(drag, diagnostic_code_t::invalid_binding,
                    "SV recipe range dragging is unavailable while timing is locked", true);
        return drag;
    }
    const auto* chart = chart_by_id(history.document, chart_id);
    if (!chart || !recipe_id) {
        reject_drag(drag, diagnostic_code_t::invalid_id,
                    "SV recipe range drag refers to an unknown chart or recipe", true);
        return drag;
    }
    const auto* recipe = recipe_by_id(*chart, recipe_id);
    if (!recipe || std::ranges::count(chart->sv_recipes, recipe_id, &sv_recipe_t::id) != 1) {
        reject_drag(drag, diagnostic_code_t::invalid_id,
                    "SV recipe range drag refers to a missing or ambiguous recipe", true);
        return drag;
    }
    const auto built_index = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(built_index)) {
        drag.diagnostics = std::get<diagnostic_list_t>(built_index);
        drag.status = sv_recipe_range_drag_status_t::rejected;
        return drag;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto snapped = snapped_position(*chart, index, pointer_position, active_snap_denominator);
    if (!snapped) {
        reject_drag(drag, diagnostic_code_t::invalid_resolution,
                    "SV recipe range drag pointer cannot be represented on the active grid", true);
        return drag;
    }
    drag.original_range = recipe->range;
    drag.preview_range = recipe->range;
    drag.snapped_pointer_anchor = *snapped;
    drag.status = sv_recipe_range_drag_status_t::previewing;
    return drag;
}

auto update_sv_recipe_range_drag(const document_history_t& history, sv_recipe_range_drag_t& drag,
                                 local_position_t pointer_position, bool timing_locked) -> bool {
    if (drag.status != sv_recipe_range_drag_status_t::previewing) {
        return false;
    }
    if (timing_locked || history.document.revision != drag.document_revision) {
        reject_drag(drag, diagnostic_code_t::invalid_binding,
                    timing_locked ? "SV recipe range drag was cancelled by timing lock"
                                  : "SV recipe range drag document revision is stale",
                    true);
        return false;
    }
    const auto* chart = chart_by_id(history.document, drag.chart);
    const auto* recipe = chart ? recipe_by_id(*chart, drag.recipe) : nullptr;
    if (!chart || !recipe) {
        reject_drag(drag, diagnostic_code_t::invalid_id,
                    "SV recipe range drag target no longer exists", true);
        return false;
    }
    const auto built_index = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(built_index)) {
        drag.diagnostics = std::get<diagnostic_list_t>(built_index);
        drag.status = sv_recipe_range_drag_status_t::rejected;
        return false;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto snapped =
        snapped_position(*chart, index, pointer_position, drag.active_snap_denominator);
    const auto original_begin = position_row(index, drag.original_range.begin);
    const auto original_end = position_row(index, drag.original_range.end, true);
    const auto anchor = position_row(index, drag.snapped_pointer_anchor, true);
    const auto target = snapped ? position_row(index, *snapped, true) : std::nullopt;
    if (!snapped || !original_begin || !original_end || !anchor || !target) {
        reject_drag(drag, diagnostic_code_t::row_out_of_range,
                    "SV recipe range drag is outside the chart grid");
        return false;
    }

    std::uint64_t begin = *original_begin;
    std::uint64_t end = *original_end;
    if (drag.handle == sv_recipe_range_handle_t::begin) {
        begin = std::min(*target, *original_end);
        end = std::max(*target, *original_end);
    } else if (drag.handle == sv_recipe_range_handle_t::end) {
        begin = std::min(*original_begin, *target);
        end = std::max(*original_begin, *target);
    } else if (*target >= *anchor) {
        const auto delta = *target - *anchor;
        if (delta > std::numeric_limits<std::uint64_t>::max() - end) {
            reject_drag(drag, diagnostic_code_t::row_out_of_range,
                        "SV recipe range drag exceeds the chart grid");
            return false;
        }
        begin += delta;
        end += delta;
    } else {
        const auto delta = *anchor - *target;
        if (delta > begin) {
            reject_drag(drag, diagnostic_code_t::row_out_of_range,
                        "SV recipe range drag exceeds the chart grid");
            return false;
        }
        begin -= delta;
        end -= delta;
    }
    if (begin == end) {
        reject_drag(drag, diagnostic_code_t::invalid_hold,
                    "SV recipe range drag cannot produce a zero-width interval");
        return false;
    }
    const auto begin_position = position_at_row(index, begin);
    const auto end_position = position_at_row(index, end, true);
    if (!begin_position || !end_position) {
        reject_drag(drag, diagnostic_code_t::row_out_of_range,
                    "SV recipe range drag exceeds the chart grid");
        return false;
    }
    auto replacement = *recipe;
    replacement.range = {*begin_position, *end_position};
    const auto validation = validate_sv_recipe(*chart, replacement);
    if (!validation.empty()) {
        drag.diagnostics = validation;
        return false;
    }
    drag.preview_range = replacement.range;
    drag.diagnostics.clear();
    return true;
}

auto commit_sv_recipe_range_drag(document_history_t& history, sv_recipe_range_drag_t& drag,
                                 bool timing_locked) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (drag.status != sv_recipe_range_drag_status_t::previewing) {
        reject(result.diagnostics, diagnostic_code_t::invalid_binding,
               "SV recipe range drag is not active");
        return result;
    }
    if (timing_locked || history.document.revision != drag.document_revision) {
        reject(result.diagnostics, diagnostic_code_t::invalid_binding,
               timing_locked ? "SV recipe range drag was cancelled by timing lock"
                             : "SV recipe range drag document revision is stale");
        drag.diagnostics = result.diagnostics;
        drag.status = sv_recipe_range_drag_status_t::rejected;
        return result;
    }
    if (drag.preview_range == drag.original_range) {
        drag.status = sv_recipe_range_drag_status_t::cancelled;
        return result;
    }
    result = apply_sv_recipe_edit(
        history, drag.document_revision,
        move_sv_recipe_range_action_t{drag.chart, drag.recipe, drag.preview_range});
    drag.diagnostics = result.diagnostics;
    drag.status =
        result ? sv_recipe_range_drag_status_t::committed : sv_recipe_range_drag_status_t::rejected;
    return result;
}

void cancel_sv_recipe_range_drag(sv_recipe_range_drag_t& drag) noexcept {
    if (drag.status == sv_recipe_range_drag_status_t::previewing) {
        drag.status = sv_recipe_range_drag_status_t::cancelled;
        drag.diagnostics.clear();
    }
}

auto apply_sv_recipe_edit(document_history_t& history, std::uint64_t expected_revision,
                          const sv_recipe_edit_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "SV recipe edit document revision is stale");
        return result;
    }

    auto proposed = history.document;
    std::string label;
    bool changed{};
    std::visit(
        [&](const auto& edit) {
            using edit_t = std::remove_cvref_t<decltype(edit)>;
            const auto chart =
                std::ranges::find(proposed.charts, edit.chart, &chart_document_t::id);
            if (!edit.chart || chart == proposed.charts.end()) {
                reject(result.diagnostics, diagnostic_code_t::invalid_id,
                       "SV recipe edit refers to an unknown chart");
                return;
            }
            if (chart->sv_recipes.size() > maximum_sv_recipes_per_chart) {
                reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the SV recipe cap");
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_sv_recipe_action_t>) {
                if (chart->sv_recipes.size() >= maximum_sv_recipes_per_chart) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
                           "SV recipe insertion would exceed the per-chart cap");
                    return;
                }
                auto recipe = edit.recipe;
                if (!recipe.id) {
                    const auto id = next_recipe_id(proposed);
                    if (!id) {
                        reject(result.diagnostics, diagnostic_code_t::invalid_id,
                               "SV recipe ID sequence is exhausted");
                        return;
                    }
                    recipe.id = *id;
                } else if (globally_used_recipe_id(proposed, recipe.id)) {
                    reject(result.diagnostics, diagnostic_code_t::duplicate_id,
                           "SV recipe ID is already used by a generator recipe");
                    return;
                }
                const auto validation = validate_sv_recipe(*chart, recipe);
                if (!validation.empty()) {
                    result.diagnostics.insert(result.diagnostics.end(), validation.begin(),
                                              validation.end());
                    return;
                }
                chart->sv_recipes.push_back(std::move(recipe));
                label = "Insert SV recipe";
                changed = true;
            } else {
                if (!edit.recipe) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_id,
                           "SV recipe edit uses the reserved zero ID");
                    return;
                }
                auto* recipe = recipe_by_id(*chart, edit.recipe);
                if (recipe == nullptr ||
                    std::ranges::count(chart->sv_recipes, edit.recipe, &sv_recipe_t::id) != 1) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_id,
                           "SV recipe edit refers to a missing or ambiguous recipe");
                    return;
                }
                if constexpr (std::is_same_v<edit_t, update_sv_recipe_action_t>) {
                    auto replacement = edit.replacement;
                    replacement.id = edit.recipe;
                    const auto validation = validate_sv_recipe(*chart, replacement);
                    if (!validation.empty()) {
                        result.diagnostics.insert(result.diagnostics.end(), validation.begin(),
                                                  validation.end());
                        return;
                    }
                    changed = *recipe != replacement;
                    if (changed) {
                        *recipe = std::move(replacement);
                        label = "Update SV recipe";
                    }
                } else if constexpr (std::is_same_v<edit_t, delete_sv_recipe_action_t>) {
                    std::erase_if(chart->sv_recipes,
                                  [&](const auto& item) { return item.id == edit.recipe; });
                    label = "Delete SV recipe";
                    changed = true;
                } else if constexpr (std::is_same_v<edit_t, reorder_sv_recipe_action_t>) {
                    if (edit.new_index >= chart->sv_recipes.size()) {
                        reject(result.diagnostics, diagnostic_code_t::invalid_resolution,
                               "SV recipe reorder index is outside the recipe list");
                        return;
                    }
                    const auto old_index =
                        static_cast<std::size_t>(recipe - chart->sv_recipes.data());
                    if (old_index == edit.new_index) {
                        return;
                    }
                    auto moved = std::move(chart->sv_recipes[old_index]);
                    chart->sv_recipes.erase(chart->sv_recipes.begin() +
                                            static_cast<std::ptrdiff_t>(old_index));
                    chart->sv_recipes.insert(chart->sv_recipes.begin() +
                                                 static_cast<std::ptrdiff_t>(edit.new_index),
                                             std::move(moved));
                    label = "Reorder SV recipes";
                    changed = true;
                } else if constexpr (std::is_same_v<edit_t, set_sv_recipe_enabled_action_t>) {
                    changed = recipe->enabled != edit.enabled;
                    if (changed) {
                        recipe->enabled = edit.enabled;
                        label = edit.enabled ? "Enable SV recipe" : "Disable SV recipe";
                    }
                } else {
                    auto replacement = *recipe;
                    replacement.range = edit.range;
                    const auto validation = validate_sv_recipe(*chart, replacement);
                    if (!validation.empty()) {
                        result.diagnostics.insert(result.diagnostics.end(), validation.begin(),
                                                  validation.end());
                        return;
                    }
                    changed = replacement.range.begin != recipe->range.begin ||
                              replacement.range.end != recipe->range.end;
                    if (changed) {
                        recipe->range = edit.range;
                        label = "Move SV recipe range";
                    }
                }
            }
        },
        action);

    if (!result.diagnostics.empty() || !changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(label));
}

} // namespace wormedit
