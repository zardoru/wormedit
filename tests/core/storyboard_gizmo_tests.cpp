#include <wormedit/core/storyboard_gizmo.h>
#include <wormedit/core/osbx_evaluator.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using namespace wormedit;
namespace {
void check_pair(const result_t<std::vector<double>, failure_code_t>& value, double x, double y) {
    REQUIRE(value);
    REQUIRE(value->size() == 2);
    CHECK((*value)[0] == Catch::Approx(x));
    CHECK((*value)[1] == Catch::Approx(y));
}
auto translation_drag(const visual_affine_t& world, storyboard_gizmo_axis_t axis = storyboard_gizmo_axis_t::both) {
    return storyboard_gizmo_drag_t::from(osbx_transform_kind_t::translate, world,
        std::array{10.0, 20.0}, world.apply({50, 70}), axis);
}
void check_rotation(const result_t<std::vector<double>, failure_code_t>& value, double degrees) {
    REQUIRE(value);
    REQUIRE(value->size() == 1);
    CHECK(value->front() == Catch::Approx(degrees));
}
} // namespace

TEST_CASE("Storyboard gizmo translation inverts rotated and sheared world-side space",
          "[core][storyboard-gizmo]") {
    const auto world = GENERATE(visual_affine_t::from(40, -70, 2, 3, std::numbers::pi / 2),
        visual_affine_t{1, 0.7, -0.4, 2, 90, 120});
    auto drag = translation_drag(world);
    REQUIRE(drag);
    check_pair(drag->revise(world.apply({63, 66}), false), 23, 16);
    check_pair(drag->revise(world.apply({50, 70}), false), 10, 20);
}

TEST_CASE("Storyboard gizmo Shift and axis handles constrain in op-local coordinates",
          "[core][storyboard-gizmo]") {
    const auto axis = GENERATE(storyboard_gizmo_axis_t::both, storyboard_gizmo_axis_t::x,
        storyboard_gizmo_axis_t::y);
    const auto world = visual_affine_t::from(40, 20, 1, 1, std::numbers::pi / 2);
    auto drag = translation_drag(world, axis);
    REQUIRE(drag);
    check_pair(drag->revise(world.apply({80, 80}), true),
        axis == storyboard_gizmo_axis_t::y ? 10 : 40,
        axis == storyboard_gizmo_axis_t::y ? 30 : 20);
}

TEST_CASE("Storyboard gizmo rotation retains authored turns and accumulates across angle wrapping",
          "[core][storyboard-gizmo]") {
    auto drag = storyboard_gizmo_drag_t::from(osbx_transform_kind_t::rotate, {}, std::array{720.0}, {1, 0});
    REQUIRE(drag);
    check_rotation(drag->revise({0, 1}, false), 810);
    check_rotation(drag->revise({-1, 0}, false), 900);
    check_rotation(drag->revise({0, -1}, false), 990);
    check_rotation(drag->revise({1, 0}, false), 1080);
}

TEST_CASE("Storyboard gizmo Shift rotation rounds the drag to fifteen degree steps",
          "[core][storyboard-gizmo]") {
    const auto world = visual_affine_t{1, 0.8, -0.3, 2, 40, 90};
    auto drag = storyboard_gizmo_drag_t::from(osbx_transform_kind_t::rotate, world,
        std::array{12.0}, world.apply({1, 0}));
    REQUIRE(drag);
    const auto angle = 22.0 * std::numbers::pi / 180;
    check_rotation(drag->revise(world.apply({std::cos(angle), std::sin(angle)}), true), 27);
}

TEST_CASE("Storyboard gizmo scaling uses local ratios and Shift preserves the starting aspect",
          "[core][storyboard-gizmo]") {
    const auto world = visual_affine_t{2, 1, 0.3, -1, 40, 90};
    auto drag = storyboard_gizmo_drag_t::from(osbx_transform_kind_t::scale, world,
        std::array{2.0, -3.0}, world.apply({10, 20}));
    REQUIRE(drag);
    check_pair(drag->revise(world.apply({20, 30}), false), 4, -4.5);
    check_pair(drag->revise(world.apply({20, 30}), true), 4, -6);
    auto exact = storyboard_gizmo_drag_t::from(osbx_transform_kind_t::scale,
        visual_affine_t{1, 0, 0, 1, 0, 0}, std::array{2.0, -3.0}, {10, 20});
    REQUIRE(exact);
    CHECK_FALSE(exact->revise({0, 30}, false));
}

