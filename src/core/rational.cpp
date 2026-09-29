#include <wormedit/core/rational.h>

#include <algorithm>
#include <bit>
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

auto normalize(std::uint64_t numerator, std::uint64_t denominator) -> rational_t {
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
    const auto candidate_value = static_cast<long double>(candidate.numerator) /
                                 static_cast<long double>(candidate.denominator);
    return std::fabs(input_value - candidate_value);
}

auto error_bound(double input, rational_t candidate) -> long double {
    const auto input_value = static_cast<long double>(input);
    const auto candidate_value = static_cast<long double>(candidate.numerator) /
                                 static_cast<long double>(candidate.denominator);
    const auto scale = std::fabs(input_value) + std::fabs(candidate_value) + 1.0L;
    const auto rounding_allowance = 4.0L * std::numeric_limits<long double>::epsilon() * scale;
    return error_center(input, candidate) + rounding_allowance;
}

auto better_candidate(double input, rational_t left, rational_t right) -> bool {
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
        return {
            .value = std::nullopt,
            .status = rationalization_status_t::error,
            .error = rationalization_error_t::approximation_exceeds_error_limit,
            .absolute_error_bound = bound,
            .iterations = iterations,
        };
    }
    return {
        .value = candidate,
        .status = rationalization_status_t::approximation,
        .error = rationalization_error_t::none,
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
    if (better_candidate(search.input, candidate, search.best)) {
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
        consider_candidate(search, normalize(numerator, denominator));
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
        consider_candidate(search, normalize(numerator_next, denominator_next));
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

auto rationalize_positive_double(double value, const rationalization_options_t& options)
    -> rationalization_result_t {
    if (!std::isfinite(value)) {
        return {.error = rationalization_error_t::non_finite};
    }
    if (!(value > 0.0)) {
        return {.error = rationalization_error_t::non_positive};
    }
    if (options.maximum_denominator == 0 || options.iteration_limit == 0 ||
        (options.maximum_absolute_error.has_value() &&
         (!std::isfinite(*options.maximum_absolute_error) ||
          *options.maximum_absolute_error < 0.0L))) {
        return {.error = rationalization_error_t::invalid_limits};
    }

    const auto maximum_denominator = options.maximum_denominator;
    if (const auto exact = exact_binary_value(value);
        exact.has_value() && exact->denominator <= maximum_denominator) {
        return {
            .value = exact,
            .status = rationalization_status_t::exact,
            .error = rationalization_error_t::none,
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
        return {.error = snap_state_error_t::zero_resolution};
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
