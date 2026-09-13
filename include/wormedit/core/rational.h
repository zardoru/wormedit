#pragma once

#include <wormedit/core/measure.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wormedit {

inline constexpr std::uint32_t maximum_rationalization_iterations = 128;

enum class rationalization_status_t {
    exact,
    approximation,
    error,
};

enum class rationalization_error_t {
    none,
    non_finite,
    non_positive,
    invalid_limits,
    no_positive_candidate,
    approximation_exceeds_error_limit,
};

struct rationalization_options_t {
    std::uint64_t maximum_denominator{1'000'000};
    std::uint32_t iteration_limit{64};
    // When set, candidates whose reported absolute-error bound exceeds this
    // value are rejected. An unset limit returns approximations for preview.
    std::optional<long double> maximum_absolute_error;
};

struct rationalization_result_t {
    std::optional<rational_t> value;
    rationalization_status_t status{rationalization_status_t::error};
    rationalization_error_t error{rationalization_error_t::none};
    // This is a conservative bound against the exact IEEE-754 input value.
    long double absolute_error_bound{};
    std::uint32_t iterations{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return value.has_value();
    }
};

// Converts a positive finite IEEE-754 double deterministically to a reduced
// positive rational. Exact binary values are retained when both limits allow
// it; otherwise bounded continued fractions return a previewable approximation.
[[nodiscard]] auto rationalize_positive_double(double value,
                                               const rationalization_options_t& options = {})
    -> rationalization_result_t;

enum class snap_state_error_t {
    none,
    zero_resolution,
};

struct snap_state_t {
    // Configured order is preserved, unsupported values and duplicates removed.
    // If none remain, the exact one-row fallback (1 / resolution) is inserted.
    std::vector<std::uint64_t> supported_denominators;
    std::uint64_t active_denominator{};
    bool active_was_fallback{};
    bool inserted_resolution_fallback{};
    snap_state_error_t error{snap_state_error_t::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == snap_state_error_t::none;
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
