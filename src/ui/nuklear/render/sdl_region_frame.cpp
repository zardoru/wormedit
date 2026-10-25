#include "ui/nuklear/render/sdl_region_frame.h"
#include <algorithm>

namespace wormedit::ui::nuklear {

auto sdl_region_frame_t::metadata_bytes() const -> std::size_t {
    return sizeof(*this) + batches_.capacity() * sizeof(sdl_command_batch_t) + omitted_.capacity();
}
auto sdl_region_frame_t::work() const -> sdl_region_work_t {
    const auto retained = cache_.work();
    auto result = prepared_ ? retained : sdl_region_work_t{};
    result.retained_cpu_bytes = retained.retained_cpu_bytes + direct_.retained_bytes() + metadata_bytes();
    result.tiles.retained_bytes = retained.tiles.retained_bytes;
    return result;
}

void sdl_region_frame_t::reset() {
    prepared_ = ready_ = false;
    for (std::size_t slot = 0; slot < regions_.size(); ++slot) {
        regions_[slot] = {.begin = {.slot = slot}, .end = {.slot = slot, .end = true}};
    }
    batches_.clear(); omitted_.clear();
}

auto sdl_region_frame_t::begin(nk_context* context, sdl_region_t id, nk_color background,
                              nk_command_custom_callback callback) -> bool {
    const auto slot = static_cast<std::size_t>(id);
    if (slot >= regions_.size() || regions_[slot].started) return false;
    auto& region = regions_[slot];
    region.started = region.valid = true;
    region.background = background;
    nk_push_custom(nk_window_get_canvas(context), nk_window_get_bounds(context), callback,
                   nk_handle_ptr(&region.begin));
    return true;
}
void sdl_region_frame_t::end(nk_context* context, sdl_region_t id,
                             nk_command_custom_callback callback) {
    const auto slot = static_cast<std::size_t>(id);
    if (slot >= regions_.size()) return;
    auto& region = regions_[slot];
    if (!region.started || region.closed) return;
    region.closed = true;
    nk_push_custom(nk_window_get_canvas(context), nk_window_get_bounds(context), callback,
                   nk_handle_ptr(&region.end));
}
auto sdl_region_frame_t::marker(const nk_draw_command* command) const -> const marker_t* {
    for (const auto& region : regions_) {
        if (!region.started) continue;
        if (command->userdata.ptr == &region.begin) return &region.begin;
        if (command->userdata.ptr == &region.end) return &region.end;
    }
    return nullptr;
}
auto sdl_region_frame_t::is_marker(const nk_draw_command* command) const -> bool {
    return marker(command) != nullptr;
}
auto sdl_region_frame_t::cached_slot(const nk_draw_command* command) const
    -> std::optional<std::size_t> {
    const auto* barrier = marker(command);
    if (!ready_ || !barrier || barrier->end || !regions_[barrier->slot].hit) return std::nullopt;
    return barrier->slot;
}

auto sdl_region_frame_t::collect(nk_context* context, nk_buffer* commands) -> bool {
    batches_.clear();
    std::optional<std::size_t> active;
    std::size_t offset{};
    const nk_draw_command* command;
    nk_draw_foreach(command, context, commands) {
        if (const auto* barrier = marker(command)) {
            if (command->elem_count) return false;
            auto& region = regions_[barrier->slot];
            if (barrier->end) {
                if (!active || *active != barrier->slot) return false;
                region.count = batches_.size() - region.first;
                active.reset();
            } else {
                if (active) return false;
                active = barrier->slot;
                region.first = batches_.size();
            }
            continue;
        }
        if (active && command->userdata.ptr != context->userdata.ptr)
            regions_[*active].valid = false;
        if (batches_.size() >= 1024) return false;
        const auto clip = command->clip_rect;
        batches_.push_back({.texture = static_cast<SDL_Texture*>(command->texture.ptr),
            .clip = {static_cast<int>(clip.x), static_cast<int>(clip.y),
                     static_cast<int>(clip.w), static_cast<int>(clip.h)},
            .first_index = offset, .index_count = command->elem_count});
        offset += command->elem_count;
    }
    return !active;
}

void sdl_region_frame_t::prepare_region(std::size_t slot, sdl_command_mesh_view_t source,
                                        SDL_FPoint scale, const sdl_region_frame_context_t& frame) {
    auto& region = regions_[slot];
    region.hit = false;
    if (!region.started || !region.closed || !region.valid || !region.count) return;
    source.batches = source.batches.subspan(region.first, region.count);
    const auto color = region.background;
    region.hit = cache_.prepare({.slot = slot, .mesh = source,
        .opaque_background = {color.r, color.g, color.b, color.a},
        .immutable_texture = frame.font, .immutable_images = frame.immutable_images,
        .scale = scale, .texture_epoch = frame.texture_epoch, .image_epoch = frame.image_epoch});
    if (region.hit) std::fill_n(omitted_.begin()+region.first, region.count, 1);
}
auto sdl_region_frame_t::prepare(nk_context* context, nk_buffer* commands,
                                 sdl_command_mesh_view_t source, const sdl_region_frame_context_t& frame) -> bool {
    prepared_ = ready_ = false;
    for (auto& region : regions_) region.hit = false;
    if (!collect(context, commands)) return false;
    source.batches = batches_;
    source.limits.maximum_bytes = std::min(source.limits.maximum_bytes,
        sdl_region_cache_t::frame_geometry_budget);
    omitted_.assign(batches_.size(), 0);
    constexpr auto metadata_limit = sdl_region_cache_t::frame_reservation -
        sdl_region_cache_t::frame_geometry_budget;
    if (metadata_bytes() > metadata_limit) {
        std::vector<sdl_command_batch_t>{}.swap(batches_);
        std::vector<unsigned char>{}.swap(omitted_);
        return false;
    }
    if (!source.is_valid()) return false;
    SDL_Rect output{};
    SDL_FPoint scale{};
    if (!SDL_GetRenderOutputSize(frame.renderer, &output.w, &output.h) ||
        !SDL_GetRenderScale(frame.renderer, &scale.x, &scale.y)) return false;
    cache_.begin(frame.renderer, output, frame.texture_epoch, frame.maximum_preparations);
    prepared_ = true;
    for (std::size_t slot = 0; slot < regions_.size(); ++slot)
        prepare_region(slot, source, scale, frame);
    ready_ = direct_.refill(source, omitted_);
    if (!ready_)
        for (auto& region : regions_) region.hit = false;
    return ready_;
}
} // namespace wormedit::ui::nuklear
