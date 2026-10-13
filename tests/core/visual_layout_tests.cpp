#include <wormedit/core/visual_layout.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("visual image layout makes stretch contain and cover policy explicit",
          "[unit][core][visual-layout]") {
    using namespace wormedit;
    const visual_destination_rect_t destination{
        .left = 10.0, .top = 20.0, .width = 300.0, .height = 300.0};

    const auto stretch = visual_image_layout_t::from({.source_width = 400,
                                                   .source_height = 200,
                                                   .destination = destination,
                                                   .fit = visual_fit_policy_t::stretch});
    REQUIRE(stretch);
    CHECK(stretch->destination == destination);
    CHECK(stretch->source == visual_source_rect_t{0, 0, 400, 200});

    const auto contain = visual_image_layout_t::from({.source_width = 400,
                                                   .source_height = 200,
                                                   .destination = destination,
                                                   .fit = visual_fit_policy_t::contain});
    REQUIRE(contain);
    CHECK(contain->destination.left == Catch::Approx(10.0));
    CHECK(contain->destination.top == Catch::Approx(95.0));
    CHECK(contain->destination.width == Catch::Approx(300.0));
    CHECK(contain->destination.height == Catch::Approx(150.0));
    CHECK(contain->source == visual_source_rect_t{0, 0, 400, 200});

    const auto cover = visual_image_layout_t::from({.source_width = 400,
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
        visual_image_layout_t::from({.source_width = 200,
                                  .source_height = 401,
                                  .destination = {.width = 100.0, .height = 100.0},
                                  .fit = visual_fit_policy_t::cover});
    REQUIRE(portrait);
    CHECK(portrait->source == visual_source_rect_t{0, 100, 200, 200});

    const auto landscape =
        visual_image_layout_t::from({.source_width = 401,
                                  .source_height = 200,
                                  .destination = {.width = 100.0, .height = 100.0},
                                  .fit = visual_fit_policy_t::cover});
    REQUIRE(landscape);
    CHECK(landscape->source == visual_source_rect_t{100, 0, 200, 200});
}

TEST_CASE("visual image layout rejects invalid source destination and policy",
          "[unit][core][visual-layout][negative]") {
    using namespace wormedit;
    CHECK_FALSE(visual_image_layout_t::from(
        {.source_width = 0, .source_height = 1, .destination = {.width = 1, .height = 1}}));
    CHECK_FALSE(visual_image_layout_t::from(
        {.source_width = 1, .source_height = 1, .destination = {.width = 0, .height = 1}}));
    CHECK_FALSE(visual_image_layout_t::from(
        {.source_width = 1,
         .source_height = 1,
         .destination = {
             .left = std::numeric_limits<double>::quiet_NaN(), .width = 1, .height = 1}}));
    CHECK_FALSE(visual_image_layout_t::from({.source_width = 1,
                                          .source_height = 1,
                                          .destination = {.width = 1, .height = 1},
                                          .fit = static_cast<visual_fit_policy_t>(255)}));
}

TEST_CASE("visual image quad rotates about its origin and preserves signed flips",
          "[unit][core][visual-layout][storyboard]") {
    using namespace wormedit;
    constexpr auto half_pi = 1.57079632679489661923;
    const auto rotated = visual_image_quad_t::from(
        {.source_width = 20,
         .source_height = 10,
         .destination = {.left = 10.0, .top = 20.0, .width = 640.0, .height = 480.0},
         .x = 100.0,
         .y = 50.0,
         .origin_x = 0.5,
         .origin_y = 0.5,
         .rotation_radians = half_pi});
    REQUIRE(rotated);
    CHECK(rotated->points[0].x == Catch::Approx(115.0));
    CHECK(rotated->points[0].y == Catch::Approx(60.0));
    CHECK(rotated->points[1].x == Catch::Approx(115.0));
    CHECK(rotated->points[1].y == Catch::Approx(80.0));
    CHECK(rotated->points[2].x == Catch::Approx(105.0));
    CHECK(rotated->points[2].y == Catch::Approx(80.0));
    CHECK(rotated->points[3].x == Catch::Approx(105.0));
    CHECK(rotated->points[3].y == Catch::Approx(60.0));

    const auto flipped = visual_image_quad_t::from({.source_width = 20,
                                                 .source_height = 10,
                                                 .destination = {.width = 320.0, .height = 240.0},
                                                 .x = 100.0,
                                                 .y = 50.0,
                                                 .origin_x = 0.0,
                                                 .origin_y = 0.0,
                                                 .scale_x = -2.0,
                                                 .scale_y = 3.0});
    REQUIRE(flipped);
    CHECK(flipped->points[0].x == Catch::Approx(50.0));
    CHECK(flipped->points[0].y == Catch::Approx(25.0));
    CHECK(flipped->points[1].x == Catch::Approx(30.0));
    CHECK(flipped->points[1].y == Catch::Approx(25.0));
    CHECK(flipped->points[2].x == Catch::Approx(30.0));
    CHECK(flipped->points[2].y == Catch::Approx(40.0));
}

TEST_CASE("visual image quad rejects invalid canvas transforms",
          "[unit][core][visual-layout][storyboard][negative]") {
    using namespace wormedit;
    CHECK_FALSE(visual_image_quad_t::from({.source_width = 1,
                                        .source_height = 1,
                                        .destination = {.width = 1, .height = 1},
                                        .logical_width = 0.0}));
    CHECK_FALSE(visual_image_quad_t::from({.source_width = 1,
                                        .source_height = 1,
                                        .destination = {.width = 1, .height = 1},
                                        .origin_x = 2.0}));
    CHECK_FALSE(visual_image_quad_t::from({.source_width = 1,
                                        .source_height = 1,
                                        .destination = {.width = 1, .height = 1},
                                        .scale_y = 0.0}));
    CHECK_FALSE(
        visual_image_quad_t::from({.source_width = 1,
                                .source_height = 1,
                                .destination = {.width = 1, .height = 1},
                                .rotation_radians = std::numeric_limits<double>::infinity()}));
}
