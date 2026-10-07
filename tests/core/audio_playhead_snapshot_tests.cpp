#include <wormedit/audio/playhead_snapshot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <barrier>
#include <atomic>
#include <cstddef>
#include <new>
#include <thread>

namespace wormedit::audio {
struct playhead_snapshot_test_access_t {
    static void contend(playhead_snapshot_t& snapshot) {
        snapshot.sequence_.store(3, std::memory_order_relaxed);
    }
    static void release(playhead_snapshot_t& snapshot) {
        snapshot.sequence_.store(4, std::memory_order_release);
    }
};
} // namespace wormedit::audio

using wormedit::audio::playhead_snapshot_t;
using wormedit::audio::playhead_state_t;

namespace {
auto per_reader_fallbacks_remain_independent() -> bool {
    playhead_snapshot_t snapshot;
    snapshot.publish({.active = true, .timeline_frame = 10, .start_frame = 2,
                      .generation = 5, .transport_generation = 71});
    std::barrier after_first{2};
    std::barrier after_second{2};
    std::barrier fallback_gate{3};
    playhead_state_t first{}, second{}, first_fallback{}, second_fallback{};
    std::jthread first_reader([&] {
        first = snapshot.read();
        after_first.arrive_and_wait();
        fallback_gate.arrive_and_wait();
        first_fallback = snapshot.read();
    });
    after_first.arrive_and_wait();
    snapshot.publish({.active = true, .timeline_frame = 20, .start_frame = 2,
                      .generation = 5, .transport_generation = 72});
    std::jthread second_reader([&] {
        second = snapshot.read();
        after_second.arrive_and_wait();
        fallback_gate.arrive_and_wait();
        second_fallback = snapshot.read();
    });
    after_second.arrive_and_wait();
    wormedit::audio::playhead_snapshot_test_access_t::contend(snapshot);
    fallback_gate.arrive_and_wait();
    first_reader.join();
    second_reader.join();
    wormedit::audio::playhead_snapshot_test_access_t::release(snapshot);
    return first.timeline_frame == 10 && first.transport_generation == 71 &&
        second.timeline_frame == 20 && second.transport_generation == 72 &&
        first_fallback.timeline_frame == 10 && first_fallback.transport_generation == 71 &&
        second_fallback.timeline_frame == 20 && second_fallback.transport_generation == 72;
}

auto stale_reader_fallback_is_inactive() -> bool {
    playhead_snapshot_t snapshot;
    snapshot.publish({.active = true, .timeline_frame = 50, .start_frame = 10,
                      .generation = 4, .transport_generation = 8});
    std::barrier reader_ready{2};
    std::barrier read_again{2};
    playhead_state_t prior{}, fallback{};
    std::jthread reader([&] {
        prior = snapshot.read();
        reader_ready.arrive_and_wait();
        read_again.arrive_and_wait();
        fallback = snapshot.read();
    });
    reader_ready.arrive_and_wait();
    snapshot.publish({.active = false, .timeline_frame = 0, .start_frame = 0,
                      .generation = 5, .transport_generation = 9});
    wormedit::audio::playhead_snapshot_test_access_t::contend(snapshot);
    read_again.arrive_and_wait();
    reader.join();
    wormedit::audio::playhead_snapshot_test_access_t::release(snapshot);
    return prior.active && prior.generation == 4 && !fallback.active &&
        fallback.generation == 5 && fallback.timeline_frame == 0 &&
        fallback.transport_generation == 0;
}

auto destroyed_snapshot_identity_cannot_reuse_fallback() -> bool {
    alignas(playhead_snapshot_t) std::byte storage[sizeof(playhead_snapshot_t)];
    auto* original = new (storage) playhead_snapshot_t{};
    original->publish({.active = true, .timeline_frame = 77, .start_frame = 7,
                       .generation = 3, .transport_generation = 4});
    const auto prior = original->read();
    original->~playhead_snapshot_t();
    auto* replacement = new (storage) playhead_snapshot_t{};
    wormedit::audio::playhead_snapshot_test_access_t::contend(*replacement);
    const auto fallback = replacement->read();
    wormedit::audio::playhead_snapshot_test_access_t::release(*replacement);
    replacement->~playhead_snapshot_t();
    return prior.active && fallback.generation == 0 && !fallback.active &&
        fallback.timeline_frame == 0 && fallback.transport_generation == 0;
}

auto evicted_reader_slot_falls_back_inactive() -> bool {
    std::array<playhead_snapshot_t, 9> snapshots;
    snapshots[0].publish({.active = true, .timeline_frame = 99, .start_frame = 9,
                          .generation = 11, .transport_generation = 5});
    const auto first = snapshots[0].read();
    for (std::size_t index = 1; index < snapshots.size(); ++index) {
        snapshots[index].publish({.active = true, .timeline_frame = index,
            .start_frame = index, .generation = index + 20, .transport_generation = index + 30});
        static_cast<void>(snapshots[index].read());
    }
    wormedit::audio::playhead_snapshot_test_access_t::contend(snapshots[0]);
    const auto fallback = snapshots[0].read();
    wormedit::audio::playhead_snapshot_test_access_t::release(snapshots[0]);
    return first.timeline_frame == 99 && !fallback.active && fallback.generation == 11 &&
        fallback.timeline_frame == 0 && fallback.transport_generation == 0;
}
auto coherent_stress_state(const playhead_state_t& state) -> bool {
    if (!state.active)
        return state.timeline_frame == 0 && state.start_frame == 0 &&
               state.transport_generation == 0 && !state.source_finished;
    return state.generation != 0 &&
           state.timeline_frame == state.start_frame + state.generation * 7 &&
           state.start_frame == state.generation * 3 &&
           state.transport_generation == state.generation * 11 &&
           state.source_finished == ((state.generation & 1U) != 0);
}
} // namespace

