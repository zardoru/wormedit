#include "ui/field/field_renderer.h"
#include "ui/nuklear/sdl_backend.h"
#include "ui/nuklear/arrow_glyph.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

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
namespace { void expect_field(bool value); }
TEST_CASE("Waveform ring keeps world rows while its head wraps in either direction", "[ui][field]") {
    ring_rows_t ring;
    std::array<std::uint64_t, 4> rows{10, 11, 12, 13};
    ring.update(1, 0, rows);
    expect_field(ring.regenerated_rows() == 4);
    ring.update(1, 0, rows);
    expect_field(ring.regenerated_rows() == 0);
    rows = {11, 12, 13, 14};
    ring.update(1, 1, rows);
    expect_field(ring.regenerated_rows() == 1);
    expect_field(ring.head() == 1);
    expect_field(ring.ranges()[0].count == 3);
    expect_field(ring.ranges()[1].count == 1);
    expect_field(ring.dirty_ranges().size() == 1);
    expect_field(ring.dirty_ranges()[0].first == 0);
    rows = {13, 14, 15, 16};
    ring.update(1, 3, rows);
    expect_field(ring.regenerated_rows() == 2);
    expect_field(ring.head() == 3);
    rows = {12, 13, 14, 15};
    ring.update(1, 2, rows);
    expect_field(ring.regenerated_rows() == 1);
    rows = {9, 10, 11, 12};
    ring.update(1, -1, rows);
    expect_field(ring.regenerated_rows() == 3);
    expect_field(ring.head() == 3);
}
TEST_CASE("Waveform ring invalidates generations seeks and changed retained detail", "[ui][field]") {
    ring_rows_t ring;
    std::array<std::uint64_t, 4> rows{10, 11, 12, 13};
    ring.update(1, 0, rows);
    rows[2] = 42;
    ring.update(1, 0, rows);
    expect_field(ring.regenerated_rows() == 1);
    expect_field(ring.dirty_ranges()[0].first == 2);
    ring.update(2, 0, rows);
    expect_field(ring.regenerated_rows() == 4);
    ring.update(2, 100, rows);
    expect_field(ring.regenerated_rows() == 4);
    ring.invalidate();
    ring.update(2, 100, rows);
    expect_field(ring.regenerated_rows() == 4);
    ring.update(2, 100, std::span(rows).first(2));
    expect_field(ring.capacity() == 2);
    expect_field(ring.regenerated_rows() == 2);
    ring.update(2, std::numeric_limits<std::int64_t>::max(), rows);
    expect_field(ring.regenerated_rows() == 0);
}
TEST_CASE("Waveform ring reuses bounded storage for 4096 rows without warm allocations", "[ui][field]") {
    ring_rows_t ring;
    std::array<std::uint64_t, 4096> rows{};
    ring.update(1, 4095, rows);
    expect_field(ring.regenerated_rows() == rows.size());
    expect_field(ring.dirty_ranges().size() == 2);
    field_allocations = 0;
    count_field_allocations = true;
    ring.update(1, 4096, rows);
    count_field_allocations = false;
    expect_field(field_allocations == 0);
    expect_field(ring.regenerated_rows() == 1);
    expect_field(ring.head() == 0);
}
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
    field_submission_fixture_t(bool accelerated = false) {
        expect_field(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("field barrier", 128, 128, SDL_WINDOW_HIDDEN);
        expect_field(window != nullptr);
        if (accelerated) {
            renderer = SDL_CreateRenderer(window, "opengl");
        } else {
            surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
            expect_field(surface != nullptr);
            renderer = SDL_CreateSoftwareRenderer(surface);
        }
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

void build_two_deferred_fields(nk_context* context) {
    using namespace wormedit::ui::nuklear;
    nk_input_begin(context);
    nk_input_end(context);
    expect_field(nk_begin(context, "two fields", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    auto* canvas = nk_window_get_canvas(context);
    auto& first = begin_compose_field(context, {0, 0, 128, 128});
    first.rect(0, {12, 12, 32, 32}, {240, 20, 30, 255});
    insert_compose_field(context, canvas, nk_rect(0, 0, 128, 128));
    auto& second = begin_compose_field(context, {0, 0, 128, 128});
    second.rect(0, {72, 72, 32, 32}, {20, 40, 240, 255});
    insert_compose_field(context, canvas, nk_rect(0, 0, 128, 128));
    expect_field(&first != &second);
    expect_field(first.indices().size() == 6);
    expect_field(second.indices().size() == 6);
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

TEST_CASE("Separate deferred Compose fields retain both pane geometries through submission",
          "[ui][field][sdl][compare]") {
    field_submission_fixture_t fixture;
    build_two_deferred_fields(fixture.context);
    const SDL_Rect clip{0, 0, 128, 128};
    expect_field(SDL_SetRenderClipRect(fixture.renderer, &clip));
    expect_field(SDL_SetRenderDrawBlendMode(fixture.renderer, SDL_BLENDMODE_NONE));
    wormedit::ui::nuklear::render_sdl_backend(fixture.context, NK_ANTI_ALIASING_ON);
    expect_field(SDL_RenderPresent(fixture.renderer));
    const auto first = fixture.pixel(20, 20);
    const auto second = fixture.pixel(80, 80);
    expect_field(first.r == 240 && first.g == 20 && first.b == 30);
    expect_field(second.r == 20 && second.g == 40 && second.b == 240);
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

namespace {
auto envelope_test_rows(std::int64_t begin, std::size_t count) -> std::vector<envelope_row_t> {
    std::vector<envelope_row_t> rows;
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = begin + static_cast<std::int64_t>(index);
        const auto amplitude = .15f + static_cast<float>((world + 1000) % 9) * .07f;
        rows.push_back({static_cast<double>(world) + .5, {amplitude * .6f, amplitude * .4f},
                                                        {amplitude, amplitude * .8f}});
    }
    return rows;
}
void envelope_reference(renderer_t& geometry, std::span<const envelope_row_t> rows, double origin) {
    for (const auto& row : rows) {
        const auto y = static_cast<float>(row.world_y - origin);
        geometry.hline(6, 64 - row.rms[0] * 50, 64, y, 2, {57,137,167,96});
        geometry.hline(6, 64, 64 + row.rms[1] * 50, y, 2, {57,137,167,96});
        geometry.hline(6, 64 - row.peak[0] * 50, 64, y, 1, {114,222,239,142});
        geometry.hline(6, 64, 64 + row.peak[1] * 50, y, 1, {114,222,239,142});
    }
}
auto envelope_pixels(field_submission_fixture_t& fixture) -> std::vector<SDL_Color> {
    expect_field(SDL_FlushRenderer(fixture.renderer));
    std::vector<SDL_Color> pixels;
    auto* surface = fixture.surface ? fixture.surface : SDL_RenderReadPixels(fixture.renderer, nullptr);
    expect_field(surface != nullptr);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) {
            SDL_Color pixel{};
            expect_field(SDL_ReadSurfacePixel(surface, x, y, &pixel.r, &pixel.g, &pixel.b, &pixel.a));
            pixels.push_back(pixel);
        }
    }
    if (!fixture.surface) SDL_DestroySurface(surface);
    return pixels;
}
auto maximum_envelope_rgb_error(std::span<const SDL_Color> before, std::span<const SDL_Color> after) -> unsigned {
    unsigned maximum_error = 0;
    for (std::size_t index = 0; index < before.size(); ++index) {
        for (const auto difference : {std::abs(int(before[index].r) - int(after[index].r)),
                                     std::abs(int(before[index].g) - int(after[index].g)),
                                     std::abs(int(before[index].b) - int(after[index].b))})
            maximum_error = std::max(maximum_error, static_cast<unsigned>(difference));
    }
    return maximum_error;
}
void compare_envelope_frame(field_submission_fixture_t& fixture, renderer_t& cached, double origin) {
    const auto begin = static_cast<std::int64_t>(std::floor(origin)) - 2;
    const auto rows = envelope_test_rows(begin, 69);
    renderer_t direct;
    direct.begin({0,0,128,64});
    direct.rect(0, {0,0,128,64}, {37,43,55,255});
    // The envelope contract snaps its origin once; exact sample curves do not.
    envelope_reference(direct, rows, std::floor(origin));
    expect_field(direct.submit(fixture.renderer));
    const auto before = envelope_pixels(fixture);
    cached.begin({0,0,128,64}, {1,1}, {}, fixture.renderer);
    cached.rect(0, {0,0,128,64}, {37,43,55,255});
    expect_field(cached.envelope(0, 6, rows, {14,0,100,64}, origin, 1,
                             {{57,137,167,96}, {114,222,239,142}, false}));
    expect_field(cached.submit(fixture.renderer));
    const auto after = envelope_pixels(fixture);
    const auto maximum_error = maximum_envelope_rgb_error(before, after);
    INFO("Origin " << origin << ", maximum RGB difference " << maximum_error);
    expect_field(maximum_error <= 1);
}
}
TEST_CASE("Waveform texture ring composites stable RMS pixels with delta rasterization",
          "[ui][field][sdl][waveform]") {
    field_submission_fixture_t fixture;
    renderer_t cached;
    compare_envelope_frame(fixture, cached, 0);
    expect_field(cached.envelope_work().regenerated_rows == 66);
    compare_envelope_frame(fixture, cached, 0);
    expect_field(cached.envelope_work().regenerated_rows == 0);
    expect_field(cached.envelope_work().raster_vertices == 0);
    compare_envelope_frame(fixture, cached, 1);
    expect_field(cached.envelope_work().regenerated_rows == 1);
    compare_envelope_frame(fixture, cached, 65);
    expect_field(cached.envelope_work().regenerated_rows == 64);
    compare_envelope_frame(fixture, cached, 66);
    expect_field(cached.envelope_work().regenerated_rows == 1);
    compare_envelope_frame(fixture, cached, 65);
    expect_field(cached.envelope_work().regenerated_rows == 1);
    compare_envelope_frame(fixture, cached, 65.3);
    expect_field(cached.envelope_work().regenerated_rows == 0);
    compare_envelope_frame(fixture, cached, 1000);
    expect_field(cached.envelope_work().regenerated_rows == 66);
}
TEST_CASE("Waveform texture ring regenerates plateau rows whose values change",
          "[ui][field][sdl][waveform]") {
    field_submission_fixture_t fixture;
    renderer_t cached;
    const auto plateau = [](float amplitude, bool present) {
        std::vector<envelope_row_t> rows;
        for (std::int64_t world = -2; world < 67; ++world) {
            if (present) rows.push_back({static_cast<double>(world) + .5,
                {amplitude, amplitude}, {amplitude, amplitude}});
        }
        return rows;
    };
    const auto frame = [&](const std::vector<envelope_row_t>& rows) {
        cached.begin({0,0,128,64}, {1,1}, {}, fixture.renderer);
        expect_field(cached.envelope(0, 6, rows.empty() ? envelope_test_rows(1000, 1) : rows,
            {14,0,100,64}, 0, 1, {{57,137,167,96}, {114,222,239,142}, false}));
        expect_field(cached.submit(fixture.renderer));
    };
    // Rows with equal neighbors, first absent and then present, then another value.
    frame(plateau(.5f, false));
    frame(plateau(.5f, true));
    expect_field(cached.envelope_work().regenerated_rows == 66);
    frame(plateau(.25f, true));
    expect_field(cached.envelope_work().regenerated_rows == 66);
}
TEST_CASE("Waveform texture ring renders the reversed world axis during downscroll",
          "[ui][field][sdl][waveform]") {
    field_submission_fixture_t fixture;
    renderer_t cached;
    compare_envelope_frame(fixture, cached, -64);
    expect_field(cached.envelope_work().regenerated_rows == 66);
    compare_envelope_frame(fixture, cached, -65);
    expect_field(cached.envelope_work().regenerated_rows == 1);
    compare_envelope_frame(fixture, cached, -65.75);
    expect_field(cached.envelope_work().regenerated_rows == 1);
    compare_envelope_frame(fixture, cached, -65);
    expect_field(cached.envelope_work().regenerated_rows == 1);
}
namespace {
void draw_scaled_envelope_reference(field_submission_fixture_t& fixture,
                                    std::span<const envelope_row_t> rows, double origin, float scale) {
    const auto height = static_cast<int>(std::ceil(64 / scale)) + 2;
    auto* texture = SDL_CreateTexture(fixture.renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 128, height);
    expect_field(texture != nullptr);
    expect_field(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED));
    expect_field(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
    expect_field(SDL_SetRenderTarget(fixture.renderer, texture));
    expect_field(SDL_SetRenderDrawColor(fixture.renderer, 0, 0, 0, 0));
    expect_field(SDL_RenderClear(fixture.renderer));
    renderer_t direct;
    direct.begin({0,0,128,static_cast<float>(height)});
    envelope_reference(direct, rows, std::floor(origin) - 1);
    expect_field(direct.submit(fixture.renderer));
    expect_field(SDL_SetRenderTarget(fixture.renderer, nullptr));
    expect_field(SDL_SetRenderDrawColor(fixture.renderer, 37,43,55,255));
    expect_field(SDL_RenderClear(fixture.renderer));
    // Canonical nearest sampling resolves exact texel-boundary ties upward.
    const double tie_bias = 1.0 / (64 * scale);
    const auto top = static_cast<float>((1 + tie_bias) / height);
    const auto bottom = static_cast<float>((64.0 / scale + 1 + tie_bias) / height);
    const std::array<SDL_Vertex, 4> quad{{
        {{0,0}, {1,1,1,1}, {0,top}}, {{128,0}, {1,1,1,1}, {1,top}},
        {{128,64}, {1,1,1,1}, {1,bottom}}, {{0,64}, {1,1,1,1}, {0,bottom}}}};
    const std::array<int, 6> indices{0,1,2,0,2,3};
    expect_field(SDL_RenderGeometry(fixture.renderer, texture, quad.data(), quad.size(),
                                    indices.data(), indices.size()));
    SDL_DestroyTexture(texture);
}
void compare_scaled_envelope_frame(field_submission_fixture_t& fixture, renderer_t& cached,
                                   double origin, float scale) {
    const auto count = static_cast<std::size_t>(std::ceil(64 / scale)) + 5;
    const auto rows = envelope_test_rows(static_cast<std::int64_t>(std::floor(origin)) - 2, count);
    draw_scaled_envelope_reference(fixture, rows, origin, scale);
    const auto before = envelope_pixels(fixture);
    cached.begin({0,0,128,64}, {1,1}, {}, fixture.renderer);
    cached.rect(0, {0,0,128,64}, {37,43,55,255});
    expect_field(cached.envelope(0, 6, rows, {14,0,100,64}, origin, 1,
                             {{57,137,167,96}, {114,222,239,142}, false}, scale));
    expect_field(cached.submit(fixture.renderer));
    const auto after = envelope_pixels(fixture);
    const auto error = maximum_envelope_rgb_error(before, after);
    const auto work = cached.envelope_work();
    std::fprintf(stderr, "GPU scale=%.6f origin=%.1f maxRGB=%u dirty=%zu raster=%zu quads=%zu\n",
                 scale, origin, error, work.regenerated_rows, work.raster_vertices, work.composites);
    INFO("Scale " << scale << ", origin " << origin << ", maximum RGB difference " << error);
    expect_field(error <= 1);
}
}
TEST_CASE("Waveform rings retain canonical projection rows across scaled canvas composition",
          "[ui][field][sdl][waveform]") {
    if (!std::getenv("WORMEDIT_TEST_GPU_ENVELOPE")) {
        field_submission_fixture_t fixture;
        renderer_t cached;
        const auto rows = envelope_test_rows(-2, 80);
        cached.begin({0,0,128,64}, {1,1}, {}, fixture.renderer);
        expect_field(!cached.envelope(0, 6, rows, {14,0,100,64}, 0, 1,
            {{57,137,167,96}, {114,222,239,142}, false}, 756.0f / 792.0f));
        return;
    }
    field_submission_fixture_t fixture(true);
    INFO("Renderer " << SDL_GetRendererName(fixture.renderer));
    renderer_t cached;
    compare_envelope_frame(fixture, cached, 0);
    for (const auto scale : {756.0f / 792.0f, 1.25f}) {
        compare_scaled_envelope_frame(fixture, cached, 0, scale);
        compare_scaled_envelope_frame(fixture, cached, 0, scale);
        expect_field(cached.envelope_work().regenerated_rows == 0);
        compare_scaled_envelope_frame(fixture, cached, 1, scale);
        expect_field(cached.envelope_work().regenerated_rows == 1);
        compare_scaled_envelope_frame(fixture, cached, 65, scale);
        compare_scaled_envelope_frame(fixture, cached, 66, scale);
        compare_scaled_envelope_frame(fixture, cached, 65.3, scale);
        expect_field(cached.envelope_work().regenerated_rows == 1);
        compare_scaled_envelope_frame(fixture, cached, 65.3, scale);
        expect_field(cached.envelope_work().regenerated_rows == 0);
        compare_scaled_envelope_frame(fixture, cached, 1000, scale);
        for (const auto& vertex : cached.vertices()) {
            expect_field(vertex.tex_coord.y >= 0);
            expect_field(vertex.tex_coord.y <= 1);
        }
    }
}
TEST_CASE("Exact stereo sample segments need fewer than 35000 vertices for 4096 rows", "[ui][field]") {
    renderer_t geometry;
    geometry.begin({0,0,200,4096});
    for (unsigned row = 1; row < 4096; ++row) {
        const auto x = static_cast<float>(row % 2) * 10;
        for (unsigned channel = 0; channel < 2; ++channel)
            geometry.line(6, {30 + x, static_cast<float>(row - 1)},
                             {40 - x, static_cast<float>(row)}, 1, {114,222,239,190});
    }
    expect_field(geometry.workload().vertices == 32760);
    expect_field(geometry.workload().draw_calls == 1);
}
TEST_CASE("SDL target reset recreates waveform rings before drawing current pixels", "[ui][field][sdl]") {
    field_submission_fixture_t fixture;
    auto& cached = wormedit::ui::nuklear::compose_field(fixture.context);
    compare_envelope_frame(fixture, cached, 65);
    compare_envelope_frame(fixture, cached, 65);
    expect_field(cached.envelope_work().regenerated_rows == 0);
    SDL_Event event{};
    event.type = SDL_EVENT_RENDER_TARGETS_RESET;
    event.render.windowID = SDL_GetWindowID(fixture.window);
    expect_field(nk_sdl_handle_event(fixture.context, &event) == 1);
    compare_envelope_frame(fixture, cached, 65);
    expect_field(cached.envelope_work().regenerated_rows == 66);
    event.type = SDL_EVENT_RENDER_DEVICE_RESET;
    expect_field(nk_sdl_handle_event(fixture.context, &event) == 1);
    compare_envelope_frame(fixture, cached, 65);
    expect_field(cached.envelope_work().regenerated_rows == 66);
}

namespace {
constexpr std::size_t pool_texture_bytes = 100U * 66U * 4U;
void begin_stem_pool(field_submission_fixture_t& fixture, renderer_t& pool,
                     std::span<const std::uint64_t> keys) {
    pool.begin({0, 0, 128, 128}, {1, 1}, {}, fixture.renderer);
    pool.pin_stem_envelopes(keys);
}
auto pool_stem(renderer_t& pool, std::uint64_t key, float width = 100) -> bool {
    const auto rows = envelope_test_rows(-2, 68);
    return pool.stem_envelope(key, 6, rows, {14, 0, width, 64}, 0, 1,
                              {{57,137,167,96}, {114,222,239,142}, true});
}
void pool_reserved(renderer_t& pool) {
    const auto rows = envelope_test_rows(-2, 68);
    for (std::size_t slot = 0; slot < 2; ++slot)
        expect_field(pool.envelope(slot, 6, rows, {14, 0, 100, 64}, 0, 1,
                                   {{57,137,167,96}, {114,222,239,142}, true}));
}
auto draw_pool_set(field_submission_fixture_t& fixture, renderer_t& pool,
                    std::span<const std::uint64_t> keys, std::uint64_t observed) -> SDL_Texture* {
    begin_stem_pool(fixture, pool, keys);
    SDL_Texture* texture{};
    for (const auto key : keys) {
        expect_field(pool_stem(pool, key));
        expect_field(pool.batches().back().texture != nullptr);
        if (key == observed) texture = pool.batches().back().texture;
    }
    expect_field(pool.submit(fixture.renderer));
    return texture;
}
}
TEST_CASE("A fitting viewport submits 300 stable stem textures without budget fallbacks", "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool;
    std::vector<std::uint64_t> keys;
    for (std::uint64_t key = 0; key < 300; ++key) keys.push_back(key);
    const auto retained = draw_pool_set(fixture, pool, keys, 150);
    expect_field(retained != nullptr);
    expect_field(pool.envelope_work().cached_stems == 300);
    expect_field(pool.envelope_work().resident_bytes == 300 * pool_texture_bytes);
    expect_field(pool.envelope_work().budget_fallbacks == 0);
    keys.erase(keys.begin());
    keys.push_back(900);
    std::ranges::reverse(keys);
    expect_field(draw_pool_set(fixture, pool, keys, 150) == retained);
    expect_field(pool.envelope_work().regenerated_rows == 66);
    expect_field(pool.envelope_work().budget_fallbacks == 0);
}
TEST_CASE("Stem pool pins the complete visible set before evicting the oldest offscreen texture", "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool{3 * pool_texture_bytes};
    const std::array<std::uint64_t, 3> initial{10, 20, 30};
    const auto retained = draw_pool_set(fixture, pool, initial, 10);
    const std::array<std::uint64_t, 2> shifted{40, 10};
    expect_field(draw_pool_set(fixture, pool, shifted, 10) == retained);
    expect_field(pool.envelope_work().evictions == 1);
    expect_field(pool.envelope_work().resident_bytes == 3 * pool_texture_bytes);
    const std::array<std::uint64_t, 1> oldest_survivor{30};
    (void)draw_pool_set(fixture, pool, oldest_survivor, 30);
    expect_field(pool.envelope_work().regenerated_rows == 0);
    const std::array<std::uint64_t, 1> evicted{20};
    (void)draw_pool_set(fixture, pool, evicted, 20);
    expect_field(pool.envelope_work().regenerated_rows == 66);
    expect_field(pool.envelope_work().evictions == 2);
}
TEST_CASE("Reserved envelopes remain distinct from stem keys zero and one under visible pressure", "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool{3 * pool_texture_bytes};
    const std::array<std::uint64_t, 2> keys{0, 1};
    begin_stem_pool(fixture, pool, keys);
    pool_reserved(pool);
    expect_field(pool_stem(pool, 0));
    expect_field(!pool_stem(pool, 1));
    pool.hline(6, 14, 114, 70, 1, {114,222,239,142});
    expect_field(pool.submit(fixture.renderer));
    expect_field(pool.envelope_work().resident_bytes == 3 * pool_texture_bytes);
    expect_field(pool.envelope_work().budget_fallbacks == 1);
    expect_field(pool.envelope_work().evictions == 0);
    begin_stem_pool(fixture, pool, {});
    pool_reserved(pool);
    expect_field(pool.envelope_work().regenerated_rows == 0);
}
TEST_CASE("Stem texture rejection keeps queued batches and byte accounting coherent", "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool{pool_texture_bytes};
    const std::array<std::uint64_t, 1> keys{42};
    begin_stem_pool(fixture, pool, keys);
    expect_field(pool_stem(pool, 42));
    const auto queued = pool.batches().back().texture;
    expect_field(!pool_stem(pool, 42, 99));
    expect_field(pool.batches().back().texture == queued);
    expect_field(pool.envelope_work().resident_bytes == pool_texture_bytes);
    expect_field(pool.submit(fixture.renderer));
    pool.invalidate_envelopes();
    expect_field(pool.batches().empty());
    expect_field(pool.envelope_work().resident_bytes == 0);
    begin_stem_pool(fixture, pool, keys);
    expect_field(!pool_stem(pool, 42, 0));
    expect_field(pool.envelope_work().budget_fallbacks == 0);
}

TEST_CASE("Stem texture replacements reserve transient bytes before allocating under pressure", "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool{2 * pool_texture_bytes};
    const std::array<std::uint64_t, 2> both{42, 43};
    const auto previous = draw_pool_set(fixture, pool, both, 42);
    begin_stem_pool(fixture, pool, both);
    expect_field(!pool_stem(pool, 42, 99));
    expect_field(pool.envelope_work().resident_bytes == 2 * pool_texture_bytes);
    expect_field(pool.envelope_work().budget_fallbacks == 1);
    expect_field(pool_stem(pool, 42));
    expect_field(pool.batches().back().texture == previous);
    expect_field(pool_stem(pool, 43));
    expect_field(pool.submit(fixture.renderer));
    const std::array<std::uint64_t, 1> visible{42};
    begin_stem_pool(fixture, pool, visible);
    expect_field(pool_stem(pool, 42, 99));
    expect_field(pool.batches().back().texture != previous);
    expect_field(pool.envelope_work().evictions == 1);
    expect_field(pool.envelope_work().resident_bytes == 99U * 66U * 4U);
    expect_field(pool.envelope_work().peak_reserved_bytes <= 2 * pool_texture_bytes);
    expect_field(pool.submit(fixture.renderer));
}

TEST_CASE("A newly selected reserved envelope admits before a full visible stem pool",
          "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool{2 * pool_texture_bytes};
    const std::array<std::uint64_t, 2> keys{10, 20};
    (void)draw_pool_set(fixture, pool, keys, 10);
    begin_stem_pool(fixture, pool, keys);
    expect_field(pool.reserve_envelope(1, {14, 0, 100, 64}));
    expect_field(pool.batches().empty());
    expect_field(pool.envelope_work().budget_fallbacks == 0);
    expect_field(pool_stem(pool, 20));
    const auto stem_texture = pool.batches().back().texture;
    expect_field(!pool_stem(pool, 10));
    const auto rows = envelope_test_rows(-2, 68);
    expect_field(pool.envelope(1, 6, rows, {14, 0, 100, 64}, 0, 1,
                               {{57,137,167,96}, {114,222,239,142}, true}));
    expect_field(pool.batches().back().texture != stem_texture);
    expect_field(pool.envelope_work().budget_fallbacks == 1);
    expect_field(pool.envelope_work().resident_bytes == 2 * pool_texture_bytes);
    expect_field(pool.envelope_work().peak_reserved_bytes <= 2 * pool_texture_bytes);
    expect_field(pool.submit(fixture.renderer));
}

TEST_CASE("Reserved preflight preserves the selected waveform overlay on a shared lane",
          "[ui][field][stem-pool]") {
    field_submission_fixture_t fixture;
    renderer_t pool;
    const std::array<std::uint64_t, 1> keys{10};
    begin_stem_pool(fixture, pool, keys);
    const SDL_FRect bounds{14, 0, 100, 64};
    expect_field(pool.reserve_envelope(1, bounds));
    const auto rows = envelope_test_rows(-2, 68);
    expect_field(pool.stem_envelope(10, 6, rows, bounds, 0, 1,
        {{255,0,0,255}, {255,0,0,255}, true}));
    expect_field(pool.envelope(1, 6, rows, bounds, 0, 1,
        {{0,255,0,255}, {0,255,0,255}, true}));
    expect_field(pool.submit(fixture.renderer));
    expect_field(SDL_RenderPresent(fixture.renderer));
    const auto selected = fixture.pixel(64, 30);
    expect_field(selected.g == 255 && selected.r == 0);
}

namespace {
struct t469_frame_sample_t {
    std::uint64_t collect_nanoseconds{}, submit_nanoseconds{};
    envelope_work_t work;
};

auto submit_t469_benchmark_frame(field_submission_fixture_t& fixture, renderer_t& pool,
    std::span<const std::uint64_t> keys, std::span<const envelope_row_t> rows,
    std::uint64_t generation) -> std::optional<t469_frame_sample_t> {
    const auto collect_start = std::chrono::steady_clock::now();
    pool.begin({0, 0, 128, 128}, {1, 1}, {}, fixture.renderer);
    pool.pin_stem_envelopes(keys);
    for (const auto key : keys) {
        if (!pool.stem_envelope(key, 6, rows, {14, 0, 100, 128}, 0, generation,
            {{57,137,167,96}, {114,222,239,142}, true})) return std::nullopt;
    }
    const auto collect_end = std::chrono::steady_clock::now();
    const auto submit_start = std::chrono::steady_clock::now();
    if (!pool.submit(fixture.renderer)) return std::nullopt;
    const auto submit_end = std::chrono::steady_clock::now();
    if (!SDL_RenderPresent(fixture.renderer)) return std::nullopt;
    const auto work = pool.envelope_work();
    return t469_frame_sample_t{
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            collect_end - collect_start).count()),
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            submit_end - submit_start).count()), work};
}

