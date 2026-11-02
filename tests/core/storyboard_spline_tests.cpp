#include <wormedit/core/storyboard_spline.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

using namespace wormedit;
namespace {
auto arch() -> storyboard_spline_t {
    return {.begin_ms = -100, .end_ms = 900, .points = {{{0, 0}, {0, 100}, {100, 100}, {100, 0}}}};
}
void check_endpoints(const storyboard_spline_t& curve, std::span<const storyboard_spline_key_t> keys) {
    CHECK(keys.front().time_ms == curve.begin_ms); CHECK(keys.back().time_ms == curve.end_ms);
    CHECK(keys.front().value == curve.points.front()); CHECK(keys.back().value == curve.points.back());
}
void check_continuous_error(const storyboard_spline_t& curve, std::span<const storyboard_spline_key_t> keys) {
    std::size_t segment = 0;
    for (int i = 0; i <= 4000; ++i) {
        const double t = static_cast<double>(i) / 4000, u = 1 - t;
        const double time = std::lerp(static_cast<double>(curve.begin_ms), static_cast<double>(curve.end_ms), t);
        while (segment + 2 < keys.size() && keys[segment + 1].time_ms < time) ++segment;
        const auto& a = keys[segment]; const auto& b = keys[segment + 1];
        const double p = (time - static_cast<double>(a.time_ms)) / static_cast<double>(b.time_ms - a.time_ms);
        double error = 0;
        for (std::size_t d = 0; d < curve.dimensions; ++d) {
            const double exact = u*u*u*curve.points[0][d] + 3*u*u*t*curve.points[1][d] +
                3*u*t*t*curve.points[2][d] + t*t*t*curve.points[3][d];
            error = std::hypot(error, exact - std::lerp(a.value[d], b.value[d], p));
        }
        CHECK(error <= curve.tolerance + 1e-10);
    }
}
}
TEST_CASE("Storyboard Bezier bakes a curved translation with bounded continuous error", "[unit][storyboard-spline]") {
    const auto curve = arch(); const auto result = curve.bake(); REQUIRE(result);
    REQUIRE(result->size() > 2);
    check_endpoints(curve, *result);
    const auto middle = std::ranges::find(*result, 400, &storyboard_spline_key_t::time_ms); REQUIRE(middle != result->end());
    CHECK(middle->value[0] == Catch::Approx(50)); CHECK(middle->value[1] == Catch::Approx(75));
    check_continuous_error(curve, *result);
}
TEST_CASE("Storyboard Bezier hull catches S curves with a straight midpoint", "[unit][storyboard-spline]") {
    auto curve = arch(); curve.points = {{{0,0}, {0,100}, {100,-100}, {100,0}}};
    const auto result = curve.bake(); REQUIRE(result); CHECK(result->size() > 2); check_continuous_error(curve, *result);
}
TEST_CASE("Storyboard Bezier supports continuous rotation and scale without wrapping", "[unit][storyboard-spline]") {
    auto curve = arch(); curve.dimensions = 1; curve.points = {{{0,0}, {120,0}, {240,0}, {360,0}}};
    const auto rotation = curve.bake(); REQUIRE(rotation); REQUIRE(rotation->size() == 2); CHECK(rotation->back().value[0] == 360);
    curve.dimensions = 2; curve.points = {{{1,1}, {2,0.5}, {0.5,2}, {3,3}}}; curve.tolerance = 0.001;
    const auto scale = curve.bake(); REQUIRE(scale); check_continuous_error(curve, *scale);
}
TEST_CASE("Storyboard Bezier refuses impossible precision invalid values and exhausted work", "[unit][storyboard-spline]") {
    auto curve = arch(); curve.end_ms = curve.begin_ms + 1; CHECK_FALSE(curve.bake());
    curve = arch(); curve.tolerance = 0; CHECK_FALSE(curve.bake());
    curve = arch(); curve.points[1][0] = std::numeric_limits<double>::infinity(); CHECK_FALSE(curve.bake());
    curve = arch(); CHECK_FALSE(curve.bake([] { return false; }));
    curve.end_ms = std::numeric_limits<std::int64_t>::max(); CHECK_FALSE(curve.bake());
}

TEST_CASE("Storyboard Bezier enforces key capacity and preserves extreme signed epochs", "[unit][storyboard-spline]") {
    auto curve = arch(); curve.end_ms = static_cast<std::int64_t>(storyboard_spline_t::maximum_duration_ms) - 100;
    curve.tolerance = 0.000001;
    const auto too_dense = curve.bake(); REQUIRE_FALSE(too_dense);
    CHECK(too_dense.error().message.find("4096") != std::string::npos);
    curve = arch(); curve.begin_ms = std::numeric_limits<std::int64_t>::max() - 1000;
    curve.end_ms = std::numeric_limits<std::int64_t>::max();
    const auto late = curve.bake(); REQUIRE(late);
    CHECK(late->front().time_ms == curve.begin_ms); CHECK(late->back().time_ms == curve.end_ms);
}

TEST_CASE("Storyboard Bezier refuses accuracy below numeric resolution", "[unit][storyboard-spline]") {
    auto curve = arch(); curve.dimensions = 1;
    curve.points = {{{1e16,0},{1e16 + 2,0},{1e16 + 2,0},{1e16,0}}};
    const auto result = curve.bake(); REQUIRE_FALSE(result);
    CHECK(result.error().code == failure_code_t::invalid_request);
    CHECK(result.error().message.find("precision") != std::string::npos);
}
