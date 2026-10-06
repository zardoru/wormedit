#include "ui/nuklear/measure_resize.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("bar line drag snaps to the grid and states the meter", "[frontend-state][measure-resize]") {
    const auto shorter = worm_ui_resize_measure(1, 1, -0.125, 8);
    REQUIRE(shorter.valid);
    CHECK(shorter.label == "7/8");
    CHECK(shorter.numerator == 7);
    CHECK(shorter.denominator == 8);

    const auto same = worm_ui_resize_measure(1, 1, 0.01, 16);
    REQUIRE(same.valid);
    CHECK(same.label == "4/4");

    const auto longer = worm_ui_resize_measure(1, 1, 0.5, 8);
    REQUIRE(longer.valid);
    CHECK(longer.label == "6/4");
}

TEST_CASE("bar line drag is bounded and rejects unwritable meters", "[frontend-state][measure-resize]") {
    const auto floor = worm_ui_resize_measure(1, 1, -5.0, 16);
    REQUIRE(floor.valid);
    CHECK(floor.label == "1/16");
    CHECK(floor.length_whole_notes == 1.0 / 16.0);

    const auto ceiling = worm_ui_resize_measure(1, 1, 100.0, 4);
    REQUIRE(ceiling.valid);
    CHECK(ceiling.length_whole_notes == 4.0);
    CHECK(ceiling.label == "16/4");

    const auto triplet = worm_ui_resize_measure(1, 1, -1.0 / 12.0, 12);
    CHECK_FALSE(triplet.valid);
    CHECK(triplet.label.find("power of two") != std::string::npos);

    CHECK_FALSE(worm_ui_resize_measure(1, 1, 0.0, 0).valid);
}

TEST_CASE("moving a bar line trades length between two measures", "[frontend-state][measure-resize]") {
    const auto moved = worm_ui_move_bar_line(1, 1, 1, 1, -0.125, 8);
    REQUIRE(moved.valid);
    CHECK(moved.label == "7/8 | 9/8");
    CHECK(moved.first_length_whole_notes == 0.875);

    const auto floor = worm_ui_move_bar_line(1, 1, 1, 1, -5.0, 16);
    REQUIRE(floor.valid);
    CHECK(floor.label == "1/16 | 31/16");

    const auto ceiling = worm_ui_move_bar_line(1, 1, 1, 1, 5.0, 16);
    REQUIRE(ceiling.valid);
    CHECK(ceiling.label == "31/16 | 1/16");

    CHECK_FALSE(worm_ui_move_bar_line(1, 1, 1, 1, -1.0 / 12.0, 12).valid);
    CHECK_FALSE(worm_ui_move_bar_line(1, 1, 1, 16, 0.0, 8).valid);
}
