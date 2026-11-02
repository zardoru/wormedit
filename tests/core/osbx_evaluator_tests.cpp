#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/storyboard.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <limits>
#include <new>
#include <tuple>

namespace {
auto clip_scene() -> wormedit::osbx_document_t {
    using namespace wormedit;
    return {.images = {{.id = "image", .file = "image.png"}}, .nodes = {
        {.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
            .uses = {{.id = "use", .clip = "motion", .start_ms = 10, .time_scale = 0.5}}}},
        .clips = {{.id = "motion", .ops = {{.id = "motion-op", .track = {
            .default_value = {0, 0}, .keys = {{.id = "begin", .value = {0, 0}},
                {.id = "end", .time_ms = 10, .value = {20, 0}}}}}}}}};
}

void check_hidden_frame_storage(const wormedit::osbx_frame_effect_t& frame) {
    REQUIRE(frame.is_success());
    CHECK(frame.instances.empty());
    REQUIRE(frame.node_states.size() == 2);
}

void check_same_pass_node_identity(const wormedit::osbx_frame_effect_t& frame, const wormedit::osbx_node_frame_t* group,
    const wormedit::osbx_node_frame_t* hidden) {
    REQUIRE(group); REQUIRE(hidden);
    CHECK(group == &frame.node_states[0]);
    CHECK(hidden == &frame.node_states[1]);
}
void check_clip_translation(const wormedit::osbx_frame_effect_t& frame, double expected) {
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    CHECK(frame.instances[0].world[4] == Catch::Approx(expected));
}
void check_clip_clock_capacity_refusals(const wormedit::osbx_clip_clock_t& clock) {
    CHECK_FALSE(clock.compose({.time_scale = std::numeric_limits<double>::denorm_min()}));
    CHECK_FALSE(wormedit::osbx_clip_clock_t{.origin = {.milliseconds = INT64_MAX}}.map(1));
}
void check_bound_clip_properties(const wormedit::osbx_frame_effect_t& frame) {
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    CHECK(frame.instances[0].world[4] == 3);
    CHECK(frame.instances[0].opacity == 0.5);
    CHECK(frame.instances[0].blend == wormedit::osbx_blend_t::additive);
}
} // namespace

TEST_CASE("OSBX clip clocks sample fractions in arbitrary order without integer retiming", "[osbx][clips]") {
    using namespace wormedit;
    const auto scene = clip_scene();
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    for (const auto [time, expected] : std::array<std::pair<int, double>, 4>{{{15, 20.0}, {9, 0.0}, {12, 8.0}, {10, 0.0}}}) {
        const auto frame = evaluate_osbx_frame(*index.value, time, arena);
        check_clip_translation(frame, expected);
    }
    const auto frame = evaluate_osbx_frame(*index.value,
        {.milliseconds = 12, .remainder = 1, .denominator = 2}, arena);
    check_clip_translation(frame, 10);
}

TEST_CASE("OSBX clip compiled storage survives index copy and move", "[osbx][clips]") {
    using namespace wormedit;
    const auto scene = clip_scene();
    auto built = osbx_evaluation_index_t::from(scene); REQUIRE(built.value);
    auto copied = *built.value; built.value.reset();
    const auto moved = std::move(copied);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(moved, 12, arena);
    REQUIRE(frame.is_success()); CHECK(frame.instances[0].world[4] == Catch::Approx(8));
}

TEST_CASE("OSBX numeric track sampling retains exact fractional time and the integer entry",
          "[osbx-evaluator][storyboard]") {
    using namespace wormedit;
    const osbx_track_t<> track{.default_value = {0}, .keys = {
        {.id = "a", .time_ms = -1, .value = {0}},
        {.id = "b", .time_ms = 1, .value = {10}}}};
    const auto fraction = sample_osbx_numeric_track(track,
        storyboard_time_t{.milliseconds = 0, .remainder = 1, .denominator = 2});
    REQUIRE(fraction.size() == 1);
    CHECK(fraction[0] == Catch::Approx(7.5));
    CHECK(sample_osbx_numeric_track(track, 0) == sample_osbx_numeric_track(track, storyboard_time_t{}));
    CHECK(sample_osbx_numeric_track(track, storyboard_time_t{.denominator = 0}).empty());
    CHECK(sample_osbx_numeric_track(track, storyboard_time_t{.remainder = 2, .denominator = 2}).empty());
}

TEST_CASE("OSBX fractional numeric sampling preserves adjacent large signed keys",
          "[osbx-evaluator][storyboard]") {
    using namespace wormedit;
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    const osbx_track_t<> track{.default_value = {0}, .keys = {
        {.id = "a", .time_ms = high - 1, .value = {0}},
        {.id = "b", .time_ms = high, .value = {10}}}};
    const auto value = sample_osbx_numeric_track(track,
        storyboard_time_t{.milliseconds = high - 1, .remainder = 1, .denominator = 2});
    REQUIRE(value.size() == 1);
    CHECK(value[0] == Catch::Approx(5));
}

TEST_CASE("OSBX shared blend sampler follows discrete keys and refuses invalid clocks",
          "[osbx-evaluator][storyboard]") {
    using namespace wormedit;
    const osbx_track_t<osbx_blend_t> track{.default_value = osbx_blend_t::normal, .keys = {
        {.id = "a", .time_ms = -2, .value = osbx_blend_t::additive},
        {.id = "b", .time_ms = 1, .value = osbx_blend_t::normal}}};
    CHECK(sample_osbx_blend_track(track, {.milliseconds = -2, .remainder = 1, .denominator = 2}).value() == osbx_blend_t::additive);
    CHECK(sample_osbx_blend_track(track, {.milliseconds = 1}).value() == osbx_blend_t::normal);
    CHECK(sample_osbx_blend_track(track, {.denominator = 0}).error() == failure_code_t::invalid_request);
}

TEST_CASE("OSBX shared visibility sampler returns authored discrete values without parent gating",
          "[osbx-evaluator][storyboard]") {
    using namespace wormedit;
    const osbx_track_t<bool> track{.default_value = true, .keys = {
        {.id = "a", .time_ms = -2, .value = false}, {.id = "b", .time_ms = 1, .value = true}}};
    CHECK_FALSE(sample_osbx_visibility_track(track, {.milliseconds = -2, .remainder = 1, .denominator = 2}).value());
    CHECK(sample_osbx_visibility_track(track, {.milliseconds = 1}).value());
    CHECK(sample_osbx_visibility_track(track, {.denominator = 0}).error() == failure_code_t::invalid_request);
    CHECK(sample_osbx_visibility_track(track, {.remainder = 1, .denominator = 1}).error() == failure_code_t::invalid_request);
}

