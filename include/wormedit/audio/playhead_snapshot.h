#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace wormedit::audio {

struct playhead_state_t {
    bool active{};
    std::uint64_t timeline_frame{};
    std::uint64_t start_frame{};
    // Increments on every start, seek, stop, or clear so a reader can tell a restarted
    // session from the one it saw before.
    std::uint64_t generation{};
    std::uint64_t transport_generation{};
    // Output owners may retain the audible clock while completed PCM drains.
    bool source_finished{};
};

// Single writer, serialized by the session mutex. Readers make bounded attempts,
// then reuse their own last coherent state. A newer publication invalidates an
// older-generation fallback. Eight fixed thread-local slots bound storage; slot
// eviction returns an inactive state rather than another snapshot's playhead.
class playhead_snapshot_t {
  public:
    static constexpr int maximum_read_attempts = 64;

    void publish(const playhead_state_t& state) noexcept {
        const auto sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_relaxed);
        published_generation_.store(state.generation, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        active_.store(state.active, std::memory_order_relaxed);
        timeline_frame_.store(state.timeline_frame, std::memory_order_relaxed);
        start_frame_.store(state.start_frame, std::memory_order_relaxed);
        generation_.store(state.generation, std::memory_order_relaxed);
        transport_generation_.store(state.transport_generation, std::memory_order_relaxed);
        source_finished_.store(state.source_finished, std::memory_order_relaxed);
        sequence_.store(sequence + 2, std::memory_order_release);
    }

    [[nodiscard]] auto read() const noexcept -> playhead_state_t {
        for (int attempt = 0; attempt < maximum_read_attempts; ++attempt) {
            const auto before = sequence_.load(std::memory_order_acquire);
            if ((before & 1U) != 0) continue;
            const playhead_state_t state{
                .active = active_.load(std::memory_order_relaxed),
                .timeline_frame = timeline_frame_.load(std::memory_order_relaxed),
                .start_frame = start_frame_.load(std::memory_order_relaxed),
                .generation = generation_.load(std::memory_order_relaxed),
                .transport_generation = transport_generation_.load(std::memory_order_relaxed),
                .source_finished = source_finished_.load(std::memory_order_relaxed)};
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) != before) continue;
            remember(state);
            return state;
        }
        return previous();
    }

  private:
    friend struct playhead_snapshot_test_access_t;
    struct reader_slot_t {
        std::uint64_t identity{};
        playhead_state_t state{};
    };
    struct reader_cache_t {
        std::array<reader_slot_t, 8> slots{};
        std::size_t next{};
    };

    [[nodiscard]] auto reader_slot() const noexcept -> reader_slot_t& {
        static thread_local reader_cache_t cache;
        for (auto& slot : cache.slots) {
            if (slot.identity == identity_) return slot;
        }
        auto& slot = cache.slots[cache.next];
        cache.next = (cache.next + 1) % cache.slots.size();
        slot = {.identity = identity_};
        return slot;
    }

    void remember(const playhead_state_t& state) const noexcept {
        reader_slot().state = state;
    }

    [[nodiscard]] auto previous() const noexcept -> playhead_state_t {
        const auto state = reader_slot().state;
        const auto generation = published_generation_.load(std::memory_order_acquire);
        if (state.generation != generation) return {.generation = generation};
        return state;
    }

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    inline static std::atomic<std::uint64_t> next_identity_{1};
    const std::uint64_t identity_{next_identity_.fetch_add(1, std::memory_order_relaxed)};
    std::atomic<std::uint64_t> sequence_{};
    std::atomic<bool> active_{};
    std::atomic<std::uint64_t> timeline_frame_{};
    std::atomic<std::uint64_t> start_frame_{};
    std::atomic<std::uint64_t> generation_{};
    std::atomic<std::uint64_t> transport_generation_{};
    std::atomic<std::uint64_t> published_generation_{};
    std::atomic<bool> source_finished_{};
};

} // namespace wormedit::audio
