#pragma once

#include "ui/nuklear/render/sdl_region_cache.h"
#include "ui/nuklear/render/sdl_region_scope.h"
#include <optional>

namespace wormedit::ui::nuklear {
struct sdl_region_frame_context_t {
    SDL_Renderer* renderer{};
    SDL_Texture* font{};
    std::span<SDL_Texture* const> immutable_images;
    std::uint64_t texture_epoch{}, image_epoch{};
    std::size_t maximum_preparations{2};
};
// Per-frame barriers and compact direct geometry share the backend lifetime.
class sdl_region_frame_t {
  public:
    sdl_region_frame_t() { reset(); }
    void reset();
    auto begin(nk_context*, sdl_region_t, nk_color, nk_command_custom_callback) -> bool;
    void end(nk_context*, sdl_region_t, nk_command_custom_callback);
    auto prepare(nk_context*, nk_buffer* commands, sdl_command_mesh_view_t source,
                 const sdl_region_frame_context_t&) -> bool;
    auto is_marker(const nk_draw_command*) const -> bool;
    auto cached_slot(const nk_draw_command*) const -> std::optional<std::size_t>;
    auto direct_mesh() const -> sdl_command_mesh_view_t { return direct_.view(); }
    auto submit(std::size_t slot, field::submission_t hooks) -> bool {
        return ready_ && cache_.submit(slot, hooks);
    }
    auto work() const -> sdl_region_work_t;

  private:
    struct marker_t { std::size_t slot{}; bool end{}; };
    struct region_t {
        marker_t begin, end;
        nk_color background{};
        std::size_t first{}, count{};
        bool started{}, closed{}, valid{}, hit{};
    };
    std::array<region_t, static_cast<std::size_t>(sdl_region_t::generator) + 1> regions_{};
    std::vector<sdl_command_batch_t> batches_;
    std::vector<unsigned char> omitted_;
    sdl_command_mesh_t direct_;
    sdl_region_cache_t cache_;
    // Preparation can do real offscreen work even if direct compaction fails;
    // account that work, but publish/submit cached regions only when ready.
    bool prepared_{}, ready_{};
    auto marker(const nk_draw_command*) const -> const marker_t*;
    auto collect(nk_context*, nk_buffer*) -> bool;
    auto metadata_bytes() const -> std::size_t;
    void prepare_region(std::size_t, sdl_command_mesh_view_t, SDL_FPoint,
                        const sdl_region_frame_context_t&);
};
} // namespace wormedit::ui::nuklear
