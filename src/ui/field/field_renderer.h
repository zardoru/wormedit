#pragma once
#include "ui/field/field_glyphs.h"
#include "ui/field/field_tile_cache.h"
#include <array>
#include <span>
#include <vector>
#include <memory>

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
    SDL_FRect clip{};
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
struct ring_range_t {
    std::size_t first{}, count{};
};
// World rows retain their physical slot while the viewport follows playback.
// This planning state has no texture, audio or document dependency.
class ring_rows_t {
  public:
    void update(std::uint64_t generation, std::int64_t first_row,
                std::span<const std::uint64_t> fingerprints);
    void invalidate() noexcept;
    auto head() const noexcept -> std::size_t;
    auto ranges() const noexcept -> std::array<ring_range_t, 2>;
    auto dirty_ranges() const noexcept -> std::span<const ring_range_t> { return dirty_; }
    auto regenerated_rows() const noexcept -> std::size_t { return regenerated_; }
    auto capacity() const noexcept -> std::size_t { return slots_.size(); }

  private:
    struct slot_t {
        std::int64_t world_row{};
        std::uint64_t fingerprint{};
        bool valid{};
    };
    std::vector<slot_t> slots_;
    std::vector<ring_range_t> dirty_;
    std::uint64_t generation_{};
    std::int64_t first_row_{};
    std::size_t regenerated_{};
    bool valid_{};
    auto slot(std::int64_t world_row) const noexcept -> std::size_t;
    void mark_dirty(std::size_t index);
};
struct envelope_row_t {
    double world_y{};
    std::array<float, 2> rms{}, peak{};
};
struct envelope_style_t {
    color_t rms{}, peak{};
    bool joined_channels{};
};
struct envelope_work_t {
    std::size_t retained_rows{}, regenerated_rows{}, raster_vertices{}, composites{};
};
struct envelope_cache_t;
// Frontend-private geometry only: no document state or toolkit objects are retained.
class renderer_t {
  public:
    renderer_t();
    ~renderer_t();
    void invalidate_envelopes() noexcept;
    void begin(SDL_FRect viewport, SDL_FPoint scale = {1, 1}, solid_texture_t solid = {},
               SDL_Renderer* device = nullptr);
    auto envelope(std::size_t slot, layer_t layer, std::span<const envelope_row_t> rows,
                  SDL_FRect bounds, double world_origin, std::uint64_t generation,
                  envelope_style_t style, float y_scale = 1) -> bool;
    auto envelope_work() const noexcept -> envelope_work_t;
    void limit_geometry(std::size_t bytes) noexcept;
    auto geometry_overflow() const noexcept -> bool { return geometry_overflow_; }
    auto geometry_bytes() const noexcept -> std::size_t;
    auto push_clip(SDL_FRect clip) -> SDL_FRect;
    void pop_clip(SDL_FRect clip);
    auto composite_tiles(const tile_request_t& request,
                         std::span<const SDL_FRect> holes = {}) -> bool;
    auto missing_tiles(const tile_request_t& request) -> std::vector<SDL_FRect>;
    void capture_tiles(const tile_request_t& request, std::size_t first_index);
    auto tile_work() const noexcept -> tile_work_t;
    void account_tile_source(std::size_t bytes);
    void prepare_tiles(const tile_request_t& request, double covered_begin, double covered_end,
                       std::size_t source_bytes,
                       const std::function<void(renderer_t&, double)>& paint);
    // Cache compositing stays at the caller's original pass insertion point.
    void cached_quad(layer_t layer, SDL_FRect bounds, SDL_Texture* texture,
                     SDL_FPoint uv_min, SDL_FPoint uv_max);
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
    SDL_Renderer* device_{};
    std::size_t geometry_limit_{};
    bool geometry_overflow_{};
    auto reserve_geometry(std::size_t vertices, std::size_t indices) -> bool;
    std::array<std::unique_ptr<envelope_cache_t>, 2> envelopes_;
    std::unique_ptr<field_tile_cache_t> tiles_;
    auto submit_envelope_fallback(SDL_Renderer* renderer, SDL_Texture* texture,
                                   submission_t hooks, bool& fallback) const -> bool;
    void envelope_quad(layer_t layer, const envelope_cache_t& cache,
                       ring_range_t span, std::int64_t world);
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
