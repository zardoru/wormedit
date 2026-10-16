#include <wormedit/core/rational.h>
#include <wormedit/core/numeric_text.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace wormedit {
namespace {

constexpr auto maximum_u64 = std::numeric_limits<std::uint64_t>::max();
constexpr auto two_to_64 = 18446744073709551616.0L;
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(sizeof(double) == sizeof(std::uint64_t));

auto decimal_exponent(std::string_view text) -> std::optional<int> {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (!text.empty() && text.front() == '-') return std::nullopt;
    }
    if (text.empty() || text.front() == '+') return std::nullopt;
    int exponent{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), exponent);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return exponent;
}

struct exact_decimal_mantissa_t {
    std::uint64_t magnitude{};
    std::int64_t scale{};
};

auto decimal_mantissa(std::string_view text) -> std::optional<exact_decimal_mantissa_t> {
    const auto point = text.find('.');
    const auto fractional = point == std::string_view::npos ? 0 : text.size() - point - 1;
    std::int64_t scale = static_cast<std::int64_t>(fractional);
    // Remove trailing zeros before accumulating, so scaling can cancel them exactly.
    while (!text.empty() && text.back() == '0') {
        text.remove_suffix(1);
        --scale;
    }
    std::uint64_t magnitude{};
    bool digit_seen = scale != static_cast<std::int64_t>(fractional);
    bool point_seen{};
    for (const auto character : text) {
        if (character == '.' && !point_seen) {
            point_seen = true;
            continue;
        }
        if (character < '0' || character > '9') return std::nullopt;
        digit_seen = true;
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (magnitude > (maximum_u64 - digit) / 10) return std::nullopt;
        magnitude = magnitude * 10 + digit;
    }
    return digit_seen ? std::optional{exact_decimal_mantissa_t{magnitude, scale}} : std::nullopt;
}

auto scaled_decimal(exact_decimal_mantissa_t mantissa, bool negative)
    -> std::optional<signed_rational_t> {
    auto magnitude = mantissa.magnitude;
    if (magnitude == 0) return signed_rational_t{0, 1};
    std::uint64_t denominator{1};
    for (; mantissa.scale > 0; --mantissa.scale) {
        const auto common = std::gcd(magnitude, std::uint64_t{10});
        magnitude /= common;
        const auto factor = 10 / common;
        if (denominator > maximum_u64 / factor) return std::nullopt;
        denominator *= factor;
    }
    const auto maximum_signed = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto limit = maximum_signed + static_cast<std::uint64_t>(negative);
    for (; mantissa.scale < 0; ++mantissa.scale) {
        if (magnitude > limit / 10) return std::nullopt;
        magnitude *= 10;
    }
    if (magnitude > limit) return std::nullopt;
    const auto numerator = negative && magnitude == maximum_signed + 1
        ? std::numeric_limits<std::int64_t>::min()
        : static_cast<std::int64_t>(magnitude) * (negative ? -1 : 1);
    return signed_rational_t{numerator, denominator};
}

auto reduce_fraction_parts(std::uint64_t numerator, std::uint64_t denominator) -> rational_t {
    const auto divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
}

auto checked_multiply_add(std::uint64_t coefficient, std::uint64_t multiplicand,
                          std::uint64_t addend, std::uint64_t limit, std::uint64_t& result)
    -> bool {
    if (addend > limit || (multiplicand != 0 && coefficient > (limit - addend) / multiplicand)) {
        return false;
    }
    result = coefficient * multiplicand + addend;
    return true;
}

auto exact_binary_value(double value) -> std::optional<rational_t> {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto encoded_exponent = static_cast<std::uint32_t>((bits >> 52) & 0x7ffU);
    const auto fraction = bits & ((std::uint64_t{1} << 52) - 1);

    std::uint64_t significand{};
    int exponent{};
    if (encoded_exponent == 0) {
        significand = fraction;
        exponent = -1074;
    } else {
        significand = (std::uint64_t{1} << 52) | fraction;
        exponent = static_cast<int>(encoded_exponent) - 1023 - 52;
    }
    if (significand == 0) {
        return std::nullopt;
    }

    const auto trailing_zeroes = std::countr_zero(significand);
    const auto removable = std::min<int>(trailing_zeroes, std::max(0, -exponent));
    significand >>= removable;
    exponent += removable;

    if (exponent >= 0) {
        if (exponent >= 64 || significand > (maximum_u64 >> exponent)) {
            return std::nullopt;
        }
        return rational_t{significand << exponent, 1};
    }

    const auto denominator_shift = -exponent;
    if (denominator_shift >= 64) {
        return std::nullopt;
    }
    return rational_t{significand, std::uint64_t{1} << denominator_shift};
}

