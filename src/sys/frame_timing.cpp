#include "sys/frame_timing.h"
#include "ui/frontend.h"
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

// App-only debug replacement; test executables retain their existing hooks.
#if defined(WORMEDIT_FRAME_ALLOCATION_HOOK) && !defined(NDEBUG)
namespace {
void* allocate_counted_heap(std::size_t bytes, std::size_t alignment = 0) {
    const auto size = std::max<std::size_t>(bytes, 1);
    for (;;) {
        void* result = alignment == 0 ? std::malloc(size) : SDL_aligned_alloc(alignment, size);
        if (result) {
            wormedit::sys::record_frame_heap_allocation(bytes);
            return result;
        }
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}
void* allocate_nothrow_heap(std::size_t bytes, std::size_t alignment = 0) noexcept {
    try { return allocate_counted_heap(bytes, alignment); }
    catch (...) { return nullptr; }
}
}
void* operator new(std::size_t bytes) { return allocate_counted_heap(bytes); }
void* operator new[](std::size_t bytes) { return allocate_counted_heap(bytes); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    return allocate_nothrow_heap(bytes);
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    return allocate_nothrow_heap(bytes);
}
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return allocate_counted_heap(bytes, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return allocate_counted_heap(bytes, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate_nothrow_heap(bytes, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate_nothrow_heap(bytes, static_cast<std::size_t>(alignment));
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete(void* value, std::align_val_t) noexcept { SDL_aligned_free(value); }
void operator delete[](void* value, std::align_val_t) noexcept { SDL_aligned_free(value); }
void operator delete(void* value, std::size_t, std::align_val_t) noexcept { SDL_aligned_free(value); }
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept { SDL_aligned_free(value); }
void operator delete(void* value, std::align_val_t, const std::nothrow_t&) noexcept {
    SDL_aligned_free(value);
}
void operator delete[](void* value, std::align_val_t, const std::nothrow_t&) noexcept {
    SDL_aligned_free(value);
}
#endif

namespace wormedit::sys {
namespace {
#ifndef NDEBUG
thread_local frame_heap_allocation_t heap_allocations{};
thread_local bool heap_hook_seen{};
#endif
void report_phase_heap_allocations(std::FILE* output, const frame_timing_t& timer) {
    for (const auto phase : frame_phase_display_order) {
        const auto count = timer.heap_summary(phase);
        if (count.count == 0) continue;
        const auto bytes = timer.heap_summary(phase, true);
        std::fprintf(output,
            "%s C++ new: average %.3f, p99 %.0f; requested bytes average %.3f, p99 %.0f\n",
            frame_phase_names[static_cast<std::size_t>(phase)], count.average, count.p99,
            bytes.average, bytes.p99);
    }
}
void report_heap_allocations(std::FILE* output, const frame_timing_t& timer) {
    const auto count = timer.heap_summary(frame_phase_t::workspace);
    const auto bytes = timer.heap_summary(frame_phase_t::workspace, true);
    if (count.count == 0) {
        std::fprintf(output, "Projection C++ heap allocations: unavailable (allocation hook inactive)\n");
        return;
    }
    std::fprintf(output,
        "Projection C++ heap allocations: average %.3f, p99 %.0f (%zu samples)\n",
        count.average, count.p99, count.count);
    std::fprintf(output,
        "Projection C++ heap requested bytes: average %.3f, p99 %.0f (%zu samples)\n",
        bytes.average, bytes.p99, bytes.count);
    const auto frame = timer.heap_summary();
    std::fprintf(output, "Frame C++ heap allocations: average %.3f, p99 %.0f (%zu samples)\n",
        frame.average, frame.p99, frame.count);
    report_phase_heap_allocations(output, timer);
}
auto milliseconds(std::uint64_t ticks) -> double {
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}
void report_if_requested(const frame_timing_t& timer) {
    // Optional diagnostic artifact for a real-window run. No I/O unless explicitly enabled.
    const auto* path = std::getenv("WORMEDIT_FRAME_TIMING_REPORT");
    if (!path || !*path)
        return;
    auto* output = std::fopen(path, "a");
    if (!output)
        return;
    const auto frame = timer.summary();
    std::fprintf(output, "Last %zu frames: average %.6f ms, p99 %.6f ms\n", frame.count,
                 frame.average_ms, frame.p99_ms);
    for (std::size_t i = 0; i < frame_phase_count; ++i) {
        const auto phase = frame_phase_display_order[i];
        const auto index = static_cast<std::size_t>(phase);
        const auto summary = timer.summary(phase);
        if (summary.count == 0)
            std::fprintf(output, "%s: N/A\n", frame_phase_names[index]);
        else
            std::fprintf(output, "%s: average %.6f ms, p99 %.6f ms (%zu samples)\n",
                         frame_phase_names[index], summary.average_ms, summary.p99_ms,
                         summary.count);
    }
    report_heap_allocations(output, timer);
    std::fprintf(output, "GPU work per frame:\n");
    for (std::size_t metric = 0; metric < frame_gpu_work_count; ++metric) {
        const auto summary = timer.gpu_work_summary(metric);
        if (summary.count == 0)
            std::fprintf(output, "%s: N/A\n", frame_gpu_work_names[metric]);
        else
            std::fprintf(output, "%s: average %.3f, p99 %.0f (%zu samples)\n",
                frame_gpu_work_names[metric], summary.average, summary.p99, summary.count);
    }
    std::fclose(output);
}
} // namespace
void record_frame_heap_allocation(std::size_t bytes) noexcept {
#ifndef NDEBUG
    heap_hook_seen = true;
    ++heap_allocations.count;
    heap_allocations.bytes += bytes;
#else
    (void)bytes;
#endif
}
auto frame_heap_allocation_snapshot() noexcept -> frame_heap_allocation_t {
#ifndef NDEBUG
    return heap_allocations;
#else
    return {};
#endif
}
auto frame_phase_parent(frame_phase_t phase) -> frame_phase_t {
    if (phase == frame_phase_t::audio_service || phase == frame_phase_t::audio_pump)
        return frame_phase_t::audio;
    if (phase >= frame_phase_t::workspace_flatten && phase <= frame_phase_t::workspace_bmson)
        return frame_phase_t::workspace;
    if (phase == frame_phase_t::render_convert || phase == frame_phase_t::render_submit ||
        phase == frame_phase_t::render_present) return frame_phase_t::render;
    return frame_phase_t::count;
}
auto frame_timing() -> frame_timing_t& {
    static frame_timing_t timer;
    return timer;
}
void frame_timing_t::begin_frame() {
    current_ = {};
#ifndef NDEBUG
    current_.heap_measured = heap_hook_seen;
#endif
    heap_begin_ = heap_last_ = frame_heap_allocation_snapshot();
    phase_ = frame_phase_t::count;
    begin_ = last_ = SDL_GetPerformanceCounter();
    active_ = true;
}
void frame_timing_t::checkpoint() {
    if (!active_)
        return;
    const auto now = SDL_GetPerformanceCounter();
    const auto heap = frame_heap_allocation_snapshot();
    const frame_heap_allocation_t delta{heap.count - heap_last_.count,
                                      heap.bytes - heap_last_.bytes};
    if (phase_ != frame_phase_t::count) {
        const auto index = static_cast<std::size_t>(phase_);
        current_.phase_ms[index] += milliseconds(now - last_);
        current_.measured[index] = true;
        current_.phase_heap[index].count += delta.count;
        current_.phase_heap[index].bytes += delta.bytes;
        const auto parent = frame_phase_parent(phase_);
        if (parent != frame_phase_t::count) {
            const auto parent_index = static_cast<std::size_t>(parent);
            current_.phase_ms[parent_index] += milliseconds(now - last_);
            current_.measured[parent_index] = true;
        }
        // Scene time remains exclusive, as before. Heap accounting includes
        // Compose scene preparation in the inclusive projection allocation count.
        const auto heap_parent = phase_ == frame_phase_t::scene
            ? frame_phase_t::workspace : parent;
        if (heap_parent != frame_phase_t::count) {
            auto& parent_heap = current_.phase_heap[static_cast<std::size_t>(heap_parent)];
            parent_heap.count += delta.count;
            parent_heap.bytes += delta.bytes;
        }
    }
    heap_last_ = heap;
    last_ = now;
}
auto frame_timing_t::enter(frame_phase_t phase) -> frame_phase_t {
    checkpoint();
    const auto previous = phase_;
    phase_ = phase;
    return previous;
}
void frame_timing_t::restore(frame_phase_t phase) {
    checkpoint();
    phase_ = phase;
}
void frame_timing_t::end_frame() {
    if (!active_)
        return;
    checkpoint();
    current_.frame_ms = milliseconds(last_ - begin_);
#ifndef NDEBUG
    current_.heap_measured = heap_hook_seen;
#endif
    current_.frame_heap = {heap_last_.count - heap_begin_.count,
                           heap_last_.bytes - heap_begin_.bytes};
    record(current_);
    active_ = false;
    if (next_ == 0)
        report_if_requested(*this);
}
void frame_timing_t::record(const frame_timing_sample_t& sample) {
    samples_[next_] = sample;
    next_ = (next_ + 1) % frame_timing_capacity;
    size_ = std::min(size_ + 1, frame_timing_capacity);
}
void frame_timing_t::record_gpu_work(const frame_gpu_work_t& work) {
    if (active_) current_.gpu_work = work;
}
auto frame_timing_t::gpu_work_summary(std::size_t metric) const -> frame_counter_summary_t {
    frame_counter_summary_t result;
    if (metric >= frame_gpu_work_count) return result;
    std::array<double, frame_timing_capacity> sorted{};
    for (std::size_t frame = 0; frame < size_; ++frame) {
        const auto& work = sample(frame).gpu_work;
        if (!work.measured) continue;
        const auto value = static_cast<double>(work.values[metric]);
        sorted[result.count++] = value;
        result.average += value;
    }
    if (result.count == 0) return result;
    result.average /= static_cast<double>(result.count);
    std::sort(sorted.begin(), sorted.begin() + result.count);
    result.p99 = sorted[(result.count * 99 + 99) / 100 - 1];
    return result;
}
auto frame_timing_t::heap_summary(frame_phase_t phase, bool bytes) const
    -> frame_counter_summary_t {
    frame_counter_summary_t result;
    std::array<double, frame_timing_capacity> sorted{};
    const auto index = static_cast<std::size_t>(phase);
    if (index > frame_phase_count) return result;
    for (std::size_t frame = 0; frame < size_; ++frame) {
        const auto& entry = sample(frame);
        if (!entry.heap_measured || (phase != frame_phase_t::count && !entry.measured[index]))
            continue;
        const auto heap = phase == frame_phase_t::count ? entry.frame_heap : entry.phase_heap[index];
        const auto value = static_cast<double>(bytes ? heap.bytes : heap.count);
        sorted[result.count++] = value;
        result.average += value;
    }
    if (result.count == 0) return result;
    result.average /= static_cast<double>(result.count);
    std::sort(sorted.begin(), sorted.begin() + result.count);
    result.p99 = sorted[(result.count * 99 + 99) / 100 - 1];
    return result;
}
auto frame_timing_t::sample(std::size_t index) const -> const frame_timing_sample_t& {
    assert(index < size_);
    return samples_[(next_ + frame_timing_capacity - size_ + index) % frame_timing_capacity];
}
auto frame_timing_t::summary(frame_phase_t phase) const -> frame_timing_summary_t {
    std::array<double, frame_timing_capacity> sorted{};
    frame_timing_summary_t result;
    for (std::size_t i = 0; i < size_; ++i) {
        const auto& value = sample(i);
        const auto index = static_cast<std::size_t>(phase);
        if (phase != frame_phase_t::count && !value.measured[index])
            continue;
        const auto duration =
            phase == frame_phase_t::count ? value.frame_ms : value.phase_ms[index];
        sorted[result.count++] = duration;
        result.average_ms += duration;
    }
    if (result.count == 0)
        return result;
    result.average_ms /= static_cast<double>(result.count);
    std::sort(sorted.begin(), sorted.begin() + result.count);
    result.p99_ms = sorted[(result.count * 99 + 99) / 100 - 1];
    return result;
}
frame_phase_scope_t::frame_phase_scope_t(frame_phase_t phase, frame_timing_t& timer)
    : timer_(timer), previous_(timer.enter(phase)) {}
frame_phase_scope_t::~frame_phase_scope_t() {
    timer_.restore(previous_);
}
} // namespace wormedit::sys

WormUiFrameTimingView worm_ui_frame_timing_view() {
    using namespace wormedit::sys;
    const auto& timer = frame_timing();
    static_assert(WORM_UI_FRAME_TIMING_PHASE_COUNT == frame_phase_count);
    WormUiFrameTimingView result{};
    result.count = timer.size();
    for (std::size_t i = 0; i < result.count; ++i)
        result.frame_ms[i] = timer.sample(i).frame_ms;
    const auto frame = timer.summary();
    result.frame = {frame.count, frame.average_ms, frame.p99_ms};
    for (std::size_t i = 0; i < frame_phase_count; ++i) {
        const auto phase = frame_phase_display_order[i];
        const auto index = static_cast<std::size_t>(phase);
        const auto summary = timer.summary(phase);
        result.phase_names[i] = frame_phase_names[index];
        result.phases[i] = {summary.count, summary.average_ms, summary.p99_ms};
    }
    if (timer.size() != 0) {
        const auto& latest = timer.sample(timer.size() - 1);
        result.heap_allocations_available = latest.heap_measured;
        const auto heap = latest.phase_heap[static_cast<std::size_t>(frame_phase_t::workspace)];
        result.projection_heap_allocations_latest = heap.count;
        result.projection_heap_bytes_latest = heap.bytes;
    }
    const auto allocation_count = timer.heap_summary(frame_phase_t::workspace);
    const auto allocation_bytes = timer.heap_summary(frame_phase_t::workspace, true);
    result.projection_heap_allocations = {allocation_count.count, allocation_count.average,
                                          allocation_count.p99};
    result.projection_heap_bytes = {allocation_bytes.count, allocation_bytes.average,
                                    allocation_bytes.p99};
    static_assert(WORM_UI_FRAME_GPU_WORK_COUNT == frame_gpu_work_count);
    if (timer.size() != 0) {
        const auto& work = timer.sample(timer.size() - 1).gpu_work;
        result.gpu_work_available = work.measured;
        result.gpu_work_latest = {work.values[0], work.values[1], work.values[2], work.values[3], work.values[4]};
    }
    for (std::size_t metric = 0; metric < frame_gpu_work_count; ++metric) {
        const auto summary = timer.gpu_work_summary(metric);
        result.gpu_work_names[metric] = frame_gpu_work_names[metric];
        result.gpu_work[metric] = {summary.count, summary.average, summary.p99};
    }
    return result;
}

void worm_ui_frame_timing_record_gpu_work(WormUiFrameGpuWork work) {
    wormedit::sys::frame_timing().record_gpu_work({
        {work.vertices, work.indices, work.draw_calls, work.clip_rect_changes, work.texture_switches}, true});
}

uint32_t worm_ui_frame_render_phase_begin(WormUiFrameRenderPhase phase) {
    using namespace wormedit::sys;
    constexpr std::array phases{frame_phase_t::render_convert, frame_phase_t::render_submit,
                               frame_phase_t::render_present};
    const auto index = static_cast<std::size_t>(phase);
    if (index >= phases.size()) return static_cast<uint32_t>(frame_phase_count);
    return static_cast<uint32_t>(frame_timing().enter(phases[index]));
}
void worm_ui_frame_render_phase_end(uint32_t previous) {
    using namespace wormedit::sys;
    if (previous <= frame_phase_count)
        frame_timing().restore(static_cast<frame_phase_t>(previous));
}
