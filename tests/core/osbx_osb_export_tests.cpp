#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/keyframe_simplify.h>
#include <wormedit/core/storyboard_preview.h>
#include <wormedit/formats/native_project_storage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <random>

namespace {
using namespace wormedit;
const std::array sizes{osbx_osb_image_size_t{.image_id = "image", .width = 64, .height = 48}};
auto scene() -> osbx_document_t {
    return {.images = {{.id = "image", .file = "sprite.png"}},
            .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image"}}};
}
auto op(std::string id, osbx_transform_kind_t kind, std::vector<double> value)
    -> osbx_transform_op_t {
    return {.id = std::move(id), .kind = kind, .track = {.default_value = std::move(value)}};
}
auto animated_rotation(std::int64_t end = 2000, double angle = 90) -> osbx_transform_op_t {
    auto rotation = op("rotation", osbx_transform_kind_t::rotate, {0});
    rotation.track.keys = {{.id = "a", .value = {0}},
                           {.id = "b", .time_ms = end, .value = {angle}}};
    return rotation;
}
auto orbit(std::int64_t end = 2000, double angle = 90) -> osbx_document_t {
    auto document = scene();
    document.nodes[0].parent = "parent";
    document.nodes[0].ops = {op("offset", osbx_transform_kind_t::translate, {100, 0})};
    document.nodes.push_back({.id = "parent", .ops = {animated_rotation(end, angle)}});
    return document;
}
auto convert(const osbx_document_t& document) -> osbx_osb_outcome_t {
    return convert_osbx_to_osb(document, {.image_sizes = sizes});
}
void require_unsupported(const osbx_osb_outcome_t& result, std::string_view reason) {
    REQUIRE_FALSE(result.value.model);
    REQUIRE_FALSE(result.diagnostics.empty());
    REQUIRE_FALSE(result.value.report.empty());
    CHECK(result.value.report.front().classification == osbx_osb_class_t::unsupported);
    CHECK(result.value.report.front().reason.find(reason) != std::string::npos);
}
auto property(const storyboard_sprite_t& sprite, storyboard_opcode_t opcode, std::int64_t time)
    -> std::array<double, 4> {
    const auto evaluated = evaluate_storyboard_property(sprite, opcode, time);
    REQUIRE(evaluated.is_success());
    REQUIRE(evaluated.value.present);
    return evaluated.value.values;
}
void check_corners(const visual_affine_t& a, const visual_affine_t& b) {
    visual_image_quad_request_t request{.source_width = 64, .source_height = 48,
                                        .destination = {.width = 640, .height = 480}, .transform = a};
    const auto expected = visual_image_quad_t::from(request);
    request.transform = b;
    const auto actual = visual_image_quad_t::from(request);
    REQUIRE(expected);
    REQUIRE(actual);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(std::hypot(expected->points[i].x - actual->points[i].x,
                          expected->points[i].y - actual->points[i].y) <= simplify_corner_error_px);
}
void check_roundtrip(const storyboard_document_model_t& model) {
    const auto emitted = emit_osb(model);
    REQUIRE(emitted.value);
    CHECK(parse_osb(*emitted.value).value.has_value());
}

void check_frame_activity(const storyboard_document_model_t& model, storyboard_time_t time, bool active) {
    const auto index = storyboard_preview_index_t::from(model);
    REQUIRE(index.value);
    storyboard_frame_arena_t arena;
    const auto frame = evaluate_storyboard_frame(model, *index.value, time, arena);
    REQUIRE(frame.is_success());
    CHECK(frame.instances.size() == (active ? 1 : 0));
}
void check_frame_activity(const storyboard_document_model_t& model, std::int64_t time, bool active) {
    check_frame_activity(model, storyboard_time_t{.milliseconds = time}, active);
}
auto reparse_osb_model(const storyboard_document_model_t& model) -> storyboard_document_model_t {
    const auto emitted = emit_osb(model);
    REQUIRE(emitted.value);
    const auto parsed = parse_osb(*emitted.value);
    REQUIRE(parsed.value);
    return *parsed.value;
}

void check_random_corners(const osbx_document_t& document, const storyboard_document_model_t& model) {
    const auto text = emit_osb(model);
    REQUIRE(text.value);
    const auto parsed = parse_osb(*text.value);
    REQUIRE(parsed.value);
    const auto index = osbx_evaluation_index_t::from(document);
    REQUIRE(index.value);
    frame_arena_t arena;
    std::mt19937 random{192};
    std::uniform_int_distribution<std::int64_t> time_distribution{0, 2000};
    for (int i = 0; i < 100; ++i) {
        const auto time = time_distribution(random);
        const auto frame = evaluate_osbx_frame(*index.value, time, arena);
        REQUIRE(frame.is_success());
        REQUIRE(frame.instances.size() == 1);
        const auto& sprite = parsed.value->sprites[0];
        const auto move = property(sprite, storyboard_opcode_t::move, time);
        const auto rotate = property(sprite, storyboard_opcode_t::rotate, time);
        const auto scale = property(sprite, storyboard_opcode_t::vector_scale, time);
        check_corners(frame.instances[0].world,
                       visual_affine_t::from(move[0], move[1], scale[0], scale[1], rotate[0]));
    }
}

auto movement(bool step) -> osbx_document_t {
    auto document = scene();
    auto move = op("move", osbx_transform_kind_t::translate, {0, 0});
    move.track.keys = {{.id = "a", .value = {0, 0},
                        .easing = step ? osbx_easing_t{osbx_step_easing_t{}} :
                                         osbx_easing_t{osbx_cubic_bezier_t{0.42, 0, 0.58, 1}}},
                       {.id = "b", .time_ms = 2000, .value = {100, 0}}};
    document.nodes[0].ops = {move};
    return document;
}

void check_movement(const osbx_osb_outcome_t& result, bool step) {
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == (step ? osbx_osb_class_t::exact : osbx_osb_class_t::baked));
    if (!step) return;
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::move, 1999)[0] == 0);
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::move, 2000)[0] == 100);
}
} // namespace

