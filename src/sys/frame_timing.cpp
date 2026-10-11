#include "sys/frame_timing.h"
#include "ui/frontend.h"
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif
#if defined(__linux__)
#include <sys/resource.h>
#endif

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
void report_gpu_work(std::FILE* output, const frame_timing_t& timer,
                     frame_gpu_work_source_t source, const char* heading) {
    std::fprintf(output, "%s\n", heading);
    for (std::size_t metric = 0; metric < frame_gpu_work_count; ++metric) {
        const auto summary = timer.gpu_work_summary(metric, source);
        if (summary.count == 0)
            std::fprintf(output, "%s: N/A\n", frame_gpu_work_names[metric]);
        else
            std::fprintf(output, "%s: average %.3f, p99 %.0f (%zu samples)\n",
                frame_gpu_work_names[metric], summary.average, summary.p99, summary.count);
    }
}
auto milliseconds(std::uint64_t ticks) -> double {
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}
// CPU time consumed by the calling thread, or a negative value where it is unavailable.
auto thread_cpu_ms() -> double {
#if defined(__unix__) || defined(__APPLE__)
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return -1.0;
    return static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1.0e6;
#else
    return -1.0;
#endif
}
// Context switches of the calling thread so far; false where the platform does not report them.
auto thread_switches(std::uint64_t& voluntary, std::uint64_t& involuntary) -> bool {
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_THREAD, &usage) != 0) return false;
    voluntary = static_cast<std::uint64_t>(usage.ru_nvcsw);
    involuntary = static_cast<std::uint64_t>(usage.ru_nivcsw);
    return true;
#else
    voluntary = involuntary = 0;
    return false;
