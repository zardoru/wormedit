#include "ui/nuklear/render/sdl_region_cache.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <limits>
#include <memory>
#include <source_location>
#include <vector>

namespace {
thread_local bool count_region_allocations{};
thread_local std::size_t region_allocations{};
}
void* operator new(std::size_t size) {
    if (count_region_allocations) ++region_allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

using namespace wormedit::ui::nuklear;
namespace {
void expect_region(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Region expectation at " << at.line());
    REQUIRE(condition);
}
void expect_pixels(const std::vector<unsigned char>& actual, const std::vector<unsigned char>& expected,
    int pitch) {
    unsigned shown{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual[index] == expected[index]) continue;
        if (shown++ < 10) std::fprintf(stderr,"pixel x=%zu y=%zu component=%zu actual=%u expected=%u\n",
            (index%pitch)/4,index/pitch,index%4,actual[index],expected[index]);
    }
    expect_region(actual == expected);
}
struct region_fixture_t {
    SDL_Surface* surface = SDL_CreateSurface(192, 1600, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    std::unique_ptr<sdl_region_cache_t> cache = std::make_unique<sdl_region_cache_t>();
    std::array<sdl_command_vertex_t, 8> vertices{{
        {{20,30},{0,0},{1,.2f,.1f,.5f}}, {{136,30},{1,0},{1,.2f,.1f,.5f}},
        {{136,600},{1,1},{1,.2f,.1f,.5f}}, {{20,600},{0,1},{1,.2f,.1f,.5f}},
        {{30,40},{0,0},{.1f,.3f,1,.25f}}, {{140,40},{1,0},{.1f,.3f,1,.25f}},
        {{140,610},{1,1},{.1f,.3f,1,.25f}}, {{30,610},{0,1},{.1f,.3f,1,.25f}}}};
    std::array<sdl_command_index_t, 12> indices{0,1,2,0,2,3,4,5,6,4,6,7};
    std::array<sdl_command_batch_t, 2> batches{{
        {nullptr,{16,24,128,600},0,6}, {nullptr,{50,60,50,500},6,6}}};
    sdl_region_request_t request{.slot=0,.mesh={vertices,indices,batches,{}},.opaque_background={37,61,83,255}};
    region_fixture_t() { expect_region(surface && renderer); SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND); }
    ~region_fixture_t() { cache.reset(); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); }
    void begin(std::uint64_t epoch = 1) { cache->begin(renderer,{0,0,192,1600},epoch); }
    void clear() {
        SDL_SetRenderClipRect(renderer,nullptr);
        SDL_SetRenderDrawColor(renderer,9,11,13,255);
        SDL_RenderClear(renderer);
    }
    void direct() {
        begin();
        const auto bounds = cache->bounds_for(request.mesh,request.scale);
        const SDL_FRect fill{static_cast<float>(bounds.x),static_cast<float>(bounds.y),
            static_cast<float>(bounds.w),static_cast<float>(bounds.h)};
        SDL_SetRenderScale(renderer,1,1);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer,37,61,83,255);
        SDL_RenderFillRect(renderer,&fill);
        SDL_SetRenderScale(renderer,request.scale.x,request.scale.y);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        for (const auto& batch : batches) {
            SDL_SetRenderClipRect(renderer,&batch.clip);
            SDL_RenderGeometryRaw(renderer,batch.texture,vertices[0].position,sizeof(sdl_command_vertex_t),
                reinterpret_cast<const SDL_FColor*>(vertices[0].col),sizeof(sdl_command_vertex_t),
                vertices[0].uv,sizeof(sdl_command_vertex_t),vertices.size(),
                indices.data()+batch.first_index,batch.index_count,sizeof(sdl_command_index_t));
        }
        SDL_SetRenderClipRect(renderer,nullptr);
        SDL_RenderPresent(renderer);
    }
    auto pixels() -> std::vector<unsigned char> {
        SDL_RenderPresent(renderer);
        const auto* begin = static_cast<const unsigned char*>(surface->pixels);
        return {begin,begin+surface->pitch*surface->h};
    }
    auto warm() -> bool {
        for (unsigned frame = 0; frame < 12; ++frame) {
            begin();
            if (cache->prepare(request)) return true;
        }
        return false;
    }
};
}