auto t469_percentile(std::vector<std::uint64_t> samples, std::size_t percentile)
    -> std::uint64_t {
    std::ranges::sort(samples);
    const auto rank = (percentile * samples.size() + 99) / 100;
    return samples[std::max<std::size_t>(rank, 1) - 1];
}

auto t469_maximum(std::span<const std::uint64_t> samples) -> std::uint64_t {
    return *std::ranges::max_element(samples);
}

auto valid_t469_frame(const t469_frame_sample_t& frame, std::size_t stem_count) -> bool {
    return frame.work.cached_stems == stem_count && frame.work.composites >= stem_count &&
        frame.work.budget_fallbacks == 0;
}

auto report_t469_benchmark(std::span<const t469_frame_sample_t> samples,
                           std::size_t first_settled_rows,
                           std::size_t repeated_settled_rows,
                           std::size_t resident_bytes) -> void {
    std::vector<std::uint64_t> collect, submit, complete;
    collect.reserve(samples.size());
    submit.reserve(samples.size());
    complete.reserve(samples.size());
    for (const auto& frame : samples) {
        collect.push_back(frame.collect_nanoseconds);
        submit.push_back(frame.submit_nanoseconds);
        complete.push_back(frame.collect_nanoseconds + frame.submit_nanoseconds);
    }
    std::printf("T469 300-stem 128-row workload frames=%zu collect_ns_p50=%llu collect_ns_p99=%llu collect_ns_max=%llu submit_ns_p50=%llu submit_ns_p99=%llu submit_ns_max=%llu total_ns_p50=%llu total_ns_p99=%llu total_ns_max=%llu first_settled_rows=%zu repeated_settled_rows=%zu resident_bytes=%zu\n",
        samples.size(), static_cast<unsigned long long>(t469_percentile(collect, 50)),
        static_cast<unsigned long long>(t469_percentile(collect, 99)),
        static_cast<unsigned long long>(t469_maximum(collect)),
        static_cast<unsigned long long>(t469_percentile(submit, 50)),
        static_cast<unsigned long long>(t469_percentile(submit, 99)),
        static_cast<unsigned long long>(t469_maximum(submit)),
        static_cast<unsigned long long>(t469_percentile(complete, 50)),
        static_cast<unsigned long long>(t469_percentile(complete, 99)),
        static_cast<unsigned long long>(t469_maximum(complete)), first_settled_rows,
        repeated_settled_rows, resident_bytes);
}

