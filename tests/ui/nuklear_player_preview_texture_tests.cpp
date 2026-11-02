#define NK_IMPLEMENTATION

#include "ui/nuklear/audio/player_preview.h"
#include "ui/frontend/workspace.h"
#include "ui/nuklear/audio/player_preview_texture.h"
#include "ui/nuklear/render/image_quad.h"
#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_VIDEO_DECODER)
#include "sys/audio/player_preview_presenter.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/command.h>
#include <wormedit/visual/image_job.h>
#include <wormedit/visual/video_job.h>
#include <thread>
#include <chrono>
#endif

#if defined(WORMEDIT_PREVIEW_LUA_ACCEPTANCE)
#include "scripting/lua_project.h"
#include <wormedit/core/generator.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#endif

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <wormedit/core/storyboard_preview.h>
#include <memory>
#include <span>

namespace {
thread_local bool count_preview_allocations{};
thread_local std::size_t preview_allocation_count{}, preview_allocation_bytes{};
}
void* operator new(std::size_t size) {
    if (count_preview_allocations) {
        ++preview_allocation_count;
        preview_allocation_bytes += size;
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace {

template <typename value_t, void (*destroy)(value_t*)> struct sdl_deleter_t {
    void operator()(value_t* value) const noexcept {
        if (value) {
            destroy(value);
        }
    }
};

using window_ptr_t = std::unique_ptr<SDL_Window, sdl_deleter_t<SDL_Window, SDL_DestroyWindow>>;
using renderer_ptr_t =
    std::unique_ptr<SDL_Renderer, sdl_deleter_t<SDL_Renderer, SDL_DestroyRenderer>>;
using surface_ptr_t = std::unique_ptr<SDL_Surface, sdl_deleter_t<SDL_Surface, SDL_DestroySurface>>;

struct nuklear_buffer_t {
    nk_buffer value{};
    nuklear_buffer_t() {
        nk_buffer_init_default(&value);
    }
    ~nuklear_buffer_t() {
        nk_buffer_free(&value);
    }
};

struct texture_fixture_t {
    window_ptr_t window;
    surface_ptr_t backing_surface;
    renderer_ptr_t renderer;

    texture_fixture_t(int width = 16, int height = 16) {
        if (std::getenv("DISPLAY") != nullptr && SDL_Init(SDL_INIT_VIDEO)) {
            window.reset(SDL_CreateWindow("wormedit texture evidence", width, height, SDL_WINDOW_HIDDEN));
            if (window) {
                renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
            }
        }
        if (!renderer) {
            backing_surface.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
            if (backing_surface) {
                renderer.reset(SDL_CreateSoftwareRenderer(backing_surface.get()));
            }
        }
    }

    ~texture_fixture_t() {
        worm_ui_nuklear_shutdown_player_preview_textures();
        renderer.reset();
        window.reset();
        backing_surface.reset();
        SDL_Quit();
    }
};

auto uploaded_texture(SDL_Renderer* renderer, wormedit::ui::player_preview_view_t& view,
                      wormedit::ui::player_preview_visual_layer_view_t& layer) -> SDL_Texture* {
    nuklear_buffer_t storage;
    nk_command_buffer canvas{};
    nk_command_buffer_init(&canvas, &storage.value, NK_CLIPPING_OFF);
    view.visual_layers = {layer};
    worm_ui_nuklear_begin_player_preview_texture_frame();
    const auto result = worm_ui_nuklear_draw_player_preview_visual_layers(
        &canvas, nk_rect(0.0F, 0.0F, 16.0F, 16.0F), renderer, &view);
    REQUIRE(result.drew_image);
    REQUIRE(result.failure_count == 0);
    const auto* memory = static_cast<const nk_byte*>(nk_buffer_memory_const(&storage.value));
    REQUIRE(memory);
    const auto* command = reinterpret_cast<const nk_command*>(memory + canvas.last);
    REQUIRE(command->type == NK_COMMAND_IMAGE);
    const auto* image = reinterpret_cast<const nk_command_image*>(command);
    REQUIRE(image->img.handle.ptr);
    return static_cast<SDL_Texture*>(image->img.handle.ptr);
}

auto render_center_pixel(SDL_Renderer* renderer, SDL_Texture* texture)
    -> std::array<std::uint8_t, 4> {
    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer));
    REQUIRE(SDL_RenderTexture(renderer, texture, nullptr, nullptr));
    surface_ptr_t capture{SDL_RenderReadPixels(renderer, nullptr)};
    REQUIRE(capture);
    std::array<std::uint8_t, 4> pixel{};
    REQUIRE(SDL_ReadSurfacePixel(capture.get(), 8, 8, &pixel[0], &pixel[1], &pixel[2], &pixel[3]));
    return pixel;
}

auto solid_bga_layer(wormedit::visual_event_kind_t kind, std::uint64_t resource,
                     const std::array<std::uint8_t, 4>& pixels) -> wormedit::ui::player_preview_visual_layer_view_t {
    return {.object_id = wormedit::object_id_t{resource}, .resource_id = wormedit::resource_id_t{resource}, .kind = kind,
        .draw_order = kind == wormedit::visual_event_kind_t::video ? 99U : 0U,
        .static_image = true, .image_ready = true, .image_content_id = 1,
        .image_width = 1, .image_height = 1, .rgba_pixels = pixels};
}

void require_bga_texture(bool value) { REQUIRE(value); }
void check_bga_texture(bool value) { CHECK(value); }

auto render_bga_composite(SDL_Renderer* renderer,
                          std::span<const wormedit::ui::player_preview_visual_layer_view_t> layers)
    -> std::array<std::uint8_t, 4> {
    nuklear_buffer_t storage;
    nk_command_buffer canvas{};
    nk_command_buffer_init(&canvas, &storage.value, NK_CLIPPING_OFF);
    wormedit::ui::player_preview_view_t view{.visual_fit = wormedit::visual_fit_policy_t::stretch,
        .project_id = wormedit::song_id_t{1}, .document_revision = 2,
        .visual_layers = {layers.begin(), layers.end()}};
    worm_ui_nuklear_begin_player_preview_texture_frame();
    const auto result = worm_ui_nuklear_draw_player_preview_visual_layers(
        &canvas, nk_rect(0, 0, 16, 16), renderer, &view);
    require_bga_texture(result.failure_count == 0);
    require_bga_texture(result.drew_image);
    require_bga_texture(SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255));
    require_bga_texture(SDL_RenderClear(renderer));
    const auto* memory = static_cast<const nk_byte*>(nk_buffer_memory_const(&storage.value));
    for (auto offset = canvas.begin;;) {
        const auto* command = reinterpret_cast<const nk_command*>(memory + offset);
        if (command->type == NK_COMMAND_IMAGE) {
            const auto* image = reinterpret_cast<const nk_command_image*>(command);
            require_bga_texture(SDL_RenderTexture(renderer, static_cast<SDL_Texture*>(image->img.handle.ptr), nullptr, nullptr));
        }
        if (offset == canvas.last) break;
        offset = command->next;
    }
    surface_ptr_t capture{SDL_RenderReadPixels(renderer, nullptr)};
    require_bga_texture(static_cast<bool>(capture));
    std::array<std::uint8_t, 4> pixel{};
    require_bga_texture(SDL_ReadSurfacePixel(capture.get(), 8, 8, &pixel[0], &pixel[1], &pixel[2], &pixel[3]));
    return pixel;
}

} // namespace

