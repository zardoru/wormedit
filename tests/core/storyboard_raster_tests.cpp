#include <wormedit/core/storyboard_raster.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/formats/native_project.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <new>
#include <iterator>
#include "../support/temporary_directory.h"

using namespace wormedit;

namespace {
thread_local bool count_raster_allocations{};
thread_local std::size_t raster_allocations{};
}
void* operator new(std::size_t size) {
    if (count_raster_allocations) ++raster_allocations;
    if (auto* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

TEST_CASE("raster clock retains exact fractional milliseconds", "[raster]") {
    const storyboard_raster_clock_t clock;
    const auto time = clock.time_at(1);
    REQUIRE(time);
    CHECK(*time == storyboard_time_t{.milliseconds = 16, .remainder = 2, .denominator = 3});
    CHECK(storyboard_raster_clock_t{.numerator = 30000, .denominator = 1001}.time_at(30)->milliseconds == 1001);
}

TEST_CASE("raster clock rejects invalid rates and bounds large timestamps", "[raster]") {
    const storyboard_raster_clock_t clock;
    CHECK_FALSE(storyboard_raster_clock_t{.numerator = 0}.time_at(0));
    CHECK_FALSE(storyboard_raster_clock_t{.denominator = 0}.time_at(0));
    const auto large = storyboard_raster_clock_t{.numerator = std::numeric_limits<std::uint32_t>::max()}
        .time_at(std::numeric_limits<std::uint64_t>::max());
    REQUIRE(large);
    CHECK(large->milliseconds == 4'294'967'297'000LL);
    CHECK(large->remainder == 0);
    CHECK(clock.time_at(std::numeric_limits<std::uint64_t>::max()).error() == failure_code_t::arithmetic_overflow);
}

namespace {
void write_ppm(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> rgba);

auto solid_image(std::array<std::uint8_t, 4> color) -> std::shared_ptr<const visual::rgba_image_t> {
    return std::make_shared<const visual::rgba_image_t>(visual::rgba_image_t{
        .width = 1, .height = 1, .pixels = {color[0], color[1], color[2], color[3]}});
}

auto make_raster(storyboard_raster_options_t options) -> storyboard_raster_t {
    auto result = storyboard_raster_t::from(options);
    REQUIRE(result);
    return std::move(*result);
}

auto render_frame(storyboard_raster_t& raster, std::span<const storyboard_draw_instance_t> draw,
    std::span<const storyboard_raster_image_t> images, frame_arena_t& arena,
    storyboard_conditional_layer_t choice = storyboard_conditional_layer_t::pass)
    -> result_t<storyboard_raster_frame_t, wormedit::error_t<failure_code_t>> {
    auto frame = raster.render(draw, images, arena, choice);
    REQUIRE(frame);
    return frame;
}

auto pixel(storyboard_raster_frame_t frame, std::size_t x, std::size_t y) -> std::array<std::uint8_t, 4> {
    const auto offset = (y * frame.width + x) * 4;
    return {frame.pixels[offset], frame.pixels[offset + 1], frame.pixels[offset + 2], frame.pixels[offset + 3]};
}

void check_pixel(storyboard_raster_frame_t frame, std::size_t x, std::size_t y,
    std::array<std::uint8_t, 4> expected) {
    CAPTURE(x, y);
    CHECK(pixel(frame, x, y) == expected);
}

auto frame_hash(std::span<const std::uint8_t> pixels) -> std::uint64_t {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : pixels) hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}

auto example_scene() -> osbx_document_t {
    osbx_document_t scene{.images = {{.id = "texture", .file = "texture.png"}}};
    osbx_node_t background{.id = "background", .kind = osbx_node_kind_t::sprite, .image = "texture",
        .origin = "TopLeft", .ops = {{.id = "scale", .kind = osbx_transform_kind_t::scale,
            .track = {.default_value = {40, 30}}}}};
    background.tracks.color.default_value = {0.2, 0.3, 0.4, 1};
    scene.nodes.push_back(background);
    osbx_node_t sprite{.id = "moving", .kind = osbx_node_kind_t::sprite, .image = "texture",
        .ops = {{.id = "matrix", .kind = osbx_transform_kind_t::matrix,
            .track = {.default_value = {10, 2, 4, 8, 0, 0}}},
            {.id = "move", .track = {.default_value = {0, 0}, .keys = {
                {.id = "a", .time_ms = 0, .value = {100, 120}},
                {.id = "b", .time_ms = 1000, .value = {480, 300}}}}}}};
    sprite.tracks.opacity.default_value = {0.7};
    scene.nodes.push_back(sprite);
    sprite.id = "glow";
    sprite.layer = osbx_layer_t::pass;
    sprite.tracks.blend.default_value = osbx_blend_t::additive;
    sprite.tracks.color.default_value = {0.5, 0.8, 1, 0.6};
    sprite.ops[1].track.keys[0].value = {450, 130};
    sprite.ops[1].track.keys[1].value = {160, 350};
    scene.nodes.push_back(sprite);
    sprite.id = "fail";
    sprite.layer = osbx_layer_t::fail;
    sprite.tracks.color.default_value = {1, 0, 0, 1};
    scene.nodes.push_back(sprite);
    for (auto& node : scene.nodes) {
        for (auto& op : node.ops) {
            op.id = node.id + "/" + op.id;
            for (auto& key : op.track.keys) key.id = op.id + "/" + key.id;
        }
    }
    return scene;
}

auto example_images() -> std::array<storyboard_raster_image_t, 1> {
    visual::rgba_image_t image{.width = 16, .height = 16};
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            image.pixels.insert(image.pixels.end(), {static_cast<std::uint8_t>(x * 17),
                static_cast<std::uint8_t>(y * 17), static_cast<std::uint8_t>((x ^ y) * 17),
                static_cast<std::uint8_t>(64 + ((x + y) % 4) * 63)});
        }
    }
    return {{{.path = "texture.png", .image = std::make_shared<const visual::rgba_image_t>(std::move(image))}}};
}

