#pragma once

#include <wormedit/core/measure.h>

#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace wormedit {

// Invalid exact values convert to zero rather than propagating infinities/NaNs.
enum class zero_denominator_policy_t { return_zero };
inline constexpr auto rational_zero_denominator_policy = zero_denominator_policy_t::return_zero;

namespace rational_detail {
[[nodiscard]] auto to_double_magnitude(std::uint64_t numerator, std::uint64_t denominator) noexcept -> double;
}

// Round the exact ratio once to binary64, including when long double has only 53 value bits.
template<class Rational>
[[nodiscard]] inline auto to_double(Rational value,
    zero_denominator_policy_t policy = rational_zero_denominator_policy) noexcept -> double {
    (void)policy;
    if (value.denominator == 0) return 0.0;
    const bool negative = std::is_signed_v<decltype(value.numerator)> && value.numerator < 0;
    const auto magnitude = negative ? static_cast<std::uint64_t>(-(value.numerator + 1)) + 1
                                    : static_cast<std::uint64_t>(value.numerator);
    const auto result = rational_detail::to_double_magnitude(magnitude, value.denominator);
    return negative ? -result : result;
}
template<class Rational>
[[nodiscard]] constexpr auto to_long_double(Rational value,
    zero_denominator_policy_t policy = rational_zero_denominator_policy) noexcept -> long double {
    if constexpr (std::numeric_limits<long double>::digits == std::numeric_limits<double>::digits)
        return to_double(value, policy);
    return value.denominator == 0 ? 0.0L
        : static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
}
enum class row_clamp_policy_t { both_ends, upper_only, reject_outside };
[[nodiscard]] inline auto nearest_row(long double row, std::uint64_t total_rows,
    row_clamp_policy_t policy) -> std::optional<std::uint64_t> {
    if (!std::isfinite(row) || total_rows == 0 ||
        (policy != row_clamp_policy_t::both_ends && row < 0))
        return std::nullopt;
    const auto nearest = std::floor(row + 0.5L);
    if (policy == row_clamp_policy_t::reject_outside &&
        nearest >= static_cast<long double>(total_rows))
        return std::nullopt;
    if (nearest <= 0) return 0;
    if (nearest >= static_cast<long double>(total_rows)) return total_rows - 1;
    return static_cast<std::uint64_t>(nearest);
}
[[nodiscard]] inline auto rows_to_fraction(std::uint64_t rows, std::uint64_t resolution)
    -> rational_t {
    const auto divisor = std::gcd(rows, resolution);
    return {rows / divisor, resolution / divisor};
}
[[nodiscard]] inline auto reduce_fraction(rational_t value) -> rational_t {
    return rows_to_fraction(value.numerator, value.denominator);
}
inline constexpr std::array<std::uint64_t, 6> default_snap_denominators{4, 8, 16, 32, 48, 64};

inline constexpr std::uint32_t maximum_rationalization_iterations = 128;

enum class rationalization_phase_t : std::uint8_t { exact, approximation };

struct rationalization_options_t {
    std::uint64_t maximum_denominator{1'000'000};
    std::uint32_t iteration_limit{64};
    // When set, candidates whose reported absolute-error bound exceeds this
    // value are rejected. An unset limit returns approximations for preview.
    std::optional<long double> maximum_absolute_error;
};

struct rationalization_error_info_t {
    failure_code_t code{failure_code_t::invalid_request};
    long double absolute_error_bound{};
    std::uint32_t iterations{};
    auto operator==(const rationalization_error_info_t&) const -> bool = default;
};

// Converts a positive finite IEEE-754 double deterministically to a reduced
// positive rational. Exact binary values are retained when both limits allow
// it; otherwise bounded continued fractions return a previewable approximation.
struct rationalized_value_t {
    [[nodiscard]] static auto from(double value, const rationalization_options_t& options = {})
        -> result_t<rationalized_value_t, rationalization_error_info_t>;

    rational_t value;
    rationalization_phase_t status{rationalization_phase_t::exact};
    // The conservative bound and actual iterations also travel with rejected approximations.
    long double absolute_error_bound{};
    std::uint32_t iterations{};
    auto operator==(const rationalized_value_t&) const -> bool = default;
};

struct snap_state_t {
    // Configured order is preserved, unsupported values and duplicates removed.
    // If none remain, the exact one-row fallback (1 / resolution) is inserted.
    std::vector<std::uint64_t> supported_denominators;
    std::uint64_t active_denominator{};
    bool active_was_fallback{};
    bool inserted_resolution_fallback{};
    std::optional<failure_code_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error.has_value();
    }
};

// A snap 1/d is exact iff d divides the global divisions-per-standard-measure.
// If the requested active denominator is absent or unsupported, the first
// supported configured denominator is selected. If no configured value is
// supported, resolution is inserted and selected as the exact fallback.
[[nodiscard]] auto filter_snap_denominators(std::uint64_t divisions_per_standard_measure,
                                            std::span<const std::uint64_t> ordered_denominators,
                                            std::uint64_t requested_active_denominator)
    -> snap_state_t;

} // namespace wormedit