TEST_CASE("OSBX frame exposes same-pass world states for groups and lifetime-hidden nodes",
          "[osbx-evaluator][storyboard]") {
    using namespace wormedit;
    const osbx_document_t document{.images = {{.id = "image", .file = "image.png"}}, .nodes = {
        {.id = "group", .ops = {{.id = "translate-group", .track = {.default_value = {20, 0}}}}},
        {.id = "hidden", .kind = osbx_node_kind_t::sprite, .parent = "group", .image = "image",
            .ops = {{.id = "translate-child", .track = {.default_value = {10, 0}}}},
            .lifetime = osbx_lifetime_t{.begin_ms = 10, .end_ms = 20}}}};
    const auto index = osbx_evaluation_index_t::from(document); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    check_hidden_frame_storage(frame);
    const auto* group = frame.node_state(*index.value, "group");
    const auto* hidden = frame.node_state(*index.value, "hidden");
    check_same_pass_node_identity(frame, group, hidden);
    CHECK(group->matrix[4] == Catch::Approx(20));
    CHECK(hidden->matrix[4] == Catch::Approx(30));
    CHECK_FALSE(hidden->visible);
    CHECK(frame.node_state(*index.value, "missing") == nullptr);
}

TEST_CASE("OSBX node-state lookup refuses nonfinite unsupported and failed frames",
          "[osbx-evaluator][storyboard]") {
    using namespace wormedit;
    const osbx_document_t document{.nodes = {
        {.id = "unsupported", .kind = osbx_node_kind_t::sample},
        {.id = "overflow", .ops = {
            {.id = "scale-a", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1e308, 1e308}}},
            {.id = "scale-b", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1e308, 1e308}}}}}}};
    const auto index = osbx_evaluation_index_t::from(document); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    CHECK(frame.node_state(*index.value, "unsupported") == nullptr);
    CHECK(frame.node_state(*index.value, "overflow") == nullptr);
    const auto invalid = evaluate_osbx_frame(*index.value, storyboard_time_t{.denominator = 0}, arena);
    CHECK(invalid.node_states.empty());
    CHECK(invalid.node_state(*index.value, "overflow") == nullptr);
}

namespace {
void check_op_query_refusals(const wormedit::osbx_frame_effect_t& frame,
    const wormedit::osbx_evaluation_index_t& index) {
    using namespace wormedit;
    CHECK_FALSE(frame.op_space(index, "node", "missing", storyboard_time_t{}));
    CHECK_FALSE(frame.op_space(index, "missing", "move", storyboard_time_t{}));
    CHECK_FALSE(frame.op_space(index, "node", "move", storyboard_time_t{.milliseconds = 1}));
    const auto other = index;
    CHECK_FALSE(frame.op_space(other, "node", "move", storyboard_time_t{}));
}
} // namespace

TEST_CASE("OSBX op-space lookup rejects another index clock and missing stable identities",
          "[osbx-evaluator][storyboard-gizmo]") {
    using namespace wormedit;
    const osbx_document_t scene{.nodes = {{.id = "node", .ops = {{.id = "move"}}}}};
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    check_op_query_refusals(frame, *index.value);
    const auto invalid = evaluate_osbx_frame(*index.value, storyboard_time_t{.denominator = 0}, arena);
    CHECK_FALSE(invalid.op_space(*index.value, "node", "move", storyboard_time_t{}));
}

TEST_CASE("OSBX op-space components retain exact fractional sampling after arena reuse",
          "[osbx-evaluator][storyboard-gizmo]") {
    using namespace wormedit;
    const osbx_document_t scene{.nodes = {{.id = "node", .ops = {{.id = "move",
        .track = {.default_value = {0, 0}, .keys = {
            {.id = "a", .time_ms = 0, .value = {0, 0}, .easing = osbx_linear_easing_t{}},
            {.id = "b", .time_ms = 1, .value = {20, 40}}}}}}}}};
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    const storyboard_time_t time{.remainder = 1, .denominator = 2};
    const auto frame = evaluate_osbx_frame(*index.value, time, arena);
    const auto space = frame.op_space(*index.value, "node", "move", time); REQUIRE(space);
    (void)evaluate_osbx_frame(*index.value, storyboard_time_t{.milliseconds = 1}, arena);
    // The frame query itself has expired; the returned numeric value is owned.
    CHECK(space->values[0] == 10);
    CHECK(space->values[1] == 20);
    CHECK(space->op == "move");
}

TEST_CASE("OSBX op-space lookup refuses an unsupported or nonfinite evaluated scene",
          "[osbx-evaluator][storyboard-gizmo][negative]") {
    using namespace wormedit;
    const auto unsupported = GENERATE(false, true);
    osbx_document_t scene{.nodes = {{.id = "node", .ops = {{.id = "move"}}}}};
    if (unsupported) scene.nodes[0].kind = osbx_node_kind_t::sample;
    else scene.nodes[0].ops = {
        {.id = "move", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1e308, 1e308}}},
        {.id = "overflow", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1e308, 1e308}}}};
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    CHECK_FALSE(frame.op_space(*index.value, "node", "move", storyboard_time_t{}));
}

namespace {
void check_lifetime_frame(const wormedit::osbx_document_t& document, wormedit::storyboard_time_t time,
    std::size_t count) {
    const auto index = wormedit::osbx_evaluation_index_t::from(document);
    REQUIRE(index.value);
    wormedit::frame_arena_t arena;
    const auto frame = wormedit::evaluate_osbx_frame(*index.value, time, arena);
    REQUIRE(frame.is_success());
    CHECK(frame.instances.size() == count);
}
} // namespace

namespace {
using namespace wormedit;
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;

// Dedicated test executable: count ordinary heap traffic as well as arena chunks.
class counting_resource_t final : public std::pmr::memory_resource {
  public:
    std::size_t allocations{};

  private:
    auto do_allocate(std::size_t size, std::size_t alignment) -> void* override {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(size, alignment);
    }
    void do_deallocate(void* p, std::size_t size, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, size, alignment);
    }
    auto do_is_equal(const std::pmr::memory_resource& other) const noexcept -> bool override {
        return this == &other;
    }
};

auto make_document() -> osbx_document_t {
    return {.images = {{.id = "image", .file = "sprite.png"}},
            .nodes = {{.id = "sprite",
                       .kind = osbx_node_kind_t::sprite,
                       .image = "image",
                       .origin = "TopLeft"}}};
}

auto make_op(std::string id, osbx_transform_kind_t kind, std::vector<double> value)
    -> osbx_transform_op_t {
    return {.id = std::move(id), .kind = kind, .track = {.default_value = std::move(value)}};
}

auto make_index(const osbx_document_t& document) -> osbx_evaluation_index_t {
    auto result = osbx_evaluation_index_t::from(document);
    REQUIRE(result.value);
    REQUIRE(result.diagnostics.empty());
    return std::move(*result.value);
}

auto instance_at(const osbx_document_t& document, std::int64_t time = 0) -> osbx_draw_instance_t {
    const auto index = make_index(document);
    frame_arena_t arena;
    auto frame = evaluate_osbx_frame(index, time, arena);
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    return frame.instances.front();
}

void check_matrix(const visual_affine_t& actual, const visual_affine_t& expected) {
    for (std::size_t i = 0; i < actual.values.size(); ++i)
        CHECK(actual[i] == Catch::Approx(expected[i]).margin(1e-10));
}

