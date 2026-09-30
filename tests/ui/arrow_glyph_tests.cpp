#include "ui/nuklear/arrow_glyph.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace {

void check_square_bounds(wormedit::ui::arrow_point_t point) {
    CHECK(point.x >= 12.0f);
    CHECK(point.x <= 52.0f);
    CHECK(point.y >= 23.0f);
    CHECK(point.y <= 63.0f);
}

} // namespace

TEST_CASE("Arrow silhouettes remain inside their square and point in each lane direction",
          "[frontend-state][arrow]") {
    const std::array<wormedit::ui::arrow_point_t, 4> tips{{
        {14.4f, 43.0f}, {32.0f, 60.6f}, {32.0f, 25.4f}, {49.6f, 43.0f}}};
    for (std::uint8_t direction = 2; direction <= 5; ++direction) {
        const auto polygon = wormedit::ui::arrow_polygon(direction, 12.0f, 23.0f, 40.0f);
        for (const auto point : polygon) check_square_bounds(point);
        const auto expected = tips[direction - 2];
        CHECK(std::abs(polygon.front().x - expected.x) < 0.001f);
        CHECK(std::abs(polygon.front().y - expected.y) < 0.001f);
    }
}

TEST_CASE("Arrow snap classes use exact standard-measure rows including tuplets",
          "[frontend-state][arrow]") {
    struct snap_case_t { std::uint64_t row; std::uint64_t divisions; std::uint8_t expected; };
    const std::array<snap_case_t, 11> cases{{
        {0, 960, 4}, {240, 960, 4}, {120, 960, 8}, {80, 960, 12},
        {60, 960, 16}, {40, 960, 24}, {30, 960, 32}, {1, 960, 0},
        {1080, 960, 8}, {0, 0, 0}, {std::numeric_limits<std::uint64_t>::max(), 960, 0}}};
    for (const auto& item : cases) {
        CHECK(wormedit::ui::note_snap_class(item.row, item.divisions) == item.expected);
    }
}

TEST_CASE("Arrow palette preserves the confirmed snap hues and other fallback",
          "[frontend-state][arrow]") {
    const std::array<std::uint8_t, 7> classes{4, 8, 12, 16, 24, 32, 0};
    const std::array<wormedit::ui::arrow_color_t, 7> expected{{
        {235, 73, 79}, {75, 139, 240}, {170, 98, 223}, {242, 218, 74},
        {239, 118, 191}, {241, 154, 65}, {155, 163, 175}}};
    for (std::size_t index = 0; index < classes.size(); ++index) {
        const auto color = wormedit::ui::arrow_snap_color(classes[index]);
        CHECK(color.red == expected[index].red);
        CHECK(color.green == expected[index].green);
        CHECK(color.blue == expected[index].blue);
    }
    CHECK(wormedit::ui::arrow_snap_color(255).red == expected.back().red);
}
