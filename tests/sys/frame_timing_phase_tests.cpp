// Frame timing samples, phase accounting and GPU workload statistics.
#include "frame_timing_support.h"
#include <wormedit/core/recovery_controller.h>
#include "ui/field/color.h"
#include "ui/frontend/runtime.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <SDL3/SDL_timer.h>
#include <cstddef>
#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif

using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("Frame timing retains exactly 240 chronological samples and nearest rank p99", "[sys][frame-timing]") {
    frame_timing_t timer;

    for (std::size_t i = 1; i <= 300; ++i) {
        frame_timing_sample_t sample;
        sample.frame_ms = static_cast<double>(i);
        sample.measured[1] = true;
        sample.phase_ms[1] = static_cast<double>(i) / 100.0;
        timer.record(sample);
    }
    REQUIRE(timer.size() == 240);
    CHECK(timer.sample(0).frame_ms == 61.0);
    CHECK(timer.sample(239).frame_ms == 300.0);
    CHECK(timer.summary(frame_phase_t::audio).p99_ms == Catch::Approx(2.98));

}

TEST_CASE("Frame timing nested phases exclude child work from parent", "[sys][frame-timing]") {
    frame_timing_t timer;
    timer.begin_frame();
    {
        frame_phase_scope_t workspace(frame_phase_t::workspace, timer);
        SDL_Delay(2);
        {
            frame_phase_scope_t scene(frame_phase_t::scene, timer);
            SDL_Delay(5);
        }
        SDL_Delay(2);
    }
    timer.end_frame();
    REQUIRE(timer.size() == 1);
    const auto& sample = timer.sample(0);
    const auto workspace = sample.phase_ms[2];
    const auto scene = sample.phase_ms[3];
    CHECK(workspace >= 3.0);
    CHECK(scene >= 4.0);
    CHECK(workspace + scene <= sample.frame_ms + 0.001);
    CHECK_FALSE(sample.measured[1]);
}

#if defined(__unix__) || defined(__APPLE__)

namespace {
auto test_thread_cpu_nanoseconds() -> std::uint64_t {
    timespec now{};
    REQUIRE(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0);
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL + now.tv_nsec;
}

// One frame with a spinning workspace phase (6 ms) and a sleeping scene phase (8 ms).
void time_busy_then_waiting_frame(frame_timing_t& timer) {
    timer.begin_frame();
    {
        frame_phase_scope_t busy(frame_phase_t::workspace, timer);
        const auto until = test_thread_cpu_nanoseconds() + 6'000'000;
        volatile std::uint64_t spin = 0;
        while (test_thread_cpu_nanoseconds() < until) spin = spin + 1;
    }
    {
        frame_phase_scope_t waiting(frame_phase_t::scene, timer);
        SDL_Delay(8);
    }
    timer.end_frame();
}
} // namespace

TEST_CASE("Frame timing separates CPU time from waiting in each phase", "[sys][frame-timing]") {
    frame_timing_t timer;
    time_busy_then_waiting_frame(timer);
    REQUIRE(timer.size() == 1);
    const auto& sample = timer.sample(0);
    REQUIRE(sample.cpu_measured);
    const auto busy = static_cast<std::size_t>(frame_phase_t::workspace);
    const auto waiting = static_cast<std::size_t>(frame_phase_t::scene);
    // CPU work remains measurable even if another task preempts this thread.
    CHECK(sample.phase_cpu_ms[busy] >= 5.0);
    CHECK(sample.phase_cpu_ms[waiting] <= sample.phase_ms[waiting] * 0.25);
    CHECK(sample.frame_cpu_ms <= sample.frame_ms + 0.5);
    CHECK(timer.cpu_summary(frame_phase_t::scene).count == 1);
}
#endif

#if defined(__linux__)

TEST_CASE("Frame timing counts the context switches of a frame", "[sys][frame-timing]") {
    frame_timing_t timer;
    time_busy_then_waiting_frame(timer);
    const auto& sample = timer.sample(0);
    REQUIRE(sample.switches_measured);
    CHECK(sample.voluntary_switches >= 1); // the sleep yields the CPU
}
#endif

