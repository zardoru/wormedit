#pragma once

#include <wormedit/core/task_graph.h>

#include <atomic>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>

namespace wormedit {

// Applies a priority to the calling thread. The OS may refuse; the thread then keeps its
// default priority and one warning is logged per process and priority.
inline void apply_current_thread_priority(SDL_ThreadPriority priority) noexcept {
    if (SDL_SetCurrentThreadPriority(priority)) return;
    static std::atomic<unsigned> warned{};
    const auto bit = 1U << static_cast<unsigned>(priority);
    if ((warned.fetch_or(bit) & bit) == 0) {
        SDL_Log("could not change thread priority: %s", SDL_GetError());
    }
}

// Scheduler options for decode, preload, analysis, copy, and autosave workers: they run
// below the frame thread so heavy background work cannot starve it.
[[nodiscard]] inline auto background_scheduler_options() -> task_scheduler_options_t {
    return {.on_worker_start = [] { apply_current_thread_priority(SDL_THREAD_PRIORITY_LOW); }};
}

}  // namespace wormedit
