#include "ui/nuklear/render/sdl_region_cache.h"
#include "ui/field/rect.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace wormedit::ui::nuklear {
namespace {
constexpr auto cpu_budget = sdl_region_cache_t::inner_cpu_reservation;
constexpr auto key_budget = cpu_budget * 3 / 8;
constexpr auto scratch_budget = cpu_budget / 4;
constexpr auto batch_budget = cpu_budget / 4;
constexpr auto transformed_budget = cpu_budget / 32;
constexpr auto output_budget = cpu_budget / 32;
// Frame storage is charged even before its compact mesh is rebuilt: texture
// admission must leave the entire shared CPU reservation available.
constexpr auto tile_external_bytes = field::tile_budget_t::limit - field::chrome_cache_reservation +
    sdl_region_cache_t::cpu_reservation;

auto same_rect(SDL_Rect a, SDL_Rect b) -> bool {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
auto same_scale(SDL_FPoint a, SDL_FPoint b) -> bool {
    return std::bit_cast<std::array<std::uint32_t, 2>>(a) ==
        std::bit_cast<std::array<std::uint32_t, 2>>(b);
}
auto same_color(SDL_Color a, SDL_Color b) -> bool {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

class physical_render_scope_t {
  public:
    explicit physical_render_scope_t(SDL_Renderer* renderer) : renderer_(renderer) {
        target_ = SDL_GetRenderTarget(renderer);
        viewport_set_ = SDL_RenderViewportSet(renderer);
        clipped_ = SDL_RenderClipEnabled(renderer);
        valid_ = SDL_GetRenderViewport(renderer, &viewport_) && SDL_GetRenderClipRect(renderer, &clip_) &&
            SDL_GetRenderScale(renderer, &scale_.x, &scale_.y) &&
            SDL_GetRenderLogicalPresentation(renderer, &logical_width_, &logical_height_, &mode_) &&
            SDL_GetRenderDrawBlendMode(renderer, &blend_) &&
            SDL_GetRenderDrawColorFloat(renderer, &color_.r, &color_.g, &color_.b, &color_.a);
    }
    auto enter() -> bool {
        return valid_ && SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
            SDL_SetRenderScale(renderer_, 1, 1) && SDL_SetRenderViewport(renderer_, nullptr) &&
            SDL_SetRenderClipRect(renderer_, nullptr);
    }
    ~physical_render_scope_t() {
        if (!valid_) return;
        SDL_SetRenderTarget(renderer_, target_);
        SDL_SetRenderLogicalPresentation(renderer_, logical_width_, logical_height_, mode_);
        SDL_SetRenderScale(renderer_, scale_.x, scale_.y);
        SDL_SetRenderViewport(renderer_, viewport_set_ ? &viewport_ : nullptr);
        SDL_SetRenderClipRect(renderer_, clipped_ ? &clip_ : nullptr);
        SDL_SetRenderDrawBlendMode(renderer_, blend_);
        SDL_SetRenderDrawColorFloat(renderer_, color_.r, color_.g, color_.b, color_.a);
    }
  private:
    SDL_Renderer* renderer_{};
    SDL_Texture* target_{};
    SDL_Rect viewport_{}, clip_{};
    SDL_FPoint scale_{};
    SDL_FColor color_{};
    SDL_BlendMode blend_{};
    SDL_RendererLogicalPresentation mode_{};
    int logical_width_{}, logical_height_{};
    bool valid_{}, viewport_set_{}, clipped_{};
};

auto logical_clip(SDL_Rect logical, SDL_FPoint scale, SDL_Rect output) -> SDL_Rect {
    const SDL_Rect viewport{0,0,static_cast<int>(std::ceil(output.w/scale.x)),
        static_cast<int>(std::ceil(output.h/scale.y))};
    SDL_Rect result{};
    SDL_GetRectIntersection(&logical,&viewport,&result);
    return result;
}

auto physical_clip(SDL_Rect logical, SDL_FPoint scale, SDL_Rect output) -> SDL_Rect {
    const auto clipped = logical_clip(logical,scale,output);
    if (clipped.w <= 0 || clipped.h <= 0) return {};
    // Match SDL's independent origin/extent rounding, rather than rounding the
    // right edge, which differs at fractional display scales.
    const SDL_Rect physical{static_cast<int>(std::floor(clipped.x * scale.x)),
        static_cast<int>(std::floor(clipped.y * scale.y)),
        static_cast<int>(std::ceil(clipped.w * scale.x)),
        static_cast<int>(std::ceil(clipped.h * scale.y))};
    SDL_Rect result{};
    SDL_GetRectIntersection(&physical, &output, &result);
    return result;
}

auto finite_vertex(const sdl_command_vertex_t& vertex) -> bool {
    const auto fields = std::bit_cast<std::array<float, 8>>(vertex);
    return std::ranges::all_of(fields, [](float value) { return std::isfinite(value); });
}

auto batch_bounds(sdl_command_mesh_view_t mesh, const sdl_command_batch_t& batch,
                  SDL_FPoint scale) -> std::optional<SDL_FRect> {
    float left = std::numeric_limits<float>::max(), top = left;
    float right = std::numeric_limits<float>::lowest(), bottom = right;
    for (const auto index : mesh.indices.subspan(batch.first_index, batch.index_count)) {
        const auto& vertex = mesh.vertices[index];
        const float x = vertex.position[0]*scale.x, y = vertex.position[1]*scale.y;
        if (!finite_vertex(vertex) || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
        left = std::min(left,x); right = std::max(right,x);
        top = std::min(top,y); bottom = std::max(bottom,y);
    }
    return SDL_FRect{left,top,right-left,bottom-top};
}

auto is_supported_blending(const sdl_region_request_t& request, SDL_Renderer* renderer) -> bool {
    SDL_BlendMode mode{};
    if (request.immutable_texture &&
        (!SDL_GetTextureBlendMode(request.immutable_texture,&mode) || mode != SDL_BLENDMODE_BLEND)) return false;
    for (const auto& batch : request.mesh.batches) {
        if (batch.texture && batch.texture != request.immutable_texture) {
            if (std::ranges::find(request.immutable_images,batch.texture) == request.immutable_images.end() ||
                !SDL_GetTextureBlendMode(batch.texture,&mode) || mode != SDL_BLENDMODE_BLEND) return false;
        }
        if (!batch.texture && batch.index_count &&
            (!SDL_GetRenderDrawBlendMode(renderer,&mode) || mode != SDL_BLENDMODE_BLEND)) return false;
    }
    return true;
}
} // namespace

struct sdl_region_cache_t::entry_t {
    sdl_command_mesh_t mesh;
    std::vector<SDL_Rect> physical_clips;
    field::renderer_t output;
    field::tile_request_t request;
    SDL_Rect bounds{};
    SDL_Color background{};
    SDL_Texture* texture{};
    SDL_FPoint scale{1,1};
    std::uint64_t texture_epoch{}, image_epoch{};
    bool valid{}, ready{};
    auto is_equal_to(const sdl_region_request_t& source) const -> bool {
        return valid && same_scale(scale, source.scale) &&
            same_color(background, source.opaque_background) && texture == source.immutable_texture &&
            texture_epoch == source.texture_epoch && image_epoch == source.image_epoch && mesh.is_equal_to(source.mesh);
    }
};

sdl_region_cache_t::sdl_region_cache_t() = default;
sdl_region_cache_t::~sdl_region_cache_t() = default;

void sdl_region_cache_t::begin(SDL_Renderer* renderer, SDL_Rect output, std::uint64_t epoch,
    std::size_t maximum_preparations) {
    if (!tiles_ || renderer_ != renderer || epoch_ != epoch || !same_rect(output_, output)) {
        entries_ = {};
        tiles_ = std::make_unique<field::field_tile_cache_t>();
    }
    renderer_ = renderer; output_ = output; epoch_ = epoch;
    const auto* name = renderer ? SDL_GetRendererName(renderer) : nullptr;
    software_renderer_ = name && std::strcmp(name,"software") == 0;
    hits_ = misses_ = 0; submitted_ = {};
    tiles_->begin(renderer,maximum_preparations);
    tiles_->account_source(tile_external_bytes);
    for (auto& entry : entries_)
        if (entry) entry->ready = false;
}

auto sdl_region_cache_t::bounds_for(sdl_command_mesh_view_t mesh, SDL_FPoint scale) const -> SDL_Rect {
    if (!mesh.is_valid() || !std::isfinite(scale.x) || !std::isfinite(scale.y) ||
        scale.x < .125f || scale.y < .125f || scale.x > 8 || scale.y > 8 ||
        output_.w <= 0 || output_.h <= 0 || output_.w > 32768 || output_.h > 32768) return {};
    SDL_FRect combined{};
    bool populated{};
    for (const auto& batch : mesh.batches) {
        if (!batch.index_count) continue;
        const auto geometry = batch_bounds(mesh,batch,scale);
        if (!geometry) return {};
        const auto clip = physical_clip(batch.clip,scale,output_);
        const auto visible = rect_intersection(*geometry,
            {static_cast<float>(clip.x),static_cast<float>(clip.y),static_cast<float>(clip.w),static_cast<float>(clip.h)});
        if (visible.w <= 0 || visible.h <= 0) continue;
        if (!populated) combined = visible;
        else SDL_GetRectUnionFloat(&combined,&visible,&combined);
        populated = true;
    }
    if (!populated) return {};
    return {static_cast<int>(std::floor(combined.x)),static_cast<int>(std::floor(combined.y)),
        static_cast<int>(std::ceil(combined.x+combined.w)-std::floor(combined.x)),
        static_cast<int>(std::ceil(combined.y+combined.h)-std::floor(combined.y))};
}

auto sdl_region_cache_t::is_available(const sdl_region_request_t& source) const -> bool {
    if (!renderer_ || !tiles_ || source.slot >= maximum_regions || !source.mesh.is_valid() ||
        output_.x != 0 || output_.y != 0 || output_.w <= 0 || output_.h <= 0 ||
        source.opaque_background.a != 255 ||
        !std::isfinite(source.scale.x) || !std::isfinite(source.scale.y) ||
        source.scale.x < .125f || source.scale.y < .125f || source.scale.x > 8 || source.scale.y > 8 ||
        SDL_GetRenderTarget(renderer_) || SDL_RenderViewportSet(renderer_)) return false;
    // SDL's software geometry path can rewrite quads as integer rectangles,
    // rounding origins/extents differently after fractional tile translation.
    // Preserve exact source rendering through the caller's direct fallback.
    if (software_renderer_ && (source.scale.x != std::trunc(source.scale.x) ||
        source.scale.y != std::trunc(source.scale.y))) return false;
    int width{}, height{};
    SDL_RendererLogicalPresentation mode{};
    SDL_FPoint scale;
    return SDL_GetRenderLogicalPresentation(renderer_, &width, &height, &mode) &&
        mode == SDL_LOGICAL_PRESENTATION_DISABLED && SDL_GetRenderScale(renderer_, &scale.x, &scale.y) &&
        same_scale(scale, source.scale) && is_supported_blending(source,renderer_);
}

auto sdl_region_cache_t::refresh(entry_t& entry, const sdl_region_request_t& source) -> bool {
    entry.valid = false;
    const auto bounds = bounds_for(source.mesh,source.scale);
    if (bounds.w < 16 || bounds.h <= 0 || bounds.w > 4096 || bounds.h > 4096) return false;
    std::size_t other_keys{};
    for (const auto& existing : entries_)
        if (existing && existing.get() != &entry)
            other_keys += existing->mesh.retained_bytes() + existing->physical_clips.capacity()*sizeof(SDL_Rect);
    const auto clip_count = std::max(entry.physical_clips.capacity(),source.mesh.batches.size());
    if (other_keys >= key_budget || clip_count > (key_budget-other_keys)/sizeof(SDL_Rect)) return false;
    const auto remaining = key_budget-other_keys-clip_count*sizeof(SDL_Rect);
    auto logical = source.mesh;
    logical.limits.maximum_bytes = std::min(logical.limits.maximum_bytes,remaining);
    if (!entry.mesh.refill(logical)) return false;
    entry.physical_clips.reserve(source.mesh.batches.size());
    if (entry.mesh.retained_bytes() + entry.physical_clips.capacity()*sizeof(SDL_Rect) > key_budget-other_keys) {
        entry.mesh = {}; std::vector<SDL_Rect>{}.swap(entry.physical_clips);
        return false;
    }
    entry.bounds = bounds; entry.background = source.opaque_background;
    entry.texture = source.immutable_texture; entry.texture_epoch = source.texture_epoch;
    entry.image_epoch = source.image_epoch; entry.scale = source.scale;
    const auto mesh = entry.mesh.view();
    entry.physical_clips.clear();
    for (const auto& batch : mesh.batches)
        entry.physical_clips.push_back(physical_clip(batch.clip, source.scale, output_));
    entry.request = {};
    entry.request.key.identity[0] = source.slot + 1;
    entry.request.key.identity[1] = ++generation_;
    entry.request.key.pass = 1;
    entry.request.viewport = {static_cast<float>(entry.bounds.x), static_cast<float>(entry.bounds.y),
        static_cast<float>(entry.bounds.w), static_cast<float>(entry.bounds.h)};
    entry.valid = true;
    return true;
}

void sdl_region_cache_t::paint(entry_t& entry, double origin) {
    const auto width = static_cast<float>(entry.bounds.w);
    scratch_.begin({0,0,width,512});
    scratch_.rect(1,{0,0,width,512}, {entry.background.r,entry.background.g,entry.background.b,255});
    const auto mesh = entry.mesh.view();
    for (std::size_t index = 0; index < mesh.batches.size(); ++index) {
        const auto& batch = mesh.batches[index];
        if (!batch.index_count) continue;
        auto source = sdl_command_mesh_view_t{mesh.vertices,mesh.indices,{&batch,1},mesh.limits};
        source.limits.maximum_bytes = std::min(source.limits.maximum_bytes,batch_budget);
        if (!batch_mesh_.refill(source)) { scratch_.limit_geometry(1); return; }
        const auto compact = batch_mesh_.view();
        if (compact.vertices.size() > transformed_budget/sizeof(SDL_Vertex)) { scratch_.limit_geometry(1); return; }
        transformed_.reserve(compact.vertices.size());
        if (transformed_.capacity()*sizeof(SDL_Vertex) > transformed_budget) {
            std::vector<SDL_Vertex>{}.swap(transformed_); scratch_.limit_geometry(1); return;
        }
        transformed_.clear();
        for (const auto& vertex : compact.vertices) {
            float x = vertex.position[0]*entry.scale.x, y = vertex.position[1]*entry.scale.y;
            transformed_.push_back({{x-entry.bounds.x,y-entry.bounds.y-static_cast<float>(origin)},
                {vertex.col[0],vertex.col[1],vertex.col[2],vertex.col[3]}, {vertex.uv[0],vertex.uv[1]}});
        }
        const auto clip = entry.physical_clips[index];
        const auto previous = scratch_.push_clip({static_cast<float>(clip.x-entry.bounds.x),
            clip.y-entry.bounds.y-static_cast<float>(origin),static_cast<float>(clip.w),static_cast<float>(clip.h)});
        if (!scratch_.triangles(1,transformed_,compact.indices,batch.texture)) scratch_.limit_geometry(1);
        scratch_.pop_clip(previous);
    }
}

auto sdl_region_cache_t::prepare(const sdl_region_request_t& source) -> bool {
    if (source.slot < entries_.size() && entries_[source.slot]) entries_[source.slot]->ready = false;
    if (!is_available(source)) { ++misses_; return false; }
    auto& pointer = entries_[source.slot];
    if (!pointer) pointer = std::make_unique<entry_t>();
    auto& entry = *pointer;
    entry.ready = false;
    if (!entry.is_equal_to(source) && !refresh(entry, source)) { ++misses_; return false; }
    if (!tiles_->is_ready(entry.request)) {
        physical_render_scope_t scope(renderer_);
        if (!scope.enter()) { ++misses_; return false; }
        scratch_.limit_geometry(scratch_budget);
        const auto covered_end = std::ceil(entry.bounds.h / 512.0) * 512;
        tiles_->prepare(scratch_,entry.request,0,covered_end,tile_external_bytes,
            [this,&entry](field::renderer_t&, double origin) { paint(entry,origin); });
    }
    if (!tiles_->is_ready(entry.request)) { ++misses_; return false; }
    entry.output.limit_geometry(output_budget/maximum_regions);
    entry.output.begin(entry.request.viewport);
    entry.ready = tiles_->composite(entry.output,entry.request);
    if (entry.ready) ++hits_; else ++misses_;
    return entry.ready;
}

auto sdl_region_cache_t::submit_background(const entry_t& entry, field::submission_t hooks) -> bool {
    physical_render_scope_t scope(renderer_);
    if (!scope.enter() || !SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE)) return false;
    const auto bounds = entry.bounds;
    const float left = bounds.x, top = bounds.y, right = bounds.x+bounds.w, bottom = bounds.y+bounds.h;
    const SDL_FColor color{entry.background.r/255.0f,entry.background.g/255.0f,entry.background.b/255.0f,1};
    const std::array<SDL_Vertex,4> vertices{{{{left,top},color,{}},{{right,top},color,{}},
        {{right,bottom},color,{}},{{left,bottom},color,{}}}};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto draw = hooks.geometry ? hooks.geometry : SDL_RenderGeometryRaw;
    const auto clip = hooks.clip ? hooks.clip : SDL_SetRenderClipRect;
    if (!clip(renderer_,&bounds)) return false;
    submitted_.vertices += vertices.size(); submitted_.indices += indices.size(); ++submitted_.draw_calls;
    return draw(renderer_,nullptr,&vertices[0].position.x,sizeof(SDL_Vertex),&vertices[0].color,sizeof(SDL_Vertex),
        &vertices[0].tex_coord.x,sizeof(SDL_Vertex),vertices.size(),indices.data(),indices.size(),sizeof(std::uint32_t));
}

auto sdl_region_cache_t::submit_original(const entry_t& entry, field::submission_t hooks) -> bool {
    const auto mesh = entry.mesh.view();
    const auto draw = hooks.geometry ? hooks.geometry : SDL_RenderGeometryRaw;
    const auto clip = hooks.clip ? hooks.clip : SDL_SetRenderClipRect;
    const bool clipped = SDL_RenderClipEnabled(renderer_);
    SDL_Rect before{};
    if (!SDL_GetRenderClipRect(renderer_,&before)) return false;
    submitted_.vertices += mesh.vertices.size();
    bool success = true;
    for (const auto& batch : mesh.batches) {
        if (!batch.index_count) continue;
        const auto bounds = logical_clip(batch.clip,entry.scale,output_);
        success = clip(renderer_,&bounds) && success;
        submitted_.indices += batch.index_count; ++submitted_.draw_calls;
        const auto& vertex = mesh.vertices.front();
        success = draw(renderer_,batch.texture,vertex.position,sizeof(sdl_command_vertex_t),
            reinterpret_cast<const SDL_FColor*>(vertex.col),sizeof(sdl_command_vertex_t),vertex.uv,
            sizeof(sdl_command_vertex_t),mesh.vertices.size(),mesh.indices.data()+batch.first_index,
            batch.index_count,sizeof(sdl_command_index_t)) && success;
    }
    return clip(renderer_,clipped ? &before : nullptr) && success;
}

auto sdl_region_cache_t::submit(std::size_t slot, field::submission_t hooks) -> bool {
    if (slot >= entries_.size() || !entries_[slot] || !entries_[slot]->ready) return false;
    auto& entry = *entries_[slot];
    bool cached{};
    {
        physical_render_scope_t scope(renderer_);
        if (scope.enter()) {
            const auto work = entry.output.workload();
            submitted_.vertices += work.vertices; submitted_.indices += work.indices; submitted_.draw_calls += work.draw_calls;
            cached = entry.output.submit(renderer_,hooks);
        }
    }
    if (cached) return true;
    // Failed cache draws may have written part of the region. Restore its
    // opaque parent before replaying original logical geometry and scissors.
    return submit_background(entry,hooks) && submit_original(entry,hooks);
}

auto sdl_region_cache_t::retained_cpu_bytes() const -> std::size_t {
    auto bytes = transformed_.capacity()*sizeof(SDL_Vertex) + scratch_.geometry_bytes() + batch_mesh_.retained_bytes();
    for (const auto& entry : entries_) {
        if (!entry) continue;
        bytes += entry->mesh.retained_bytes() + entry->physical_clips.capacity()*sizeof(SDL_Rect) + entry->output.geometry_bytes();
    }
    return bytes;
}

auto sdl_region_cache_t::work() const -> sdl_region_work_t {
    return {tiles_ ? tiles_->work() : field::tile_work_t{}, submitted_, hits_, misses_, retained_cpu_bytes()};
}
} // namespace wormedit::ui::nuklear
