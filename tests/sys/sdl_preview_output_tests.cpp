#include "sys/sdl_preview_output.h"
#include "sys/chart_preview_state.h"

#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <atomic>
#include <cmath>
#include <vector>
#include <source_location>
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

namespace {
void expect_pitch(bool value, std::source_location where = std::source_location::current()) {
    INFO("pitch check caller line " << where.line());
    REQUIRE(value);
}

struct device_capture_t {
    SDL_AudioDeviceID device;
    std::vector<float> samples = std::vector<float>(44100 * 5);
    std::atomic<std::size_t> count{};
    std::atomic<int> rate{};
    explicit device_capture_t(SDL_AudioDeviceID id) : device(id) {
        expect_pitch(SDL_SetAudioPostmixCallback(device, callback, this));
    }
    ~device_capture_t() { static_cast<void>(SDL_SetAudioPostmixCallback(device, nullptr, nullptr)); }
    static void SDLCALL callback(void* userdata, const SDL_AudioSpec* spec, float* data, int bytes) {
        auto& capture = *static_cast<device_capture_t*>(userdata);
        const auto offset = capture.count.load(std::memory_order_relaxed);
        const auto frames = std::min(static_cast<std::size_t>(bytes) / sizeof(float) /
                                     static_cast<std::size_t>(spec->channels), capture.samples.size() - offset);
        for (std::size_t frame = 0; frame < frames; ++frame)
            capture.samples[offset + frame] = data[frame * static_cast<std::size_t>(spec->channels)];
        capture.rate.store(spec->freq, std::memory_order_relaxed);
        capture.count.store(offset + frames, std::memory_order_release);
    }
    void detach() { expect_pitch(SDL_SetAudioPostmixCallback(device, nullptr, nullptr)); }
};

auto generated_sine(std::size_t frames) -> std::vector<float> {
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(std::sin(frame * 2.0 * 3.141592653589793 * 440.0 / 44100.0));
        samples[frame * 2] = samples[frame * 2 + 1] = value;
    }
    return samples;
}

auto captured_frequency(std::span<const float> samples, int rate) -> double {
    std::vector<double> periods;
    std::optional<double> previous;
    for (std::size_t frame = 1; frame < samples.size(); ++frame) {
        if (samples[frame - 1] <= 0.0F && samples[frame] > 0.0F) {
            const auto at = static_cast<double>(frame - 1) - samples[frame - 1] /
                static_cast<double>(samples[frame] - samples[frame - 1]);
            if (previous) periods.push_back(at - *previous);
            previous = at;
        }
    }
    if (periods.empty()) return 0.0;
    std::sort(periods.begin(), periods.end());
    return rate / periods[periods.size() / 2];
}

void exercise_failed_drain_seek(wormedit::sys::sdl_preview_output_t& output,
                               wormedit::audio::preview_object_source_binding_t& binding) {
    const auto previous = output.timeline_frame();
    const auto duration = binding.voices.front().frame_count;
    binding.voices.front().frame_count = 0;
    expect_pitch(!output.seek(0));
    expect_pitch(output.playing());
    expect_pitch(output.timeline_frame() >= previous);
    binding.voices.front().frame_count = duration;
}

void exercise_pitch_output(wormedit::rational_t rate) {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.register_generated_source({
        .source = {"generated:440Hz", "pitch-test-v1"}, .interleaved_stereo = generated_sine(11025)});
    expect_pitch(static_cast<bool>(registered));
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    expect_pitch(output.open());
    device_capture_t capture{output.audio_device_id()};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    preview_object_source_binding_t binding{.object = object_id_t{1},
        .voices = {{.source = registered.binding->source, .frame_count = 11025}}};
    expect_pitch(output.set_playback_rate(rate));
    expect_pitch(output.set_keep_pitch(true));
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    const auto deadline = SDL_GetTicks() + 6000;
    bool saw_drain{}, restarted{};
    while (output.playing() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        expect_pitch(!service || static_cast<bool>(*service));
        if (!session.active() && output.playing()) {
            saw_drain = true;
            if (!restarted) {
                exercise_failed_drain_seek(output, binding);
                expect_pitch(static_cast<bool>(output.seek(0)));
                restarted = true;
            }
        }
        SDL_Delay(2);
    }
    capture.detach();
    expect_pitch(saw_drain && restarted);
    expect_pitch(!output.playing());
    expect_pitch(output.keep_pitch());
    expect_pitch(output.playback_rate() == rate);
    expect_pitch(std::abs(captured_frequency(std::span(capture.samples).first(capture.count.load()), capture.rate.load()) - 440.0) <= 2.0);
    static_cast<void>(output.stop());
    expect_pitch(output.source_latency_frames() == 0);
}
}

