#include "ui/nuklear/chrome/content_margin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using wormedit::ui::layout::form_side_margin;
using wormedit::ui::layout::maximum_column_width;

TEST_CASE("Form columns stop growing on wide windows", "[unit][frontend][layout]") {
    CHECK(form_side_margin(1920) == Catch::Approx((1920.0f - maximum_column_width) * 0.5f));
    CHECK(form_side_margin(2560) == Catch::Approx((2560.0f - maximum_column_width) * 0.5f));
    CHECK(1920.0f - 2.0f * form_side_margin(1920) == Catch::Approx(maximum_column_width));
}

TEST_CASE("Form columns fill narrow windows and handle bad sizes", "[unit][frontend][layout]") {
    CHECK(form_side_margin(1366) == Catch::Approx(63.0f));
    CHECK(form_side_margin(1280) == Catch::Approx(32.0f));
    CHECK(form_side_margin(1024) == Catch::Approx(32.0f));
    CHECK(form_side_margin(0) == Catch::Approx(32.0f));
    CHECK(form_side_margin(-5) == Catch::Approx(32.0f));
}