auto error_center(double input, rational_t candidate) -> long double {
    const auto input_value = static_cast<long double>(input);
    const auto candidate_value = to_long_double(candidate);
    return std::fabs(input_value - candidate_value);
}

auto error_bound(double input, rational_t candidate) -> long double {
    const auto input_value = static_cast<long double>(input);
    const auto candidate_value = to_long_double(candidate);
    const auto scale = std::fabs(input_value) + std::fabs(candidate_value) + 1.0L;
    const auto rounding_allowance = 4.0L * std::numeric_limits<long double>::epsilon() * scale;
    return error_center(input, candidate) + rounding_allowance;
}

auto candidate_error_less(double input, rational_t left, rational_t right) -> bool {
    const auto left_error = error_center(input, left);
    const auto right_error = error_center(input, right);
    if (left_error != right_error) {
        return left_error < right_error;
    }
    if (left.denominator != right.denominator) {
        return left.denominator < right.denominator;
    }
    return left.numerator < right.numerator;
}

auto make_approximation(double input, rational_t candidate, std::uint32_t iterations,
                        const rationalization_options_t& options) -> rationalization_result_t {
    const auto bound = error_bound(input, candidate);
    if (options.maximum_absolute_error.has_value() && bound > *options.maximum_absolute_error) {
        return std::unexpected(rationalization_error_info_t{
            .code = failure_code_t::capacity_exceeded,
            .absolute_error_bound = bound,
            .iterations = iterations,
        });
    }
    return rationalized_value_t{
        .value = candidate,
        .status = rationalization_phase_t::approximation,
        .absolute_error_bound = bound,
        .iterations = iterations,
    };
}

struct convergent_search_t {
    double input{};
    std::uint64_t maximum_denominator{};
    rational_t best{};
    std::uint64_t numerator_before_previous{};
    std::uint64_t denominator_before_previous{1};
    std::uint64_t numerator_previous{1};
    std::uint64_t denominator_previous{};
    long double remainder{};
    std::uint32_t iterations{};
};

auto consider_candidate(convergent_search_t& search, rational_t candidate) -> void {
    if (candidate_error_less(search.input, candidate, search.best)) {
        search.best = candidate;
    }
}

auto consider_semiconvergent(convergent_search_t& search) -> void {
    auto factor_limit = maximum_u64;
    bool factor_limit_set{};
    if (search.numerator_previous != 0) {
        factor_limit = std::min(factor_limit, (maximum_u64 - search.numerator_before_previous) /
                                                  search.numerator_previous);
        factor_limit_set = true;
    }
    if (search.denominator_previous != 0) {
        factor_limit = std::min(factor_limit,
                                (search.maximum_denominator - search.denominator_before_previous) /
                                    search.denominator_previous);
        factor_limit_set = true;
    }
    if (!factor_limit_set || factor_limit == 0) {
        return;
    }
    std::uint64_t numerator{};
    std::uint64_t denominator{};
    if (checked_multiply_add(factor_limit, search.numerator_previous,
                             search.numerator_before_previous, maximum_u64, numerator) &&
        checked_multiply_add(factor_limit, search.denominator_previous,
                             search.denominator_before_previous, search.maximum_denominator,
                             denominator) &&
        numerator != 0 && denominator != 0) {
        consider_candidate(search, reduce_fraction_parts(numerator, denominator));
    }
}

auto advance_convergent(convergent_search_t& search, long double integral) -> bool {
    const auto coefficient = static_cast<std::uint64_t>(integral);
    std::uint64_t numerator_next{};
    std::uint64_t denominator_next{};
    const auto numerator_fits =
        checked_multiply_add(coefficient, search.numerator_previous,
                             search.numerator_before_previous, maximum_u64, numerator_next);
    const auto denominator_fits = checked_multiply_add(
        coefficient, search.denominator_previous, search.denominator_before_previous,
        search.maximum_denominator, denominator_next);
    if (!numerator_fits || !denominator_fits) {
        consider_semiconvergent(search);
        return false;
    }
    if (numerator_next != 0) {
        consider_candidate(search, reduce_fraction_parts(numerator_next, denominator_next));
    }
    const auto fractional = search.remainder - integral;
    search.numerator_before_previous = search.numerator_previous;
    search.denominator_before_previous = search.denominator_previous;
    search.numerator_previous = numerator_next;
    search.denominator_previous = denominator_next;
    if (!(fractional > 0.0L)) {
        return false;
    }
    search.remainder = 1.0L / fractional;
    return std::isfinite(search.remainder);
}

