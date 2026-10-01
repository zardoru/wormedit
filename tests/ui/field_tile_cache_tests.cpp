#include "ui/field/field_tile_cache.h"
#include "ui/field/field_renderer.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
using namespace wormedit::ui::field;
static void expect_tile(bool condition) { REQUIRE(condition); }
TEST_CASE("Static tile identity preserves exact chart layout revision and tile fields", "[ui][field][tile]") {
    tile_key_t key;
    for (std::size_t index=0; index<key.identity.size(); ++index) {
        auto changed=key;
        changed.identity[index]=1;
        expect_tile(changed!=key);
    }
    for (std::size_t index=0; index<key.style.size(); ++index) {
        auto changed=key;
        changed.style[index]=1;
        expect_tile(changed!=key);
    }
    auto changed=key;
    changed.tile=-1;
    expect_tile(changed!=key);
    changed=key;
    changed.pass=1;
    expect_tile(changed!=key);
}
TEST_CASE("Static tile budget caps bytes and admits two fills per frame", "[ui][field][tile]") {
    tile_budget_t budget;
    expect_tile(budget.reserve(tile_budget_t::limit/2));
    expect_tile(budget.reserve(tile_budget_t::limit/2));
    expect_tile(!(budget.reserve(0)));
    expect_tile(budget.bytes()==tile_budget_t::limit);
    budget.begin();
    expect_tile(!(budget.reserve(1)));
    budget.release(tile_budget_t::limit/2);
    expect_tile(budget.reserve(tile_budget_t::limit/2));
    budget.release(tile_budget_t::limit+1);
    expect_tile(budget.bytes()==0);
}
TEST_CASE("Static tiles retain coverage expose only missing strips and reset with device", "[ui][field][tile]") {
    auto* surface=SDL_CreateSurface(128,128,SDL_PIXELFORMAT_RGBA32);
    expect_tile(surface!=nullptr);
    auto* device=SDL_CreateSoftwareRenderer(surface);
    expect_tile(device!=nullptr);
    renderer_t field;
    tile_request_t request;
    request.key.pass=1;
    request.viewport={0,0,128,128};
    field.begin(request.viewport,{1,1},{},device);
    expect_tile(!(field.composite_tiles(request)));
    field.rect(1,{0,0,128,128},{51,62,73});
    field.capture_tiles(request,0);
    expect_tile(field.tile_work().prepared==1);
    field.begin(request.viewport,{1,1},{},device);
    expect_tile(field.composite_tiles(request));
    expect_tile(field.missing_tiles(request).empty());
    request.origin=12;
    field.begin(request.viewport,{1,1},{},device);
    expect_tile(!(field.composite_tiles(request)));
    const auto missing=field.missing_tiles(request);
    expect_tile(missing.size()==1);
    expect_tile(missing.front().y==116);
    expect_tile(missing.front().h==12);
    field.invalidate_envelopes();
    field.begin(request.viewport,{1,1},{},device);
    expect_tile(!(field.composite_tiles(request)));
    expect_tile(field.tile_work().retained_bytes==0);
    // Cache targets must be destroyed before the owning SDL renderer.
    field.invalidate_envelopes();
    SDL_DestroyRenderer(device);
    SDL_DestroySurface(surface);
}
TEST_CASE("Static interval lookup preserves overlapping painter order without dense scans", "[ui][field][tile]") {
    std::vector<static_interval_t> intervals;
    for (std::size_t index=0; index<32768; ++index)
        intervals.push_back({double(index)*10, double(index)*10+2, 32767-index});
    intervals.push_back({0, 400000, 32768});
    static_interval_index_t lookup;
    lookup.rebuild(intervals);
    const auto result=lookup.query(100001,100011);
    expect_tile(result.indices==std::vector<std::size_t>{22766,22767,32768});
    expect_tile(result.scanned<64);
    expect_tile(lookup.query(-100,-1).indices.empty());
    expect_tile(lookup.query(10,0).indices.empty());
    expect_tile(lookup.accounted_bytes()<2*1024*1024);
    lookup.rebuild({});
    expect_tile(lookup.query(0,400000).indices.empty());
}
TEST_CASE("Complete static tiles remain reusable across fractional follow and reset", "[ui][field][tile]") {
    auto* surface=SDL_CreateSurface(256,256,SDL_PIXELFORMAT_RGBA32);
    auto* device=SDL_CreateSoftwareRenderer(surface);
    REQUIRE(device);
    renderer_t field;
    tile_request_t request;
    request.viewport={0,0,128,100}; request.origin=100.25; request.key.pass=1;
    std::size_t paints{};
    const auto paint=[&](renderer_t& target,double origin) {
        ++paints;
        target.rect(1,{0,0,128,512},{120,80,40});
        expect_tile(origin==0);
    };
    field.begin(request.viewport,{1,1},{},device);
    field.prepare_tiles(request,0,2048,1024,paint);
    expect_tile(field.composite_tiles(request));
    expect_tile(paints==1);
    expect_tile(field.tile_work().prepared==1);
    for (const auto origin:{100.5,101.25,65.3,120.75}) {
        request.origin=origin;
        field.begin(request.viewport,{1,1},{},device);
        field.prepare_tiles(request,0,2048,1024,paint);
        expect_tile(field.composite_tiles(request));
        expect_tile(field.tile_work().prepared==0);
    }
    expect_tile(paints==1);
    field.invalidate_envelopes();
    field.begin(request.viewport,{1,1},{},device);
    expect_tile(!field.composite_tiles(request));
    field.invalidate_envelopes();
    SDL_DestroyRenderer(device); SDL_DestroySurface(surface);
}
static bool tile_rect_contains(SDL_FRect rect,float x,float y) {
    return x>=rect.x && y>=rect.y && x<rect.x+rect.w && y<rect.y+rect.h;
}
TEST_CASE("Dynamic holes partition tile coverage without overlapping alpha", "[ui][field][tile]") {
    const std::array<SDL_FRect,2> holes{{{2,2,3,3},{4,3,3,3}}};
    const auto regions=subtract_rectangles({0,0,11,11},holes);
    for (int y=0; y<11; ++y) for (int x=0; x<11; ++x) {
        std::size_t coverage{};
        for (const auto region:regions) coverage+=tile_rect_contains(region,x+.5f,y+.5f);
        const bool excluded=tile_rect_contains(holes[0],x+.5f,y+.5f) ||
                            tile_rect_contains(holes[1],x+.5f,y+.5f);
        expect_tile(coverage==(excluded ? 0 : 1));
    }
    expect_tile(subtract_rectangles({0,0,11,11},std::array<SDL_FRect,1>{{{0,0,11,11}}}).empty());
}