// Use the very same evaluated instance conversion as the interactive presenter.
void evaluate_example(const osbx_document_t& scene, const osbx_evaluation_index_t& index,
    storyboard_time_t time, frame_arena_t& arena, std::vector<storyboard_draw_instance_t>& draw,
    storyboard_conditional_layer_t choice = storyboard_conditional_layer_t::pass) {
    const auto evaluated = evaluate_osbx_frame(index, time, arena, {.conditional_layer = choice});
    REQUIRE(evaluated.is_success());
    draw.clear();
    for (const auto& instance : evaluated.instances) {
        const auto node = std::ranges::find(scene.nodes, instance.id, &osbx_node_t::id);
        REQUIRE(node != scene.nodes.end());
        draw.push_back(storyboard_draw_instance_t::from(scene, *node, scene.images[0]).with_evaluation(instance));
    }
}
} // namespace

TEST_CASE("raster bilinear filtering premultiplies before filtering and blends on black", "[raster]") {
    auto raster = make_raster({.width = 4, .height = 1});
    frame_arena_t arena;
    const std::array images{storyboard_raster_image_t{.path = "edge", .image =
        std::make_shared<const visual::rgba_image_t>(visual::rgba_image_t{
            .width = 2, .height = 1, .pixels = {255, 0, 0, 255, 0, 255, 0, 0}})}};
    const std::array draw{storyboard_draw_instance_t{.origin = "TopLeft", .path = "edge",
        .transform = {2, 0, 0, 1, 0, 0}, .logical_width = 4, .logical_height = 1}};
    const auto frame = render_frame(raster, draw, images, arena);

    check_pixel(*frame, 0, 0, {255, 0, 0, 255});
    check_pixel(*frame, 1, 0, {191, 0, 0, 255});
    check_pixel(*frame, 2, 0, {64, 0, 0, 255});
    check_pixel(*frame, 3, 0, {0, 0, 0, 255});
}

