#include <wormedit/core/arena.h>
#include <wormedit/core/storyboard.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <new>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {

using namespace wormedit;

static_assert(std::is_same_v<storyboard_frame_arena_t, frame_arena_t>);
static_assert(std::is_same_v<storyboard_arena_options_t, arena_options_t>);
static_assert(std::is_same_v<storyboard_arena_error_t, arena_error_t>);
static_assert(std::is_same_v<storyboard_arena_allocation_t, arena_allocation_t>);
static_assert(std::is_nothrow_constructible_v<frame_arena_t>);
static_assert(std::is_nothrow_constructible_v<frame_arena_t, arena_options_t,
                                             std::pmr::memory_resource*>);

// Counts every chunk and control-block allocation the arenas make.
class counting_resource_t final : public std::pmr::memory_resource {
  public:
    std::size_t allocations{};
    std::size_t deallocations{};
    std::size_t outstanding_bytes{};
    bool fail{};

  private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        if (fail)
            throw std::bad_alloc();
        ++allocations;
        outstanding_bytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        ++deallocations;
        outstanding_bytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] auto do_is_equal(const std::pmr::memory_resource& other) const noexcept
        -> bool override {
        return this == &other;
    }
};

auto address_of(const void* pointer) -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

auto stats_tuple(const arena_statistics_t& statistics) {
    return std::tuple{statistics.committed_bytes, statistics.used_bytes, statistics.object_count,
                      statistics.chunk_count};
}

void fill(const append_arena_span_t& span, std::uint8_t seed) {
    for (std::size_t index = 0; index < span.bytes.size(); ++index)
        span.bytes[index] = static_cast<std::byte>(seed + index);
}

auto holds_pattern(std::span<const std::byte> bytes, std::uint8_t seed) -> bool {
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (bytes[index] != static_cast<std::byte>(seed + index))
            return false;
    }
    return true;
}

// Returns the alignments whose allocation came back misaligned or failed.
auto misaligned_alignments(frame_arena_t& arena) -> std::vector<std::size_t> {
    std::vector<std::size_t> failures;
    for (std::size_t alignment = 1; alignment <= alignof(std::max_align_t); alignment *= 2) {
        const auto padding = arena.allocate_bytes(1, 1);
        const auto allocation = arena.allocate_bytes(3, alignment);
        if (!padding || !allocation || address_of(allocation.data) % alignment != 0)
            failures.push_back(alignment);
    }
    return failures;
}

auto run_frame(frame_arena_t& arena) -> std::vector<std::uintptr_t> {
    std::vector<std::uintptr_t> addresses;
    for (std::size_t index = 0; index < 20; ++index)
        addresses.push_back(address_of(arena.allocate_bytes(24 + index, 8).data));
    return addresses;
}

// Counts frames whose reset did not rewind or whose addresses moved.
auto frames_differing_after_reset(frame_arena_t& arena, const std::vector<std::uintptr_t>& warm,
                                  int frames) -> int {
    int differing = 0;
    for (int frame = 0; frame < frames; ++frame) {
        arena.reset();
        const bool rewound = arena.used_bytes() == 0 && arena.object_count() == 0;
        if (!rewound || run_frame(arena) != warm)
            ++differing;
    }
    return differing;
}

auto append_patterned(append_arena_t& arena, std::size_t count, std::size_t first_size,
                      std::size_t size_step, std::uint8_t seed_step)
    -> std::vector<append_arena_span_t> {
    std::vector<append_arena_span_t> spans;
    for (std::size_t index = 0; index < count; ++index) {
        auto result = arena.append(first_size + index * size_step, 4);
        if (!result)
            return {};
        fill(result.span, static_cast<std::uint8_t>(index * seed_step));
        spans.push_back(std::move(result.span));
    }
    return spans;
}

// True when every span is 4-aligned, has a chunk handle, holds its pattern,
// and lies after its predecessor in memory order.
auto spans_ordered_and_intact(const std::vector<append_arena_span_t>& spans) -> bool {
    for (std::size_t index = 0; index < spans.size(); ++index) {
        const auto& current = spans[index];
        if (address_of(current.bytes.data()) % 4 != 0 || current.chunk == nullptr ||
            !holds_pattern(current.bytes, static_cast<std::uint8_t>(index)))
            return false;
        if (index == 0)
            continue;
        const auto& previous = spans[index - 1];
        if (current.chunk_index < previous.chunk_index)
            return false;
        if (current.chunk_index == previous.chunk_index &&
            current.offset < previous.offset + previous.bytes.size())
            return false;
    }
    return true;
}

