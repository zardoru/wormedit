#include <wormedit/core/natural_order.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("natural name order compares digit runs and breaks ties deterministically",
          "[unit][core][natural-order]") {
    CHECK(natural_name_less("snd2", "snd10"));
    CHECK(natural_name_less("a1b2", "a1b10"));
    CHECK(natural_name_less("snd01", "snd1"));
}

TEST_CASE("natural name order uses folded text and raw bytes for ties",
          "[unit][core][natural-order]") {
    CHECK(natural_name_less("a01", "A1"));
    CHECK(natural_name_less("Alpha", "alpha"));
    CHECK_FALSE(natural_name_less("alpha", "Alpha"));
    CHECK_FALSE(natural_name_less("snd10", "snd2"));
}
