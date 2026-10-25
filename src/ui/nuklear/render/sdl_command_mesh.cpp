#include "ui/nuklear/render/sdl_command_mesh.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace wormedit::ui::nuklear {
namespace {
constexpr auto missing_index = std::numeric_limits<sdl_command_index_t>::max();

// Subtraction guards every byte sum before multiplication.
auto fits_bytes(sdl_command_mesh_view_t source, std::size_t index_count) -> bool {
    auto remaining = source.limits.maximum_bytes;
    const auto take = [&remaining](std::size_t count, std::size_t width) {
        if (count > remaining / width) return false;
        remaining -= count * width;
        return true;
    };
    return take(source.vertices.size(), sizeof(sdl_command_vertex_t)) &&
        take(index_count, sizeof(sdl_command_index_t)) &&
        take(source.batches.size(), sizeof(sdl_command_batch_t));
}

auto referenced_bounds(sdl_command_mesh_view_t source,
                       std::span<const unsigned char> omitted = {})
    -> std::pair<std::size_t, std::size_t> {
    std::size_t minimum = source.vertices.size(), maximum{};
    for (std::size_t index = 0; index < source.batches.size(); ++index) {
        if (!omitted.empty() && omitted[index]) continue;
        const auto& batch = source.batches[index];
        for (const auto vertex : source.indices.subspan(batch.first_index, batch.index_count)) {
            minimum = std::min(minimum, static_cast<std::size_t>(vertex));
            maximum = std::max(maximum, static_cast<std::size_t>(vertex));
        }
    }
    return {minimum, minimum == source.vertices.size() ? 0 : maximum - minimum + 1};
}

struct memory_range_t {
    const void* data{};
    std::size_t bytes{};
    auto overlaps(memory_range_t other) const -> bool {
        if (!bytes || !other.bytes) return false;
        const auto first = reinterpret_cast<std::uintptr_t>(data);
        const auto second = reinterpret_cast<std::uintptr_t>(other.data);
        return first >= second ? first - second < other.bytes : second - first < bytes;
    }
};

auto retained_index_count(sdl_command_mesh_view_t source, std::span<const unsigned char> omitted)
    -> std::size_t {
    std::size_t count{};
    for (std::size_t index = 0; index < source.batches.size(); ++index)
        if (omitted.empty() || !omitted[index]) count += source.batches[index].index_count;
    return count;
}

auto same_batch(const sdl_command_batch_t& first, const sdl_command_batch_t& second) -> bool {
    return first.texture == second.texture && first.index_count == second.index_count &&
        first.clip.x == second.clip.x && first.clip.y == second.clip.y &&
        first.clip.w == second.clip.w && first.clip.h == second.clip.h;
}

auto same_vertex(const sdl_command_vertex_t& first, const sdl_command_vertex_t& second) -> bool {
    return std::memcmp(first.position, second.position, sizeof(first.position)) == 0 &&
        std::memcmp(first.uv, second.uv, sizeof(first.uv)) == 0 &&
        std::memcmp(first.col, second.col, sizeof(first.col)) == 0;
}
} // namespace

auto sdl_command_mesh_view_t::is_valid() const -> bool {
    if (vertices.size() > limits.maximum_vertices || vertices.size() > missing_index ||
        indices.size() > limits.maximum_indices || batches.size() > limits.maximum_batches ||
        !fits_bytes(*this, indices.size())) return false;
    std::size_t referenced{};
    for (const auto& batch : batches) {
        if (batch.first_index > indices.size() || batch.index_count > indices.size() - batch.first_index ||
            batch.index_count > limits.maximum_indices - referenced) return false;
        referenced += batch.index_count;
        for (const auto index : indices.subspan(batch.first_index, batch.index_count))
            if (index >= vertices.size()) return false;
    }
    // Repeated batch ranges must not produce an oversized compact output.
    return fits_bytes(*this, referenced);
}

auto sdl_command_mesh_view_t::from(std::span<const sdl_command_vertex_t> vertices,
    std::span<const sdl_command_index_t> indices, std::span<const sdl_command_batch_t> batches,
    sdl_command_mesh_limits_t limits) -> std::optional<sdl_command_mesh_view_t> {
    const sdl_command_mesh_view_t result{vertices, indices, batches, limits};
    return result.is_valid() ? std::optional{result} : std::nullopt;
}

auto sdl_command_mesh_t::from(sdl_command_mesh_view_t source) -> std::optional<sdl_command_mesh_t> {
    sdl_command_mesh_t result;
    if (!result.refill(source)) return std::nullopt;
    return result;
}

auto sdl_command_mesh_t::overlaps_storage(sdl_command_mesh_view_t source,
    std::span<const unsigned char> omitted) const -> bool {
    const std::array owned{
        memory_range_t{vertices_.data(), vertices_.capacity() * sizeof(sdl_command_vertex_t)},
        memory_range_t{indices_.data(), indices_.capacity() * sizeof(sdl_command_index_t)},
        memory_range_t{batches_.data(), batches_.capacity() * sizeof(sdl_command_batch_t)},
        memory_range_t{source_offsets_.data(), source_offsets_.capacity() * sizeof(sdl_command_index_t)},
        memory_range_t{remap_.data(), remap_.capacity() * sizeof(sdl_command_index_t)}};
    const std::array borrowed{
        memory_range_t{source.vertices.data(), source.vertices.size_bytes()},
        memory_range_t{source.indices.data(), source.indices.size_bytes()},
        memory_range_t{source.batches.data(), source.batches.size_bytes()},
        memory_range_t{omitted.data(), omitted.size_bytes()}};
    for (const auto allocation : owned)
        for (const auto input : borrowed)
            if (allocation.overlaps(input)) return true;
    return false;
}

