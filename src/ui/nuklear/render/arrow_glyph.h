#pragma once
#include "ui/field/color.h"
#include <wormedit/core/snap.h>
#include <wormedit/core/vocabulary.h>

#include <array>
#include <cstdint>

namespace wormedit::ui {

struct arrow_point_t {
    float x;
    float y;
};

inline constexpr std::uint8_t last_arrow_glyph = 10;

[[nodiscard]] constexpr auto is_arrow_glyph(std::uint8_t glyph) -> bool {
    return glyph >= 2 && glyph <= last_arrow_glyph;
}

// Uses the frontend/core glyph values, including diagonal arrows and Pump center.
auto arrow_polygon(std::uint8_t direction, float x, float y, float size)
    -> std::array<arrow_point_t, 7>;

[[nodiscard]] constexpr auto is_arrow_glyph(wormedit::lane_glyph_t glyph) -> bool {
    return is_arrow_glyph(static_cast<std::uint8_t>(glyph));
}
inline auto arrow_polygon(wormedit::lane_glyph_t glyph, float x, float y, float size)
    -> std::array<arrow_point_t, 7> {
    return arrow_polygon(static_cast<std::uint8_t>(glyph), x, y, size);
}

// First matching subdivision of `period` rows (a standard measure, or in authored space the
// measure itself); 0 denotes "other".
auto note_snap_class(std::uint64_t row, std::uint64_t period) -> std::uint8_t;
inline auto note_snap_class(wormedit::grid_phase_t phase) -> std::uint8_t {
    return note_snap_class(phase.row, phase.period);
}

// Exact fractional chart rows, without rounding into a neighboring snap class.
auto note_snap_class(std::uint64_t whole, std::uint64_t numerator,
                     std::uint64_t denominator, std::uint64_t period) -> std::uint8_t;
inline auto note_snap_class(wormedit::grid_phase_t phase, std::uint64_t numerator,
                            std::uint64_t denominator) -> std::uint8_t {
    return note_snap_class(phase.row, numerator, denominator, phase.period);
}

auto arrow_snap_color(std::uint8_t snap_class) -> color_t;

} // namespace wormedit::ui