auto animated_translation(osbx_easing_t easing) -> osbx_document_t {
    auto document = make_document();
    document.nodes[0].ops.push_back(
        {.id = "move",
         .track = {.default_value = {99, 99},
                   .keys = {{.id = "from", .time_ms = 0, .value = {0, 10}, .easing = easing},
                            {.id = "to", .time_ms = 100, .value = {100, 30}}}}});
    return document;
}
} // namespace

void* operator new(std::size_t size) {
    if (count_allocations)
        ++allocations;
    if (auto* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

TEST_CASE("OSBX transform stacks apply in declaration order", "[osbx]") {
    auto document = make_document();
    document.nodes[0].ops = {make_op("move", osbx_transform_kind_t::translate, {10, 20}),
                             make_op("rotate", osbx_transform_kind_t::rotate, {90})};
    check_matrix(instance_at(document).world, {0, 1, -1, 0, -20, 10});
    std::ranges::reverse(document.nodes[0].ops);
    check_matrix(instance_at(document).world, {0, 1, -1, 0, 10, 20});
}

TEST_CASE("OSBX pivot wrapper matches explicit translations around nested ops", "[osbx]") {
    auto document = make_document();
    auto pivot = make_op("pivot", osbx_transform_kind_t::pivot, {10, 20});
    pivot.children = {make_op("rotate", osbx_transform_kind_t::rotate, {90}),
                      make_op("scale", osbx_transform_kind_t::scale, {2, 3})};
    document.nodes[0].ops = {pivot};
    const auto wrapped = instance_at(document).world;
    document.nodes[0].ops = {make_op("negative", osbx_transform_kind_t::translate, {-10, -20}),
                             pivot.children[0], pivot.children[1],
                             make_op("positive", osbx_transform_kind_t::translate, {10, 20})};
    check_matrix(wrapped, instance_at(document).world);
    check_matrix(wrapped, {0, 3, -2, 0, 50, -10});
}

TEST_CASE("OSBX hierarchy multiplies parent by local despite child declaration first", "[osbx]") {
    auto document = make_document();
    document.nodes[0].parent = "parent";
    document.nodes[0].ops = {make_op("move", osbx_transform_kind_t::translate, {10, 0})};
    document.nodes.push_back(
        {.id = "parent", .ops = {make_op("rotate", osbx_transform_kind_t::rotate, {90})}});
    check_matrix(instance_at(document).world, {0, 1, -1, 0, 0, 10});
}

TEST_CASE("OSBX rotation interpolates authored turns without wrapping", "[osbx]") {
    auto document = make_document();
    document.nodes[0].ops = {{.id = "rotate",
                              .kind = osbx_transform_kind_t::rotate,
                              .track = {.default_value = {0},
                                        .keys = {{.id = "from", .value = {0}},
                                                 {.id = "to", .time_ms = 100, .value = {720}}}}}};
    check_matrix(instance_at(document, 25).world, {-1, 0, 0, -1, 0, 0});
    check_matrix(instance_at(document, 50).world, {1, 0, 0, 1, 0, 0});
    check_matrix(instance_at(document, 75).world, {-1, 0, 0, -1, 0, 0});
}

TEST_CASE("OSBX matrix keys interpolate every coefficient including singular matrices", "[osbx]") {
    auto document = make_document();
    document.nodes[0].ops = {
        {.id = "matrix",
         .kind = osbx_transform_kind_t::matrix,
         .track = {.default_value = {1, 0, 0, 1, 0, 0},
                   .keys = {{.id = "from", .value = {1, 2, 3, 4, 5, 6}},
                            {.id = "to", .time_ms = 100, .value = {-1, -2, -3, -4, 15, 26}}}}}};
    check_matrix(instance_at(document, 25).world, {0.5, 1, 1.5, 2, 7.5, 11});
    check_matrix(instance_at(document, 50).world, {0, 0, 0, 0, 10, 16});
}

TEST_CASE("OSBX skew uses clockwise degrees and default transforms", "[osbx]") {
    auto document = make_document();
    document.nodes[0].ops = {make_op("skew", osbx_transform_kind_t::skew, {45, -45})};
    check_matrix(instance_at(document).world, {1, -1, 1, 1, 0, 0});
}

TEST_CASE("OSBX numeric sampling clamps endpoints and uses the departing key easing", "[osbx]") {
    auto document = animated_translation(osbx_linear_easing_t{});
    CHECK(instance_at(document, -1).world[4] == 0);
    CHECK(instance_at(document, 0).world[4] == 0);
    CHECK(instance_at(document, 25).world[4] == 25);
    CHECK(instance_at(document, 25).world[5] == 15);
    CHECK(instance_at(document, 100).world[4] == 100);
    CHECK(instance_at(document, 101).world[4] == 100);
}

TEST_CASE("OSBX single-key tracks hold their value and empty tracks use defaults", "[osbx]") {
    auto document = animated_translation(osbx_linear_easing_t{});
    document.nodes[0].ops[0].track.keys.resize(1);
    CHECK(instance_at(document, 500).world[4] == 0);
    document.nodes[0].ops[0].track.keys.clear();
    CHECK(instance_at(document, 50).world[4] == 99);
}

TEST_CASE("OSBX step and OSB easing share known sampling values", "[osbx]") {
    const auto step = animated_translation(osbx_step_easing_t{});
    CHECK(instance_at(step, 99).world[4] == 0);
    CHECK(instance_at(step, 100).world[4] == 100);
    const auto quadratic = animated_translation(osbx_osb_easing_t{.value = 3});
    CHECK(instance_at(quadratic, 50).world[4] == 25);
    CHECK(evaluate_storyboard_easing(3, -1) == 0);
    CHECK(evaluate_storyboard_easing(3, 2) == 1);
}

TEST_CASE("OSBX cubic Bezier inverts x with bounded Newton and bisection", "[osbx]") {
    const auto symmetric = animated_translation(osbx_cubic_bezier_t{0.42, 0, 0.58, 1});
    CHECK(instance_at(symmetric, 50).world[4] == Catch::Approx(50).margin(1e-9));
    CHECK(instance_at(symmetric, 25).world[4] == Catch::Approx(12.9161931047).margin(1e-8));
    // x=t^3, y=3t^2-2t^3: x=0.125 -> t=0.5 -> y=0.5.
    auto flat = animated_translation(osbx_cubic_bezier_t{0, 0, 0, 1});
    flat.nodes[0].ops[0].track.keys.back().time_ms = 800;
    CHECK(instance_at(flat, 100).world[4] == Catch::Approx(50).margin(1e-9));
    // Flat derivative at t=0.5 exercises the safeguarded fallback near the cusp.
    const auto cusp = animated_translation(osbx_cubic_bezier_t{1, 0, 0, 1});
    CHECK(instance_at(cusp, 49).world[4] == Catch::Approx(30.1418678804).margin(1e-8));
}

TEST_CASE("OSBX sampling accepts the full signed millisecond domain", "[osbx]") {
    auto document = animated_translation(osbx_linear_easing_t{});
    auto& keys = document.nodes[0].ops[0].track.keys;
    keys.front().time_ms = std::numeric_limits<std::int64_t>::min();
    keys.back().time_ms = std::numeric_limits<std::int64_t>::max();
    CHECK(instance_at(document, 0).world[4] == Catch::Approx(50));
    keys.front().time_ms = std::numeric_limits<std::int64_t>::max() - 2;
    CHECK(instance_at(document, keys.front().time_ms + 1).world[4] == Catch::Approx(50));
}

TEST_CASE("OSBX inherits opacity color and visibility while sampling sprite blend", "[osbx]") {
    auto document = make_document();
    document.nodes[0].parent = "group";
    document.nodes[0].tracks.opacity.default_value = {0.5};
    document.nodes[0].tracks.color.default_value = {0.5, 0.4, 0.3, 0.2};
    document.nodes[0].tracks.blend.keys = {
        {.id = "blend-a", .value = osbx_blend_t::normal},
        {.id = "blend-b", .time_ms = 10, .value = osbx_blend_t::additive}};
    document.nodes.push_back({.id = "group"});
    auto& group = document.nodes.back();
    group.tracks.opacity.default_value = {0.4};
    group.tracks.color.default_value = {0.2, 0.3, 0.4, 0.5};
    group.tracks.visibility.keys = {{.id = "show", .value = true},
                                    {.id = "hide", .time_ms = 20, .value = false}};
    const auto instance = instance_at(document, 10);
    CHECK(instance.opacity == Catch::Approx(0.2));
    const std::array expected{0.1, 0.12, 0.12, 0.1};
    for (std::size_t i = 0; i < expected.size(); ++i)
        CHECK(instance.color[i] == Catch::Approx(expected[i]));
    CHECK(
        std::tuple{instance.blend, instance.image, instance.origin} ==
        std::tuple{osbx_blend_t::additive, std::string_view{"image"}, std::string_view{"TopLeft"}});
    CHECK(instance_at(document, 9).blend == osbx_blend_t::normal);
    const auto index = make_index(document);
    frame_arena_t arena;
    CHECK(evaluate_osbx_frame(index, 20, arena).instances.empty());
}

TEST_CASE("OSBX draw order sorts layer order and declaration independently of topology", "[osbx]") {
    auto document = make_document();
    const auto sprite = document.nodes.front();
    for (int i = 0; i < 4; ++i) {
        auto next = sprite;
        next.id = "sprite" + std::to_string(i);
        document.nodes.push_back(std::move(next));
    }
    document.nodes[0].layer = osbx_layer_t::foreground;
    document.nodes[1].order = 10;
    document.nodes[2].layer = osbx_layer_t::pass;
    document.nodes[3].order = -1;
    document.nodes[4].order = -1;
    const auto index = make_index(document);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(index, 0, arena);
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 5);
    const std::array<std::string_view, 5> expected{"sprite2", "sprite3", "sprite0", "sprite1",
                                                   "sprite"};
    CHECK(std::ranges::equal(frame.instances, expected, {}, &osbx_draw_instance_t::id));
}

TEST_CASE("OSBX skips non-finite world transforms and diagnoses each affected ID", "[osbx]") {
    auto document = make_document();
    document.nodes[0].ops = {make_op("scale-a", osbx_transform_kind_t::scale, {1e300, 1}),
                             make_op("scale-b", osbx_transform_kind_t::scale, {1e300, 1})};
    document.nodes.push_back({.id = "healthy", .kind = osbx_node_kind_t::sprite, .image = "image"});
    document.nodes.push_back({.id = "descendant", .parent = "sprite"});
    const auto index = make_index(document);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(index, 0, arena);
    REQUIRE_FALSE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    CHECK(frame.instances[0].id == "healthy");
    REQUIRE(frame.diagnostics.size() == 2);
    CHECK(std::tuple{frame.diagnostics[0].code, frame.diagnostics[0].id, frame.diagnostics[1].id} ==
          std::tuple{failure_code_t::arithmetic_overflow, std::string_view{"sprite"},
                     std::string_view{"descendant"}});
}

TEST_CASE("OSBX structural placeholders return OSBX-0 number 10 errors even when hidden",
          "[osbx]") {
    auto document = make_document();
    document.nodes.push_back({.id = "animation", .kind = osbx_node_kind_t::animation});
    document.nodes.push_back({.id = "sample", .kind = osbx_node_kind_t::sample});
    document.nodes.back().tracks.visibility.default_value = false;
    document.nodes.back().ops = {make_op("scale-a", osbx_transform_kind_t::scale, {1e300, 1}),
                                 make_op("scale-b", osbx_transform_kind_t::scale, {1e300, 1})};
    const auto index = make_index(document);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(index, 0, arena);
    REQUIRE_FALSE(frame.is_success());
    REQUIRE(frame.diagnostics.size() == 2);
    CHECK(frame.diagnostics[0].code == failure_code_t::unsupported);
    CHECK(frame.diagnostics[0].id == "animation");
    CHECK(frame.diagnostics[1].code == failure_code_t::unsupported);
    CHECK(frame.diagnostics[1].id == "sample");
}

TEST_CASE("OSBX skips overflowing inherited colors with a diagnostic", "[osbx]") {
    auto document = make_document();
    document.nodes[0].parent = "group";
    document.nodes[0].tracks.color.default_value = {1e300, 1, 1, 1};
    document.nodes.push_back({.id = "group"});
    document.nodes.back().tracks.color.default_value = {1e300, 1, 1, 1};
    document.nodes.push_back({.id = "healthy", .kind = osbx_node_kind_t::sprite, .image = "image"});
    const auto index = make_index(document);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(index, 0, arena);
    CHECK_FALSE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    CHECK(frame.instances.front().id == "healthy");
    REQUIRE(frame.diagnostics.size() == 1);
    CHECK(frame.diagnostics.front().code == failure_code_t::arithmetic_overflow);
    CHECK(frame.diagnostics.front().id == "sprite");
}

TEST_CASE("OSBX index rejects invalid scenes with structural diagnostics", "[osbx]") {
    auto document = make_document();
    document.nodes[0].parent = "missing";
    const auto rejected = osbx_evaluation_index_t::from(document);
    CHECK_FALSE(rejected.value);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(rejected.diagnostics[0].code == osbx_diagnostic_code_t::missing_parent);
}

TEST_CASE("OSBX frame arena failures stay typed and empty scenes need no storage", "[osbx]") {
    const auto document = make_document();
    const auto index = make_index(document);
    frame_arena_t tiny{{.chunk_bytes = 1, .maximum_bytes = 1}};
    const auto frame = evaluate_osbx_frame(index, 0, tiny);
    CHECK_FALSE(frame.is_success());
    CHECK(frame.arena_error == failure_code_t::capacity_exceeded);
    CHECK(frame.instances.empty());
    const osbx_document_t empty;
    const auto empty_frame = evaluate_osbx_frame(make_index(empty), 0, tiny);
    CHECK(empty_frame.is_success());
    CHECK(empty_frame.instances.empty());
}

TEST_CASE("OSBX repeat frames are deterministic and allocate nothing after warm-up", "[osbx]") {
    auto document = animated_translation(osbx_cubic_bezier_t{0.42, 0, 0.58, 1});
    document.nodes.push_back({.id = "unsupported", .kind = osbx_node_kind_t::sample});
    const auto index = make_index(document);
    counting_resource_t resource;
    frame_arena_t arena{{.chunk_bytes = 64}, &resource};
    auto first = evaluate_osbx_frame(index, 37, arena);
    const std::vector expected(first.instances.begin(), first.instances.end());
    const std::vector expected_diagnostics(first.diagnostics.begin(), first.diagnostics.end());
    const auto chunk_allocations = resource.allocations;
    const auto committed = arena.committed_bytes();
    allocations = 0;
    bool identical = true;
    count_allocations = true;
    for (int i = 0; i < 100; ++i) {
        (void)evaluate_osbx_frame(index, i, arena);
        const auto repeated = evaluate_osbx_frame(index, 37, arena);
        identical = identical && std::ranges::equal(repeated.instances, expected) &&
                    std::ranges::equal(repeated.diagnostics, expected_diagnostics);
    }
    count_allocations = false;
    CHECK(identical);
    CHECK(allocations == 0);
    CHECK(resource.allocations == chunk_allocations);
    CHECK(arena.committed_bytes() == committed);
}

namespace {
template <class Value> void require_osbx_cap(const Value& value) { REQUIRE(static_cast<bool>(value)); }
void check_osbx_cap(bool value) { CHECK(value); }
}

TEST_CASE("OSBX preview cap candidates have bounded warm frame storage", "[osbx][allocation][cap]") {
    // Measure candidates before the frontend cap changes. No presenter limit is bypassed.
    for (const std::size_t count : {27U, 256U, 1024U}) {
        auto document = make_document();
        const auto source = document.nodes.front();
        document.nodes.clear();
        for (std::size_t i = 0; i < count; ++i) {
            auto sprite = source;
            sprite.id = "sprite-" + std::to_string(i);
            document.nodes.push_back(std::move(sprite));
        }
        const auto index = make_index(document);
        counting_resource_t resource;
        frame_arena_t arena{{}, &resource};
        require_osbx_cap(evaluate_osbx_frame(index, 0, arena).instances.size() == count);
        const auto committed = arena.committed_bytes(), used = arena.used_bytes();
        const auto chunk_allocations = resource.allocations;
        bool valid = true;
        allocations = 0;
        count_allocations = true;
        const auto begin = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 100; ++frame) {
            const auto evaluated = evaluate_osbx_frame(index, frame, arena);
            valid = valid && evaluated.is_success() && evaluated.instances.size() == count &&
                    arena.committed_bytes() == committed && arena.used_bytes() == used;
        }
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        count_allocations = false;
        const auto warmed_allocations = allocations;
        check_osbx_cap(valid);
        check_osbx_cap(warmed_allocations == 0);
        check_osbx_cap(resource.allocations == chunk_allocations);
        std::printf("OSBX cap candidate=%zu evaluator_used=%zu committed=%zu warm_allocations=%zu avg_us=%.3f\n",
                    count, used, committed, warmed_allocations,
                    std::chrono::duration<double, std::micro>(elapsed).count() / 100);
    }
}

