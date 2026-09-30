#include "ui/field/field_renderer.h"
#include "ui/nuklear/sdl_backend.h"
#include "ui/nuklear/arrow_glyph.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <new>

namespace {
thread_local bool count_field_allocations{};
thread_local std::size_t field_allocations{};
} // namespace
void* operator new(std::size_t size) {
    if (count_field_allocations)
        ++field_allocations;
    if (auto* value = std::malloc(size == 0 ? 1 : size))
        return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept {
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}

using namespace wormedit::ui::field;
namespace {
void expect_field(bool value) {
    REQUIRE(value);
}
auto font_query(void*, float, std::uint32_t, std::uint32_t) -> glyph_t {
    return {{0, 0}, {8, 12}, {.1f, .2f}, {.9f, .8f}, 8};
}
auto fixture_font() -> font_t {
    static unsigned texture_identity;
    return {nullptr, reinterpret_cast<SDL_Texture*>(&texture_identity), 12, font_query};
}
void dense_scene(renderer_t& renderer) {
    renderer.begin({0, 0, 1280, 980});
    for (unsigned index = 0; index < 256; ++index) {
        const auto x = static_cast<float>(index % 32) * 32;
        const auto y = static_cast<float>(index / 32) * 80;
        renderer.rect(0, {x, y, 30, 70}, {20, 30, 40, 255});
        const std::array<SDL_FPoint, 7> arrow{{{x + 15, y + 5},
                                               {x + 28, y + 19},
                                               {x + 21, y + 19},
                                               {x + 21, y + 35},
                                               {x + 9, y + 35},
                                               {x + 9, y + 19},
                                               {x + 2, y + 19}}};
        renderer.polygon(2, arrow, {220, 180, 40, 255}, 1);
    }
    renderer.polygon(3, rounded_rectangle_points({20, 850, 40, 20}, 3), {180, 110, 210, 255});
    renderer.stroke_polygon(3, rounded_rectangle_points({20, 850, 40, 20}, 3), 1,
                             {50, 35, 58, 255});
    renderer.text(3, {10, 900}, "M1 / \xc2\xb5", {200, 210, 220, 255}, fixture_font());
}
} // namespace

TEST_CASE("Field rectangles snap clip and cull without changing submission order", "[ui][field]") {
    renderer_t renderer;
    renderer.begin({0, 0, 100, 100});
    renderer.rect(2, {-2.2f, 1.4f, 12.6f, 10.2f}, {255, 100, 50, 200});
    renderer.rect(1, {20, 20, 10, 10}, {10, 20, 30, 255});
    renderer.rect(1, {40, 20, 10, 10}, {10, 20, 30, 255});
    renderer.rect(0, {200, 200, 10, 10}, {255, 255, 255, 255});
    renderer.rect(0, {0, 0, 10, 10}, {255, 255, 255, 0});
    expect_field(renderer.workload().vertices == 12);
    expect_field(renderer.workload().indices == 18);
    expect_field(renderer.workload().draw_calls == 2);
    expect_field(renderer.vertices()[0].position.x == 0);
    expect_field(renderer.vertices()[0].position.y == 1);
    expect_field(renderer.vertices()[2].position.x == 10);
    expect_field(renderer.vertices()[2].position.y == 12);
    expect_field(renderer.batches()[0].layer == 2);
    expect_field(renderer.batches()[1].layer == 1);
    expect_field(renderer.batches()[1].index_offset == 6);
    expect_field(renderer.batches()[1].index_count == 12);
}

TEST_CASE("Field glyphs clip UVs and decode valid UTF8 with bounded replacement", "[ui][field]") {
    renderer_t renderer;
    renderer.begin({0, 0, 100, 100});
    renderer.text(0, {-4, 0}, "A", {255, 255, 255, 255}, fixture_font());
    expect_field(renderer.workload().vertices == 4);
    expect_field(renderer.vertices()[0].position.x == 0);
    expect_field(renderer.vertices()[2].position.x == 4);
    expect_field(std::abs(renderer.vertices()[0].tex_coord.x - .5f) < .0001f);
    expect_field(decode_utf8("\xf0\x9f\x8e\xb5").value == 0x1f3b5);
    expect_field(decode_utf8("\xed\xa0\x80").value == 0xfffd);
    expect_field(decode_utf8("\xc0\x80").bytes == 1);
    expect_field(decode_utf8("\xe2\x82").bytes == 1);
    expect_field(decode_utf8("").bytes == 0);
}

TEST_CASE("Field geometry retains vertex index and batch storage after warmup", "[ui][field]") {
    renderer_t renderer;
    dense_scene(renderer);
    const auto expected = renderer.workload();
    const auto* vertices = renderer.vertices().data();
    const auto* indices = renderer.indices().data();
    const auto* batches = renderer.batches().data();
    field_allocations = 0;
    count_field_allocations = true;
    for (unsigned frame = 0; frame < 20; ++frame)
        dense_scene(renderer);
    count_field_allocations = false;
    expect_field(field_allocations == 0);
    expect_field(renderer.vertices().data() == vertices);
    expect_field(renderer.indices().data() == indices);
    expect_field(renderer.batches().data() == batches);
    expect_field(renderer.workload().vertices == expected.vertices);
    expect_field(renderer.workload().indices == expected.indices);
}

TEST_CASE("Field subpixel grid strokes remain visible on the pixel grid", "[ui][field]") {
    renderer_t renderer;
    renderer.begin({0, 0, 100, 100});
    renderer.hline(0, 0, 100, 50, .7f, {100, 120, 140, 255});
    renderer.vline(0, 40, 0, 100, .7f, {100, 120, 140, 255});
    expect_field(renderer.workload().vertices == 8);
    expect_field(renderer.vertices()[2].position.y - renderer.vertices()[0].position.y == 1);
    expect_field(renderer.vertices()[6].position.x - renderer.vertices()[4].position.x == 1);
}

namespace {
struct field_submission_fixture_t {
    SDL_Window* window{};
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};
    nk_context* context{};
    field_submission_fixture_t() {
        expect_field(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("field barrier", 128, 128, SDL_WINDOW_HIDDEN);
        expect_field(window != nullptr);
        surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
        expect_field(surface != nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        expect_field(renderer != nullptr);
        context = nk_sdl_init(window, renderer, nk_sdl_allocator());
        expect_field(context != nullptr);
        nk_sdl_style_set_debug_font(context);
    }
    ~field_submission_fixture_t() {
        if (context)
            nk_sdl_shutdown(context);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (surface)
            SDL_DestroySurface(surface);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
    auto pixel(int x, int y) const -> SDL_Color {
        SDL_Color result{};
        expect_field(SDL_ReadSurfacePixel(surface, x, y, &result.r, &result.g, &result.b, &result.a));
        return result;
    }
};
void build_interleaved_field(nk_context* context) {
    using namespace wormedit::ui::nuklear;
    nk_input_begin(context);
    nk_input_end(context);
    expect_field(nk_begin(context, "barriers", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    auto* canvas = nk_window_get_canvas(context);
    nk_push_scissor(canvas, nk_rect(0, 0, 128, 128));
    nk_fill_rect(canvas, nk_rect(20, 20, 70, 70), 0, nk_rgb(255, 0, 0));
    auto& field = begin_compose_field(context, {0, 0, 128, 128});
    field.rect(0, {30, 30, 50, 50}, {0, 255, 0, 255});
    insert_compose_field(context, canvas, nk_rect(0, 0, 128, 128));
    nk_fill_rect(canvas, nk_rect(40, 40, 30, 30), 0, nk_rgb(0, 0, 255));
    field.rect(0, {50, 50, 10, 10}, {255, 255, 0, 255});
    insert_compose_field(context, canvas, nk_rect(0, 0, 128, 128));
    nk_end(context);
}
} // namespace
TEST_CASE("Field conversion barriers preserve interleaved SDL and Nuklear layers",
          "[ui][field][sdl]") {
    field_submission_fixture_t fixture;
    build_interleaved_field(fixture.context);
    const SDL_Rect clip{0, 0, 128, 128};
    expect_field(SDL_SetRenderClipRect(fixture.renderer, &clip));
    expect_field(SDL_SetRenderDrawBlendMode(fixture.renderer, SDL_BLENDMODE_NONE));
    wormedit::ui::nuklear::render_sdl_backend(fixture.context, NK_ANTI_ALIASING_ON);
    expect_field(SDL_RenderPresent(fixture.renderer));
    const auto red = fixture.pixel(25, 25), green = fixture.pixel(35, 35);
    const auto blue = fixture.pixel(45, 45), yellow = fixture.pixel(55, 55);
    expect_field(red.r == 255 && red.g == 0 && red.b == 0);
    expect_field(green.r == 0 && green.g == 255 && green.b == 0);
    expect_field(blue.r == 0 && blue.g == 0 && blue.b == 255);
    expect_field(yellow.r == 255 && yellow.g == 255 && yellow.b == 0);
    SDL_BlendMode blend{};
    expect_field(SDL_GetRenderDrawBlendMode(fixture.renderer, &blend));
    expect_field(blend == SDL_BLENDMODE_NONE);
    SDL_Rect retained{};
    expect_field(SDL_GetRenderClipRect(fixture.renderer, &retained));
    expect_field(retained.x == clip.x && retained.y == clip.y && retained.w == clip.w &&
                 retained.h == clip.h);
}

TEST_CASE("Arrow antialias fringe straddles the authored polygon and closes its outline",
          "[ui][field]") {
    renderer_t renderer;
    const std::array<SDL_FPoint, 4> square{{{10, 10}, {20, 10}, {20, 20}, {10, 20}}};
    renderer.begin({0, 0, 100, 100});
    renderer.polygon(0, square, {255, 100, 50, 255}, 1);
    expect_field(renderer.vertices().size() == 8);
    expect_field(renderer.indices().size() == 30);
    expect_field(renderer.vertices()[0].position.x == 10.5f);
    expect_field(renderer.vertices()[0].position.y == 10.5f);
    expect_field(renderer.vertices()[4].position.x == 9.5f);
    expect_field(renderer.vertices()[4].position.y == 9.5f);
    expect_field(renderer.vertices()[0].color.a == 1 && renderer.vertices()[4].color.a == 0);
    renderer.begin({0, 0, 100, 100});
    renderer.stroke_polygon(0, square, 1, {0, 0, 0, 255}, 1);
    expect_field(renderer.vertices().size() == 12 && renderer.indices().size() == 48);
    expect_field(renderer.vertices()[0].position.x == 9);
    expect_field(renderer.vertices()[1].position.x == 10);
    expect_field(renderer.vertices()[2].position.x == 11);
    expect_field(renderer.vertices()[1].color.a == 1);
    // The final edge joins the final corner back to the first, rather than leaving a seam.
    expect_field(renderer.indices()[37] == 0);
}

TEST_CASE("Field polygon vertices preserve canonical arrow artwork in all lane directions",
          "[ui][field]") {
    renderer_t renderer;
    for (std::uint8_t direction : {2, 3, 4, 5}) {
        const auto canonical = wormedit::ui::arrow_polygon(direction, 13.4f, 27.8f, 42.3f);
        std::array<SDL_FPoint, 7> points{};
        for (std::size_t index = 0; index < canonical.size(); ++index)
            points[index] = {std::trunc(canonical[index].x), std::trunc(canonical[index].y)};
        renderer.begin({0, 0, 128, 128});
        renderer.polygon(2, points, {255, 255, 255, 255});
        expect_field(renderer.vertices().size() == canonical.size());
        for (std::size_t index = 0; index < canonical.size(); ++index) {
            expect_field(renderer.vertices()[index].position.x == std::trunc(canonical[index].x));
            expect_field(renderer.vertices()[index].position.y == std::trunc(canonical[index].y));
        }
    }
}

TEST_CASE("Field rounded tiles retain their silhouette and use closed shared-vertex outlines",
          "[ui][field]") {
    renderer_t renderer;
    const auto rounded = rounded_rectangle_points({10, 20, 40, 20}, 3);
    expect_field(rounded.front().x == 10 && rounded.front().y == 23);
    expect_field(rounded[3].x == 13 && rounded[3].y == 20);
    expect_field(rounded[11].x == 47 && rounded[11].y == 40);
    renderer.begin({0, 0, 100, 100});
    renderer.polygon(3, rounded, {180, 110, 210, 255});
    renderer.stroke_polygon(3, rounded, 1, {50, 35, 58, 255});
    expect_field(renderer.vertices().size() == 48);
    expect_field(renderer.indices().size() == 138);
    expect_field(renderer.batches().size() == 1);
    const auto before = renderer.workload();
    renderer.stroke_polygon(3, rounded_rectangle_points({200, 200, 40, 20}, 3), 1,
                             {50, 35, 58, 255});
    expect_field(renderer.workload().vertices == before.vertices);
}

TEST_CASE("Field solids and interleaved glyphs share the white font atlas without reordering",
          "[ui][field]") {
    renderer_t renderer;
    const auto font = fixture_font();
    const SDL_FPoint white{.03f, .07f};
    renderer.begin({0, 0, 100, 100}, {1, 1}, {font.texture, white});
    renderer.rect(2, {10, 10, 20, 20}, {100, 120, 140, 255});
    renderer.text(2, {15, 15}, "A", {240, 240, 240, 255}, font);
    const std::array<SDL_FPoint, 4> outline{{{10, 10}, {30, 10}, {30, 30}, {10, 30}}};
    renderer.stroke_polygon(2, outline, 1, {250, 200, 100, 255});
    expect_field(renderer.workload().vertices == 16);
    expect_field(renderer.workload().indices == 36);
    expect_field(renderer.workload().draw_calls == 1);
    expect_field(renderer.batches().front().texture == font.texture);
    expect_field(renderer.vertices()[0].tex_coord.x == white.x);
    expect_field(renderer.vertices()[0].tex_coord.y == white.y);
    expect_field(renderer.vertices()[4].tex_coord.x == .1f);
    expect_field(renderer.vertices()[4].tex_coord.y == .2f);
    expect_field(renderer.vertices()[8].tex_coord.x == white.x);
    expect_field(renderer.vertices()[8].tex_coord.y == white.y);
    expect_field(renderer.indices()[0] == 0);
    expect_field(renderer.indices()[6] == 4);
    expect_field(renderer.indices()[12] == 8);
    renderer.begin({0, 0, 100, 100});
    renderer.rect(2, {10, 10, 20, 20}, {100, 120, 140, 255});
    expect_field(renderer.batches().front().texture == nullptr);
    expect_field(renderer.vertices()[0].tex_coord.x == 0);
}