TEST_CASE("OSBX OSB dimensions are scoped by authored file across local image IDs", "[osbx][export]") {
    const std::array scoped{
        osbx_osb_image_size_t{.image_id = "image", .file = "other.png"},
        osbx_osb_image_size_t{.image_id = "image", .width = 64, .height = 48, .file = "sprite.png"}};
    const auto converted = convert_osbx_to_osb(scene(), {.image_sizes = scoped});
    REQUIRE(converted.value.model);
    CHECK(converted.value.model->sprites.size() == 1);
}

TEST_CASE("OSBX OSB static translated sprite is exact and deterministic", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].ops = {op("move", osbx_transform_kind_t::translate, {12, 34})};
    const auto result = convert(document);
    REQUIRE(result.value.model);
    REQUIRE(result.value.report.size() == 1);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(result.value.report[0].key_count == 5);
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::move, 0)[0] == 12);
    CHECK(result.value.model == convert(document).value.model);
    check_roundtrip(*result.value.model);
}

TEST_CASE("OSBX OSB constant tracks retain the authored frame lifetime", "[osbx][export][lifetime]") {
    auto document = scene();
    document.nodes[0].tracks.opacity.keys = {{.id = "begin", .value = {1}},
                                            {.id = "end", .time_ms = 2000, .value = {1}}};
    const auto converted = convert(document);
    REQUIRE(converted.value.model);
    check_frame_activity(*converted.value.model, 1000, true);
    check_frame_activity(*converted.value.model, 2000, true);
}

TEST_CASE("OSBX OSB static scenes cover an explicit long playback context", "[osbx][export][lifetime]") {
    const auto converted = convert_osbx_to_osb(scene(), {.image_sizes = sizes,
        .playback_span = osbx_osb_span_t{1234, 180000}, .maximum_samples_per_sprite = 2});
    REQUIRE(converted.value.model);
    CHECK(converted.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(converted.value.report[0].key_count == 5);
    check_frame_activity(*converted.value.model, 1233, false);
    check_frame_activity(*converted.value.model, 1234, true);
    check_frame_activity(*converted.value.model, 179999, true);
    check_frame_activity(*converted.value.model, 180001, false);
}

TEST_CASE("OSBX OSB playback context retains clamped native opacity tails", "[osbx][export][lifetime]") {
    auto document = scene();
    document.nodes[0].tracks.opacity.keys = {{.id = "begin", .time_ms = 100, .value = {0.25}},
                                            {.id = "end", .time_ms = 1000, .value = {0.75}}};
    const auto converted = convert_osbx_to_osb(document, {.image_sizes = sizes,
        .playback_span = osbx_osb_span_t{0, 2000}});
    REQUIRE(converted.value.model);
    const auto& sprite = converted.value.model->sprites.front();
    CHECK(property(sprite, storyboard_opcode_t::fade, 0)[0] == 0.25);
    CHECK(property(sprite, storyboard_opcode_t::fade, 1500)[0] == 0.75);
    check_frame_activity(*converted.value.model, 0, true);
    check_frame_activity(*converted.value.model, 1500, true);
    check_frame_activity(*converted.value.model, 2000, true);
}

TEST_CASE("OSBX OSB playback context rejects reversed endpoints", "[osbx][export][lifetime]") {
    const auto converted = convert_osbx_to_osb(scene(), {.image_sizes = sizes,
        .playback_span = osbx_osb_span_t{2, 1}});
    CHECK_FALSE(converted.value.model);
    REQUIRE_FALSE(converted.diagnostics.empty());
    CHECK(converted.diagnostics.front().code == failure_code_t::invalid_request);
}

TEST_CASE("OSBX OSB rotating parent bakes reduced keys and serialized corners", "[osbx][export]") {
    const auto document = orbit();
    const auto result = convert(document);
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::baked);
    CHECK(result.value.report[0].key_count < result.value.report[0].keys_before_simplify);
    CHECK(result.value.report[0].worst_error <= 1);
    check_random_corners(document, *result.value.model);
}