auto t469_benchmark_succeeds_and_reports_metrics() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool;
    std::vector<std::uint64_t> keys;
    keys.reserve(300);
    for (std::uint64_t key = 1; key <= 300; ++key) keys.push_back(key);
    const auto rows = envelope_test_rows(-2, 132);
    const auto warm = submit_t469_benchmark_frame(fixture, pool, keys, rows, 0);
    if (!warm || !valid_t469_frame(*warm, keys.size()) ||
        warm->work.regenerated_rows != keys.size() * 130) return false;
    std::vector<t469_frame_sample_t> samples;
    samples.reserve(122);
    for (std::uint64_t frame = 0; frame < 120; ++frame) {
        const auto current = submit_t469_benchmark_frame(fixture, pool, keys, rows, frame + 1);
        if (!current || !valid_t469_frame(*current, keys.size())) return false;
        samples.push_back(*current);
    }
    const auto first_settled = submit_t469_benchmark_frame(fixture, pool, keys, rows, 120);
    if (!first_settled || !valid_t469_frame(*first_settled, keys.size())) return false;
    samples.push_back(*first_settled);
    const auto repeated_settled = submit_t469_benchmark_frame(fixture, pool, keys, rows, 120);
    if (!repeated_settled || !valid_t469_frame(*repeated_settled, keys.size())) return false;
    samples.push_back(*repeated_settled);
    const auto first_regenerated = first_settled->work.regenerated_rows;
    const auto repeated_regenerated = repeated_settled->work.regenerated_rows;
    const auto final_work = repeated_settled->work;
    report_t469_benchmark(samples, first_regenerated, repeated_regenerated,
                          final_work.resident_bytes);
    return samples.size() == 122 && first_regenerated == 0 &&
        repeated_regenerated == 0 && final_work.cached_stems == keys.size() &&
        valid_t469_frame(*repeated_settled, keys.size()) &&
        final_work.resident_bytes == keys.size() * 100U * 130U * 4U;
}
} // namespace

