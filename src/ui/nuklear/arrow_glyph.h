#pragma once

#include <array>
#include <cstdint>

namespace wormedit::ui {

struct arrow_point_t {
    float x;
    float y;
};

// Directions use the frontend/core glyph values: left=2, down=3, up=4, right=5.
auto arrow_polygon(std::uint8_t direction, float x, float y, float size)
    -> std::array<arrow_point_t, 7>;

// First matching subdivision of a standard measure; 0 denotes "other".
auto note_snap_class(std::uint64_t row, std::uint64_t divisions_per_standard_measure)
    -> std::uint8_t;

struct arrow_color_t {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

auto arrow_snap_color(std::uint8_t snap_class) -> arrow_color_t;

} // namespace wormedit::ui
