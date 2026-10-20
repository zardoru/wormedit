#include "sys/compose_scroll_geometry.h"
#include "sys/compose_timing_target.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit::sys;

TEST_CASE("downscroll mirrors screen y into source order", "[unit][sys][compose]") {
    CHECK(compose_source_y(100.0, 800.0, false) == 100.0);
    CHECK(compose_source_y(100.0, 800.0, true) == 700.0);
    CHECK(compose_source_fraction(200.0, 800.0, false) == 0.25);
    CHECK(compose_source_fraction(200.0, 800.0, true) == 0.75);
    CHECK(compose_source_fraction(-5.0, 800.0, false) == 0.0);
    CHECK(compose_source_fraction(900.0, 800.0, false) == 1.0);
    CHECK(compose_source_fraction(10.0, 0.0, true) == 0.0);
}

TEST_CASE("timing target maps raw pointer y to a document fraction", "[unit][sys][compose]") {
    const wormedit::ui::timing_target_t up{200.0, 800.0, 0.5, 1.0, false};
    CHECK(timing_target_fraction(up) == 0.625);
    const wormedit::ui::timing_target_t down{200.0, 800.0, 0.5, 1.0, true};
    CHECK(timing_target_fraction(down) == 0.875);
}