TEST_CASE("Frame timing hidden collection allocates no C++ heap storage", "[sys][frame-timing]") {
    frame_timing_t timer;
    // Warm SDL's counters before tracking the instrumentation's allocations.
    timer.begin_frame();
    timer.end_frame();
    allocations = 0;
    count_allocations = true;
    for (std::size_t i = 0; i < 240; ++i) {
        timer.begin_frame();
        {
            frame_phase_scope_t workspace(frame_phase_t::workspace, timer);
            frame_phase_scope_t scene(frame_phase_t::scene, timer);
        }
        timer.end_frame();
    }
    count_allocations = false;
    CHECK(allocations == 0);
    CHECK(timer.size() == 240);
}

TEST_CASE("Frame timing child phases accumulate into comparable parent totals", "[sys][frame-timing]") {
    frame_timing_t timer;
    timer.begin_frame();
    {
        frame_phase_scope_t workspace(frame_phase_t::workspace, timer);
        SDL_Delay(2);
        frame_phase_scope_t flatten(frame_phase_t::workspace_flatten, timer);
        SDL_Delay(2);
    }
    timer.end_frame();
    CHECK(timer.summary(frame_phase_t::workspace_flatten).count == 1);
    CHECK(timer.summary(frame_phase_t::workspace).average_ms >=
          timer.summary(frame_phase_t::workspace_flatten).average_ms + 1.0);
    CHECK(frame_phase_parent(frame_phase_t::audio_pump) == frame_phase_t::audio);
    CHECK(frame_phase_parent(frame_phase_t::scene) == frame_phase_t::count);
}

TEST_CASE("GPU workload keeps unique frame counters and nearest rank statistics", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    for (std::uint64_t frame = 1; frame <= 300; ++frame) {
        frame_timing_sample_t sample;
        sample.gpu_work = {{frame, frame * 3, 4, 3, 2}, true};
        timer.record(sample);
    }
    expect_gpu_work(timer.size() == 240);
    expect_gpu_work(timer.sample(0).gpu_work.values[0] == 61);
    expect_gpu_work(timer.gpu_work_summary(0).count == 240);
    expect_gpu_work(timer.gpu_work_summary(0).average == 180.5);
    expect_gpu_work(timer.gpu_work_summary(0).p99 == 298);
    expect_gpu_work(timer.gpu_work_summary(1).p99 == 894);
    expect_gpu_work(timer.gpu_work_summary(2).average == 4);
    expect_gpu_work(timer.gpu_work_summary(frame_gpu_work_count).count == 0);
}

TEST_CASE("GPU workload distinguishes unrendered frames from measured empty frames", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    timer.begin_frame();
    timer.record_gpu_work({{100, 300, 2, 1, 0}, true});
    timer.end_frame();
    timer.begin_frame();
    timer.end_frame();
    expect_gpu_work(!timer.sample(1).gpu_work.measured);
    expect_gpu_work(timer.gpu_work_summary(0).count == 1);
    expect_gpu_work(timer.gpu_work_summary(0).average == 100);
    timer.begin_frame();
    timer.record_gpu_work({{}, true});
    timer.end_frame();
    expect_gpu_work(timer.gpu_work_summary(0).count == 2);
    expect_gpu_work(timer.gpu_work_summary(0).average == 50);
    timer.record_gpu_work({{999, 999, 999, 999, 999}, true});
    expect_gpu_work(timer.sample(2).gpu_work.values[0] == 0);
}

TEST_CASE("Diagnostic GPU work is recorded and summarized apart from application work", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    timer.begin_frame();
    timer.record_gpu_work({{100, 300, 5, 4, 3}, true});
    timer.record_gpu_work({{40, 60, 2, 1, 1}, true}, frame_gpu_work_source_t::diagnostic);
    timer.end_frame();
    timer.begin_frame();
    timer.record_gpu_work({{200, 500, 7, 6, 5}, true});
    timer.end_frame();
    const auto& first = timer.sample(0);
    expect_gpu_work(first.gpu_work.values[0] == 100 && first.diagnostic_gpu_work.values[0] == 40);
    expect_gpu_work(!timer.sample(1).diagnostic_gpu_work.measured);
    expect_gpu_work(&frame_timing_t::gpu_work(first, frame_gpu_work_source_t::diagnostic) ==
                    &first.diagnostic_gpu_work);
    expect_gpu_work(timer.gpu_work_summary(0).count == 2);
    expect_gpu_work(timer.gpu_work_summary(0).average == 150);
    const auto diagnostic = timer.gpu_work_summary(2, frame_gpu_work_source_t::diagnostic);
    expect_gpu_work(diagnostic.count == 1 && diagnostic.average == 2 && diagnostic.p99 == 2);
}

