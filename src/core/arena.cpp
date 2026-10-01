#include <wormedit/core/arena.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace wormedit {
namespace {

constexpr auto max_default_alignment = alignof(std::max_align_t);

auto is_power_of_two(std::size_t value) noexcept -> bool {
    return value != 0 && (value & (value - 1)) == 0;
}

auto sanitized_options(arena_options_t options) noexcept -> arena_options_t {
    if (options.chunk_bytes == 0)
        options.chunk_bytes = 1;
    if (options.maximum_bytes == 0)
        options.chunk_bytes = 0;
    options.chunk_bytes = std::min(options.chunk_bytes, options.maximum_bytes);
    return options;
}

auto find_space(const chunk_arena_core_t::chunk_t& chunk, std::size_t size,
                std::size_t alignment) noexcept -> std::optional<std::size_t> {
    const auto remainder = chunk.used % alignment;
    const auto padding = remainder == 0 ? 0 : alignment - remainder;
    if (chunk.used > std::numeric_limits<std::size_t>::max() - padding)
        return std::nullopt;
    const auto begin = chunk.used + padding;
    if (begin > chunk.capacity || size > chunk.capacity - begin)
        return std::nullopt;
    return begin;
}

struct chunk_deleter_t {
    std::pmr::memory_resource* resource{};
    std::size_t capacity{};

    void operator()(std::byte* storage) const noexcept {
        resource->deallocate(storage, capacity, max_default_alignment);
    }
};

// Zero-filled like the original std::make_unique<std::byte[]> chunks.
auto make_chunk_storage(std::pmr::memory_resource* resource, std::size_t capacity)
    -> std::shared_ptr<std::byte[]> {
    auto* storage = static_cast<std::byte*>(resource->allocate(capacity, max_default_alignment));
    std::memset(storage, 0, capacity);
    return {storage, chunk_deleter_t{.resource = resource, .capacity = capacity},
            std::pmr::polymorphic_allocator<std::byte>{resource}};
}

} // namespace

chunk_arena_core_t::chunk_arena_core_t(arena_options_t options,
                                       std::pmr::memory_resource* resource) noexcept
    : options_(sanitized_options(options)),
      resource_(resource != nullptr ? resource : std::pmr::new_delete_resource()) {}

chunk_arena_core_t::chunk_arena_core_t(chunk_arena_core_t&& other) noexcept
    : options_(other.options_), resource_(other.resource_), chunks_(std::move(other.chunks_)),
      committed_bytes_(std::exchange(other.committed_bytes_, 0)),
      used_bytes_(std::exchange(other.used_bytes_, 0)),
      object_count_(std::exchange(other.object_count_, 0)),
      chunk_index_(std::exchange(other.chunk_index_, 0)) {
    other.chunks_.clear();
}

auto chunk_arena_core_t::operator=(chunk_arena_core_t&& other) noexcept -> chunk_arena_core_t& {
    if (this == &other)
        return *this;
    options_ = other.options_;
    resource_ = other.resource_;
    chunks_ = std::move(other.chunks_);
    other.chunks_.clear();
    committed_bytes_ = std::exchange(other.committed_bytes_, 0);
    used_bytes_ = std::exchange(other.used_bytes_, 0);
    object_count_ = std::exchange(other.object_count_, 0);
    chunk_index_ = std::exchange(other.chunk_index_, 0);
    return *this;
}

auto chunk_arena_core_t::commit(std::size_t index, std::size_t begin, std::size_t size) noexcept
    -> allocation_t {
    auto& chunk = chunks_[index];
    const auto consumed = begin + size - chunk.used;
    auto* address = chunk.storage.get() + begin;
    chunk.used = begin + size;
    used_bytes_ += consumed;
    ++object_count_;
    chunk_index_ = index;
    return {.data = address, .chunk_index = index, .offset = begin};
}

auto chunk_arena_core_t::find_existing(std::size_t size, std::size_t alignment,
                                       arena_fit_policy_t policy) const noexcept
    -> std::optional<placement_t> {
    if (chunks_.empty())
        return std::nullopt;
    if (policy == arena_fit_policy_t::newest_chunk) {
        const auto index = chunks_.size() - 1;
        const auto begin = find_space(chunks_[index], size, alignment);
        if (!begin.has_value())
            return std::nullopt;
        return placement_t{.chunk_index = index, .begin = *begin};
    }
    for (std::size_t offset = 0; offset < chunks_.size(); ++offset) {
        const auto index = (chunk_index_ + offset) % chunks_.size();
        if (const auto begin = find_space(chunks_[index], size, alignment); begin.has_value())
            return placement_t{.chunk_index = index, .begin = *begin};
    }
    return std::nullopt;
}

