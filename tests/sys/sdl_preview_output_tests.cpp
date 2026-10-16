#include "project_audio_sources_fixture.h"
#include "sys/sdl_preview_output.h"
#include "sys/chart_preview_state.h"

#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <algorithm>
#include <array>
#include <ranges>
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
    while (session.timeline_frame() == 0 && session.is_active() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        if (service && !*service)
            return false;
        SDL_Delay(2);
    }
    return session.is_active() && session.timeline_frame() > 0;
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
    if (!started || !started->session || !started->session->value().detail.materialize)
        return false;
    const bool advanced = started->session->value().detail.materialize->missing_sources == 1 &&
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
    const auto registered = sources.create_source({
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
        .voices = {{.source = registered->source,
                    .frame_count = registered->output_frame_count}},
    };
    const auto started = output.start(schedule, std::span{&binding, 1});
    REQUIRE(started);
    REQUIRE(started->session.has_value());
    const auto first_token = started->session->value().token;

    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() == 0 && session.is_active() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        if (service)
            REQUIRE(*service);
        SDL_Delay(2);
    }
    CHECK(session.timeline_frame() > 0);

    const auto sought = output.seek(0);
    REQUIRE(sought);
    REQUIRE(sought->session.has_value());
    CHECK(sought->session->value().token != first_token);
    CHECK_FALSE(transport.is_current(first_token));

    const auto stopped = output.stop();
    CHECK(stopped.scheduled_voices <= 1);
    CHECK_FALSE(session.is_active());
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
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    const auto registered_music = sources.create_source(
        {.asset = asset_id_t{5},
         .path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio/keysound-c4.ogg",
         .source = {"fixture:document-music", "music-v1"}});
    const auto registered_inspection = sources.create_source(
        {.asset = asset_id_t{6},
         .path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio/keysound-g4.ogg",
         .source = {"fixture:waveform-inspection", "waveform-v1"}});
    sys::runtime::chart_preview_t preview;
    const auto error = preview.compile_if_needed(document.charts.front(), document, retained_sources_for(document, project_sources));
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

TEST_CASE("count-in setting recompiles preview pulses without resetting paused music position",
          "[integration][sys][chart-preview][timing-settings]") {
    using namespace wormedit;
    song_document_t document{.id = {1}, .revision = 2,
        .charts = {{.id = {1}, .beat_zero_audio_offset_seconds = {4, 1},
            .measures = {{.id = {1}}},
            .bpms = {{.id = {2}, .position = {{1}, 0}, .beats_per_minute = {120, 1}}}}}};
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    sys::runtime::chart_preview_t preview;
    REQUIRE_FALSE(preview.compile_if_needed(document.charts.front(), document, retained_sources_for(document, project_sources)));
    CHECK(preview.reusable({1}, 2, std::nullopt));
    preview.pause_at(3000);
    sys::metronome_t metronome;
    project_sources.metronome = &metronome;
    preview.metronome = &metronome;
    metronome.settings.count_in = 2;
    CHECK_FALSE(preview.reusable({1}, 2, std::nullopt));
    REQUIRE_FALSE(preview.compile_if_needed(document.charts.front(), document, retained_sources_for(document, project_sources)));
    CHECK(preview.reusable({1}, 2, std::nullopt));
    CHECK(preview.resume_frame == 3000);
    CHECK(preview.paused);
    REQUIRE(preview.schedule);
    CHECK(std::ranges::count_if(preview.schedule->metronome,
        [](const auto& click) { return click.pulse < 0; }) == 8);
    CHECK(document.revision == 2);
}

TEST_CASE("bookmark insertion can advance only the matching compiled preview revision",
          "[integration][sys][chart-preview][bookmarks]") {
    wormedit::sys::runtime::chart_preview_t preview;
    preview.schedule.emplace();
    preview.timing.emplace();
    preview.chart = wormedit::chart_id_t{9};
    preview.revision = 12;

    CHECK(preview.preserve_bookmark_revision(wormedit::chart_id_t{9}, 12, 13));
    CHECK(preview.revision == 13);
    CHECK_FALSE(preview.preserve_bookmark_revision(wormedit::chart_id_t{9}, 12, 13));
    CHECK_FALSE(preview.preserve_bookmark_revision(wormedit::chart_id_t{8}, 13, 14));
    CHECK_FALSE(preview.preserve_bookmark_revision(wormedit::chart_id_t{9}, 13, 15));
    CHECK(preview.revision == 13);
}

namespace {
void expect_pitch(bool value, std::source_location where = std::source_location::current()) {
    INFO("pitch check caller line " << where.line());
    REQUIRE(value);
}

auto fractional_preview_start(double subrow, std::optional<std::uint64_t> exact_start = std::nullopt)
    -> std::uint64_t {
    using namespace wormedit;
    song_document_t document{.id = song_id_t{1}, .revision = 1,
        .charts = {{.id = chart_id_t{1}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}}},
            .bpms = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}}}}}};
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    audio::decoded_block_cache_t cache;
    audio::transport_t transport{cache};
    audio::preview_transport_bridge_t preview_bridge{transport};
    audio::audition_transport_bridge_t audition_bridge{transport};
    audio::preview_session_t session{sources, transport, preview_bridge};
    audio::audition_session_t audition{sources, transport, audition_bridge};
    sys::sdl_preview_output_t output{session};
    sys::sdl_audition_output_t audition_output{audition};
    expect_pitch(bool(output.open()));
    sys::runtime::chart_preview_t preview;
    (void)preview.play_pause(&document.charts.front(), document, retained_sources_for(document, project_sources), audition_output,
        session, output, local_position_t{measure_id_t{1}, 480}, false, exact_start, subrow);
    expect_pitch(output.playing());
    expect_pitch(preview.feedback.accepted &&
                 preview.feedback.severity == notification_severity_t::success);
    const auto start = preview.resume_frame;
    preview.stop(audition_output, output);
    output.close();
    return start;
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

void wait_capture_frames(device_capture_t& capture, wormedit::sys::sdl_preview_output_t& output,
                         std::size_t frames);

auto sdl_bmson_seek_renders_pcm() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy") || !SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    const preview_audio_subsystem_t audio_subsystem;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source(
        {.source = {"generated:bmson-resume", "sdl-bmson-resume-v1"},
         .interleaved_stereo = generated_sine(output_sample_rate)});
    if (!registered)
        return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    if (!output.open())
        return false;
    device_capture_t capture{output.audio_device_id()};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 0, .object = object_id_t{91}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{91},
        .voices = {{.source = registered->source, .frame_count = output_sample_rate / 2}}};
    constexpr std::uint64_t seek_frame = output_sample_rate / 20;
    if (!output.start(schedule, std::span{&binding, 1}, {}, seek_frame))
        return false;
    wait_capture_frames(capture, output, output_sample_rate / 100);
    const auto captured = std::span(capture.samples).first(output_sample_rate / 100);
    const bool audible = std::ranges::any_of(captured, [](float value) { return std::abs(value) > 0.001F; });
    capture.detach();
    static_cast<void>(output.stop());
    output.close();
    return audible;
}

TEST_CASE("SDL dummy audio renders a resumed BMSON slice within ten milliseconds",
          "[integration][audio][sdl][preview][seek][bmson]") {
    CHECK(sdl_bmson_seek_renders_pcm());
}

