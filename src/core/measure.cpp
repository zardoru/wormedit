#include <wormedit/core/rational.h>
#include "../core/text_util.h"
#include "../core/checked_math.h"
#include <wormedit/core/measure.h>

#include <charconv>
#include <cctype>
#include <limits>
#include <numeric>
#include <string_view>
#include <system_error>

namespace wormedit {
namespace {

constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

auto trim(std::string_view text) -> std::string_view {
    return text_util::trim(text, text_util::ascii_whitespace);
}

enum class unsigned_parse_error_t {
    none,
    invalid,
    overflow,
};

auto parse_unsigned(std::string_view text, std::uint64_t& value) -> unsigned_parse_error_t
{
    if (text.empty()) {
        return unsigned_parse_error_t::invalid;
    }
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec == std::errc::result_out_of_range) {
        return unsigned_parse_error_t::overflow;
    }
    if (result.ec != std::errc{} || result.ptr != end) {
        return unsigned_parse_error_t::invalid;
    }
    return unsigned_parse_error_t::none;
}

using checked_math::checked_multiply;

auto reduce_fraction_parts(std::uint64_t numerator, std::uint64_t denominator) -> rational_t
{
    return reduce_fraction({numerator, denominator});
}

auto parse_fractional_measure(std::string_view text, std::size_t slash)
    -> measure_parse_result_t {
    if (text.find('/', slash + 1) != std::string_view::npos) {
        return std::unexpected(measure_error_t::invalid_syntax);
    }
    std::uint64_t numerator{};
    std::uint64_t denominator{};
    const auto numerator_error = parse_unsigned(trim(text.substr(0, slash)), numerator);
    const auto denominator_error = parse_unsigned(trim(text.substr(slash + 1)), denominator);
    if (numerator_error == unsigned_parse_error_t::overflow ||
        denominator_error == unsigned_parse_error_t::overflow) {
        return std::unexpected(measure_error_t::overflow);
    }
    if (numerator_error != unsigned_parse_error_t::none ||
        denominator_error != unsigned_parse_error_t::none) {
        return std::unexpected(measure_error_t::invalid_syntax);
    }
    if (denominator == 0) {
        return std::unexpected(measure_error_t::zero_denominator);
    }
    if (numerator == 0) {
        return std::unexpected(measure_error_t::non_positive);
    }
    return reduce_fraction_parts(numerator, denominator);
}

auto parse_decimal_measure(std::string_view text) -> measure_parse_result_t {
    const auto dot = text.find('.');
    if (dot != std::string_view::npos && text.find('.', dot + 1) != std::string_view::npos) {
        return std::unexpected(measure_error_t::invalid_syntax);
    }
    auto integer_text = dot == std::string_view::npos ? text : text.substr(0, dot);
    auto fractional_text =
        dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    if (integer_text.empty() && fractional_text.empty()) {
        return std::unexpected(measure_error_t::invalid_syntax);
    }
    while (!fractional_text.empty() && fractional_text.back() == '0') {
        fractional_text.remove_suffix(1);
    }
    std::uint64_t integer_part{};
    if (!integer_text.empty()) {
        const auto error = parse_unsigned(integer_text, integer_part);
        if (error == unsigned_parse_error_t::overflow) {
            return std::unexpected(measure_error_t::overflow);
        }
        if (error != unsigned_parse_error_t::none) {
            return std::unexpected(measure_error_t::invalid_syntax);
        }
    }
    std::uint64_t denominator = 1;
    for ([[maybe_unused]] const auto digit : fractional_text) {
        if (denominator > maximum / 10) {
            return std::unexpected(measure_error_t::overflow);
        }
        denominator *= 10;
    }
    std::uint64_t fractional_part{};
    if (!fractional_text.empty()) {
        const auto error = parse_unsigned(fractional_text, fractional_part);
        if (error == unsigned_parse_error_t::overflow) {
            return std::unexpected(measure_error_t::overflow);
        }
        if (error != unsigned_parse_error_t::none) {
            return std::unexpected(measure_error_t::invalid_syntax);
        }
    }
    std::uint64_t scaled_integer{};
    if (!checked_multiply(integer_part, denominator, scaled_integer) ||
        fractional_part > maximum - scaled_integer) {
        return std::unexpected(measure_error_t::overflow);
    }
    const auto numerator = scaled_integer + fractional_part;
    if (numerator == 0) {
        return std::unexpected(measure_error_t::non_positive);
    }
    return reduce_fraction_parts(numerator, denominator);
}

} // namespace