TEST_CASE("OSBX OSB explicit skew and raw matrices always refuse before sampling", "[osbx][export]") {
    for (const auto& blocked : {op("skew", osbx_transform_kind_t::skew, {0, 0}),
                                op("identity", osbx_transform_kind_t::matrix, {1, 0, 0, 1, 0, 0}),
                                op("translation", osbx_transform_kind_t::matrix, {1, 0, 0, 1, 5, 8})}) {
        auto document = scene();
        document.nodes[0].ops = {blocked};
        const auto result = convert_osbx_to_osb(document, {.maximum_evaluation_work = 0});
        require_unsupported(result, "no .osb equivalent");
        CHECK(result.value.report[0].reason.find(blocked.id) != std::string::npos);
        CHECK(result.value.report[0].reason.find("sprite") != std::string::npos);
    }
}

TEST_CASE("OSBX OSB ancestor and nested pivot refusal identifies original owners", "[osbx][export]") {
    for (const auto kind : {osbx_transform_kind_t::skew, osbx_transform_kind_t::matrix}) {
        auto document = scene();
        document.nodes[0].parent = "ancestor";
        auto pivot = op("pivot", osbx_transform_kind_t::pivot, {0, 0});
        pivot.children = {op("forbidden", kind, kind == osbx_transform_kind_t::matrix ?
                                                    std::vector<double>{1, 0, 0, 1, 0, 0} :
                                                    std::vector<double>{0, 0})};
        document.nodes.push_back({.id = "ancestor", .ops = {pivot}});
        const auto result = convert(document);
        require_unsupported(result, "owner ancestor");
        CHECK(result.value.report[0].reason.find("forbidden") != std::string::npos);
    }
}

TEST_CASE("OSBX OSB implicit shear blocks including axial submillisecond spans", "[osbx][export]") {
    for (const auto duration : {1, 1000}) {
        auto document = scene();
        document.nodes[0].parent = "scaled";
        document.nodes[0].ops = {animated_rotation(duration, 180)};
        document.nodes.push_back({.id = "scaled", .ops = {op("scale", osbx_transform_kind_t::scale, {2, 1})}});
        require_unsupported(convert(document), "Implicit shear");
    }
}

TEST_CASE("OSBX OSB nonuniform scale without rotation stays exact", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].parent = "scaled";
    document.nodes.push_back({.id = "scaled", .ops = {op("scale", osbx_transform_kind_t::scale, {2, 3})}});
    const auto result = convert(document);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
}

TEST_CASE("OSBX OSB density and finite sampling limits refuse explicitly", "[osbx][export]") {
    require_unsupported(convert(orbit(100, 180)), "density");
    require_unsupported(convert_osbx_to_osb(orbit(), {.image_sizes = sizes, .maximum_samples_per_sprite = 10}), "limit");
    require_unsupported(convert_osbx_to_osb(orbit(), {.image_sizes = sizes, .maximum_evaluation_work = 1}), "work limit");
}

TEST_CASE("OSBX OSB refuses missing dimensions and unsupported placeholders", "[osbx][export]") {
    require_unsupported(convert_osbx_to_osb(scene(), {}), "dimensions");
    auto document = scene();
    document.nodes[0].kind = osbx_node_kind_t::animation;
    document.nodes[0].image.reset();
    require_unsupported(convert(document), "OSBX-10");
}

TEST_CASE("OSBX OSB inherited alpha uses fade and additive parameter survives reparse", "[osbx][export]") {
    auto document = orbit();
    document.nodes[0].tracks.blend.default_value = osbx_blend_t::additive;
    document.nodes[1].tracks.opacity.keys = {{.id = "opacity-a", .value = {1}},
                                            {.id = "opacity-b", .time_ms = 2000, .value = {0.5}}};
    document.nodes[1].tracks.color.default_value = {0.5, 1, 1, 0.5};
    const auto result = convert(document);
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    const auto& sprite = result.value.model->sprites[0];
    CHECK(property(sprite, storyboard_opcode_t::fade, 1000)[0] == Catch::Approx(0.375).margin(1.0 / 255));
    const auto text = emit_osb(*result.value.model);
    REQUIRE(text.value);
    CHECK(text.value->find("P,0,0,2000,A") != std::string::npos);
    CHECK(parse_osb(*text.value).value.has_value());
}

TEST_CASE("OSBX OSB exact native easing and authored turns survive serialization", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].ops = {animated_rotation(2000, 720)};
    document.nodes[0].ops[0].track.keys.front().easing = osbx_osb_easing_t{3};
    const auto result = convert(document);
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    const auto& sprite = result.value.model->sprites.front();
    CHECK(property(sprite, storyboard_opcode_t::rotate, 1000)[0] == Catch::Approx(std::acos(-1)));
    const auto found = std::ranges::find(sprite.nodes, storyboard_opcode_t::rotate,
                                        [](const auto& node) { return node.command.opcode; });
    REQUIRE(found != sprite.nodes.end());
    CHECK(found->command.easing == 3);
}

TEST_CASE("OSBX OSB exact signed and zero scales retain their native linear spans", "[osbx][export]") {
    auto document = scene();
    auto scale = op("scale", osbx_transform_kind_t::scale, {1, 1});
    scale.track.keys = {{.id = "a", .value = {-1, 1}},
                        {.id = "b", .time_ms = 2000, .value = {1, 1}}};
    document.nodes[0].ops = {scale};
    const auto result = convert(document);
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(property(result.value.model->sprites.front(), storyboard_opcode_t::vector_scale, 500)[0] == -0.5);
}