auto exercise_sdl_loop_restart(std::size_t rate_index = wormedit::audio::playback_rates.size() - 1,
                               bool keep_pitch = false) -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    constexpr std::uint64_t loop_begin = 1'024;
    constexpr std::uint64_t loop_end = 5'120;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:sdl-loop", "sdl-loop-v1"},
        .interleaved_stereo = generated_sine(loop_end),
    });
    if (!registered) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    if (!output.open()) return false;
    if (!output.set_playback_rate(playback_rates[rate_index]) || !output.set_keep_pitch(keep_pitch)) return false;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{1}, .frame_count = loop_end};
    const preview_object_source_binding_t binding{
        .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
        .voices = {{.source = registered->source, .frame_count = loop_end}},
    };
    const preview_loop_region_t loop{loop_begin, loop_end};
    if (!output.set_loop_region(loop) || !output.start(schedule, std::span{&binding, 1})) {
        output.close();
        return false;
    }
    const bool start_clock_aligned = output.timeline_frame() == loop_begin;
    const auto initial_token = bridge.session_token();
    const auto generation = session.playhead().generation;
    const auto deadline = SDL_GetTicks() + 2'000;
    auto previous = output.timeline_frame();
    bool wrapped{}, bounded = true;
    while (!wrapped && SDL_GetTicks() < deadline) {
        const auto frame = output.timeline_frame();
        bounded = bounded && frame >= loop_begin && frame < loop_end;
        wrapped = frame < previous;
        previous = frame;
        SDL_Delay(1); // No control service is needed to prepare a wrap.
    }
    const auto token = bridge.session_token();
    const auto serviced = output.service();
    const bool repeated = start_clock_aligned && bounded && wrapped && token && initial_token &&
        token->generation > initial_token->generation && session.playhead().generation == generation &&
        (!serviced || static_cast<bool>(*serviced)) && output.playing() &&
        session.loop_region() == std::optional<preview_loop_region_t>{loop};
    static_cast<void>(output.stop());
    std::array<float, 32> stopped{};
    stopped.fill(1.0F);
    const auto silence = session.render_frames(stopped);
    const bool hard_stop = silence.status.is_in_phase(session_render_phase_t::stopped) &&
        std::ranges::all_of(stopped, [](float sample) { return sample == 0.0F; });
    output.close();
    return repeated && hard_stop;
}

auto chart_preview_end_includes_music_tail() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    song_document_t document{
        .id = song_id_t{1},
        .revision = 1,
        .audio_assets = {{.id = asset_id_t{2}, .source_path = "generated:music-tail"}},
        .resources = {{.id = resource_id_t{3}, .name = "music", .asset = asset_id_t{2},
                       .roles = {resource_role_t::music}}},
        .charts = {{.id = chart_id_t{4},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{5},
                                  .definition = {{1, 1}, meter_t{4, 4}}}},
                    .bpms = {{.id = object_id_t{6},
                              .position = {measure_id_t{5}, 0},
                              .beats_per_minute = {120, 1}}}}}};
    sys::project_audio_sources_t project_sources;
    auto& sources = project_sources.registry();
    const auto registered = sources.create_source({
        .asset = asset_id_t{2},
        .path = fixture_path("sliced-seek-source.ogg"),
        .source = {"fixture:music-tail", "chart-preview-tail-v1"},
    });
    if (!registered) return false;
    const auto music_frames = registered->output_frame_count;
    if (music_frames <= output_sample_rate * 2U) return false;

    sys::runtime::chart_preview_t preview;
    if (preview.compile_if_needed(document.charts.front(), document, retained_sources_for(document, project_sources)) ||
        !preview.schedule || !preview.schedule->music)
        return false;
    return preview.schedule->music->frame_count == music_frames && preview.end_frame == music_frames;
}

TEST_CASE("chart preview end includes decoded music after the visual chart endpoint",
          "[integration][sys][chart-preview][tail]") {
    CHECK(chart_preview_end_includes_music_tail());
}

auto wait_for_session_frame(wormedit::audio::preview_session_t& session,
                            std::uint64_t frame, std::uint64_t deadline) -> bool {
    while (session.timeline_frame() < frame && SDL_GetTicks() < deadline) SDL_Delay(1);
    return session.timeline_frame() == frame;
}

auto service_until_after_frame(wormedit::sys::sdl_preview_output_t& output,
                               wormedit::audio::preview_session_t& session,
                               std::uint64_t frame, std::uint64_t deadline) -> bool {
    while (SDL_GetTicks() < deadline) {
        const auto serviced = output.service();
        if (serviced && !*serviced) return false;
        if (session.timeline_frame() > frame && output.playing()) return true;
        SDL_Delay(1);
    }
    return false;
}

auto exercise_sdl_silent_tail_loop() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    constexpr std::uint64_t loop_begin = 1'024;
    constexpr std::uint64_t source_end = 2'048;
    constexpr std::uint64_t loop_end = 5'120;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:sdl-loop-silent-tail", "sdl-loop-silent-tail-v1"},
        .interleaved_stereo = generated_sine(source_end),
    });
    if (!registered) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    if (!output.open()) return false;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{1}, .frame_count = source_end};
    const preview_object_source_binding_t binding{
        .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
        .voices = {{.source = registered->source, .frame_count = source_end}},
    };
    const preview_loop_region_t loop{loop_begin, loop_end};
    if (!output.set_loop_region(loop) || !output.start(schedule, std::span{&binding, 1})) {
        output.close();
        return false;
    }
    const auto initial = bridge.session_token();
    const auto deadline = SDL_GetTicks() + 2'000;
    auto previous = output.timeline_frame();
    bool wrapped{};
    while (!wrapped && SDL_GetTicks() < deadline) {
        const auto frame = output.timeline_frame();
        wrapped = frame < previous;
        previous = frame;
        SDL_Delay(1);
    }
    const auto token = bridge.session_token();
    const bool looped = wrapped && session.is_active() && output.playing() && token && initial &&
        token->generation > initial->generation;
    static_cast<void>(output.stop());
    output.close();
    return looped;
}

auto exercise_sdl_loop_disable_at_boundary() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    constexpr std::uint64_t loop_begin = 1'024;
    constexpr std::uint64_t loop_end = 5'120;
    constexpr std::uint64_t source_end = loop_end + output_sample_rate;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:sdl-loop-disable", "sdl-loop-disable-v1"},
        .interleaved_stereo = generated_sine(source_end),
    });
    if (!registered) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    if (!output.open()) return false;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{1}, .frame_count = source_end};
    const preview_object_source_binding_t binding{
        .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
        .voices = {{.source = registered->source, .frame_count = source_end}},
    };
    if (!output.set_loop_region(preview_loop_region_t{loop_begin, loop_end}) ||
        !output.start(schedule, std::span{&binding, 1})) {
        output.close();
        return false;
    }
    const auto deadline = SDL_GetTicks() + 2'000;
    while (output.timeline_frame() < loop_end - 1024 && SDL_GetTicks() < deadline) SDL_Delay(1);
    const auto at_boundary = output.timeline_frame() >= loop_end - 1024;
    const bool disabled = output.set_loop_region(std::nullopt);
    const auto continued = service_until_after_frame(output, session, loop_end, deadline);
    INFO("boundary=" << at_boundary << " disabled=" << disabled << " continued=" << continued
         << " session=" << session.timeline_frame() << " output=" << output.timeline_frame()
         << " playing=" << output.playing());
    CHECK(at_boundary);
    CHECK(disabled);
    CHECK(continued);
    static_cast<void>(output.stop());
    output.close();
    return at_boundary && disabled && continued;
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

auto pitch_restart_budget_ms(wormedit::rational_t rate) -> std::uint64_t {
    constexpr std::uint64_t source_frames = 11025;
    // Each of two runs includes the source, a WSOLA lookahead, and one maximum
    // 4096-source-frame callback block. The latter matches sdl_preview_output's
    // bounded block and conservatively covers queued device work at slow rates.
    const wormedit::audio::time_stretch_t stretcher{wormedit::audio::output_sample_rate};
    const auto output_frames = wormedit::audio::output_frames_for(
        source_frames + stretcher.lookahead_frames() + 4096, rate).value();
    // One second additionally bounds device startup/control scheduling overhead.
    return (2 * output_frames * 1000 + wormedit::audio::output_sample_rate - 1) /
           wormedit::audio::output_sample_rate + 1000;
}