auto rational_t::from(std::string_view text) -> measure_parse_result_t
{
    text = trim(text);
    if (text.empty()) {
        return std::unexpected(measure_error_t::empty);
    }
    if (text.front() == '-') {
        return std::unexpected(measure_error_t::non_positive);
    }
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty()) {
            return std::unexpected(measure_error_t::invalid_syntax);
        }
    }

    const auto slash = text.find('/');
    if (slash != std::string_view::npos) {
        return parse_fractional_measure(text, slash);
    }
    return parse_decimal_measure(text);
}

auto parse_measure_length(std::string_view text) -> measure_parse_result_t {
    return rational_t::from(text);
}

auto measure_capacity_t::from(
    std::uint64_t divisions_per_standard_measure,
    const measure_definition_t& measure) -> result_t<measure_capacity_t, measure_error_t>
{
    if (divisions_per_standard_measure == 0) {
        return std::unexpected(measure_error_t::zero_resolution);
    }
    if (measure.length.numerator == 0) {
        return std::unexpected(measure_error_t::non_positive);
    }
    if (measure.length.denominator == 0) {
        return std::unexpected(measure_error_t::zero_denominator);
    }

    const auto length = reduce_fraction_parts(measure.length.numerator, measure.length.denominator);
    if (measure.meter.has_value()) {
        if (measure.meter->denominator == 0) {
            return std::unexpected(measure_error_t::zero_denominator);
        }
        if (measure.meter->numerator == 0) {
            return std::unexpected(measure_error_t::non_positive);
        }
        const auto meter = reduce_fraction_parts(measure.meter->numerator, measure.meter->denominator);
        if (meter != length) {
            return std::unexpected(measure_error_t::meter_mismatch);
        }
    }

    const auto common = std::gcd(divisions_per_standard_measure, length.denominator);
    if (length.denominator / common != 1) {
        return std::unexpected(measure_error_t::non_integral_capacity);
    }

    std::uint64_t capacity{};
    if (!checked_multiply(
            divisions_per_standard_measure / common,
            length.numerator,
            capacity)) {
        return std::unexpected(measure_error_t::overflow);
    }
    return measure_capacity_t{.row_capacity = capacity};
}

auto measure_validation_t::from(result_t<measure_capacity_t, measure_error_t>&& outcome)
    -> measure_validation_t {
    if (outcome) return {.row_capacity = outcome->row_capacity, .error = measure_error_t::none};
    return {.row_capacity = std::nullopt, .error = outcome.error()};
}

auto validate_measure(std::uint64_t divisions_per_standard_measure,
                       const measure_definition_t& measure) -> measure_validation_t {
    return measure_validation_t::from(measure_capacity_t::from(divisions_per_standard_measure, measure));
}

auto bmson_grid_t::from(std::uint64_t divisions_per_standard_measure)
    -> std::optional<bmson_grid_t> {
    if (divisions_per_standard_measure == 0) {
        return std::nullopt;
    }
    const auto common = std::gcd(divisions_per_standard_measure, std::uint64_t{4});
    return bmson_grid_t{
        .resolution = divisions_per_standard_measure / common,
        .pulses_per_editor_row = 4 / common,
    };
}

auto bmson_pulse_for_row(
    const bmson_grid_t& grid,
    std::uint64_t absolute_editor_row) -> std::optional<std::uint64_t>
{
    if (grid.resolution == 0 || grid.pulses_per_editor_row == 0) {
        return std::nullopt;
    }
    std::uint64_t pulse{};
    if (!checked_multiply(absolute_editor_row, grid.pulses_per_editor_row, pulse)) {
        return std::nullopt;
    }
    return pulse;
}

auto suggest_meters(const rational_t& length) -> std::vector<meter_t>
{
    std::vector<meter_t> meters;
    if (length.numerator == 0 || length.denominator == 0) {
        return meters;
    }
    const auto reduced = reduce_fraction_parts(length.numerator, length.denominator);
    for (const std::uint32_t denominator : {4U, 8U, 16U}) {
        std::uint64_t scaled{};
        if (!checked_multiply(reduced.numerator, denominator, scaled)
            || scaled % reduced.denominator != 0) {
            continue;
        }
        const auto numerator = scaled / reduced.denominator;
        if (numerator <= std::numeric_limits<std::uint32_t>::max()) {
            meters.push_back({static_cast<std::uint32_t>(numerator), denominator});
        }
    }
    return meters;
}

} // namespace wormedit