TEST_CASE("T469 cached stem envelope zoom and settle workload reports collect and submit costs",
          "[.performance][ui][field][T469]") {
    expect_field(t469_benchmark_succeeds_and_reports_metrics());
}

namespace {
struct t469_zoom_frame_t {
    envelope_work_t work;
    std::vector<std::uint64_t> refilled_keys;
    bool submitted{};
};

auto t469_test_style() -> envelope_style_t {
    return {{25, 80, 180, 255}, {240, 45, 30, 255}, true};
}

auto t469_zoom_rows(std::uint64_t key, bool changed) -> std::vector<envelope_row_t> {
    std::vector<envelope_row_t> rows;
    rows.reserve(132);
    for (int world_y = -2; world_y < 130; ++world_y) {
        const auto phase = static_cast<float>((key * 7U + static_cast<std::uint64_t>(world_y + 3)) % 19U);
        const auto peak = world_y == 80 ? 1.0f : 0.12f + phase / 30.0f;
        const auto adjusted = changed && world_y != 80 ? 0.9f - peak / 2.0f : peak;
        const auto rms = changed ? 0.1f + phase / 25.0f : adjusted / 3.0f;
        rows.push_back({static_cast<double>(world_y), {rms, rms},
                        {adjusted, adjusted}});
    }
    return rows;
}

auto t469_draw_zoom_frame(field_submission_fixture_t& fixture, renderer_t& pool,
                          std::span<const std::uint64_t> keys, double zoom, double origin,
                          SDL_FRect bounds, std::uint64_t generation, std::size_t budget,
                          bool changed = false,
                          envelope_style_t style = t469_test_style(),
                          double row_origin = 0) -> t469_zoom_frame_t {
    expect_field(SDL_SetRenderDrawColor(fixture.renderer, 0, 0, 0, 255));
    expect_field(SDL_RenderClear(fixture.renderer));
    pool.begin({0, 0, 128, 128}, {1, 1}, {}, fixture.renderer);
    pool.pin_stem_envelopes(keys);
    pool.prepare_stem_zoom(zoom, keys, budget);
    t469_zoom_frame_t result;
    for (const auto key : keys) {
        const auto before = pool.envelope_work().collected_stem_rows;
        auto rows = t469_zoom_rows(key, changed);
        for (auto& row : rows) row.world_y += row_origin;
        if (!pool.stem_envelope(key, 6, rows, bounds, origin, generation, style))
            return result;
        if (pool.envelope_work().collected_stem_rows > before)
            result.refilled_keys.push_back(key);
    }
    result.submitted = pool.submit(fixture.renderer) && SDL_RenderPresent(fixture.renderer);
    result.work = pool.envelope_work();
    return result;
}

auto t469_capture_surface(const field_submission_fixture_t& fixture) -> std::vector<SDL_Color> {
    std::vector<SDL_Color> pixels;
    pixels.reserve(128U * 128U);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) pixels.push_back(fixture.pixel(x, y));
    return pixels;
}

