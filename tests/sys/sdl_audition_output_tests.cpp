#include "sys/sdl_audition_output.h"

#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <array>
#include <algorithm>
#include <string>

namespace wormedit::sys {

struct sdl_audition_output_test_access_t {
    static void park_worker(sdl_audition_output_t& output) {
        {
            const std::scoped_lock lock(output.refill_mutex_);
            output.refill_worker_.request_stop();
        }
        output.refill_ready_.notify_all();
        output.refill_worker_.join();
        // Keep a live worker that cannot compete for the refill mutex during
        // the probe. Its local stop-aware wait exits when close requests stop.
        output.refill_worker_ = std::jthread{[](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any ready;
            std::unique_lock lock(mutex);
            ready.wait(lock, stop, [] { return false; });
        }};
    }

    static auto stop_token(sdl_audition_output_t& output) -> std::stop_token {
        return output.refill_worker_.get_stop_token();
    }

    static auto can_enter_wait(sdl_audition_output_t& output) -> bool {
        // Probe on a different thread: the stop callback runs synchronously
        // inside request_stop(), possibly while the caller owns this mutex.
        bool acquired = false;
        std::jthread probe{[&output, &acquired] {
            const std::unique_lock lock(output.refill_mutex_, std::try_to_lock);
            acquired = lock.owns_lock();
        }};
        probe.join();
        return acquired;
    }
};

} // namespace wormedit::sys

namespace {

struct audio_subsystem_t {
    ~audio_subsystem_t() {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

} // namespace

TEST_CASE("SDL dummy output drives and hard-stops a primed audition session",
          "[integration][audio][sdl][audition]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const audio_subsystem_t audio_subsystem;

    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_source({
        .asset = wormedit::asset_id_t{1},
        .path = fixture_path("keysound-c4.ogg"),
        .source = {"fixture:keysound-c4.ogg", "generated-v1:keysound-c4.ogg"},
    });
    REQUIRE(registered);

    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_audition_output_t output{session};
    std::string error;
    REQUIRE(output.open(&error));

    const audition_clip_t clip{registered->source, 0,
                               registered->output_frame_count, 1.0F};
    REQUIRE(output.start(std::span{&clip, 1}));

    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() == 0 && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        if (service) {
            REQUIRE_FALSE(service->status.has_failure());
        }
        SDL_Delay(2);
    }
    CHECK(session.timeline_frame() > 0);

    const auto stopped = output.stop();
    CHECK(stopped.scheduled_voices <= 1);
    CHECK_FALSE(session.is_active());
    output.close();
}

namespace {
auto audition_lock_order_stress() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_source({
        .asset = wormedit::asset_id_t{1}, .path = fixture_path("keysound-c4.ogg"),
        .source = {"fixture:keysound-c4.ogg", "lock-order-stress"}});
    if (!registered) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_audition_output_t output{session};
    if (!output.open()) return false;
    const audition_clip_t clip{registered->source, 0, registered->output_frame_count, 1.0F};
    if (!output.start(std::span{&clip, 1})) return false;
    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() == 0 && SDL_GetTicks() < deadline) SDL_Delay(2);
    if (session.timeline_frame() == 0) return false;
    for (unsigned iteration = 0; iteration < 2'000; ++iteration) {
        if (iteration % 2 == 0) {
            if (!output.start(std::span{&clip, 1})) return false;
            if (iteration % 16 == 0) SDL_Delay(2);
        } else static_cast<void>(output.stop());
    }
    std::array<float, 64> samples;
    samples.fill(1.0F);
    static_cast<void>(session.render_frames(samples));
    return !session.is_active() && std::ranges::all_of(samples, [](float value) { return value == 0.0F; });
}
} // namespace

TEST_CASE("SDL audition completes 2000 concurrent start and hard Stop transitions",
          "[integration][audio][sdl][audition][lock-order]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const audio_subsystem_t audio_subsystem;
    CHECK(audition_lock_order_stress());
}

namespace {
auto rejected_audition_continues_previous_session() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_source({
        .asset = wormedit::asset_id_t{1}, .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord.ogg", "rejected-audition"}});
    if (!registered) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_audition_output_t output{session};
    if (!output.open()) return false;
    const audition_clip_t clip{registered->source, 0, registered->output_frame_count, 1.0F};
    if (!output.start(std::span{&clip, 1})) return false;
    const auto token = bridge.session_token();
    const auto before = session.timeline_frame();
    const auto rejected = output.start({});
    if (!rejected.has_failure(wormedit::failure_code_t::session_failed) ||
        !session.is_active() || bridge.session_token() != token) return false;
    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() <= before && session.is_active() && SDL_GetTicks() < deadline)
        SDL_Delay(2);
    return session.timeline_frame() > before;
}
} // namespace

TEST_CASE("SDL audition rejection preserves and resumes the previous active session",
          "[integration][audio][sdl][audition][failure]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const audio_subsystem_t audio_subsystem;
    CHECK(rejected_audition_continues_previous_session());
}

namespace {
void audition_expect(bool value) { REQUIRE(value); }
auto streamed_audition_without_service() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = sources.create_source({.asset = wormedit::asset_id_t{1},
        .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord.ogg", "worker-only"}});
    if (!source) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_audition_output_t output{session};
    if (!output.open()) return false;
    const audition_clip_t clip{source->source, 0, source->output_frame_count, 1};
    if (!output.start(std::span{&clip, 1})) return false;
    const auto deadline = SDL_GetTicks() + 2000;
    while (session.timeline_frame() == 0 && SDL_GetTicks() < deadline) SDL_Delay(2);
    const bool progressed = session.timeline_frame() > 0;
    static_cast<void>(output.stop());
    return progressed && !output.service() && !session.is_active();
}
}
TEST_CASE("SDL streamed audition decodes and advances without caller service",
          "[integration][audio][sdl][audition][worker]") {
    audition_expect(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    audition_expect(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const audio_subsystem_t lifetime;
    audition_expect(streamed_audition_without_service());
}

TEST_CASE("SDL audition shutdown excludes the refill predicate-to-wait gap",
          "[integration][audio][sdl][audition][worker]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const audio_subsystem_t lifetime;
    using namespace wormedit::audio;
    using access = wormedit::sys::sdl_audition_output_test_access_t;
    decoded_source_registry_t sources;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_audition_output_t output{session};
    REQUIRE(output.open());
    access::park_worker(output);
    bool wait_gap_is_open = true;
    // This callback observes the exact stop-predicate publication, without
    // relying on scheduling luck to lose a notification and hang the test.
    const std::stop_callback observe_stop{access::stop_token(output), [&] {
        wait_gap_is_open = access::can_enter_wait(output);
    }};
    output.close();
    CHECK_FALSE(wait_gap_is_open);
    CHECK_FALSE(output.available());
    // A new worker must still open and shut down after the synchronized stop.
    REQUIRE(output.open());
    output.close();
}