TEST_CASE("raster normal additive and colour multiply retain ordered composition", "[raster]") {
    auto raster = make_raster({.width = 1, .height = 1});
    frame_arena_t arena;
    const std::array images{storyboard_raster_image_t{.path = "white", .image = solid_image({255, 255, 255, 128})}};
    storyboard_draw_instance_t sprite{.origin = "TopLeft", .path = "white",
        .transform = {1, 0, 0, 1, 0, 0}, .logical_width = 1, .logical_height = 1,
        .opacity = 0.5, .red = 128, .green = 64, .blue = 0};
    std::array draw{sprite, sprite};
    auto frame = render_frame(raster, draw, images, arena);

    check_pixel(*frame, 0, 0, {56, 28, 0, 255});
    draw[1].blend = osbx_blend_t::additive;
    arena.reset();
    frame = render_frame(raster, draw, images, arena);

    check_pixel(*frame, 0, 0, {64, 32, 0, 255});
}

namespace {
void check_sheared_coverage(storyboard_raster_frame_t frame) {
    CHECK(pixel(frame, 0, 0)[0] == 255);
    CHECK(pixel(frame, 2, 0)[0] == 0);
    CHECK(pixel(frame, 0, 1)[0] == 0);
    CHECK(pixel(frame, 2, 1)[0] == 255);
}
} // namespace

TEST_CASE("raster sheared quad uses inverse affine coverage with flipped images", "[raster]") {
    auto raster = make_raster({.width = 4, .height = 2});
    frame_arena_t arena;
    const std::array images{storyboard_raster_image_t{.path = "white", .image = solid_image({255, 255, 255, 255})}};
    std::array draw{storyboard_draw_instance_t{.origin = "TopLeft", .path = "white",
        .transform = {2, 0, 1, 2, 0, 0}, .logical_width = 4, .logical_height = 2}};
    auto frame = render_frame(raster, draw, images, arena);

    check_sheared_coverage(*frame);
    draw[0].transform = {-2, 0, 1, 2, 2, 0};
    arena.reset();
    frame = render_frame(raster, draw, images, arena);

    CHECK(pixel(*frame, 0, 0)[0] == 255);
    CHECK(pixel(*frame, 2, 1)[0] == 255);
}

TEST_CASE("raster fits the logical canvas and samples full source resolution", "[raster]") {
    auto raster = make_raster({.width = 8, .height = 4});
    frame_arena_t arena;
    const std::array images{storyboard_raster_image_t{.path = "wide", .image =
        std::make_shared<const visual::rgba_image_t>(visual::rgba_image_t{.width = 4, .height = 1,
            .pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}})}};
    const std::array draw{storyboard_draw_instance_t{.origin = "TopLeft", .path = "wide",
        .transform = {0.5, 0, 0, 2, 0, 0}, .logical_width = 2, .logical_height = 2}};
    const auto frame = render_frame(raster, draw, images, arena);

    check_pixel(*frame, 0, 0, {0, 0, 0, 255});
    check_pixel(*frame, 2, 0, {255, 0, 0, 255});
    check_pixel(*frame, 3, 0, {0, 255, 0, 255});
    check_pixel(*frame, 4, 0, {0, 0, 255, 255});
    check_pixel(*frame, 5, 0, {255, 255, 255, 255});
    check_pixel(*frame, 6, 0, {0, 0, 0, 255});
    auto oversized = draw;
    oversized[0].transform = {4, 0, 0, 2, -4, 0};
    arena.reset();
    const auto clipped = render_frame(raster, oversized, images, arena);

    check_pixel(*clipped, 0, 0, {0, 0, 0, 255});
    check_pixel(*clipped, 6, 0, {0, 0, 0, 255});
}

