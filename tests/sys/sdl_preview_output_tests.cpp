#include "sys/sdl_preview_output.h"

#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {

struct preview_audio_subsystem_t {
    ~preview_audio_subsystem_t() {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

} // namespace

TEST_CASE("SDL dummy output drives seeks and hard-stops a chart preview session",
          "[integration][audio][sdl][preview]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;

    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.register_source({
        .asset = asset_id_t{1},
        .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord.ogg", "chart-output-v1"},
    });
    REQUIRE(registered);

    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    std::string error;
    REQUIRE(output.open(&error));

    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {
        {.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1},
        .voices = {{.source = registered.binding->source,
                    .frame_count = registered.binding->output_frame_count}},
    };
    const auto started = output.start(schedule, std::span{&binding, 1});
    REQUIRE(started);
    REQUIRE(started.session.has_value());
    const auto first_token = *started.session->token;

    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() == 0 && session.active() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        if (service)
            REQUIRE(*service);
        SDL_Delay(2);
    }
    CHECK(session.timeline_frame() > 0);

    const auto sought = output.seek(0);
    REQUIRE(sought);
    REQUIRE(sought.session.has_value());
    CHECK(sought.session->token != first_token);
    CHECK_FALSE(transport.is_current(first_token));

    const auto stopped = output.stop();
    CHECK(stopped.scheduled_voices <= 1);
    CHECK_FALSE(session.active());
    output.close();
}
