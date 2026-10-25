#pragma once
#include "ui/field/color.h"
#include "ui/field/field_glyphs.h"
#include "ui/field/field_tile_cache.h"
#include <array>
#include <span>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <unordered_set>
#include <set>
#include <utility>

namespace wormedit::ui::field {
using layer_t = std::uint16_t;
enum class waveform_piece_kind_t : std::uint8_t { stem = 0, reserved = 1, packed_event = 2 };
using waveform_piece_key_t = std::pair<waveform_piece_kind_t, std::uint64_t>;
using color_t = wormedit::ui::color_t;
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
enum class envelope_axis_t { vertical, horizontal };
class renderer_t;
struct envelope_raster_region_t {
    SDL_FRect clip{};
    std::int64_t first_world{};
    std::size_t count{};
    double time_offset{};
    float amplitude_offset{};
    envelope_axis_t axis{envelope_axis_t::vertical};
    bool is_screen{};
};
using envelope_painter_t = bool (*)(renderer_t&, const envelope_raster_region_t&, void*);
// Bounds are display pixels; the ring retains rows along the selected time axis.
// Fingerprints cover first_world through first_world+count, including the
// caller's antialias halo. Painter data remains alive through submission.
struct envelope_strip_view_t {
    SDL_FRect bounds{};
    double origin{};
    std::int64_t first_world{};
    std::span<const std::uint64_t> fingerprints;
    std::uint64_t generation{};
    envelope_axis_t axis{envelope_axis_t::vertical};
    envelope_painter_t painter{};
    void* data{};
};
struct envelope_work_t {
    std::size_t retained_rows{}, regenerated_rows{}, raster_vertices{}, composites{};
    std::size_t resident_bytes{}, peak_reserved_bytes{}, cached_stems{}, evictions{}, budget_fallbacks{};
    std::size_t collected_stem_rows{}, refilled_stems{}, deferred_stems{}, stem_fallbacks{};
};
struct envelope_cache_t;
// Frontend-private geometry only: no document state or toolkit objects are retained.
class renderer_t {
  public:
    explicit renderer_t(std::size_t envelope_budget_bytes = 64U * 1024U * 1024U);
    ~renderer_t();
    void invalidate_envelopes() noexcept;
    void begin(SDL_FRect viewport, SDL_FPoint scale = {1, 1}, solid_texture_t solid = {},
               SDL_Renderer* device = nullptr);
    auto envelope(std::size_t slot, layer_t layer, std::span<const envelope_row_t> rows,
                  SDL_FRect bounds, double world_origin, std::uint64_t generation,
                  envelope_style_t style, float y_scale = 1) -> bool;
    auto envelope_strip(std::size_t slot, layer_t layer, const envelope_strip_view_t&) -> bool;
    // Pin the complete next visible set before any stem admission or drawing.
    auto reserve_envelope(std::size_t slot, SDL_FRect bounds, float y_scale = 1) -> bool;
    void pin_stem_envelopes(std::span<const std::uint64_t> keys);
    // Keys describe viewport order. Changed zooms refresh a rotating, bounded
    // window; the first stable zoom refreshes every outstanding stem.
    void prepare_stem_zoom(double zoom, std::span<const std::uint64_t> keys,
                           std::size_t refill_budget = 8);
    // Before converting projected rows, try a compatible ready texture. The
    // caller draws current line fallbacks outside the returned screen coverage.
    auto retain_stem_envelope(std::uint64_t key, layer_t layer, SDL_FRect bounds,
                              double origin, std::uint64_t generation,
                              envelope_style_t style, float y_scale,
                              SDL_FRect& coverage) -> bool;
    // Each channel may enqueue one envelope per frame; repeated draws fall back safely.
    auto stem_envelope(std::uint64_t key, layer_t layer, std::span<const envelope_row_t> rows,
                       SDL_FRect bounds, double world_origin, std::uint64_t generation,
                       envelope_style_t style, float y_scale = 1) -> bool;
    void pin_piece_envelopes(std::span<const waveform_piece_key_t> keys);
    void prepare_piece_zoom(double zoom, std::span<const waveform_piece_key_t> keys,
                            std::size_t refill_budget = 8);
    auto retain_piece_envelope(waveform_piece_key_t key, layer_t layer, SDL_FRect bounds,
                              double origin, std::uint64_t generation,
                              envelope_style_t style, float y_scale, SDL_FRect& coverage) -> bool;
    auto piece_envelope(waveform_piece_key_t key, layer_t layer,
                        std::span<const envelope_row_t> rows, SDL_FRect bounds,
                        double origin, std::uint64_t generation,
                        envelope_style_t style, float y_scale = 1) -> bool;
    auto envelope_work() const noexcept -> envelope_work_t;
    void limit_geometry(std::size_t bytes) noexcept;
    auto geometry_overflow() const noexcept -> bool { return geometry_overflow_; }
    auto geometry_bytes() const noexcept -> std::size_t;
    auto viewport() const noexcept -> SDL_FRect { return viewport_; }
    auto translate_geometry(SDL_FPoint offset) -> bool;
    auto can_retain_tiles() const noexcept -> bool { return tiles_ && tiles_->can_retain(); }
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
                       const std::function<void(renderer_t&, double)>& paint,
                       SDL_FPoint raster_origin = {});
    // Cache compositing stays at the caller's original pass insertion point.
    void cached_quad(layer_t layer, SDL_FRect bounds, SDL_Texture* texture,
                     SDL_FPoint uv_min, SDL_FPoint uv_max);
    void image(layer_t layer, SDL_FRect bounds, SDL_Texture* texture, color_t tint);
    void rect(layer_t layer, SDL_FRect bounds, color_t color);
    void hline(layer_t layer, float x0, float x1, float y, float width, color_t color);
    void vline(layer_t layer, float x, float y0, float y1, float width, color_t color);
    void line(layer_t layer, SDL_FPoint a, SDL_FPoint b, float width, color_t color);
    void polygon(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                 float aa_fringe = 0);
    void stroke_polygon(layer_t layer, std::span<const SDL_FPoint> points, float width,
                        color_t color, float aa_fringe = 0);
    auto triangles(layer_t layer, std::span<const SDL_Vertex> vertices,
                   std::span<const std::uint32_t> indices, SDL_Texture* texture = nullptr) -> bool;
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
    // Reserved keys are separate from all channel IDs, including 0 and 1.
    using envelope_key_t = waveform_piece_key_t;
    std::map<envelope_key_t, std::unique_ptr<envelope_cache_t>> envelopes_;
    std::set<waveform_piece_key_t> pinned_stems_;
    std::set<waveform_piece_key_t> zoom_refill_keys_;
    double stem_zoom_{};
    std::size_t stem_zoom_cursor_{}, collected_stem_rows_{}, stem_fallbacks_{};
    bool stem_zoom_enabled_{}, stem_zoom_changing_{};
    std::size_t envelope_budget_bytes_{}, envelope_peak_bytes_{};
    std::size_t envelope_evictions_{}, envelope_budget_fallbacks_{};
    std::uint64_t envelope_draw_sequence_{};
    auto envelope_bytes() const noexcept -> std::size_t;
    auto evict_unsubmitted_stem() -> bool;
    auto evict_offscreen_envelope(envelope_key_t except) -> bool;
    auto envelope_dimensions(SDL_FRect bounds, float y_scale) const -> std::optional<std::pair<int, int>>;
    auto prepare_envelope_cache(envelope_key_t key, int width, int height, envelope_axis_t axis = envelope_axis_t::vertical) -> envelope_cache_t*;
    auto envelope_for_key(envelope_key_t key, layer_t layer, std::span<const envelope_row_t> rows,
                          SDL_FRect bounds, double origin, std::uint64_t generation,
                          envelope_style_t style, float y_scale) -> bool;
    std::unique_ptr<field_tile_cache_t> tiles_;
    auto submit_envelope_fallback(SDL_Renderer* renderer, SDL_Texture* texture,
                                   submission_t hooks, bool& fallback) const -> bool;
    void envelope_quad(layer_t layer, const envelope_cache_t& cache,
                       ring_range_t span, std::int64_t world);
    void composite_envelope(layer_t layer, const envelope_cache_t& cache);
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