void exercise_pitch_output(wormedit::rational_t rate) {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
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
        .voices = {{.source = registered->source, .frame_count = 11025}}};
    expect_pitch(output.set_playback_rate(rate));
    expect_pitch(output.set_keep_pitch(true));
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    const auto deadline = SDL_GetTicks() + pitch_restart_budget_ms(rate);
    bool saw_drain{}, restarted{};
    while (output.playing() && SDL_GetTicks() < deadline) {
        const auto service = output.service();
        expect_pitch(!service || static_cast<bool>(*service));
        if (!session.is_active() && output.playing()) {
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

TEST_CASE("Chart playback starts from fractional cursor rows and honors exact frame overrides",
          "[integration][audio][sdl][preview][fractional-preview]") {
    expect_pitch(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_pitch(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    expect_pitch(fractional_preview_start(7.25) == 44'766);
    expect_pitch(fractional_preview_start(-7.25) == 43'434);
    expect_pitch(fractional_preview_start(7.25, 12'345) == 12'345);
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
    const auto registered = sources.create_generated_source({
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
        .voices = {{.source = registered->source, .frame_count = 22050}}};
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

namespace {
void exercise_background_refill() {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:worker440Hz", "refill-test-v1"},
        .interleaved_stereo = generated_sine(44100 * 2)});
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
        .voices = {{.source = registered->source, .frame_count = 44100 * 2}}};
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    // No frame-thread service: callback playback must cross several prime windows.
    SDL_Delay(1000);
    capture.detach();
    const auto count = capture.count.load();
    expect_pitch(count >= 30000);
    const auto samples = std::span(capture.samples).first(count).last(4096);
    const auto maximum = *std::ranges::max_element(samples, {}, [](float value) { return std::abs(value); });
    expect_pitch(std::abs(maximum) > 0.9);
    const auto service = output.service();
    expect_pitch(service && static_cast<bool>(*service));
    expect_pitch(service->value().telemetry.silenced_voices == 0);
    static_cast<void>(output.stop());
    std::array<float, 256> silence;
    silence.fill(1.0F);
    const auto rendered = session.render_frames(silence);
    expect_pitch(rendered.status.is_in_phase(session_render_phase_t::stopped));
    expect_pitch(std::ranges::all_of(silence, [](float value) { return value == 0.0F; }));
}
void check_preview_seek_refill(wormedit::sys::sdl_preview_output_t& output, std::uint64_t seek) {
    using namespace wormedit::audio;
    const auto sought = output.seek((seek % 4) * 4096);
    const auto& sought_data = sought ? *sought : sought.error();
    INFO("seek iteration " << seek << " status " << (sought_data.error ? static_cast<int>(sought_data.error->code) : -1)
         << " message " << sought_data.message());
    const auto* detail = sought_data.session
        ? (*sought_data.session ? &sought_data.session->value().detail : &sought_data.session->error().detail)
        : nullptr;
    const auto session_status = sought_data.session
        ? (*sought_data.session ? -1 : static_cast<int>(sought_data.session->error().code))
        : -1;
    const auto bridge_status = detail && detail->bridge
        ? (*detail->bridge ? -1 : static_cast<int>(detail->bridge->error()))
        : -1;
    INFO("session status " << session_status);
    INFO("bridge status " << bridge_status);
    expect_pitch(static_cast<bool>(sought));
    SDL_Delay(2);
    const auto service = output.service();
    expect_pitch(!service || static_cast<bool>(*service));
}

void exercise_refill_generations() {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:seek440Hz", "refill-test-v1"},
        .interleaved_stereo = generated_sine(44100)});
    expect_pitch(static_cast<bool>(registered));
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    expect_pitch(output.open());
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{1}, .frame_count = 44100};
    const preview_object_source_binding_t binding{
        .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
        .voices = {{.source = registered->source, .frame_count = 44100}}};
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    for (std::uint64_t seek = 0; seek < 20; ++seek)
        check_preview_seek_refill(output, seek);
    static_cast<void>(output.stop());
    expect_pitch(!session.is_active() && !output.playing());
    SDL_Delay(20);
    expect_pitch(!output.service());
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    SDL_Delay(20);
    const auto service = output.service();
    expect_pitch(!service || static_cast<bool>(*service));
    static_cast<void>(output.stop());
    output.close();
    expect_pitch(output.open());
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    static_cast<void>(output.stop());
}
void exercise_pending_refill_failure() {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:failure440Hz", "refill-test-v1"},
        .interleaved_stereo = generated_sine(44100 * 2)});
    expect_pitch(static_cast<bool>(registered));
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    expect_pitch(output.open());
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{.object = object_id_t{1},
        .voices = {{.source = registered->source, .frame_count = 44100 * 2}}};
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    expect_pitch(wait_for_silent_preview_frame(session, output));
    expect_pitch(sources.remove(registered->asset));
    // The missing next block drops the voice and completes the session; the
    // unread worker failure must survive that completion until the frame reads it.
    SDL_Delay(800);
    expect_pitch(!session.is_active());
    const auto failed = output.service();
    expect_pitch(failed && !*failed && failed->error().status.has_failure(wormedit::failure_code_t::pump_failed));
    expect_pitch(failed->error().pump && !*failed->error().pump && failed->error().pump->error().status == wormedit::failure_code_t::decode_failed);
    static_cast<void>(output.stop());
}

}
TEST_CASE("SDL refill worker advances through prime windows without frame service and hard Stop stays silent",
          "[integration][audio][sdl][preview][refill]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    exercise_background_refill();
}
TEST_CASE("SDL refill worker discards obsolete seek Stop and reopen generations",
          "[integration][audio][sdl][preview][refill]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    exercise_refill_generations();
}

TEST_CASE("SDL refill failure remains readable after its session has completed",
          "[integration][audio][sdl][preview][refill][failure]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    exercise_pending_refill_failure();
}

namespace {
auto preview_stress_step(wormedit::sys::sdl_preview_output_t& output,
    wormedit::audio::preview_session_t& session, const wormedit::preview_schedule_t& schedule,
    const wormedit::audio::preview_object_source_binding_t& binding, unsigned iteration) -> bool {
    switch (iteration % 4) {
    case 0:
        return static_cast<bool>(output.start(schedule, std::span{&binding, 1}));
    case 1:
        return static_cast<bool>(output.seek((iteration * 127U) % binding.voices.front().frame_count));
    case 2: {
        const auto rates = wormedit::audio::playback_rates;
        const auto changed = output.set_playback_rate(rates[(iteration / 4) % rates.size()]) &&
                             output.set_keep_pitch((iteration / 8) % 2 != 0);
        SDL_Delay(2); // Allow real callbacks to overlap subsequent control operations.
        return changed;
    }
    default: {
        static_cast<void>(output.stop());
        std::array<float, 64> samples;
        samples.fill(1.0F);
        static_cast<void>(session.render_frames(samples));
        return !session.is_active() && std::ranges::all_of(samples, [](float value) { return value == 0.0F; });
    }
    }
}

auto preview_lock_order_stress() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.create_source({
        .asset = asset_id_t{1}, .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord.ogg", "lock-order-stress"}});
    if (!registered || registered->output_frame_count == 0) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    if (!output.open()) return false;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1}, .voices = {{.source = registered->source,
            .frame_count = registered->output_frame_count}}};
    if (!output.start(schedule, std::span{&binding, 1}) ||
        !wait_for_silent_preview_frame(session, output)) return false;
    for (unsigned iteration = 0; iteration < 2'000; ++iteration)
        if (!preview_stress_step(output, session, schedule, binding, iteration)) return false;
    return true;
}
} // namespace

TEST_CASE("SDL preview completes 2000 concurrent seek start Stop and rate transitions",
          "[integration][audio][sdl][preview][lock-order]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(preview_lock_order_stress());
}

namespace {
auto exercise_preview_worker_closes(wormedit::sys::sdl_preview_output_t& output,
                                    wormedit::audio::preview_session_t& session,
                                    const wormedit::preview_schedule_t& schedule,
                                    const wormedit::audio::preview_object_source_binding_t& binding) -> bool {
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        if (!output.open()) return false;
        output.close(); // Worker may be entering its initial idle wait.
        if (!output.open() || !output.start(schedule, std::span{&binding, 1})) return false;
        static_cast<void>(output.stop());
        output.close(); // Worker may be entering its wait after Stop.
        if (output.available() || session.is_active()) return false;
    }
    return true;
}
} // namespace

