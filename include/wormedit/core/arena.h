#pragma once

#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace wormedit {

using arena_allocation_outcome_t = result_t<void*, failure_code_t>;

struct arena_options_t {
    std::size_t chunk_bytes{64 * 1024};
    std::size_t maximum_bytes{16 * 1024 * 1024};
    std::size_t maximum_objects{250'000};
};

struct arena_statistics_t {
    std::size_t committed_bytes{};
    // Physical bump usage, including alignment padding.
    std::size_t used_bytes{};
    std::size_t object_count{};
    std::size_t chunk_count{};
};

// Where a new allocation may be placed. Frame arenas look through every
// committed chunk (wrapping from the last one used) so reset() can reuse them;
// append arenas only use the newest chunk so data stays in allocation order.
enum class arena_fit_policy_t : std::uint8_t {
    any_chunk,
    newest_chunk,
};

// Chunked, aligned bump allocation shared by every arena policy. Chunks are
// separately allocated and never move, so returned addresses stay valid until
// the policy rewinds or drops the chunk. Failures return typed errors; nothing
// here throws.
class chunk_arena_core_t {
  public:
    struct chunk_t {
        std::shared_ptr<std::byte[]> storage;
        std::size_t capacity{};
        std::size_t used{};
    };

    struct allocation_location_t {
        std::byte* data{};
        std::size_t chunk_index{};
        std::size_t offset{};

        auto operator==(const allocation_location_t&) const -> bool = default;
    };
    using allocation_outcome_t = result_t<allocation_location_t, failure_code_t>;

    // A null resource means std::pmr::new_delete_resource(). The resource must
    // outlive every chunk, including chunks kept alive by append-arena handles.
    explicit chunk_arena_core_t(arena_options_t options = {},
                                std::pmr::memory_resource* resource = nullptr) noexcept;

    chunk_arena_core_t(chunk_arena_core_t&& other) noexcept;
    auto operator=(chunk_arena_core_t&& other) noexcept -> chunk_arena_core_t&;
    chunk_arena_core_t(const chunk_arena_core_t&) = delete;
    auto operator=(const chunk_arena_core_t&) -> chunk_arena_core_t& = delete;
    ~chunk_arena_core_t() = default;

    [[nodiscard]] auto allocate(std::size_t size, std::size_t alignment,
                                arena_fit_policy_t policy) noexcept -> allocation_outcome_t;

    // Rewinds every chunk to empty and keeps it committed.
    void rewind() noexcept;
    // Raises the allocation ceiling without moving or releasing existing chunks.
    void grow_maximum_bytes(std::size_t maximum_bytes) noexcept;

    [[nodiscard]] auto options() const noexcept -> const arena_options_t&;
    [[nodiscard]] auto resource() const noexcept -> std::pmr::memory_resource*;
    [[nodiscard]] auto statistics() const noexcept -> arena_statistics_t;
    [[nodiscard]] auto chunks() const noexcept -> std::span<const chunk_t>;

  private:
    [[nodiscard]] auto commit(std::size_t index, std::size_t begin, std::size_t size) noexcept
        -> allocation_location_t;
    struct placement_t {
        std::size_t chunk_index{};
        std::size_t begin{};
    };

    [[nodiscard]] auto find_existing(std::size_t size, std::size_t alignment,
                                     arena_fit_policy_t policy) const noexcept
        -> std::optional<placement_t>;
    [[nodiscard]] auto add_chunk(std::size_t size, std::size_t alignment) noexcept
        -> std::optional<failure_code_t>;

    arena_options_t options_;
    std::pmr::memory_resource* resource_{};
    std::vector<chunk_t> chunks_;
    std::size_t committed_bytes_{};
    std::size_t used_bytes_{};
    std::size_t object_count_{};
    std::size_t chunk_index_{};
};

// Per-frame scratch: allocations are stable until reset(); reset keeps
// committed chunks so a warmed-up frame allocates nothing.
class frame_arena_t {
  public:
    explicit frame_arena_t(arena_options_t options = {},
                           std::pmr::memory_resource* resource = nullptr) noexcept;
    ~frame_arena_t();

