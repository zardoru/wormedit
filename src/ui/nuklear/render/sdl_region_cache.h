#pragma once

#include "ui/nuklear/render/sdl_command_mesh.h"
#include "ui/field/field_renderer.h"
#include <array>
#include <cstdint>
#include <memory>

namespace wormedit::ui::nuklear {

struct sdl_region_request_t {
    std::size_t slot{};
    sdl_command_mesh_view_t mesh;
    SDL_Color opaque_background{};
    SDL_Texture* immutable_texture{};
    std::span<SDL_Texture* const> immutable_images;
    SDL_FPoint scale{1, 1};
    std::uint64_t texture_epoch{}, image_epoch{};
};
struct sdl_region_work_t {
    field::tile_work_t tiles;
    field::workload_t submitted;
    std::size_t hits{}, misses{}, retained_cpu_bytes{};
};

// One tile budget and preparation allowance serve every chrome slot. Widgets
// stay live; callers submit ready regions at their original command barriers.
class sdl_region_cache_t {
  public:
    static constexpr std::size_t maximum_regions = 16;
    // One chrome CPU reservation includes both this owner and its frame's
    // compact direct mesh, command batches, omission mask and bookkeeping.
    static constexpr std::size_t cpu_reservation = field::chrome_cache_reservation / 4;
    static constexpr std::size_t frame_reservation = cpu_reservation / 4;
    static constexpr std::size_t frame_geometry_budget = frame_reservation - 64U * 1024U;
    static constexpr std::size_t inner_cpu_reservation = cpu_reservation - frame_reservation;
    sdl_region_cache_t();
    ~sdl_region_cache_t();
    void begin(SDL_Renderer*, SDL_Rect physical_output, std::uint64_t renderer_scale_epoch,
               std::size_t maximum_preparations = 2);
    auto bounds_for(sdl_command_mesh_view_t, SDL_FPoint scale) const -> SDL_Rect;
    auto prepare(const sdl_region_request_t&) -> bool;
    auto submit(std::size_t slot, field::submission_t hooks = {}) -> bool;
    auto work() const -> sdl_region_work_t;

  private:
    struct entry_t;
    std::array<std::unique_ptr<entry_t>, maximum_regions> entries_;
    std::unique_ptr<field::field_tile_cache_t> tiles_;
    field::renderer_t scratch_;
    sdl_command_mesh_t batch_mesh_;
    std::vector<SDL_Vertex> transformed_;
    SDL_Renderer* renderer_{};
    bool software_renderer_{};
    SDL_Rect output_{};
    std::uint64_t epoch_{}, generation_{};
    std::size_t hits_{}, misses_{};
    field::workload_t submitted_;
    auto is_available(const sdl_region_request_t&) const -> bool;
    auto refresh(entry_t&, const sdl_region_request_t&) -> bool;
    void paint(entry_t&, double origin);
    auto retained_cpu_bytes() const -> std::size_t;
    auto submit_background(const entry_t&, field::submission_t) -> bool;
    auto submit_original(const entry_t&, field::submission_t) -> bool;
};

} // namespace wormedit::ui::nuklear
