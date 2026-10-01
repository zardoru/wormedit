#pragma once

#include <atomic>
#include <cstdint>

namespace wormedit::audio {

struct playhead_state_t {
    bool active{};
    std::uint64_t timeline_frame{};
    std::uint64_t start_frame{};
    // Increments on every start, seek, stop, or clear so a reader can tell a restarted
    // session from the one it saw before.
    std::uint64_t generation{};
};

// Single-writer seqlock for the playhead. publish() must be serialized by the caller
// (the session mutex); read() takes no lock and never blocks the writer. A reader that
// keeps hitting writes retries a bounded number of times and then reports its previous
// successful result, so the returned state is never torn.
class playhead_snapshot_t {
  public:
    static constexpr int maximum_read_attempts = 64;

    void publish(const playhead_state_t& state) noexcept {
        const auto sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        active_.store(state.active, std::memory_order_relaxed);
        timeline_frame_.store(state.timeline_frame, std::memory_order_relaxed);
        start_frame_.store(state.start_frame, std::memory_order_relaxed);
        generation_.store(state.generation, std::memory_order_relaxed);
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
                .generation = generation_.load(std::memory_order_relaxed)};
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) != before) continue;
            remember(state);
            return state;
        }
        return previous();
    }

  private:
    // Readers record each consistent result in a second seqlock-protected copy. It is
    // consulted only after the bounded retries fail, and a reader that cannot store
    // simply skips the update, so a whole (possibly stale) state is always returned.
    void remember(const playhead_state_t& state) const noexcept {
        const auto sequence = last_sequence_.load(std::memory_order_relaxed);
        if ((sequence & 1U) != 0) return;  // another reader is storing; skip
        auto expected = sequence;
        if (!last_sequence_.compare_exchange_strong(expected, sequence + 1,
                                                    std::memory_order_acquire)) {
            return;
        }
        last_active_.store(state.active, std::memory_order_relaxed);
        last_timeline_frame_.store(state.timeline_frame, std::memory_order_relaxed);
        last_start_frame_.store(state.start_frame, std::memory_order_relaxed);
        last_generation_.store(state.generation, std::memory_order_relaxed);
        last_sequence_.store(sequence + 2, std::memory_order_release);
    }

    [[nodiscard]] auto previous() const noexcept -> playhead_state_t {
        for (int attempt = 0; attempt < maximum_read_attempts; ++attempt) {
            const auto before = last_sequence_.load(std::memory_order_acquire);
            if ((before & 1U) != 0) continue;
            const playhead_state_t state{
                .active = last_active_.load(std::memory_order_relaxed),
                .timeline_frame = last_timeline_frame_.load(std::memory_order_relaxed),
                .start_frame = last_start_frame_.load(std::memory_order_relaxed),
                .generation = last_generation_.load(std::memory_order_relaxed)};
            std::atomic_thread_fence(std::memory_order_acquire);
            if (last_sequence_.load(std::memory_order_relaxed) == before) return state;
        }
        return {};
    }

    std::atomic<std::uint64_t> sequence_{};
    std::atomic<bool> active_{};
    std::atomic<std::uint64_t> timeline_frame_{};
    std::atomic<std::uint64_t> start_frame_{};
    std::atomic<std::uint64_t> generation_{};

    mutable std::atomic<std::uint64_t> last_sequence_{};
    mutable std::atomic<bool> last_active_{};
    mutable std::atomic<std::uint64_t> last_timeline_frame_{};
    mutable std::atomic<std::uint64_t> last_start_frame_{};
    mutable std::atomic<std::uint64_t> last_generation_{};
};

} // namespace wormedit::audio