auto t469_surface_matches(const field_submission_fixture_t& fixture,
                          std::span<const SDL_Color> expected) -> bool {
    if (expected.size() != 128U * 128U) return false;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const auto& a = expected[static_cast<std::size_t>(y) * 128U + x];
            const auto b = fixture.pixel(x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) return false;
        }
    return true;
}

auto t469_zoom_keys() -> std::array<std::uint64_t, 12> {
    return {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
}

auto t469_refill_budget_and_settle_pixels() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool, reference;
    const auto keys = t469_zoom_keys();
    const SDL_FRect bounds{14, 0, 100, 128};
    const auto initial = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, bounds, 1, 3);
    const auto zoom_two = t469_draw_zoom_frame(fixture, pool, keys, 2, 0, bounds, 1, 3);
    const auto zoom_four = t469_draw_zoom_frame(fixture, pool, keys, 4, 0, bounds, 1, 3);
    const auto first_stable = t469_draw_zoom_frame(fixture, pool, keys, 4, 0, bounds, 1, 3);
    const auto repeated = t469_draw_zoom_frame(fixture, pool, keys, 4, 0, bounds, 1, 3);
    const auto expected_first = std::array<std::uint64_t, 3>{1, 2, 3};
    const auto expected_second = std::array<std::uint64_t, 3>{4, 5, 6};
    if (!initial.submitted || !zoom_two.submitted || !zoom_four.submitted ||
        !first_stable.submitted || !repeated.submitted) return false;
    if (initial.work.regenerated_rows != 12U * 130U ||
        !std::ranges::equal(zoom_two.refilled_keys, expected_first) ||
        !std::ranges::equal(zoom_four.refilled_keys, expected_second)) return false;
    if (zoom_two.work.refilled_stems != 3 || zoom_two.work.deferred_stems != 9 ||
        zoom_four.work.refilled_stems != 3 || zoom_four.work.deferred_stems != 9 ||
        zoom_two.work.composites < 12 || zoom_four.work.composites < 12 ||
        zoom_two.work.budget_fallbacks != 0 || zoom_four.work.budget_fallbacks != 0) return false;
    if (first_stable.work.regenerated_rows != 9U * 130U ||
        first_stable.work.refilled_stems != 9 || repeated.work.regenerated_rows != 0) return false;
    const auto actual_pixels = t469_capture_surface(fixture);
    const auto expected = t469_draw_zoom_frame(fixture, reference, keys, 4, 0,
                                                bounds, 1, keys.size());
    return expected.submitted && t469_surface_matches(fixture, actual_pixels);
}

