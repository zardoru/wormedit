#include "ui/field/field_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <bit>
#include <string_view>

namespace wormedit::ui::field {
namespace {
auto rgba(color_t color) -> SDL_FColor {
    constexpr float unit = 1.0f / 255.0f;
    return {color.r * unit, color.g * unit, color.b * unit, color.a * unit};
}
auto intersection(SDL_FRect a, SDL_FRect b) -> SDL_FRect {
    const auto left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const auto right = std::min(a.x + a.w, b.x + b.w);
    const auto bottom = std::min(a.y + a.h, b.y + b.h);
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}
auto bounds_of(std::span<const SDL_FPoint> points) -> SDL_FRect {
    if (points.empty())
        return {};
    float left = points.front().x, right = left, top = points.front().y, bottom = top;
    for (const auto point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}
auto normal(SDL_FPoint a, SDL_FPoint b) -> SDL_FPoint {
    const auto x = b.x - a.x, y = b.y - a.y;
    const auto length = std::hypot(x, y);
    return length == 0 ? SDL_FPoint{} : SDL_FPoint{y / length, -x / length};
}
auto outward_corner(std::span<const SDL_FPoint> points, std::size_t index, float width)
    -> SDL_FPoint {
    const auto point = points[index];
    const auto before = normal(points[(index + points.size() - 1) % points.size()], point);
    const auto after = normal(point, points[(index + 1) % points.size()]);
    const auto x = (before.x + after.x) * .5f, y = (before.y + after.y) * .5f;
    const auto squared = x * x + y * y;
    const auto factor = squared > 0 ? std::min(100.0f, 1.0f / squared) * width : 0;
    return {point.x + x * factor, point.y + y * factor};
}
auto clipped_uv(float start, float end, float offset, float size) -> float {
    return start + (end - start) * offset / size;
}
} // namespace

auto ring_rows_t::slot(std::int64_t world_row) const noexcept -> std::size_t {
    if (slots_.empty()) return 0;
    const auto count = static_cast<std::int64_t>(slots_.size());
    const auto remainder = world_row % count;
    return static_cast<std::size_t>(remainder < 0 ? remainder + count : remainder);
}
void ring_rows_t::invalidate() noexcept {
    valid_ = false;
    dirty_.clear();
    regenerated_ = 0;
}
void ring_rows_t::mark_dirty(std::size_t index) {
    ++regenerated_;
    if (!dirty_.empty() && dirty_.back().first + dirty_.back().count == index)
        ++dirty_.back().count;
    else
        dirty_.push_back({index, 1});
}
void ring_rows_t::update(std::uint64_t generation, std::int64_t first_row,
                         std::span<const std::uint64_t> fingerprints) {
    constexpr std::size_t maximum_rows = 4'098;
    dirty_.clear();
    regenerated_ = 0;
    if (fingerprints.size() > maximum_rows ||
        first_row > std::numeric_limits<std::int64_t>::max() -
                        static_cast<std::int64_t>(fingerprints.size())) {
        invalidate();
        return;
    }
    const bool reset = !valid_ || generation != generation_ || fingerprints.size() != slots_.size();
    slots_.resize(fingerprints.size());
    dirty_.reserve(fingerprints.size());
    generation_ = generation;
    first_row_ = first_row;
    for (std::size_t index = 0; index < fingerprints.size(); ++index) {
        const auto world_row = first_row + static_cast<std::int64_t>(index);
        auto& entry = slots_[slot(world_row)];
        if (reset || !entry.valid || entry.world_row != world_row ||
            entry.fingerprint != fingerprints[index]) {
            entry = {world_row, fingerprints[index], true};
            mark_dirty(slot(world_row));
        }
    }
    valid_ = true;
}
auto ring_rows_t::head() const noexcept -> std::size_t {
    return slot(first_row_);
}
auto ring_rows_t::ranges() const noexcept -> std::array<ring_range_t, 2> {
    const auto first = head();
    return {{{first, slots_.size() - first}, {0, first}}};
}

struct envelope_cache_t {
    SDL_Texture* texture{};
    SDL_Renderer* device{};
    SDL_FRect bounds{};
    double origin{};
    float y_scale{1};
    envelope_style_t style{};
    ring_rows_t plan;
    std::vector<envelope_row_t> rows;
    std::vector<envelope_row_t> input_rows;
    std::vector<std::int64_t> world_rows;
    std::vector<std::uint64_t> row_hashes, fingerprints;
    std::unique_ptr<renderer_t> raster;
    std::uint64_t generation{};
    int width{}, height{};
    bool active{}, pending{}, ready{}, fallback_submitted{}, unavailable{};
    ~envelope_cache_t() { if (texture) SDL_DestroyTexture(texture); }
};
namespace {
auto hash_value(std::uint64_t hash, std::uint64_t value) -> std::uint64_t {
    return (hash ^ value) * 1099511628211ULL;
}
// Rows the projection did not produce hash to this sentinel, never to the
// value a present row of zeros would give.
constexpr std::uint64_t absent_envelope_hash = 0x9E3779B97F4A7C15ULL;
auto envelope_hash(const envelope_row_t& row) -> std::uint64_t {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto value : {row.rms[0], row.rms[1], row.peak[0], row.peak[1]})
        hash = hash_value(hash, std::bit_cast<std::uint32_t>(value));
    return hash;
}
auto color_hash(color_t color) -> std::uint64_t {
    return (static_cast<std::uint64_t>(color.r) << 24U) |
           (static_cast<std::uint64_t>(color.g) << 16U) |
           (static_cast<std::uint64_t>(color.b) << 8U) | color.a;
}
auto envelope_slot(std::int64_t row, int height) -> std::size_t {
    const auto remainder = row % height;
    return static_cast<std::size_t>(remainder < 0 ? remainder + height : remainder);
}
auto valid_envelope_row(const envelope_row_t& row) -> bool {
    return std::isfinite(row.world_y) && std::abs(row.world_y) <= 1e12 &&
           std::isfinite(row.rms[0]) && std::isfinite(row.rms[1]) &&
           std::isfinite(row.peak[0]) && std::isfinite(row.peak[1]);
}
auto ensure_envelope_texture(envelope_cache_t& cache, SDL_Renderer* device, int width,
                              int height) -> bool {
    // Two independent rings have a combined 32 MiB texture ceiling.
    constexpr std::uint64_t maximum_pixels = 4U * 1024U * 1024U;
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) * height > maximum_pixels)
        return false;
    if (cache.device == device && cache.width == width && cache.height == height)
        return cache.texture && !cache.unavailable;
    if (cache.texture) SDL_DestroyTexture(cache.texture);
    cache.texture = nullptr;
    cache.plan.invalidate();
    cache.ready = false;
    cache.unavailable = false;
    cache.device = device;
    cache.width = width;
    cache.height = height;
    cache.texture = SDL_CreateTexture(device, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                       width, height);
    if (cache.texture && SDL_SetTextureBlendMode(cache.texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED) &&
        SDL_SetTextureScaleMode(cache.texture, SDL_SCALEMODE_NEAREST)) return true;
    if (cache.texture) SDL_DestroyTexture(cache.texture);
    cache.texture = nullptr;
    cache.unavailable = true;
    return false;
}
void collect_envelope_rows(envelope_cache_t& cache, std::span<const envelope_row_t> rows,
                           std::int64_t first) {
    const auto count = static_cast<std::size_t>(cache.height);
    cache.rows.assign(count + 1, {});
    cache.world_rows.assign(count + 1, std::numeric_limits<std::int64_t>::min());
    cache.row_hashes.assign(count + 1, absent_envelope_hash);
    cache.fingerprints.resize(count);
    for (const auto& row : rows) {
        if (!valid_envelope_row(row)) continue;
        const auto world = static_cast<std::int64_t>(std::floor(row.world_y));
        if (world < first - 1 || world >= first + cache.height) continue;
        const auto slot = envelope_slot(world, static_cast<int>(cache.rows.size()));
        cache.rows[slot] = row;
        cache.world_rows[slot] = world;
        cache.row_hashes[slot] = envelope_hash(row);
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = first + static_cast<std::int64_t>(index);
        const auto current = envelope_slot(world, static_cast<int>(cache.rows.size()));
        const auto previous = envelope_slot(world - 1, static_cast<int>(cache.rows.size()));
        // A scanline draws rows world-1 and world. Chain the hashes so the pair is
        // ordered: xor-combining them collapses every plateau of equal rows to
        // zero, hiding changed data from the ring and leaving stale or blank rows.
        cache.fingerprints[index] = hash_value(
            hash_value(1469598103934665603ULL, cache.row_hashes[current]), cache.row_hashes[previous]);
    }
}
void envelope_scanline(renderer_t& geometry, const envelope_cache_t& cache,
                        std::int64_t source, std::int64_t world, float y) {
    const auto slot = envelope_slot(source, static_cast<int>(cache.rows.size()));
    if (cache.world_rows[slot] != source) return;
    const auto& row = cache.rows[slot];
    const auto absolute_center = cache.bounds.x + cache.bounds.w * .5f;
    const auto center = std::round(absolute_center) - std::round(cache.bounds.x);
    const auto half = cache.bounds.w * .5f;
    const auto draw = [&](const std::array<float, 2>& values, color_t color) {
        const auto left = std::round(absolute_center - std::clamp(values[0], 0.0f, 1.0f) * half) - std::round(cache.bounds.x);
        const auto right = std::round(absolute_center + std::clamp(values[1], 0.0f, 1.0f) * half) - std::round(cache.bounds.x);
        if (cache.style.joined_channels) {
            geometry.rect(0, {left, y, right - left, 1}, color);
        } else {
            geometry.rect(0, {left, y, center - left, 1}, color);
            geometry.rect(0, {center, y, right - center, 1}, color);
        }
    };
    draw(row.rms, cache.style.rms);
    if (source == world) draw(row.peak, cache.style.peak);
}
void prepare_envelope_raster(envelope_cache_t& cache, std::int64_t first) {
    if (!cache.raster) cache.raster = std::make_unique<renderer_t>();
    cache.raster->begin({0, 0, static_cast<float>(cache.width), static_cast<float>(cache.height)});
    for (const auto range : cache.plan.dirty_ranges()) {
        for (std::size_t slot = range.first; slot < range.first + range.count; ++slot) {
            const auto relative = (slot + cache.plan.capacity() - cache.plan.head()) % cache.plan.capacity();
            const auto world = first + static_cast<std::int64_t>(relative);
            const auto y = static_cast<float>(slot);
            envelope_scanline(*cache.raster, cache, world - 1, world, y);
            envelope_scanline(*cache.raster, cache, world, world, y);
        }
    }
    cache.pending = cache.plan.regenerated_rows() != 0;
}
auto refill_envelope(envelope_cache_t& cache, submission_t hooks) -> bool {
    if (!cache.pending) return cache.ready;
    cache.ready = false;
    auto* device = cache.device;
    auto* previous = SDL_GetRenderTarget(device);
    SDL_BlendMode blend{};
    std::uint8_t r{}, g{}, b{}, a{};
    if (!SDL_GetRenderDrawBlendMode(device, &blend) || !SDL_GetRenderDrawColor(device, &r, &g, &b, &a))
        return false;
    if (!SDL_SetRenderTarget(device, cache.texture)) {
        cache.plan.invalidate();
        cache.pending = false;
        return false;
    }
    bool success = true;
    success = SDL_SetRenderDrawBlendMode(device, SDL_BLENDMODE_NONE) && success;
    success = SDL_SetRenderDrawColor(device, 0, 0, 0, 0) && success;
    success = SDL_SetRenderClipRect(device, nullptr) && success;
    for (const auto range : cache.plan.dirty_ranges()) {
        const SDL_FRect clear{0, static_cast<float>(range.first), static_cast<float>(cache.width),
                               static_cast<float>(range.count)};
        success = SDL_RenderFillRect(device, &clear) && success;
    }
    success = cache.raster->submit(device, hooks) && success;
    success = SDL_SetRenderTarget(device, previous) && success;
    success = SDL_SetRenderDrawBlendMode(device, blend) && success;
    success = SDL_SetRenderDrawColor(device, r, g, b, a) && success;
    cache.pending = false;
    cache.ready = success;
    if (!success) cache.plan.invalidate();
    return success;
}
auto draw_envelope_direct(const envelope_cache_t& cache, SDL_Renderer* device,
                          SDL_FRect viewport, submission_t hooks) -> bool {
    renderer_t direct;
    direct.begin(viewport);
    const auto center = cache.bounds.x + cache.bounds.w * .5f;
    const auto half = cache.bounds.w * .5f;
    for (const auto& row : cache.input_rows) {
        if (!valid_envelope_row(row)) continue;
        const auto y = cache.bounds.y + static_cast<float>(row.world_y - cache.origin) * cache.y_scale;
        const auto draw = [&](const std::array<float, 2>& values, float width, color_t color) {
            if (cache.style.joined_channels) {
                direct.hline(0, center - values[0] * half, center + values[1] * half, y, width, color);
            } else {
                direct.hline(0, center - values[0] * half, center, y, width, color);
                direct.hline(0, center, center + values[1] * half, y, width, color);
            }
        };
        draw(row.rms, 2, cache.style.rms);
        draw(row.peak, 1, cache.style.peak);
    }
    return direct.submit(device, hooks);
}
} // namespace
renderer_t::renderer_t() = default;
renderer_t::~renderer_t() = default;
void renderer_t::invalidate_envelopes() noexcept {
    for (auto& cache : envelopes_) cache.reset();
    tiles_.reset();
}
auto renderer_t::envelope(std::size_t slot, layer_t layer, std::span<const envelope_row_t> rows,
                          SDL_FRect bounds, double origin, std::uint64_t generation,
                          envelope_style_t style, float y_scale) -> bool {
    if (!device_ || !std::isfinite(y_scale) || y_scale <= 0 || slot >= envelopes_.size() || rows.empty() || !std::isfinite(origin) ||
        std::abs(origin) > 1e12 || bounds.w <= 0 || bounds.h <= 0 || bounds.w > 4096 || bounds.h > 4096)
        return false;
    // Software SDL truncates UV and destination vertices to integers. Wrapped
    // fractional scaling cannot retain the direct sampling phase there.
    const auto* backend = SDL_GetRendererName(device_);
    if (y_scale != 1 && backend && std::string_view(backend) == "software") return false;
    origin = std::floor(origin);
    if (!envelopes_[slot]) envelopes_[slot] = std::make_unique<envelope_cache_t>();
    auto& cache = *envelopes_[slot];
    const auto width = static_cast<int>(std::round(bounds.x + bounds.w) - std::round(bounds.x));
    const auto projected_height = static_cast<double>(bounds.h) / y_scale;
    if (!std::isfinite(projected_height) || projected_height > 4096) return false;
    const auto height = static_cast<int>(std::ceil(projected_height)) + 2;
    if (!ensure_envelope_texture(cache, device_, width, height)) return false;
    cache.active = true;
    cache.bounds = bounds;
    cache.origin = origin;
    cache.y_scale = y_scale;
    cache.style = style;
    cache.fallback_submitted = false;
    cache.input_rows.assign(rows.begin(), rows.end());
    const auto first = static_cast<std::int64_t>(std::floor(origin)) - 1;
    collect_envelope_rows(cache, rows, first);
    generation = hash_value(generation, std::bit_cast<std::uint32_t>(y_scale));
    generation = hash_value(generation, std::bit_cast<std::uint32_t>(bounds.w));
    generation = hash_value(generation, std::bit_cast<std::uint32_t>(bounds.x - std::floor(bounds.x)));
    generation = hash_value(generation, color_hash(style.rms));
    generation = hash_value(generation, color_hash(style.peak));
    generation = hash_value(generation, style.joined_channels);
    cache.plan.update(generation, first, cache.fingerprints);
    prepare_envelope_raster(cache, first);
    const auto spans = cache.plan.ranges();
    auto world = first;
    for (const auto span : spans) {
        if (span.count == 0) continue;
        envelope_quad(layer, cache, span, world);
        world += static_cast<std::int64_t>(span.count);
    }
    return true;
}
void renderer_t::envelope_quad(layer_t layer, const envelope_cache_t& cache,
                               ring_range_t span, std::int64_t world) {
    const auto& bounds = cache.bounds;
    const auto y_scale = cache.y_scale;
    // A 1/64 screen-pixel shift resolves nearest-filter and fixed-subpixel
    // rasterizer ties across wrap seams without extending UVs past an edge.
    // Unity drawing retains its original pixel phase.
    const auto origin = cache.origin + (y_scale == 1 ? 0 : 1.0 / (64 * y_scale));
    const auto width = cache.width;
    const auto height = cache.height;
    // Adjacent spans of the ring must share one computed edge. Deriving the
    // bottom from `count * y_scale` rounds differently from the next span's
    // top and can leave a one-pixel blank seam between the two quads.
    const auto edge = [&](std::int64_t row) {
        return static_cast<float>(static_cast<double>(bounds.y) +
                                  (static_cast<double>(row) - origin) * y_scale);
    };
    const auto y = edge(world);
    const SDL_FRect destination{std::round(bounds.x), y, static_cast<float>(width),
                                edge(world + static_cast<std::int64_t>(span.count)) - y};
    const auto left_x = std::max(destination.x, viewport_.x);
    const auto top_y = std::max(destination.y, viewport_.y);
    const auto right_x = std::min(destination.x + destination.w, viewport_.x + viewport_.w);
    const auto bottom_y = std::min(destination.y + destination.h, viewport_.y + viewport_.h);
    if (right_x <= left_x || bottom_y <= top_y) return;
    // Derive clipped UVs from canonical rows, not a second floating bounds ratio.
    const auto top_row = top_y == destination.y ? static_cast<double>(span.first)
        : span.first + (static_cast<double>(top_y) - bounds.y) / y_scale + origin - world;
    const auto bottom_row = bottom_y == edge(world + static_cast<std::int64_t>(span.count))
        ? static_cast<double>(span.first + span.count)
        : span.first + (static_cast<double>(bottom_y) - bounds.y) / y_scale + origin - world;
    const SDL_FPoint first_uv{(left_x - destination.x) / destination.w,
                              std::clamp(static_cast<float>(top_row / height), 0.0f, 1.0f)};
    const SDL_FPoint last_uv{(right_x - destination.x) / destination.w,
                             std::clamp(static_cast<float>(bottom_row / height), 0.0f, 1.0f)};
    const std::array<SDL_FPoint, 4> points{{{left_x,top_y}, {right_x,top_y},
                                           {right_x,bottom_y}, {left_x,bottom_y}}};
    quad(layer, points, {255,255,255,255}, cache.texture, first_uv, last_uv);
}
auto renderer_t::envelope_work() const noexcept -> envelope_work_t {
    envelope_work_t work;
    for (const auto& cache : envelopes_) {
        if (!cache || !cache->active) continue;
        work.regenerated_rows += cache->plan.regenerated_rows();
        work.retained_rows += cache->plan.capacity() - cache->plan.regenerated_rows();
        work.raster_vertices += cache->raster ? cache->raster->workload().vertices : 0;
        work.composites += cache->plan.ranges()[1].count != 0 ? 2 : 1;
    }
    return work;
}

