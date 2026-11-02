#include <wormedit/core/osbx_export_metric.h>
#include <wormedit/core/keyframe_simplify.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {
using namespace wormedit;
auto scene() -> osbx_document_t {
    return {.images = {{.id = "image", .file = "sprite.png"}},
        .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
            .ops = {{.id = "position", .track = {.default_value = {0,0}, .keys = {
                {.id = "first", .value = {0,0}}, {.id = "last", .time_ms = 2, .value = {2,0}}}}}}}}};
}
const std::array sizes{osbx_osb_image_size_t{.image_id = "image", .width = 64, .height = 48}};
} // namespace

TEST_CASE("OSBX compiled clip property and clock work is charged before export sampling", "[osbx][export][clips]") {
    auto document = scene();
    const auto plain = osbx_evaluation_index_t::from(document); REQUIRE(plain.value);
    const auto baseline = osbx_export_budget_t::from(*plain.value, 1, 1000).frame_work;
    document.clips = {{.id = "clip"}};
    document.nodes[0].uses = {{.id = "use", .clip = "clip"}};
    const auto index = osbx_evaluation_index_t::from(document); REQUIRE(index.value);
    auto budget = osbx_export_budget_t::from(*index.value, 1, baseline);
    CHECK(budget.frame_work > baseline);
    frame_arena_t arena;
    CHECK_FALSE(osbx_export_frame_t::from(*index.value, {}, arena, budget));
}

namespace {
void check_fractional_export_sample(const osbx_export_frame_t& sample) {
    REQUIRE(sample.instances.size() == 1);
    CHECK(sample.instances[0].world[4] == Catch::Approx(0.5));
}
}

TEST_CASE("OSBX export samples copy fractional state without borrowing frame arena values", "[osbx][export]") {
    const auto document = scene();
    const auto index = osbx_evaluation_index_t::from(document);
    REQUIRE(index.value);
    auto budget = osbx_export_budget_t::from(*index.value, document.images.size(), 100);
    frame_arena_t arena;
    const auto sample = osbx_export_frame_t::from(*index.value,
        {.milliseconds = 0, .remainder = 1, .denominator = 2}, arena, budget);
    REQUIRE(sample);
    check_fractional_export_sample(*sample);
    const auto next = osbx_export_frame_t::from(*index.value, {.milliseconds = 1}, arena, budget);
    REQUIRE(next);
    CHECK(sample->instances[0].world[4] == Catch::Approx(0.5));
    CHECK(next->instances[0].world[4] == Catch::Approx(1));
}

TEST_CASE("OSBX export normalized metric retains singular maps opacity and identity refusal", "[osbx][export]") {
    osbx_draw_instance_t expected{.id = "sprite", .world = {0,0,0,0,0,0}, .image = "image", .origin = "Centre"};
    auto actual = expected;
    const visual_image_quad_request_t request{.source_width = 64, .source_height = 48,
        .destination = {.width = 640, .height = 480}};
    CHECK(osbx_export_instance_error(expected, actual, request) == 0);
    actual.world.values[4] = simplify_corner_error_px;
    CHECK(osbx_export_instance_error(expected, actual, request) == Catch::Approx(1));
    actual = expected;
    actual.opacity = 1 - simplify_opacity_error;
    CHECK(osbx_export_instance_error(expected, actual, request) == Catch::Approx(1));
    actual.image = "different";
    CHECK(std::isinf(osbx_export_instance_error(expected, actual, request)));
}

TEST_CASE("OSBX export sample work and storage bounds refuse before returning partial frames", "[osbx][export]") {
    const auto document = scene();
    const auto index = osbx_evaluation_index_t::from(document);
    REQUIRE(index.value);
    auto budget = osbx_export_budget_t::from(*index.value, document.images.size(), 0);
    frame_arena_t arena;
    CHECK_FALSE(osbx_export_frame_t::from(*index.value, {}, arena, budget));
    budget = osbx_export_budget_t::from(*index.value, document.images.size(), 100);
    budget.maximum_frame_bytes = 1;
    CHECK_FALSE(osbx_export_frame_t::from(*index.value, {}, arena, budget));
}