TEST_CASE("SDL preview repeatedly closes idle and stopped refill workers",
          "[integration][audio][sdl][preview][lock-order]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:shutdown", "shutdown-v1"},
        .interleaved_stereo = generated_sine(11025)});
    REQUIRE(registered);
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1},
        .voices = {{.source = registered->source, .frame_count = 11025}}};
    CHECK(exercise_preview_worker_closes(output, session, schedule, binding));
}

TEST_CASE("SDL loop wraps autonomously at the source tail and Stop clears it",
          "[integration][audio][sdl][preview][loop]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(exercise_sdl_loop_restart());
}

TEST_CASE("SDL selection loops restart at every rate with either pitch mode",
          "[integration][audio][sdl][preview][loop][loop-rates]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    for (bool keep_pitch : {false, true}) {
        for (std::size_t rate = 0; rate < wormedit::audio::playback_rates.size(); ++rate) {
            CAPTURE(keep_pitch, rate);
            CHECK(exercise_sdl_loop_restart(rate, keep_pitch));
        }
    }
}

TEST_CASE("SDL loop can be disabled during a queued wrap and continue the chart",
          "[integration][audio][sdl][preview][loop]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(exercise_sdl_loop_disable_at_boundary());
}

TEST_CASE("SDL loop timeline reaches its end after chart audio has become silent",
          "[integration][audio][sdl][preview][loop]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(exercise_sdl_silent_tail_loop());
}

namespace {
struct preview_feedback_fixture_t {
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    wormedit::audio::decoded_block_cache_t cache;
    wormedit::audio::transport_t transport{cache};
    wormedit::audio::preview_transport_bridge_t preview_bridge{transport};
    wormedit::audio::audition_transport_bridge_t audition_bridge{transport};
    wormedit::audio::preview_session_t session{sources, transport, preview_bridge};
    wormedit::audio::audition_session_t audition{sources, transport, audition_bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    wormedit::sys::sdl_audition_output_t audition_output{audition};
    wormedit::sys::runtime::chart_preview_t preview;
    wormedit::song_document_t document{.id = wormedit::song_id_t{1}, .revision = 1,
        .charts = {{.id = wormedit::chart_id_t{1}, .divisions_per_standard_measure = 960,
            .measures = {{.id = wormedit::measure_id_t{1}}},
            .bpms = {{.id = wormedit::object_id_t{1}, .position = {wormedit::measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}}}}}};
};
void expect_preview_feedback(const preview_feedback_fixture_t& fixture,
                             wormedit::notification_severity_t severity, bool accepted) {
    CHECK(fixture.preview.feedback.severity == severity);
    CHECK(fixture.preview.feedback.accepted == accepted);
}
}

TEST_CASE("chart preview result metadata separates user precondition from output failure",
          "[integration][sys][chart-preview][notifications]") {
    using namespace wormedit;
    preview_feedback_fixture_t fixture;
    fixture.preview.resume_frame = 123;
    CHECK(fixture.preview.play_pause(nullptr, fixture.document, retained_sources_for(fixture.document, fixture.project_sources),
        fixture.audition_output, fixture.session, fixture.output) == "Select a chart before starting preview.");
    expect_preview_feedback(fixture, notification_severity_t::warning, false);
    CHECK(fixture.preview.resume_frame == 123);
    const auto failed = fixture.preview.play_pause(&fixture.document.charts.front(), fixture.document,
        retained_sources_for(fixture.document, fixture.project_sources), fixture.audition_output, fixture.session, fixture.output);
    CHECK_FALSE(failed.empty());
    expect_preview_feedback(fixture, notification_severity_t::error, false);
}

TEST_CASE("chart preview rejected seek retains explicit metadata and current position",
          "[integration][sys][chart-preview][notifications]") {
    using namespace wormedit;
    preview_feedback_fixture_t fixture;
    workspace_controller_t workspace;
    fixture.preview.resume_frame = 123;
    CHECK(fixture.preview.seek_audio_milliseconds(200, fixture.document, workspace,
        fixture.audition_output, fixture.session, fixture.output) == "Chart preview seek is unavailable for the selected chart.");
    expect_preview_feedback(fixture, notification_severity_t::warning, false);
    CHECK(fixture.preview.resume_frame == 123);
}

TEST_CASE("chart preview starts and advances through unmetered partial measures",
          "[integration][sys][chart-preview][partial-measure]") {
    using namespace wormedit;
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    preview_feedback_fixture_t fixture;
    fixture.document.charts.front().measures.front().definition = {{721, 960}, std::nullopt};
    fixture.document.charts.front().beat_zero_audio_offset_seconds = {-1, 4};
    REQUIRE(fixture.output.open());
    const auto started = fixture.preview.play_pause(&fixture.document.charts.front(),
        fixture.document, retained_sources_for(fixture.document, fixture.project_sources), fixture.audition_output, fixture.session,
        fixture.output, local_position_t{{1}, 0});
    expect_preview_feedback(fixture, notification_severity_t::success, true);
    CHECK(started == "Chart preview started at the cursor.");
    CHECK(wait_for_silent_preview_frame(fixture.session, fixture.output));
    fixture.preview.stop(fixture.audition_output, fixture.output);
    fixture.output.close();
}

TEST_CASE("chart preview reports a fatal diagnostic after preceding timing warnings",
          "[integration][sys][chart-preview][partial-measure]") {
    using namespace wormedit;
    preview_feedback_fixture_t fixture;
    auto& chart = fixture.document.charts.front();
    chart.beat_zero_audio_offset_seconds = {-1, 1};
    chart.divisions_per_standard_measure = 3;
    chart.measures.front().definition = {{1, 1}, meter_t{4, 4}};
    const auto failed = fixture.preview.compile_if_needed(chart, fixture.document, retained_sources_for(fixture.document, fixture.project_sources));
    REQUIRE(failed);
    CHECK(*failed == "measure meter cannot produce exact metronome pulse rows");
    expect_preview_feedback(fixture, notification_severity_t::error, false);
    CHECK_FALSE(fixture.preview.schedule);
}

namespace {
struct seek_gap_capture_t {
    std::atomic<std::uint64_t> first_audible{};
    static void SDLCALL callback(void* userdata, const SDL_AudioSpec*, float* data, int bytes) {
        auto& capture = *static_cast<seek_gap_capture_t*>(userdata);
        const auto samples = std::span{data, static_cast<std::size_t>(bytes) / sizeof(float)};
        if (std::ranges::any_of(samples, [](float value) { return std::abs(value) > 0.01F; })) {
            std::uint64_t empty{};
            capture.first_audible.compare_exchange_strong(empty, SDL_GetTicksNS());
        }
    }
};

struct seek_gap_fixture_t {
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    wormedit::audio::decoded_block_cache_t cache;
    wormedit::audio::transport_t transport{cache};
    wormedit::audio::preview_transport_bridge_t bridge{transport};
    wormedit::audio::preview_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    wormedit::audio::preview_object_source_binding_t binding;
    seek_gap_capture_t capture;