TEST_CASE("OSBX OSB canvas contain mapping and origin use actual image dimensions", "[osbx][export]") {
    auto document = scene();
    document.canvas = {1280, 720};
    document.nodes[0].origin = "BottomRight";
    document.nodes[0].ops = {op("position", osbx_transform_kind_t::translate, {100, 100})};
    const auto result = convert(document);
    REQUIRE(result.value.model);
    const auto& sprite = result.value.model->sprites.front();
    CHECK(sprite.origin == "BottomRight");
    const auto move = property(sprite, storyboard_opcode_t::move, 0);
    CHECK(move[0] == 50);
    CHECK(move[1] == 110);
    CHECK(property(sprite, storyboard_opcode_t::vector_scale, 0)[0] == 0.5);
}

TEST_CASE("OSBX OSB Bezier motion bakes and exact step easing stays discontinuous", "[osbx][export]") {
    for (const bool step : {false, true})
        check_movement(convert(movement(step)), step);
}

TEST_CASE("OSBX OSB any unsupported sprite prevents partial model publication", "[osbx][export]") {
    auto document = scene();
    document.nodes.push_back({.id = "blocked", .kind = osbx_node_kind_t::sprite, .image = "image",
                              .ops = {op("skew", osbx_transform_kind_t::skew, {0, 0})}});
    const auto result = convert(document);
    REQUIRE(result.value.report.size() == 2);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(result.value.report[1].classification == osbx_osb_class_t::unsupported);
    CHECK_FALSE(result.value.model);
}

TEST_CASE("OSBX OSB analytic shear accepts cancelling anisotropic scales", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].parent = "parent";
    document.nodes[0].ops = {animated_rotation()};
    document.nodes.push_back({.id = "parent", .ops = {
        op("stretch", osbx_transform_kind_t::scale, {2, 1}),
        op("unstretch", osbx_transform_kind_t::scale, {0.5, 1})}});
    const auto result = convert(document);
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::baked);
}

TEST_CASE("OSBX OSB sampled constant implicit shear reports first time", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].ops = {op("rotate", osbx_transform_kind_t::rotate, {45}),
                             op("stretch", osbx_transform_kind_t::scale, {2, 1})};
    require_unsupported(convert(document), "Implicit shear first sampled at 0 ms");
}

TEST_CASE("OSBX OSB equal runs collapse exact output without moving the next span", "[osbx][export]") {
    auto document = scene();
    auto move = op("move", osbx_transform_kind_t::translate, {0, 0});
    move.track.keys = {{.id = "a", .value = {1, 0}},
                       {.id = "b", .time_ms = 100, .value = {1, 0}},
                       {.id = "c", .time_ms = 200, .value = {1, 0}},
                       {.id = "d", .time_ms = 300, .value = {2, 0}},
                       {.id = "e", .time_ms = 400, .value = {2, 0}}};
    document.nodes[0].ops = {move};
    const auto result = convert(document);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(result.value.report[0].key_count < result.value.report[0].keys_before_simplify);
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::move, 250)[0] == 1.5);
}

TEST_CASE("OSBX OSB identity uses FNV1a and draw order maps layers one to one", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].id = "a";
    document.nodes[0].layer = osbx_layer_t::foreground;
    document.nodes.push_back({.id = "background", .kind = osbx_node_kind_t::sprite, .image = "image"});
    const auto result = convert(document);
    REQUIRE(result.value.model);
    REQUIRE(result.value.model->sprites.size() == 2);
    CHECK(result.value.model->sprites[0].layer == "Background");
    CHECK(result.value.model->sprites[1].layer == "Foreground");
    CHECK(result.value.model->sprites[0].source_order < result.value.model->sprites[1].source_order);
    CHECK(result.value.model->sprites[1].stable_id == 0xaf63dc4c8601ec8cULL);
}

TEST_CASE("OSBX OSB malformed source retains structural diagnostics", "[osbx][export]") {
    auto document = scene();
    document.nodes[0].parent = "missing";
    const auto result = convert(document);
    CHECK_FALSE(result.value.model);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == failure_code_t::invalid_data);
}

TEST_CASE("OSBX OSB exact tracks clamp before independently timed native keys", "[osbx][export]") {
    auto document = scene();
    auto scale = op("scale", osbx_transform_kind_t::scale, {1, 1});
    scale.track.keys = {{.id = "scale-a", .value = {1, 1}},
                        {.id = "scale-b", .time_ms = 2000, .value = {2, 2}}};
    auto rotation = animated_rotation(2000, 90);
    rotation.track.keys.front().time_ms = 1000;
    rotation.track.keys.front().value = {45};
    document.nodes[0].ops = {scale, rotation};
    const auto result = convert(document);
    INFO(result.value.report.front().reason);
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::rotate, 500)[0] ==
          Catch::Approx(std::acos(-1) / 4));
}

