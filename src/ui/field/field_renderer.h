#pragma once
#include "ui/field/field_glyphs.h"
#include <array>
#include <span>
#include <vector>

namespace wormedit::ui::field {
using layer_t = std::uint16_t;
struct color_t {
    std::uint8_t r{}, g{}, b{}, a{255};
};
struct batch_t {
    layer_t layer{};
    SDL_Texture* texture{};
    std::size_t index_offset{};
    std::size_t index_count{};
};
struct workload_t {
    std::size_t vertices{}, indices{}, draw_calls{};
};
struct solid_texture_t {
    SDL_Texture* texture{};
    SDL_FPoint white_uv{};
};
struct submission_t {
    auto (*clip)(SDL_Renderer*, const SDL_Rect*) -> bool{};
    auto (*geometry)(SDL_Renderer*, SDL_Texture*, const float*, int, const SDL_FColor*, int,
                     const float*, int, int, const void*, int, int) -> bool{};
};
// Frontend-private geometry only: no document state or toolkit objects are retained.
class renderer_t {
  public:
    void begin(SDL_FRect viewport, SDL_FPoint scale = {1, 1}, solid_texture_t solid = {});
    void rect(layer_t layer, SDL_FRect bounds, color_t color);
    void hline(layer_t layer, float x0, float x1, float y, float width, color_t color);
    void vline(layer_t layer, float x, float y0, float y1, float width, color_t color);
    void line(layer_t layer, SDL_FPoint a, SDL_FPoint b, float width, color_t color);
    void polygon(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                 float aa_fringe = 0);
    void stroke_polygon(layer_t layer, std::span<const SDL_FPoint> points, float width,
                        color_t color, float aa_fringe = 0);
    void text(layer_t layer, SDL_FPoint pen, std::string_view utf8, color_t color,
              const font_t& font);
    auto submit(SDL_Renderer* renderer, submission_t hooks = {}) const -> bool;
    auto submit_range(SDL_Renderer* renderer, std::size_t first_index, std::size_t index_count,
                      submission_t hooks = {}) const -> bool;
    auto workload() const noexcept -> workload_t;
    auto vertices() const noexcept -> std::span<const SDL_Vertex> {
        return vertices_;
    }
    auto indices() const noexcept -> std::span<const std::uint32_t> {
        return indices_;
    }
    auto batches() const noexcept -> std::span<const batch_t> {
        return batches_;
    }

  private:
    SDL_FRect viewport_{};
    SDL_FPoint scale_{1, 1};
    solid_texture_t solid_{};
    std::vector<SDL_Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<batch_t> batches_;
    void append_batch(layer_t layer, SDL_Texture* texture, std::size_t count);
    void quad(layer_t layer, std::span<const SDL_FPoint, 4> points, color_t color,
              SDL_Texture* texture = nullptr, SDL_FPoint uv_min = {}, SDL_FPoint uv_max = {});
    void glyph(layer_t layer, SDL_FRect bounds, color_t color, SDL_Texture* texture,
               SDL_FPoint uv_min, SDL_FPoint uv_max);
    void stroke_joined(layer_t layer, std::span<const SDL_FPoint> points, float width, color_t color);
    void stroke_fringe(layer_t layer, std::span<const SDL_FPoint> points, float width,
                       color_t color, float aa_fringe);
    void polygon_fringe(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                        float width, std::uint32_t base);
};
} // namespace wormedit::ui::field