    explicit seek_gap_fixture_t(const std::filesystem::path& path = {}) {
        const auto source = path.empty()
            ? sources.create_generated_source({.source = {"seek-gap", "v1"},
                .interleaved_stereo = std::vector<float>(44100 * 4, 0.5F)})
            : sources.create_source({.asset = wormedit::asset_id_t{1}, .path = path,
                .source = {"seek-gap-ogg", "v1"}});
        expect_pitch(static_cast<bool>(source));
        binding = {.object = wormedit::object_id_t{1},
                   .voices = {{.source = source->source,
                               .frame_count = source->output_frame_count}}};
        schedule.bgm = {{.sample_frame = 0, .object = wormedit::object_id_t{1},
                         .resource = wormedit::resource_id_t{1}}};
        expect_pitch(output.open());
        expect_pitch(SDL_SetAudioPostmixCallback(output.audio_device_id(), capture.callback, &capture));
        expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    }
    ~seek_gap_fixture_t() {
        static_cast<void>(SDL_SetAudioPostmixCallback(output.audio_device_id(), nullptr, nullptr));
    }

    auto warm_seek_gap(std::uint64_t frame) -> std::pair<double, double> {
        const auto begin = SDL_GetTicksNS();
        expect_pitch(static_cast<bool>(output.seek(frame)));
        const auto returned = SDL_GetTicksNS();
        capture.first_audible.store(0);
        while (capture.first_audible.load() == 0 && SDL_GetTicksNS() - begin < 100'000'000)
            SDL_Delay(1);
        const auto audible = capture.first_audible.load();
        expect_pitch(audible != 0);
        return {static_cast<double>(returned - begin) / 1e6,
                static_cast<double>(audible - begin) / 1e6};
    }
};
}

namespace {
void verify_warm_seek_measurement(const std::filesystem::path& path = {}) {
    expect_pitch(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_pitch(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    seek_gap_fixture_t fixture{path};
    const auto* requested_frame = std::getenv("WORMEDIT_SEEK_START_FRAME");
    const auto first_frame = requested_frame ? std::stoull(requested_frame) : 100ULL;
    static_cast<void>(fixture.warm_seek_gap(first_frame));
    const wormedit::audio::audio_block_key_t warm_key{fixture.binding.voices.front().source,
                                           first_frame / wormedit::audio::maximum_read_frames};
    const auto deadline = SDL_GetTicks() + 500;
    while (!fixture.cache.has_block(warm_key) && SDL_GetTicks() < deadline)
        SDL_Delay(1);
    expect_pitch(fixture.cache.has_block(warm_key));
    std::vector<double> latency, gaps;
    for (unsigned iteration = 0; iteration < 30; ++iteration) {
        const auto [returned, audible] = fixture.warm_seek_gap(first_frame + iteration * 10);
        latency.push_back(returned);
        gaps.push_back(audible);
    }
    std::ranges::sort(latency);
    std::ranges::sort(gaps);
    WARN("SDL dummy warm source=" << path << " seek latency p50=" << latency[15]
         << "ms p99=" << latency.back() << "ms; audible gap p50=" << gaps[15]
         << "ms maximum=" << gaps.back() << "ms");
    expect_pitch(gaps.back() < 50.0);
}
}

TEST_CASE("SDL warm asynchronous seeks resume audible output within fifty milliseconds",
          "[integration][audio][sdl][preview][async-seek]") {
    verify_warm_seek_measurement();
}

TEST_CASE("SDL warm compressed-source seeks resume audible output within fifty milliseconds",
          "[integration][audio][sdl][preview][async-seek][ogg-seek]") {
    const auto* original = std::getenv("WORMEDIT_SEEK_OGG_PATH");
    verify_warm_seek_measurement(original ? std::filesystem::path{original}
                                        : fixture_path("polyphonic-chord.ogg"));
}

namespace wormedit::sys {
struct sdl_preview_output_test_access_t {
    static auto lock_stream(sdl_preview_output_t& output) -> bool {
        return output.stream_ != nullptr && SDL_LockAudioStream(output.stream_);
    }
    static void unlock_stream(sdl_preview_output_t& output) {
        if (output.stream_ != nullptr) SDL_UnlockAudioStream(output.stream_);
    }
    static auto snapshot(const sdl_preview_output_t& output) -> audio::playhead_state_t {
        return output.read_output_playhead();
    }
    static auto source_finished(const sdl_preview_output_t& output) -> bool {
        return output.source_finished_;
    }
    static auto buffered_source_frames(const sdl_preview_output_t& output) -> std::uint64_t {
        return output.buffered_source_frames_.load(std::memory_order_acquire);
    }
    static auto rendered_source_frame(const sdl_preview_output_t& output) -> std::uint64_t {
        return output.rendered_source_frame_.load(std::memory_order_acquire);
    }
    static auto render_block(sdl_preview_output_t& output, std::size_t frames)
        -> std::vector<float> {
        const auto produced = output.render_block(frames);
        return {output.scratch_.begin(), output.scratch_.begin() +
            static_cast<std::ptrdiff_t>(produced * audio::output_channel_count)};
    }
};
} // namespace wormedit::sys

namespace {
struct t276_output_fixture_t {
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    wormedit::audio::decoded_block_cache_t cache;
    wormedit::audio::transport_t transport{cache};
    wormedit::audio::preview_transport_bridge_t bridge{transport};
    wormedit::audio::preview_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    wormedit::audio::preview_object_source_binding_t binding;
    std::uint64_t source_frames{10U * wormedit::audio::output_sample_rate};
    bool ready{};

    t276_output_fixture_t() {
        const auto source = sources.create_generated_source({
            .source = {"t276-generated", "playhead-generation-v1"},
            .interleaved_stereo = std::vector<float>(source_frames * 2U, 0.25F)});
        if (!source || !output.open()) return;
        binding = {.object = wormedit::object_id_t{1},
                   .voices = {{.source = source->source,
                               .frame_count = source->output_frame_count}}};
        schedule.bgm = {{.sample_frame = 0, .object = wormedit::object_id_t{1},
                         .resource = wormedit::resource_id_t{1}}};
        ready = static_cast<bool>(output.start(schedule, std::span{&binding, 1}, {}, 0,
                                                source_frames));
    }

    ~t276_output_fixture_t() {
        static_cast<void>(output.stop());
        output.close();
    }
};

auto t276_observer_finishes_while_stream_locked(wormedit::sys::sdl_preview_output_t& output)
    -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    if (!access::lock_stream(output)) return false;
    std::atomic<bool> entered{};
    std::atomic<bool> finished{};
    std::atomic<std::uint64_t> frame{};
    std::thread observer([&] {
        entered.store(true, std::memory_order_release);
        frame.store(output.timeline_frame(), std::memory_order_relaxed);
        finished.store(true, std::memory_order_release);
    });
    const auto deadline = SDL_GetTicksNS() + 500'000'000ULL;
    while (!entered.load(std::memory_order_acquire) && SDL_GetTicksNS() < deadline)
        SDL_Delay(1);
    while (entered.load(std::memory_order_acquire) &&
           !finished.load(std::memory_order_acquire) && SDL_GetTicksNS() < deadline)
        SDL_Delay(1);
    const auto completed_while_locked = finished.load(std::memory_order_acquire);
    access::unlock_stream(output);
    observer.join();
    return completed_while_locked && frame.load(std::memory_order_relaxed) <=
        10U * wormedit::audio::output_sample_rate;
}

auto t276_seek_snapshot_matches(wormedit::sys::sdl_preview_output_t& output,
                                std::uint64_t frame) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    const auto sought = output.seek(frame);
    if (!sought || !sought->session || !sought->session->has_value() || !access::lock_stream(output))
        return false;
    const auto state = access::snapshot(output);
    access::unlock_stream(output);
    return state.active && state.start_frame == frame && state.timeline_frame >= frame &&
        state.transport_generation == sought->session->value().token.generation;
}

auto t276_stop_snapshot_is_inactive_zero(wormedit::sys::sdl_preview_output_t& output,
                                         std::uint64_t previous_generation) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    static_cast<void>(output.stop());
    if (!access::lock_stream(output)) return false;
    const auto state = access::snapshot(output);
    access::unlock_stream(output);
    return !state.active && state.timeline_frame == 0 && state.start_frame == 0 &&
        state.transport_generation == 0 && state.generation > previous_generation;
}

auto t276_start_snapshot_is_coherent(t276_output_fixture_t& fixture) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    const auto started = fixture.output.start(fixture.schedule,
        std::span{&fixture.binding, 1}, {}, 0, fixture.source_frames);
    if (!started || !started->session || !started->session->has_value() ||
        !access::lock_stream(fixture.output)) return false;
    const auto state = access::snapshot(fixture.output);
    access::unlock_stream(fixture.output);
    return state.active && state.timeline_frame >= state.start_frame &&
        state.transport_generation == started->session->value().token.generation;
}