namespace {
void check_conditional_sample(const osbx_frame_effect_t& frame, storyboard_conditional_layer_t choice) {
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    CHECK(frame.instances[0].id == (choice == storyboard_conditional_layer_t::pass ? "sprite" : "fail"));
    CHECK(frame.instances[0].world[4] == Catch::Approx(100.0 / 6));
}
} // namespace

TEST_CASE("OSBX conditional layers and fractional keys use the shared clock", "[osbx]") {
    auto document = animated_translation(osbx_linear_easing_t{});
    document.nodes[0].layer = osbx_layer_t::pass;
    auto fail = document.nodes[0];
    fail.id = "fail";
    fail.ops[0].id = "fail-move";
    fail.ops[0].track.keys[0].id = "fail-from";
    fail.ops[0].track.keys[1].id = "fail-to";
    fail.layer = osbx_layer_t::fail;
    document.nodes.push_back(fail);
    const auto index = make_index(document);
    frame_arena_t arena;
    const storyboard_time_t time{.milliseconds = 16, .remainder = 2, .denominator = 3};
    for (const auto choice : {storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail}) {
        const auto frame = evaluate_osbx_frame(index, time, arena, {.conditional_layer = choice});
        check_conditional_sample(frame, choice);
    }
    CHECK(evaluate_osbx_frame(index, time, arena,
        {.conditional_layer = storyboard_conditional_layer_t::neither}).instances.empty());
    REQUIRE(evaluate_osbx_frame(index, 0, arena).instances.size() == 1);
    CHECK(evaluate_osbx_frame(index, 0, arena).instances[0].id == "sprite");
    CHECK(evaluate_osbx_frame(index, time, arena, {.include_all_conditional_layers = true}).instances.size() == 2);
    CHECK_FALSE(evaluate_osbx_frame(index, storyboard_time_t{.denominator = 0}, arena).is_success());
}