#endif
}
// WORMEDIT_FRAME_SPIKE_LOG names a file that receives one entry per frame slower than
// WORMEDIT_FRAME_SPIKE_MS (default 12 ms): wall and CPU time of the frame and of every phase
// that took at least a millisecond, plus the frame's context switches.
struct spike_log_settings_t {
    const char* path{};
    double threshold_ms{12.0};
};
auto spike_log_settings() -> const spike_log_settings_t& {
    static const spike_log_settings_t settings = [] {
        spike_log_settings_t result;
        const auto* path = std::getenv("WORMEDIT_FRAME_SPIKE_LOG");
        if (path && *path) result.path = path;
        if (const auto* threshold = std::getenv("WORMEDIT_FRAME_SPIKE_MS")) {
            const auto parsed = std::strtod(threshold, nullptr);
            if (parsed > 0.0) result.threshold_ms = parsed;
        }
        return result;
    }();
    return settings;
}
void write_spike_entry(std::FILE* output, const frame_timing_sample_t& sample) {
    std::fprintf(output, "[%.3f s] frame %.3f ms wall", static_cast<double>(SDL_GetTicks()) / 1000.0,
                 sample.frame_ms);
    if (sample.cpu_measured) std::fprintf(output, ", %.3f ms cpu", sample.frame_cpu_ms);
    if (sample.switches_measured)
        std::fprintf(output, ", switches voluntary %llu involuntary %llu",
                     static_cast<unsigned long long>(sample.voluntary_switches),
                     static_cast<unsigned long long>(sample.involuntary_switches));
    std::fprintf(output, "\n");
    for (const auto phase : frame_phase_display_order) {
        const auto index = static_cast<std::size_t>(phase);
        if (!sample.measured[index] || sample.phase_ms[index] < 1.0) continue;
        std::fprintf(output, "  %s: %.3f ms wall", frame_phase_names[index], sample.phase_ms[index]);
        if (sample.cpu_measured) std::fprintf(output, ", %.3f ms cpu", sample.phase_cpu_ms[index]);
        std::fprintf(output, "\n");
    }
}
void log_spike_if_requested(const frame_timing_sample_t& sample) {
    const auto& settings = spike_log_settings();
    if (!settings.path || sample.frame_ms < settings.threshold_ms) return;
    auto* output = std::fopen(settings.path, "a");
    if (!output) return;
    write_spike_entry(output, sample);
    std::fclose(output);
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
    const auto frame_cpu = timer.cpu_summary();
    if (frame_cpu.count != 0)
        std::fprintf(output, "Frame CPU: average %.6f ms, p99 %.6f ms (%zu samples)\n",
                     frame_cpu.average_ms, frame_cpu.p99_ms, frame_cpu.count);
    for (std::size_t i = 0; i < frame_phase_count; ++i) {
        const auto phase = frame_phase_display_order[i];
        const auto index = static_cast<std::size_t>(phase);
        const auto summary = timer.summary(phase);
        if (summary.count == 0) {
            std::fprintf(output, "%s: N/A\n", frame_phase_names[index]);
            continue;
        }
        std::fprintf(output, "%s: average %.6f ms, p99 %.6f ms (%zu samples)",
                     frame_phase_names[index], summary.average_ms, summary.p99_ms, summary.count);
        const auto cpu = timer.cpu_summary(phase);
        if (cpu.count != 0)
            std::fprintf(output, "; cpu average %.6f ms, p99 %.6f ms", cpu.average_ms, cpu.p99_ms);
        std::fprintf(output, "\n");
    }
    report_heap_allocations(output, timer);
    report_gpu_work(output, timer, frame_gpu_work_source_t::application,
                    "GPU work per frame (application, excluding diagnostics):");
    report_gpu_work(output, timer, frame_gpu_work_source_t::diagnostic,
                    "GPU work per frame (diagnostic windows):");
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
    if (phase == frame_phase_t::events_input || phase == frame_phase_t::events_drops ||
        phase == frame_phase_t::events_jobs) return frame_phase_t::events;
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
    cpu_begin_ms_ = cpu_last_ms_ = thread_cpu_ms();
    current_.cpu_measured = cpu_begin_ms_ >= 0.0;
    current_.switches_measured = thread_switches(voluntary_begin_, involuntary_begin_);
    active_ = true;
}
void frame_timing_t::checkpoint() {
    if (!active_)
        return;
    const auto now = SDL_GetPerformanceCounter();
    const auto cpu_now = current_.cpu_measured ? thread_cpu_ms() : -1.0;
    const auto cpu_delta = cpu_now >= cpu_last_ms_ ? cpu_now - cpu_last_ms_ : 0.0;
    const auto heap = frame_heap_allocation_snapshot();
    const frame_heap_allocation_t delta{heap.count - heap_last_.count,
                                      heap.bytes - heap_last_.bytes};
    if (phase_ != frame_phase_t::count) {
        const auto index = static_cast<std::size_t>(phase_);
        current_.phase_ms[index] += milliseconds(now - last_);
        current_.phase_cpu_ms[index] += cpu_delta;
        current_.measured[index] = true;
        current_.phase_heap[index].count += delta.count;
        current_.phase_heap[index].bytes += delta.bytes;
        const auto parent = frame_phase_parent(phase_);
        if (parent != frame_phase_t::count) {
            const auto parent_index = static_cast<std::size_t>(parent);
            current_.phase_ms[parent_index] += milliseconds(now - last_);
            current_.phase_cpu_ms[parent_index] += cpu_delta;
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
    if (cpu_now >= 0.0) cpu_last_ms_ = cpu_now;
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
    if (current_.cpu_measured) current_.frame_cpu_ms = std::max(0.0, cpu_last_ms_ - cpu_begin_ms_);
    std::uint64_t voluntary{};
    std::uint64_t involuntary{};
    if (current_.switches_measured && thread_switches(voluntary, involuntary)) {
        current_.voluntary_switches = voluntary - voluntary_begin_;
        current_.involuntary_switches = involuntary - involuntary_begin_;
    }
#ifndef NDEBUG
    current_.heap_measured = heap_hook_seen;
#endif
    current_.frame_heap = {heap_last_.count - heap_begin_.count,
                           heap_last_.bytes - heap_begin_.bytes};
    record(current_);
    log_spike_if_requested(current_);
    active_ = false;
    if (next_ == 0)
        report_if_requested(*this);
}
void frame_timing_t::record(const frame_timing_sample_t& sample) {
    samples_[next_] = sample;
    next_ = (next_ + 1) % frame_timing_capacity;
    size_ = std::min(size_ + 1, frame_timing_capacity);
}
auto frame_timing_t::gpu_work(const frame_timing_sample_t& sample, frame_gpu_work_source_t source)
    -> const frame_gpu_work_t& {
    return source == frame_gpu_work_source_t::diagnostic ? sample.diagnostic_gpu_work
                                                         : sample.gpu_work;
}
void frame_timing_t::record_gpu_work(const frame_gpu_work_t& work, frame_gpu_work_source_t source) {
    if (!active_) return;
    if (source == frame_gpu_work_source_t::diagnostic) current_.diagnostic_gpu_work = work;
    else current_.gpu_work = work;
}
auto frame_timing_t::gpu_work_summary(std::size_t metric, frame_gpu_work_source_t source) const
    -> frame_counter_summary_t {
    frame_counter_summary_t result;
    if (metric >= frame_gpu_work_count) return result;
    std::array<double, frame_timing_capacity> sorted{};
    for (std::size_t frame = 0; frame < size_; ++frame) {
        const auto& work = gpu_work(sample(frame), source);
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
    return summarize(phase, false);
}
auto frame_timing_t::cpu_summary(frame_phase_t phase) const -> frame_timing_summary_t {
    return summarize(phase, true);
}
auto frame_timing_t::summarize(frame_phase_t phase, bool cpu) const -> frame_timing_summary_t {
    std::array<double, frame_timing_capacity> sorted{};
    frame_timing_summary_t result;
    const auto index = static_cast<std::size_t>(phase);
    for (std::size_t i = 0; i < size_; ++i) {
        const auto& value = sample(i);
        if ((phase != frame_phase_t::count && !value.measured[index]) || (cpu && !value.cpu_measured))
            continue;
        const auto duration = phase == frame_phase_t::count
            ? (cpu ? value.frame_cpu_ms : value.frame_ms)
            : (cpu ? value.phase_cpu_ms[index] : value.phase_ms[index]);
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

namespace {
using wormedit::sys::frame_gpu_work_count;
static_assert(WORM_UI_FRAME_GPU_WORK_COUNT == frame_gpu_work_count);
void view_gpu_work(const wormedit::sys::frame_timing_t& timer,
                   wormedit::sys::frame_gpu_work_source_t source, uint32_t& available,
                   WormUiFrameGpuWork& latest,
                   WormUiFrameGpuWorkStats (&stats)[WORM_UI_FRAME_GPU_WORK_COUNT]) {
    if (timer.size() != 0) {
        const auto& work = wormedit::sys::frame_timing_t::gpu_work(timer.sample(timer.size() - 1), source);
        available = work.measured;
        latest = {work.values[0], work.values[1], work.values[2], work.values[3], work.values[4]};
    }
    for (std::size_t metric = 0; metric < frame_gpu_work_count; ++metric) {
        const auto summary = timer.gpu_work_summary(metric, source);
        stats[metric] = {summary.count, summary.average, summary.p99};
    }
}
auto gpu_work_sample(const WormUiFrameGpuWork& work) -> wormedit::sys::frame_gpu_work_t {
    return {{work.vertices, work.indices, work.draw_calls, work.clip_rect_changes,
             work.texture_switches}, true};
}
} // namespace

WormUiFrameTimingView worm_ui_frame_timing_view() {
    using namespace wormedit::sys;
    const auto& timer = frame_timing();
    static_assert(WORM_UI_FRAME_TIMING_PHASE_COUNT == frame_phase_count);
    WormUiFrameTimingView result{};
    result.count = timer.size();
    for (std::size_t i = 0; i < result.count; ++i)
        result.frame_ms[i] = timer.sample(i).frame_ms;
    const auto frame = timer.summary();
    const auto frame_cpu = timer.cpu_summary();
    result.frame = {frame.count, frame.average_ms, frame.p99_ms, frame_cpu.count, frame_cpu.p99_ms};
    for (std::size_t i = 0; i < frame_phase_count; ++i) {
        const auto phase = frame_phase_display_order[i];
        const auto index = static_cast<std::size_t>(phase);
        const auto summary = timer.summary(phase);
        const auto cpu = timer.cpu_summary(phase);
        result.phase_names[i] = frame_phase_names[index];
        result.phases[i] = {summary.count, summary.average_ms, summary.p99_ms, cpu.count, cpu.p99_ms};
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
    for (std::size_t metric = 0; metric < frame_gpu_work_count; ++metric)
        result.gpu_work_names[metric] = frame_gpu_work_names[metric];
    view_gpu_work(timer, frame_gpu_work_source_t::application, result.gpu_work_available,
                  result.gpu_work_latest, result.gpu_work);
    view_gpu_work(timer, frame_gpu_work_source_t::diagnostic, result.diagnostic_gpu_work_available,
                  result.diagnostic_gpu_work_latest, result.diagnostic_gpu_work);
    return result;
}

void worm_ui_frame_timing_record_gpu_work(WormUiFrameGpuWork work) {
    wormedit::sys::frame_timing().record_gpu_work(gpu_work_sample(work));
}

void worm_ui_frame_timing_record_diagnostic_gpu_work(WormUiFrameGpuWork work) {
    wormedit::sys::frame_timing().record_gpu_work(
        gpu_work_sample(work), wormedit::sys::frame_gpu_work_source_t::diagnostic);
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