TEST_CASE("raster example golden frames at 1080p and UHD", "[raster][slow]") {
    const auto scene = example_scene();
    const auto index = osbx_evaluation_index_t::from(scene);
    REQUIRE(index.value);
    const auto images = example_images();
    frame_arena_t arena;
    std::vector<storyboard_draw_instance_t> draw;
    auto raster = make_raster({.threads = 4});
    // FNV-1a over complete opaque RGBA8 frames, including pillarboxes.
    const std::array<std::uint64_t, 3> golden{
        0x1cacd082d7690e35ULL, 0x4576323fdb57915cULL, 0x2c77561b8f02d3b9ULL};
    for (std::size_t i = 0; i < golden.size(); ++i) {
        evaluate_example(scene, *index.value, *storyboard_raster_clock_t{}.time_at(i * 17), arena, draw);
        const auto frame = render_frame(raster, draw, images, arena);

        CAPTURE(i, frame_hash(frame->pixels));
        CHECK(frame_hash(frame->pixels) == golden[i]);
        if (const auto* directory = std::getenv("WORMEDIT_RASTER_GOLDEN_OUTPUT")) {
            std::filesystem::create_directories(directory);
            write_ppm(std::filesystem::path{directory} / ("frame-" + std::to_string(i * 17) + ".ppm"),
                frame->width, frame->height, frame->pixels);
        }
    }
    auto uhd = make_raster({.width = 3840, .height = 2160, .threads = 4, .frame_slots = 1});
    evaluate_example(scene, *index.value, *storyboard_raster_clock_t{}.time_at(17), arena, draw);
    const auto frame = render_frame(uhd, draw, images, arena);

    CHECK(frame->pixels.size() == 33'177'600);
    CAPTURE(frame_hash(frame->pixels));
    CHECK(frame_hash(frame->pixels) == 0x10f279f6da28757cULL);
}

TEST_CASE("raster frame order and worker count cannot change pixels", "[raster]") {
    const auto scene = example_scene();
    const auto index = osbx_evaluation_index_t::from(scene);
    REQUIRE(index.value);
    const auto images = example_images();
    frame_arena_t arena;
    std::vector<storyboard_draw_instance_t> draw;
    std::array<std::vector<std::uint8_t>, 3> reference;
    for (const auto threads : {1U, 2U, 4U, 64U}) {
        auto raster = make_raster({.width = 160, .height = 120, .threads = threads});
        for (const auto i : {2U, 0U, 1U, 0U}) {
            evaluate_example(scene, *index.value, *storyboard_raster_clock_t{}.time_at(i * 17), arena, draw);
            const auto frame = render_frame(raster, draw, images, arena);

            if (reference[i].empty()) reference[i].assign(frame->pixels.begin(), frame->pixels.end());
            CHECK(std::ranges::equal(frame->pixels, reference[i]));
        }
    }
}

TEST_CASE("raster keeps a fixed frame ring and reuses warmed arena storage", "[raster]") {
    auto raster = make_raster({.width = 2, .height = 2});
    frame_arena_t arena;
    const std::array images{storyboard_raster_image_t{.path = "white", .image = solid_image({255, 255, 255, 255})}};
    const std::array draw{storyboard_draw_instance_t{.origin = "TopLeft", .path = "white",
        .transform = {1, 0, 0, 1, 0, 0}, .logical_width = 2, .logical_height = 2}};
    const auto first = render_frame(raster, draw, images, arena);

    const auto committed = arena.committed_bytes();
    arena.reset();
    const auto second = render_frame(raster, draw, images, arena);

    CHECK(first->pixels.data() != second->pixels.data());
    arena.reset();
    raster_allocations = 0;
    count_raster_allocations = true;
    const auto third = raster.render(draw, images, arena);
    count_raster_allocations = false;
    REQUIRE(third);
    CHECK(raster_allocations == 0);
    CHECK(first->pixels.data() == third->pixels.data());
    CHECK(arena.committed_bytes() == committed);
    frame_arena_t tiny({.maximum_bytes = 1});
    CHECK(raster.render(draw, images, tiny).error().code == failure_code_t::capacity_exceeded);
}

