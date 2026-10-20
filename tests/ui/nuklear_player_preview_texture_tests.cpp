#define NK_IMPLEMENTATION

#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/player_preview_texture.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

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

    texture_fixture_t() {
        if (std::getenv("DISPLAY") != nullptr && SDL_Init(SDL_INIT_VIDEO)) {
            window.reset(SDL_CreateWindow("wormedit texture evidence", 16, 16, SDL_WINDOW_HIDDEN));
            if (window) {
                renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
            }
        }
        if (!renderer) {
            backing_surface.reset(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32));
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

// image_quad.cpp references the application-side converted draw-list callback. The visual-layer
// path exercised here records ordinary Nuklear image commands, so no converted quad is expected.
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
