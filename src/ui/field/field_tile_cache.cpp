#include "ui/field/rect.h"
#include "ui/field/field_tile_cache.h"
#include "ui/field/field_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace wormedit::ui::field {
namespace {
auto can_retain_logical_tiles(SDL_Renderer* renderer) -> bool {
    if (!renderer) return false;
    const auto* name=SDL_GetRendererName(renderer);
    if (!name || SDL_strcmp(name,"software")!=0) return true;
    float x{},y{};
    return SDL_GetRenderScale(renderer,&x,&y) && x==std::trunc(x) && y==std::trunc(y);
}
// Pass ten is the BGA lane. Both admission paths share this rotation so every
// static layer eventually receives the same bounded preparation opportunity.
auto can_admit_pass(unsigned pass, std::uint64_t frame) -> bool {
    constexpr unsigned pass_count = 10;
    const auto admitted = static_cast<unsigned>((frame - 1) % pass_count + 1);
    return pass == admitted || pass == admitted % pass_count + 1;
}
struct raster_geometry_t {
    std::vector<SDL_Vertex> transformed_vertices;
    std::vector<std::uint32_t> transformed_indices;
    std::span<const SDL_Vertex> vertices;
    std::span<const std::uint32_t> indices;
    auto initialize(const renderer_t& field, std::size_t first_index, const tile_request_t& request,
                    double offset) -> bool {
        const auto selected = field.indices().subspan(first_index);
        const auto first_vertex =
            selected.empty() ? field.vertices().size()
                             : static_cast<std::size_t>(*std::ranges::min_element(selected));
        const auto source = field.vertices().subspan(first_vertex);
        vertices = source;
        indices = selected;
        if (first_vertex || request.viewport.x || request.viewport.y || offset) {
            const auto bytes =
                source.size() * sizeof(SDL_Vertex) + selected.size() * sizeof(std::uint32_t);
            if (bytes > tile_geometry_scratch_budget)
                return false;
            transformed_vertices.assign(source.begin(), source.end());
            transformed_indices.assign(selected.begin(), selected.end());
            for (auto& index : transformed_indices)
                index -= static_cast<std::uint32_t>(first_vertex);
            for (auto& vertex : transformed_vertices) {
                vertex.position.x -= request.viewport.x;
                vertex.position.y -= request.viewport.y + static_cast<float>(offset);
            }
            vertices = transformed_vertices;
            indices = transformed_indices;
        }
        return true;
    }
};
void subtract_rectangle(SDL_FRect bounds, SDL_FRect hole, std::vector<SDL_FRect>& output) {
    const auto overlap = rect_intersection(bounds, hole);
    const auto left = overlap.x, top = overlap.y;
    const auto right = left + overlap.w, bottom = top + overlap.h;
    if (right <= left || bottom <= top) {
        output.push_back(bounds);
        return;
    }
    const auto append = [&](float x, float y, float width, float height) {
        if (width > 0 && height > 0)
            output.push_back({x, y, width, height});
    };
    append(bounds.x, bounds.y, bounds.w, top - bounds.y);
    append(bounds.x, bottom, bounds.w, bounds.y + bounds.h - bottom);
    append(bounds.x, top, left - bounds.x, bottom - top);
    append(right, top, bounds.x + bounds.w - right, bottom - top);
}
} // namespace
auto subtract_rectangles(SDL_FRect bounds, std::span<const SDL_FRect> holes)
    -> std::vector<SDL_FRect> {
    std::vector<SDL_FRect> output{bounds};
    for (const auto hole : holes) {
        std::vector<SDL_FRect> next;
        for (const auto region : output)
            subtract_rectangle(region, hole, next);
        output = std::move(next);
    }
    return output;
}
auto tile_budget_t::reserve(std::size_t bytes) noexcept -> bool {
    if (!can_prepare() || bytes > allowance() || bytes_ > allowance() - bytes) return false;
    bytes_ += bytes;
    ++prepared_;
    return true;
}
void tile_budget_t::release(std::size_t bytes) noexcept {
    bytes_ -= std::min(bytes_, bytes);
}
struct field_tile_cache_t::entry_t {
    tile_key_t key;
    SDL_Texture* texture{};
    std::uint64_t used{};
    std::size_t bytes{};
    int begin{512}, end{}, width{};
    ~entry_t() { SDL_DestroyTexture(texture); }
};
field_tile_cache_t::field_tile_cache_t() = default;
field_tile_cache_t::~field_tile_cache_t() = default;
void field_tile_cache_t::begin(SDL_Renderer* device, std::size_t maximum_preparations) {
    // Software's quad shortcut and intermediate glyph sampling do not retain
    // fractional-scale coverage. Keep its original direct rendering path.
    if (!can_retain_logical_tiles(device)) device=nullptr;
    if (device != device_) {
        entries_.clear();
        budget_ = {};
        device_ = device;
    }
    ++frame_;
    budget_.begin(maximum_preparations);
    work_ = {};
}
void field_tile_cache_t::account_source(std::size_t bytes) {
    budget_.external(bytes);
    evict(0);
}
auto field_tile_cache_t::find(const tile_key_t& key) -> entry_t* {
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) {
        return entry->key == key;
    });
    if (found == entries_.end()) return nullptr;
    (*found)->used = frame_;
    return found->get();
}
void field_tile_cache_t::evict(std::size_t bytes) {
    while ((bytes > budget_.allowance() || budget_.bytes() > budget_.allowance() - bytes) && !entries_.empty()) {
        const auto oldest = std::ranges::min_element(entries_, {}, [](const auto& entry) {
            return entry->used;
        });
        // Textures referenced by this frame's already queued geometry must live.
        if ((*oldest)->used == frame_) return;
        budget_.release((*oldest)->bytes);
        entries_.erase(oldest);
    }
}
auto field_tile_cache_t::acquire(const tile_key_t& key, int width) -> entry_t* {
    if (auto* entry = find(key)) return entry;
    const auto bytes = static_cast<std::size_t>(width) * 512 * 4;
    evict(bytes);
    if (!budget_.reserve(bytes)) return nullptr;
    auto entry = std::make_unique<entry_t>();
    entry->key = key;
    entry->width = width;
    entry->bytes = bytes;
    entry->used = frame_;
    entry->texture = SDL_CreateTexture(device_, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_TARGET, width, 512);
    if (!entry->texture) { budget_.release(bytes); return nullptr; }
    SDL_SetTextureScaleMode(entry->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(entry->texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    auto* result = entry.get();
    entries_.push_back(std::move(entry));
    return result;
}
namespace {
auto visible_tiles(const tile_request_t& request) -> std::pair<std::int64_t, std::int64_t> {
    return {static_cast<std::int64_t>(std::floor(request.origin / 512)),
            static_cast<std::int64_t>(std::floor((request.origin + request.viewport.h - .001) / 512))};
}
auto tile_clip(const tile_request_t& request, std::int64_t tile) -> std::pair<int, int> {
    const auto start = tile * 512.0;
    return {std::max(0, static_cast<int>(std::floor(request.origin - start))),
            std::min(512, static_cast<int>(std::ceil(request.origin + request.viewport.h - start)))};
}
}
auto field_tile_cache_t::missing(const tile_request_t& request) -> std::vector<SDL_FRect> {
    std::vector<SDL_FRect> result;
    if (!device_ || !std::isfinite(request.origin) || std::abs(request.origin)>1e12 ||
        request.viewport.w<=0 || request.viewport.w>4096 || request.viewport.h<=0 ||
        request.viewport.h>4096) return {request.viewport};
    const auto [first,last] = visible_tiles(request);
    for (auto tile=first; tile<=last; ++tile) {
        auto key=request.key; key.tile=tile;
        const auto* entry=find(key);
        const auto [begin,end]=tile_clip(request,tile);
        const auto y=request.viewport.y+static_cast<float>(tile*512.0-request.origin);
        const auto append=[&](int start,int finish) {
            const auto top=std::max(request.viewport.y,y+start);
            const auto bottom=std::min(request.viewport.y+request.viewport.h,y+finish);
            if (bottom>top) result.push_back({request.viewport.x,top,request.viewport.w,bottom-top});
        };
        if (!entry || entry->end<=begin || entry->begin>=end) { append(begin,end); continue; }
        append(begin,std::min(end,entry->begin));
        append(std::max(begin,entry->end),end);
    }
    return result;
}
auto field_tile_cache_t::is_ready(const tile_request_t& request) -> bool {
    if (!device_ || !std::isfinite(request.origin) || std::abs(request.origin)>1e12 ||
        request.viewport.w<=0 || request.viewport.w>4096 || request.viewport.h<=0 ||
        request.viewport.h>4096) return false;
    const auto [first,last] = visible_tiles(request);
    for (auto tile = first; tile <= last; ++tile) {
        auto key = request.key; key.tile = tile;
        const auto* entry = find(key);
        const auto [begin,end] = tile_clip(request,tile);
        if (!entry || entry->begin > begin || entry->end < end) return false;
    }
    return true;
}
auto field_tile_cache_t::composite(renderer_t& field, const tile_request_t& request,
                                   std::span<const SDL_FRect> holes) -> bool {
    if (!device_ || !std::isfinite(request.origin) || std::abs(request.origin)>1e12 ||
        request.viewport.w<=0 || request.viewport.w>4096 || request.viewport.h<=0 ||
        request.viewport.h>4096) return false;
    const auto [first,last]=visible_tiles(request);
    for (auto tile=first; tile<=last; ++tile) {
        auto key=request.key; key.tile=tile;
        auto* entry=find(key);
        if (!entry) continue;
        const auto y=request.viewport.y+static_cast<float>(tile*512.0-request.origin);
        const auto top=std::max(request.viewport.y,y+entry->begin);
        const auto bottom=std::min(request.viewport.y+request.viewport.h,y+entry->end);
        if (bottom<=top) continue;
        const SDL_FRect bounds{request.viewport.x,top,request.viewport.w,bottom-top};
        if (holes.empty()) {
            field.cached_quad(request.key.pass,bounds,entry->texture,
                {0,(top-y)/512}, {request.viewport.w/entry->width,(bottom-y)/512});
            ++work_.composites;
            continue;
        }
        for (const auto region:subtract_rectangles(bounds,holes)) {
            field.cached_quad(request.key.pass,region,entry->texture,
                {(region.x-request.viewport.x)/entry->width,(region.y-y)/512},
                {(region.x+region.w-request.viewport.x)/entry->width,(region.y+region.h-y)/512});
            ++work_.composites;
        }
    }
    const bool ready=is_ready(request);
    if (ready) ++work_.hits; else ++work_.misses;
    return ready;
}
auto field_tile_cache_t::raster(entry_t& entry, const renderer_t& field,
                              const tile_request_t& request, std::size_t first_index,
                              int begin, int end) -> bool {
    auto* previous_target = SDL_GetRenderTarget(device_);
    SDL_BlendMode previous_blend{};
    SDL_GetRenderDrawBlendMode(device_, &previous_blend);
    Uint8 red{},green{},blue{},alpha{};
    SDL_GetRenderDrawColor(device_, &red,&green,&blue,&alpha);
    SDL_Rect previous_clip{};
    const bool clipped = SDL_RenderClipEnabled(device_);
    SDL_GetRenderClipRect(device_, &previous_clip);
    if (field.geometry_overflow()) return false;
    raster_geometry_t geometry;
    const auto offset=entry.key.tile*512.0-request.origin;
    if (!geometry.initialize(field,first_index,request,offset)) return false;
    const auto vertices=geometry.vertices;
    const auto indices=geometry.indices;
    if (!SDL_SetRenderTarget(device_, entry.texture)) return false;
    bool success=true;
    const SDL_Rect clip{0, begin, entry.width, end-begin};
    success = SDL_SetRenderClipRect(device_, &clip) && success;
    SDL_SetRenderDrawColor(device_, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(device_, SDL_BLENDMODE_NONE);
    success = SDL_RenderFillRect(device_, nullptr) && success;
    work_.raster_vertices+=vertices.size()+4;
    work_.raster_indices+=indices.size()+6;
    ++work_.raster_draws;
    SDL_SetRenderDrawBlendMode(device_, SDL_BLENDMODE_BLEND);
    for (const auto& batch : field.batches()) {
        const auto start = std::max(first_index, batch.index_offset);
        const auto finish = batch.index_offset + batch.index_count;
        if (start >= finish) continue;
        const auto& source_clip = batch.clip;
        const SDL_FRect translated_clip{source_clip.x-request.viewport.x,
            source_clip.y-request.viewport.y-static_cast<float>(offset),source_clip.w,source_clip.h};
        const SDL_Rect batch_clip{static_cast<int>(std::floor(translated_clip.x)),
            static_cast<int>(std::floor(translated_clip.y)),
            static_cast<int>(std::ceil(translated_clip.x+translated_clip.w)-std::floor(translated_clip.x)),
            static_cast<int>(std::ceil(translated_clip.y+translated_clip.h)-std::floor(translated_clip.y))};
        SDL_Rect intersection{};
        if (!SDL_GetRectIntersection(&clip,&batch_clip,&intersection)) continue;
        success = SDL_SetRenderClipRect(device_, &intersection) && success;
        const auto& front = vertices.front();
        success = SDL_RenderGeometryRaw(device_, batch.texture, &front.position.x, sizeof(SDL_Vertex),
            &front.color, sizeof(SDL_Vertex), &front.tex_coord.x, sizeof(SDL_Vertex),
            static_cast<int>(vertices.size()), indices.data()+start-first_index,
            static_cast<int>(finish-start), sizeof(std::uint32_t)) && success;
        ++work_.raster_draws;
    }
    success = SDL_SetRenderTarget(device_, previous_target) && success;
    success = SDL_SetRenderClipRect(device_, clipped ? &previous_clip : nullptr) && success;
    success = SDL_SetRenderDrawBlendMode(device_, previous_blend) && success;
    success = SDL_SetRenderDrawColor(device_, red,green,blue,alpha) && success;
    return success;
}
void field_tile_cache_t::capture(const renderer_t& field, const tile_request_t& request,
                                std::size_t first_index) {
    // Rotate admitted passes so an exposed background strip cannot starve
    // dense notes and timing during continuous following.
    if (!can_admit_pass(request.key.pass, frame_)) return;
    if (!device_ || request.viewport.w <= 0 || request.viewport.w > 4096 ||
        request.viewport.h<=0 || request.viewport.h>4096 ||
        !std::isfinite(request.origin) || std::abs(request.origin)>1e12) return;
    const auto [first, last] = visible_tiles(request);
    for (auto tile = first; tile <= last && budget_.can_prepare(); ++tile) {
        auto key = request.key;
        key.tile = tile;
        auto* existing = find(key);
        const auto [begin, end] = tile_clip(request, tile);
        if (existing && begin >= existing->begin && end <= existing->end) continue;
        auto* entry = acquire(key, static_cast<int>(std::ceil(request.viewport.w)));
        if (!entry) continue;
        if (existing && !budget_.reserve(0)) continue;
        // Repaint the current visible intersection, preserving pixels outside it.
        if (!raster(*entry, field, request, first_index, begin, end)) {
            entry->begin = 512;
            entry->end = 0;
            continue;
        }
        if (end < entry->begin || begin > entry->end) { entry->begin=begin; entry->end=end; }
        entry->begin = std::min(begin, entry->begin);
        entry->end = std::max(end, entry->end);
        ++work_.prepared;
    }
}
void field_tile_cache_t::prepare(renderer_t& scratch, const tile_request_t& request,
                                 double covered_begin, double covered_end,
                                 std::size_t source_bytes,
                                 const std::function<void(renderer_t&, double)>& paint) {
    account_source(source_bytes);
    if (!device_ || !std::isfinite(request.origin) || std::abs(request.origin)>1e12 ||
        request.viewport.w<=0 || request.viewport.w>4096 || request.viewport.h<=0 ||
        request.viewport.h>4096 || !std::isfinite(covered_begin) ||
        !std::isfinite(covered_end) || source_bytes >= tile_budget_t::limit) return;
    if (!can_admit_pass(request.key.pass, frame_)) return;
    const auto [first,last]=visible_tiles(request);
    for (auto tile=first; tile<=last && budget_.can_prepare(); ++tile) {
        const auto origin=tile*512.0;
        if (origin<covered_begin || origin+512>covered_end) continue;
        auto key=request.key; key.tile=tile;
        auto* existing=find(key);
        if (existing && existing->begin==0 && existing->end==512) continue;
        auto* entry=acquire(key, static_cast<int>(std::ceil(request.viewport.w)));
        if (!entry || (existing && !budget_.reserve(0))) continue;
        paint(scratch, origin);
        const auto painted=scratch.viewport();
        if (!scratch.translate_geometry({-painted.x,-painted.y})) continue;
        auto full=request;
        full.origin=origin;
        full.viewport=scratch.viewport();
        if (!raster(*entry,scratch,full,0,0,512)) {
            entry->begin=512; entry->end=0;
            continue;
        }
        entry->begin=0; entry->end=512;
        ++work_.prepared;
    }
}
auto field_tile_cache_t::work() const noexcept -> tile_work_t {
    auto result = work_;
    result.retained_bytes = budget_.bytes();
    return result;
}
}