TEST_CASE("SDL keep-pitch output preserves sine and supports seeks during EOF drain",
          "[integration][audio][sdl][preview][keep-pitch]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    exercise_pitch_output({1, 2});
    exercise_pitch_output({1, 10});
}
namespace {
auto maximum_step(std::span<const float> samples) -> double {
    double maximum{};
    for (std::size_t frame = 1; frame < samples.size(); ++frame)
        maximum = std::max(maximum, std::abs(static_cast<double>(samples[frame] - samples[frame - 1])));
    return maximum;
}

void wait_capture_frames(device_capture_t& capture, wormedit::sys::sdl_preview_output_t& output,
                         std::size_t frames) {
    const auto deadline = SDL_GetTicks() + 2000;
    while (capture.count.load() < frames && output.playing() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        expect_pitch(!service || static_cast<bool>(*service));
        SDL_Delay(2);
    }
    expect_pitch(capture.count.load() >= frames);
}

void wait_pitch_mode(wormedit::sys::sdl_preview_output_t& output, bool preserve,
                     wormedit::rational_t rate) {
    const auto deadline = SDL_GetTicks() + 2000;
    while ((output.keep_pitch() != preserve || output.playback_rate() != rate) &&
           output.playing() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        expect_pitch(!service || static_cast<bool>(*service));
        SDL_Delay(2);
    }
    expect_pitch(output.keep_pitch() == preserve && output.playback_rate() == rate);
}

void exercise_mode_transition() {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.register_generated_source({
        .source = {"generated:transition440Hz", "pitch-test-v1"}, .interleaved_stereo = generated_sine(22050)});
    expect_pitch(static_cast<bool>(registered));
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    expect_pitch(output.open());
    device_capture_t capture{output.audio_device_id()};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{.object = object_id_t{1},
        .voices = {{.source = registered.binding->source, .frame_count = 22050}}};
    expect_pitch(output.set_playback_rate({1, 10}));
    expect_pitch(output.set_keep_pitch(true));
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    wait_capture_frames(capture, output, 4410);
    expect_pitch(output.set_keep_pitch(false));
    wait_pitch_mode(output, false, {1, 10});
    wait_capture_frames(capture, output, capture.count.load() + 4410);
    expect_pitch(!output.keep_pitch());
    expect_pitch(output.set_playback_rate({1, 2}));
    expect_pitch(output.set_keep_pitch(true));
    wait_pitch_mode(output, true, {1, 2});
    wait_capture_frames(capture, output, capture.count.load() + 4410);
    expect_pitch(output.keep_pitch());
    expect_pitch(output.playback_rate() == rational_t{1, 2});
    capture.detach();
    const auto step = maximum_step(std::span(capture.samples).first(capture.count.load()));
    INFO("maximum mode-transition sample step " << step);
    expect_pitch(step <= 0.1);
    expect_pitch(output.playing());
    static_cast<void>(output.stop());
    expect_pitch(!output.playing());
    expect_pitch(output.timeline_frame() == 0);
    expect_pitch(output.source_latency_frames() == 0);
}
}

TEST_CASE("SDL pitch mode changes fade and mid-play Stop clears buffered clocks",
          "[integration][audio][sdl][preview][keep-pitch][transition]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    exercise_mode_transition();
}
