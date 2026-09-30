#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <array>
#include <string_view>

namespace wormedit::ui::field {
struct glyph_t {
    SDL_FPoint offset{};
    SDL_FPoint size{};
    SDL_FPoint uv_min{};
    SDL_FPoint uv_max{};
    float advance{};
};
struct font_t {
    void* userdata{};
    SDL_Texture* texture{};
    float height{};
    auto (*query)(void*, float, std::uint32_t, std::uint32_t) -> glyph_t{};
};
struct decoded_codepoint_t {
    std::uint32_t value{};
    std::size_t bytes{};
};
auto decode_utf8(std::string_view text) noexcept -> decoded_codepoint_t;
auto rounded_rectangle_points(SDL_FRect bounds, float radius) -> std::array<SDL_FPoint, 16>;
auto glyph_bounds(glyph_t glyph, SDL_FPoint pen) noexcept -> SDL_FRect;
} // namespace wormedit::ui::field