auto sdl_command_mesh_t::can_reserve(std::size_t vertices, std::size_t indices, std::size_t batches,
    std::size_t remap, std::size_t byte_limit) const -> bool {
    const std::array counts{std::max(vertices, vertices_.capacity()), std::max(indices, indices_.capacity()),
        std::max(batches, batches_.capacity()), std::max(vertices, source_offsets_.capacity()),
        std::max(remap, remap_.capacity())};
    const std::array widths{sizeof(sdl_command_vertex_t), sizeof(sdl_command_index_t),
        sizeof(sdl_command_batch_t), sizeof(sdl_command_index_t), sizeof(sdl_command_index_t)};
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if (counts[index] > byte_limit / widths[index]) return false;
        byte_limit -= counts[index] * widths[index];
    }
    return true;
}

void sdl_command_mesh_t::reserve(std::size_t vertices, std::size_t indices,
    std::size_t batches, std::size_t remap) {
    vertices_.reserve(vertices); indices_.reserve(indices); batches_.reserve(batches);
    source_offsets_.reserve(vertices); remap_.reserve(remap);
}

void sdl_command_mesh_t::release() {
    std::vector<sdl_command_vertex_t>{}.swap(vertices_);
    std::vector<sdl_command_index_t>{}.swap(indices_);
    std::vector<sdl_command_batch_t>{}.swap(batches_);
    std::vector<sdl_command_index_t>{}.swap(source_offsets_);
    std::vector<sdl_command_index_t>{}.swap(remap_);
}

void sdl_command_mesh_t::append_batch(sdl_command_mesh_view_t source,
    const sdl_command_batch_t& batch, std::size_t minimum, bool omitted) {
    auto& output = batches_.emplace_back(batch);
    output.first_index = indices_.size();
    if (omitted) { output.index_count = 0; return; }
    for (const auto index : source.indices.subspan(batch.first_index, batch.index_count)) {
        const auto offset = static_cast<std::size_t>(index) - minimum;
        auto& mapped = remap_[offset];
        if (mapped == missing_index) {
            mapped = static_cast<sdl_command_index_t>(vertices_.size());
            vertices_.push_back(source.vertices[index]);
            source_offsets_.push_back(static_cast<sdl_command_index_t>(offset));
        }
        indices_.push_back(mapped);
    }
}

auto sdl_command_mesh_t::refill(sdl_command_mesh_view_t source,
                               std::span<const unsigned char> omitted) -> bool {
    if (!source.is_valid() || (!omitted.empty() && omitted.size() != source.batches.size())) return false;
    if (overlaps_storage(source, omitted)) return false;
    const auto [minimum, count] = referenced_bounds(source, omitted);
    const auto index_count = retained_index_count(source, omitted);
    const auto vertex_bound = std::min(count, index_count);
    if (!can_reserve(vertex_bound, index_count, source.batches.size(), count,
                     source.limits.maximum_bytes)) return false;
    try {
        // Explicit reservation prevents push_back's geometric growth from
        // escaping the preflight capacity budget.
        reserve(vertex_bound, index_count, source.batches.size(), count);
        if (retained_bytes() > source.limits.maximum_bytes) { release(); return false; }
        remap_.assign(count, missing_index);
        vertices_.clear(); indices_.clear(); batches_.clear(); source_offsets_.clear();
        for (std::size_t index = 0; index < source.batches.size(); ++index)
            append_batch(source, source.batches[index], minimum, !omitted.empty() && omitted[index]);
    } catch (const std::bad_alloc&) {
        release();
        return false;
    }
    limits_ = source.limits;
    return true;
}

auto sdl_command_mesh_t::is_equal_to(sdl_command_mesh_view_t source) const -> bool {
    if (!source.is_valid() || source.batches.size() != batches_.size()) return false;
    std::size_t cursor{}, minimum{};
    bool initialized{};
    for (std::size_t batch_index = 0; batch_index < batches_.size(); ++batch_index) {
        const auto& batch = source.batches[batch_index];
        if (!same_batch(batch, batches_[batch_index])) return false;
        if (batch.index_count && !initialized) {
            const auto first = source.indices[batch.first_index];
            const auto offset = source_offsets_[indices_[cursor]];
            if (first < offset) return false;
            minimum = first-offset;
            initialized = true;
        }
        for (const auto index : source.indices.subspan(batch.first_index, batch.index_count)) {
            const auto retained = indices_[cursor++];
            if (index < minimum || index-minimum != source_offsets_[retained]) return false;
        }
    }
    // Topology above proves every unique source vertex exists. Compare its
    // field bits once, rather than once for each triangle corner referencing it.
    for (std::size_t index = 0; index < vertices_.size(); ++index)
        if (!same_vertex(source.vertices[minimum+source_offsets_[index]],vertices_[index])) return false;
    return true;
}

auto sdl_command_mesh_t::view() const -> sdl_command_mesh_view_t {
    return {vertices_, indices_, batches_, limits_};
}
auto sdl_command_mesh_t::retained_bytes() const -> std::size_t {
    return vertices_.capacity() * sizeof(sdl_command_vertex_t) +
        indices_.capacity() * sizeof(sdl_command_index_t) +
        batches_.capacity() * sizeof(sdl_command_batch_t) +
        (source_offsets_.capacity() + remap_.capacity()) * sizeof(sdl_command_index_t);
}
} // namespace wormedit::ui::nuklear
