#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/lucide_icon_pixels.h"

#include <SDL3/SDL_render.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace wormedit::ui::lucide {
namespace {

std::array<SDL_Texture*, static_cast<std::size_t>(Glyph::count)> textures{};

auto make_texture(SDL_Renderer* renderer, std::size_t index) -> SDL_Texture* {
    constexpr int size = pixels::size;
    std::array<std::uint8_t, size * size * 4> rgba{};
    for (std::size_t pixel = 0; pixel < pixels::alpha[index].size(); ++pixel) {
        rgba[pixel * 4] = 255;
        rgba[pixel * 4 + 1] = 255;
        rgba[pixel * 4 + 2] = 255;
        rgba[pixel * 4 + 3] = pixels::alpha[index][pixel];
    }
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, size, size);
    if (!texture) return nullptr;
    if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) ||
        !SDL_UpdateTexture(texture, nullptr, rgba.data(), size * 4)) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    return texture;
}

} // namespace

auto initialize(SDL_Renderer* renderer) -> bool {
    shutdown();
    if (!renderer) return false;
    for (std::size_t index = 0; index < textures.size(); ++index) {
        textures[index] = make_texture(renderer, index);
        if (!textures[index]) {
            shutdown();
            return false;
        }
    }
    return true;
}

void shutdown() {
    for (auto*& texture : textures) {
        if (texture) SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}

void draw(nk_context* context, struct nk_rect button, Glyph glyph, struct nk_color tint) {
    if (!context) return;
    const auto index = static_cast<std::size_t>(glyph);
    if (index >= textures.size() || !textures[index]) return;
    constexpr float displayed_size = 18.0f;
    const auto bounds = nk_rect(button.x + (button.w - displayed_size) * 0.5f,
                                button.y + (button.h - displayed_size) * 0.5f,
                                displayed_size, displayed_size);
    const auto image = nk_image_ptr(textures[index]);
    nk_draw_image(nk_window_get_canvas(context), bounds, &image, tint);
}

} // namespace wormedit::ui::lucide