TEST_CASE("playhead snapshot starts inactive and returns published values",
          "[unit][core][audio][playhead]") {
    playhead_snapshot_t snapshot;
    CHECK_FALSE(snapshot.read().active);
    snapshot.publish({.active = true, .timeline_frame = 480, .start_frame = 100,
                      .generation = 3, .transport_generation = 17});
    const auto state = snapshot.read();
    CHECK(state.active);
    CHECK(state.timeline_frame == 480);
    CHECK(state.start_frame == 100);
    CHECK(state.generation == 3);
    CHECK(state.transport_generation == 17);
}

TEST_CASE("playhead snapshot contention reuses each reader's own coherent value",
          "[unit][core][audio][playhead]") {
    CHECK(per_reader_fallbacks_remain_independent());
}

TEST_CASE("playhead snapshot never falls back across generations",
          "[unit][core][audio][playhead]") {
    CHECK(stale_reader_fallback_is_inactive());
}

TEST_CASE("playhead snapshot identity prevents fallback after address reuse",
          "[unit][core][audio][playhead]") {
    CHECK(destroyed_snapshot_identity_cannot_reuse_fallback());
}

TEST_CASE("playhead snapshot reader cache eviction returns an inactive state",
          "[unit][core][audio][playhead]") {
    CHECK(evicted_reader_slot_falls_back_inactive());
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
                              .generation = generation,
                              .transport_generation = generation * 11,
                              .source_finished = (generation & 1U) != 0});
        }
        done = true;
    });
    std::uint64_t torn = 0;
    std::uint64_t last_generation = 0;
    std::uint64_t regressions = 0;
    while (!done.load()) {
        const auto state = snapshot.read();
        if (state.generation == 0) continue;
        if (!coherent_stress_state(state)) {
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
            if (!coherent_stress_state(state))
                ++torn;
            ++reads;
        } while (!done.load());
    });
    start.arrive_and_wait();
    for (std::uint64_t generation = 1; generation <= 100000; ++generation)
        snapshot.publish({true, generation * 10, generation * 3, generation,
                          generation * 11, (generation & 1U) != 0});
    done = true;
    for (auto& reader : readers) reader.join();
    CHECK(reads.load() > 0);
    CHECK(torn.load() == 0);
    CHECK(snapshot.read().generation == 100000);
}