auto remap_matches(std::span<const append_arena_span_t> live,
                   std::span<const append_arena_span_t> remap,
                   std::span<const std::uint8_t> seeds) -> bool {
    for (std::size_t index = 0; index < live.size(); ++index) {
        const auto& moved = remap[index];
        if (moved.bytes.size() != live[index].bytes.size() ||
            moved.bytes.data() == live[index].bytes.data() ||
            address_of(moved.bytes.data()) % 4 != 0 || !holds_pattern(moved.bytes, seeds[index]))
            return false;
    }
    return true;
}

// Appends two chunks, snapshots the first chunk, compacts, and destroys every
// arena. Returns the snapshotted bytes, or an empty span when setup failed.
auto snapshot_then_compact(counting_resource_t& resource,
                           std::vector<arena_chunk_handle_t>& snapshot)
    -> std::span<const std::byte> {
    append_arena_t arena{{.chunk_bytes = 64, .maximum_bytes = 4096, .maximum_objects = 10},
                         &resource};
    auto first = arena.append(48, 8);
    if (!first || !arena.append(48, 8) || arena.statistics().chunk_count != 2)
        return {};
    fill(first.span, 7);
    snapshot.push_back(first.span.chunk);
    auto compacted = arena.compact(std::span{&first.span, 1});
    if (!compacted)
        return {};
    arena = std::move(compacted.arena);
    return first.span.bytes;
}

} // namespace

TEST_CASE("frame arena aligns every power-of-two request and rejects other alignments",
          "[unit][arena][frame]") {
    frame_arena_t arena{{.chunk_bytes = 256, .maximum_bytes = 4096, .maximum_objects = 64}};
    CHECK(misaligned_alignments(arena).empty());
    const auto objects = arena.object_count();
    CHECK(std::tuple{arena.allocate_bytes(1, 0).error, arena.allocate_bytes(1, 3).error,
                     arena.allocate_bytes(1, alignof(std::max_align_t) * 2).error} ==
          std::tuple{arena_error_t::invalid_alignment, arena_error_t::invalid_alignment,
                     arena_error_t::invalid_alignment});
    CHECK(arena.object_count() == objects);
}

TEST_CASE("frame arena enforces byte and object limits without partial allocation",
          "[unit][arena][frame][capacity]") {
    frame_arena_t bytes{{.chunk_bytes = 16, .maximum_bytes = 32, .maximum_objects = 10}};
    REQUIRE(bytes.allocate_bytes(16, 1));
    REQUIRE(bytes.allocate_bytes(16, 1));
    const auto before = bytes.statistics();
    CHECK(bytes.allocate_bytes(1, 1).error == arena_error_t::byte_limit);
    CHECK(stats_tuple(bytes.statistics()) == stats_tuple(before));
    CHECK(before.chunk_count == 2);
}

TEST_CASE("frame arena enforces its object limit", "[unit][arena][frame][capacity]") {
    frame_arena_t objects{{.chunk_bytes = 64, .maximum_bytes = 64, .maximum_objects = 3}};
    const bool filled = objects.allocate_bytes(1, 1) && objects.allocate_bytes(1, 1) &&
                        objects.allocate_bytes(1, 1);
    REQUIRE(filled);
    CHECK(objects.allocate_bytes(1, 1).error == arena_error_t::object_limit);
    CHECK(objects.object_count() == 3);
}

TEST_CASE("frame arena reports out of memory instead of throwing", "[unit][arena][frame]") {
    counting_resource_t resource;
    resource.fail = true;
    frame_arena_t arena{{.chunk_bytes = 64, .maximum_bytes = 1024, .maximum_objects = 8},
                        &resource};
    const auto allocation = arena.allocate_bytes(8, 8);
    CHECK(std::tuple{allocation.data, allocation.error} ==
          std::tuple{static_cast<void*>(nullptr), arena_error_t::out_of_memory});
    CHECK(stats_tuple(arena.statistics()) == stats_tuple({}));
}

TEST_CASE("frame arena reset reuses warmed chunks without allocating", "[unit][arena][frame]") {
    counting_resource_t resource;
    frame_arena_t arena{{.chunk_bytes = 128, .maximum_bytes = 4096, .maximum_objects = 100},
                        &resource};
    const auto warm = run_frame(arena);
    REQUIRE(std::ranges::find(warm, std::uintptr_t{}) == warm.end());
    const auto allocations = resource.allocations;
    const auto committed = arena.committed_bytes();
    REQUIRE(allocations > 0);
    CHECK(frames_differing_after_reset(arena, warm, 100) == 0);
    CHECK(resource.allocations == allocations);
    CHECK(arena.committed_bytes() == committed);
}

