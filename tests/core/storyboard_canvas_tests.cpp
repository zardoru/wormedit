#include <wormedit/core/storyboard_canvas.h>
#include <wormedit/core/storyboard_vocabulary.h>
#include <wormedit/core/storyboard_preview.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <numbers>

using namespace wormedit;
namespace {
void check_canvas_coordinate_mapping(const storyboard_canvas_geometry_t& geometry, double zoom) {
    CHECK(geometry.scale == Catch::Approx(1.25 * zoom));
    const auto screen = geometry.to_screen({73, 219});
    CHECK(geometry.to_canvas(screen).x == Catch::Approx(73));
    CHECK(geometry.to_canvas(screen).y == Catch::Approx(219));
    CHECK(geometry.contains({10, 20}));
    CHECK_FALSE(geometry.contains({811, 20}));
}

void check_transformed_hit_bounds(const storyboard_canvas_target_t& target, const visual_affine_t& world) {
    CHECK(target.contains(world.apply({0, 0})));
    CHECK(target.contains(world.apply({49, -29})));
    CHECK_FALSE(target.contains(world.apply({51, 0})));
    CHECK_FALSE(target.contains(world.apply({0, -31})));
}

void check_world_inverse_roundtrip(const visual_affine_t& world) {
    const auto inverse = world.inverse();
    REQUIRE(inverse);
    CHECK(inverse->apply(world.apply({10, 20})).x == Catch::Approx(10));
    CHECK(inverse->apply(world.apply({10, 20})).y == Catch::Approx(20));
}

void check_shared_frame_clock_bounds() {
    const auto time = storyboard_time_t::from_frame(1, 48000);
    REQUIRE(time);
    CHECK(*time == storyboard_time_t{0, 1, 48});
    CHECK_FALSE(storyboard_time_t::from_frame(1, 0));
    CHECK_FALSE(storyboard_time_t::from_frame(1, 1, 0));
    CHECK_FALSE(storyboard_time_t::from_frame(std::numeric_limits<std::uint64_t>::max(), 1));
}

void check_origin_catalog_entry(const storyboard_origin_entry_t& entry) {
    const auto origin = storyboard_origin_t::from(entry.name);
    REQUIRE(origin);
    CHECK(origin->x == entry.value.x);
    CHECK(origin->y == entry.value.y);
}

void check_draw_instance_identity(const storyboard_draw_instance_t& good) {
    auto bad = good; bad.stable_id = 0; CHECK_FALSE(bad.is_valid());
    bad = good; bad.layer_order = 5; CHECK_FALSE(bad.is_valid());
    bad = good; bad.layer = {}; CHECK_FALSE(bad.is_valid());
    bad = good; bad.origin = {}; CHECK_FALSE(bad.is_valid());
    bad = good; bad.path = {}; CHECK_FALSE(bad.is_valid());
}

void check_draw_instance_geometry(const storyboard_draw_instance_t& good) {
    auto bad = good; bad.transform.values[0] = std::numeric_limits<double>::infinity(); CHECK_FALSE(bad.is_valid());
    bad = good; bad.logical_width = 0; CHECK_FALSE(bad.is_valid());
    bad = good; bad.logical_height = std::numeric_limits<double>::quiet_NaN(); CHECK_FALSE(bad.is_valid());
}
} // namespace