TEST_CASE("raster missing images match preview omission and malformed quads are harmless", "[raster]") {
    auto raster = make_raster({.width = 1, .height = 1});
    frame_arena_t arena;
    const std::array images{storyboard_raster_image_t{.path = "white", .image = solid_image({255, 255, 255, 255})}};
    std::array draw{storyboard_draw_instance_t{.path = "missing", .transform = {1, 0, 0, 1, 0, 0}}};
    (void)render_frame(raster, draw, images, arena);
    arena.reset();
    draw[0].path = "white";
    draw[0].transform.values[0] = std::numeric_limits<double>::infinity();
    const auto frame = render_frame(raster, draw, images, arena);

    check_pixel(*frame, 0, 0, {0, 0, 0, 255});
    CHECK_FALSE(storyboard_raster_t::from({.width = 3841}));
    CHECK_FALSE(storyboard_raster_t::from({.height = 2161}));
    CHECK_FALSE(storyboard_raster_t::from({.threads = 0}));
    CHECK_FALSE(storyboard_raster_t::from({.frame_slots = 4}));
}

// Opt-in benchmark; kept out of ordinary GATE, because measuring speed is not a
// correctness assertion. Same deterministic example and exact 60fps clock.
TEST_CASE("raster example throughput", "[.][raster][benchmark]") {
    const auto scene = example_scene();
    const auto index = osbx_evaluation_index_t::from(scene);
    REQUIRE(index.value);
    const auto images = example_images();
    frame_arena_t arena;
    std::vector<storyboard_draw_instance_t> draw;
    for (const auto scale : {1U, 2U}) {
        auto raster = make_raster({.width = 1920 * scale, .height = 1080 * scale, .threads = 4});
        evaluate_example(scene, *index.value, {}, arena, draw);
        (void)render_frame(raster, draw, images, arena);
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; i < 8; ++i) {
            evaluate_example(scene, *index.value, *storyboard_raster_clock_t{}.time_at(i), arena, draw);
            (void)render_frame(raster, draw, images, arena);
        }
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("VID-1 %ux%u, 4 workers, 8 frames: %.3f frames/s\n", 1920 * scale, 1080 * scale, 8 / seconds);
    }
}

namespace {
void check_decoded_texture(const attempt_t<storyboard_raster_images_t>& result) {
    REQUIRE(result.value);
    REQUIRE(result.value->bindings[0].image);
    CHECK(result.value->bindings[0].image->width == 2);
    CHECK(result.value->bindings[0].image->pixels == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255});
}
void check_missing_texture(const attempt_t<storyboard_raster_images_t>& result) {
    REQUIRE(result.value);
    CHECK_FALSE(result.value->bindings[0].image);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].code == diagnostic_code_t::dangling_asset);
}
void check_scenario_count(const storyboard_frame_effect_t& evaluated, storyboard_conditional_layer_t choice) {
    REQUIRE(evaluated.is_success());
    REQUIRE(evaluated.instances.size() == (choice == storyboard_conditional_layer_t::neither ? 1 : 2));
}
} // namespace

TEST_CASE("raster image loading shares revision cache identity and retains decode diagnostics", "[raster]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "texture.ppm";
    { std::ofstream output(path, std::ios::binary); output << "P6\n2 1\n255\n"; output.write("\xff\0\0\0\xff\0", 6); }
    visual::image_cache_t cache;
    std::array sources{storyboard_raster_image_source_t{.path = "texture.ppm",
        .source = {.resource = resource_id_t{1}, .asset = asset_id_t{2}, .kind = asset_kind_t::image, .path = path}}};
    const auto first = storyboard_raster_images_t::from(song_id_t{1}, 1, sources, cache);
    check_decoded_texture(first);
    { std::ofstream output(path); output << "broken"; }
    const auto cached = storyboard_raster_images_t::from(song_id_t{1}, 1, sources, cache);
    REQUIRE(cached.value);
    CHECK(cached.value->bindings[0].image == first.value->bindings[0].image);
    const auto changed = storyboard_raster_images_t::from(song_id_t{1}, 2, sources, cache);
    check_missing_texture(changed);
    const auto limited = storyboard_raster_images_t::from(song_id_t{1}, 1, sources, cache,
        {.maximum_result_rgba_bytes = 7});
    CHECK_FALSE(limited.value);
    REQUIRE(limited.diagnostics.size() == 1);
    CHECK(limited.diagnostics[0].code == diagnostic_code_t::invalid_binding);
}

