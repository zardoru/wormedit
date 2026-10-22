#include "ui/nuklear/audio/meter_state.h"

#include <wormedit/core/rational.h>
#include <wormedit/core/numeric_text.h>

#include <limits>

auto worm_ui_nuklear_suggest_meters(const char* numerator_text, const char* denominator_text)
    -> worm_ui_nuklear_meter_suggestions_t {
    worm_ui_nuklear_meter_suggestions_t suggestions;
    const auto numerator = wormedit::parse_integer<std::uint32_t>(numerator_text).value_or(0);
    const auto denominator = wormedit::parse_integer<std::uint32_t>(denominator_text).value_or(0);
    if (numerator == 0 || denominator == 0) return suggestions;
    const auto reduced = wormedit::rows_to_fraction(numerator, denominator);
    const auto reduced_numerator = reduced.numerator;
    const auto reduced_denominator = reduced.denominator;
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
