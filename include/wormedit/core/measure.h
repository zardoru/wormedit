#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace wormedit {

inline constexpr std::uint64_t default_divisions_per_standard_measure = 960;

enum class measure_error_t {
    none,
    empty,
    invalid_syntax,
    non_positive,
    zero_denominator,
    overflow,
    zero_resolution,
    non_integral_capacity,
    meter_mismatch,
};

struct rational_t {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};

    auto operator==(const rational_t&) const -> bool = default;
};

struct meter_t {
    std::uint32_t numerator{};
    std::uint32_t denominator{};

    auto operator==(const meter_t&) const -> bool = default;
};

struct measure_parse_result_t {
    std::optional<rational_t> value;
    measure_error_t error{measure_error_t::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

struct measure_definition_t {
    rational_t length;
    std::optional<meter_t> meter;
};

struct measure_validation_t {
    std::optional<std::uint64_t> row_capacity;
    measure_error_t error{measure_error_t::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return row_capacity.has_value();
    }
};

struct bmson_grid_t {
    std::uint64_t resolution{};
    std::uint64_t pulses_per_editor_row{};
};

[[nodiscard]] auto parse_measure_length(std::string_view text) -> measure_parse_result_t;
[[nodiscard]] auto validate_measure(
    std::uint64_t divisions_per_standard_measure,
    const measure_definition_t& measure) -> measure_validation_t;
[[nodiscard]] auto make_default_bmson_grid(
    std::uint64_t divisions_per_standard_measure) -> std::optional<bmson_grid_t>;
[[nodiscard]] auto bmson_pulse_for_row(
    const bmson_grid_t& grid,
    std::uint64_t absolute_editor_row) -> std::optional<std::uint64_t>;

} // namespace wormedit