TEST_CASE("OSB and OSBX offline frames agree for every shared conditional choice", "[raster]") {
    const auto osb = parse_osb("[Events]\nSprite,Background,TopLeft,texture.png,0,0\n M,0,-10,100,10,20,120,240\n"
        "Sprite,Pass,TopLeft,texture.png,0,0\n M,0,-10,100,10,20,120,240\n"
        "Sprite,Fail,TopLeft,texture.png,0,0\n M,0,-10,100,10,20,120,240\n");
    REQUIRE(osb.value);
    const auto osb_index = storyboard_preview_index_t::from(*osb.value);
    REQUIRE(osb_index.value);
    osbx_document_t osbx{.images = {{.id = "texture", .file = "texture.png"}}};
    for (const auto layer : {osbx_layer_t::background, osbx_layer_t::pass, osbx_layer_t::fail}) {
        const auto id = std::to_string(osbx.nodes.size());
        osbx.nodes.push_back({.id = id, .kind = osbx_node_kind_t::sprite, .layer = layer,
            .image = "texture", .origin = "TopLeft", .ops = {{.id = id + "move", .track = {
                .default_value = {0, 0}, .keys = {{.id = id + "from", .time_ms = -10, .value = {10, 20}},
                    {.id = id + "to", .time_ms = 100, .value = {120, 240}}}}}}});
    }
    const auto osbx_index = osbx_evaluation_index_t::from(osbx);
    REQUIRE(osbx_index.value);
    const auto images = example_images();
    auto raster = make_raster({.width = 640, .height = 480});
    frame_arena_t arena;
    std::vector<storyboard_draw_instance_t> draw;
    const storyboard_time_t time{.milliseconds = -1, .remainder = 1, .denominator = 2};
    for (const auto choice : {storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail,
                              storyboard_conditional_layer_t::neither}) {
        const auto evaluated = evaluate_storyboard_frame(*osb.value, *osb_index.value, time, arena,
            {.conditional_layer = choice});
        check_scenario_count(evaluated, choice);
        const auto expected = render_frame(raster, evaluated.instances, images, arena, choice);

        evaluate_example(osbx, *osbx_index.value, time, arena, draw, choice);
        const auto actual = render_frame(raster, draw, images, arena, choice);

        CHECK(std::ranges::equal(expected->pixels, actual->pixels));
    }
}

