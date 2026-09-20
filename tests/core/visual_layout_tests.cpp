#include <wormedit/core/visual_layout.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("visual image layout makes stretch contain and cover policy explicit",
          "[unit][core][visual-layout]") {
    using namespace wormedit;
    const visual_destination_rect_t destination{
        .left = 10.0, .top = 20.0, .width = 300.0, .height = 300.0};

    const auto stretch = make_visual_image_layout({.source_width = 400,
                                                   .source_height = 200,
                                                   .destination = destination,
                                                   .fit = visual_fit_policy_t::stretch});
    REQUIRE(stretch);
    CHECK(stretch->destination == destination);
    CHECK(stretch->source == visual_source_rect_t{0, 0, 400, 200});

    const auto contain = make_visual_image_layout({.source_width = 400,
                                                   .source_height = 200,
                                                   .destination = destination,
                                                   .fit = visual_fit_policy_t::contain});
    REQUIRE(contain);
    CHECK(contain->destination.left == Catch::Approx(10.0));
    CHECK(contain->destination.top == Catch::Approx(95.0));
    CHECK(contain->destination.width == Catch::Approx(300.0));
    CHECK(contain->destination.height == Catch::Approx(150.0));
    CHECK(contain->source == visual_source_rect_t{0, 0, 400, 200});

    const auto cover = make_visual_image_layout({.source_width = 400,
                                                 .source_height = 200,
                                                 .destination = destination,
                                                 .fit = visual_fit_policy_t::cover});
    REQUIRE(cover);
    CHECK(cover->destination == destination);
    CHECK(cover->source == visual_source_rect_t{100, 0, 200, 200});
}

TEST_CASE("visual cover layout center-crops portrait and odd-sized sources deterministically",
          "[unit][core][visual-layout]") {
    using namespace wormedit;
    const auto portrait =
        make_visual_image_layout({.source_width = 200,
                                  .source_height = 401,
                                  .destination = {.width = 100.0, .height = 100.0},
                                  .fit = visual_fit_policy_t::cover});
    REQUIRE(portrait);
    CHECK(portrait->source == visual_source_rect_t{0, 100, 200, 200});

    const auto landscape =
        make_visual_image_layout({.source_width = 401,
                                  .source_height = 200,
                                  .destination = {.width = 100.0, .height = 100.0},
                                  .fit = visual_fit_policy_t::cover});
    REQUIRE(landscape);
    CHECK(landscape->source == visual_source_rect_t{100, 0, 200, 200});
}

TEST_CASE("visual image layout rejects invalid source destination and policy",
          "[unit][core][visual-layout][negative]") {
    using namespace wormedit;
    CHECK_FALSE(make_visual_image_layout(
        {.source_width = 0, .source_height = 1, .destination = {.width = 1, .height = 1}}));
    CHECK_FALSE(make_visual_image_layout(
        {.source_width = 1, .source_height = 1, .destination = {.width = 0, .height = 1}}));
    CHECK_FALSE(make_visual_image_layout(
        {.source_width = 1,
         .source_height = 1,
         .destination = {
             .left = std::numeric_limits<double>::quiet_NaN(), .width = 1, .height = 1}}));
    CHECK_FALSE(make_visual_image_layout({.source_width = 1,
                                          .source_height = 1,
                                          .destination = {.width = 1, .height = 1},
                                          .fit = static_cast<visual_fit_policy_t>(255)}));
}