TEST_CASE("OSBX fractional easing preserves negative and adjacent extreme key boundaries", "[osbx]") {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const std::array cases{
        std::tuple{maximum - 1, maximum,
            storyboard_time_t{.milliseconds = maximum - 1, .remainder = 1, .denominator = 2},
            osbx_easing_t{osbx_osb_easing_t{3}}, 25.0},
        std::tuple{std::int64_t{-1}, std::int64_t{0},
            storyboard_time_t{.milliseconds = -1, .remainder = 1, .denominator = 2},
            osbx_easing_t{osbx_linear_easing_t{}}, 50.0},
        std::tuple{std::int64_t{-1}, std::int64_t{0}, storyboard_time_t{},
            osbx_easing_t{osbx_linear_easing_t{}}, 100.0}};
    for (const auto& [begin, end, time, easing, expected] : cases) {
        auto document = animated_translation(easing);
        auto& keys = document.nodes[0].ops[0].track.keys;
        keys.front().time_ms = begin;
        keys.back().time_ms = end;
        const auto index = make_index(document);
        frame_arena_t arena;
        const auto frame = evaluate_osbx_frame(index, time, arena);
        REQUIRE(frame.is_success());
        REQUIRE(frame.instances.size() == 1);
        CHECK(frame.instances[0].world[4] == Catch::Approx(expected));
    }
}

TEST_CASE("OSBX lifetime gates shared fractional frames and inherits parent visibility", "[osbx][lifetime]") {
    auto document = make_document();
    document.nodes[0].lifetime = osbx_lifetime_t{.begin_ms = -2, .end_ms = 2};
    check_lifetime_frame(document, {.milliseconds = -3, .remainder = 999, .denominator = 1000}, 0);
    check_lifetime_frame(document, {.milliseconds = -2}, 1);
    check_lifetime_frame(document, {.milliseconds = 2}, 1);
    check_lifetime_frame(document, {.milliseconds = 2, .remainder = 1, .denominator = 1000}, 0);
    document.nodes[0].parent = "parent";
    document.nodes.push_back({.id = "parent", .lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 0}});
    check_lifetime_frame(document, {.milliseconds = -1}, 0);
    check_lifetime_frame(document, {.milliseconds = 0}, 1);
    check_lifetime_frame(document, {.milliseconds = 0, .remainder = 1, .denominator = 2}, 0);
    document.nodes[1].lifetime.reset();
    document.nodes[0].tracks.visibility.default_value = false;
    check_lifetime_frame(document, {.milliseconds = 0}, 0);
    document.nodes[0].tracks.visibility.default_value = true;
    document.nodes[0].lifetime.reset();
    check_lifetime_frame(document, {.milliseconds = std::numeric_limits<std::int64_t>::max(), .remainder = 1, .denominator = 2}, 1);
}