TEST_CASE("Frame timing view exposes application and diagnostic GPU work separately", "[sys][frame-timing][gpu-work]") {
    auto& timer = frame_timing();
    timer.begin_frame();
    worm_ui_frame_timing_record_gpu_work({1000, 3000, 50, 20, 10});
    worm_ui_frame_timing_record_diagnostic_gpu_work({400, 700, 3, 2, 1});
    timer.end_frame();
    wormedit::ui::frame_timing_view_t view;
    worm_ui_frame_timing_view(view);
    expect_gpu_work(view.gpu_work_available && view.diagnostic_gpu_work_available);
    expect_gpu_work(view.gpu_work_latest.vertices == 1000 && view.gpu_work_latest.draw_calls == 50);
    expect_gpu_work(view.diagnostic_gpu_work_latest.vertices == 400);
    expect_gpu_work(view.diagnostic_gpu_work_latest.texture_switches == 1);
    expect_gpu_work(view.diagnostic_gpu_work[0].count >= 1);
    timer.begin_frame();
    worm_ui_frame_timing_record_gpu_work({1000, 3000, 50, 20, 10});
    timer.end_frame();
    worm_ui_frame_timing_view(view);
    expect_gpu_work(view.gpu_work_available && !view.diagnostic_gpu_work_available);
}

TEST_CASE("Render convert submit and present child phases retain inclusive parent timing", "[sys][frame-timing][gpu-work]") {
    auto& timer = frame_timing();
    timer.begin_frame();
    {
        frame_phase_scope_t render(frame_phase_t::render);
        const auto submit = worm_ui_frame_render_phase_begin(WORM_UI_FRAME_RENDER_SUBMIT);
        const auto convert = worm_ui_frame_render_phase_begin(WORM_UI_FRAME_RENDER_CONVERT);
        worm_ui_frame_render_phase_end(convert);
        worm_ui_frame_render_phase_end(submit);
        frame_phase_scope_t present(frame_phase_t::render_present);
    }
    timer.end_frame();
    const auto& sample = timer.sample(timer.size() - 1);
    const auto at = [](frame_phase_t phase) { return static_cast<std::size_t>(phase); };
    expect_gpu_work(sample.measured[at(frame_phase_t::render_convert)]);
    expect_gpu_work(sample.measured[at(frame_phase_t::render_submit)]);
    expect_gpu_work(sample.measured[at(frame_phase_t::render_present)]);
    const auto children = sample.phase_ms[at(frame_phase_t::render_convert)] +
        sample.phase_ms[at(frame_phase_t::render_submit)] + sample.phase_ms[at(frame_phase_t::render_present)];
    expect_gpu_work(sample.phase_ms[at(frame_phase_t::render)] + 0.001 >= children);
    expect_gpu_work(frame_phase_parent(frame_phase_t::render_convert) == frame_phase_t::render);
}

TEST_CASE("Hidden GPU workload collection and summaries allocate no C++ storage", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    timer.begin_frame();
    timer.end_frame();
    allocations = 0;
    count_allocations = true;
    for (std::uint64_t frame = 0; frame < frame_timing_capacity; ++frame) {
        timer.begin_frame();
        timer.record_gpu_work({{frame, frame * 3, 2, 1, 0}, true});
        {
            frame_phase_scope_t submit(frame_phase_t::render_submit, timer);
            frame_phase_scope_t convert(frame_phase_t::render_convert, timer);
        }
        timer.end_frame();
    }
    const auto summary = timer.gpu_work_summary(0);
    count_allocations = false;
    expect_gpu_work(allocations == 0);
    expect_gpu_work(summary.count == frame_timing_capacity);
}
