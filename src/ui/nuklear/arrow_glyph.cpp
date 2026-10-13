#include "arrow_glyph.h"

#include <numeric>

namespace wormedit::ui {
namespace {

// StepMania/ITG snap palette (4th red, 8th blue, 12th purple, 16th yellow, 24th pink,
// 32nd orange, 48th cyan, 64th green); the last color is every other division.
constexpr std::array<std::uint8_t, 8> snap_classes{4, 8, 12, 16, 24, 32, 48, 64};
constexpr std::array<color_t, 9> snap_colors{{
    {235, 73, 79}, {75, 139, 240}, {170, 98, 223}, {242, 218, 74},
    {239, 118, 191}, {241, 154, 65}, {80, 200, 220}, {96, 200, 110}, {155, 163, 175}}};

auto rotate_arrow_point(arrow_point_t point, std::uint8_t direction) -> arrow_point_t {
    switch (direction) {
    case 2: return {point.y, -point.x};
    case 3: return {-point.x, -point.y};
    case 5: return {-point.y, point.x};
    case 6: return {(point.x + point.y) * 0.70710678f, (point.y - point.x) * 0.70710678f};
    case 7: return {(point.x - point.y) * 0.70710678f, (point.x + point.y) * 0.70710678f};
    case 8: return {(point.y - point.x) * 0.70710678f, (-point.x - point.y) * 0.70710678f};
    case 9: return {(-point.x - point.y) * 0.70710678f, (point.x - point.y) * 0.70710678f};
    default: return point;
    }
}

} // namespace

auto arrow_polygon(std::uint8_t direction, float x, float y, float size)
    -> std::array<arrow_point_t, 7> {
    // Up arrow: broad triangular head and a narrower, flat-ended stem.
    std::array<arrow_point_t, 7> points{{
        {0.0f, -0.44f}, {0.44f, 0.02f}, {0.19f, 0.02f},
        {0.19f, 0.42f}, {-0.19f, 0.42f}, {-0.19f, 0.02f}, {-0.44f, 0.02f}}};
    if (direction == last_arrow_glyph) {
        // Center panel: a diamond, with collinear edge points to share the seven-point API.
        points = {{{0.0f, -0.4f}, {0.2f, -0.2f}, {0.4f, 0.0f}, {0.2f, 0.2f},
                   {0.0f, 0.4f}, {-0.4f, 0.0f}, {-0.2f, -0.2f}}};
    }
    for (auto& point : points) {
        point = rotate_arrow_point(point, direction);
        point = {x + size * (0.5f + point.x), y + size * (0.5f + point.y)};
    }
    return points;
}

auto note_snap_class(std::uint64_t row, std::uint64_t divisions_per_standard_measure)
    -> std::uint8_t {
    if (divisions_per_standard_measure == 0) return 0;
    const auto denominator = divisions_per_standard_measure /
                             std::gcd(row, divisions_per_standard_measure);
    for (const auto snap : snap_classes) {
        if (snap % denominator == 0) return snap;
    }
    return 0;
}

namespace {
auto scaled_remainder(std::uint64_t value, std::uint64_t scale,
                      std::uint64_t modulus) -> std::uint64_t {
    value %= modulus;
    std::uint64_t result{};
    // scale is at most 64; modular addition avoids overflow even at UINT64_MAX.
    for (std::uint64_t index = 0; index < scale; ++index) {
        if (value >= modulus - result) result -= modulus - value;
        else result += value;
    }
    return result;
}
}

auto note_snap_class(std::uint64_t whole, std::uint64_t numerator,
                     std::uint64_t denominator, std::uint64_t divisions) -> std::uint8_t {
    if (denominator == 0 || numerator >= denominator || divisions == 0) return 0;
    if (numerator == 0) return note_snap_class(whole, divisions);
    const auto reduction = std::gcd(numerator, denominator);
    numerator /= reduction;
    denominator /= reduction;
    for (const auto snap : snap_classes) {
        const auto common = std::gcd(static_cast<std::uint64_t>(snap), divisions);
        const auto scale = snap / common;
        const auto divisor = divisions / common;
        if (scale % denominator != 0) continue;
        const auto fraction = (numerator * (scale / denominator)) % divisor;
        const auto remainder = scaled_remainder(whole, scale, divisor);
        if (fraction == 0 ? remainder == 0 : remainder == divisor - fraction) return snap;
    }
    return 0;
}

auto arrow_snap_color(std::uint8_t snap_class) -> color_t {
    for (std::size_t index = 0; index < snap_classes.size(); ++index) {
        if (snap_class == snap_classes[index]) return snap_colors[index];
    }
    return snap_colors.back();
}

} // namespace wormedit::ui