auto t469_same_revision_rows_match_reference() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool, reference;
    const auto keys = t469_zoom_keys();
    const SDL_FRect bounds{14, 0, 100, 128};
    const auto initial = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, bounds, 1, 3);
    const auto zoomed = t469_draw_zoom_frame(fixture, pool, keys, 2, 0, bounds, 1, 3);
    const auto updated = t469_draw_zoom_frame(fixture, pool, keys, 2, 0, bounds, 1, 3, true);
    const auto actual_pixels = t469_capture_surface(fixture);
    const auto expected = t469_draw_zoom_frame(fixture, reference, keys, 2, 0,
                                                bounds, 1, keys.size(), true);
    return initial.submitted && zoomed.submitted && updated.submitted && expected.submitted &&
        updated.work.regenerated_rows == keys.size() * 130U &&
        t469_surface_matches(fixture, actual_pixels);
}

auto t469_retained_gap_fallbacks_survive_scroll_and_zoom_toggles() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool;
    const auto keys = t469_zoom_keys();
    const SDL_FRect bounds{14, 0, 100, 128};
    const auto initial = t469_draw_zoom_frame(fixture, pool, keys, 2, 0, bounds, 1, 0);
    const auto zoom_out = t469_draw_zoom_frame(fixture, pool, keys, 0.5, -8, bounds, 1, 0);
    if (!initial.submitted || !zoom_out.submitted || zoom_out.work.composites < keys.size() ||
        zoom_out.work.stem_fallbacks == 0 || zoom_out.work.budget_fallbacks != 0) return false;
    const auto gap_peak = fixture.pixel(64, 88);
    const auto downscroll = t469_draw_zoom_frame(fixture, pool, keys, 2, 20, bounds, 1, 2);
    const auto toggled = t469_draw_zoom_frame(fixture, pool, keys, 0.5, -8.5, bounds, 1, 2);
    return gap_peak.r > 200 && gap_peak.g < 100 && downscroll.submitted && toggled.submitted &&
        downscroll.work.composites >= keys.size() && toggled.work.composites >= keys.size();
}