namespace {
auto reparse_baked_conditionals(const osbx_document_t& document) -> storyboard_document_model_t {
    const auto result = convert(document);
    REQUIRE(result.value.model);
    REQUIRE(result.value.report.size() == 2);
    for (const auto& item : result.value.report) CHECK(item.classification == osbx_osb_class_t::baked);
    return reparse_osb_model(*result.value.model);
}
void check_conditional_layer_motion(const storyboard_document_model_t& model) {
    REQUIRE(model.sprites.size() == 2);
    CHECK(model.sprites[0].layer == "Fail");
    CHECK(model.sprites[1].layer == "Pass");
    for (const auto& sprite : model.sprites)
        CHECK(property(sprite, storyboard_opcode_t::move, 1000)[0] == Catch::Approx(50).margin(0.5));
}
void check_conditional_export_preview(const storyboard_document_model_t& model) {
    const auto index = storyboard_preview_index_t::from(model);
    REQUIRE(index.value);
    frame_arena_t arena;
    for (const auto choice : {storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail}) {
        const auto frame = evaluate_storyboard_frame(model, *index.value, 1000, arena,
            {.conditional_layer = choice});
        REQUIRE(frame.instances.size() == 1);
        CHECK(is_storyboard_layer_visible(frame.instances[0].layer, choice));
    }
}
} // namespace

TEST_CASE("OSB baking preserves animated Pass and Fail independently of preview choice", "[osbx][osb]") {
    auto document = movement(false);
    document.nodes[0].layer = osbx_layer_t::pass;
    auto fail = document.nodes[0];
    fail.id = "fail";
    fail.layer = osbx_layer_t::fail;
    fail.order = 3;
    fail.ops[0].id = "fail-move";
    fail.ops[0].track.keys[0].id = "fail-from";
    fail.ops[0].track.keys[1].id = "fail-to";
    document.nodes.push_back(fail);
    const auto reparsed = reparse_baked_conditionals(document);
    check_conditional_layer_motion(reparsed);
    check_conditional_export_preview(reparsed);
}

TEST_CASE("OSB export clips finite flat lifetime without extending to playback range", "[osbx][osb][lifetime]") {
    auto document = scene();
    document.nodes[0].lifetime = osbx_lifetime_t{.begin_ms = -20, .end_ms = 20};
    const auto result = convert_osbx_to_osb(document, {.image_sizes = sizes,
        .playback_span = osbx_osb_span_t{.begin_time_ms = -100, .end_time_ms = 100}});
    REQUIRE(result.value.model);
    const auto parsed = reparse_osb_model(*result.value.model);
    const auto index = storyboard_preview_index_t::from(parsed);
    REQUIRE(index.value);
    REQUIRE(index.value->sources.size() == 1);
    CHECK(index.value->sources[0].begin_time_ms == -20);
    CHECK(index.value->sources[0].end_time_ms == 20);
    check_frame_activity(parsed, 20, true);
    check_frame_activity(parsed, storyboard_time_t{.milliseconds = 20, .remainder = 1, .denominator = 1000}, false);
}

TEST_CASE("OSB export refuses ancestor or mixed visibility lifetime clipping atomically", "[osbx][osb][lifetime]") {
    auto document = scene();
    document.nodes[0].parent = "parent";
    document.nodes.push_back({.id = "parent", .lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 10}});
    require_unsupported(convert(document), "Ancestor lifetime");
    document.nodes[0].parent.reset();
    document.nodes.pop_back();
    document.nodes[0].lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 10};
    document.nodes[0].tracks.visibility.keys = {{.id = "a", .value = false}, {.id = "b", .time_ms = 5, .value = true}};
    require_unsupported(convert(document), "animated visibility");
}

