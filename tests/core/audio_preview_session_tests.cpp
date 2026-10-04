#include <wormedit/audio/preview_session.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry)
    -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.register_source({
        .asset = wormedit::asset_id_t{1},
        .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord", "chart-session-v1"},
    });
    REQUIRE(added);
    return *added.binding;
}

auto any_nonzero(const std::vector<float>& samples) -> bool {
    return std::ranges::any_of(samples, [](float sample) { return sample != 0.0F; });
}

auto preview_session_survives_underrun_and_completes() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources);
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {
        {.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1},
        .voices = {{.source = source.source, .frame_count = 512}},
    };
    const auto started = session.start(schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = 512}, 64);
    if (!started)
        return false;
    cache.clear_source(source.source);

    std::vector<float> output(32U * output_channel_count);
    const auto underrun = session.render(output);
    const bool underrun_kept_session =
        underrun.status == preview_session_render_status_t::underrun &&
        underrun.mix.dropped_voices == 1 && session.active() && session.timeline_frame() == 32;
    const auto completed = session.render(output);
    return underrun_kept_session && completed.status == preview_session_render_status_t::complete &&
           completed.first_timeline_frame == 32 && !session.active();
}

auto contiguous_music_segments(const wormedit::audio::active_voice_snapshot_t& active) -> bool {
    using namespace wormedit::audio;
    if (active.voices.size() != 3) return false;
    for (std::size_t index = 0; index < active.voices.size(); ++index) {
        const auto& voice = active.voices[index];
        const auto expected = static_cast<std::uint64_t>(index) * maximum_read_frames;
        if (voice.kind != voice_kind_t::music || voice.timeline_start_frame != expected ||
            voice.source_start_frame != expected || voice.frame_count != maximum_read_frames)
            return false;
    }
    return true;
}

auto seek_music_segment_starts_at_source_frame(wormedit::audio::preview_session_t& session,
                                               wormedit::audio::transport_t& transport,
                                               wormedit::audio::preview_transport_bridge_t& bridge)
    -> bool {
    using namespace wormedit::audio;
    const auto seek = session.seek(500, {.lookahead_frames = maximum_read_frames});
    const auto current_token = bridge.session_token();
    const auto active = seek && current_token ? transport.snapshot_active_voices(*current_token)
                                               : active_voice_snapshot_t{};
    return seek && active && !active.voices.empty() &&
           active.voices.front().timeline_start_frame == 500 &&
           active.voices.front().source_start_frame == 500 &&
           active.voices.front().frame_count == maximum_read_frames - 500;
}