TEST_CASE("Opaque SDL regions retain exact alpha triangles and batch scissors across tiles", "[ui][region-cache]") {
    region_fixture_t fixture;
    fixture.clear(); fixture.direct();
    const auto expected = fixture.pixels();
    fixture.clear();
    expect_region(fixture.warm());
    expect_region(fixture.cache->submit(0));
    INFO("retained CPU bytes=" << fixture.cache->work().retained_cpu_bytes
        << " retained tile bytes=" << fixture.cache->work().tiles.retained_bytes);
    expect_pixels(fixture.pixels(),expected,fixture.surface->pitch);
    fixture.begin();
    expect_region(fixture.cache->prepare(fixture.request));
    expect_region(fixture.cache->work().tiles.prepared == 0 && fixture.cache->work().tiles.composites == 2);
}

TEST_CASE("Opaque SDL regions preserve fractional software rasterization through direct fallback", "[ui][region-cache]") {
    region_fixture_t fixture;
    fixture.request.scale = {1.25f,1.25f};
    fixture.clear(); fixture.direct();
    const auto expected = fixture.pixels();
    fixture.clear();
    fixture.begin();
    expect_region(!fixture.cache->prepare(fixture.request) && !fixture.cache->submit(0));
    fixture.direct();
    expect_pixels(fixture.pixels(),expected,fixture.surface->pitch);
}

TEST_CASE("Opaque SDL regions never submit partial readiness or stale epochs", "[ui][region-cache]") {
    region_fixture_t fixture;
    fixture.vertices[2].position[1] = fixture.vertices[3].position[1] = 1500;
    fixture.batches[0].clip.h = 1300;
    fixture.begin();
    expect_region(!fixture.cache->prepare(fixture.request));
    expect_region(fixture.cache->work().tiles.prepared == 2);
    expect_region(!fixture.cache->submit(0));
    expect_region(fixture.warm());
    fixture.begin(2);
    expect_region(!fixture.cache->submit(0));
}

TEST_CASE("Opaque SDL regions reject unsupported textures transparent backgrounds and nonfinite mesh", "[ui][region-cache]") {
    region_fixture_t fixture;
    fixture.begin();
    fixture.batches[0].texture = reinterpret_cast<SDL_Texture*>(std::uintptr_t{1});
    expect_region(!fixture.cache->prepare(fixture.request));
    fixture.batches[0].texture = nullptr;
    fixture.request.opaque_background.a = 200;
    expect_region(!fixture.cache->prepare(fixture.request));
    fixture.request.opaque_background.a = 255;
    fixture.vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    expect_region(!fixture.cache->prepare(fixture.request));
    expect_region(fixture.cache->work().tiles.retained_bytes == 0);
}

TEST_CASE("Opaque SDL regions invalidate exact content style scale and texture epochs", "[ui][region-cache]") {
    region_fixture_t fixture;
    expect_region(fixture.warm());
    fixture.vertices[0].col[0] = .7f;
    fixture.begin();
    (void)fixture.cache->prepare(fixture.request);
    expect_region(fixture.cache->work().tiles.prepared == 0); // Pass-one warmup rotates.
    expect_region(!fixture.cache->submit(0));
    expect_region(fixture.warm());
    fixture.request.texture_epoch = 2;
    fixture.begin();
    const bool refreshed = fixture.cache->prepare(fixture.request);
    expect_region(!refreshed || fixture.cache->work().tiles.prepared == 2);
}

