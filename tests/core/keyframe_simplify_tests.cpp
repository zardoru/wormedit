#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <wormedit/core/keyframe_simplify.h>
using namespace wormedit;
namespace {
const simplify_projection_t identity = [](const simplify_key_t& k) { return k.value; };
auto metric(int decimals = 1) -> simplify_metric_t {
    return {.dimensions = 1,
            .tolerance = 10,
            .project = identity,
            .emit =
                [decimals](const simplify_key_t& k) {
                    auto v = k.value;
                    v[0] = round_simplify_value(v[0], decimals);
                    return v;
                },
            .error =
                [](const simplify_key_t& k, const std::array<double, 3>& v) {
                    return std::abs(k.value[0] - v[0]) / simplify_corner_error_px;
                }};
}
} // namespace
TEST_CASE("Equal runs keep first and last including trailing run", "[core][simplify]") {
    const std::vector<simplify_key_t> keys{{0, {1}}, {1, {1}}, {2, {1}},
                                           {3, {2}}, {4, {2}}, {5, {2}}};
    REQUIRE(simplify_equal_runs(keys).value() == std::vector<std::size_t>{0, 2, 3, 5});
    CHECK(simplify_equal_runs({}).value().empty());
}
TEST_CASE("RDP uses projected mixed space distances in all dimensions", "[core][simplify]") {
    const std::vector<simplify_key_t> line{{0, {0, 0, 0}}, {100, {1, 2, 3}}, {200, {2, 4, 6}}};
    CHECK(simplify_rdp_1d(line, 0.000001, identity).value() == std::vector<std::size_t>{0, 2});
    CHECK(simplify_rdp_2d(line, 0.000001, identity).value() == std::vector<std::size_t>{0, 2});
    CHECK(simplify_rdp_3d(line, 0.000001, identity).value() == std::vector<std::size_t>{0, 2});
    const std::vector<simplify_key_t> triangle{{0, {0, 0, 0}}, {1, {0, 0, 1}}, {2, {0, 0, 0}}};
    CHECK(simplify_rdp_2d(triangle, 0.5, identity).value().size() == 2);
    CHECK(simplify_rdp_3d(triangle, 0.5, identity).value().size() == 3);
    const simplify_projection_t scale = [](const simplify_key_t& k) {
        return std::array{k.value[0] * 100, 0., 0.};
    };
    const std::vector<simplify_key_t> small{{0, {0}}, {100, {0.01}}, {200, {0}}};
    CHECK(simplify_rdp_1d(small, 0.1, scale).value().size() == 3);
}
TEST_CASE("Sine sampled every millisecond retains peaks within tolerance", "[core][simplify]") {
    std::vector<simplify_key_t> keys;
    for (int t = 0; t <= 1000; ++t)
        keys.push_back({static_cast<double>(t), {std::sin(t * 0.006283185307179586)}});
    const auto selected = simplify_rdp_1d(keys, 0.01, identity).value();
    REQUIRE(selected.size() > 2);
    std::size_t segment = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        while (segment + 1 < selected.size() - 1 && selected[segment + 1] < i)
            ++segment;
        const auto& a = keys[selected[segment]];
        const auto& b = keys[selected[segment + 1]];
        const auto value = std::lerp(a.value[0], b.value[0],
                                     (keys[i].time_ms - a.time_ms) / (b.time_ms - a.time_ms));
        CHECK(std::abs(value - keys[i].value[0]) <= 0.010001);
    }
    CHECK(simplify_rdp_1d(keys, 0.01, identity).value() == selected);
}
TEST_CASE("Metric repairs permissive RDP and verifies rounded emission", "[core][simplify]") {
    const std::vector<simplify_key_t> bump{{0, {0}}, {100, {0.8}}, {200, {0}}};
    CHECK(simplify_rdp_1d(bump, 10, identity).value().size() == 2);
    CHECK(simplify_to_metric(bump, metric()).value() == std::vector<std::size_t>{0, 1, 2});
    // Unrounded interpolation misses the middle by .49; rounding endpoints adds .04.
    const std::vector<simplify_key_t> rounded{{0, {0.04}}, {100, {0.53}}, {200, {0.04}}};
    CHECK(simplify_to_metric(rounded, metric()).value().size() == 3);
    auto opacity = metric(simplify_opacity.decimals);
    opacity.error = [](const simplify_key_t& k, const std::array<double, 3>& v) {
        return std::abs(k.value[0] - v[0]) / simplify_opacity_error;
    };
    const std::vector<simplify_key_t> impossible{{0, {0.005}}, {200, {0.005}}};
    const auto failed = simplify_to_metric(impossible, opacity);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == failure_code_t::unsupported);
}
TEST_CASE("Metric refuses excess density and accepts half-open window boundary",
          "[core][simplify]") {
    const std::vector<simplify_key_t> dense{{0, {0}}, {20, {2}}, {40, {0}}, {60, {2}}, {80, {0}}};
    const auto failed = simplify_to_metric(dense, metric());
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == failure_code_t::unsupported);
    auto boundary = dense;
    boundary.back().time_ms = 100;
    CHECK(simplify_to_metric(boundary, metric()).has_value());
}
TEST_CASE("Deep RDP is iterative bounded and deterministic", "[core][simplify]") {
    std::vector<simplify_key_t> keys;
    for (std::size_t i = 0; i < 4096; ++i)
        keys.push_back({static_cast<double>(i), {static_cast<double>(i % 2)}});
    const auto first = simplify_rdp_1d(keys, 0.1, identity, {.maximum_work = 12'000'000});
    REQUIRE(first);
    CHECK(first->size() == keys.size());
    CHECK(simplify_rdp_1d(keys, 0.1, identity, {.maximum_work = 12'000'000}) == first);
    CHECK_FALSE(simplify_rdp_1d(keys, 0.1, identity, {.maximum_work = 10}));
    CHECK_FALSE(simplify_rdp_1d(keys, 0.1, identity, {.consume_work = [] { return false; }}));
}
TEST_CASE("Simplification rejects unordered nonfinite and invalid requests", "[core][simplify]") {
    const std::vector<simplify_key_t> duplicate{{0, {0}}, {0, {1}}};
    CHECK_FALSE(simplify_rdp_1d(duplicate, 0, identity));
    const std::vector<simplify_key_t> nan{{0, {std::numeric_limits<double>::quiet_NaN()}}};
    CHECK_FALSE(simplify_equal_runs(nan));
    CHECK_FALSE(simplify_rdp_1d({}, -1, identity));
    CHECK(simplify_rdp_1d({}, 0, identity)->empty());
    const std::vector<simplify_key_t> singleton{{0, {1}}};
    CHECK(simplify_to_metric(singleton, metric())->size() == 1);
}

TEST_CASE("Emit decimals round ties to even independently of sign", "[core][simplify]") {
    CHECK(round_simplify_value(0.25, 1) == 0.2);
    CHECK(round_simplify_value(0.35, 1) == 0.4);
    CHECK(round_simplify_value(-0.25, 1) == -0.2);
    CHECK(round_simplify_value(-0.35, 1) == -0.4);
    CHECK(std::isnan(round_simplify_value(1, 16)));
}