TEST_CASE("append arena keeps earlier spans stable while it grows", "[unit][arena][append]") {
    append_arena_t arena{{.chunk_bytes = 64, .maximum_bytes = 4096, .maximum_objects = 100}};
    const auto spans = append_patterned(arena, 20, 24, 0, 1);
    REQUIRE(spans.size() == 20);
    CHECK(arena.statistics().chunk_count > 1);
    CHECK(spans_ordered_and_intact(spans));
}

TEST_CASE("append arena never back-fills older chunks", "[unit][arena][append]") {
    append_arena_t arena{{.chunk_bytes = 32, .maximum_bytes = 1024, .maximum_objects = 10}};
    const auto first = arena.append(24, 1);
    const auto second = arena.append(32, 1);
    const auto small = arena.append(4, 1);
    REQUIRE((first && second && small));
    // Chunk 0 still has 8 free bytes, but the last append goes to a new chunk.
    CHECK(std::tuple{first.span.chunk_index, second.span.chunk_index, small.span.chunk_index} ==
          std::tuple{std::size_t{0}, std::size_t{1}, std::size_t{2}});
}

TEST_CASE("append arena returns typed limit errors", "[unit][arena][append][capacity]") {
    append_arena_t arena{{.chunk_bytes = 16, .maximum_bytes = 32, .maximum_objects = 2}};
    REQUIRE(arena.append(16, 1));
    CHECK(arena.append(32, 1).error == arena_error_t::byte_limit);
    REQUIRE(arena.append(8, 1));
    CHECK(std::tuple{arena.append(1, 1).error, arena.append(1, 3).error} ==
          std::tuple{arena_error_t::object_limit, arena_error_t::invalid_alignment});

    counting_resource_t resource;
    resource.fail = true;
    append_arena_t failing{{}, &resource};
    CHECK(failing.append(8, 8).error == arena_error_t::out_of_memory);
}

