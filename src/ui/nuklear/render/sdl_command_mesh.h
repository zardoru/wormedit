#pragma once

#include <wormedit/nuklear_config.h>
#include <SDL3/SDL.h>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace wormedit::ui::nuklear {

// Canonical converted layout used by Nuklear's SDL geometry adapter.
struct sdl_command_vertex_t {
    float position[2];
    float uv[2];
    float col[4];
};
using sdl_command_index_t = nk_draw_index;
static_assert(sizeof(sdl_command_index_t) == 4);
static_assert(sizeof(sdl_command_vertex_t) == 8 * sizeof(float));

struct sdl_command_batch_t {
    SDL_Texture* texture{};
    SDL_Rect clip{};
    std::size_t first_index{}, index_count{};
};
struct sdl_command_mesh_limits_t {
    std::size_t maximum_vertices{65536}, maximum_indices{196608}, maximum_batches{1024};
    // Bounds source buffer bytes and, separately, ALL owned capacities,
    // including normalization offsets and remapping scratch.
    std::size_t maximum_bytes{8U * 1024U * 1024U};
};
struct sdl_command_mesh_view_t {
    std::span<const sdl_command_vertex_t> vertices;
    std::span<const sdl_command_index_t> indices;
    std::span<const sdl_command_batch_t> batches;
    sdl_command_mesh_limits_t limits;

    static auto from(std::span<const sdl_command_vertex_t> vertices,
                     std::span<const sdl_command_index_t> indices,
                     std::span<const sdl_command_batch_t> batches,
                     sdl_command_mesh_limits_t limits = {})
        -> std::optional<sdl_command_mesh_view_t>;
    auto is_valid() const -> bool;
};

// CPU data only. Prefix offsets are ignored; changed internal vertex spacing
// conservatively misses. Equality reads only referenced vertex field bits.
class sdl_command_mesh_t {
  public:
    static auto from(sdl_command_mesh_view_t source) -> std::optional<sdl_command_mesh_t>;
    auto refill(sdl_command_mesh_view_t source, std::span<const unsigned char> omitted = {}) -> bool;
    auto is_equal_to(sdl_command_mesh_view_t source) const -> bool;
    auto view() const -> sdl_command_mesh_view_t;
    auto retained_bytes() const -> std::size_t;

  private:
    std::vector<sdl_command_vertex_t> vertices_;
    std::vector<sdl_command_index_t> indices_;
    std::vector<sdl_command_batch_t> batches_;
    std::vector<sdl_command_index_t> source_offsets_, remap_;
    sdl_command_mesh_limits_t limits_;
    auto overlaps_storage(sdl_command_mesh_view_t source,
                          std::span<const unsigned char> omitted) const -> bool;
    auto can_reserve(std::size_t vertices, std::size_t indices, std::size_t batches,
                     std::size_t remap, std::size_t byte_limit) const -> bool;
    void reserve(std::size_t vertices, std::size_t indices, std::size_t batches, std::size_t remap);
    void release();
    void append_batch(sdl_command_mesh_view_t source, const sdl_command_batch_t& batch,
                      std::size_t minimum, bool omitted);
};

} // namespace wormedit::ui::nuklear