TEST_CASE("Storyboard gizmo pivot point maps through its world-side factor",
          "[core][storyboard-gizmo]") {
    const auto world = visual_affine_t{1, 1, -0.5, 2, 30, -20};
    auto drag = storyboard_gizmo_drag_t::from(osbx_transform_kind_t::pivot, world,
        std::array{10.0, 20.0}, world.apply({10, 20}));
    REQUIRE(drag);
    check_pair(drag->revise(world.apply({18, 17}), false), 18, 17);
}

TEST_CASE("Storyboard gizmos refuse singular nonfinite and inspector-only operations",
          "[core][storyboard-gizmo][negative]") {
    const auto kind = GENERATE(osbx_transform_kind_t::translate, osbx_transform_kind_t::scale);
    const std::array value{1.0, 1.0};
    CHECK_FALSE(storyboard_gizmo_drag_t::from(kind, {1, 2, 2, 4, 0, 0}, value, {1, 1}));
    CHECK_FALSE(storyboard_gizmo_drag_t::from(kind, {}, value,
        {std::numeric_limits<double>::infinity(), 1}));
    CHECK_FALSE(storyboard_gizmo_drag_t::from(osbx_transform_kind_t::matrix, {}, value, {1, 1}));
    CHECK_FALSE(storyboard_gizmo_drag_t::from(osbx_transform_kind_t::skew, {}, value, {1, 1}));
}

namespace {
void check_evaluated_drag(const osbx_frame_effect_t& frame, const osbx_evaluation_index_t& index,
    std::string_view op, const visual_affine_t& expected) {
    const auto space = frame.op_space(index, "node", op, storyboard_time_t{});
    REQUIRE(space);
    CHECK(space->world_side == expected);
    auto drag = storyboard_gizmo_drag_t::from(space->kind, space->world_side, space->components(),
        space->world_side.apply({10, 20}));
    REQUIRE(drag);
    check_pair(drag->revise(space->world_side.apply({23, 16}), false), 23, 16);
}
} // namespace

TEST_CASE("Storyboard gizmo query uses the evaluator world-side suffix in a noncommuting stack",
          "[core][storyboard-gizmo][osbx]") {
    const visual_affine_t shear{1, 0.7, -0.4, 2, 40, 90};
    osbx_document_t scene{.nodes = {{.id = "node", .parent = "parent", .ops = {
        {.id = "move", .kind = osbx_transform_kind_t::translate, .track = {.default_value = {10, 20}}},
        {.id = "later-matrix", .kind = osbx_transform_kind_t::matrix,
            .track = {.default_value = {0, 1, -1, 0, 0, 0}}}}},
        {.id = "parent", .ops = {{.id = "shear", .kind = osbx_transform_kind_t::matrix,
            .track = {.default_value = {1, 0.7, -0.4, 2, 40, 90}}}}}}};
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    REQUIRE(frame.is_success());
    check_evaluated_drag(frame, *index.value, "move", shear * visual_affine_t{0, 1, -1, 0, 0, 0});
}

TEST_CASE("Storyboard gizmo query includes enclosing pivot and nested later ops",
          "[core][storyboard-gizmo][osbx]") {
    osbx_document_t scene{.nodes = {{.id = "node", .ops = {
        {.id = "pivot", .kind = osbx_transform_kind_t::pivot, .track = {.default_value = {40, 90}},
            .children = {
                {.id = "move", .kind = osbx_transform_kind_t::translate, .track = {.default_value = {10, 20}}},
                {.id = "scale", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {2, 3}}}}}}}}};
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    REQUIRE(frame.is_success());
    check_evaluated_drag(frame, *index.value, "move", visual_affine_t::from(40, 90, 2, 3));
}
