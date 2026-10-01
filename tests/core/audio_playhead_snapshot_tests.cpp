#include <wormedit/audio/playhead_snapshot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <barrier>
#include <atomic>
#include <thread>

using wormedit::audio::playhead_snapshot_t;
using wormedit::audio::playhead_state_t;

TEST_CASE("playhead snapshot starts inactive and returns published values",
          "[unit][core][audio][playhead]") {
    playhead_snapshot_t snapshot;
    CHECK_FALSE(snapshot.read().active);
    snapshot.publish({.active = true, .timeline_frame = 480, .start_frame = 100, .generation = 3});
    const auto state = snapshot.read();
    CHECK(state.active);
    CHECK(state.timeline_frame == 480);
    CHECK(state.start_frame == 100);
    CHECK(state.generation == 3);
}

TEST_CASE("playhead snapshot readers never observe a torn state under a concurrent writer",
          "[unit][core][audio][playhead]") {
    // Every published state keeps timeline_frame == start_frame + generation * 7, so any
    // mixed read of two publications breaks the relation.
    playhead_snapshot_t snapshot;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (std::uint64_t generation = 1; generation <= 200000; ++generation) {
            const auto start = generation * 3;
            snapshot.publish({.active = true,
                              .timeline_frame = start + generation * 7,
                              .start_frame = start,
                              .generation = generation});
        }
        done = true;
    });
    std::uint64_t torn = 0;
    std::uint64_t last_generation = 0;
    std::uint64_t regressions = 0;
    while (!done.load()) {
        const auto state = snapshot.read();
        if (state.generation == 0) continue;
        if (state.timeline_frame != state.start_frame + state.generation * 7 ||
            state.start_frame != state.generation * 3) {
            ++torn;
        }
        if (state.generation < last_generation) ++regressions;
        last_generation = state.generation;
    }
    writer.join();
    CHECK(torn == 0);
    CHECK(regressions == 0);
    CHECK(snapshot.read().generation == 200000);
}

TEST_CASE("multiple playhead readers return coherent live or bounded fallback states",
          "[unit][core][audio][playhead][integration-fix]") {
    playhead_snapshot_t snapshot;
    std::barrier start{5};
    std::atomic<bool> done{};
    std::atomic<unsigned> torn{};
    std::atomic<unsigned> reads{};
    std::array<std::jthread, 4> readers;
    for (auto& reader : readers) reader = std::jthread([&] {
        start.arrive_and_wait();
        do {
            const auto state = snapshot.read();
            if (state.timeline_frame != state.start_frame + state.generation * 7 ||
                state.start_frame != state.generation * 3 || state.active != (state.generation != 0))
                ++torn;
            ++reads;
        } while (!done.load());
    });
    start.arrive_and_wait();
    for (std::uint64_t generation = 1; generation <= 100000; ++generation)
        snapshot.publish({true, generation * 10, generation * 3, generation});
    done = true;
    for (auto& reader : readers) reader.join();
    CHECK(reads.load() > 0);
    CHECK(torn.load() == 0);
    CHECK(snapshot.read().generation == 100000);
}