auto best_bounded_approximation(double input, std::uint64_t maximum_denominator,
                                std::uint32_t iteration_limit)
    -> std::pair<rational_t, std::uint32_t> {
    convergent_search_t search{
        .input = input,
        .maximum_denominator = maximum_denominator,
        .best = {1, maximum_denominator},
        .remainder = static_cast<long double>(input),
    };
    consider_candidate(search, {1, 1});
    for (; search.iterations < iteration_limit; ++search.iterations) {
        const auto integral = std::floor(search.remainder);
        if (!(integral >= 0.0L) || integral >= two_to_64) {
            consider_candidate(search, {maximum_u64, 1});
            break;
        }
        if (!advance_convergent(search, integral)) {
            break;
        }
    }
    return {search.best, std::min(search.iterations + 1, iteration_limit)};
}

} // namespace

auto parse_exact_decimal(std::string_view text, std::optional<std::uint32_t> maximum_exponent) -> std::optional<signed_rational_t> {
    bool negative{};
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto marker = text.find_first_of("eE");
    auto mantissa = decimal_mantissa(text.substr(0, marker));
    if (!mantissa) return std::nullopt;
    if (marker != std::string_view::npos) {
        const auto exponent = decimal_exponent(text.substr(marker + 1));
        if (!exponent || (maximum_exponent &&
            (*exponent < -static_cast<std::int64_t>(*maximum_exponent) ||
             *exponent > static_cast<std::int64_t>(*maximum_exponent)))) return std::nullopt;
        mantissa->scale -= *exponent;
    }
    return scaled_decimal(*mantissa, negative);
}

auto rationalized_value_t::from(double value, const rationalization_options_t& options)
    -> rationalization_result_t {
    if (!std::isfinite(value)) {
        return std::unexpected(rationalization_error_info_t{.code = failure_code_t::invalid_data});
    }
    if (!(value > 0.0)) {
        return std::unexpected(rationalization_error_info_t{.code = failure_code_t::invalid_request});
    }
    if (options.maximum_denominator == 0 || options.iteration_limit == 0 ||
        (options.maximum_absolute_error.has_value() &&
         (!std::isfinite(*options.maximum_absolute_error) ||
          *options.maximum_absolute_error < 0.0L))) {
        return std::unexpected(rationalization_error_info_t{.code = failure_code_t::invalid_request});
    }

    const auto maximum_denominator = options.maximum_denominator;
    if (const auto exact = exact_binary_value(value);
        exact.has_value() && exact->denominator <= maximum_denominator) {
        return rationalized_value_t{
            .value = *exact,
            .status = rationalization_phase_t::exact,
            .absolute_error_bound = 0.0L,
            .iterations = 0,
        };
    }

    constexpr auto maximum_iterations = maximum_rationalization_iterations;
    const auto iteration_limit = std::min(options.iteration_limit, maximum_iterations);
    const auto input_value = static_cast<long double>(value);
    const auto maximum_positive_candidate = rational_t{1, maximum_denominator};
    if (input_value < 1.0L / static_cast<long double>(maximum_denominator)) {
        return make_approximation(value, maximum_positive_candidate, 0, options);
    }

    const auto [best, completed_iterations] =
        best_bounded_approximation(value, maximum_denominator, iteration_limit);
    return make_approximation(value, best, completed_iterations, options);
}

auto filter_snap_denominators(std::uint64_t divisions_per_standard_measure,
                              std::span<const std::uint64_t> ordered_denominators,
                              std::uint64_t requested_active_denominator) -> snap_state_t {
    if (divisions_per_standard_measure == 0) {
        return {.error = failure_code_t::invalid_request};
    }

    snap_state_t state;
    for (const auto denominator : ordered_denominators) {
        if (denominator == 0 || divisions_per_standard_measure % denominator != 0 ||
            std::find(state.supported_denominators.begin(), state.supported_denominators.end(),
                      denominator) != state.supported_denominators.end()) {
            continue;
        }
        state.supported_denominators.push_back(denominator);
    }

    if (state.supported_denominators.empty()) {
        state.supported_denominators.push_back(divisions_per_standard_measure);
        state.inserted_resolution_fallback = true;
    }

    const auto requested_supported =
        std::find(state.supported_denominators.begin(), state.supported_denominators.end(),
                  requested_active_denominator);
    if (requested_supported != state.supported_denominators.end()) {
        state.active_denominator = requested_active_denominator;
    } else {
        state.active_denominator = state.supported_denominators.front();
        state.active_was_fallback = true;
    }
    return state;
}

} // namespace wormedit
