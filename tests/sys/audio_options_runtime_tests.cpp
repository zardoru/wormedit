#include "sys/audio/audio_options_runtime.h"
#include "sys/preferences/options_runtime.h"
#include "sys/audio/chart_preview_state.h"
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <source_location>

namespace {
void check_audio_option(bool value, const std::source_location caller = std::source_location::current()) {
    INFO("audio options caller line " << caller.line());
    CHECK(value);
}
void require_audio_option(bool value, const std::source_location caller = std::source_location::current()) {
    INFO("audio options caller line " << caller.line());
    REQUIRE(value);
}
struct audio_subsystem_t {
    audio_subsystem_t() {
        require_audio_option(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
        require_audio_option(SDL_InitSubSystem(SDL_INIT_AUDIO));
    }
    ~audio_subsystem_t() { SDL_QuitSubSystem(SDL_INIT_AUDIO); }
};
struct audio_options_fixture_t {
    audio_subsystem_t subsystem;
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::audio::decoded_block_cache_t audition_cache, preview_cache;
    wormedit::audio::transport_t audition_transport{audition_cache}, preview_transport{preview_cache};
    wormedit::audio::audition_transport_bridge_t audition_bridge{audition_transport};
    wormedit::audio::preview_transport_bridge_t preview_bridge{preview_transport};
    wormedit::audio::audition_session_t audition_session{sources, audition_transport, audition_bridge};
    wormedit::audio::preview_session_t preview_session{sources, preview_transport, preview_bridge};
    wormedit::sys::sdl_audition_output_t audition{audition_session};
    wormedit::sys::sdl_preview_output_t preview{preview_session};
    wormedit::sys::runtime::chart_preview_t chart;
    wormedit::sys::metronome_t metronome;
    wormedit::sys::options_runtime_t options;
    wormedit::sys::audio_options_runtime_t runtime;
    wormedit::audio_preferences_t preferences;
    auto service(bool audition_active = false) -> std::string {
        return runtime.service(preferences, options, audition, preview, metronome, chart, audition_active);
    }
};
}

TEST_CASE("Audio Options switch both outputs only after paused preview and audition Stop", "[audio][sdl][audio-options]") {
    audio_options_fixture_t f;
    f.runtime.refresh_devices(f.options);
    require_audio_option(!f.options.audio_devices.empty());
    require_audio_option(f.service().empty());
    const auto original = f.preview.audio_device_id();
    f.chart.paused = true;
    f.preferences.output_device = f.options.audio_devices.front();
    f.preferences.buffer_frames = 256;
    f.preferences.audition_volume = 35;
    f.preferences.metronome_volume = 70;
    f.preferences.accent_volume = 40;
    f.preferences.count_in_measures = 2;
    check_audio_option(f.service().empty() && f.options.audio_output_pending);
    check_audio_option(f.preview.audio_device_id() == original && f.audition.volume() == 35);
    f.chart.paused = false;
    check_audio_option(f.service(true).empty() && f.preview.audio_device_id() == original);
    require_audio_option(f.service().empty());
    check_audio_option(!f.options.audio_output_pending && f.preview.audio_device_id() != original);
    check_audio_option(f.preview.available() && f.audition.available());
    check_audio_option(f.metronome.settings.volume == 70 && f.metronome.settings.accent_volume == 40 &&
        f.metronome.settings.count_in == 2);
}

TEST_CASE("Audio Options missing devices fall back once and preserve the requested preference", "[audio][sdl][audio-options]") {
    audio_options_fixture_t f;
    f.preferences.output_device = "Unavailable test device";
    check_audio_option(!f.service().empty());
    check_audio_option(f.preview.available() && f.audition.available());
    check_audio_option(f.preferences.output_device == "Unavailable test device");
    check_audio_option(f.service().empty());
    f.runtime.refresh_devices(f.options);
    check_audio_option(!f.service().empty());
}

TEST_CASE("Audio output opening falls back and restores the sample-frame hint", "[audio][sdl][audio-options]") {
    audio_options_fixture_t f;
    require_audio_option(SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512"));
    std::string warning;
    require_audio_option(f.preview.open(&warning, 0x12345678, 256));
    check_audio_option(!warning.empty());
    check_audio_option(std::string_view{SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES)} == "512");
    SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
}

TEST_CASE("Preview latency shifts only the displayed clock and clamps at zero", "[audio][sdl][audio-options]") {
    audio_options_fixture_t f;
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    require_audio_option(static_cast<bool>(f.preview_session.start_session(schedule, {}, {}, 44100, {}, 441000)));
    f.chart.display_latency_ms = 100;
    check_audio_option(f.chart.display_frame(f.preview_session) == 48510);
    check_audio_option(f.chart.current_frame(f.preview_session) == 44100);
    f.chart.display_latency_ms = -100;
    check_audio_option(f.chart.display_frame(f.preview_session) == 39690);
    require_audio_option(static_cast<bool>(f.preview_session.seek_session(100)));
    check_audio_option(f.chart.display_frame(f.preview_session) == 0);
    check_audio_option(f.chart.current_frame(f.preview_session) == 100);
}
