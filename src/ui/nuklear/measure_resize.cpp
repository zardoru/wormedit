#include "ui/nuklear/measure_resize.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

auto worm_ui_resize_measure(std::uint64_t old_numerator, std::uint64_t old_denominator,
                            double delta_whole_notes, std::uint64_t snap_denominator)
    -> worm_ui_measure_resize_t {
    worm_ui_measure_resize_t result;
    if (snap_denominator == 0 || old_denominator == 0 || !std::isfinite(delta_whole_notes) ||
        snap_denominator > 1'000'000) {
        result.label = "No grid snap";
        return result;
    }
    const auto old_length = static_cast<double>(old_numerator) / static_cast<double>(old_denominator);
    const auto maximum_units = worm_ui_measure_resize_maximum_measures * snap_denominator;
    const auto units = std::clamp<double>(
        std::llround((old_length + delta_whole_notes) * static_cast<double>(snap_denominator)), 1.0,
        static_cast<double>(maximum_units));
    auto numerator = static_cast<std::uint64_t>(units);
    auto denominator = snap_denominator;
    const auto divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    result.length_whole_notes = static_cast<double>(numerator) / static_cast<double>(denominator);
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
