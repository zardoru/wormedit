#include "ui/nuklear/measure_resize.h"

#include <wormedit/core/rational.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>

auto worm_ui_resize_measure(std::uint64_t old_numerator, std::uint64_t old_denominator,
                            double delta_whole_notes, std::uint64_t snap_denominator)
    -> worm_ui_measure_resize_t {
    worm_ui_measure_resize_t result;
    if (snap_denominator == 0 || old_denominator == 0 || !std::isfinite(delta_whole_notes) ||
        snap_denominator > 1'000'000) {
        result.label = "No grid snap";
        return result;
    }
    const auto old_length = wormedit::to_double(wormedit::rational_t{old_numerator, old_denominator});
    const auto maximum_units = worm_ui_measure_resize_maximum_measures * snap_denominator;
    const auto units = std::clamp<double>(
        std::llround((old_length + delta_whole_notes) * static_cast<double>(snap_denominator)), 1.0,
        static_cast<double>(maximum_units));
    const auto reduced = wormedit::rows_to_fraction(static_cast<std::uint64_t>(units), snap_denominator);
    auto numerator = reduced.numerator;
    auto denominator = reduced.denominator;
    result.length_whole_notes = wormedit::to_double(wormedit::rational_t{numerator, denominator});
    if (!std::has_single_bit(denominator)) {
        result.label = std::to_string(numerator) + "/" + std::to_string(denominator) +
                       ": denominator must be a power of two";
        return result;
    }
    while (denominator < 4) {
        numerator *= 2;
        denominator *= 2;
    }
    result.valid = true;
    result.numerator = static_cast<std::uint32_t>(numerator);
    result.denominator = static_cast<std::uint32_t>(denominator);
    result.label = std::to_string(numerator) + "/" + std::to_string(denominator);
    return result;
}

namespace {
// units/snap as a meter: lowest terms, denominator widened to at least 4, power of two only.
auto meter_of_units(std::uint64_t units, std::uint64_t snap, std::uint32_t& numerator,
                    std::uint32_t& denominator) -> bool {
    const auto reduced = wormedit::rows_to_fraction(units, snap);
    auto top = reduced.numerator;
    auto bottom = reduced.denominator;
    if (!std::has_single_bit(bottom)) return false;
    while (bottom < 4) {
        top *= 2;
        bottom *= 2;
    }
    if (top > std::numeric_limits<std::uint32_t>::max()) return false;
    numerator = static_cast<std::uint32_t>(top);
    denominator = static_cast<std::uint32_t>(bottom);
    return true;
}

// A length as a whole number of grid steps, if it is on the grid.
auto units_on_grid(std::uint64_t numerator, std::uint64_t denominator, std::uint64_t snap)
    -> std::optional<std::uint64_t> {
    if (denominator == 0 || (numerator * snap) % denominator != 0) return std::nullopt;
    return numerator * snap / denominator;
}
} // namespace

auto worm_ui_move_bar_line(std::uint64_t first_numerator, std::uint64_t first_denominator,
                           std::uint64_t second_numerator, std::uint64_t second_denominator,
                           double delta_whole_notes, std::uint64_t snap_denominator)
    -> worm_ui_bar_line_move_t {
    worm_ui_bar_line_move_t result;
    if (snap_denominator == 0 || snap_denominator > 1'000'000 || !std::isfinite(delta_whole_notes)) {
        result.label = "No grid snap";
        return result;
    }
    const auto first = units_on_grid(first_numerator, first_denominator, snap_denominator);
    const auto second = units_on_grid(second_numerator, second_denominator, snap_denominator);
    if (!first || !second) {
        result.label = "A neighboring measure is off the grid";
        return result;
    }
    const auto total = *first + *second;
    const auto wanted = std::llround((wormedit::to_double(wormedit::rational_t{*first, snap_denominator}) +
                                      delta_whole_notes) * static_cast<double>(snap_denominator));
    const auto units = static_cast<std::uint64_t>(
        std::clamp<long long>(wanted, 1, static_cast<long long>(total) - 1));
    if (total < 2) {
        result.label = "No room to move the line";
        return result;
    }
    result.first_length_whole_notes = wormedit::to_double(wormedit::rational_t{units, snap_denominator});
    if (!meter_of_units(units, snap_denominator, result.first_numerator, result.first_denominator) ||
        !meter_of_units(total - units, snap_denominator, result.second_numerator, result.second_denominator)) {
        result.label = "Denominator must be a power of two";
        return result;
    }
    result.valid = true;
    result.label = std::to_string(result.first_numerator) + "/" + std::to_string(result.first_denominator) +
                   " | " + std::to_string(result.second_numerator) + "/" +
                   std::to_string(result.second_denominator);
    return result;
}