    frame_arena_t(frame_arena_t&&) noexcept;
    auto operator=(frame_arena_t&&) noexcept -> frame_arena_t&;
    frame_arena_t(const frame_arena_t&) = delete;
    auto operator=(const frame_arena_t&) -> frame_arena_t& = delete;

    [[nodiscard]] auto allocate(std::size_t size,
                                std::size_t alignment = alignof(void*)) noexcept
        -> arena_allocation_outcome_t;

    void reset() noexcept;

    [[nodiscard]] auto object_count() const noexcept -> std::size_t;
    [[nodiscard]] auto committed_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto used_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto statistics() const noexcept -> arena_statistics_t;

  private:
    std::unique_ptr<chunk_arena_core_t> core_;
};

// Shared ownership of one append-arena chunk. Holding a handle keeps the chunk
// alive after the arena that created it is compacted or destroyed.
using arena_chunk_handle_t = std::shared_ptr<const std::byte[]>;

struct append_arena_span_t {
    std::span<std::byte> bytes;
    arena_chunk_handle_t chunk;
    std::size_t chunk_index{};
    std::size_t offset{};
    std::size_t alignment{1};
};

using append_arena_outcome_t = result_t<append_arena_span_t, failure_code_t>;

static_assert(sizeof(std::optional<failure_code_t>) <= 2 * sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<std::optional<failure_code_t>>);

struct arena_compaction_failure_t {
    failure_code_t code{failure_code_t::allocation_failed};
    std::optional<failure_code_t> allocation_error;
    // Cleared on failure, retaining the legacy result's remap capacity.
    std::vector<append_arena_span_t> remap;
};
static_assert(sizeof(arena_compaction_failure_t) <=
              sizeof(std::vector<append_arena_span_t>) + alignof(std::vector<append_arena_span_t>));
struct append_arena_snapshot_t;
using append_arena_compaction_outcome_t =
    result_t<append_arena_snapshot_t, arena_compaction_failure_t>;

// Append-only storage: spans never move or get reused. Callers report
// replaced data with mark_stale(); compact() copies the live spans into a
// fresh arena. Not thread-safe; chunk handles may be shared across threads.
class append_arena_t {
  public:
    explicit append_arena_t(arena_options_t options = {},
                            std::pmr::memory_resource* resource = nullptr) noexcept;

    append_arena_t(append_arena_t&& other) noexcept;
    auto operator=(append_arena_t&& other) noexcept -> append_arena_t&;
    append_arena_t(const append_arena_t&) = delete;
    auto operator=(const append_arena_t&) -> append_arena_t& = delete;
    ~append_arena_t() = default;

    [[nodiscard]] auto append_span(std::size_t size,
                                   std::size_t alignment = alignof(void*)) noexcept
        -> append_arena_outcome_t;

    // Adds payload bytes to the stale count, clamped to appended payload.
    // Alignment padding is physical usage, not live or stale payload.
    void mark_stale(std::size_t bytes) noexcept;
    void grow_maximum_bytes(std::size_t maximum_bytes) noexcept;

    [[nodiscard]] auto stale_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto live_bytes() const noexcept -> std::size_t;
    // Stale / appended payload, or 0 when no payload has been appended.
    [[nodiscard]] auto stale_ratio() const noexcept -> double;
    // Physical chunk usage, including alignment padding.
    [[nodiscard]] auto statistics() const noexcept -> arena_statistics_t;
    [[nodiscard]] auto chunk_handle(std::size_t chunk_index) const noexcept
        -> arena_chunk_handle_t;

    // Copies live spans (all from this arena) into a new arena with the same
    // options and resource. `order` lists indices into `live_spans` in copy
    // order and must be a permutation; empty means input order. remap[i] is the
    // new location of live_spans[i]. This arena is left unchanged.
    [[nodiscard]] auto build_compaction(std::span<const append_arena_span_t> live_spans,
                                        std::span<const std::size_t> order = {}) const noexcept
        -> append_arena_compaction_outcome_t;

  private:
    [[nodiscard]] auto has_allocation(const append_arena_span_t& span) const noexcept -> bool;

    chunk_arena_core_t core_;
    std::size_t payload_bytes_{};
    std::size_t stale_bytes_{};
};

struct append_arena_snapshot_t {
    append_arena_t arena;
    std::vector<append_arena_span_t> remap;
};

} // namespace wormedit