TEST_CASE("OSBX op-space queries allocate no heap storage after the shared frame",
          "[osbx-evaluator][storyboard-gizmo][allocation]") {
    using namespace wormedit;
    const osbx_document_t scene{.nodes = {{.id = "node", .ops = {
        {.id = "move"}, {.id = "rotate", .kind = osbx_transform_kind_t::rotate, .track = {.default_value = {90}}}}}}};
    const auto index = osbx_evaluation_index_t::from(scene); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{}, arena);
    allocations = 0; count_allocations = true;
    const auto space = frame.op_space(*index.value, "node", "move", storyboard_time_t{});
    count_allocations = false;
    REQUIRE(space);
    CHECK(allocations == 0);
}

TEST_CASE("OSBX nested clips compose exact clocks and identity activation gates", "[osbx][clips]") {
    auto source = clip_scene();
    source.clips.push_back({.id = "outer", .uses = {{.id = "nested", .clip = "motion", .start_ms = 2, .time_scale = 0.5}}});
    source.nodes[0].uses[0].clip = "outer";
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    const auto before = evaluate_osbx_frame(*index.value, 10, arena);
    REQUIRE(before.is_success()); CHECK(before.instances[0].world[4] == 0);
    const auto sampled = evaluate_osbx_frame(*index.value, {.milliseconds = 12, .remainder = 1, .denominator = 4}, arena);
    REQUIRE(sampled.is_success()); CHECK(sampled.instances[0].world[4] == Catch::Approx(10));
}

TEST_CASE("OSBX exact clip maps retain full signed clocks and refuse overflow", "[osbx][clips]") {
    const osbx_clip_clock_t identity;
    CHECK(identity.map(INT64_MIN)->milliseconds == INT64_MIN);
    CHECK(identity.map(INT64_MAX)->milliseconds == INT64_MAX);
    const osbx_clip_clock_t half{.origin = {.milliseconds = INT64_MIN, .remainder = 1, .denominator = 2}, .denominator = 2};
    const auto edge = half.map(-1); REQUIRE(edge);
    CHECK(edge->milliseconds == INT64_MIN); CHECK(edge->remainder == 0);
    check_clip_clock_capacity_refusals(identity);
}

TEST_CASE("OSBX parameter defaults properties and explicit blend preserve declaration order", "[osbx][clips]") {
    auto source = clip_scene();
    source.clips[0].ops[0].track.keys.clear();
    source.clips[0].ops[0].track.parameter = "offset";
    source.clips[0].parameters = {{.name = "offset", .default_value = {3,4}}};
    source.clips[0].tracks.opacity.default_value = {0.5};
    source.nodes[0].tracks.blend.default_value = osbx_blend_t::additive;
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, 10, arena);
    check_bound_clip_properties(frame);
    source.clips[0].tracks.blend = osbx_track_t<osbx_blend_t>{.default_value = osbx_blend_t::normal};
    const auto normal = osbx_evaluation_index_t::from(source); REQUIRE(normal.value);
    CHECK(evaluate_osbx_frame(*normal.value, 10, arena).instances[0].blend == osbx_blend_t::normal);
}

TEST_CASE("OSBX authored operation queries include compiled suffixes without exposing template IDs", "[osbx][clips][storyboard-gizmo]") {
    auto source = clip_scene();
    source.nodes[0].ops = {{.id = "authored", .track = {.default_value = {1,0}}}};
    source.clips[0].ops = {{.id = "template-scale", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {2,2}}}};
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, 10, arena);
    const auto authored = frame.op_space(*index.value, "sprite", "authored", {.milliseconds = 10});
    REQUIRE(authored); CHECK(authored->world_side[0] == 2);
    CHECK_FALSE(frame.op_space(*index.value, "sprite", "template-scale", {.milliseconds = 10}));
}

TEST_CASE("OSBX clip frames reuse warmed arena without heap allocations", "[osbx][clips][allocation]") {
    const auto source = clip_scene();
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    REQUIRE(evaluate_osbx_frame(*index.value, 12, arena).is_success());
    allocations = 0; count_allocations = true;
    const auto first = evaluate_osbx_frame(*index.value, 15, arena).is_success();
    const auto second = evaluate_osbx_frame(*index.value, 10, arena).is_success();
    count_allocations = false;
    CHECK(first); CHECK(second); CHECK(allocations == 0);
}

TEST_CASE("OSBX clip fractional easing and literal overrides use the shared sampler", "[osbx][clips]") {
    auto source = clip_scene();
    source.clips[0].ops[0].track.keys[0].easing = osbx_osb_easing_t{3};
    const auto eased = osbx_evaluation_index_t::from(source); REQUIRE(eased.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*eased.value, {.milliseconds = 11, .remainder = 1, .denominator = 4}, arena);
    REQUIRE(frame.is_success()); CHECK(frame.instances[0].world[4] == Catch::Approx(1.25));
    source.clips[0].ops[0].track.keys.clear();
    source.clips[0].ops[0].track.parameter = "offset";
    source.clips[0].parameters = {{.name = "offset", .default_value = {1,2}}};
    source.nodes[0].uses[0].parameters = {{.name = "offset", .value = {8,9}}};
    const auto bound = osbx_evaluation_index_t::from(source); REQUIRE(bound.value);
    CHECK(evaluate_osbx_frame(*bound.value, 10, arena).instances[0].world[4] == 8);
}

TEST_CASE("OSBX two uses of one clip keep independent fractional clocks", "[osbx][clips]") {
    auto source = clip_scene();
    source.nodes[0].uses.push_back({.id = "second", .clip = "motion", .start_ms = 20, .time_scale = 2});
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    CHECK(evaluate_osbx_frame(*index.value, 22, arena).instances[0].world[4] == Catch::Approx(22));
    CHECK(evaluate_osbx_frame(*index.value, 12, arena).instances[0].world[4] == Catch::Approx(8));
    CHECK(evaluate_osbx_frame(*index.value, 22, arena).instances[0].world[4] == Catch::Approx(22));
}

TEST_CASE("OSBX nonrepresentable fractional clip clocks refuse instead of truncating", "[osbx][clips]") {
    auto source = clip_scene();
    source.nodes[0].uses[0].time_scale = 0.1;
    const auto result = osbx_evaluation_index_t::from(source);
    CHECK_FALSE(result.value);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].message.find("exact") != std::string::npos);
}