auto preview_loop_restarts_at_begin_and_stop_is_hard() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    constexpr std::uint64_t loop_begin = 1'024;
    constexpr std::uint64_t loop_duration = output_sample_rate * 2U;
    constexpr std::uint64_t loop_end = loop_begin + loop_duration;
    constexpr std::size_t chunk_frames = 4'096;
    decoded_source_registry_t sources;
    std::vector<float> pcm(static_cast<std::size_t>(loop_end) * output_channel_count);
    for (std::size_t frame = 0; frame < static_cast<std::size_t>(loop_end); ++frame) {
        const auto sample = static_cast<float>(frame % 128U) / 128.0F;
        pcm[frame * output_channel_count] = sample;
        pcm[frame * output_channel_count + 1] = sample;
    }
    const auto generated = sources.register_generated_source({
        .source = {"generated:preview-loop", "preview-loop-v1"},
        .interleaved_stereo = std::move(pcm),
    });
    if (!generated) return false;

    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    constexpr object_id_t music_object{std::numeric_limits<std::uint64_t>::max()};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{7}, .frame_count = generated.binding->output_frame_count};
    const preview_object_source_binding_t binding{
        .object = music_object,
        .voices = {{.source = generated.binding->source,
                    .frame_count = generated.binding->output_frame_count}},
    };
    const preview_loop_region_t region{loop_begin, loop_end};
    if (!session.set_loop_region(region) || session.set_loop_region(preview_loop_region_t{64, 64}))
        return false;
    const auto started = session.start(schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = chunk_frames});
    if (!started || session.timeline_frame() != region.begin_frame) return false;

    std::vector<float> first_block(chunk_frames * output_channel_count);
    const auto first_render = session.render(first_block);
    if (first_render.status != preview_session_render_status_t::mixed ||
        first_render.first_timeline_frame != region.begin_frame ||
        session.timeline_frame() != region.begin_frame + chunk_frames || first_block.front() != 0.0F)
        return false;
    while (session.timeline_frame() < region.end_frame) {
        const auto pump = session.pump({.lookahead_frames = chunk_frames});
        if (pump.status != preview_session_pump_status_t::ready) return false;
        const auto remaining = region.end_frame - session.timeline_frame();
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk_frames));
        std::vector<float> block(frames * output_channel_count);
        const auto rendered = session.render(block);
        if (rendered.status != preview_session_render_status_t::mixed || rendered.mix.frames != frames)
            return false;
    }
    const auto boundary = session.pump({.lookahead_frames = chunk_frames});
    if (boundary.status != preview_session_pump_status_t::loop_boundary) return false;
    const auto restarted = session.seek(region.begin_frame, {.lookahead_frames = chunk_frames});
    if (!restarted || session.timeline_frame() != region.begin_frame) return false;

    std::vector<float> repeated_block(first_block.size());
    const auto repeated_render = session.render(repeated_block);
    if (repeated_render.status != preview_session_render_status_t::mixed ||
        repeated_render.first_timeline_frame != region.begin_frame ||
        !std::ranges::equal(first_block, repeated_block)) return false;

    static_cast<void>(session.stop());
    std::vector<float> stopped(chunk_frames * output_channel_count, 1.0F);
    const auto stopped_render = session.render(stopped);
    return stopped_render.status == preview_session_render_status_t::stopped &&
           std::ranges::all_of(stopped, [](float sample) { return sample == 0.0F; });
}

auto preview_music_seek_geometry_is_correct() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    std::vector<float> pcm(3U * maximum_read_frames * output_channel_count, 0.125F);
    const auto registered = sources.register_generated_source(
        {.source = {"generated:preview-music", "preview-music-v1"},
         .interleaved_stereo = std::move(pcm)});
    if (!registered) return false;
    const auto source = *registered.binding;
    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    constexpr object_id_t music_object{std::numeric_limits<std::uint64_t>::max()};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{7}, .frame_count = source.output_frame_count};
    const preview_object_source_binding_t binding{
        .object = music_object,
        .voices = {{.source = source.source, .frame_count = source.output_frame_count}},
    };
    const auto started = session.start(schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = maximum_read_frames});
    if (!started || bridge.materialize_until(3 * maximum_read_frames).status !=
                        preview_bridge_pump_status_t::ready)
        return false;
    const auto active = transport.snapshot_active_voices(*started.token);
    const bool valid = contiguous_music_segments(active) &&
                       seek_music_segment_starts_at_source_frame(session, transport, bridge);
    return session.stop().scheduled_voices >= 1 && valid;
}

auto dropped_music_segment_is_reported(wormedit::audio::preview_session_t& session,
                                       wormedit::audio::decoded_block_cache_t& cache,
                                       const wormedit::audio::audio_source_identity_t& source)
    -> bool {
    using namespace wormedit::audio;
    cache.clear_source(source);
    std::vector<float> output(32U * output_channel_count);
    const auto dropped = session.render(output);
    return dropped.status == preview_session_render_status_t::underrun &&
           dropped.mix.dropped_voices == 1;
}

auto next_music_segment_is_still_playing(wormedit::audio::preview_session_t& session,
                                         wormedit::audio::decoded_source_registry_t& sources,
                                         wormedit::audio::transport_t& transport,
                                         wormedit::audio::session_token_t token) -> bool {
    using namespace wormedit::audio;
    std::vector<float> block(32U * output_channel_count);
    while (session.timeline_frame() < maximum_read_frames)
        static_cast<void>(session.render(block));
    const auto next = session.render(block);
    return (next.status == preview_session_render_status_t::mixed ||
            next.status == preview_session_render_status_t::complete) &&
           any_nonzero(block) && transport.is_current(token) && sources.size() != 0;
}

