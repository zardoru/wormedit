#include "ui/nuklear/compose/measure_resize.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("bar line drag snaps to the grid and states the meter", "[frontend-state][measure-resize]") {
    const auto shorter = worm_ui_resize_measure(1, 1, -0.125, {1, 8});
    REQUIRE(shorter.valid);
    CHECK(shorter.label == "7/8");
    CHECK(shorter.numerator == 7);
    CHECK(shorter.denominator == 8);

    const auto same = worm_ui_resize_measure(1, 1, 0.01, {1, 16});
    REQUIRE(same.valid);
    CHECK(same.label == "4/4");

    const auto longer = worm_ui_resize_measure(1, 1, 0.5, {1, 8});
    REQUIRE(longer.valid);
    CHECK(longer.label == "6/4");
}

TEST_CASE("bar line drag is bounded and rejects unwritable meters", "[frontend-state][measure-resize]") {
    const auto floor = worm_ui_resize_measure(1, 1, -5.0, {1, 16});
    REQUIRE(floor.valid);
    CHECK(floor.label == "1/16");
    CHECK(floor.length_whole_notes == 1.0 / 16.0);

    const auto ceiling = worm_ui_resize_measure(1, 1, 100.0, {1, 4});
    REQUIRE(ceiling.valid);
    CHECK(ceiling.length_whole_notes == 4.0);
    CHECK(ceiling.label == "16/4");

    const auto triplet = worm_ui_resize_measure(1, 1, -1.0 / 12.0, {1, 12});
    CHECK_FALSE(triplet.valid);
    CHECK(triplet.label.find("power of two") != std::string::npos);

    CHECK_FALSE(worm_ui_resize_measure(1, 1, 0.0, {0, 1}).valid);
}

TEST_CASE("moving a bar line trades length between two measures", "[frontend-state][measure-resize]") {
    const auto moved = worm_ui_move_bar_line(1, 1, 1, 1, -0.125, {1, 8}, {1, 8});
    REQUIRE(moved.valid);
    CHECK(moved.label == "7/8 | 9/8");
    CHECK(moved.first_length_whole_notes == 0.875);

    const auto floor = worm_ui_move_bar_line(1, 1, 1, 1, -5.0, {1, 16}, {1, 16});
    REQUIRE(floor.valid);
    CHECK(floor.label == "1/16 | 31/16");

    const auto ceiling = worm_ui_move_bar_line(1, 1, 1, 1, 5.0, {1, 16}, {1, 16});
    REQUIRE(ceiling.valid);
    CHECK(ceiling.label == "31/16 | 1/16");

    CHECK_FALSE(worm_ui_move_bar_line(1, 1, 1, 1, -1.0 / 12.0, {1, 12}, {1, 12}).valid);
    CHECK_FALSE(worm_ui_move_bar_line(1, 1, 1, 16, 0.0, {1, 8}, {1, 8}).valid);
}

TEST_CASE("authored steps divide the measure's own length", "[frontend-state][measure-resize][authored-snap]") {
    // A 5/4 measure at authored 1/4 steps by 5/16.
    const wormedit::rational_t five_sixteenths{5, 16};
    const auto shorter = worm_ui_resize_measure(5, 4, -0.3, five_sixteenths);
    REQUIRE(shorter.valid);
    CHECK(shorter.label == "15/16");
    const auto longer = worm_ui_resize_measure(5, 4, 0.3, five_sixteenths);
    REQUIRE(longer.valid);
    CHECK(longer.label == "25/16");
}

TEST_CASE("a moved bar line follows the grid of the measure it moves into",
          "[frontend-state][measure-resize][authored-snap]") {
    // 5/4 then 4/4 at authored quarters: 5/16 steps above the line, 1/4 steps below it.
    const auto earlier = worm_ui_move_bar_line(5, 4, 1, 1, -0.3, {5, 16}, {1, 4});
    REQUIRE(earlier.valid);
    CHECK(earlier.label == "15/16 | 21/16");
    const auto later = worm_ui_move_bar_line(5, 4, 1, 1, 0.3, {5, 16}, {1, 4});
    REQUIRE(later.valid);
    CHECK(later.label == "6/4 | 3/4");
    // Each measure keeps at least one of its own steps.
    const auto floor = worm_ui_move_bar_line(5, 4, 1, 1, -5.0, {5, 16}, {1, 4});
    REQUIRE(floor.valid);
    CHECK(floor.label == "5/16 | 31/16");
}
