#define NK_IMPLEMENTATION

#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/player_preview_texture.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>

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

auto uploaded_texture(SDL_Renderer* renderer, WormUiPlayerPreviewView& view,
                      WormUiPlayerPreviewVisualLayerView& layer) -> SDL_Texture* {
    nuklear_buffer_t storage;
    nk_command_buffer canvas{};
    nk_command_buffer_init(&canvas, &storage.value, NK_CLIPPING_OFF);
    view.visual_layers = &layer;
    view.visual_layer_count = 1;
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

} // namespace

// image_quad.cpp references the application-side converted draw-list callback. The visual-layer
// path exercised here records ordinary Nuklear image commands, so no converted quad is expected.
void worm_ui_nuklear_append_image_quad(void*, short, short, unsigned short, unsigned short,
                                       nk_handle) {}

TEST_CASE("player preview uploads and replaces renderer-backed video textures",
          "[integration][frontend][visual][player-preview][texture]") {
    texture_fixture_t fixture;
    REQUIRE(fixture.renderer);
    INFO("SDL renderer: " << SDL_GetRendererName(fixture.renderer.get()));

    WormUiPlayerPreviewView view{
        .visual_fit = WORM_UI_VISUAL_FIT_STRETCH,
        .project_id = 1,
        .document_revision = 2,
    };
    std::array<std::uint8_t, 2U * 2U * 4U> pixels{};
    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        pixels[index] = 255;
        pixels[index + 3] = 255;
    }
    WormUiPlayerPreviewVisualLayerView layer{
        .object_id = 3,
        .resource_id = 4,
        .kind = WORM_UI_PLAYER_PREVIEW_VISUAL_VIDEO,
        .video_frame = true,
        .image_ready = true,
        .image_content_id = 1,
        .image_width = 2,
        .image_height = 2,
        .rgba_pixels = pixels.data(),
        .rgba_byte_count = pixels.size(),
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
    WormUiPlayerPreviewVisualLayerView layer{
        .object_id = 3,
        .resource_id = 4,
        .kind = WORM_UI_PLAYER_PREVIEW_VISUAL_VIDEO,
        .video_clock_ready = true,
        .video_elapsed_sample_frames = 5,
        .video_time_numerator = 1,
        .video_time_denominator = 2,
        .video_frame = true,
        .image_pending = true,
        .diagnostic = WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_FRAME_LATE,
        .diagnostic_text = "late",
    };
    WormUiPlayerPreviewView view{
        .visible = true,
        .scene_available = true,
        .skin = WORM_UI_PLAYER_PREVIEW_SKIN_MANIA,
        .visual_fit = WORM_UI_VISUAL_FIT_CONTAIN,
        .project_id = 1,
        .chart_id = 2,
        .document_revision = 3,
        .sample_frame = 5,
        .viewport_width_pixels = 300.0,
        .viewport_height_pixels = 420.0,
        .visual_layers = &layer,
        .visual_layer_count = 1,
    };

    CHECK(worm_ui_nuklear_player_preview_view_valid(&view));
    layer.diagnostic_text = nullptr;
    CHECK_FALSE(worm_ui_nuklear_player_preview_view_valid(&view));
    layer.diagnostic_text = "late";
    layer.image_pending = false;
    CHECK_FALSE(worm_ui_nuklear_player_preview_view_valid(&view));
    layer.image_pending = true;
    layer.video_clock_ready = false;
    layer.video_elapsed_sample_frames = 0;
    layer.video_time_numerator = 0;
    layer.video_time_denominator = 0;
    CHECK_FALSE(worm_ui_nuklear_player_preview_view_valid(&view));
}