namespace {
auto preview_comparison_scene() -> osbx_document_t {
    auto scene = example_scene();
    scene.nodes.resize(3);
    for (auto& node : scene.nodes) {
        node.layer = osbx_layer_t::foreground;
        node.order = static_cast<int>(&node - scene.nodes.data());
        for (auto& op : node.ops) {
            if (op.track.keys.empty()) continue;
            op.track.default_value = op.track.keys.front().value;
            op.track.keys.clear();
        }
    }
    scene.nodes[0].tracks.color.default_value = {1, 1, 1, 1};
    scene.images[0].file = "texture.ppm";
    return scene;
}

void write_ppm(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> rgba) {
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        output.write(reinterpret_cast<const char*>(rgba.data() + i), 3);
    REQUIRE(output);
}

void write_preview_project(const char* project, const char* directory, const osbx_document_t& scene) {
    std::ifstream input(project, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>{input}, {}};
    auto loaded = parse_native_project(source);
    REQUIRE(loaded.value);
    loaded.value->storyboards = {{.id = storyboard_id_t{1000}, .name = "VID-1 comparison", .osbx = scene}};
    const auto saved = write_native_project(*loaded.value);
    REQUIRE(saved.value);
    std::ofstream output(std::filesystem::path{directory} / "preview.wormedit");
    output << *saved.value;
    REQUIRE(output);
}

void compare_preview_canvas(storyboard_raster_frame_t frame, const visual::rgba_image_t& reference) {
    const auto scale = std::min(frame.width / 640.0, frame.height / 480.0);
    const auto left = (frame.width - 640 * scale) * 0.5;
    const auto top = (frame.height - 480 * scale) * 0.5;
    std::size_t samples{}, outliers{};
    double total_error{};
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            // The interactive player overlays lanes in its letterboxes. Compare
            // only the storyboard canvas, excluding its two-pixel outer edge.
            if (x < left + 2 || x >= frame.width - left - 2 ||
                y < top + 2 || y >= frame.height - top - 2) continue;
            const auto offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto error = std::abs(int(frame.pixels[offset + channel]) - int(reference.pixels[offset + channel]));
                total_error += error;
                outliers += error > 5;
                ++samples;
            }
        }
    }
    REQUIRE(samples > 0);
    std::printf("VID-1 Xvfb comparison: mean RGB error %.4f/255; channels >5: %.4f%%\n",
        total_error / static_cast<double>(samples), 100.0 * static_cast<double>(outliers) / samples);
    CHECK(total_error / static_cast<double>(samples) <= 1.5);
    CHECK(static_cast<double>(outliers) / samples <= 0.005);
}
} // namespace

// Manual integration fixture: generate project + expected frame, capture the
// actual application under isolated Xvfb, then rerun with the cropped viewport
// in WORMEDIT_RASTER_PREVIEW_REFERENCE. See the VID-1 card for exact commands.
TEST_CASE("raster interactive preview comparison", "[.][raster][preview]") {
    const auto* directory = std::getenv("WORMEDIT_RASTER_PREVIEW_DIR");
    const auto* project = std::getenv("WORMEDIT_RASTER_PREVIEW_PROJECT");
    REQUIRE(directory);
    REQUIRE(project);
    std::filesystem::create_directories(directory);
    const auto scene = preview_comparison_scene();
    write_preview_project(project, directory, scene);
    auto original = example_images();
    auto opaque = std::make_shared<visual::rgba_image_t>(*original[0].image);
    for (std::size_t i = 3; i < opaque->pixels.size(); i += 4) opaque->pixels[i] = 255;
    const std::array images{storyboard_raster_image_t{.path = "texture.ppm", .image = opaque}};
    write_ppm(std::filesystem::path{directory} / "texture.ppm", opaque->width, opaque->height, opaque->pixels);
    const auto index = osbx_evaluation_index_t::from(scene);
    REQUIRE(index.value);
    std::optional<visual::rgba_image_t> reference;
    if (const auto* path = std::getenv("WORMEDIT_RASTER_PREVIEW_REFERENCE")) {
        auto decoded = visual::open_image_rgba({.kind = asset_kind_t::image, .path = path});
        REQUIRE(decoded);
        reference = std::move(*decoded);
    }
    auto raster = make_raster({.width = reference ? reference->width : 337,
        .height = reference ? reference->height : 488});
    frame_arena_t arena;
    std::vector<storyboard_draw_instance_t> draw;
    evaluate_example(scene, *index.value, {}, arena, draw);
    const auto frame = render_frame(raster, draw, images, arena);

    write_ppm(std::filesystem::path{directory} / "offline.ppm", frame->width, frame->height, frame->pixels);
    if (reference) compare_preview_canvas(*frame, *reference);
}