TEST_CASE("OSB capability query shares converter preflight refusal reasons", "[osbx][osb][capability]") {
    auto document = scene();
    document.nodes[0].ops = {op("shear", osbx_transform_kind_t::skew, {0.1, 0})};
    const auto before = document;
    const auto known = osbx_osb_known_unsupported(document);
    REQUIRE(known.size() == 1);
    const auto converted = convert(document);
    REQUIRE(converted.value.report.size() == 1);
    CHECK(known[0].classification == osbx_osb_class_t::unsupported);
    CHECK(known[0].node_id == "sprite");
    CHECK(known[0].reason == converted.value.report[0].reason);
    CHECK(document == before);
}
TEST_CASE("OSB capability omission means unknown even when dimensions are unavailable", "[osbx][osb][capability]") {
    const auto document = scene();
    CHECK(osbx_osb_known_unsupported(document).empty());
    const auto conversion = convert_osbx_to_osb(document, {});
    REQUIRE(conversion.value.report.size() == 1);
    CHECK(conversion.value.report[0].classification == osbx_osb_class_t::unsupported);
    CHECK(conversion.value.report[0].reason.find("dimensions") != std::string::npos);
}
TEST_CASE("OSB capability work exhaustion never invents an unsupported result", "[osbx][osb][capability]") {
    auto document = scene();
    document.nodes[0].ops = {op("shear", osbx_transform_kind_t::skew, {0.1, 0})};
    CHECK(osbx_osb_known_unsupported(document, 0).empty());
    CHECK(osbx_osb_known_unsupported(document, 1).empty());
    CHECK(osbx_osb_known_unsupported(document, 2).size() == 1);
}
TEST_CASE("OSB capability uses shared lifetime refusal without baking", "[osbx][osb][capability]") {
    auto document = scene();
    document.nodes[0].parent = "parent";
    document.nodes.push_back({.id = "parent", .lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 10}});
    const auto known = osbx_osb_known_unsupported(document);
    REQUIRE(known.size() == 1);
    const auto converted = convert(document);
    REQUIRE(converted.value.report.size() == 1);
    CHECK(known[0].reason == converted.value.report[0].reason);
}
TEST_CASE("OSB capability invalid hierarchy is unknown and terminates", "[osbx][osb][capability]") {
    auto document = scene();
    document.nodes[0].parent = "sprite";
    CHECK(osbx_osb_known_unsupported(document).empty());
    document.nodes[0].parent = "missing";
    CHECK(osbx_osb_known_unsupported(document).empty());
}
TEST_CASE("OSB capability reports shared animation placeholder refusal", "[osbx][osb][capability]") {
    auto document = scene();
    document.nodes[0].kind = osbx_node_kind_t::animation;
    document.nodes[0].image.reset();
    const auto known = osbx_osb_known_unsupported(document);
    REQUIRE(known.size() == 1);
    const auto converted = convert(document);
    REQUIRE(converted.value.report.size() == 1);
    CHECK(known[0].reason == converted.value.report[0].reason);
}
TEST_CASE("OSB capability shares animated blend refusal", "[osbx][osb][capability]") {
    auto document = scene();
    document.nodes[0].tracks.blend.keys = {
        {.id = "normal", .value = osbx_blend_t::normal},
        {.id = "additive", .time_ms = 10, .value = osbx_blend_t::additive}};
    const auto known = osbx_osb_known_unsupported(document);
    REQUIRE(known.size() == 1);
    const auto converted = convert(document);
    REQUIRE(converted.value.report.size() == 1);
    CHECK(known[0].reason == converted.value.report[0].reason);
}

TEST_CASE("OSB capability leaves authored Lua scenes unknown without running source", "[osbx][osb][capability]") {
    auto document = scene();
    const std::string source = "error('capability must never execute this')";
    document.nodes[0].tracks.opacity.lua = osbx_lua_track_link_t{.link = {.id = "link",
        .source = {.frozen_source = source, .source_reference = "scripts/source.lua",
            .source_hash = native_project_content_checksum(source)}}};
    document.nodes[0].ops = {op("shear", osbx_transform_kind_t::skew, {0.1, 0})};
    REQUIRE(validate_osbx_document(document).value);
    CHECK(osbx_osb_known_unsupported(document).empty());
}

TEST_CASE("OSBX clips flatten into both conditional OSB branches before classification", "[osbx][export][clips]") {
    auto document = scene();
    document.nodes[0].layer = osbx_layer_t::pass;
    document.nodes[0].uses = {{.id = "pass-use", .clip = "motion", .time_scale = 0.5}};
    document.nodes.push_back({.id = "fail", .kind = osbx_node_kind_t::sprite, .layer = osbx_layer_t::fail,
        .image = "image", .uses = {{.id = "fail-use", .clip = "motion", .time_scale = 2}}});
    document.clips = {{.id = "motion", .ops = {{.id = "motion-op", .track = {
        .default_value = {0, 0}, .keys = {{.id = "a", .value = {0, 0}}, {.id = "b", .time_ms = 10, .value = {20, 0}}}}}}}};
    const auto result = convert(document); REQUIRE(result.value.model);
    REQUIRE(result.value.model->sprites.size() == 2);
    const auto& sprites = result.value.model->sprites;
    const auto pass = std::ranges::find(sprites, "Pass", &storyboard_sprite_t::layer);
    const auto fail = std::ranges::find(sprites, "Fail", &storyboard_sprite_t::layer);
    REQUIRE(pass != sprites.end());
    REQUIRE(fail != sprites.end());
    CHECK(property(*pass, storyboard_opcode_t::move, 5)[0] == Catch::Approx(20));
    CHECK(property(*fail, storyboard_opcode_t::move, 5)[0] == Catch::Approx(5));
    check_roundtrip(*result.value.model);
}

TEST_CASE("OSBX clip fractional keys and forbidden transforms refuse instead of rounding", "[osbx][export][clips]") {
    auto document = scene();
    document.nodes[0].uses = {{.id = "use", .clip = "motion", .time_scale = 0.5}};
    document.clips = {{.id = "motion", .ops = {{.id = "clip-op", .track = {.default_value = {0, 0},
        .keys = {{.id = "first", .value = {0, 0}}, {.id = "last", .time_ms = 3, .value = {3, 0}}}}}}}};
    SECTION("fractional") { require_unsupported(convert(document), "fractional"); }
    SECTION("shear") {
        document.nodes[0].uses[0].time_scale = 1;
        document.clips[0].ops[0].kind = osbx_transform_kind_t::skew;
        require_unsupported(convert(document), "no .osb equivalent");
    }
}

