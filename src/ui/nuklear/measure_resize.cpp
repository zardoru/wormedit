#include "ui/nuklear/measure_resize.h"

#include <wormedit/core/rational.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <string>

namespace {
using wormedit::rational_t;

auto is_valid_step(rational_t step) -> bool {
    return step.numerator != 0 && step.denominator != 0 && step.denominator <= 1'000'000 &&
           step.numerator <= 1'000'000;
}

auto times(rational_t step, std::uint64_t units) -> rational_t {
    return wormedit::reduce_fraction({step.numerator * units, step.denominator});
}

// A length as a whole number of grid steps, if it is on the grid.
auto units_on_grid(rational_t length, rational_t step) -> std::optional<std::uint64_t> {
    const auto numerator = length.numerator * step.denominator;
    const auto denominator = length.denominator * step.numerator;
    if (denominator == 0 || numerator % denominator != 0) return std::nullopt;
    return numerator / denominator;
}

// first ± second, for lengths with small denominators.
auto combine(rational_t first, rational_t second, bool add) -> rational_t {
    const auto common = std::lcm(first.denominator, second.denominator);
    const auto left = first.numerator * (common / first.denominator);
    const auto right = second.numerator * (common / second.denominator);
    return wormedit::reduce_fraction({add ? left + right : left - right, common});
}

// A length as a meter: lowest terms, denominator widened to at least 4, power of two only.
auto meter_of(rational_t length, std::uint32_t& numerator, std::uint32_t& denominator) -> bool {
    const auto reduced = wormedit::reduce_fraction(length);
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

auto meter_label(std::uint32_t numerator, std::uint32_t denominator) -> std::string {
    return std::to_string(numerator) + "/" + std::to_string(denominator);
}
} // namespace

auto worm_ui_resize_measure(std::uint64_t old_numerator, std::uint64_t old_denominator,
                            double delta_whole_notes, wormedit::rational_t step)
    -> worm_ui_measure_resize_t {
    worm_ui_measure_resize_t result;
    if (!is_valid_step(step) || old_denominator == 0 || !std::isfinite(delta_whole_notes)) {
        result.label = "No grid snap";
        return result;
    }
    const auto old_length = wormedit::to_double(rational_t{old_numerator, old_denominator});
    const auto step_length = wormedit::to_double(step);
    const auto maximum_units =
        std::max<std::uint64_t>(1, worm_ui_measure_resize_maximum_measures * step.denominator / step.numerator);
    const auto units = std::clamp<double>(std::llround((old_length + delta_whole_notes) / step_length), 1.0,
                                          static_cast<double>(maximum_units));
    const auto length = times(step, static_cast<std::uint64_t>(units));
    result.length_whole_notes = wormedit::to_double(length);
    if (!meter_of(length, result.numerator, result.denominator)) {
        result.label = meter_label(static_cast<std::uint32_t>(length.numerator),
                                   static_cast<std::uint32_t>(length.denominator)) +
                       ": denominator must be a power of two";
        return result;
    }
    result.valid = true;
    result.label = meter_label(result.numerator, result.denominator);
    return result;
}

auto worm_ui_move_bar_line(std::uint64_t first_numerator, std::uint64_t first_denominator,
                           std::uint64_t second_numerator, std::uint64_t second_denominator,
                           double delta_whole_notes, wormedit::rational_t step_before,
                           wormedit::rational_t step_after) -> worm_ui_bar_line_move_t {
    worm_ui_bar_line_move_t result;
    if (!is_valid_step(step_before) || !is_valid_step(step_after) ||
        !std::isfinite(delta_whole_notes) || first_denominator == 0 || second_denominator == 0) {
        result.label = "No grid snap";
        return result;
    }
    const rational_t first{first_numerator, first_denominator};
    const rational_t second{second_numerator, second_denominator};
    const auto first_units = units_on_grid(first, step_before);
    const auto second_units = units_on_grid(second, step_after);
    if (!first_units || !second_units || !*first_units || !*second_units) {
        result.label = "A neighboring measure is off the grid";
        return result;
    }
    // The line moves along the grid of the measure it moves into, leaving it at least one step.
    const auto later = delta_whole_notes >= 0.0;
    const auto step = later ? step_after : step_before;
    const auto room = (later ? *second_units : *first_units) - 1;
    const auto wanted = std::llround(std::abs(delta_whole_notes) / wormedit::to_double(step));
    const auto units = static_cast<std::uint64_t>(std::clamp<long long>(wanted, 0, static_cast<long long>(room)));
    const auto moved = combine(first, times(step, units), later);
    const auto rest = combine(combine(first, second, true), moved, false);
    result.first_length_whole_notes = wormedit::to_double(moved);
    if (!meter_of(moved, result.first_numerator, result.first_denominator) ||
        !meter_of(rest, result.second_numerator, result.second_denominator)) {
        result.label = "Denominator must be a power of two";
        return result;
    }
    result.valid = true;
    result.label = meter_label(result.first_numerator, result.first_denominator) + " | " +
                   meter_label(result.second_numerator, result.second_denominator);
    return result;
}