namespace {
auto temporal_track() -> wormedit::osbx_track_t<> {
    using namespace wormedit;
    return {.default_value = {0.25}, .program = {{.data = osbx_loop_t<>{.id = "repeat", .start_ms = 100, .count = 3,
        .keys = {{.id = "first", .value = {0}}, {.id = "last", .time_ms = 10, .value = {1}}}}}}};
}
void check_temporal_sample(const wormedit::osbx_track_t<>& track, wormedit::storyboard_time_t time, double expected,
    const wormedit::storyboard_trigger_scenario_t* scenario = nullptr) {
    const auto value = wormedit::sample_osbx_numeric_track(track, time, scenario);
    REQUIRE(value.size() == 1);
    CHECK(value[0] == Catch::Approx(expected));
}
}
TEST_CASE("OSBX loop sampling is arithmetic exact fractional and independent of query order", "[osbx][program]") {
    const auto track = temporal_track();
    check_temporal_sample(track, {.milliseconds = 130}, 1);
    check_temporal_sample(track, {.milliseconds = 99}, 0.25);
    check_temporal_sample(track, {.milliseconds = 110}, 0);
    check_temporal_sample(track, {.milliseconds = 105, .remainder = 1, .denominator = 2}, 0.55);
    check_temporal_sample(track, {.milliseconds = 129, .remainder = 1, .denominator = 2}, 0.95);
}
TEST_CASE("OSBX loop gaps hold prior iteration and zero period never divides", "[osbx][program]") {
    auto track = temporal_track();
    auto& loop = std::get<wormedit::osbx_loop_t<>>(track.program[0].data);
    loop.keys[0].time_ms = 3;
    check_temporal_sample(track, {.milliseconds = 101}, 0.25);
    check_temporal_sample(track, {.milliseconds = 111}, 1);
    loop.keys.resize(1); loop.keys[0].time_ms = 0; loop.keys[0].value = {0.75};
    loop.count = UINT32_MAX;
    check_temporal_sample(track, {.milliseconds = 100}, 0.75);
}
TEST_CASE("OSBX triggers use explicit exact names windows and latest firing restart", "[osbx][program]") {
    using namespace wormedit;
    const auto name = *storyboard_trigger_name_t::from("Passing");
    osbx_track_t<> track{.default_value = {1}, .program = {{.data = osbx_trigger_t<>{.id = "trigger",
        .trigger = {.name = name, .start_time_ms = 0, .end_time_ms = 100},
        .keys = {{.id = "a", .value = {0}}, {.id = "b", .time_ms = 10, .value = {1}}}}}}};
    const std::array firings{storyboard_trigger_firing_t{.name = name, .time = {.milliseconds = 50}},
        storyboard_trigger_firing_t{.name = name, .time = {.milliseconds = 55}}};
    const auto scenario = storyboard_trigger_scenario_t::from(firings);
    REQUIRE(scenario);
    check_temporal_sample(track, {.milliseconds = 52}, 1);
    check_temporal_sample(track, {.milliseconds = 52}, 0.2, &*scenario);
    check_temporal_sample(track, {.milliseconds = 56}, 0.1, &*scenario);
}

TEST_CASE("OSBX temporal precedence follows latest started segment and authored ties", "[osbx][program]") {
    using namespace wormedit;
    auto track = temporal_track();
    track.keys = {{.id = "root-a", .time_ms = 107, .value = {0.2}}, {.id = "root-b", .time_ms = 120, .value = {0.4}}};
    // The root segment wins and still interpolates from107 through120.
    check_temporal_sample(track, {.milliseconds = 108}, 0.2 + 0.2 / 13);
    check_temporal_sample(track, {.milliseconds = 111}, 0.1); // next loop110 beats root107
    check_temporal_sample(track, {.milliseconds = 120}, 0); // program ties beat root
    track.program.push_back({.data = osbx_loop_t<>{.id = "later", .start_ms = 110, .count = 1,
        .keys = {{.id = "later-key", .value = {0.8}}}}});
    check_temporal_sample(track, {.milliseconds = 111}, 0.8);
    check_temporal_sample(track, {.milliseconds = 121}, 0.1); // repeat120 beats held later110
}

TEST_CASE("OSBX explicit trigger gaps keep the latest already started activation", "[osbx][program]") {
    using namespace wormedit;
    const auto name = *storyboard_trigger_name_t::from("Passing");
    osbx_track_t<> track{.default_value = {0.25}, .program = {{.data = osbx_trigger_t<>{.id = "trigger",
        .trigger = {.name = name, .start_time_ms = 50, .end_time_ms = 55},
        .keys = {{.id = "a", .time_ms = 3, .value = {0}}, {.id = "b", .time_ms = 10, .value = {1}}}}}}};
    const std::array events{storyboard_trigger_firing_t{name, {.milliseconds = 49}},
        storyboard_trigger_firing_t{name, {.milliseconds = 50}}, storyboard_trigger_firing_t{name, {.milliseconds = 55}},
        storyboard_trigger_firing_t{name, {.milliseconds = 56}}};
    const auto scenario = storyboard_trigger_scenario_t::from(events); REQUIRE(scenario);
    check_temporal_sample(track, {.milliseconds = 52}, 0.25, &*scenario);
    check_temporal_sample(track, {.milliseconds = 56}, 3.0 / 7, &*scenario);
    check_temporal_sample(track, {.milliseconds = 58}, 0, &*scenario);
    check_temporal_sample(track, {.milliseconds = 65}, 1, &*scenario);
}

TEST_CASE("OSBX discrete temporal programs share fractional restart and held semantics", "[osbx][program]") {
    using namespace wormedit;
    osbx_track_t<bool> visibility{.default_value = true, .program = {{.data = osbx_loop_t<bool>{.id = "v", .count = 2,
        .keys = {{.id = "v0", .value = false}, {.id = "v1", .time_ms = 2, .value = true}}}}}};
    CHECK_FALSE(*sample_osbx_visibility_track(visibility, {.milliseconds = 1, .remainder = 1, .denominator = 2}));
    CHECK_FALSE(*sample_osbx_visibility_track(visibility, {.milliseconds = 2}));
    CHECK(*sample_osbx_visibility_track(visibility, {.milliseconds = 4}));
    osbx_track_t<osbx_blend_t> blend{.default_value = osbx_blend_t::normal, .program = {{.data = osbx_loop_t<osbx_blend_t>{
        .id = "blend", .count = UINT32_MAX, .keys = {{.id = "blend0", .value = osbx_blend_t::additive}}}}}};
    CHECK(*sample_osbx_blend_track(blend, {.milliseconds = INT64_MAX}) == osbx_blend_t::additive);
}

TEST_CASE("OSBX program clip clocks invert exactly and retain mapped fractional keys", "[osbx][program][clips]") {
    using namespace wormedit;
    auto source = clip_scene();
    source.clips[0].ops[0].track.keys.clear();
    source.clips[0].ops[0].track.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = 2,
        .keys = {{.id = "a", .value = {0,0}}, {.id = "b", .time_ms = 3, .value = {6,0}}}}}};
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, {.milliseconds = 10, .remainder = 3, .denominator = 4}, arena);
    check_clip_translation(frame, 3);
    check_clip_translation(evaluate_osbx_frame(*index.value, {.milliseconds = 11, .remainder = 1, .denominator = 2}, arena), 0);
    const osbx_clip_clock_t clock{.origin = {.milliseconds = -10, .remainder = 1, .denominator = 3}, .numerator = 3, .denominator = 2};
    const auto mapped = clock.map(-7); REQUIRE(mapped);
    const auto inverse = clock.unmap(*mapped); REQUIRE(inverse);
    CHECK(inverse->milliseconds == -7); CHECK(inverse->remainder == 0);
}