namespace {
void require_animation_raster(bool value) { REQUIRE(value); }
void check_animation_raster(bool value) { CHECK(value); }
auto render_animation_hash(storyboard_raster_t& raster, const storyboard_document_model_t& model,
    const storyboard_preview_index_t& index, storyboard_time_t time,
    std::span<const storyboard_raster_image_t> images) -> std::uint64_t {
    frame_arena_t draw_arena, raster_arena;
    const auto evaluated = evaluate_storyboard_frame(model, index, time, draw_arena);
    REQUIRE(evaluated.is_success());
    const auto frame = render_frame(raster, evaluated.instances, images, raster_arena);
    return frame_hash(frame->pixels);
}
}

TEST_CASE("Animation offline frames repeat identically after backwards fractional seeks", "[storyboard][raster][animation]") {
    const auto parsed = parse_osb("Animation,Foreground,TopLeft,frame.png,0,0,2,2.5,LoopForever\n F,0,0,10,1,1\n V,0,0,10,640,480,640,480\n");
    require_animation_raster(parsed.value.has_value());
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_animation_raster(index.value.has_value());
    auto raster = make_raster({.width = 4, .height = 3});
    const std::array images{storyboard_raster_image_t{.path = "frame0.png", .image = solid_image({255, 0, 0, 255})},
        storyboard_raster_image_t{.path = "frame1.png", .image = solid_image({0, 255, 0, 255})}};
    const auto first = render_animation_hash(raster, *parsed.value, *index.value, {}, images);
    const auto second = render_animation_hash(raster, *parsed.value, *index.value,
        {.milliseconds = 2, .remainder = 1, .denominator = 2}, images);
    check_animation_raster(first != second);
    check_animation_raster(render_animation_hash(raster, *parsed.value, *index.value, {}, images) == first);
    check_animation_raster(render_animation_hash(raster, *parsed.value, *index.value, {.milliseconds = 5}, images) == first);
}

namespace {
auto osbx_animation_raster_hash(const osbx_document_t& scene, const osbx_evaluation_index_t& index,
    storyboard_time_t time, std::span<const storyboard_raster_image_t> images) -> std::uint64_t {
    frame_arena_t arena, raster_arena;
    const auto frame = evaluate_osbx_frame(index, time, arena);
    require_animation_raster(frame.is_success() && frame.instances.size() == 1);
    const auto source = storyboard_draw_instance_t::from(scene, scene.nodes.front(), scene.images.front());
    const auto draw = source.with_evaluation(frame.instances.front(), arena);
    require_animation_raster(draw.has_value());
    auto raster = make_raster({.width = 4, .height = 3});
    const auto output = render_frame(raster, std::span{&*draw, std::size_t{1}}, images, raster_arena);
    return frame_hash(output->pixels);
}
}
TEST_CASE("OSBX Animation raster frames use the shared fractional query and selected paths", "[storyboard][raster][osbx][animation]") {
    osbx_node_t node{.id = "hero", .kind = osbx_node_kind_t::animation, .image = "frames", .origin = "TopLeft",
        .ops = {{.id = "size", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {640,480}}}},
        .lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 10}};
    node.animation = storyboard_animation_t{.frame_count = 2, .frame_delay_ms = 2.5};
    const osbx_document_t scene{.images = {{.id = "frames", .file = "frame.png"}}, .nodes = {node}};
    const auto index = osbx_evaluation_index_t::from(scene);
    require_animation_raster(index.value.has_value());
    const std::array images{storyboard_raster_image_t{.path = "frame0.png", .image = solid_image({255,0,0,255})},
        storyboard_raster_image_t{.path = "frame1.png", .image = solid_image({0,255,0,255})}};
    const auto first = osbx_animation_raster_hash(scene, *index.value, {}, images);
    const auto second = osbx_animation_raster_hash(scene, *index.value, {.milliseconds = 2, .remainder = 1, .denominator = 2}, images);
    check_animation_raster(first != second);
    check_animation_raster(osbx_animation_raster_hash(scene, *index.value, {}, images) == first);
}
