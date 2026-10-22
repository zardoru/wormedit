#include "ui/nuklear/audio/meter_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Meter suggestions list exact signatures for a typed length",
          "[unit][frontend][meter]") {
    const auto three_quarters = worm_ui_nuklear_suggest_meters("3", "4");
    REQUIRE(three_quarters.count == 3);
    CHECK(three_quarters.items[0].numerator == 3);
    CHECK(three_quarters.items[0].denominator == 4);
    CHECK(three_quarters.items[1].numerator == 6);
    CHECK(three_quarters.items[1].denominator == 8);
    CHECK(three_quarters.items[2].numerator == 12);

    const auto seven_eighths = worm_ui_nuklear_suggest_meters("7", "8");
    REQUIRE(seven_eighths.count == 2);
    CHECK(seven_eighths.items[0].numerator == 7);
    CHECK(seven_eighths.items[0].denominator == 8);
    CHECK(seven_eighths.items[1].numerator == 14);

    CHECK(worm_ui_nuklear_suggest_meters("1", "3").count == 0);
    CHECK(worm_ui_nuklear_suggest_meters("", "4").count == 0);
    CHECK(worm_ui_nuklear_suggest_meters("0", "4").count == 0);
    CHECK(worm_ui_nuklear_suggest_meters("3", "x").count == 0);
}