TEST_CASE("Storyboard canvas fits and maps zoomed panned coordinates reversibly", "[core][storyboard-canvas]") {
    const auto zoom = GENERATE(0.05, 1.0, 2.0, 32.0);
    const auto geometry = storyboard_canvas_geometry_t::from({10, 20, 810, 620}, 640, 480, zoom, {31, -47});
    REQUIRE(geometry);
    check_canvas_coordinate_mapping(*geometry, zoom);
    const auto fit = storyboard_canvas_geometry_t::from({0, 0, 1280, 720}, 640, 480, 1, {});
    REQUIRE(fit);
    CHECK(fit->canvas == visual_destination_rect_t{160, 0, 960, 720});
}
TEST_CASE("Storyboard canvas rejects invalid and overflowing viewport values", "[core][storyboard-canvas]") {
    const auto bad = GENERATE(0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN());
    CHECK_FALSE(storyboard_canvas_geometry_t::from({0, 0, 640, 480}, bad, 480, 1, {}));
    CHECK_FALSE(storyboard_canvas_geometry_t::from({0, 0, 640, 480}, 640, 480, bad, {}));
    CHECK_FALSE(storyboard_canvas_geometry_t::from({}, 640, 480, 1, {}));
}
TEST_CASE("Storyboard hit tests invert rotated sheared and negative scale world matrices", "[core][storyboard-canvas]") {
    const auto world = GENERATE(visual_affine_t::from(320, 240, 2, 3, std::numbers::pi / 3),
        visual_affine_t{{1, 0.7, -0.4, 1, 27, 63}}, visual_affine_t::from(12, 31, -2, 0.5));
    const storyboard_canvas_target_t target{.node = "sprite", .world = world, .width = 100, .height = 60};
    check_transformed_hit_bounds(target, world);
    check_world_inverse_roundtrip(world);
    const auto geometry = storyboard_canvas_geometry_t::from({0, 0, 640, 480}, 640, 480, 1, {});
    REQUIRE(geometry);
    const auto quad = target.quad(*geometry, 640, 480);
    REQUIRE(quad);
    CHECK(quad->points.front().x == Catch::Approx(world.apply({-50, -30}).x));
    CHECK(quad->points.front().y == Catch::Approx(world.apply({-50, -30}).y));
}
TEST_CASE("Storyboard singular invalid and missing images cannot be hit", "[core][storyboard-canvas]") {
    CHECK_FALSE(storyboard_canvas_target_t{.world = {{1, 2, 2, 4, 0, 0}}, .width = 10, .height = 10}.contains({}));
    CHECK_FALSE(storyboard_canvas_target_t{.world = {{1, 0, 0, 1, std::numeric_limits<double>::infinity(), 0}}, .width = 10, .height = 10}.contains({}));
    CHECK_FALSE(storyboard_canvas_target_t{}.contains({}));
}
TEST_CASE("Storyboard stacked hits return topmost first and refill without stale IDs", "[core][storyboard-canvas]") {
    const std::array targets{storyboard_canvas_target_t{.node = "back", .width = 20, .height = 20},
        storyboard_canvas_target_t{.node = "front", .width = 20, .height = 20}};
    std::vector<std::string_view> hits{"stale"};
    build_storyboard_canvas_hits(targets, {}, hits);
    CHECK(hits == std::vector<std::string_view>{"front", "back"});
    build_storyboard_canvas_hits(targets, {21, 0}, hits);
    CHECK(hits.empty());
    CHECK(storyboard_canvas_target_t::legacy_node_id(47) == "osb/sprite/47");
}
TEST_CASE("Storyboard shared frame clock and origin catalog retain exact vocabulary", "[core][storyboard-canvas]") {
    check_shared_frame_clock_bounds();
    CHECK(storyboard_origin_t::catalog().size() == 9);
    for (const auto& entry : storyboard_origin_t::catalog()) {
        check_origin_catalog_entry(entry);
    }
    CHECK_FALSE(storyboard_origin_t::from("bogus"));
}

TEST_CASE("Shared draw instance validation retains player boundary checks", "[core][storyboard-canvas][negative]") {
    const storyboard_draw_instance_t good{.stable_id = 1, .layer = "Background", .origin = "Centre", .path = "sprite.png"};
    REQUIRE(good.is_valid());
    check_draw_instance_identity(good);
    check_draw_instance_geometry(good);
    auto bad = good; bad.opacity = std::numeric_limits<double>::infinity(); CHECK_FALSE(bad.is_valid());
    bad = good; bad.red = std::numeric_limits<double>::infinity(); CHECK_FALSE(bad.is_valid());
    bad = good; bad.green = std::numeric_limits<double>::infinity(); CHECK_FALSE(bad.is_valid());
    bad = good; bad.blue = std::numeric_limits<double>::infinity(); CHECK_FALSE(bad.is_valid());
}