auto t469_canonical_downscroll_keeps_gap_pixels() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool, reference;
    const auto keys = t469_zoom_keys();
    const SDL_FRect bounds{14, 0, 100, 128};
    // Canvas downscroll origin = -scroll - viewport height.
    constexpr double origin = -40.0 - 128.0;
    const auto initial = t469_draw_zoom_frame(fixture, pool, keys, 2, origin, bounds, 1, 0, false, t469_test_style(), origin);
    const auto changed = t469_draw_zoom_frame(fixture, pool, keys, .5, origin, bounds, 1, 0, false, t469_test_style(), origin);
    const auto gap = fixture.pixel(64, 80);
    const auto settled = t469_draw_zoom_frame(fixture, pool, keys, .5, origin, bounds, 1, 0, false, t469_test_style(), origin);
    const auto pixels = t469_capture_surface(fixture);
    const auto exact = t469_draw_zoom_frame(fixture, reference, keys, .5, origin,
        bounds, 1, keys.size(), false, t469_test_style(), origin);
    return initial.submitted && changed.submitted && settled.submitted && exact.submitted &&
        changed.work.refilled_stems == 0 && changed.work.deferred_stems == keys.size() &&
        changed.work.stem_fallbacks == keys.size() && gap.r > 200 && gap.g < 100 &&
        settled.work.refilled_stems == keys.size() && t469_surface_matches(fixture, pixels);
}

auto t469_incompatible_stem_identity_refills() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool;
    const auto keys = t469_zoom_keys();
    const SDL_FRect bounds{14, 0, 100, 128};
    const auto initial = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, bounds, 1, keys.size());
    const auto generation = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, bounds, 2, keys.size());
    const envelope_style_t alternate_style{{35, 200, 80, 255}, {220, 220, 20, 255}, true};
    const auto style_change = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, bounds, 2,
                                                    keys.size(), false, alternate_style);
    const SDL_FRect wider{14, 0, 101, 128};
    const auto width_change = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, wider, 2,
                                                   keys.size());
    const SDL_FRect subpixel{14.25f, 0, 101, 128};
    const auto x_change = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, subpixel, 2,
                                               keys.size());
    return initial.submitted && generation.submitted && style_change.submitted &&
        width_change.submitted && x_change.submitted &&
        generation.work.regenerated_rows == keys.size() * 130U &&
        style_change.work.regenerated_rows == keys.size() * 130U &&
        width_change.work.regenerated_rows > 0 && x_change.work.regenerated_rows > 0;
}

