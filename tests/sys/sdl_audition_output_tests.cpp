#include "sys/sdl_audition_output.h"

#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

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
    const auto registered = sources.register_source({
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

    const audition_clip_t clip{registered.binding->source, 0,
                               registered.binding->output_frame_count, 1.0F};
    REQUIRE(output.start(std::span{&clip, 1}));

    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() == 0 && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        if (service) {
            REQUIRE(*service);
        }
        SDL_Delay(2);
    }
    CHECK(session.timeline_frame() > 0);

    const auto stopped = output.stop();
    CHECK(stopped.scheduled_voices <= 1);
    CHECK_FALSE(session.active());
    output.close();
}