TEST_CASE("OSBX exact temporal opacity emits native OSB L and T nodes", "[osbx][program][export]") {
    auto document = scene();
    document.nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "loop", .start_ms = 100, .count = 3,
        .keys = {{.id = "a", .value = {0}}, {.id = "b", .time_ms = 10, .value = {1}}}}}};
    const auto loop = convert_osbx_to_osb(document, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{0, 200}});
    REQUIRE(loop.value.model);
    CHECK(loop.value.report[0].classification == osbx_osb_class_t::exact);
    const auto text = emit_osb(*loop.value.model);
    REQUIRE(text.value);
    CHECK(text.value->find(" L,100,3\n") != std::string::npos);
    document.nodes[0].tracks.opacity.program = {{.data = osbx_trigger_t<>{.id = "trigger",
        .trigger = {.name = *storyboard_trigger_name_t::from("Passing"), .start_time_ms = 0, .end_time_ms = 200},
        .keys = {{.id = "ta", .value = {0}}, {.id = "tb", .time_ms = 10, .value = {1}}}}}};
    const auto trigger = convert_osbx_to_osb(document, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{0, 200}});
    REQUIRE(trigger.value.model);
    CHECK(emit_osb(*trigger.value.model).value->find(" T,Passing,0,200\n") != std::string::npos);
}

namespace {
auto opacity_loop_scene(std::int64_t first = 0) -> osbx_document_t {
    auto source = scene();
    source.nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .start_ms = 100, .count = 3,
        .keys = {{.id = "a", .time_ms = first, .value = {0}}, {.id = "b", .time_ms = 10, .value = {1}}}}}};
    return source;
}
}
TEST_CASE("OSBX symbolic loop reconstruction preserves wraps gaps and final holds", "[osbx][program][export]") {
    const auto first = GENERATE(0, 3);
    const auto source = opacity_loop_scene(first);
    const auto converted = convert_osbx_to_osb(source, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{0, 200}});
    REQUIRE(converted.value.model);
    CHECK(converted.value.report[0].classification == osbx_osb_class_t::exact);
    const auto& sprite = converted.value.model->sprites[0];
    for (const int time : {99, 100, 102, 103, 109, 110, 112, 113, 120, 130, 200}) {
        const auto expected = sample_osbx_numeric_track(source.nodes[0].tracks.opacity, time);
        CHECK(property(sprite, storyboard_opcode_t::fade, time)[0] == Catch::Approx(expected[0]));
    }
}
TEST_CASE("OSBX dense temporal overlap refuses rather than skipping the shared density cap", "[osbx][program][export]") {
    auto source = opacity_loop_scene();
    source.nodes[0].tracks.opacity.keys = {{.id = "root-a", .time_ms = 107, .value = {0.2}},
        {.id = "root-b", .time_ms = 120, .value = {0.4}}};
    const auto result = convert_osbx_to_osb(source, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{95, 135}});
    require_unsupported(result, "keyframe density");
}
TEST_CASE("OSBX nonexact triggers require an explicit sealed finite bake scenario", "[osbx][program][export]") {
    auto source = scene();
    source.nodes[0].tracks.opacity.keys = {{.id = "root", .value = {0.5}}};
    source.nodes[0].tracks.opacity.program = {{.data = osbx_trigger_t<>{.id = "trigger",
        .trigger = {.name = *storyboard_trigger_name_t::from("Passing")},
        .keys = {{.id = "key", .value = {0.75}}}}}};
    require_unsupported(convert(source), "explicit sealed scenario");
    const auto empty = storyboard_trigger_scenario_t::from({}); REQUIRE(empty);
    require_unsupported(convert_osbx_to_osb(source, {.image_sizes = sizes, .scenario = &*empty}), "finite playback span");
    const auto baked = convert_osbx_to_osb(source, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{0,10}, .scenario = &*empty});
    REQUIRE(baked.value.model);
    CHECK(baked.value.report[0].classification == osbx_osb_class_t::baked);
    CHECK(property(baked.value.model->sprites[0], storyboard_opcode_t::fade, 5)[0] == Catch::Approx(0.5));
}
namespace {
void check_native_loop_time(const osbx_document_t& source, const storyboard_sprite_t& sprite, storyboard_time_t time) {
    const auto actual = evaluate_storyboard_property(sprite, storyboard_opcode_t::fade, time);
    REQUIRE(actual.is_success()); REQUIRE(actual.value.present);
    CHECK(actual.value.values[0] == Catch::Approx(sample_osbx_numeric_track(source.nodes[0].tracks.opacity, time)[0]));
}
}
TEST_CASE("OSBX symbolic loops compare to native OSB at fractional wrap boundaries", "[osbx][program][export]") {
    const auto source = opacity_loop_scene();
    const auto result = convert_osbx_to_osb(source, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{0, 200}});
    REQUIRE(result.value.model);
    const auto& sprite = result.value.model->sprites[0];
    for (const std::int64_t whole : {99, 100, 109, 110, 129, 130}) {
        for (const std::uint32_t fraction : {0, 1, 3}) {
            const storyboard_time_t time{.milliseconds = whole, .remainder = fraction, .denominator = 4};
            check_native_loop_time(source, sprite, time);
        }
    }
}
TEST_CASE("OSBX finite temporal composition bakes a bounded held step", "[osbx][program][export]") {
    auto source = scene();
    auto& track = source.nodes[0].tracks.opacity;
    track.keys = {{.id = "root", .time_ms = -10, .value = {0.2}}};
    track.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = 1,
        .keys = {{.id = "constant", .value = {0.5}}}}}};
    const auto result = convert_osbx_to_osb(source, {.image_sizes = sizes, .playback_span = osbx_osb_span_t{-10, 10}});
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::baked);
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::fade, -1)[0] == Catch::Approx(0.2));
    CHECK(property(result.value.model->sprites[0], storyboard_opcode_t::fade, 0)[0] == Catch::Approx(0.5));
}
TEST_CASE("OSBX huge native loop counts keep output bounded at signed clock origins", "[osbx][program][export]") {
    auto source = opacity_loop_scene();
    auto& loop = std::get<osbx_loop_t<>>(source.nodes[0].tracks.opacity.program[0].data);
    loop.start_ms = INT64_MIN; loop.count = UINT32_MAX; loop.keys.back().time_ms = 1;
    const auto result = convert_osbx_to_osb(source, {.image_sizes = sizes});
    REQUIRE(result.value.model);
    CHECK(result.value.report[0].classification == osbx_osb_class_t::exact);
    CHECK(result.value.model->sprites[0].nodes.size() < 16);
    const auto value = evaluate_storyboard_property(result.value.model->sprites[0], storyboard_opcode_t::fade,
        storyboard_time_t{.milliseconds = INT64_MIN, .remainder = 1, .denominator = 2});
    REQUIRE(value.is_success()); REQUIRE(value.value.present);
    CHECK(value.value.values[0] == Catch::Approx(0.5));
}

