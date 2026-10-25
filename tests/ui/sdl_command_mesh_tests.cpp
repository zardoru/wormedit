#include "ui/nuklear/render/sdl_command_mesh.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
thread_local bool count_mesh_allocations{};
thread_local std::size_t mesh_allocations{};
}
void* operator new(std::size_t size) {
    if (count_mesh_allocations) ++mesh_allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

using namespace wormedit::ui::nuklear;
namespace {
void expect_mesh(bool condition) { REQUIRE(condition); }
struct mesh_fixture_t {
    std::array<sdl_command_vertex_t, 7> vertices{};
    std::array<sdl_command_index_t, 9> indices{99, 99, 2, 4, 5, 5, 4, 6, 99};
    std::array<sdl_command_batch_t, 3> batches{{
        {nullptr, {1, 2, 30, 40}, 2, 3},
        {reinterpret_cast<SDL_Texture*>(std::uintptr_t{1}), {2, 3, 40, 50}, 5, 3},
        {nullptr, {8, 9, 10, 11}, 8, 0}}};
    mesh_fixture_t() {
        for (std::size_t index = 0; index < vertices.size(); ++index)
            vertices[index] = {{static_cast<float>(index), 2}, {0.25f, 0.75f}, {1, 0.5f, 0, 1}};
    }
    auto view() const -> sdl_command_mesh_view_t { return {vertices, indices, batches, {}}; }
};
}

TEST_CASE("SDL command mesh equality ignores unused global prefixes", "[ui][command-mesh]") {
    mesh_fixture_t source;
    auto owned = sdl_command_mesh_t::from(source.view());
    expect_mesh(owned.has_value());
    expect_mesh(owned->is_equal_to(source.view()));
    auto shifted = source.view();
    shifted.vertices = shifted.vertices.subspan(2);
    auto indices = source.indices;
    for (std::size_t index = 2; index < 8; ++index) indices[index] -= 2;
    shifted.indices = std::span{indices}.subspan(2);
    auto batches = source.batches;
    for (auto& batch : batches) batch.first_index -= 2;
    shifted.batches = batches;
    expect_mesh(owned->is_equal_to(shifted));
    source.vertices[0].position[0] = 900;
    expect_mesh(owned->is_equal_to(source.view()));
}

TEST_CASE("SDL command mesh equality checks every referenced vertex field bit", "[ui][command-mesh]") {
    mesh_fixture_t source;
    auto owned = sdl_command_mesh_t::from(source.view());
    expect_mesh(owned.has_value());
    for (std::size_t component = 0; component < 8; ++component) {
        auto changed = source;
        auto bits = std::bit_cast<std::array<std::uint32_t, 8>>(changed.vertices[4]);
        bits[component] ^= 1;
        changed.vertices[4] = std::bit_cast<sdl_command_vertex_t>(bits);
        expect_mesh(!owned->is_equal_to(changed.view()));
    }
    source.vertices[4].col[2] = -0.0f;
    expect_mesh(!owned->is_equal_to(source.view()));
}

TEST_CASE("SDL command mesh equality checks clips textures topology and ordering", "[ui][command-mesh]") {
    mesh_fixture_t source;
    auto owned = sdl_command_mesh_t::from(source.view());
    expect_mesh(owned.has_value());
    for (std::size_t component = 0; component < 4; ++component) {
        auto changed = source;
        auto clip = std::bit_cast<std::array<int, 4>>(changed.batches[0].clip);
        ++clip[component];
        changed.batches[0].clip = std::bit_cast<SDL_Rect>(clip);
        expect_mesh(!owned->is_equal_to(changed.view()));
    }
    auto changed = source;
    changed.batches[0].texture = source.batches[1].texture;
    expect_mesh(!owned->is_equal_to(changed.view()));
    changed = source;
    std::swap(changed.indices[2], changed.indices[3]);
    expect_mesh(!owned->is_equal_to(changed.view()));
    changed = source;
    std::swap(changed.batches[0], changed.batches[1]);
    expect_mesh(!owned->is_equal_to(changed.view()));
}

TEST_CASE("SDL command mesh compaction preserves exact referenced data and batches", "[ui][command-mesh]") {
    mesh_fixture_t source;
    sdl_command_mesh_t owned;
    const std::array<unsigned char, 3> omitted{1, 0, 0};
    expect_mesh(owned.refill(source.view(), omitted));
    const auto compact = owned.view();
    expect_mesh(compact.is_valid() && compact.vertices.size() == 3 && compact.indices.size() == 3);
    expect_mesh(compact.batches.size() == 3 && compact.batches[0].index_count == 0);
    expect_mesh(compact.batches[1].first_index == 0 && compact.batches[2].first_index == 3);
    for (std::size_t index = 0; index < 3; ++index) {
        expect_mesh(compact.indices[index] == index);
        expect_mesh(compact.vertices[index].position[0] == source.vertices[source.indices[5 + index]].position[0]);
    }
    expect_mesh(compact.batches[1].texture == source.batches[1].texture &&
        compact.batches[1].clip.x == source.batches[1].clip.x);
}

TEST_CASE("SDL command mesh warm queries and compaction perform no heap allocations", "[ui][command-mesh]") {
    mesh_fixture_t source;
    sdl_command_mesh_t owned;
    const std::array<unsigned char, 3> omitted{1, 0, 0};
    expect_mesh(owned.refill(source.view()));
    const auto before = mesh_allocations;
    count_mesh_allocations = true;
    const bool equal = owned.is_equal_to(source.view());
    const bool compacted = owned.refill(source.view(), omitted);
    const bool refilled = owned.refill(source.view());
    count_mesh_allocations = false;
    expect_mesh(equal && compacted && refilled);
    expect_mesh(mesh_allocations == before);
}

TEST_CASE("SDL command mesh rejects invalid ranges and referenced indices before copying", "[ui][command-mesh]") {
    mesh_fixture_t source;
    auto changed = source;
    changed.batches[0].first_index = std::numeric_limits<std::size_t>::max();
    expect_mesh(!sdl_command_mesh_t::from(changed.view()));
    changed = source;
    changed.batches[0].index_count = std::numeric_limits<std::size_t>::max();
    expect_mesh(!changed.view().is_valid());
    changed = source;
    changed.indices[2] = std::numeric_limits<sdl_command_index_t>::max();
    expect_mesh(!changed.view().is_valid());
    sdl_command_mesh_t owned;
    expect_mesh(!owned.refill(source.view(), std::array<unsigned char, 1>{1}));
}

TEST_CASE("SDL command mesh enforces count and byte caps including repeated ranges", "[ui][command-mesh]") {
    mesh_fixture_t source;
    auto view = source.view();
    view.limits.maximum_vertices = 6;
    expect_mesh(!view.is_valid());
    view = source.view(); view.limits.maximum_indices = 8;
    expect_mesh(!view.is_valid());
    view = source.view(); view.limits.maximum_batches = 2;
    expect_mesh(!view.is_valid());
    view = source.view(); view.limits.maximum_bytes = 1;
    expect_mesh(!view.is_valid());
    auto batches = source.batches;
    for (auto& batch : batches) { batch.first_index = 2; batch.index_count = 6; }
    view = source.view(); view.batches = batches; view.limits.maximum_indices = 9;
    expect_mesh(!view.is_valid());
}

TEST_CASE("SDL command mesh omits all geometry while preserving empty batch order", "[ui][command-mesh]") {
    mesh_fixture_t source;
    sdl_command_mesh_t owned;
    expect_mesh(owned.refill(source.view(), std::array<unsigned char, 3>{1, 1, 1}));
    const auto compact = owned.view();
    expect_mesh(compact.vertices.empty() && compact.indices.empty() && compact.batches.size() == 3);
    for (std::size_t index = 0; index < compact.batches.size(); ++index)
        expect_mesh(compact.batches[index].index_count == 0 && compact.batches[index].first_index == 0 &&
            compact.batches[index].texture == source.batches[index].texture);
    expect_mesh(!owned.is_equal_to(source.view()));
}

TEST_CASE("SDL command mesh rejects source subspans overlapping any owned buffer", "[ui][command-mesh]") {
    mesh_fixture_t source;
    auto owned = sdl_command_mesh_t::from(source.view());
    expect_mesh(owned.has_value());
    const auto retained = owned->view();
    const std::array<sdl_command_index_t, 6> indices{0, 1, 2, 2, 1, 3};
    const std::array<sdl_command_batch_t, 1> batches{{{nullptr, {}, 0, 3}}};
    const sdl_command_mesh_view_t vertex_alias{retained.vertices.subspan(1), indices, batches, {}};
    expect_mesh(vertex_alias.is_valid() && !owned->refill(vertex_alias));
    const sdl_command_mesh_view_t index_alias{source.vertices, retained.indices.subspan(1), batches, {}};
    expect_mesh(index_alias.is_valid() && !owned->refill(index_alias));
    const sdl_command_mesh_view_t batch_alias{source.vertices, indices, retained.batches.subspan(1), {}};
    expect_mesh(batch_alias.is_valid() && !owned->refill(batch_alias));
    expect_mesh(owned->is_equal_to(source.view()));
}

namespace {
struct triangle_mesh_t {
    std::array<sdl_command_vertex_t, 3> vertices{};
    std::array<sdl_command_index_t, 3> indices{0, 1, 2};
    std::array<sdl_command_batch_t, 1> batches{{{nullptr, {}, 0, 3}}};
    auto view(std::size_t maximum_bytes) const -> sdl_command_mesh_view_t {
        sdl_command_mesh_view_t result{vertices, indices, batches, {}};
        result.limits.maximum_bytes = maximum_bytes;
        return result;
    }
    static constexpr auto owned_bytes = 3 * sizeof(sdl_command_vertex_t) +
        9 * sizeof(sdl_command_index_t) + sizeof(sdl_command_batch_t);
};
}

TEST_CASE("SDL command mesh byte admission includes offsets remapping and exact boundary", "[ui][command-mesh]") {
    triangle_mesh_t source;
    sdl_command_mesh_t owned;
    const auto too_small = source.view(triangle_mesh_t::owned_bytes - 1);
    expect_mesh(too_small.is_valid()); // Source fits; owned normalization scratch does not.
    const auto before = mesh_allocations;
    count_mesh_allocations = true;
    const bool rejected = !owned.refill(too_small);
    count_mesh_allocations = false;
    expect_mesh(rejected && mesh_allocations == before && owned.retained_bytes() == 0);
    expect_mesh(owned.refill(source.view(triangle_mesh_t::owned_bytes)));
    expect_mesh(owned.retained_bytes() == triangle_mesh_t::owned_bytes);
    expect_mesh(!owned.refill(too_small) && owned.is_equal_to(source.view(triangle_mesh_t::owned_bytes)));
}

TEST_CASE("SDL command mesh lower budgets reject retained capacity before mutation", "[ui][command-mesh]") {
    mesh_fixture_t larger;
    auto owned = sdl_command_mesh_t::from(larger.view());
    expect_mesh(owned.has_value());
    triangle_mesh_t smaller;
    const auto before = owned->retained_bytes();
    expect_mesh(before > triangle_mesh_t::owned_bytes);
    expect_mesh(!owned->refill(smaller.view(triangle_mesh_t::owned_bytes)));
    expect_mesh(owned->retained_bytes() == before && owned->is_equal_to(larger.view()));
}

TEST_CASE("SDL command mesh normalization handles first references above the minimum index", "[ui][command-mesh]") {
    mesh_fixture_t source;
    source.indices = {99,99,5,2,4,4,2,6,99};
    auto owned = sdl_command_mesh_t::from(source.view());
    expect_mesh(owned.has_value() && owned->is_equal_to(source.view()));
    auto shifted = source.view();
    shifted.vertices = shifted.vertices.subspan(2);
    auto indices = source.indices;
    for (std::size_t index = 2; index < 8; ++index) indices[index] -= 2;
    auto batches = source.batches;
    for (auto& batch : batches) batch.first_index -= 2;
    shifted.indices = std::span{indices}.subspan(2); shifted.batches = batches;
    expect_mesh(owned->is_equal_to(shifted));
    source.indices[2] = 1; // Below the retained first-reference offset: reject safely.
    expect_mesh(!owned->is_equal_to(source.view()));
}