TEST_CASE("append arena accounts stale bytes and clamps them to used bytes",
          "[unit][arena][append]") {
    append_arena_t arena{{.chunk_bytes = 1024, .maximum_bytes = 4096, .maximum_objects = 10}};
    CHECK(arena.stale_ratio() == 0.0);
    REQUIRE((arena.append(300, 1) && arena.append(300, 1) && arena.append(300, 1)));
    CHECK(arena.statistics().used_bytes == 900);
    arena.mark_stale(300);
    CHECK(std::tuple{arena.stale_bytes(), arena.live_bytes(), arena.stale_ratio() * 3.0} ==
          std::tuple{std::size_t{300}, std::size_t{600}, 1.0});
    arena.mark_stale(10'000);
    CHECK(std::tuple{arena.stale_bytes(), arena.live_bytes(), arena.stale_ratio()} ==
          std::tuple{std::size_t{900}, std::size_t{0}, 1.0});
}

TEST_CASE("append arena compaction copies live spans in order and returns the remap",
          "[unit][arena][append][compact]") {
    append_arena_t arena{{.chunk_bytes = 64, .maximum_bytes = 4096, .maximum_objects = 100}};
    const auto spans = append_patterned(arena, 6, 20, 1, 16);
    REQUIRE(spans.size() == 6);
    const auto stale = spans[1].bytes.size() + spans[4].bytes.size();
    arena.mark_stale(stale);
    const std::array live{spans[0], spans[2], spans[3], spans[5]};
    const std::array<std::size_t, 4> order{3, 1, 0, 2};
    const std::array<std::uint8_t, 4> seeds{0, 32, 48, 80};

    const auto compacted = arena.compact(live, order);
    REQUIRE((compacted && compacted.remap.size() == live.size()));
    CHECK(std::tuple{compacted.arena.stale_bytes(), compacted.arena.statistics().object_count,
                     remap_matches(live, compacted.remap, seeds)} ==
          std::tuple{std::size_t{0}, live.size(), true});
    // Copy order follows `order`: live[3] first at offset 0, then live[1], live[0], live[2].
    CHECK(std::tuple{compacted.remap[3].chunk_index, compacted.remap[3].offset,
                     compacted.remap[1].offset > compacted.remap[3].offset} ==
          std::tuple{std::size_t{0}, std::size_t{0}, true});
    // The source arena is left untouched.
    CHECK((arena.stale_bytes() == stale && holds_pattern(spans[5].bytes, 80)));
}

TEST_CASE("append arena stale accounting excludes alignment padding",
          "[unit][arena][append][capacity]") {
    append_arena_t arena;
    constexpr auto alignment = alignof(std::max_align_t);
    REQUIRE((arena.append(1, 1) && arena.append(1, alignment)));
    CHECK(std::tuple{arena.statistics().used_bytes, arena.live_bytes()} ==
          std::tuple{alignment + 1, std::size_t{2}});
    arena.mark_stale(2);
    CHECK(std::tuple{arena.stale_bytes(), arena.live_bytes(), arena.stale_ratio()} ==
          std::tuple{std::size_t{2}, std::size_t{0}, 1.0});
    arena.mark_stale(100);
    CHECK(arena.stale_bytes() == 2);
}

TEST_CASE("append arena moves and compaction preserve payload accounting",
          "[unit][arena][append][compact]") {
    append_arena_t source;
    const auto stale = source.append(1, 1);
    auto live = source.append(1, alignof(std::max_align_t));
    REQUIRE((stale && live));
    source.mark_stale(1);
    append_arena_t moved{std::move(source)};
    CHECK(std::tuple{moved.live_bytes(), moved.stale_bytes(), moved.stale_ratio(),
                     source.live_bytes(), source.stale_bytes(), source.stale_ratio()} ==
          std::tuple{std::size_t{1}, std::size_t{1}, 0.5,
                     std::size_t{0}, std::size_t{0}, 0.0});
    auto compacted = moved.compact(std::span{&live.span, 1});
    REQUIRE(compacted);
    append_arena_t assigned;
    assigned = std::move(compacted.arena);
    CHECK(std::tuple{assigned.statistics().used_bytes, assigned.live_bytes(),
                     assigned.stale_bytes()} ==
          std::tuple{std::size_t{1}, std::size_t{1}, std::size_t{0}});
    assigned.mark_stale(1);
    CHECK(std::tuple{assigned.live_bytes(), assigned.stale_ratio(),
                     compacted.arena.live_bytes()} == std::tuple{std::size_t{0}, 1.0, std::size_t{0}});
}

TEST_CASE("append arena compaction rejects foreign spans and bad orders",
          "[unit][arena][append][compact]") {
    append_arena_t arena;
    append_arena_t other;
    const auto mine = arena.append(16, 8);
    const auto theirs = other.append(16, 8);
    REQUIRE((mine && theirs));

    const std::array foreign{mine.span, theirs.span};
    const auto rejected = arena.compact(foreign);
    CHECK(rejected.error == arena_compaction_error_t::foreign_span);
    CHECK(rejected.remap.empty());

    const std::array live{mine.span, mine.span};
    const std::array<std::size_t, 2> duplicate{0, 0};
    const std::array<std::size_t, 1> short_order{0};
    const std::array<std::size_t, 2> out_of_range{0, 2};
    CHECK(std::tuple{arena.compact(live, duplicate).error, arena.compact(live, short_order).error,
                     arena.compact(live, out_of_range).error} ==
          std::tuple{arena_compaction_error_t::invalid_order,
                     arena_compaction_error_t::invalid_order,
                     arena_compaction_error_t::invalid_order});
}

TEST_CASE("append arena chunks outlive the arena while a snapshot holds them",
          "[unit][arena][append][lifetime]") {
    counting_resource_t resource;
    std::vector<arena_chunk_handle_t> snapshot;
    const auto retained = snapshot_then_compact(resource, snapshot);
    REQUIRE(retained.size() == 48);
    // The compacted arena and the second chunk are gone; the snapshot's chunk is not.
    CHECK((resource.outstanding_bytes > 0 && holds_pattern(retained, 7)));
    snapshot.clear();
    CHECK(resource.outstanding_bytes == 0);
    CHECK(resource.allocations == resource.deallocations);
}

namespace {
void check_retained_arena_payload(const append_arena_t& arena,
                                  const append_arena_span_t& first,
                                  const arena_chunk_handle_t& retained) {
    CHECK(arena.chunk_handle(0) == retained);
    CHECK(holds_pattern(first.bytes, 7));
}
} // namespace

TEST_CASE("append arena ceiling grows without relocating retained payload",
          "[unit][arena][append][budget]") {
    append_arena_t arena{{.chunk_bytes = 16, .maximum_bytes = 16}};
    const auto first = arena.append(16, 1);
    REQUIRE(first);
    fill(first.span, 7);
    CHECK(arena.append(16, 1).error == arena_error_t::byte_limit);
    const auto retained = first.span.chunk;
    arena.grow_maximum_bytes(48);
    REQUIRE(arena.append(16, 1));
    check_retained_arena_payload(arena, first.span, retained);
    arena.grow_maximum_bytes(8);
    REQUIRE(arena.append(16, 1));
    CHECK(arena.statistics().committed_bytes == 48);
}
