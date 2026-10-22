#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Signature suggestions for a typed measure length (numerator/denominator text). Mirrors
// `wormedit::suggest_meters` (denominators 4, 8, 16 in order, exact lengths only) without
// crossing the core boundary; applying one never changes the length.
struct worm_ui_nuklear_meter_suggestion_t {
    std::uint32_t numerator{};
    std::uint32_t denominator{};
};

struct worm_ui_nuklear_meter_suggestions_t {
    std::array<worm_ui_nuklear_meter_suggestion_t, 3> items{};
    std::size_t count{};
};

[[nodiscard]] auto worm_ui_nuklear_suggest_meters(const char* numerator_text,
                                                  const char* denominator_text)
    -> worm_ui_nuklear_meter_suggestions_t;