// Tests inspect frame-owned quad payloads before deferred conversion. Ordinary image
// commands use this executable's independent NK_IMPLEMENTATION.
void worm_ui_nuklear_append_image_quad(void*, short, short, unsigned short, unsigned short,
                                       nk_handle) {}

TEST_CASE("BGA composites video base then Layer then Layer 2 regardless of draw order", "[bga][texture]") {
    texture_fixture_t fixture;
    REQUIRE(fixture.renderer);
    const std::array<std::uint8_t, 4> red{255, 0, 0, 255}, green{0, 255, 0, 255}, blue{0, 0, 255, 255};
    const std::array layers{solid_bga_layer(wormedit::visual_event_kind_t::layer2, 3, blue),
        solid_bga_layer(wormedit::visual_event_kind_t::layer, 2, green),
        solid_bga_layer(wormedit::visual_event_kind_t::video, 1, red)};
    CHECK(render_bga_composite(fixture.renderer.get(), layers) == blue);
    CHECK(render_bga_composite(fixture.renderer.get(), std::span(layers).subspan(1)) == green);
}

TEST_CASE("BGA black key keeps base black opaque and separates same-resource cache roles", "[bga][texture]") {
    texture_fixture_t fixture;
    REQUIRE(fixture.renderer);
    const std::array<std::uint8_t, 4> black{0, 0, 0, 255};
    const std::array base{solid_bga_layer(wormedit::visual_event_kind_t::background, 4, black)};
    const std::array overlay{solid_bga_layer(wormedit::visual_event_kind_t::layer, 4, black)};
    CHECK(render_bga_composite(fixture.renderer.get(), base) == black);
    CHECK(render_bga_composite(fixture.renderer.get(), overlay) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
    CHECK(render_bga_composite(fixture.renderer.get(), base) == black);
    CHECK(black[3] == 255);
}

TEST_CASE("BGA base preserves storyboard draw-order placement", "[bga][texture]") {
    texture_fixture_t fixture;
    REQUIRE(fixture.renderer);
    const std::array<std::uint8_t, 4> red{255, 0, 0, 255}, white{255, 255, 255, 255};
    std::array layers{solid_bga_layer(wormedit::visual_event_kind_t::background, 7, red),
        solid_bga_layer(wormedit::visual_event_kind_t::storyboard, 8, white)};
    layers[0].draw_order = 2;
    layers[1].draw_order = 0;
    CHECK(render_bga_composite(fixture.renderer.get(), layers) == red);
    layers[1].draw_order = 3;
    CHECK(render_bga_composite(fixture.renderer.get(), layers) == white);
}

TEST_CASE("BGA layer preserves nonblack alpha and keys only exact RGB black", "[bga][texture]") {
    texture_fixture_t fixture;
    REQUIRE(fixture.renderer);
    const std::array<std::uint8_t, 4> red_alpha{255, 0, 0, 128}, near_black{0, 0, 1, 255};
    const std::array alpha_layer{solid_bga_layer(wormedit::visual_event_kind_t::layer2, 5, red_alpha)};
    const auto blended = render_bga_composite(fixture.renderer.get(), alpha_layer);
    check_bga_texture(blended[0] >= 127);
    check_bga_texture(blended[0] <= 128);
    check_bga_texture(blended[2] >= 126);
    check_bga_texture(blended[2] <= 127);
    const std::array near_layer{solid_bga_layer(wormedit::visual_event_kind_t::layer, 6, near_black)};
    check_bga_texture(render_bga_composite(fixture.renderer.get(), near_layer) == near_black);
    check_bga_texture(red_alpha[3] == 128);
}

TEST_CASE("player preview uploads and replaces renderer-backed video textures",
          "[integration][frontend][visual][player-preview][texture]") {
    texture_fixture_t fixture;
    REQUIRE(fixture.renderer);
    INFO("SDL renderer: " << SDL_GetRendererName(fixture.renderer.get()));

    wormedit::ui::player_preview_view_t view{
        .visual_fit = wormedit::visual_fit_policy_t::stretch,
        .project_id = wormedit::song_id_t{1},
        .document_revision = 2,
    };
    std::array<std::uint8_t, 2U * 2U * 4U> pixels{};
    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        pixels[index] = 255;
        pixels[index + 3] = 255;
    }
    wormedit::ui::player_preview_visual_layer_view_t layer{
        .object_id = wormedit::object_id_t{3},
        .resource_id = wormedit::resource_id_t{4},
        .kind = wormedit::visual_event_kind_t::video,
        .video_frame = true,
        .image_ready = true,
        .image_content_id = 1,
        .image_width = 2,
        .image_height = 2,
        .rgba_pixels = pixels,
    };

    auto* red_texture = uploaded_texture(fixture.renderer.get(), view, layer);
    const auto red = render_center_pixel(fixture.renderer.get(), red_texture);
    CHECK(red[0] > 240);
    CHECK(red[1] < 16);
    CHECK(red[2] < 16);

    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        pixels[index] = 0;
        pixels[index + 1] = 255;
    }
    layer.image_content_id = 2;
    auto* green_texture = uploaded_texture(fixture.renderer.get(), view, layer);
    CHECK(green_texture != red_texture);
    const auto green = render_center_pixel(fixture.renderer.get(), green_texture);
    CHECK(green[0] < 16);
    CHECK(green[1] > 240);
    CHECK(green[2] < 16);
}

TEST_CASE("player preview validates event-linked video diagnostic invariants",
          "[unit][frontend][visual][player-preview][diagnostic]") {
    wormedit::ui::player_preview_visual_layer_view_t layer{
        .object_id = wormedit::object_id_t{3},
        .resource_id = wormedit::resource_id_t{4},
        .kind = wormedit::visual_event_kind_t::video,
        .video_clock_ready = true,
        .video_elapsed_sample_frames = 5,
        .video_time_numerator = 1,
        .video_time_denominator = 2,
        .video_frame = true,
        .image_pending = true,
        .diagnostic = wormedit::ui::player_preview_visual_diagnostic_t::video_frame_late,
        .diagnostic_text = "late",
    };
    wormedit::ui::player_preview_view_t view{
        .visible = true,
        .scene_available = true,
        .skin = wormedit::player_preview_skin_t::mania,
        .visual_fit = wormedit::visual_fit_policy_t::contain,
        .project_id = wormedit::song_id_t{1},
        .chart_id = wormedit::chart_id_t{2},
        .document_revision = 3,
        .sample_frame = 5,
        .viewport_width_pixels = 300.0,
        .viewport_height_pixels = 420.0,
        .visual_layers = {layer},
    };

    CHECK(worm_ui_nuklear_player_preview_view_valid(&view));
    auto& projected = view.visual_layers.front();
    projected.diagnostic_text = {};
    CHECK_FALSE(worm_ui_nuklear_player_preview_view_valid(&view));
    projected.diagnostic_text = "late";
    projected.image_pending = false;
    CHECK_FALSE(worm_ui_nuklear_player_preview_view_valid(&view));
    projected.image_pending = true;
    projected.video_clock_ready = false;
    projected.video_elapsed_sample_frames = 0;
    projected.video_time_numerator = 0;
    projected.video_time_denominator = 0;
    CHECK_FALSE(worm_ui_nuklear_player_preview_view_valid(&view));
}