auto t276_mutate_seek_stop(t276_output_fixture_t& fixture, std::size_t iterations) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    for (std::size_t index = 0; index < iterations; ++index) {
        const auto frame = static_cast<std::uint64_t>(index + 1U) * 2'048U;
        if (!t276_seek_snapshot_matches(fixture.output, frame)) return false;
        if (!access::lock_stream(fixture.output)) return false;
        const auto before_stop = access::snapshot(fixture.output);
        access::unlock_stream(fixture.output);
        if (!t276_stop_snapshot_is_inactive_zero(fixture.output, before_stop.generation) ||
            !t276_start_snapshot_is_coherent(fixture)) return false;
    }
    return true;
}

auto t276_coherent_state(const wormedit::audio::playhead_state_t& state) -> bool {
    if (state.generation == 0 || state.timeline_frame < state.start_frame) return false;
    if (state.active) return state.transport_generation != 0;
    return state.transport_generation != 0 ||
        (state.timeline_frame == 0 && state.start_frame == 0);
}

auto t276_nonregressing_state(const wormedit::audio::playhead_state_t& state,
                               std::uint64_t& generation) -> bool {
    if (state.generation < generation) return false;
    generation = state.generation;
    return t276_coherent_state(state);
}

auto t276_seek_stop_publications_remain_coherent(t276_output_fixture_t& fixture) -> bool {
    std::atomic<bool> reader_ready{};
    std::atomic<bool> stop_reader{};
    std::atomic<bool> reader_valid{true};
    std::atomic<std::size_t> reader_count{};
    std::thread reader([&] {
        reader_ready.store(true, std::memory_order_release);
        std::uint64_t generation{};
        while (!stop_reader.load(std::memory_order_acquire)) {
            if (!t276_nonregressing_state(wormedit::sys::sdl_preview_output_test_access_t::snapshot(
                    fixture.output), generation))
                reader_valid.store(false, std::memory_order_relaxed);
            reader_count.fetch_add(1, std::memory_order_relaxed);
        }
    });
    const auto deadline = SDL_GetTicksNS() + 1'000'000'000ULL;
    while (!reader_ready.load(std::memory_order_acquire) && SDL_GetTicksNS() < deadline)
        SDL_Delay(1);
    while (reader_count.load(std::memory_order_relaxed) < 32 && SDL_GetTicksNS() < deadline)
        SDL_Delay(1);
    const bool reader_started = reader_ready.load(std::memory_order_acquire) &&
        reader_count.load(std::memory_order_relaxed) >= 32;
    const auto writer_valid = reader_started && t276_mutate_seek_stop(fixture, 5);
    stop_reader.store(true, std::memory_order_release);
    reader.join();
    return writer_valid && reader_valid.load(std::memory_order_relaxed) &&
        reader_count.load(std::memory_order_relaxed) > 32;
}

auto t471_service_finishes_with_stream_lock_held(t276_output_fixture_t& fixture) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    if (!fixture.output.playing() || !fixture.session.is_active() ||
        fixture.session.loop_region().has_value() ||
        !access::lock_stream(fixture.output)) return false;
    if (access::source_finished(fixture.output)) {
        access::unlock_stream(fixture.output);
        return false;
    }
    std::atomic<bool> entered{};
    std::atomic<bool> finished{};
    std::atomic<bool> valid{};
    std::thread service_thread([&] {
        entered.store(true, std::memory_order_release);
        static_cast<void>(fixture.output.service());
        const auto state = access::snapshot(fixture.output);
        valid.store(state.active && state.timeline_frame >= state.start_frame &&
            state.transport_generation != 0, std::memory_order_relaxed);
        finished.store(true, std::memory_order_release);
    });
    const auto deadline = SDL_GetTicksNS() + 500'000'000ULL;
    while (!entered.load(std::memory_order_acquire) && SDL_GetTicksNS() < deadline)
        SDL_Delay(1);
    while (entered.load(std::memory_order_acquire) &&
           !finished.load(std::memory_order_acquire) && SDL_GetTicksNS() < deadline)
        SDL_Delay(1);
    const auto completed_while_locked = finished.load(std::memory_order_acquire);
    access::unlock_stream(fixture.output);
    service_thread.join();
    return completed_while_locked && valid.load(std::memory_order_relaxed);
}

auto t471_distinct_pcm(std::size_t frames) -> std::vector<float> {
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> source(frames * 2U);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto time = static_cast<double>(frame) / 44100.0;
        source[frame * 2U] = static_cast<float>(0.45 * std::sin(2.0 * pi * 173.0 * time));
        source[frame * 2U + 1U] = static_cast<float>(0.35 * std::cos(2.0 * pi * 617.0 * time));
    }
    for (const auto frame : {std::size_t{127}, std::size_t{2048}, frames - 1U}) {
        source[frame * 2U] = 0.8F;
        source[frame * 2U + 1U] = -0.7F;
    }
    return source;
}

auto t471_install_distinct_source(t276_output_fixture_t& fixture,
                                  std::vector<float>& source_pcm) -> bool {
    static_cast<void>(fixture.output.stop());
    source_pcm = t471_distinct_pcm(fixture.source_frames);
    const auto registered = fixture.sources.create_generated_source({
        .source = {"t471-unity-source", "exact-pcm-v1"},
        .interleaved_stereo = source_pcm});
    if (!registered) return false;
    fixture.binding.voices = {{.source = registered->source,
        .frame_count = registered->output_frame_count}};
    return static_cast<bool>(fixture.output.start(fixture.schedule,
        std::span{&fixture.binding, 1}, {}, 0, fixture.source_frames));
}