TEST_CASE("OSBX Animation export proves reparsed phase and every concrete frame geometry", "[osbx][animation][export]") {
    auto source = scene(); auto& node = source.nodes[0]; node.kind = osbx_node_kind_t::animation;
    node.animation = storyboard_animation_t{.frame_count = 2, .frame_delay_ms = 12.5};
    const std::array geometry{osbx_osb_image_size_t{"image",64,48,"sprite.png"},
        osbx_osb_image_size_t{"image",64,48,"sprite0.png"}, osbx_osb_image_size_t{"image",64,48,"sprite1.png"}};
    const auto result = convert_osbx_to_osb(source, {.image_sizes = geometry, .playback_span = osbx_osb_span_t{0,100}});
    REQUIRE(result.value.model);
    CHECK(result.value.model->sprites[0].animation == node.animation);
    CHECK_FALSE(convert_osbx_to_osb(source, {.image_sizes = sizes}).value.model);
    CHECK_FALSE(convert_osbx_to_osb(source, {.image_sizes = geometry, .playback_span = osbx_osb_span_t{10,100}}).value.model);
}
TEST_CASE("OSBX Sample export preserves authored events and refuses gated properties atomically", "[osbx][sample][export]") {
    osbx_node_t cue{.id = "cue", .kind = osbx_node_kind_t::sample, .layer = osbx_layer_t::foreground};
    cue.sample = osbx_sample_t{.time_ms = -10, .path = "tone.wav"};
    osbx_document_t source{.nodes = {cue}};
    const auto result = convert_osbx_to_osb(source, {}); REQUIRE(result.value.model);
    REQUIRE(result.value.model->samples.size() == 1);
    CHECK(result.value.model->samples[0].time_ms == -10);
    CHECK_FALSE(result.value.model->samples[0].volume);
    source.nodes[0].tracks.opacity.default_value = {0.5};
    CHECK_FALSE(convert_osbx_to_osb(source, {}).value.model);
}

TEST_CASE("OSBX Animation without a finite output lifetime refuses instead of emitting a one-instant scene", "[osbx][animation][export]") {
    auto source = scene(); source.nodes[0].kind = osbx_node_kind_t::animation;
    source.nodes[0].animation = storyboard_animation_t{.frame_count = 1, .frame_delay_ms = 10};
    const std::array geometry{osbx_osb_image_size_t{"image",64,48,"sprite.png"},
        osbx_osb_image_size_t{"image",64,48,"sprite0.png"}};
    const auto result = convert_osbx_to_osb(source, {.image_sizes = geometry});
    REQUIRE_FALSE(result.value.model);
    CHECK(result.value.report[0].reason.find("finite") != std::string::npos);
}

TEST_CASE("OSBX independently authored same-valued Samples remain separate events", "[osbx][sample][export]") {
    osbx_node_t cue{.id = "first", .kind = osbx_node_kind_t::sample};
    cue.sample = osbx_sample_t{.time_ms = 10, .path = "tone.wav"};
    auto second = cue; second.id = "second";
    const osbx_document_t source{.nodes = {cue, second}};
    const auto result = convert_osbx_to_osb(source, {}); REQUIRE(result.value.model);
    REQUIRE(result.value.model->samples.size() == 2);
    CHECK(result.value.model->samples[0].stable_id != result.value.model->samples[1].stable_id);
    const auto emitted = emit_osb(*result.value.model); REQUIRE(emitted.value);
    const auto parsed = parse_osb(*emitted.value); REQUIRE(parsed.value);
    CHECK(parsed.value->samples.size() == 2);
}