namespace {
auto validate_draw_indices(const nk_draw_index* values, std::size_t count,
                            std::size_t vertex_count) -> nk_draw_index {
    REQUIRE(count > 0);
    REQUIRE(std::all_of(values, values + count, [vertex_count](nk_draw_index value) {
        return value < vertex_count;
    }));
    return *std::max_element(values, values + count);
}
} // namespace

TEST_CASE("Nuklear converts dense geometry with indices above 65535", "[unit][frontend][draw-index]") {
    static_assert(sizeof(nk_draw_index) == 4);
    struct vertex_t { float position[2]; float uv[2]; nk_byte color[4]; };
    static const nk_draw_vertex_layout_element layout[]{
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(vertex_t, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(vertex_t, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(vertex_t, color)},
        {NK_VERTEX_LAYOUT_END}};
    nk_user_font font{};
    font.height = 13;
    font.width = [](nk_handle, float, const char*, int length) { return length * 8.0f; };
    nk_context context{};
    REQUIRE(nk_init_default(&context, &font));
    REQUIRE(nk_begin(&context, "dense", nk_rect(0, 0, 1000, 1000), NK_WINDOW_NO_SCROLLBAR));
    auto* canvas = nk_window_get_canvas(&context);
    for (int index = 0; index < 18000; ++index)
        nk_fill_rect(canvas, nk_rect(10 + index % 250 * 3, 10 + index / 250 * 3, 2, 2),
                     0, nk_rgb(255, 255, 255));
    nk_end(&context);
    nuklear_buffer_t commands, vertices, indices;
    nk_convert_config config{};
    config.vertex_layout = layout;
    config.vertex_size = sizeof(vertex_t);
    config.vertex_alignment = alignof(vertex_t);
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_OFF;
    config.line_AA = NK_ANTI_ALIASING_OFF;
    REQUIRE(nk_convert(&context, &commands.value, &vertices.value, &indices.value, &config) == NK_CONVERT_SUCCESS);
    const auto vertex_count = vertices.value.allocated / sizeof(vertex_t);
    REQUIRE(vertex_count > 70000);
    const auto index_count = indices.value.allocated / sizeof(nk_draw_index);
    const auto* values = static_cast<const nk_draw_index*>(nk_buffer_memory_const(&indices.value));
    CHECK(validate_draw_indices(values, index_count, vertex_count) > 70000);
    nk_free(&context);
}

TEST_CASE("player preview rejects an exceeded visual work budget and accepts an empty list",
          "[unit][frontend][visual][player-preview][texture]") {
    nk_command_buffer canvas{};
    wormedit::ui::player_preview_view_t view;
    view.visual_layers.resize(wormedit::ui::player_preview_visual_layer_limit + 1);
    const auto oversized = worm_ui_nuklear_draw_player_preview_visual_layers(
        &canvas, nk_rect(0, 0, 16, 16), nullptr, &view);
    CHECK_FALSE(oversized.drew_image);
    CHECK(oversized.failure_count == 1);
    CHECK(oversized.first_failure == worm_ui_nuklear_preview_visual_failure_t::invalid_image);

    view.visual_layers.clear();
    const auto empty = worm_ui_nuklear_draw_player_preview_visual_layers(
        &canvas, nk_rect(0, 0, 16, 16), nullptr, &view);
    CHECK_FALSE(empty.drew_image);
    CHECK(empty.failure_count == 0);
}

#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_VIDEO_DECODER)
namespace {
auto osbx_preview_document() -> wormedit::song_document_t {
    using namespace wormedit;
    chart_document_t chart{
        .id = chart_id_t{7}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .lane_layouts = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
            .layout = {.profile_id = "test-single", .display_name = "Test Single",
                .lanes = {{.id = lane_id_t{1}, .name = "Lane", .role = lane_role_t::playable}}}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}}};
    osbx_node_t group{.id = "group", .layer = osbx_layer_t::foreground,
        .ops = {{.id = "rotate", .kind = osbx_transform_kind_t::rotate,
                  .track = {.default_value = {90}}},
                {.id = "move", .kind = osbx_transform_kind_t::translate,
                  .track = {.default_value = {100, 50}}}}};
    group.tracks.opacity.default_value = {0.5};
    osbx_node_t sprite{.id = "shear", .kind = osbx_node_kind_t::sprite, .parent = "group",
        .layer = osbx_layer_t::foreground, .image = "image", .origin = "TopLeft",
        .ops = {{.id = "matrix", .kind = osbx_transform_kind_t::matrix,
                  .track = {.default_value = {1, 0, 0.5, 1, 0, 0}}}}};
    sprite.tracks.opacity.default_value = {0.8};
    sprite.tracks.color.default_value = {0.5, 1, 1, 0.5};
    sprite.tracks.blend.default_value = osbx_blend_t::additive;
    return {.id = song_id_t{1}, .revision = 9,
        .visual_assets = {{.id = asset_id_t{20}, .kind = asset_kind_t::image,
                          .source_path = "sprite.bmp", .width = 20, .height = 10}},
        .resources = {{.id = resource_id_t{10}, .name = "sprite", .asset = asset_id_t{20},
                       .roles = {resource_role_t::bga}}},
        .storyboards = {{.id = storyboard_id_t{30}, .name = "Affine",
            .osbx = osbx_document_t{.images = {{.id = "image", .file = "sprite.bmp"}},
                                    .nodes = {group, sprite}}}},
        .charts = {std::move(chart)}};
}

struct osbx_presenter_fixture_t {
    wormedit::visual::image_cache_t image_cache;
    wormedit::task_scheduler_t image_scheduler{1, 2};
    wormedit::visual::image_job_t image_job{image_cache, image_scheduler};
    wormedit::visual::video_frame_cache_t video_cache;
    wormedit::task_scheduler_t video_scheduler{1, 2};
    wormedit::visual::video_frame_job_t video_job{video_cache, video_scheduler};
    wormedit::sys::player_preview_presenter_t presenter;
    wormedit::ui::player_preview_view_t view;

    void populate(const wormedit::document_history_t& history, const std::filesystem::path& directory) {
        presenter.populate(view, history, wormedit::chart_id_t{7}, true,
            wormedit::player_preview_skin_t::mania, 24'000, 48'000, directory, image_job, video_job);
    }
};

void check_refused_storyboard_budget(const wormedit::ui::player_preview_view_t& view) {
    CHECK(view.scene_available);
    CHECK(!view.lanes.empty());
    CHECK(view.storyboard_sprites.empty());
    CHECK(view.status_text == "Shared storyboard sprite budget exceeded.");
}