TEST_CASE("Opaque SDL regions restore clip scale color blend and reject unsupported viewport", "[ui][region-cache]") {
    region_fixture_t fixture;
    SDL_SetRenderScale(fixture.renderer,2,2);
    fixture.request.scale = {2,2};
    for (auto& vertex : fixture.vertices) { vertex.position[0] /= 2; vertex.position[1] /= 2; }
    for (auto& batch : fixture.batches) {
        batch.clip.x /= 2; batch.clip.y /= 2; batch.clip.w /= 2; batch.clip.h /= 2;
    }
    const SDL_Rect clip{2,3,90,100};
    SDL_SetRenderClipRect(fixture.renderer,&clip);
    SDL_SetRenderDrawColor(fixture.renderer,7,8,9,10);
    SDL_SetRenderDrawBlendMode(fixture.renderer,SDL_BLENDMODE_BLEND);
    expect_region(fixture.warm());
    SDL_Rect restored{}; float x{},y{}; SDL_BlendMode blend{}; Uint8 r{},g{},b{},a{};
    SDL_GetRenderClipRect(fixture.renderer,&restored); SDL_GetRenderScale(fixture.renderer,&x,&y);
    SDL_GetRenderDrawBlendMode(fixture.renderer,&blend); SDL_GetRenderDrawColor(fixture.renderer,&r,&g,&b,&a);
    expect_region(restored.x == 2 && restored.y == 3 && restored.w == 90 && restored.h == 100);
    expect_region(x == 2 && y == 2 && blend == SDL_BLENDMODE_BLEND && r == 7 && a == 10);
    const SDL_Rect viewport{1,2,100,100};
    SDL_SetRenderViewport(fixture.renderer,&viewport);
    expect_region(!fixture.cache->prepare(fixture.request));
}

TEST_CASE("Opaque SDL region warm reuse makes no C++ heap allocations", "[ui][region-cache]") {
    region_fixture_t fixture;
    expect_region(fixture.warm());
    const auto before = region_allocations;
    count_region_allocations = true;
    fixture.begin();
    const bool ready = fixture.cache->prepare(fixture.request);
    count_region_allocations = false;
    expect_region(ready && region_allocations == before);
    expect_region(fixture.cache->work().retained_cpu_bytes <= sdl_region_cache_t::inner_cpu_reservation);
}

TEST_CASE("Opaque SDL region preparation obeys the global remaining allowance", "[ui][region-cache]") {
    region_fixture_t fixture;
    fixture.cache->begin(fixture.renderer,{0,0,192,1600},1,0);
    expect_region(!fixture.cache->prepare(fixture.request));
    expect_region(!fixture.cache->submit(0) && fixture.cache->work().tiles.prepared == 0);
    expect_region(fixture.cache->work().tiles.retained_bytes == 0);
    expect_region(fixture.warm());
    expect_region(fixture.cache->work().tiles.retained_bytes <=
        wormedit::ui::field::chrome_cache_reservation*3/4);
}

TEST_CASE("Opaque SDL regions retain immutable font texture geometry and refresh upload epochs", "[ui][region-cache]") {
    region_fixture_t fixture;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> font(
        SDL_CreateTexture(fixture.renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,1,1),SDL_DestroyTexture);
    expect_region(font != nullptr);
    const std::array<unsigned char,4> white{255,255,255,255};
    expect_region(SDL_UpdateTexture(font.get(),nullptr,white.data(),4) &&
        SDL_SetTextureBlendMode(font.get(),SDL_BLENDMODE_BLEND));
    fixture.request.immutable_texture = font.get();
    for (auto& batch : fixture.batches) batch.texture = font.get();
    fixture.clear(); fixture.direct();
    const auto expected = fixture.pixels();
    fixture.clear();
    expect_region(fixture.warm() && fixture.cache->submit(0));
    expect_pixels(fixture.pixels(),expected,fixture.surface->pitch);
    ++fixture.request.texture_epoch;
    expect_region(fixture.warm() && fixture.cache->work().tiles.prepared == 2);
}

TEST_CASE("Opaque SDL region unsupported targets preserve state and keep direct fallback available", "[ui][region-cache]") {
    region_fixture_t fixture;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> target(
        SDL_CreateTexture(fixture.renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,8,8),SDL_DestroyTexture);
    expect_region(target && SDL_SetRenderTarget(fixture.renderer,target.get()));
    fixture.begin();
    expect_region(!fixture.cache->prepare(fixture.request) && !fixture.cache->submit(0));
    expect_region(SDL_GetRenderTarget(fixture.renderer) == target.get());
    expect_region(SDL_SetRenderTarget(fixture.renderer,nullptr));
    expect_region(fixture.warm());
}

