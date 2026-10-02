#include "ui/nuklear/timing_state.h"

#include <limits>
#include <numeric>

auto worm_ui_nuklear_timing_state() noexcept -> worm_ui_nuklear_timing_state_t& {
    static worm_ui_nuklear_timing_state_t state;
    return state;
}

void worm_ui_nuklear_reset_timing_state() {
    worm_ui_nuklear_timing_state() = {};
}

namespace {

auto parse_meter_digits(const char* text) -> std::uint64_t {
    constexpr std::uint64_t limit = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t value = 0;
    if (!text) return 0;
    for (; *text >= '0' && *text <= '9'; ++text) {
        value = value * 10 + static_cast<std::uint64_t>(*text - '0');
        if (value > limit) return 0;
    }
    return *text == '\0' ? value : 0;
}

} // namespace

auto worm_ui_nuklear_suggest_meters(const char* numerator_text, const char* denominator_text)
    -> worm_ui_nuklear_meter_suggestions_t {
    worm_ui_nuklear_meter_suggestions_t suggestions;
    const auto numerator = parse_meter_digits(numerator_text);
    const auto denominator = parse_meter_digits(denominator_text);
    if (numerator == 0 || denominator == 0) return suggestions;
    const auto divisor = std::gcd(numerator, denominator);
    const auto reduced_numerator = numerator / divisor;
    const auto reduced_denominator = denominator / divisor;
    for (const std::uint32_t candidate : {4U, 8U, 16U}) {
        const auto scaled = reduced_numerator * candidate;
        if (scaled % reduced_denominator != 0) continue;
        const auto scaled_numerator = scaled / reduced_denominator;
        if (scaled_numerator > std::numeric_limits<std::uint32_t>::max()) continue;
        suggestions.items[suggestions.count++] = {static_cast<std::uint32_t>(scaled_numerator),
                                                  candidate};
    }
    return suggestions;
}