TEST_CASE("Bounded interval query preserves painter order and reports every truncated result",
          "[ui][field][tile]") {
    static_interval_index_t index;
    const std::array<static_interval_t, 5> records{
        {{2, 4, 5},
         {1, 8, 2},
         {2, 4, 3},
         {std::numeric_limits<double>::quiet_NaN(), 8, 9},
         {9, 7, 8}}};
    index.rebuild(records);
    std::array<std::size_t, 4> scratch{};
    const auto complete = index.query_into(3, 3, scratch);
    expect_tile(complete.count == 3 && !complete.overflow);
    expect_tile(scratch[0] == 2 && scratch[1] == 3 && scratch[2] == 5);
    const auto limited = index.query_into(3, 3, std::span(scratch).first(1));
    expect_tile(limited.count == 1 && limited.overflow);
    const auto empty = index.query_into(3, 3, {});
    expect_tile(empty.count == 0 && empty.overflow);
    expect_tile(index.query_into(10, 9, scratch).count == 0);
    expect_tile(index.query_into(std::numeric_limits<double>::infinity(), 9, scratch).count == 0);
    const auto compatible = index.query(3, 3);
    expect_tile(compatible.indices == std::vector<std::size_t>({2, 3, 5}));
    expect_tile(compatible.scanned == complete.scanned);
}

TEST_CASE("Geometry scratch budget bounds capacity and abandons oversized retained tiles", "[ui][field][tile]") {
    renderer_t limited;
    limited.limit_geometry(2048);
    limited.begin({0, 0, 64, 64});
    for (unsigned index = 0; index < 1024; ++index)
        limited.rect(1, {float(index % 64), float(index % 64), 1, 1}, {100, 120, 140, 255});
    expect_tile(limited.geometry_overflow());
    expect_tile(limited.geometry_bytes() <= 2048);
    const auto vertices = limited.vertices().size();
    limited.rect(1, {0, 0, 1, 1}, {100, 120, 140, 255});
    expect_tile(limited.vertices().size() == vertices);
    limited.limit_geometry(0);
    limited.begin({0, 0, 64, 64});
    limited.rect(1, {0, 0, 1, 1}, {100, 120, 140, 255});
    expect_tile(!limited.geometry_overflow());
    expect_tile(limited.indices().size() == 6);
}
TEST_CASE("Oversized tile preparation keeps the direct fallback available", "[ui][field][tile]") {
    auto* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    expect_tile(surface != nullptr);
    auto* device = SDL_CreateSoftwareRenderer(surface);
    expect_tile(device != nullptr);
    {
        renderer_t field;
        field.begin({0, 0, 64, 64}, {1, 1}, {}, device);
        tile_request_t request;
        request.key.pass = 1;
        request.viewport = {0, 0, 64, 64};
        bool overflow{};
        field.prepare_tiles(request, 0, 512, 0, [&](renderer_t& scratch, double) {
            for (unsigned index = 0; index < 32768; ++index)
                scratch.rect(1, {float(index % 64), float(index % 512), 1, 1},
                             {100, 120, 140, 255});
            overflow = scratch.geometry_overflow();
            expect_tile(scratch.geometry_bytes() <= 4 * 1024 * 1024);
        });
        expect_tile(overflow);
        expect_tile(!field.composite_tiles(request));
        expect_tile(field.tile_work().prepared == 0);
        field.rect(1, {0, 0, 64, 64}, {20, 40, 60, 255});
        expect_tile(field.submit(device));
    }
    SDL_DestroyRenderer(device);
    SDL_DestroySurface(surface);
}
