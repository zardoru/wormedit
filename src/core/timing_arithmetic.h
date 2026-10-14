#pragma once

#include <wormedit/core/timing.h>
#include "wide_math.h"
#include "checked_math.h"

#include <array>
#include <limits>
#include <span>
#include <numeric>
#include <optional>
#include <type_traits>

namespace wormedit::timing_arithmetic {

inline constexpr auto positive_limit = static_cast<std::uint64_t>(
    std::numeric_limits<std::int64_t>::max());
inline constexpr auto negative_limit = positive_limit + 1;

using checked_math::magnitude;

inline auto signed_value(std::uint64_t value, bool negative) -> std::optional<std::int64_t> {
    if (value > (negative ? negative_limit : positive_limit)) return std::nullopt;
    if (negative && value == negative_limit) return std::numeric_limits<std::int64_t>::min();
    return negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
}

inline auto try_normalize(signed_rational_t value) -> std::optional<signed_rational_t> {
    if (value.denominator == 0) return std::nullopt;
    const auto divisor = std::gcd(magnitude(value.numerator), value.denominator);
    const auto numerator = signed_value(magnitude(value.numerator) / divisor, value.numerator < 0);
    return signed_rational_t{*numerator, value.denominator / divisor};
}

struct signed_wide_t {
    wide_math::u128_t value;
    bool negative{};
};

inline auto combine(signed_wide_t left, signed_wide_t right) -> signed_wide_t {
    if (left.negative == right.negative)
        return {wide_math::add(left.value, right.value), left.negative};
    if (wide_math::u128_less(left.value, right.value))
        return {wide_math::subtract(right.value, left.value), right.negative};
    return {wide_math::subtract(left.value, right.value), left.negative};
}

inline auto divide_wide(wide_math::u128_t numerator, wide_math::u128_t denominator)
    -> wide_math::divmod_t {
    if (wide_math::can_fit_u64(numerator) && wide_math::can_fit_u64(denominator))
        return {{0, numerator.low / denominator.low}, {0, numerator.low % denominator.low}};
    return wide_math::divide(numerator, denominator);
}

template <class Result = signed_rational_t>
inline auto reduce_sum(signed_wide_t sum, std::uint64_t common,
                       std::uint64_t left_denominator, std::uint64_t right_denominator)
    -> std::optional<Result> {
    static_assert(std::is_same_v<Result, signed_rational_t> || std::is_same_v<Result, rational_t>);
    const auto remainder = divide_wide(sum.value, {0, common}).remainder.low;
    const auto divisor = std::gcd(common, remainder);
    const auto numerator = divide_wide(sum.value, {0, divisor}).quotient;
    const auto denominator = wide_math::multiply(left_denominator / common,
                                                 right_denominator / divisor);
    if (!wide_math::can_fit_u64(numerator) || !wide_math::can_fit_u64(denominator))
        return std::nullopt;
    if constexpr (std::is_same_v<Result, rational_t>) {
        if (sum.negative && numerator.low != 0) return std::nullopt;
        return rational_t{numerator.low, denominator.low};
    } else {
        const auto signed_numerator = signed_value(numerator.low, sum.negative);
        if (!signed_numerator) return std::nullopt;
        return signed_rational_t{*signed_numerator, denominator.low};
    }
}

// Reduce after the signed 128-bit sum: a representable result must not be
// rejected merely because its common-denominator intermediate exceeded 64 bits.
template <class Result = signed_rational_t>
inline auto sum(signed_rational_t left, signed_rational_t right, bool subtract = false)
    -> std::optional<Result> {
    const auto a = try_normalize(left);
    const auto b = try_normalize(right);
    if (!a || !b) return std::nullopt;
    const auto common = std::gcd(a->denominator, b->denominator);
    const auto lhs = signed_wide_t{wide_math::multiply(magnitude(a->numerator),
                                                        b->denominator / common),
                                   a->numerator < 0};
    const auto rhs = signed_wide_t{wide_math::multiply(magnitude(b->numerator),
                                                        a->denominator / common),
                                   (b->numerator < 0) != subtract};
    return reduce_sum<Result>(combine(lhs, rhs), common, a->denominator, b->denominator);
}

inline auto add_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> { return sum(left, right); }
// The default retains signed-clock bounds; duration callers request rational_t
// explicitly to retain the full unsigned range while rejecting negative results.
template <class Result = signed_rational_t>
inline auto subtract_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<Result> { return sum<Result>(left, right, true); }

inline constexpr std::uint64_t clock_quantum = 1'000'000'000'000;
inline constexpr std::uint64_t row_quantum = 1'000'000'000;

inline auto rounded_quotient(wide_math::u128_t numerator, wide_math::u128_t denominator)
    -> std::optional<std::uint64_t> {
    if (denominator == wide_math::u128_t{}) return std::nullopt;
    auto result = divide_wide(numerator, denominator);
    const auto complement = wide_math::subtract(denominator, result.remainder);
    const auto round_up = wide_math::u128_less(complement, result.remainder) ||
        (complement == result.remainder && (result.quotient.low & 1U) != 0);
    if (round_up) result.quotient = wide_math::add(result.quotient, 1);
    if (!wide_math::can_fit_u64(result.quotient)) return std::nullopt;
    return result.quotient.low;
}

using checked_math::checked_multiply;

inline auto factor_product(std::span<const std::uint64_t> factors)
    -> std::optional<wide_math::u128_t> {
    auto product = wide_math::u128_t{0, 1};
    for (const auto factor : factors) {
        const auto next = checked_multiply(product, factor);
        if (!next) return std::nullopt;
        product = *next;
    }
    return product;
}

inline auto rounded_factor_ratio(std::array<std::uint64_t, 4> numerators,
                                  std::array<std::uint64_t, 3> denominators)
    -> std::optional<std::uint64_t> {
    for (const auto denominator : denominators) {
        if (denominator == 0) return std::nullopt;
    }
    for (auto& numerator : numerators) {
        for (auto& denominator : denominators) {
            const auto common = std::gcd(numerator, denominator);
            numerator /= common;
            denominator /= common;
        }
    }
    const auto numerator = factor_product(numerators);
    const auto denominator = factor_product(denominators);
    if (!numerator || !denominator) return std::nullopt;
    return rounded_quotient(*numerator, *denominator);
}

template <class Result = signed_rational_t>
inline auto rows_to_beat_fraction(std::uint64_t rows, std::uint64_t resolution,
                                  std::uint64_t units = 4) -> std::optional<Result> {
    if (resolution == 0 || units == 0) return std::nullopt;
    return reduce_sum<Result>({wide_math::multiply(rows, units), false}, resolution, resolution,
                              resolution);
}
inline auto frames_to_seconds(std::int64_t frame, std::uint32_t sample_rate)
    -> std::optional<signed_rational_t> { return try_normalize({frame, sample_rate}); }
inline auto seconds_to_frames(signed_rational_t seconds, std::uint32_t sample_rate,
    sample_rounding_t rounding) -> sample_frame_result_t {
    return sample_frame_at(seconds, sample_rate, rounding);
}

inline auto clock_ticks(signed_rational_t seconds) -> std::optional<std::int64_t> {
    if (seconds.denominator == 0) return std::nullopt;
    if (clock_quantum % seconds.denominator == 0) {
        const auto product = wide_math::multiply(magnitude(seconds.numerator),
                                                  clock_quantum / seconds.denominator);
        return wide_math::can_fit_u64(product) ? signed_value(product.low, seconds.numerator < 0)
                                             : std::nullopt;
    }
    const auto ticks = rounded_quotient(wide_math::multiply(magnitude(seconds.numerator),
                                                           clock_quantum),
                                        {0, seconds.denominator});
    return ticks ? signed_value(*ticks, seconds.numerator < 0) : std::nullopt;
}

inline auto clock_value(std::int64_t ticks) -> signed_rational_t {
    return *try_normalize({ticks, clock_quantum});
}

inline auto advance_ticks(std::int64_t begin, std::uint64_t delta)
    -> std::optional<std::int64_t> {
    if (begin < 0) {
        const auto absolute = magnitude(begin);
        return delta < absolute ? signed_value(absolute - delta, true)
                                : signed_value(delta - absolute, false);
    }
    if (delta > positive_limit - static_cast<std::uint64_t>(begin)) return std::nullopt;
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(begin) + delta);
}

