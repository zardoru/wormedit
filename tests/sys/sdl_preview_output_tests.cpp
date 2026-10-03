#include "sys/sdl_preview_output.h"
#include "sys/chart_preview_state.h"

#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>

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

auto exercise_sdl_loop_restart() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    constexpr std::uint64_t loop_begin = 1'024;
    constexpr std::uint64_t loop_end = 5'120;
    decoded_source_registry_t sources;
    const auto registered = sources.register_generated_source({
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
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{1}, .frame_count = loop_end};
    const preview_object_source_binding_t binding{
        .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
        .voices = {{.source = registered.binding->source, .frame_count = loop_end}},
    };
    const preview_loop_region_t loop{loop_begin, loop_end};
    if (!output.set_loop_region(loop) || !output.start(schedule, std::span{&binding, 1})) {
        output.close();
        return false;
    }
    const bool start_clock_aligned = output.timeline_frame() == loop_begin;
    const auto initial_token = bridge.session_token();
    const auto deadline = SDL_GetTicks() + 2'000;
    while (session.timeline_frame() < loop_end && SDL_GetTicks() < deadline) SDL_Delay(1);
    if (session.timeline_frame() != loop_end) {
        static_cast<void>(output.stop());
        output.close();
        return false;
    }
    SDL_Delay(20);
    const bool worker_waited_for_control = session.timeline_frame() == loop_end &&
        bridge.session_token() == initial_token;
    bool restarted{};
    while (!restarted && SDL_GetTicks() < deadline) {
        const auto serviced = output.service();
        if (serviced && !*serviced) break;
        const auto token = bridge.session_token();
        restarted = token && initial_token && token->generation > initial_token->generation &&
                    session.timeline_frame() < loop_end;
        SDL_Delay(1);
    }
    const bool repeated = start_clock_aligned && worker_waited_for_control && restarted &&
        output.timeline_frame() >= loop_begin && output.timeline_frame() < loop_end && output.playing() &&
        session.loop_region() == std::optional<preview_loop_region_t>{loop};
    static_cast<void>(output.stop());
    std::array<float, 32> stopped{};
    stopped.fill(1.0F);
    const auto silence = session.render(stopped);
    const bool hard_stop = silence.status == preview_session_render_status_t::stopped &&
        std::ranges::all_of(stopped, [](float sample) { return sample == 0.0F; });
    output.close();
    return repeated && hard_stop;
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
    const auto registered = sources.register_generated_source({
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
        .voices = {{.source = registered.binding->source, .frame_count = source_end}},
    };
    const preview_loop_region_t loop{loop_begin, loop_end};
    if (!output.set_loop_region(loop) || !output.start(schedule, std::span{&binding, 1})) {
        output.close();
        return false;
    }
    const auto initial = bridge.session_token();
    const auto deadline = SDL_GetTicks() + 2'000;
    const bool reached_end = wait_for_session_frame(session, loop_end, deadline);
    const bool stayed_active = session.active() && output.playing() &&
                               session.timeline_frame() == loop_end;
    SDL_Delay(20);
    const bool worker_waited = bridge.session_token() == initial &&
                               session.timeline_frame() == loop_end;
    bool restarted{};
    while (!restarted && SDL_GetTicks() < deadline) {
        const auto serviced = output.service();
        if (serviced && !*serviced) break;
        const auto token = bridge.session_token();
        restarted = token && initial && token->generation > initial->generation &&
                    session.timeline_frame() < loop_end;
        SDL_Delay(1);
    }
    const bool looped = reached_end && stayed_active && worker_waited && restarted;
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
    const auto registered = sources.register_generated_source({
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
        .voices = {{.source = registered.binding->source, .frame_count = source_end}},
    };
    if (!output.set_loop_region(preview_loop_region_t{loop_begin, loop_end}) ||
        !output.start(schedule, std::span{&binding, 1})) {
        output.close();
        return false;
    }
    const auto deadline = SDL_GetTicks() + 2'000;
    const auto at_boundary = wait_for_session_frame(session, loop_end, deadline);
    SDL_Delay(20); // Let the refill worker publish its pending boundary marker.
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
    const auto deadline = SDL_GetTicks() + pitch_restart_budget_ms(rate);
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

namespace {
void exercise_background_refill() {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.register_generated_source({
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
        .voices = {{.source = registered.binding->source, .frame_count = 44100 * 2}}};
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
    expect_pitch(service->silenced_voices == 0);
    static_cast<void>(output.stop());
    std::array<float, 256> silence;
    silence.fill(1.0F);
    const auto rendered = session.render(silence);
    expect_pitch(rendered.status == preview_session_render_status_t::stopped);
    expect_pitch(std::ranges::all_of(silence, [](float value) { return value == 0.0F; }));
}
void exercise_refill_generations() {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.register_generated_source({
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
        .voices = {{.source = registered.binding->source, .frame_count = 44100}}};
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    for (std::uint64_t seek = 0; seek < 20; ++seek) {
        const auto sought = output.seek((seek % 4) * 4096);
        INFO("seek iteration " << seek << " status " << static_cast<int>(sought.status)
             << " message " << sought.message);
        INFO("session status " << (sought.session ? static_cast<int>(sought.session->status) : -1));
        INFO("bridge status " << (sought.session && sought.session->bridge
             ? static_cast<int>(sought.session->bridge->status) : -1));
        expect_pitch(static_cast<bool>(sought));
        SDL_Delay(2);
        const auto service = output.service();
        expect_pitch(!service || static_cast<bool>(*service));
    }
    static_cast<void>(output.stop());
    expect_pitch(!session.active() && !output.playing());
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
    const auto registered = sources.register_generated_source({
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
        .voices = {{.source = registered.binding->source, .frame_count = 44100 * 2}}};
    expect_pitch(static_cast<bool>(output.start(schedule, std::span{&binding, 1})));
    expect_pitch(sources.remove(registered.binding->asset));
    // The missing next block drops the voice and completes the session; the
    // unread worker failure must survive that completion until the frame reads it.
    SDL_Delay(800);
    expect_pitch(!session.active());
    const auto failed = output.service();
    expect_pitch(failed && failed->status == sys::sdl_preview_service_status_t::pump_failed);
    expect_pitch(failed->pump && failed->pump->status == preview_session_pump_status_t::decode_failed);
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
        static_cast<void>(session.render(samples));
        return !session.active() && std::ranges::all_of(samples, [](float value) { return value == 0.0F; });
    }
    }
}

auto preview_lock_order_stress() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto registered = sources.register_source({
        .asset = asset_id_t{1}, .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord.ogg", "lock-order-stress"}});
    if (!registered || registered.binding->output_frame_count == 0) return false;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    wormedit::sys::sdl_preview_output_t output{session};
    if (!output.open()) return false;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1}, .voices = {{.source = registered.binding->source,
            .frame_count = registered.binding->output_frame_count}}};
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
        if (output.available() || session.active()) return false;
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
    const auto registered = sources.register_generated_source({
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
        .voices = {{.source = registered.binding->source, .frame_count = 11025}}};
    CHECK(exercise_preview_worker_closes(output, session, schedule, binding));
}

TEST_CASE("SDL loop restarts at the source tail on control service and Stop clears it",
          "[integration][audio][sdl][preview][loop]") {
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    REQUIRE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const preview_audio_subsystem_t audio_subsystem;
    CHECK(exercise_sdl_loop_restart());
}

TEST_CASE("SDL loop can be disabled at a pending boundary and continue the chart",
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
