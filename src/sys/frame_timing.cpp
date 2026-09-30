#include "sys/frame_timing.h"
#include "ui/frontend.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace wormedit::sys {
namespace {
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
    std::fclose(output);
}
} // namespace
auto frame_phase_parent(frame_phase_t phase) -> frame_phase_t {
    if (phase == frame_phase_t::audio_service || phase == frame_phase_t::audio_pump)
        return frame_phase_t::audio;
    if (phase >= frame_phase_t::workspace_flatten && phase <= frame_phase_t::workspace_bmson)
        return frame_phase_t::workspace;
    return frame_phase_t::count;
}
auto frame_timing() -> frame_timing_t& {
    static frame_timing_t timer;
    return timer;
}
void frame_timing_t::begin_frame() {
    current_ = {};
    phase_ = frame_phase_t::count;
    begin_ = last_ = SDL_GetPerformanceCounter();
    active_ = true;
}
void frame_timing_t::checkpoint() {
    if (!active_)
        return;
    const auto now = SDL_GetPerformanceCounter();
    if (phase_ != frame_phase_t::count) {
        const auto index = static_cast<std::size_t>(phase_);
        current_.phase_ms[index] += milliseconds(now - last_);
        current_.measured[index] = true;
        const auto parent = frame_phase_parent(phase_);
        if (parent != frame_phase_t::count) {
            const auto parent_index = static_cast<std::size_t>(parent);
            current_.phase_ms[parent_index] += milliseconds(now - last_);
            current_.measured[parent_index] = true;
        }
    }
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
    return result;
}