auto chunk_arena_core_t::add_chunk(std::size_t size, std::size_t alignment) noexcept
    -> arena_error_t {
    const auto available = options_.maximum_bytes - committed_bytes_;
    if (available == 0 || size > available || alignment - 1 > available - size)
        return arena_error_t::byte_limit;
    const auto required = size + alignment - 1;
    const auto capacity = std::min(available, std::max(options_.chunk_bytes, required));
    try {
        chunks_.push_back(chunk_t{.storage = make_chunk_storage(resource_, capacity),
                                  .capacity = capacity});
    } catch (...) {
        return arena_error_t::out_of_memory;
    }
    committed_bytes_ += capacity;
    chunk_index_ = chunks_.size() - 1;
    return arena_error_t::none;
}

auto chunk_arena_core_t::allocate_bytes(std::size_t size, std::size_t alignment,
                                        arena_fit_policy_t policy) noexcept -> allocation_t {
    if (!is_power_of_two(alignment) || alignment > max_default_alignment)
        return {.error = arena_error_t::invalid_alignment};
    if (object_count_ >= options_.maximum_objects)
        return {.error = arena_error_t::object_limit};
    if (const auto placement = find_existing(size, alignment, policy); placement.has_value())
        return commit(placement->chunk_index, placement->begin, size);
    if (const auto error = add_chunk(size, alignment); error != arena_error_t::none)
        return {.error = error};
    const auto begin = find_space(chunks_.back(), size, alignment);
    if (!begin.has_value())
        return {.error = arena_error_t::byte_limit};
    return commit(chunks_.size() - 1, *begin, size);
}

void chunk_arena_core_t::rewind() noexcept {
    for (auto& chunk : chunks_)
        chunk.used = 0;
    used_bytes_ = 0;
    object_count_ = 0;
    chunk_index_ = 0;
}

auto chunk_arena_core_t::options() const noexcept -> const arena_options_t& {
    return options_;
}

auto chunk_arena_core_t::resource() const noexcept -> std::pmr::memory_resource* {
    return resource_;
}

auto chunk_arena_core_t::statistics() const noexcept -> arena_statistics_t {
    return {.committed_bytes = committed_bytes_,
            .used_bytes = used_bytes_,
            .object_count = object_count_,
            .chunk_count = chunks_.size()};
}

auto chunk_arena_core_t::chunks() const noexcept -> std::span<const chunk_t> {
    return chunks_;
}

frame_arena_t::frame_arena_t(arena_options_t options, std::pmr::memory_resource* resource) noexcept
    : core_(new (std::nothrow) chunk_arena_core_t(options, resource)) {}

frame_arena_t::~frame_arena_t() = default;
frame_arena_t::frame_arena_t(frame_arena_t&&) noexcept = default;
auto frame_arena_t::operator=(frame_arena_t&&) noexcept -> frame_arena_t& = default;

auto frame_arena_t::allocate_bytes(std::size_t size, std::size_t alignment) noexcept
    -> arena_allocation_t {
    if (!core_)
        return {.error = arena_error_t::out_of_memory};
    const auto allocation = core_->allocate_bytes(size, alignment, arena_fit_policy_t::any_chunk);
    return {.data = allocation.data, .error = allocation.error};
}

void frame_arena_t::reset() noexcept {
    if (core_)
        core_->rewind();
}

auto frame_arena_t::object_count() const noexcept -> std::size_t {
    return core_ ? core_->statistics().object_count : 0;
}

auto frame_arena_t::committed_bytes() const noexcept -> std::size_t {
    return core_ ? core_->statistics().committed_bytes : 0;
}

auto frame_arena_t::used_bytes() const noexcept -> std::size_t {
    return core_ ? core_->statistics().used_bytes : 0;
}

auto frame_arena_t::statistics() const noexcept -> arena_statistics_t {
    return core_ ? core_->statistics() : arena_statistics_t{};
}

append_arena_t::append_arena_t(arena_options_t options,
                               std::pmr::memory_resource* resource) noexcept
    : core_(options, resource) {}

append_arena_t::append_arena_t(append_arena_t&& other) noexcept
    : core_(std::move(other.core_)), payload_bytes_(std::exchange(other.payload_bytes_, 0)),
      stale_bytes_(std::exchange(other.stale_bytes_, 0)) {}