void renderer_t::begin(SDL_FRect viewport, SDL_FPoint scale, solid_texture_t solid, SDL_Renderer* device) {
    device_ = device;
    if (!tiles_) tiles_ = std::make_unique<field_tile_cache_t>();
    tiles_->begin(device);
    for (auto& cache : envelopes_) if (cache) cache->active = false;
    solid_ = solid;
    viewport_ = viewport;
    scale_ = {std::max(.001f, scale.x), std::max(.001f, scale.y)};
    geometry_overflow_=false;
    vertices_.clear();
    indices_.clear();
    batches_.clear();
}
auto renderer_t::push_clip(SDL_FRect clip) -> SDL_FRect {
    const auto previous = viewport_;
    viewport_ = intersection(viewport_, clip);
    return previous;
}
void renderer_t::pop_clip(SDL_FRect clip) { viewport_ = clip; }
auto renderer_t::composite_tiles(const tile_request_t& request,
                                 std::span<const SDL_FRect> holes) -> bool {
    return tiles_ && tiles_->composite(*this, request, holes);
}
auto renderer_t::missing_tiles(const tile_request_t& request) -> std::vector<SDL_FRect> {
    return tiles_ ? tiles_->missing(request) : std::vector<SDL_FRect>{request.viewport};
}
void renderer_t::capture_tiles(const tile_request_t& request, std::size_t first_index) {
    if (tiles_) tiles_->capture(*this, request, first_index);
}
void renderer_t::prepare_tiles(const tile_request_t& request, double covered_begin,
                               double covered_end, std::size_t source_bytes,
                               const std::function<void(renderer_t&, double)>& paint) {
    if (!tiles_) return;
    renderer_t scratch;
    scratch.limit_geometry(4*1024*1024);
    const auto initialize=[&](renderer_t& target, double origin) {
        target.begin({0,0,request.viewport.w,512}, {1,1}, solid_);
        paint(target,origin);
    };
    tiles_->prepare(scratch,request,covered_begin,covered_end,source_bytes,initialize);
}
void renderer_t::account_tile_source(std::size_t bytes) {
    if (tiles_) tiles_->account_source(bytes);
}
auto renderer_t::tile_work() const noexcept -> tile_work_t {
    return tiles_ ? tiles_->work() : tile_work_t{};
}
void renderer_t::cached_quad(layer_t layer, SDL_FRect bounds, SDL_Texture* texture,
                              SDL_FPoint uv_min, SDL_FPoint uv_max) {
    const std::array<SDL_FPoint,4> points{{{bounds.x,bounds.y}, {bounds.x+bounds.w,bounds.y},
        {bounds.x+bounds.w,bounds.y+bounds.h}, {bounds.x,bounds.y+bounds.h}}};
    quad(layer, points, {255,255,255,255}, texture, uv_min, uv_max);
}
void renderer_t::append_batch(layer_t layer, SDL_Texture* texture, std::size_t count) {
    const auto offset = indices_.size() - count;
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (!batches_.empty() && batches_.back().layer == layer && batches_.back().texture == texture &&
        batches_.back().index_count <= maximum - count &&
        batches_.back().clip.x == viewport_.x && batches_.back().clip.y == viewport_.y &&
        batches_.back().clip.w == viewport_.w && batches_.back().clip.h == viewport_.h) {
        batches_.back().index_count += count;
    } else {
        batches_.push_back({layer, texture, offset, count, viewport_});
    }
}
void renderer_t::limit_geometry(std::size_t bytes) noexcept {
    geometry_limit_ = bytes;
    geometry_overflow_ = bytes && geometry_bytes() > bytes;
}
auto renderer_t::geometry_bytes() const noexcept -> std::size_t {
    return vertices_.capacity() * sizeof(SDL_Vertex) + indices_.capacity() * sizeof(std::uint32_t) +
           batches_.capacity() * sizeof(batch_t);
}
auto renderer_t::reserve_geometry(std::size_t vertices, std::size_t indices) -> bool {
    if (!geometry_limit_)
        return true;
    if (geometry_overflow_ || vertices > geometry_limit_ / sizeof(SDL_Vertex) ||
        indices > geometry_limit_ / sizeof(std::uint32_t)) {
        geometry_overflow_ = true;
        return false;
    }
    const auto vertex_count = vertices_.size() + vertices;
    const auto index_count = indices_.size() + indices;
    const auto batch_count = batches_.size() + 2;
    const auto vertex_capacity = vertex_count > vertices_.capacity()
                                     ? std::max(vertex_count, vertices_.capacity() * 2)
                                     : vertices_.capacity();
    const auto index_capacity = index_count > indices_.capacity()
                                    ? std::max(index_count, indices_.capacity() * 2)
                                    : indices_.capacity();
    const auto batch_capacity = batch_count > batches_.capacity()
                                    ? std::max(batch_count, batches_.capacity() * 2)
                                    : batches_.capacity();
    // Reserve retains old storage until each replacement completes. Count all
    // replacements against the limit, conservatively including simultaneous ones.
    const auto replacement =
        (vertex_capacity != vertices_.capacity() ? vertex_capacity * sizeof(SDL_Vertex) : 0) +
        (index_capacity != indices_.capacity() ? index_capacity * sizeof(std::uint32_t) : 0) +
        (batch_capacity != batches_.capacity() ? batch_capacity * sizeof(batch_t) : 0);
    if (geometry_bytes() > geometry_limit_ || replacement > geometry_limit_ - geometry_bytes()) {
        geometry_overflow_ = true;
        return false;
    }
    vertices_.reserve(vertex_capacity);
    indices_.reserve(index_capacity);
    batches_.reserve(batch_capacity);
    return true;
}
void renderer_t::quad(layer_t layer, std::span<const SDL_FPoint, 4> points, color_t color,
                      SDL_Texture* texture, SDL_FPoint uv_min, SDL_FPoint uv_max) {
    if (!reserve_geometry(4, 6))
        return;
    if (!texture) {
        texture = solid_.texture;
        uv_min = solid_.white_uv;
        uv_max = solid_.white_uv;
    }
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const std::array<SDL_FPoint, 4> uv{
        {uv_min, {uv_max.x, uv_min.y}, uv_max, {uv_min.x, uv_max.y}}};
    for (std::size_t index = 0; index < 4; ++index)
        vertices_.push_back({points[index], rgba(color), uv[index]});
    for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U})
        indices_.push_back(base + index);
    append_batch(layer, texture, 6);
}
void renderer_t::rect(layer_t layer, SDL_FRect bounds, color_t color) {
    if (color.a == 0 || bounds.w <= 0 || bounds.h <= 0)
        return;
    const auto left = std::round(bounds.x * scale_.x) / scale_.x;
    const auto top = std::round(bounds.y * scale_.y) / scale_.y;
    const auto right =
        std::max(left + 1.0f / scale_.x, std::round((bounds.x + bounds.w) * scale_.x) / scale_.x);
    const auto bottom =
        std::max(top + 1.0f / scale_.y, std::round((bounds.y + bounds.h) * scale_.y) / scale_.y);
    bounds = intersection({left, top, right - left, bottom - top}, viewport_);
    if (bounds.w <= 0 || bounds.h <= 0)
        return;
    const std::array<SDL_FPoint, 4> points{{{bounds.x, bounds.y},
                                            {bounds.x + bounds.w, bounds.y},
                                            {bounds.x + bounds.w, bounds.y + bounds.h},
                                            {bounds.x, bounds.y + bounds.h}}};
    quad(layer, points, color);
}
void renderer_t::hline(layer_t layer, float x0, float x1, float y, float width, color_t color) {
    rect(layer, {std::min(x0, x1), y - width * .5f, std::abs(x1 - x0), width}, color);
}
void renderer_t::vline(layer_t layer, float x, float y0, float y1, float width, color_t color) {
    rect(layer, {x - width * .5f, std::min(y0, y1), width, std::abs(y1 - y0)}, color);
}
void renderer_t::line(layer_t layer, SDL_FPoint a, SDL_FPoint b, float width, color_t color) {
    if (width <= 0 || color.a == 0)
        return;
    if (a.y == b.y) {
        hline(layer, a.x, b.x, a.y, width, color);
        return;
    }
    if (a.x == b.x) {
        vline(layer, a.x, a.y, b.y, width, color);
        return;
    }
    const auto offset = normal(a, b);
    const auto half = width * .5f;
    const std::array<SDL_FPoint, 4> points{{{a.x + offset.x * half, a.y + offset.y * half},
                                            {b.x + offset.x * half, b.y + offset.y * half},
                                            {b.x - offset.x * half, b.y - offset.y * half},
                                            {a.x - offset.x * half, a.y - offset.y * half}}};
    const auto clipped = intersection(bounds_of(points), viewport_);
    if (clipped.w > 0 && clipped.h > 0)
        quad(layer, points, color);
}
void renderer_t::polygon_fringe(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                                float width, std::uint32_t base) {
    color.a = 0;
    for (std::size_t index = 0; index < points.size(); ++index)
        vertices_.push_back({outward_corner(points, index, width * .5f), rgba(color), solid_.white_uv});
    const auto outer = base + static_cast<std::uint32_t>(points.size());
    for (std::uint32_t index = 0; index < points.size(); ++index) {
        const auto next = (index + 1U) % static_cast<std::uint32_t>(points.size());
        for (const auto vertex :
             {base + index, base + next, outer + next, base + index, outer + next, outer + index})
            indices_.push_back(vertex);
    }
    append_batch(layer, solid_.texture, points.size() * 6);
}
void renderer_t::polygon(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                         float aa_fringe) {
    if (points.size() < 3 || color.a == 0)
        return;
    const auto clipped = intersection(bounds_of(points), viewport_);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;
    if (!reserve_geometry(points.size()*(aa_fringe>0 ? 2 : 1),
        (points.size()-2)*3+(aa_fringe>0 ? points.size()*6 : 0))) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto point = aa_fringe > 0 ? outward_corner(points, index, -aa_fringe * .5f)
                                         : points[index];
        vertices_.push_back({point, rgba(color), solid_.white_uv});
    }
    // Compose arrows are star-shaped about their first (tip) vertex, matching
    // the pinned adapter's fan; callers retain their original point order.
    for (std::uint32_t index = 1; index + 1 < points.size(); ++index)
        for (const auto vertex : {base, base + index, base + index + 1})
            indices_.push_back(vertex);
    append_batch(layer, solid_.texture, (points.size() - 2) * 3);
    if (aa_fringe > 0)
        polygon_fringe(layer, points, color, aa_fringe, base);
}
void renderer_t::stroke_joined(layer_t layer, std::span<const SDL_FPoint> points, float width,
                               color_t color) {
    if (!reserve_geometry(points.size()*2,points.size()*6)) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        vertices_.push_back({outward_corner(points, index, width * .5f), rgba(color), solid_.white_uv});
        vertices_.push_back({outward_corner(points, index, -width * .5f), rgba(color), solid_.white_uv});
    }
    for (std::uint32_t index = 0; index < points.size(); ++index) {
        const auto current = base + index * 2;
        const auto next = base + ((index + 1) % static_cast<std::uint32_t>(points.size())) * 2;
        for (const auto vertex : {current, next, next + 1, current, next + 1, current + 1})
            indices_.push_back(vertex);
    }
    append_batch(layer, solid_.texture, points.size() * 6);
}
void renderer_t::stroke_fringe(layer_t layer, std::span<const SDL_FPoint> points, float width,
                                color_t color, float aa_fringe) {
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const bool thin = width <= aa_fringe;
    const std::uint32_t rings = thin ? 3 : 4;
    if (!reserve_geometry(points.size()*rings,points.size()*(rings-1)*6)) return;
    const auto inner = std::max(0.0f, (width - aa_fringe) * .5f);
    const std::array<float, 4> offsets{inner + aa_fringe, inner, -inner, -inner - aa_fringe};
    auto transparent = color;
    transparent.a = 0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        for (std::uint32_t ring = 0; ring < rings; ++ring) {
            const auto offset = thin ? aa_fringe * (1.0f - static_cast<float>(ring)) : offsets[ring];
            const auto shade = ring == 0 || ring + 1 == rings ? transparent : color;
            vertices_.push_back({outward_corner(points, index, offset), rgba(shade), solid_.white_uv});
        }
    }
    for (std::uint32_t index = 0; index < points.size(); ++index) {
        const auto current = base + index * rings;
        const auto next = base + ((index + 1) % static_cast<std::uint32_t>(points.size())) * rings;
        for (std::uint32_t ring = 0; ring + 1 < rings; ++ring)
            for (const auto vertex : {current + ring, next + ring, next + ring + 1,
                                      current + ring, next + ring + 1, current + ring + 1})
                indices_.push_back(vertex);
    }
    append_batch(layer, solid_.texture, points.size() * (rings - 1) * 6);
}
void renderer_t::stroke_polygon(layer_t layer, std::span<const SDL_FPoint> points, float width,
                                color_t color, float aa_fringe) {
    if (points.size() < 2 || width <= 0 || color.a == 0)
        return;
    const auto bounds = bounds_of(points);
    const auto margin = width * .5f + aa_fringe;
    const auto visible = intersection({bounds.x - margin, bounds.y - margin,
                                       bounds.w + margin * 2, bounds.h + margin * 2}, viewport_);
    if (visible.w <= 0 || visible.h <= 0)
        return;
    if (points.size() == 2) {
        line(layer, points.front(), points.back(), width, color);
        return;
    }
    if (aa_fringe > 0) {
        stroke_fringe(layer, points, width, color, aa_fringe);
        return;
    }
    stroke_joined(layer, points, width, color);
}
void renderer_t::glyph(layer_t layer, SDL_FRect bounds, color_t color, SDL_Texture* texture,
                       SDL_FPoint uv_min, SDL_FPoint uv_max) {
    if (bounds.w <= 0 || bounds.h <= 0)
        return;
    const auto clipped = intersection(bounds, viewport_);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;
    const SDL_FPoint first{clipped_uv(uv_min.x, uv_max.x, clipped.x - bounds.x, bounds.w),
                           clipped_uv(uv_min.y, uv_max.y, clipped.y - bounds.y, bounds.h)};
    const SDL_FPoint last{
        clipped_uv(uv_min.x, uv_max.x, clipped.x + clipped.w - bounds.x, bounds.w),
        clipped_uv(uv_min.y, uv_max.y, clipped.y + clipped.h - bounds.y, bounds.h)};
    const std::array<SDL_FPoint, 4> points{{{clipped.x, clipped.y},
                                            {clipped.x + clipped.w, clipped.y},
                                            {clipped.x + clipped.w, clipped.y + clipped.h},
                                            {clipped.x, clipped.y + clipped.h}}};
    quad(layer, points, color, texture, first, last);
}
void renderer_t::text(layer_t layer, SDL_FPoint pen, std::string_view utf8, color_t color,
                      const font_t& font) {
    if (!font.query || !font.texture || color.a == 0)
        return;
    while (!utf8.empty()) {
        const auto current = decode_utf8(utf8);
        utf8.remove_prefix(current.bytes);
        const auto next = decode_utf8(utf8);
        const auto shape = font.query(font.userdata, font.height, current.value, next.value);
        glyph(layer, glyph_bounds(shape, pen), color, font.texture, shape.uv_min, shape.uv_max);
        pen.x += shape.advance;
    }
}
auto renderer_t::workload() const noexcept -> workload_t {
    workload_t result{vertices_.size(), indices_.size(), batches_.size()};
    const auto tiles=tile_work();
    result.vertices+=tiles.raster_vertices;
    result.indices+=tiles.raster_indices;
    result.draw_calls+=tiles.raster_draws;
    for (const auto& cache : envelopes_) {
        if (!cache || !cache->active || !cache->pending || !cache->raster) continue;
        const auto work = cache->raster->workload();
        result.vertices += work.vertices;
        result.indices += work.indices;
        result.draw_calls += work.draw_calls;
    }
    return result;
}
auto renderer_t::submit(SDL_Renderer* renderer, submission_t hooks) const -> bool {
    return submit_range(renderer, 0, indices_.size(), hooks);
}
auto renderer_t::submit_envelope_fallback(SDL_Renderer* renderer, SDL_Texture* texture,
                                            submission_t hooks, bool& fallback) const -> bool {
    fallback = false;
    bool success = true;
        for (const auto& cache : envelopes_) {
            if (!cache || !cache->active || cache->texture != texture || cache->ready) continue;
            fallback = true;
            if (!cache->fallback_submitted) {
                success = draw_envelope_direct(*cache, renderer, viewport_, hooks) && success;
                cache->fallback_submitted = true;
            }
        }
    return success;
}
auto renderer_t::submit_range(SDL_Renderer* renderer, std::size_t first_index,
                              std::size_t index_count, submission_t hooks) const -> bool {
    if (!renderer || vertices_.empty() || index_count == 0)
        return renderer != nullptr;
    const auto draw = hooks.geometry ? hooks.geometry : SDL_RenderGeometryRaw;
    const auto clip = hooks.clip ? hooks.clip : SDL_SetRenderClipRect;
    const bool was_clipped = SDL_RenderClipEnabled(renderer);
    SDL_BlendMode previous_blend{};
    if (!SDL_GetRenderDrawBlendMode(renderer, &previous_blend))
        return false;
    SDL_Rect before{};
    if (!SDL_GetRenderClipRect(renderer, &before))
        return false;
    const SDL_Rect viewport{
        static_cast<int>(std::floor(viewport_.x)), static_cast<int>(std::floor(viewport_.y)),
        static_cast<int>(std::ceil(viewport_.x + viewport_.w) - std::floor(viewport_.x)),
        static_cast<int>(std::ceil(viewport_.y + viewport_.h) - std::floor(viewport_.y))};
    bool success = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    success = clip(renderer, &viewport) && success;
    for (const auto& cache : envelopes_) {
        if (cache && cache->active && cache->pending)
            refill_envelope(*cache, hooks);
    }
    success = clip(renderer, &viewport) && success;
    const auto& first = vertices_.front();
    const auto end = std::min(indices_.size(), first_index + index_count);
    for (const auto& batch : batches_) {
        const auto begin = std::max(first_index, batch.index_offset);
        const auto batch_end = std::min(end, batch.index_offset + batch.index_count);
        if (begin >= batch_end)
            continue;
        const auto region = intersection(batch.clip, viewport_);
        const SDL_Rect batch_clip{static_cast<int>(std::floor(region.x)),
            static_cast<int>(std::floor(region.y)),
            static_cast<int>(std::ceil(region.x+region.w)-std::floor(region.x)),
            static_cast<int>(std::ceil(region.y+region.h)-std::floor(region.y))};
        success = clip(renderer, &batch_clip) && success;
        bool fallback = false;
        success = submit_envelope_fallback(renderer, batch.texture, hooks, fallback) && success;
        if (fallback) continue;
        success = draw(renderer, batch.texture, &first.position.x, sizeof(SDL_Vertex), &first.color,
                       sizeof(SDL_Vertex), &first.tex_coord.x, sizeof(SDL_Vertex),
                       static_cast<int>(vertices_.size()), indices_.data() + begin,
                       static_cast<int>(batch_end - begin), sizeof(std::uint32_t)) &&
                  success;
    }
    const bool restored_clip = clip(renderer, was_clipped ? &before : nullptr);
    const bool restored_blend = SDL_SetRenderDrawBlendMode(renderer, previous_blend);
    return restored_clip && restored_blend && success;
}
} // namespace wormedit::ui::field