auto dropped_music_segment_does_not_stop_following_segment() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    std::vector<float> pcm(3U * maximum_read_frames * output_channel_count, 0.25F);
    const auto registered = sources.register_generated_source(
        {.source = {"generated:preview-music-drop", "preview-music-drop-v1"},
         .interleaved_stereo = std::move(pcm)});
    if (!registered) return false;
    const auto source = *registered.binding;
    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{7}, .frame_count = source.output_frame_count};
    constexpr object_id_t music_object{std::numeric_limits<std::uint64_t>::max()};
    const preview_object_source_binding_t binding{
        .object = music_object,
        .voices = {{.source = source.source, .frame_count = source.output_frame_count}},
    };
    const auto started = session.start(schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = maximum_read_frames});
    if (!started || bridge.materialize_until(2 * maximum_read_frames).status !=
                        preview_bridge_pump_status_t::ready ||
        !dropped_music_segment_is_reported(session, cache, source.source))
        return false;
    const auto pumped = pump_preview_lookahead(
        sources, transport, *started.token, session.timeline_frame(),
        {.lookahead_frames = maximum_read_frames});
    const auto active = transport.snapshot_active_voices(*started.token);
    const bool later_segment_scheduled = pumped && active && active.voices.size() == 1 &&
                                         active.voices.front().timeline_start_frame ==
                                             maximum_read_frames;
    const bool later_segment_played = next_music_segment_is_still_playing(
        session, sources, transport, *started.token);
    return session.stop().scheduled_voices >= 1 && later_segment_scheduled &&
           later_segment_played;
}

} // namespace

TEST_CASE("preview session primes advances refills and completes one chart timeline",
          "[unit][audio][preview-session][timeline][fixture]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources);
    decoded_block_cache_t cache;
    transport_t transport{cache, 8};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};

    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {
        {.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}},
        {.sample_frame = 256, .object = object_id_t{2}, .resource = resource_id_t{2}}};
    const std::vector<preview_object_source_binding_t> bindings{
        {.object = object_id_t{1},
         .voices = {{.source = source.source, .source_start_frame = 0, .frame_count = 512}}},
        {.object = object_id_t{2},
         .voices = {{.source = source.source, .source_start_frame = 512, .frame_count = 512}}},
    };
    const auto started = session.start(schedule, bindings, {}, 0, {.lookahead_frames = 512});
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());
    CHECK(started.materialize->scheduled_events == 2);
    REQUIRE(started.prime.has_value());
    CHECK(started.prime->overlapping_voices == 2);
    CHECK(session.active());

    std::vector<float> first(512 * output_channel_count);
    const auto first_render = session.render(first);
    CHECK(first_render.status == preview_session_render_status_t::mixed);
    CHECK(first_render.first_timeline_frame == 0);
    CHECK(any_nonzero(first));
    CHECK(session.timeline_frame() == 512);

    const auto refilled = session.pump({.lookahead_frames = 512});
    REQUIRE(refilled);
    std::vector<float> tail(256 * output_channel_count);
    const auto completed = session.render(tail);
    CHECK(completed.status == preview_session_render_status_t::complete);
    CHECK(completed.first_timeline_frame == 512);
    CHECK(any_nonzero(tail));
    CHECK_FALSE(session.active());
    CHECK(session.timeline_frame() == 0);
}

TEST_CASE("preview session streams contiguous music segments and seeks within a segment",
          "[unit][audio][preview-session][music][seek]") {
    CHECK(preview_music_seek_geometry_is_correct());
}

TEST_CASE("preview session loops a source interval on control pump and Stop silences",
          "[unit][audio][preview-session][loop][stop]") {
    CHECK(preview_loop_restarts_at_begin_and_stop_is_hard());
}

TEST_CASE("a dropped music segment does not prevent the next segment from playing",
          "[unit][audio][preview-session][music][underrun]") {
    CHECK(dropped_music_segment_does_not_stop_following_segment());
}