TEST_CASE("OSBX frame metric refuses missing objects and missing decoded geometry", "[osbx][export]") {
    const auto document = scene();
    osbx_export_frame_t expected{.instances = {{.id = "sprite", .image = "image", .origin = "Centre"}}};
    auto actual = expected;
    CHECK(osbx_export_frame_error(expected, actual, document, sizes) == 0);
    CHECK(std::isinf(osbx_export_frame_error(expected, actual, document, {})));
    actual.instances.clear();
    CHECK(std::isinf(osbx_export_frame_error(expected, actual, document, sizes)));
}

TEST_CASE("OSBX frame metric shares the contained logical canvas pixel units", "[osbx][export]") {
    auto document = scene();
    document.canvas.width = 1280;
    document.canvas.height = 960;
    osbx_export_frame_t expected{.instances = {{.id = "sprite", .image = "image", .origin = "Centre"}}};
    auto actual = expected;
    actual.instances.front().world.values[4] = 1;
    CHECK(osbx_export_frame_error(expected, actual, document, sizes) == Catch::Approx(1));
    actual.instances.front().world.values[4] = std::numeric_limits<double>::infinity();
    CHECK(std::isinf(osbx_export_frame_error(expected, actual, document, sizes)));
}

TEST_CASE("OSBX export work saturation never wraps into an available frame", "[osbx][export][clips]") {
    const auto document = scene();
    const auto index = osbx_evaluation_index_t::from(document); REQUIRE(index.value);
    auto budget = osbx_export_budget_t::from(*index.value, SIZE_MAX, SIZE_MAX);
    CHECK(budget.frame_work == SIZE_MAX); CHECK(budget.remaining_work == 0);
    frame_arena_t arena;
    CHECK_FALSE(osbx_export_frame_t::from(*index.value, {}, arena, budget));
}

TEST_CASE("OSBX temporal query work is charged without multiplying loop count", "[osbx][program][export]") {
    auto document = scene();
    const auto plain = osbx_evaluation_index_t::from(document);
    REQUIRE(plain.value);
    const auto baseline = osbx_export_budget_t::from(*plain.value, 1, 1000).frame_work;
    document.nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = UINT32_MAX,
        .keys = {{.id = "constant", .value = {0.5}}}}}};
    const auto index = osbx_evaluation_index_t::from(document);
    REQUIRE(index.value);
    const auto cost = osbx_export_budget_t::from(*index.value, 1, 1000).frame_work;
    CHECK(cost > baseline);
    CHECK(cost < 1000);
}

TEST_CASE("shared export metric refuses a different Animation frame even with identical transforms", "[osbx][metric][animation]") {
    wormedit::osbx_draw_instance_t expected{.id = "hero", .image = "frames", .origin = "Centre", .frame_index = 0};
    auto actual = expected; actual.frame_index = 1;
    const wormedit::visual_image_quad_request_t request{.source_width = 2, .source_height = 1,
        .destination = {.width = 640, .height = 480}, .origin_x = 0.5, .origin_y = 0.5};
    CHECK(std::isinf(wormedit::osbx_export_instance_error(expected, actual, request)));
}

namespace {
void check_animation_frames(const wormedit::osbx_export_frame_t& first, const wormedit::osbx_export_frame_t& second) {
    CHECK(first.instances[0].frame_index == 1);
    CHECK(second.instances[0].frame_index == 2);
}
}
TEST_CASE("Animation frame identity survives export arena reuse and charges exact arithmetic work", "[osbx][metric][animation]") {
    using namespace wormedit;
    osbx_document_t source{.images = {{.id = "frames", .file = "hero.png"}},
        .nodes = {{.id = "hero", .kind = osbx_node_kind_t::animation, .image = "frames",
            .animation = storyboard_animation_t{.frame_count = 3, .frame_delay_ms = 12.5}}}};
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    auto budget = osbx_export_budget_t::from(*index.value, 1, 20'000);
    CHECK(budget.frame_work >= 2048);
    frame_arena_t arena;
    const auto first = osbx_export_frame_t::from(*index.value, {.milliseconds = 12, .remainder = 1, .denominator = 2}, arena, budget);
    REQUIRE(first); REQUIRE(first->instances.size() == 1);
    const auto second = osbx_export_frame_t::from(*index.value, {.milliseconds = 25}, arena, budget);
    REQUIRE(second);
    check_animation_frames(*first, *second);
}
