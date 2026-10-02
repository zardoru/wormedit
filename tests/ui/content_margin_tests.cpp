#include "ui/nuklear/content_margin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using wormedit::ui::layout::form_side_margin;

TEST_CASE("Form margins grow with the window aspect ratio", "[unit][frontend][layout]") {
    const float wide = form_side_margin(1920, 1080);
    const float sixteen_ten = form_side_margin(1920, 1200);
    const float three_two = form_side_margin(1800, 1200);
    const float four_three = form_side_margin(1600, 1200);
    CHECK(wide == Catch::Approx(480.0f));
    CHECK(wide > sixteen_ten);
    CHECK(sixteen_ten > three_two);
    CHECK(three_two > four_three);
    CHECK(four_three == Catch::Approx(40.0f));
}

TEST_CASE("Form margins keep a usable column and handle bad sizes", "[unit][frontend][layout]") {
    CHECK(form_side_margin(0, 0) == Catch::Approx(12.0f));
    CHECK(form_side_margin(500, 200) <= (500 - 420) * 0.5f + 0.001f);
    CHECK(form_side_margin(300, 700) == Catch::Approx(12.0f));
}
