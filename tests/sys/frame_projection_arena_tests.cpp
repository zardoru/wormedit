#include "sys/frame_timing.h"
#include "sys/workspace_projection.h"
#include <cstring>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <new>

using namespace wormedit::sys;
using wormedit::frame_arena_t;

TEST_CASE("Production debug heap hook counts scalar array aligned and nothrow new",
          "[sys][frame-arena][allocation]") {
#ifndef NDEBUG
    const auto before = frame_heap_allocation_snapshot();
    auto* scalar = ::operator new(17);
    auto* array = ::operator new[](31);
    auto* aligned = ::operator new(128, std::align_val_t{64});
    auto* optional = ::operator new(13, std::nothrow);
    auto* aligned_optional = ::operator new[](64, std::align_val_t{64}, std::nothrow);
    const auto after = frame_heap_allocation_snapshot();
    const auto address = reinterpret_cast<std::uintptr_t>(aligned);
    const auto optional_address = reinterpret_cast<std::uintptr_t>(aligned_optional);
    ::operator delete(scalar);
    ::operator delete[](array);
    ::operator delete(aligned, std::align_val_t{64});
    ::operator delete(optional, std::nothrow);
    ::operator delete[](aligned_optional, std::align_val_t{64}, std::nothrow);
    CHECK(after.count - before.count == 5);
    CHECK(after.bytes - before.bytes == 253);
    CHECK(address % 64 == 0);
    CHECK(optional_address % 64 == 0);
#else
    CHECK(frame_heap_allocation_snapshot().count == 0);
#endif
}

namespace {
template<class Condition>
void check_frame_storage(const Condition& condition) {
    CHECK(condition);
}
}

TEST_CASE("Frame storage arrays retain fill order and reject capacity overflow",
          "[sys][frame-arena]") {
    frame_arena_t arena({.chunk_bytes = 1024, .maximum_bytes = 4096});
    auto rows = arena_array<std::uint64_t>(arena, 2);
    REQUIRE(rows.capacity() == 2);
    check_frame_storage(rows.push_back(7));
    check_frame_storage(rows.push_back(11));
    check_frame_storage(!(rows.push_back(13)));
    check_frame_storage(rows.size() == 2);
    check_frame_storage(rows[0] == 7);
    check_frame_storage(rows[1] == 11);
    check_frame_storage(rows.end() - rows.begin() == 2);
    rows.clear();
    check_frame_storage(rows.push_back(19));
    check_frame_storage(rows[0] == 19);
    check_frame_storage(arena_array<std::uint64_t>(arena, std::numeric_limits<std::size_t>::max()).empty());
}

TEST_CASE("Arena strings own formatted bytes and fail without partial publication",
          "[sys][frame-arena]") {
    frame_arena_t arena({.chunk_bytes = 64, .maximum_bytes = 128});
    const auto* copied = arena_string(arena, "plain");
    const auto* formatted = arena_format(arena, "row %d:%.2f", 7, 1.25);
    REQUIRE(copied);
    REQUIRE(formatted);
    check_frame_storage(std::strcmp(copied, "plain") == 0);
    check_frame_storage(std::strcmp(formatted, "row 7:1.25") == 0);
    check_frame_storage(arena_format(arena, nullptr) == nullptr);
    check_frame_storage(arena_array<std::uint64_t>(arena, 128).capacity() == 0);
    check_frame_storage(std::strcmp(copied, "plain") == 0);
}

TEST_CASE("Warmed frame storage reuses chunks with stable bytes and no heap allocation",
          "[sys][frame-arena]") {
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    auto fill = [&] {
        arena.reset();
        auto rows = arena_array<std::uint64_t>(arena, 32);
        for (std::uint64_t row = 0; row < 32; ++row) (void)rows.push_back(row);
        (void)arena_format(arena, "position %d", 42);
        return arena.used_bytes();
    };
    const auto used = fill();
    const auto committed = arena.committed_bytes();
    const auto before = frame_heap_allocation_snapshot();
    bool stable = true;
    for (int frame = 0; frame < 1000; ++frame)
        stable = stable && fill() == used && arena.committed_bytes() == committed;
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(stable);
    check_frame_storage(after.count == before.count);
}