namespace {
thread_local unsigned region_geometry_attempts{};
auto fail_first_region_geometry(SDL_Renderer* renderer, SDL_Texture* texture,
    const float* xy, int xy_stride, const SDL_FColor* color, int color_stride,
    const float* uv, int uv_stride, int vertex_count, const void* indices, int index_count,
    int index_size) -> bool {
    if (region_geometry_attempts++ == 0) return false;
    return SDL_RenderGeometryRaw(renderer,texture,xy,xy_stride,color,color_stride,uv,uv_stride,
        vertex_count,indices,index_count,index_size);
}
}
TEST_CASE("Opaque SDL region submission failure repaints the parent before exact direct replay", "[ui][region-cache]") {
    region_fixture_t fixture;
    fixture.clear(); fixture.direct();
    const auto expected = fixture.pixels();
    fixture.clear();
    expect_region(fixture.warm());
    region_geometry_attempts = 0;
    expect_region(fixture.cache->submit(0,{nullptr,fail_first_region_geometry}));
    expect_pixels(fixture.pixels(),expected,fixture.surface->pitch);
    const auto work = fixture.cache->work().submitted;
    expect_region(region_geometry_attempts == 5 && work.draw_calls == 5);
    expect_region(work.vertices == 20 && work.indices == 30);
}

TEST_CASE("Opaque SDL region immutable image snapshots admit icons and invalidate reused pointers", "[ui][region-cache]") {
    region_fixture_t fixture;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> icon(
        SDL_CreateTexture(fixture.renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,1,1),SDL_DestroyTexture);
    expect_region(icon != nullptr);
    const std::array<unsigned char,4> white{255,255,255,255};
    expect_region(SDL_UpdateTexture(icon.get(),nullptr,white.data(),4) &&
        SDL_SetTextureBlendMode(icon.get(),SDL_BLENDMODE_BLEND));
    for (auto& batch : fixture.batches) batch.texture = icon.get();
    fixture.begin();
    expect_region(!fixture.cache->prepare(fixture.request));
    const std::array<SDL_Texture*,1> images{icon.get()};
    fixture.request.immutable_images = images;
    fixture.request.image_epoch = 1;
    expect_region(fixture.warm());
    ++fixture.request.image_epoch; // Same pointer; lifecycle publication changed.
    expect_region(fixture.warm() && fixture.cache->work().tiles.prepared == 2);
    fixture.request.immutable_images = {};
    expect_region(!fixture.cache->prepare(fixture.request) && !fixture.cache->submit(0));
}

TEST_CASE("Opaque SDL region CPU reservation includes frame compaction without shrinking texture allowance", "[ui][region-cache]") {
    expect_region(sdl_region_cache_t::inner_cpu_reservation + sdl_region_cache_t::frame_reservation ==
        sdl_region_cache_t::cpu_reservation);
    expect_region(sdl_region_cache_t::frame_geometry_budget + 64U*1024U ==
        sdl_region_cache_t::frame_reservation);
    region_fixture_t fixture;
    for (std::size_t slot = 0; slot < sdl_region_cache_t::maximum_regions; ++slot) {
        fixture.request.slot = slot;
        expect_region(fixture.warm());
        const auto work = fixture.cache->work();
        expect_region(work.retained_cpu_bytes <= sdl_region_cache_t::inner_cpu_reservation);
        expect_region(work.tiles.retained_bytes <= wormedit::ui::field::chrome_cache_reservation-
            sdl_region_cache_t::cpu_reservation);
    }
}

TEST_CASE("Opaque SDL region oversized borrowed meshes fall back before copying into inner CPU storage", "[ui][region-cache]") {
    region_fixture_t fixture;
    std::vector<sdl_command_vertex_t> vertices(40000);
    vertices[0] = {{20,30},{0,0},{1,1,1,1}};
    vertices[1] = {{100,30},{0,0},{1,1,1,1}};
    vertices[2] = {{20,100},{0,0},{1,1,1,1}};
    const std::array<sdl_command_index_t,3> indices{0,1,2};
    const std::array<sdl_command_batch_t,1> batches{{{nullptr,{0,0,192,1600},0,3}}};
    fixture.request.mesh = {vertices,indices,batches,{}};
    expect_region(fixture.request.mesh.is_valid());
    fixture.begin();
    expect_region(!fixture.cache->prepare(fixture.request));
    expect_region(fixture.cache->work().retained_cpu_bytes == 0 && fixture.cache->work().tiles.retained_bytes == 0);
}