auto t471_wait_for_output_block(t276_output_fixture_t& fixture) -> bool {
    const auto deadline = SDL_GetTicks() + 1'000;
    while (SDL_GetTicks() < deadline) {
        const auto frame = wormedit::sys::sdl_preview_output_test_access_t::rendered_source_frame(
            fixture.output);
        const auto first = frame / wormedit::audio::maximum_read_frames;
        const auto last = (frame + 1'024U) / wormedit::audio::maximum_read_frames;
        const auto& source = fixture.binding.voices.front().source;
        if (fixture.cache.has_block({source, first}) && fixture.cache.has_block({source, last}))
            return true;
        SDL_Delay(1);
    }
    return false;
}

auto t471_unity_render_block_is_exact_pcm(t276_output_fixture_t& fixture) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    std::vector<float> source_pcm;
    if (!t471_install_distinct_source(fixture, source_pcm) ||
        !fixture.output.set_keep_pitch(true)) return false;
    wait_pitch_mode(fixture.output, true, {1, 1});
    if (!fixture.output.keep_pitch() || !t471_wait_for_output_block(fixture) ||
        fixture.output.playback_rate() != wormedit::rational_t{1, 1} ||
        !access::lock_stream(fixture.output)) return false;
    const auto before = access::rendered_source_frame(fixture.output);
    const auto before_buffer = access::buffered_source_frames(fixture.output);
    const auto samples = access::render_block(fixture.output, 1'024);
    const auto after = access::rendered_source_frame(fixture.output);
    const auto after_buffer = access::buffered_source_frames(fixture.output);
    access::unlock_stream(fixture.output);
    if (before + 1'024U > source_pcm.size() / 2U) return false;
    const auto exact = samples.size() == 2'048 && std::equal(samples.begin(), samples.end(),
        source_pcm.begin() + static_cast<std::ptrdiff_t>(before * 2U));
    return exact && before_buffer == 0 && after_buffer == 0 && after >= before &&
        after - before == 1'024U;
}

auto t471_unity_transition_keeps_pitch_and_stop_resets_clocks(t276_output_fixture_t& fixture)
    -> bool {
    if (!fixture.output.set_keep_pitch(true) || !fixture.output.set_playback_rate({1, 2}))
        return false;
    wait_pitch_mode(fixture.output, true, {1, 2});
    if (!fixture.output.set_playback_rate({1, 1})) return false;
    wait_pitch_mode(fixture.output, true, {1, 1});
    if (!fixture.output.keep_pitch() || fixture.output.playback_rate() != wormedit::rational_t{1, 1})
        return false;
    if (!fixture.output.set_playback_rate({1, 2})) return false;
    wait_pitch_mode(fixture.output, true, {1, 2});
    static_cast<void>(fixture.output.stop());
    return fixture.output.keep_pitch() && !fixture.output.playing() &&
        fixture.output.timeline_frame() == 0 && fixture.output.source_latency_frames() == 0;
}
} // namespace

TEST_CASE("SDL output playhead readers finish while the audio stream lock is held",
          "[integration][audio][sdl][preview][playhead]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    t276_output_fixture_t fixture;
    REQUIRE(fixture.ready);
    expect_pitch(t276_observer_finishes_while_stream_locked(fixture.output));
}

TEST_CASE("SDL output playhead stays coherent across concurrent seek and Stop generations",
          "[integration][audio][sdl][preview][playhead][generation]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    t276_output_fixture_t fixture;
    REQUIRE(fixture.ready);
    expect_pitch(t276_seek_stop_publications_remain_coherent(fixture));
}

namespace {
auto t276_rejects_invalidated_live_clock(bool direct_session_stop) -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    t276_output_fixture_t fixture;
    if (!fixture.ready || !access::lock_stream(fixture.output)) return false;
    if (direct_session_stop) static_cast<void>(fixture.session.stop());
    else static_cast<void>(fixture.transport.start());
    const auto state = access::snapshot(fixture.output);
    const auto frame = fixture.output.timeline_frame();
    access::unlock_stream(fixture.output);
    return !state.active && state.timeline_frame == 0 && frame == 0;
}