// Fractions are nonnegative offsets within a segment. Both products fit 128
// bits under the signed tick and nanorow bounds enforced by compilation.
inline auto interpolate_clock(std::int64_t begin, std::int64_t end,
                               signed_rational_t offset, std::uint64_t width)
    -> std::optional<signed_rational_t> {
    if (end < begin || offset.numerator < 0 || offset.denominator == 0 || width == 0)
        return std::nullopt;
    const auto duration = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
    const auto elapsed = rounded_quotient(
        wide_math::multiply(duration, static_cast<std::uint64_t>(offset.numerator)),
        wide_math::multiply(width, offset.denominator));
    if (!elapsed || *elapsed > duration) return std::nullopt;
    const auto ticks = advance_ticks(begin, *elapsed);
    return ticks ? std::optional{clock_value(*ticks)} : std::nullopt;
}

inline auto interpolate_row(std::uint64_t begin_row, std::uint64_t end_row,
                             std::int64_t begin, std::int64_t end, std::int64_t query)
    -> std::optional<signed_rational_t> {
    if (end <= begin || query < begin || query > end || begin_row > end_row ||
        end_row > positive_limit / row_quantum) return std::nullopt;
    const auto duration = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
    const auto elapsed = static_cast<std::uint64_t>(query) - static_cast<std::uint64_t>(begin);
    const auto offset = rounded_quotient(
        wide_math::multiply(elapsed, (end_row - begin_row) * row_quantum), {0, duration});
    if (!offset || *offset > (end_row - begin_row) * row_quantum) return std::nullopt;
    const auto row = begin_row * row_quantum + *offset;
    return try_normalize({static_cast<std::int64_t>(row), row_quantum});
}

} // namespace wormedit::timing_arithmetic
