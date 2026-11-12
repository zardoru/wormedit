#pragma once

#include <SDL3/SDL_events.h>

#include <atomic>
#include <cstdint>

namespace wormedit::sys {

// Short-circuits the frame loop's wait: any thread may call wake_event_loop() to make
// SDL_WaitEventTimeout() return so the next frame runs now (a job finished, a worker
// published progress). Header-only so schedulers in tests link without the app.
//
// Pushes are coalesced: at most one wake event is queued; the frame thread clears the
// flag when it dequeues it (acknowledge_event_loop_wake), after which the next call
// pushes again. A wake before init_event_loop_wake() is a no-op, which is harmless
// because the loop has not started waiting yet.
namespace detail {
inline std::atomic<std::uint32_t> wake_event_type{0};
inline std::atomic<bool> wake_queued{false};
} // namespace detail

// Frame thread, once, after SDL_Init.
inline void init_event_loop_wake() noexcept {
    if (detail::wake_event_type.load(std::memory_order_relaxed) != 0) return;
    detail::wake_event_type.store(SDL_RegisterEvents(1), std::memory_order_release);
}

inline void wake_event_loop() noexcept {
    const auto type = detail::wake_event_type.load(std::memory_order_acquire);
    if (type == 0 || detail::wake_queued.exchange(true, std::memory_order_acq_rel)) return;
    SDL_Event event{};
    event.type = type;
    if (!SDL_PushEvent(&event)) detail::wake_queued.store(false, std::memory_order_release);
}

[[nodiscard]] inline auto is_event_loop_wake(const SDL_Event& event) noexcept -> bool {
    const auto type = detail::wake_event_type.load(std::memory_order_acquire);
    return type != 0 && event.type == type;
}

// Frame thread, for each wake event taken from the queue.
inline void acknowledge_event_loop_wake() noexcept {
    detail::wake_queued.store(false, std::memory_order_release);
}

} // namespace wormedit::sys