namespace {
auto triggered_translation() -> wormedit::osbx_key_program_t<> {
    using namespace wormedit;
    return {.data = osbx_trigger_t<>{.id = "activate",
        .trigger = {.name = *storyboard_trigger_name_t::from("Passing"), .start_time_ms = 0, .end_time_ms = 100},
        .keys = {{.id = "first", .value = {0,0}}, {.id = "last", .time_ms = 10, .value = {20,0}}}}};
}
}
TEST_CASE("OSBX frame and op-space borrow the same sealed fractional firing scenario", "[osbx][program]") {
    using namespace wormedit;
    osbx_document_t source{.images = {{.id = "image", .file = "image.png"}}, .nodes = {
        {.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image", .ops = {
            {.id = "move", .track = {.default_value = {0,0}, .program = {triggered_translation()}}}}}}};
    const auto scenario = storyboard_trigger_scenario_t::from(std::array{storyboard_trigger_firing_t{
        *storyboard_trigger_name_t::from("Passing"), {.milliseconds = 10, .remainder = 1, .denominator = 2}}}); REQUIRE(scenario);
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena; const storyboard_time_t time{.milliseconds = 11, .remainder = 1, .denominator = 2};
    for (auto choice : {storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail, storyboard_conditional_layer_t::neither}) {
        const auto frame = evaluate_osbx_frame(*index.value, time, arena, {.conditional_layer = choice, .scenario = &*scenario});
        check_clip_translation(frame, 2);
        const auto space = frame.op_space(*index.value, "sprite", "move", time); REQUIRE(space);
        CHECK(space->values[0] == Catch::Approx(2));
    }
    check_clip_translation(evaluate_osbx_frame(*index.value, time, arena), 0);
}
TEST_CASE("OSBX compiled trigger programs use source-song firings through clip scale", "[osbx][program][clips]") {
    using namespace wormedit;
    auto source = clip_scene(); source.clips[0].ops[0].track.keys.clear();
    source.clips[0].ops[0].track.program = {triggered_translation()};
    const auto scenario = storyboard_trigger_scenario_t::from(std::array{storyboard_trigger_firing_t{
        *storyboard_trigger_name_t::from("Passing"), {.milliseconds = 11}}}); REQUIRE(scenario);
    const auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
    frame_arena_t arena;
    check_clip_translation(evaluate_osbx_frame(*index.value, 12, arena, {.scenario = &*scenario}), 4);
    check_clip_translation(evaluate_osbx_frame(*index.value, 12, arena), 0);
}
TEST_CASE("OSBX arithmetic loops retain adjacent fractions at the signed clock boundary", "[osbx][program]") {
    using namespace wormedit;
    auto track = temporal_track(); auto& loop = std::get<osbx_loop_t<>>(track.program[0].data);
    loop.start_ms = INT64_MIN; loop.count = UINT32_MAX; loop.keys.back().time_ms = 1;
    check_temporal_sample(track, {.milliseconds = INT64_MIN, .remainder = 1, .denominator = 2}, 0.5);
    check_temporal_sample(track, {.milliseconds = INT64_MIN + 1}, 0);
}

namespace {
void check_media_evaluation(bool value) { CHECK(value); }
void require_media_evaluation(bool value) { REQUIRE(value); }
}
TEST_CASE("OSBX Animation uses own explicit epoch while ancestor lifetime gates drawing", "[osbx][animation][epoch]") {
    using namespace wormedit;
    osbx_node_t animation{.id = "hero", .kind = osbx_node_kind_t::animation, .parent = "gate", .image = "frames"};
    animation.animation = storyboard_animation_t{.frame_count = 3, .frame_delay_ms = 100};
    osbx_document_t document{.images = {{.id = "frames", .file = "hero.png"}},
        .nodes = {{.id = "gate", .lifetime = osbx_lifetime_t{.begin_ms = 100, .end_ms = 500}}, animation}};
    auto index = osbx_evaluation_index_t::from(document); require_media_evaluation(index.value.has_value());
    frame_arena_t arena;
    auto hidden = evaluate_osbx_frame(*index.value, 99, arena);
    check_media_evaluation(hidden.is_success() && hidden.instances.empty());
    auto shown = evaluate_osbx_frame(*index.value, 100, arena);
    require_media_evaluation(shown.is_success() && shown.instances.size() == 1);
    check_media_evaluation(shown.instances.front().frame_index == 1);
    document.nodes.back().lifetime = osbx_lifetime_t{.begin_ms = 100, .end_ms = 500};
    index = osbx_evaluation_index_t::from(document); require_media_evaluation(index.value.has_value());
    shown = evaluate_osbx_frame(*index.value, 100, arena);
    check_media_evaluation(shown.instances.front().frame_index == 0);
}

TEST_CASE("OSBX Sample cues share canonical schedule query and survive index moves", "[osbx][sample][schedule]") {
    using namespace wormedit;
    osbx_node_t node{.id = "cue", .kind = osbx_node_kind_t::sample, .layer = osbx_layer_t::pass};
    node.sample.emplace(); node.sample->path = "tone.wav"; node.sample->time_ms = -10;
    const osbx_document_t document{.nodes = {node}};
    auto built = osbx_evaluation_index_t::from(document); require_media_evaluation(built.value.has_value());
    auto index = std::move(*built.value); auto copy = index;
    require_media_evaluation(copy.sample_schedule().events.size() == 1);
    const auto event = copy.sample_schedule().events.front();
    check_media_evaluation(copy.sample_for(event, storyboard_conditional_layer_t::pass) != nullptr);
    check_media_evaluation(copy.sample_for(event, storyboard_conditional_layer_t::neither) == nullptr);
    auto wrong = event; ++wrong.time_ms;
    check_media_evaluation(copy.sample_for(wrong) == nullptr);
    frame_arena_t arena;
    check_media_evaluation(evaluate_osbx_frame(index, 0, arena).is_success());
}

TEST_CASE("OSBX Sample ancestor temporal gates explicitly refuse cues rather than bypassing scenario", "[osbx][sample][program]") {
    using namespace wormedit;
    osbx_node_t cue{.id = "cue", .kind = osbx_node_kind_t::sample, .parent = "gate"};
    cue.sample = osbx_sample_t{.time_ms = 10, .path = "tone.wav"};
    osbx_document_t source{.nodes = {{.id = "gate"}, cue}};
    source.nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat",
        .keys = {{.id = "value", .value = {1}}}}}};
    auto index = osbx_evaluation_index_t::from(source); require_media_evaluation(index.value.has_value());
    check_media_evaluation(index.value->sample_schedule().events.empty());
    frame_arena_t arena;
    check_media_evaluation(!evaluate_osbx_frame(*index.value, 10, arena).is_success());
}