auto t469_invalid_zoom_and_missing_stem_fallbacks() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool;
    const std::array<std::uint64_t, 1> known{1};
    const SDL_FRect bounds{14, 0, 100, 128};
    const auto initial = t469_draw_zoom_frame(fixture, pool, known, 1, 0, bounds, 1, 1);
    const auto invalid = t469_draw_zoom_frame(fixture, pool, known, 0, 0, bounds, 1, 1);
    const auto rearmed = t469_draw_zoom_frame(fixture, pool, known, 1, 0, bounds, 1, 1);
    if (!initial.submitted || !invalid.submitted || !rearmed.submitted ||
        invalid.work.budget_fallbacks != 0)
        return false;

    const std::array<std::uint64_t, 2> with_missing{1, 99};
    expect_field(SDL_SetRenderDrawColor(fixture.renderer, 0, 0, 0, 255));
    expect_field(SDL_RenderClear(fixture.renderer));
    pool.begin({0, 0, 128, 128}, {1, 1}, {}, fixture.renderer);
    pool.pin_stem_envelopes(with_missing);
    pool.prepare_stem_zoom(2, with_missing, 0);
    const auto rows = t469_zoom_rows(1, false);
    if (pool.stem_envelope(1, 6, {}, bounds, 0, 1, t469_test_style())) return false;
    if (!pool.stem_envelope(1, 6, rows, bounds, 0, 1, t469_test_style())) return false;
    if (pool.stem_envelope(99, 6, rows, bounds, 0, 1, t469_test_style())) return false;
    pool.hline(6, 20, 108, 50, 1, {255, 0, 0, 255});
    if (!pool.submit(fixture.renderer) || !SDL_RenderPresent(fixture.renderer)) return false;
    const auto work = pool.envelope_work();
    const auto fallback_pixel = fixture.pixel(64, 50);

    pool.begin({0, 0, 128, 128}, {1, 1}, {}, fixture.renderer);
    pool.prepare_stem_zoom(std::numeric_limits<double>::quiet_NaN(), with_missing, 0);
    const auto bytes_before = pool.envelope_work().resident_bytes;
    const auto rejected = pool.stem_envelope(100, 6, rows, {14, 0, 0, 128}, 0, 1,
                                             t469_test_style());
    return !rejected && work.stem_fallbacks > 0 && work.budget_fallbacks == 0 &&
        fallback_pixel.r > 200 && fallback_pixel.g < 100 &&
        pool.envelope_work().resident_bytes == bytes_before;
}

auto t469_incompatible_retained_textures_are_rejected() -> bool {
    field_submission_fixture_t fixture;
    renderer_t pool;
    const std::array<std::uint64_t, 4> keys{1, 2, 3, 4};
    const SDL_FRect bounds{14, 0, 100, 128};
    const auto initial = t469_draw_zoom_frame(fixture, pool, keys, 1, 0, bounds, 1, 4);
    if (!initial.submitted) return false;
    const auto resident_before = initial.work.resident_bytes;
    expect_field(SDL_SetRenderDrawColor(fixture.renderer, 0, 0, 0, 255));
    expect_field(SDL_RenderClear(fixture.renderer));
    pool.begin({0, 0, 128, 128}, {1, 1}, {}, fixture.renderer);
    pool.pin_stem_envelopes(keys);
    pool.prepare_stem_zoom(2, keys, 0);
    const auto rows = t469_zoom_rows(1, false);
    const envelope_style_t alternate{{35, 200, 80, 255}, {220, 220, 20, 255}, true};
    const SDL_FRect wider{14, 0, 101, 128};
    const SDL_FRect subpixel{14.25f, 0, 100, 128};
    const auto generation_reused = pool.stem_envelope(1, 6, rows, bounds, 0, 2,
                                                       t469_test_style());
    const auto style_reused = pool.stem_envelope(2, 6, rows, bounds, 0, 1, alternate);
    const auto width_reused = pool.stem_envelope(3, 6, rows, wider, 0, 1,
                                                  t469_test_style());
    const auto subpixel_reused = pool.stem_envelope(4, 6, rows, subpixel, 0, 1,
                                                     t469_test_style());
    const auto work = pool.envelope_work();
    return !generation_reused && !style_reused && !width_reused && !subpixel_reused &&
        work.stem_fallbacks == 4 && work.composites == 0 && work.budget_fallbacks == 0 &&
        work.resident_bytes == resident_before;
}
} // namespace

TEST_CASE("T469 stem zoom applies a rotating refill budget and settles to reference pixels",
          "[ui][field][stem][T469]") {
    expect_field(t469_refill_budget_and_settle_pixels());
}

TEST_CASE("T469 stem zoom refreshes changed same-revision rows against reference pixels",
          "[ui][field][stem][T469]") {
    expect_field(t469_same_revision_rows_match_reference());
}

TEST_CASE("T469 stem zoom retains transformed textures and draws gaps through scroll toggles",
          "[ui][field][stem][T469]") {
    expect_field(t469_retained_gap_fallbacks_survive_scroll_and_zoom_toggles());
    expect_field(t469_canonical_downscroll_keeps_gap_pixels());
}

TEST_CASE("T469 stem zoom invalidates textures after generation and layout identity changes",
          "[ui][field][stem][T469]") {
    expect_field(t469_incompatible_stem_identity_refills());
}

TEST_CASE("T469 stem zoom rejects invalid geometry and counts missing texture line fallbacks",
          "[ui][field][stem][T469]") {
    expect_field(t469_invalid_zoom_and_missing_stem_fallbacks());
}

TEST_CASE("T469 stem zoom rejects incompatible retained texture identities",
          "[ui][field][stem][T469]") {
    expect_field(t469_incompatible_retained_textures_are_rejected());
}