TEST_CASE("preview session seek replaces generations and hard Stop silences later renders",
          "[unit][audio][preview-session][seek][stop]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources);
    decoded_block_cache_t cache;
    transport_t transport{cache, 4};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 100, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1},
        .voices = {{.source = source.source, .frame_count = 1'000}},
    };
    const auto started = session.start(schedule, std::span{&binding, 1});
    REQUIRE(started);
    const auto old_token = *started.token;

    const auto sought = session.seek(200);
    REQUIRE(sought);
    CHECK(sought.token != old_token);
    CHECK(session.timeline_frame() == 200);
    CHECK_FALSE(transport.is_current(old_token));

    const auto stopped = session.stop();
    CHECK(stopped.scheduled_voices == 0);
    std::vector<float> output(128 * output_channel_count, 1.0F);
    const auto rendered = session.render(output);
    CHECK(rendered.status == preview_session_render_status_t::stopped);
    CHECK(std::ranges::all_of(output, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("preview session mixes generated metronome sources on the chart timeline",
          "[unit][audio][preview-session][metronome]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    std::vector<float> click(64 * output_channel_count, 0.25F);
    const auto generated = sources.register_generated_source({
        .source = {"internal:metronome:measure", "chart-session-v1"},
        .interleaved_stereo = std::move(click),
    });
    REQUIRE(generated);

    decoded_block_cache_t cache;
    transport_t transport{cache, 2};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    CHECK(session.metronome_volume() == 100);
    CHECK(session.metronome_count_in() == 0);
    CHECK(session.set_metronome_count_in(2));
    CHECK_FALSE(session.set_metronome_count_in(5));
    CHECK(session.metronome_count_in() == 2);
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.metronome = {{.sample_frame = 32,
                           .measure = measure_id_t{1},
                           .pulse = 0,
                           .accent = metronome_accent_t::measure}};
    const preview_metronome_source_binding_t metronome{
        .accent = metronome_accent_t::measure,
        .voice = {.source = generated.binding->source, .frame_count = 64},
    };

    const auto started =
        session.start(schedule, {}, std::span{&metronome, 1}, 0, {.lookahead_frames = 96});
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());
    CHECK(started.materialize->scheduled_events == 1);
    CHECK(session.set_metronome_volume(50));
    CHECK(session.metronome_volume() == 50);
    CHECK_FALSE(session.set_metronome_volume(101));
    CHECK(session.metronome_volume() == 50);

    std::vector<float> output(96 * output_channel_count);
    const auto rendered = session.render(output);
    CHECK(rendered.status == preview_session_render_status_t::complete);
    CHECK(std::ranges::all_of(output.begin(), output.begin() + 32 * output_channel_count,
                              [](float sample) { return sample == 0.0F; }));
    CHECK(std::ranges::all_of(output.begin() + 32 * output_channel_count, output.end(),
                              [](float sample) { return sample == 0.125F; }));
}

TEST_CASE("preview session keeps the visual timeline moving through unavailable audio",
          "[unit][audio][preview-session][missing-source]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {
        {.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const auto started = session.start(schedule, {}, {}, 0, {}, 64);
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());

    std::vector<float> output(32 * output_channel_count, 1.0F);
    const auto first = session.render(output);
    const bool first_active = session.active();
    const auto first_frame = session.timeline_frame();
    const bool silent = std::ranges::all_of(output, [](float sample) { return sample == 0.0F; });

    const auto final = session.render(output);
    const std::array checks{
        started.materialize->missing_sources == 1,
        first.status == preview_session_render_status_t::mixed,
        first_active, first_frame == 32, silent,
        final.status == preview_session_render_status_t::complete, !session.active(),
    };
    CHECK(std::ranges::all_of(checks, [](bool value) { return value; }));
}

TEST_CASE("preview session remains active after an underrun and completes on its timeline",
          "[unit][audio][preview-session][underrun]") {
    CHECK(preview_session_survives_underrun_and_completes());
}