auto append_arena_t::operator=(append_arena_t&& other) noexcept -> append_arena_t& {
    if (this == &other)
        return *this;
    core_ = std::move(other.core_);
    payload_bytes_ = std::exchange(other.payload_bytes_, 0);
    stale_bytes_ = std::exchange(other.stale_bytes_, 0);
    return *this;
}

auto append_arena_t::append(std::size_t size, std::size_t alignment) noexcept
    -> append_arena_result_t {
    const auto allocation =
        core_.allocate_bytes(size, alignment, arena_fit_policy_t::newest_chunk);
    if (allocation.error != arena_error_t::none)
        return {.span = {}, .error = allocation.error};
    payload_bytes_ += size;
    return {.span = {.bytes = std::span<std::byte>{allocation.data, size},
                     .chunk = chunk_handle(allocation.chunk_index),
                     .chunk_index = allocation.chunk_index,
                     .offset = allocation.offset,
                     .alignment = alignment}};
}

void append_arena_t::mark_stale(std::size_t bytes) noexcept {
    stale_bytes_ = bytes > payload_bytes_ - stale_bytes_ ? payload_bytes_ : stale_bytes_ + bytes;
}

auto append_arena_t::stale_bytes() const noexcept -> std::size_t {
    return stale_bytes_;
}

auto append_arena_t::live_bytes() const noexcept -> std::size_t {
    return payload_bytes_ - stale_bytes_;
}

auto append_arena_t::stale_ratio() const noexcept -> double {
    if (payload_bytes_ == 0)
        return 0.0;
    return static_cast<double>(stale_bytes_) / static_cast<double>(payload_bytes_);
}

auto append_arena_t::statistics() const noexcept -> arena_statistics_t {
    return core_.statistics();
}

auto append_arena_t::chunk_handle(std::size_t chunk_index) const noexcept
    -> arena_chunk_handle_t {
    const auto chunks = core_.chunks();
    if (chunk_index >= chunks.size())
        return {};
    return chunks[chunk_index].storage;
}

auto append_arena_t::owns(const append_arena_span_t& span) const noexcept -> bool {
    const auto chunks = core_.chunks();
    if (span.chunk_index >= chunks.size())
        return false;
    const auto& chunk = chunks[span.chunk_index];
    if (span.offset > chunk.used || span.bytes.size() > chunk.used - span.offset)
        return false;
    return chunk.storage.get() + span.offset == span.bytes.data();
}

namespace {

// Returns false when `order` is neither empty nor a permutation of
// [0, count). May throw std::bad_alloc.
auto is_valid_order(std::span<const std::size_t> order, std::size_t count) -> bool {
    if (order.empty())
        return true;
    if (order.size() != count)
        return false;
    std::vector<bool> seen(count, false);
    for (const auto index : order) {
        if (index >= count || seen[index])
            return false;
        seen[index] = true;
    }
    return true;
}

} // namespace

auto append_arena_t::compact(std::span<const append_arena_span_t> live_spans,
                             std::span<const std::size_t> order) const noexcept
    -> append_arena_compaction_t {
    append_arena_compaction_t result{
        .arena = append_arena_t{core_.options(), core_.resource()}, .remap = {}};
    const auto fail = [&](arena_compaction_error_t error, arena_error_t allocation_error) {
        result.arena = append_arena_t{core_.options(), core_.resource()};
        result.remap.clear();
        result.error = error;
        result.allocation_error = allocation_error;
        return std::move(result);
    };
    if (!std::ranges::all_of(live_spans, [this](const auto& span) { return owns(span); }))
        return fail(arena_compaction_error_t::foreign_span, arena_error_t::none);
    try {
        if (!is_valid_order(order, live_spans.size()))
            return fail(arena_compaction_error_t::invalid_order, arena_error_t::none);
        result.remap.resize(live_spans.size());
    } catch (...) {
        return fail(arena_compaction_error_t::allocation_failed, arena_error_t::out_of_memory);
    }
    for (std::size_t position = 0; position < live_spans.size(); ++position) {
        const auto index = order.empty() ? position : order[position];
        const auto& source = live_spans[index];
        auto copy = result.arena.append(source.bytes.size(), source.alignment);
        if (!copy)
            return fail(arena_compaction_error_t::allocation_failed, copy.error);
        if (!source.bytes.empty())
            std::memcpy(copy.span.bytes.data(), source.bytes.data(), source.bytes.size());
        result.remap[index] = std::move(copy.span);
    }
    return result;
}

} // namespace wormedit