auto draw_osbx_preview(const wormedit::ui::player_preview_view_t& view, SDL_Renderer* renderer,
                       nk_command_buffer& canvas) -> worm_ui_nuklear_preview_visual_result_t {
    worm_ui_nuklear_begin_image_quad_frame();
    worm_ui_nuklear_begin_player_preview_texture_frame();
    return worm_ui_nuklear_draw_player_preview_storyboard_sprites(
        &canvas, nk_rect(0, 0, 640, 480), renderer, &view, true);
}
} // namespace

namespace {
template <class Value> void require_osbx_preview(const Value& value) { REQUIRE(static_cast<bool>(value)); }
void check_osbx_preview(bool value) { CHECK(value); }
}

TEST_CASE("OSBX document draws inherited sheared additive sprite through the real preview cache",
          "[osbx][player-preview][texture][integration]") {
    using namespace wormedit;
    texture_fixture_t texture;
    require_osbx_preview(texture.renderer);
    auto directory = test::temporary_directory_t::from();
    require_osbx_preview(directory);
    surface_ptr_t image{SDL_CreateSurface(20, 10, SDL_PIXELFORMAT_RGBA32)};
    require_osbx_preview(image);
    require_osbx_preview(SDL_FillSurfaceRect(image.get(), nullptr, SDL_MapSurfaceRGBA(image.get(), 255, 255, 255, 255)));
    require_osbx_preview(SDL_SaveBMP(image.get(), (directory->path() / "sprite.bmp").c_str()));
    const auto history = document_history_t::from(osbx_preview_document());
    osbx_presenter_fixture_t fixture;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    do {
        fixture.populate(history, directory->path());
        if (fixture.view.storyboard_sprites.size() == 1 && fixture.view.storyboard_sprites[0].image_ready)
            break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    require_osbx_preview(fixture.view.scene_available);
    require_osbx_preview(fixture.view.storyboard_sprites.size() == 1);
    require_osbx_preview(fixture.view.storyboard_sprites[0].image_ready);
    check_osbx_preview(worm_ui_nuklear_player_preview_view_valid(&fixture.view));
    nuklear_buffer_t storage;
    nk_command_buffer canvas{};
    nk_command_buffer_init(&canvas, &storage.value, NK_CLIPPING_OFF);
    const auto result = draw_osbx_preview(fixture.view, texture.renderer.get(), canvas);
    require_osbx_preview(result.drew_image);
    require_osbx_preview(result.failure_count == 0);
    const auto* memory = static_cast<const nk_byte*>(nk_buffer_memory_const(&storage.value));
    const auto* command = reinterpret_cast<const nk_command_custom*>(memory + canvas.last);
    require_osbx_preview(command->header.type == NK_COMMAND_CUSTOM);
    const auto* quad = static_cast<const WormUiNuklearImageQuad*>(command->callback_data.ptr);
    require_osbx_preview(quad);
    const std::array<visual_point_t, 4> expected{{{100, 50}, {100, 70}, {90, 75}, {90, 55}}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check_osbx_preview(quad->points[i].x == static_cast<float>(expected[i].x));
        check_osbx_preview(quad->points[i].y == static_cast<float>(expected[i].y));
    }
    check_osbx_preview(quad->blend == ui::image_blend_t::additive);
    check_osbx_preview(quad->color.a == 51);
    check_osbx_preview(quad->color.r == 128);
    const auto* cached_pixels = fixture.view.storyboard_sprites[0].rgba_pixels.data();
    // Cached indices must not accumulate duplicates over repeated frames.
    for (int i = 0; i < 3; ++i) fixture.populate(history, directory->path());
    require_osbx_preview(fixture.view.storyboard_sprites.size() == 1);
    check_osbx_preview(fixture.view.storyboard_sprites[0].rgba_pixels.data() == cached_pixels);
}

TEST_CASE("OSBX missing images preserve background and lane fallback without a sprite quad",
          "[osbx][player-preview][texture][missing]") {
    using namespace wormedit;
    auto source = osbx_preview_document();
    source.visual_assets.clear();
    source.resources.clear();
    const auto history = document_history_t::from(std::move(source));
    osbx_presenter_fixture_t fixture;
    fixture.populate(history, {});
    require_osbx_preview(fixture.view.scene_available);
    require_osbx_preview(fixture.view.storyboard_sprites.size() == 1);
    check_osbx_preview(!fixture.view.storyboard_sprites[0].image_ready);
    check_osbx_preview(!fixture.view.lanes.empty());
    check_osbx_preview(worm_ui_nuklear_player_preview_view_valid(&fixture.view));
    nuklear_buffer_t storage;
    nk_command_buffer canvas{};
    nk_command_buffer_init(&canvas, &storage.value, NK_CLIPPING_OFF);
    const auto result = draw_osbx_preview(fixture.view, nullptr, canvas);
    check_osbx_preview(!result.drew_image);
    check_osbx_preview(result.failure_count == 0);
    check_osbx_preview(storage.value.allocated == 0);
}
#endif


namespace {
void require_preview_budget(bool value) { REQUIRE(value); }
void check_preview_budget(bool value) { CHECK(value); }

struct preview_command_storage_t {
    std::size_t allocation_calls{}, allocated_bytes{};
    nk_buffer storage{};
    nk_command_buffer canvas{};
    preview_command_storage_t() {
        nk_allocator allocator{};
        allocator.userdata = nk_handle_ptr(this);
        allocator.alloc = [](nk_handle user, void*, nk_size size) -> void* {
            auto& self = *static_cast<preview_command_storage_t*>(user.ptr);
            ++self.allocation_calls;
            self.allocated_bytes += size;
            return std::malloc(size);
        };
        allocator.free = [](nk_handle, void* memory) { std::free(memory); };
        nk_buffer_init(&storage, &allocator, 1024);
        nk_command_buffer_init(&canvas, &storage, NK_CLIPPING_OFF);
    }
    ~preview_command_storage_t() { nk_buffer_free(&storage); }
    void reset() {
        nk_buffer_clear(&storage);
        nk_command_buffer_reset(&canvas);
        worm_ui_nuklear_begin_image_quad_frame();
        // This helper starts the complete frontend frame, before either scene.
        worm_ui_nuklear_begin_player_preview_texture_frame();
    }
};
} // namespace

TEST_CASE("storyboard quad recording reuses bounded storage and reports overflow",
          "[player-preview][cap][allocation]") {
    preview_command_storage_t commands;
    const auto image = nk_image_id(1);
    const std::array points{nk_vec2(1, 1), nk_vec2(2, 1), nk_vec2(2, 2), nk_vec2(1, 2)};
    for (std::size_t i = 0; i < wormedit::ui::maximum_preview_storyboard_instances; ++i)
        require_preview_budget(worm_ui_nuklear_push_image_quad(&commands.canvas, &image,
            points.data(), nk_rgba(255, 255, 255, 255)));
    check_preview_budget(!worm_ui_nuklear_push_image_quad(&commands.canvas, &image,
        points.data(), nk_rgba(255, 255, 255, 255)));
    const auto allocations = commands.allocation_calls;
    commands.reset();
    for (std::size_t i = 0; i < wormedit::ui::maximum_preview_storyboard_instances; ++i)
        require_preview_budget(worm_ui_nuklear_push_image_quad(&commands.canvas, &image,
            points.data(), nk_rgba(255, 255, 255, 255)));
    check_preview_budget(commands.allocation_calls == allocations);
}

#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_VIDEO_DECODER)
namespace {
auto populated_osbx_document(std::size_t count) -> wormedit::song_document_t {
    auto document = osbx_preview_document();
    auto& nodes = document.storyboards.front().osbx->nodes;
    const auto prototype = nodes.back();
    nodes.clear();
    for (std::size_t i = 0; i < count; ++i) {
        auto sprite = prototype;
        sprite.id = "sprite-" + std::to_string(i);
        sprite.ops.front().id = "matrix-" + std::to_string(i);
        sprite.parent.reset();
        nodes.push_back(std::move(sprite));
    }
    return document;
}

struct preview_measurement_t {
    std::size_t allocations{}, bytes{};
    double microseconds{};
};

auto measure_osbx_recording(osbx_presenter_fixture_t& fixture,
                            const wormedit::document_history_t& history,
                            const std::filesystem::path& directory,
                            SDL_Renderer* renderer, std::size_t count) -> preview_measurement_t {
    preview_command_storage_t commands;
    // Warm index, decoded image and texture caches, persistent frontend lists and NK storage.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        fixture.populate(history, directory);
        if (!fixture.view.storyboard_sprites.empty() && fixture.view.storyboard_sprites[0].image_ready)
            break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    require_preview_budget(fixture.view.storyboard_sprites.size() == count);
    require_preview_budget(fixture.view.storyboard_sprites[0].image_ready);
    for (int frame = 0; frame < 5; ++frame) {
        commands.reset();
        fixture.populate(history, directory);
        require_preview_budget(draw_osbx_preview(fixture.view, renderer, commands.canvas).drew_image);
    }
    const auto nk_allocations = commands.allocation_calls;
    preview_allocation_count = preview_allocation_bytes = 0;
    count_preview_allocations = true;
    bool valid = true;
    const auto begin = std::chrono::steady_clock::now();
    constexpr int frames = 20;
    for (int frame = 0; frame < frames; ++frame) {
        commands.reset();
        fixture.populate(history, directory);
        const auto result = draw_osbx_preview(fixture.view, renderer, commands.canvas);
        valid = valid && fixture.view.scene_available && fixture.view.storyboard_sprites.size() == count &&
                result.drew_image && result.failure_count == 0;
    }
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    count_preview_allocations = false;
    const preview_measurement_t measurement{preview_allocation_count / frames,
        preview_allocation_bytes / frames,
        std::chrono::duration<double, std::micro>(elapsed).count() / frames};
    check_preview_budget(valid);
    check_preview_budget(commands.allocation_calls == nk_allocations);
    std::printf("OSBX actual preview sprites=%zu warmup=5 measured=20 cpp_allocations_per_frame=%zu cpp_bytes_per_frame=%zu nk_warm_allocations=%zu nk_record_bytes=%zu avg_us=%.3f quad_static_bytes=%zu frontend_list_bytes=%zu core_list_bytes=%zu\n",
        count, measurement.allocations, measurement.bytes, commands.allocation_calls - nk_allocations,
        static_cast<std::size_t>(commands.storage.allocated), measurement.microseconds,
        wormedit::ui::maximum_preview_storyboard_instances * sizeof(WormUiNuklearImageQuad),
        count * sizeof(wormedit::ui::player_preview_storyboard_sprite_view_t),
        count * sizeof(wormedit::storyboard_draw_instance_t));
    return measurement;
}
} // namespace

TEST_CASE("OSBX actual warmed presenter image cache and quad recording costs stay bounded",
          "[osbx][player-preview][allocation][cap]") {
    using namespace wormedit;
    texture_fixture_t texture;
    require_osbx_preview(texture.renderer);
    auto directory = test::temporary_directory_t::from();
    require_osbx_preview(directory);
    surface_ptr_t image{SDL_CreateSurface(20, 10, SDL_PIXELFORMAT_RGBA32)};
    require_osbx_preview(image);
    require_osbx_preview(SDL_FillSurfaceRect(image.get(), nullptr,
        SDL_MapSurfaceRGBA(image.get(), 255, 255, 255, 255)));
    require_osbx_preview(SDL_SaveBMP(image.get(), (directory->path() / "sprite.bmp").c_str()));
    for (const std::size_t count : {27U, 256U, 1024U}) {
        const auto history = document_history_t::from(populated_osbx_document(count));
        osbx_presenter_fixture_t fixture;
        (void)measure_osbx_recording(fixture, history, directory->path(), texture.renderer.get(), count);
    }
}

TEST_CASE("OSBX preview rejects cap plus one across single and combined storyboards",
          "[osbx][player-preview][cap][negative]") {
    using namespace wormedit;
    for (const int scenario : {0, 1, 2}) {
        INFO("overflow scenario: " << scenario);
        auto document = populated_osbx_document(ui::maximum_preview_storyboard_instances + (scenario == 0));
        if (scenario != 0) {
            auto second = populated_osbx_document(1).storyboards.front();
            second.id = storyboard_id_t{31};
            if (scenario == 2) {
                second.osbx.reset();
                storyboard_sprite_t sprite{.stable_id = 1, .source_order = 1,
                    .layer = "Foreground", .origin = "Centre", .path = "sprite.bmp"};
                sprite.nodes.push_back({.kind = storyboard_node_kind_t::command,
                    .command = {.opcode = storyboard_opcode_t::fade, .start_time_ms = 0,
                        .end_time_ms = 1000, .from = {1}, .to = {1}, .value_count = 1,
                        .source_order = 1}, .source_order = 1});
                second.model = storyboard_document_model_t{.sprites = {sprite}};
            }
            document.storyboards.push_back(std::move(second));
        }
        document.visual_assets.clear();
        document.resources.clear();
        const auto history = document_history_t::from(std::move(document));
        osbx_presenter_fixture_t fixture;
        fixture.populate(history, {});
        check_refused_storyboard_budget(fixture.view);
    }
}
#endif

TEST_CASE("oversized storyboard views fail before recording any quad", "[player-preview][cap][negative]") {
    wormedit::ui::player_preview_view_t view;
    view.storyboard_sprites.resize(wormedit::ui::maximum_preview_storyboard_instances + 1);
    preview_command_storage_t commands;
    const auto result = worm_ui_nuklear_draw_player_preview_storyboard_sprites(
        &commands.canvas, nk_rect(0, 0, 640, 480), nullptr, &view, true);
    check_preview_budget(!result.drew_image);
    check_preview_budget(result.failure_count == 1);
    check_preview_budget(result.first_failure == worm_ui_nuklear_preview_visual_failure_t::command_capacity);
    check_preview_budget(commands.storage.allocated == 0);
}

namespace {
struct preview_image_lifetime_fixture_t {
    static constexpr std::size_t layer_count = wormedit::ui::player_preview_visual_layer_limit;
    static constexpr std::size_t sprite_count = wormedit::ui::maximum_preview_storyboard_instances;
    static constexpr std::size_t image_count = layer_count + sprite_count;
    std::vector<std::array<std::uint8_t, 4>> pixels;
    wormedit::ui::player_preview_view_t view;

    preview_image_lifetime_fixture_t() : pixels(image_count) {
        view.project_id = wormedit::song_id_t{1};
        view.document_revision = 2;
        for (std::size_t i = 0; i < layer_count; ++i) {
            pixels[i] = {240, 20, 30, 255};
            view.visual_layers.push_back(solid_bga_layer(
                static_cast<wormedit::visual_event_kind_t>(i), i + 1, pixels[i]));
        }
        for (std::size_t i = 0; i < sprite_count; ++i) {
            pixels[layer_count + i] = {static_cast<std::uint8_t>(i % 251 + 1),
                static_cast<std::uint8_t>(i / 251 + 1), 97, 255};
            view.storyboard_sprites.push_back({.stable_id = i + 1,
                .resource_id = wormedit::resource_id_t{layer_count + i + 1},
                .layer_order = static_cast<std::uint8_t>(i < sprite_count / 2 ? 0 : 3),
                .opacity = 1, .red = 255, .green = 255, .blue = 255,
                .image_ready = true, .image_width = 1, .image_height = 1,
                .rgba_pixels = pixels[layer_count + i]});
        }
    }

    void record(nk_command_buffer& canvas, SDL_Renderer* renderer) const {
        const auto layers = worm_ui_nuklear_draw_player_preview_visual_layers(
            &canvas, nk_rect(0, 0, 640, 480), renderer, &view);
        require_preview_budget(layers.drew_image && layers.failure_count == 0);
        for (const bool foreground : {false, true}) {
            const auto sprites = worm_ui_nuklear_draw_player_preview_storyboard_sprites(
                &canvas, nk_rect(0, 0, 640, 480), renderer, &view, foreground);
            require_preview_budget(sprites.drew_image && sprites.failure_count == 0);
        }
    }
};

auto recorded_preview_textures(const preview_command_storage_t& commands)
    -> std::vector<SDL_Texture*> {
    std::vector<SDL_Texture*> result;
    const auto* memory = static_cast<const nk_byte*>(nk_buffer_memory_const(&commands.storage));
    require_preview_budget(commands.storage.allocated != 0);
    for (auto offset = commands.canvas.begin;;) {
        const auto* command = reinterpret_cast<const nk_command*>(memory + offset);
        if (command->type == NK_COMMAND_IMAGE) {
            result.push_back(static_cast<SDL_Texture*>(
                reinterpret_cast<const nk_command_image*>(command)->img.handle.ptr));
        } else if (command->type == NK_COMMAND_CUSTOM) {
            const auto* custom = reinterpret_cast<const nk_command_custom*>(command);
            const auto* quad = static_cast<const WormUiNuklearImageQuad*>(custom->callback_data.ptr);
            result.push_back(static_cast<SDL_Texture*>(quad->image.handle.ptr));
        }
        if (offset == commands.canvas.last) break;
        offset = command->next;
    }
    return result;
}

void check_deferred_preview_pixels(SDL_Renderer* renderer, const preview_command_storage_t& commands,
                                   std::span<const std::array<std::uint8_t, 4>> expected) {
    const auto textures = recorded_preview_textures(commands);
    require_preview_budget(textures.size() == expected.size());
    require_preview_budget(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    require_preview_budget(SDL_RenderClear(renderer));
    // Consume the borrowed texture handles only after every later image has been recorded.
    for (std::size_t i = 0; i < textures.size(); ++i) {
        const SDL_FRect pixel{static_cast<float>(i), 0, 1, 1};
        require_preview_budget(SDL_RenderTexture(renderer, textures[i], nullptr, &pixel));
    }
    surface_ptr_t capture{SDL_RenderReadPixels(renderer, nullptr)};
    require_preview_budget(static_cast<bool>(capture));
    for (std::size_t i = 0; i < expected.size(); ++i) {
        std::array<std::uint8_t, 4> pixel{};
        require_preview_budget(SDL_ReadSurfacePixel(capture.get(), static_cast<int>(i), 0,
            &pixel[0], &pixel[1], &pixel[2], &pixel[3]));
        check_preview_budget(pixel == expected[i]);
    }
}

void check_pinned_preview_pressure(SDL_Renderer* renderer, preview_command_storage_t& commands,
                                   const preview_image_lifetime_fixture_t& fixture) {
    check_deferred_preview_pixels(renderer, commands, fixture.pixels);
    const std::array<std::uint8_t, 4> pixel{10, 240, 70, 255};
    auto pressure = fixture.view;
    pressure.visual_layers = {solid_bga_layer(wormedit::visual_event_kind_t::background,
        preview_image_lifetime_fixture_t::image_count + 1, pixel)};
    // One extra 1x1 image exhausts the entry budget through the same eviction path as
    // the byte budget, avoiding a >256 MiB fixture. Do not begin a new pending frame.
    const auto result = worm_ui_nuklear_draw_player_preview_visual_layers(
        &commands.canvas, nk_rect(0, 0, 640, 480), renderer, &pressure);
    check_preview_budget(!result.drew_image);
    check_preview_budget(result.failure_count == 1);
    check_preview_budget(result.first_failure == worm_ui_nuklear_preview_visual_failure_t::upload_out_of_memory);
    check_deferred_preview_pixels(renderer, commands, fixture.pixels);
}
} // namespace

TEST_CASE("preview pins distinct BGA and both sprite passes until deferred texture use completes",
          "[player-preview][texture][lifetime][cap]") {
    preview_image_lifetime_fixture_t fixture;
    texture_fixture_t texture{static_cast<int>(fixture.image_count), 1};
    require_preview_budget(static_cast<bool>(texture.renderer));
    preview_command_storage_t commands;
    commands.reset();
    fixture.record(commands.canvas, texture.renderer.get());
    check_pinned_preview_pressure(texture.renderer.get(), commands, fixture);
    // Reuse all cached entries in the next frame: hits must be pinned just like uploads.
    commands.reset();
    fixture.record(commands.canvas, texture.renderer.get());
    check_pinned_preview_pressure(texture.renderer.get(), commands, fixture);
    // Previous frames have been submitted/read back. A new frame may safely evict them.
    commands.reset();
    const std::array<std::uint8_t, 4> green{10, 240, 70, 255};
    fixture.view.visual_layers = {solid_bga_layer(wormedit::visual_event_kind_t::background,
        fixture.image_count + 1, green)};
    const auto next = worm_ui_nuklear_draw_player_preview_visual_layers(
        &commands.canvas, nk_rect(0, 0, 640, 480), texture.renderer.get(), &fixture.view);
    check_preview_budget(next.drew_image && next.failure_count == 0);
    const std::array<std::array<std::uint8_t, 4>, 1> expected{green};
    check_deferred_preview_pixels(texture.renderer.get(), commands, expected);
}

#if defined(WORMEDIT_PREVIEW_LUA_ACCEPTANCE)
namespace {
auto generated_example_history(const std::filesystem::path& root) -> wormedit::document_history_t {
    using namespace wormedit;
    auto song = osbx_preview_document();
    song.storyboards.clear();
    song.visual_assets.clear();
    song.resources.clear();
    const auto profile = make_osu_mania_profile(4);
    require_preview_budget(profile.has_value());
    song.charts.front().input_profile = profile->input;
    song.charts.front().lane_layouts.front().layout = profile->layout;
    auto history = document_history_t::from(std::move(song));
    generator_recipe_t recipe{.api_version = "1", .source_path = "banner-project.lua", .chart = chart_id_t{7}};
    const auto frozen = scripting::lua_project_source_t::from(history.document,
        history.document.charts.front(), recipe, {}, root);
    for (const auto& diagnostic : frozen.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_preview_budget(frozen.ok());
    recipe.source = frozen.value.source;
    recipe.source_hash = native_project_content_checksum(recipe.source);
    auto generated = scripting::run_lua_generator(history.document, history.document.charts.front(), recipe);
    require_preview_budget(generated.ok());
    auto proposal = generator_preview_t::propose(history, recipe, std::move(generated.value));
    for (const auto& diagnostic : proposal.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_preview_budget(proposal.value.has_value());
    require_preview_budget(apply_generator(history, *proposal.value).applied);
    require_preview_budget(history.undo.size() == 1);
    const auto saved = write_native_project(history.document);
    require_preview_budget(saved.value.has_value());
    auto loaded = parse_native_project(*saved.value);
    require_preview_budget(loaded.value.has_value());
    require_preview_budget(loaded.value->visual_assets.empty() && loaded.value->resources.empty());
    return document_history_t::from(std::move(*loaded.value));
}

auto wait_for_generated_images(osbx_presenter_fixture_t& fixture,
                               const wormedit::document_history_t& history,
                               const std::filesystem::path& root) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        fixture.populate(history, root);
        if (fixture.view.scene_available && fixture.view.storyboard_sprites.size() == 5 &&
            std::ranges::all_of(fixture.view.storyboard_sprites,
                [](const auto& sprite) { return sprite.image_ready; })) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
} // namespace

TEST_CASE("generated Lua example decodes unregistered PNGs after Apply and native reload and emits affine quads",
          "[osbx][player-preview][image-fallback][acceptance]") {
    const auto root = std::filesystem::path{WORMEDIT_LUA_EXAMPLE_DIR} / "storyboard";
    const auto history = generated_example_history(root);
    texture_fixture_t texture;
    require_preview_budget(static_cast<bool>(texture.renderer));
    osbx_presenter_fixture_t fixture;
    require_preview_budget(wait_for_generated_images(fixture, history, root));
    preview_command_storage_t commands;
    commands.reset();
    const auto drawn = worm_ui_nuklear_draw_player_preview_storyboard_sprites(
        &commands.canvas, nk_rect(0, 0, 640, 480), texture.renderer.get(), &fixture.view, false);
    check_preview_budget(drawn.drew_image && drawn.failure_count == 0);
    const auto textures = recorded_preview_textures(commands);
    require_preview_budget(textures.size() == 5);
    // Every file has its own texture; the sheared banner is still a custom affine quad.
    for (std::size_t i = 0; i < textures.size(); ++i)
        for (std::size_t j = i + 1; j < textures.size(); ++j)
            check_preview_budget(textures[i] != textures[j]);
    const auto* memory = static_cast<const nk_byte*>(nk_buffer_memory_const(&commands.storage));
    const auto* last = reinterpret_cast<const nk_command_custom*>(memory + commands.canvas.last);
    require_preview_budget(last->header.type == NK_COMMAND_CUSTOM);
    const auto* quad = static_cast<const WormUiNuklearImageQuad*>(last->callback_data.ptr);
    check_preview_budget(quad->points[0].x != quad->points[3].x);
    check_preview_budget(history.document.visual_assets.empty() && history.document.resources.empty());
    check_preview_budget(worm_ui_nuklear_player_preview_view_valid(&fixture.view));
}
#endif

#if defined(WORMEDIT_PREVIEW_LUA_ACCEPTANCE)
namespace {
struct preview_window_commands_t {
    nk_user_font font{};
    nk_context context{};
    std::vector<std::string> labels;
    std::optional<struct nk_rect> viewport;
    std::size_t sprite_quads{}, lane_rectangles{};

    preview_window_commands_t() {
        font.height = 14;
        font.width = [](nk_handle, float, const char*, int length) { return length * 7.0f; };
        require_preview_budget(nk_init_default(&context, &font));
    }
    ~preview_window_commands_t() { nk_free(&context); }
    void record(const nk_command& command) {
        if (command.type == NK_COMMAND_TEXT) {
            const auto& text = reinterpret_cast<const nk_command_text&>(command);
            labels.emplace_back(text.string, text.length);
        } else if (command.type == NK_COMMAND_CUSTOM) ++sprite_quads;
        else if (command.type == NK_COMMAND_RECT_FILLED) {
            const auto& rect = reinterpret_cast<const nk_command_rect_filled&>(command);
            if (rect.color.r == 14 && rect.color.g == 17 && rect.color.b == 24)
                viewport = nk_rect(rect.x, rect.y, rect.w, rect.h);
            if ((rect.color.r == 28 && rect.color.g == 34 && rect.color.b == 47) ||
                (rect.color.r == 34 && rect.color.g == 40 && rect.color.b == 54))
                ++lane_rectangles;
        }
    }
    void render(const wormedit::ui::player_preview_view_t& view, SDL_Renderer* renderer) {
        nk_clear(&context);
        labels.clear();
        viewport.reset();
        sprite_quads = lane_rectangles = 0;
        nk_input_begin(&context);
        nk_input_end(&context);
        worm_ui_nuklear_render_player_preview(&context, renderer, &view, nullptr, 1280, 720);
        const nk_command* command{};
        nk_foreach(command, &context) record(*command);
    }
    auto count_label(std::string_view prefix) const -> std::size_t {
        return static_cast<std::size_t>(std::ranges::count_if(labels,
            [&](const auto& label) { return label.starts_with(prefix); }));
    }
};

void check_missing_image_window(osbx_presenter_fixture_t& fixture,
                                const wormedit::document_history_t& history,
                                const std::filesystem::path& root, SDL_Renderer* renderer) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        fixture.populate(history, root);
        if (!fixture.view.status_text.empty() && fixture.view.storyboard_sprites[0].image_ready) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    require_preview_budget(fixture.view.scene_available);
    require_preview_budget(fixture.view.status_text.starts_with("Storyboard image missing.png"));
    preview_window_commands_t window;
    auto without_diagnostic = fixture.view;
    without_diagnostic.status_text.clear();
    window.render(without_diagnostic, renderer);
    require_preview_budget(window.viewport.has_value());
    const auto ordinary = *window.viewport;
    check_preview_budget(window.count_label("Frame ") == 1);
    check_preview_budget(window.count_label("Storyboard image") == 0);
    window.render(fixture.view, renderer);
    require_preview_budget(window.viewport.has_value());
    check_preview_budget(window.count_label("Storyboard image missing.png") == 1);
    check_preview_budget(window.count_label("Frame ") == 1);
    check_preview_budget(window.sprite_quads == 4);
    check_preview_budget(window.lane_rectangles == fixture.view.lanes.size());
    const auto row_height = 20.0f + window.context.style.window.spacing.y;
    check_preview_budget(window.viewport->y == ordinary.y + row_height);
    check_preview_budget(window.viewport->h == ordinary.h - row_height);
    auto unavailable = fixture.view;
    unavailable.scene_available = false;
    window.render(unavailable, renderer);
    check_preview_budget(window.count_label("Storyboard image missing.png") == 1);
    check_preview_budget(window.count_label("Frame ") == 0);
    check_preview_budget(window.sprite_quads == 0 && window.lane_rectangles == 0);
}
} // namespace

TEST_CASE("OSBX missing source diagnostic renders once and keeps valid images lanes and viewport accounting",
          "[osbx][player-preview][image-fallback][diagnostic]") {
    const auto root = std::filesystem::path{WORMEDIT_LUA_EXAMPLE_DIR} / "storyboard";
    auto history = generated_example_history(root);
    auto& images = history.document.storyboards.back().osbx->images;
    const auto banner = std::ranges::find(images, "banner-image", &wormedit::osbx_image_t::id);
    require_preview_budget(banner != images.end());
    banner->file = "missing.png";
    ++history.document.revision;
    texture_fixture_t texture;
    require_preview_budget(static_cast<bool>(texture.renderer));
    osbx_presenter_fixture_t fixture;
    check_missing_image_window(fixture, history, root, texture.renderer.get());
}
#endif

TEST_CASE("Canvas and player share a complete texture frame until both deferred groups are consumed",
          "[storyboard-canvas][texture][lifetime][cap]") {
    preview_image_lifetime_fixture_t fixture;
    texture_fixture_t texture{static_cast<int>(fixture.image_count), 1};
    require_preview_budget(static_cast<bool>(texture.renderer));
    preview_command_storage_t commands;
    commands.reset();
    // commands.reset() starts the complete frame, never a per-scene reset.
    constexpr auto half = preview_image_lifetime_fixture_t::sprite_count / 2;
    const auto canvas = std::span{fixture.view.storyboard_sprites}.first(half);
    const auto player = std::span{fixture.view.storyboard_sprites}.subspan(half);
    const auto layers = worm_ui_nuklear_draw_player_preview_visual_layers(&commands.canvas,
        nk_rect(0, 0, 640, 480), texture.renderer.get(), &fixture.view);
    require_preview_budget(layers.drew_image && layers.failure_count == 0);
    const auto first = worm_ui_nuklear_draw_player_preview_storyboard_sprites(&commands.canvas,
        nk_rect(0, 0, 640, 480), texture.renderer.get(), fixture.view.project_id,
        fixture.view.document_revision, canvas, false);
    require_preview_budget(first.drew_image && first.failure_count == 0);
    const auto second = worm_ui_nuklear_draw_player_preview_storyboard_sprites(&commands.canvas,
        nk_rect(0, 0, 640, 480), texture.renderer.get(), fixture.view.project_id,
        fixture.view.document_revision, player, true);
    require_preview_budget(second.drew_image && second.failure_count == 0);
    check_pinned_preview_pressure(texture.renderer.get(), commands, fixture);
}

namespace {
void check_large_primary_pixels(SDL_Renderer* renderer, std::span<SDL_Texture* const> handles) {
    REQUIRE(handles.size() == 2);
    CHECK(render_center_pixel(renderer, handles.front()) == std::array<std::uint8_t, 4>{255, 255, 255, 255});
    CHECK(render_center_pixel(renderer, handles.back()) == std::array<std::uint8_t, 4>{255, 255, 255, 255});
}
void check_thumbnail_refusal(const worm_ui_nuklear_preview_visual_result_t& result) {
    CHECK_FALSE(result.drew_image);
    CHECK(result.failure_count == 1);
    CHECK(result.first_failure == worm_ui_nuklear_preview_visual_failure_t::upload_out_of_memory);
}
}
TEST_CASE("Primary texture preflight pins every current source before excess thumbnail entry admission",
          "[storyboard-assets][texture][lifetime][cap]") {
    preview_image_lifetime_fixture_t fixture;
    texture_fixture_t texture{static_cast<int>(fixture.image_count), 1}; REQUIRE(texture.renderer);
    wormedit::ui::workspace_view_t root;
    root.player_preview = fixture.view; root.player_preview.visible = true;
    preview_command_storage_t commands; commands.reset();
    const auto pinned = worm_ui_nuklear_pin_primary_preview_textures(texture.renderer.get(), root);
    REQUIRE(pinned.failure_count == 0);
    const std::array<std::uint8_t, 4> pixel{1, 2, 3, 255};
    const auto thumbnail = solid_bga_layer(wormedit::visual_event_kind_t::background, 9000, pixel);
    const auto result = worm_ui_nuklear_draw_player_preview_thumbnail(&commands.canvas,
        nk_rect(0, 0, 16, 16), texture.renderer.get(), fixture.view.project_id, fixture.view.document_revision, thumbnail);
    check_thumbnail_refusal(result);
    fixture.record(commands.canvas, texture.renderer.get());
    check_deferred_preview_pixels(texture.renderer.get(), commands, fixture.pixels);
}
TEST_CASE("Primary texture byte pins refuse thumbnails without evicting either deferred image",
          "[storyboard-assets][texture][lifetime][byte-cap]") {
    texture_fixture_t texture; REQUIRE(texture.renderer);
    // Two real 128MiB RGBA uploads exhaust the existing 256MiB owner budget.
    // The same immutable pixel input may feed distinct source identities.
    std::vector<std::uint8_t> pixels(8192U * 4096U * 4U, 255);
    wormedit::ui::workspace_view_t root;
    auto& player = root.player_preview;
    player.visible = true; player.project_id = wormedit::song_id_t{1}; player.document_revision = 2;
    for (std::uint64_t id : {1, 2}) player.visual_layers.push_back({.resource_id = wormedit::resource_id_t{id},
        .static_image = true, .image_ready = true, .image_content_id = 1,
        .image_width = 8192, .image_height = 4096, .rgba_pixels = pixels});
    preview_command_storage_t commands; commands.reset();
    REQUIRE(worm_ui_nuklear_pin_primary_preview_textures(texture.renderer.get(), root).failure_count == 0);
    const std::array<std::uint8_t, 4> pixel{1, 2, 3, 255};
    const auto thumbnail = solid_bga_layer(wormedit::visual_event_kind_t::background, 3, pixel);
    check_thumbnail_refusal(worm_ui_nuklear_draw_player_preview_thumbnail(&commands.canvas,
        nk_rect(0, 0, 16, 16), texture.renderer.get(), player.project_id, player.document_revision, thumbnail));
    const auto primary = worm_ui_nuklear_draw_player_preview_visual_layers(&commands.canvas,
        nk_rect(0, 0, 16, 16), texture.renderer.get(), &player);
    CHECK(primary.drew_image);
    CHECK(primary.failure_count == 0);
    const auto handles = recorded_preview_textures(commands);
    check_large_primary_pixels(texture.renderer.get(), handles);
}
