#pragma once
#include <SDL3/SDL.h>
#include "common/static_interval_index.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <span>
#include <functional>
namespace wormedit::ui::field {
class renderer_t;
inline constexpr std::size_t chrome_cache_reservation = 16U * 1024U * 1024U;
inline constexpr std::size_t tile_geometry_scratch_budget = 4U * 1024U * 1024U;
using common::static_interval_t;
using common::interval_query_t;
using common::static_interval_index_t;
// Disjoint rectangle complements preserve alpha compositing at dynamic holes.
auto subtract_rectangles(SDL_FRect bounds, std::span<const SDL_FRect> holes)
    -> std::vector<SDL_FRect>;
struct tile_key_t {
    std::array<std::uint64_t, 16> identity{};
    std::array<std::uint64_t,2> style{};
    std::int64_t tile{};
    std::uint32_t pass{};
    auto operator==(const tile_key_t&) const -> bool = default;
};
struct tile_request_t {
    tile_key_t key;
    SDL_FRect viewport{};
    double origin{};
};
struct tile_work_t {
    std::size_t hits{}, misses{}, prepared{}, retained_bytes{}, composites{};
    std::size_t raster_vertices{}, raster_indices{}, raster_draws{};
};
// Plain accounting is shared by the SDL cache and its device-independent tests.
class tile_budget_t {
  public:
    static constexpr std::size_t limit = 64 * 1024 * 1024;
    void begin(std::size_t maximum_preparations = 2) noexcept {
        prepared_ = 0; maximum_preparations_ = std::min(maximum_preparations, std::size_t{2});
    }
    auto reserve(std::size_t bytes) noexcept -> bool;
    void release(std::size_t bytes) noexcept;
    void external(std::size_t bytes) noexcept { external_ = std::min(bytes, limit); }
    auto bytes() const noexcept -> std::size_t { return bytes_; }
    auto prepared() const noexcept -> std::size_t { return prepared_; }
    auto can_prepare() const noexcept -> bool { return prepared_ < maximum_preparations_; }
  private:
    std::size_t bytes_{}, prepared_{}, external_{}, maximum_preparations_{2};
  public:
    auto allowance() const noexcept -> std::size_t { return limit - external_; }
};
class field_tile_cache_t {
  public:
    field_tile_cache_t();
    ~field_tile_cache_t();
    void begin(SDL_Renderer* device, std::size_t maximum_preparations = 2);
    void account_source(std::size_t bytes);
    auto composite(renderer_t& field, const tile_request_t& request,
                   std::span<const SDL_FRect> holes = {}) -> bool;
    auto is_ready(const tile_request_t& request) -> bool;
    auto can_retain() const noexcept -> bool { return device_ != nullptr; }
    auto missing(const tile_request_t& request) -> std::vector<SDL_FRect>;
    void capture(const renderer_t& field, const tile_request_t& request,
                 std::size_t first_index);
    auto work() const noexcept -> tile_work_t;
    void prepare(renderer_t& scratch, const tile_request_t& request, double covered_begin,
                 double covered_end, std::size_t source_bytes,
                 const std::function<void(renderer_t&, double)>& paint);
  private:
    struct entry_t;
    std::vector<std::unique_ptr<entry_t>> entries_;
    SDL_Renderer* device_{};
    tile_budget_t budget_;
    tile_work_t work_;
    std::uint64_t frame_{};
    auto find(const tile_key_t& key) -> entry_t*;
    auto acquire(const tile_key_t& key, int width) -> entry_t*;
    void evict(std::size_t bytes);
    auto raster(entry_t& entry, const renderer_t& field, const tile_request_t& request,
                std::size_t first_index, int begin, int end) -> bool;
};
}
