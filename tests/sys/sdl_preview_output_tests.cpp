#include "sys/sdl_preview_output.h"
#include "sys/chart_preview_state.h"

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

auto wait_for_silent_preview_frame(wormedit::audio::preview_session_t& session,
                                   wormedit::sys::sdl_preview_output_t& output) -> bool {
    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() == 0 && session.active() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        if (service && !*service)
            return false;
        SDL_Delay(2);
    }
    return session.active() && session.timeline_frame() > 0;
}

auto silent_preview_advances() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    if (!output.open())
        return false;

    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {
        {.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const auto started = output.start(schedule, {}, {}, 0, output_sample_rate * 60U);
    if (!started || !started.session || !started.session->materialize)
        return false;
    const bool advanced = started.session->materialize->missing_sources == 1 &&
                          wait_for_silent_preview_frame(session, output);
    static_cast<void>(output.stop());
    output.close();
    return advanced;
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

TEST_CASE("SDL dummy output advances chart preview when its audio source is unavailable",
          "[integration][audio][sdl][preview][missing-source]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(silent_preview_advances());
}

auto chart_music_preview_ignores_waveform_selection() -> bool {
    using namespace wormedit;
    song_document_t document{
        .id = song_id_t{8},
        .revision = 4,
        .audio_assets = {{.id = asset_id_t{5}, .source_path = "keysound-c4.ogg"},
                         {.id = asset_id_t{6}, .source_path = "keysound-g4.ogg"}},
        .resources = {{.id = resource_id_t{7}, .name = "song music", .asset = asset_id_t{5},
                       .roles = {resource_role_t::music}}},
        .charts = {{.id = chart_id_t{9},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{10},
                                  .definition = {{1, 1}, meter_t{4, 4}}}},
                    .bpms = {{.id = object_id_t{11},
                              .position = {measure_id_t{10}, 0},
                              .beats_per_minute = {120, 1}}}}}};
    audio::decoded_source_registry_t sources;
    const auto registered_music = sources.register_source(
        {.asset = asset_id_t{5},
         .path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio/keysound-c4.ogg",
         .source = {"fixture:document-music", "music-v1"}});
    const auto registered_inspection = sources.register_source(
        {.asset = asset_id_t{6},
         .path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio/keysound-g4.ogg",
         .source = {"fixture:waveform-inspection", "waveform-v1"}});
    sys::runtime::chart_preview_t preview;
    const auto error = preview.compile_if_needed(document.charts.front(), document, sources, {});
    workspace_controller_t workspace;
    workspace.selected_chart = chart_id_t{9};
    workspace.transport.audio_asset = asset_id_t{6};
    return registered_music && registered_inspection && !error && preview.schedule &&
           preview.schedule->music && preview.schedule->music->resource == resource_id_t{7} &&
           preview.music_asset == asset_id_t{5} && preview.matches_selection(document, workspace);
}

TEST_CASE("chart music preview follows the document music resource, not waveform selection",
          "[integration][sys][chart-preview][music]") {
    CHECK(chart_music_preview_ignores_waveform_selection());
}
