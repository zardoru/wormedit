#include "ui/nuklear/render/lucide_icons.h"
#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

namespace {
void expect_icons(bool condition) { REQUIRE(condition); }
struct icon_fixture_t {
    SDL_Surface* surface = SDL_CreateSurface(16,16,SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    icon_fixture_t() { expect_icons(renderer != nullptr); }
    ~icon_fixture_t() {
        wormedit::ui::lucide::shutdown();
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
    }
};
}

TEST_CASE("Lucide immutable texture snapshots identify the renderer and every published glyph", "[ui][lucide]") {
    using namespace wormedit::ui::lucide;
    icon_fixture_t fixture;
    expect_icons(initialize(fixture.renderer));
    const auto snapshot = immutable_textures();
    expect_icons(snapshot.renderer == fixture.renderer);
    expect_icons(snapshot.textures.size() == static_cast<std::size_t>(Glyph::count));
    for (std::size_t index = 0; index < snapshot.textures.size(); ++index) {
        auto* image = snapshot.textures[index];
        SDL_BlendMode blend{};
        expect_icons(image == texture(static_cast<Glyph>(index)) && image != nullptr);
        expect_icons(SDL_GetTextureBlendMode(image,&blend) && blend == SDL_BLENDMODE_BLEND);
    }
}

TEST_CASE("Lucide texture clear and republication advance identity even if handles are reused", "[ui][lucide]") {
    using namespace wormedit::ui::lucide;
    icon_fixture_t fixture;
    expect_icons(initialize(fixture.renderer));
    const auto published = immutable_textures();
    shutdown();
    const auto cleared = immutable_textures();
    expect_icons(cleared.renderer == nullptr && cleared.textures.empty());
    expect_icons(cleared.generation > published.generation);
    expect_icons(initialize(fixture.renderer));
    const auto next = immutable_textures();
    expect_icons(next.generation > cleared.generation && next.renderer == fixture.renderer);
    expect_icons(std::ranges::all_of(next.textures,[](auto* image) { return image != nullptr; }));
}

TEST_CASE("Lucide failed initialization retires the previous immutable publication", "[ui][lucide]") {
    using namespace wormedit::ui::lucide;
    icon_fixture_t fixture;
    expect_icons(initialize(fixture.renderer));
    const auto before = immutable_textures().generation;
    expect_icons(!initialize(nullptr));
    const auto after = immutable_textures();
    expect_icons(after.generation > before && after.renderer == nullptr && after.textures.empty());
    expect_icons(texture(Glyph::play) == nullptr);
}
