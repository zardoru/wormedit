#include <wormedit/core/span_manipulation.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("dragging an edge moves it to the pointer and swaps when edges cross",
          "[unit][core][span]") {
    const span_rows_t span{100, 200};
    CHECK(manipulate_span(span, span_handle_t::begin, 100, 150, 1000) == span_rows_t{150, 200});
    CHECK(manipulate_span(span, span_handle_t::begin, 100, 260, 1000) == span_rows_t{200, 260});
    CHECK(manipulate_span(span, span_handle_t::end, 200, 40, 1000) == span_rows_t{40, 100});
    CHECK_FALSE(manipulate_span(span, span_handle_t::end, 200, 100, 1000));
    CHECK(manipulate_span(span, span_handle_t::end, 200, 100, 1000, {.allow_empty = true}) ==
          span_rows_t{100, 100});
}

TEST_CASE("dragging the body keeps the length and clamps inside the chart", "[unit][core][span]") {
    const span_rows_t span{100, 200};
    CHECK(manipulate_span(span, span_handle_t::body, 150, 170, 1000) == span_rows_t{120, 220});
    CHECK(manipulate_span(span, span_handle_t::body, 150, 10, 1000) == span_rows_t{0, 100});
    CHECK(manipulate_span(span, span_handle_t::body, 150, 990, 1000) == span_rows_t{900, 1000});
    CHECK(manipulate_span(span, span_handle_t::body, 150, 150, 1000) == span_rows_t{100, 200});
}

TEST_CASE("an instant span can be stretched and a span outside the chart is refused",
          "[unit][core][span]") {
    CHECK(manipulate_span({300, 300}, span_handle_t::end, 300, 360, 1000, {.allow_empty = true}) ==
          span_rows_t{300, 360});
    CHECK_FALSE(manipulate_span({900, 1200}, span_handle_t::body, 950, 960, 1000));
}