auto t276_completed_pitch_tail_retains_clock() -> bool {
    using access = wormedit::sys::sdl_preview_output_test_access_t;
    t276_output_fixture_t fixture;
    if (!fixture.ready) return false;
    fixture.binding.voices.front().frame_count = 2'048;
    if (!fixture.output.set_keep_pitch(true) || !fixture.output.set_playback_rate({1, 2}) ||
        !fixture.output.start(fixture.schedule, std::span{&fixture.binding, 1}, {}, 0, 2'048))
        return false;
    const auto deadline = SDL_GetTicks() + 500;
    auto state = access::snapshot(fixture.output);
    while ((!state.source_finished || state.timeline_frame == 0) && SDL_GetTicks() < deadline) {
        SDL_Delay(1);
        state = access::snapshot(fixture.output);
    }
    const bool valid_tail = state.source_finished && state.transport_generation != 0 &&
                            state.timeline_frame > 0 && fixture.output.playing() && !fixture.session.is_active() &&
                            fixture.session.playhead().generation - state.generation == 1;
    static_cast<void>(fixture.session.stop());
    const auto cancelled = access::snapshot(fixture.output);
    return valid_tail && !cancelled.active && cancelled.timeline_frame == 0;
}
}

TEST_CASE("SDL cached playhead rejects external replacement and direct session Stop",
          "[integration][audio][sdl][preview][playhead]") {
    expect_pitch(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_pitch(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    expect_pitch(t276_rejects_invalidated_live_clock(false));
    expect_pitch(t276_rejects_invalidated_live_clock(true));
}

TEST_CASE("SDL cached playhead preserves a completed keep-pitch source tail",
          "[integration][audio][sdl][preview][playhead][tail]") {
    expect_pitch(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_pitch(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    expect_pitch(t276_completed_pitch_tail_retains_clock());
}

TEST_CASE("SDL service and playhead complete while an active non-EOF stream is locked",
          "[integration][audio][sdl][preview][keep-pitch][service-lock]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    t276_output_fixture_t fixture;
    REQUIRE(fixture.ready);
    expect_pitch(t471_service_finishes_with_stream_lock_held(fixture));
}

TEST_CASE("SDL keep-pitch unity render block remains exact and reduced-rate transitions stop cleanly",
          "[integration][audio][sdl][preview][keep-pitch][unity]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    const preview_audio_subsystem_t audio_subsystem;
    t276_output_fixture_t fixture;
    REQUIRE(fixture.ready);
    expect_pitch(t471_unity_render_block_is_exact_pcm(fixture));
    expect_pitch(t471_unity_transition_keeps_pitch_and_stop_resets_clocks(fixture));
}

TEST_CASE("Layer assist runtime filters stable memberships and deduplicates chords",
          "[unit][audio][layer-assist]") {
    using namespace wormedit;
    sys::runtime::chart_preview_t preview;
    preview.chart = chart_id_t{7};
    preview.layer_assist_chart = chart_id_t{7};
    preview.layer_assist_layers = {layer_id_t{11}, layer_id_t{12}};
    preview.schedule = preview_schedule_t{};
    preview.schedule->layer_ticks = {{20, {layer_id_t{11}}}, {10, {layer_id_t{11}, layer_id_t{12}}},
                                     {10, {layer_id_t{12}}}, {30, {layer_id_t{99}}}};
    CHECK(preview.layer_assist_frames() == std::vector<std::uint64_t>{10,20});
    preview.layer_assist_chart = chart_id_t{8};
    CHECK(preview.layer_assist_frames().empty());
}

namespace {
auto capture_loop_silence(std::size_t rate_index, bool preserve_pitch, std::uint64_t length)
    -> std::size_t {
    using namespace wormedit;
    using namespace wormedit::audio;
    if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy") || !SDL_InitSubSystem(SDL_INIT_AUDIO)) return 999'999;
    const preview_audio_subsystem_t audio_subsystem;
    decoded_source_registry_t sources;
    const auto source = sources.create_generated_source({
        .source = {"t559-loop-continuity", "constant-v1"},
        .interleaved_stereo = std::vector<float>(8192 * 2, 0.25F)});
    if (!source) return 999'999;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    if (!output.open()) return 999'999;
    device_capture_t capture{output.audio_device_id()};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{.resource = resource_id_t{1}, .frame_count = 8192};
    const preview_object_source_binding_t binding{
        .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
        .voices = {{.source = source->source, .frame_count = 8192}}};
    if (!output.set_playback_rate(playback_rates[rate_index]) || !output.set_keep_pitch(preserve_pitch) ||
        !output.set_loop_region(preview_loop_region_t{1024, 1024 + length}) ||
        !output.start(schedule, std::span{&binding, 1})) return 999'999;
    const auto deadline = SDL_GetTicks() + 2000;
    while (capture.count.load(std::memory_order_acquire) < 44100 / 2 && SDL_GetTicks() < deadline)
        SDL_Delay(2); // Deliberately never service the frontend across the wraps.
    capture.detach();
    const auto count = capture.count.load(std::memory_order_acquire);
    static_cast<void>(output.stop());
    output.close();
    if (count < 44100 / 2) return 999'999;
    std::size_t run{}, longest{};
    for (const auto sample : std::span(capture.samples).subspan(44100 / 5, 44100 / 4)) {
        run = std::abs(sample) < 0.01F ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}
} // namespace
TEST_CASE("Loop PCM stays continuous without frontend service at every rate and pitch mode", "[integration][audio][sdl][loop-continuity][T559]") {
    for (std::size_t rate = 0; rate < wormedit::audio::playback_rates.size(); ++rate) {
        for (const bool preserve : {false, true}) {
            CAPTURE(rate, preserve);
            const auto silence = capture_loop_silence(rate, preserve, 128);
            INFO("Longest unintended silent run: " << silence << " device frames");
            CHECK(silence <= 2);
        }
    }
}

namespace {
auto loop_clock_stays_owned() -> bool {
    using namespace wormedit::sys;
    t276_output_fixture_t fixture;
    if (!fixture.ready || !fixture.output.set_loop_region(wormedit::audio::preview_loop_region_t{1024, 1152}) ||
        !fixture.output.seek(1024)) return false;
    const auto generation = fixture.session.playhead().generation;
    const auto deadline = SDL_GetTicksNS() + 300'000'000ULL;
    while (SDL_GetTicksNS() < deadline) {
        const auto state = sdl_preview_output_test_access_t::snapshot(fixture.output);
        if (!state.active || state.timeline_frame < 1024 || state.timeline_frame >= 1152 ||
            state.generation != generation) return false;
        std::this_thread::yield();
    }
    return true;
}
} // namespace
TEST_CASE("Loop prefetch never retires the audible play generation during an internal wrap", "[integration][audio][sdl][loop-clock][T559]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(loop_clock_stays_owned());
}

namespace {
auto stale_loop_work_preserves_replacement() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    using access = sys::sdl_preview_output_test_access_t;
    t276_output_fixture_t fixture;
    if (!fixture.ready || !fixture.output.set_loop_region(preview_loop_region_t{1024, 1152}) ||
        !fixture.output.seek(1024) || !access::lock_stream(fixture.output)) return false;
    const auto old = fixture.session.playhead().generation;
    const auto replaced = fixture.session.seek_session(1040);
    std::array<float, 32> samples;
    samples.fill(1.0F);
    const auto rendered = fixture.session.render_frames(samples, old);
    static_cast<void>(fixture.session.stop(old));
    const auto wrapped = fixture.session.restart_session_loop(old);
    const auto hidden = access::snapshot(fixture.output);
    const bool protected_session = replaced && !wrapped && fixture.session.is_active() &&
        fixture.session.playhead().generation != old && !hidden.active && hidden.timeline_frame == 0 &&
        rendered.status.has_failure(wormedit::failure_code_t::stale_generation) &&
        std::ranges::all_of(samples, [](float sample) { return sample == 0; });
    access::unlock_stream(fixture.output);
    SDL_Delay(30);
    return protected_session && fixture.session.is_active() && fixture.output.timeline_frame() == 0;
}
} // namespace
TEST_CASE("Stale loop render restart and Stop cannot mutate a replacement generation", "[integration][audio][sdl][loop-generation][T559]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(stale_loop_work_preserves_replacement());
}

TEST_CASE("One-frame and callback-sized loops remain continuous with either pitch mode", "[integration][audio][sdl][loop-continuity][T559]") {
    for (const auto length : {1U, 4096U}) {
        for (const bool preserve : {false, true}) {
            CAPTURE(length, preserve);
            CHECK(capture_loop_silence(wormedit::audio::playback_rates.size() - 1, preserve, length) <= 2);
        }
    }
}

TEST_CASE("SDL BGM lane mixing mutes and resumes a live stem without restarting playback",
          "[integration][audio][sdl][bgm-mix]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    expect_pitch(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_pitch(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    decoded_source_registry_t sources;
    const auto registered = sources.create_generated_source({
        .source = {"generated:lane-mix", "lane-mix-v1"},
        .interleaved_stereo = generated_sine(output_sample_rate * 2)});
    expect_pitch(static_cast<bool>(registered));
    decoded_block_cache_t blocks;
    transport_t transport{blocks};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    sys::sdl_preview_output_t output{session};
    expect_pitch(output.open());
    device_capture_t capture{output.audio_device_id()};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{.object = object_id_t{1}, .voices = {{
        .source = registered->source, .frame_count = output_sample_rate * 2, .mix_group = 77}}};
    sys::runtime::chart_preview_t preview;
    const std::array<std::uint64_t, 1> muted{77};
    preview.set_bgm_mix(chart_id_t{1}, muted, session);
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    const auto generation = session.playhead().generation;
    wait_capture_frames(capture, output, 4410);
    expect_pitch(std::ranges::all_of(std::span(capture.samples).first(4410),
        [](float value) { return value == 0; }));
    preview.set_bgm_mix(chart_id_t{1}, {}, session);
    auto end = capture.count.load() + 17640;
    wait_capture_frames(capture, output, end);
    expect_pitch(std::ranges::any_of(std::span(capture.samples).subspan(end - 4410, 4410),
        [](float value) { return std::abs(value) > .001F; }));
    preview.set_bgm_mix(chart_id_t{1}, muted, session);
    end = capture.count.load() + 17640;
    wait_capture_frames(capture, output, end);
    expect_pitch(std::ranges::all_of(std::span(capture.samples).subspan(end - 4410, 4410),
        [](float value) { return value == 0; }));
    expect_pitch(session.playhead().generation == generation);
    expect_pitch(output.playing());
    capture.detach();
    (void)output.stop();
    output.close();
}

namespace {
void check_revision_only_keeps_preview(preview_feedback_fixture_t& fixture) {
    fixture.preview.paused = true;
    fixture.preview.resume_frame = 123;
    ++fixture.document.revision;
    CHECK_FALSE(fixture.project_sources.reconcile_if_needed(fixture.document));
    CHECK(fixture.preview.paused);
    CHECK(fixture.preview.resume_frame == 123);
}
void check_binding_change_invalidates_preview(preview_feedback_fixture_t& fixture) {
    using namespace wormedit;
    fixture.document.audio_assets.push_back({.id = asset_id_t{55},
        .source_path = fixture_path("keysound-c4.ogg")});
    ++fixture.document.revision;
    REQUIRE(fixture.project_sources.reconcile_if_needed(fixture.document) == std::optional<std::size_t>{0});
    CHECK_FALSE(fixture.preview.paused);
    CHECK(fixture.preview.resume_frame == 0);
    CHECK(fixture.project_sources.registry().binding(asset_id_t{55}));
}
}
TEST_CASE("project source reconciliation invalidates preview only when bindings change",
          "[integration][sys][chart-preview][audio-sources]") {
    preview_feedback_fixture_t fixture;
    fixture.project_sources.audition_output = &fixture.audition_output;
    fixture.project_sources.preview_output = &fixture.output;
    fixture.project_sources.preview = &fixture.preview;
    REQUIRE(fixture.project_sources.reconcile(fixture.document, {}) == std::optional<std::size_t>{0});
    check_revision_only_keeps_preview(fixture);
    check_binding_change_invalidates_preview(fixture);
}
