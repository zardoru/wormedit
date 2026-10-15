#include <wormedit/audio/preview_session.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <vector>

namespace {

auto start_primed(wormedit::audio::preview_session_t& session,
                  const wormedit::preview_schedule_t& schedule,
                  std::span<const wormedit::audio::preview_object_source_binding_t> objects,
                  std::span<const wormedit::audio::preview_metronome_source_binding_t> metronome = {},
                  std::uint64_t frame = 0, wormedit::audio::preview_pump_options_t options = {},
                  std::uint64_t end = 0) -> wormedit::audio::preview_session_start_result_t {
    auto result = session.start(schedule, objects, metronome, frame, options, end);
    if (result) result.prime = session.pump().decode;
    return result;
}

auto seek_primed(wormedit::audio::preview_session_t& session, std::uint64_t frame,
                 wormedit::audio::preview_pump_options_t options = {})
    -> wormedit::audio::preview_session_start_result_t {
    auto result = session.seek(frame, options);
    if (result) result.prime = session.pump().decode;
    return result;
}


auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry)
    -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.create_source({
        .asset = wormedit::asset_id_t{1},
        .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord", "chart-session-v1"},
    });
    REQUIRE(added);
    return *added;
}

auto has_active_voice(const std::optional<wormedit::audio::active_voice_snapshot_outcome_t>& snapshot,
                      std::uint64_t source_start, std::uint64_t frame_count) -> bool {
    return snapshot && snapshot->has_value() && std::ranges::any_of(snapshot->value(), [&](const auto& voice) {
        return voice.source_start_frame == source_start && voice.frame_count == frame_count;
    });
}

auto any_nonzero(const std::vector<float>& samples) -> bool {
    return std::ranges::any_of(samples, [](float sample) { return sample != 0.0F; });
}

void check_session_condition(bool condition,
                             std::source_location location = std::source_location::current()) {
    INFO("assertion at source line " << location.line());
    CHECK(condition);
}

void require_session_condition(bool condition,
                               std::source_location location = std::source_location::current()) {
    INFO("required condition at source line " << location.line());
    REQUIRE(condition);
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
    const auto started = start_primed(session, schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = 512}, 64);
    if (!started)
        return false;
    cache.clear_source(source.source);

    std::vector<float> output(32U * output_channel_count);
    const auto rendered = session.render_frames(output);
    if (rendered.has_value()) return false;
    const auto& underrun = rendered.error();
    const bool underrun_kept_session =
        underrun.status == preview_session_render_status_t::underrun &&
        underrun.mix.dropped_voices == 1 && session.is_active() && session.timeline_frame() == 32;
    const auto completed = session.render_frames(output);
    return underrun_kept_session && completed.value().status == preview_session_render_status_t::complete &&
           completed.value().first_timeline_frame == 32 && !session.is_active();
}

auto contiguous_music_segments(const wormedit::audio::active_voice_snapshot_outcome_t& active) -> bool {
    using namespace wormedit::audio;
    if (!active || active.value().size() != 3) return false;
    for (std::size_t index = 0; index < active.value().size(); ++index) {
        const auto& voice = active.value()[index];
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
    const auto seek = seek_primed(session, 500, {.lookahead_frames = maximum_read_frames});
    const auto current_token = bridge.session_token();
    const auto active = seek && current_token ? transport.snapshot_voices(*current_token)
                                               : active_voice_snapshot_outcome_t{std::unexpected(active_voice_snapshot_status_t::stale_generation)};
    return seek && active && !active.value().empty() &&
           active.value().front().timeline_start_frame == 500 &&
           active.value().front().source_start_frame == 500 &&
           active.value().front().frame_count == maximum_read_frames - 500;
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
    const auto generated = sources.create_generated_source({
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
        .resource = resource_id_t{7}, .frame_count = generated->output_frame_count};
    const preview_object_source_binding_t binding{
        .object = music_object,
        .voices = {{.source = generated->source,
                    .frame_count = generated->output_frame_count}},
    };
    const preview_loop_region_t region{loop_begin, loop_end};
    if (!session.set_loop_region(region) || session.set_loop_region(preview_loop_region_t{64, 64}))
        return false;
    const auto started = start_primed(session, schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = chunk_frames});
    if (!started || session.timeline_frame() != region.begin_frame) return false;

    std::vector<float> first_block(chunk_frames * output_channel_count);
    const auto first_render = session.render_frames(first_block);
    if (first_render.value().status != preview_session_render_status_t::mixed ||
        first_render.value().first_timeline_frame != region.begin_frame ||
        session.timeline_frame() != region.begin_frame + chunk_frames || first_block.front() != 0.0F)
        return false;
    while (session.timeline_frame() < region.end_frame) {
        const auto pump = session.pump({.lookahead_frames = chunk_frames});
        if (pump.status != preview_session_pump_status_t::ready) return false;
        const auto remaining = region.end_frame - session.timeline_frame();
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk_frames));
        std::vector<float> block(frames * output_channel_count);
        const auto rendered = session.render_frames(block);
        if (rendered.value().status != preview_session_render_status_t::mixed || rendered.value().mix.frames != frames)
            return false;
    }
    const auto boundary = session.pump({.lookahead_frames = chunk_frames});
    if (boundary.status != preview_session_pump_status_t::loop_boundary) return false;
    const auto restarted = seek_primed(session, region.begin_frame, {.lookahead_frames = chunk_frames});
    if (!restarted || session.timeline_frame() != region.begin_frame) return false;

    std::vector<float> repeated_block(first_block.size());
    const auto repeated_render = session.render_frames(repeated_block);
    if (repeated_render.value().status != preview_session_render_status_t::mixed ||
        repeated_render.value().first_timeline_frame != region.begin_frame ||
        !std::ranges::equal(first_block, repeated_block)) return false;

    static_cast<void>(session.stop());
    std::vector<float> stopped(chunk_frames * output_channel_count, 1.0F);
    const auto stopped_render = session.render_frames(stopped);
    return stopped_render.value().status == preview_session_render_status_t::stopped &&
           std::ranges::all_of(stopped, [](float sample) { return sample == 0.0F; });
}

auto preview_music_seek_geometry_is_correct() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    std::vector<float> pcm(3U * maximum_read_frames * output_channel_count, 0.125F);
    const auto registered = sources.create_generated_source(
        {.source = {"generated:preview-music", "preview-music-v1"},
         .interleaved_stereo = std::move(pcm)});
    if (!registered) return false;
    const auto source = *registered;
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
    const auto started = start_primed(session, schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = maximum_read_frames});
    if (!started || bridge.materialize_until(3 * maximum_read_frames).status !=
                        preview_bridge_pump_status_t::ready)
        return false;
    const auto active = transport.snapshot_voices(*started.token);
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
    const auto dropped = session.render_frames(output);
    return !dropped && dropped.error().status == preview_session_render_status_t::underrun &&
           dropped.error().mix.dropped_voices == 1;
}

auto next_music_segment_is_still_playing(wormedit::audio::preview_session_t& session,
                                         wormedit::audio::decoded_source_registry_t& sources,
                                         wormedit::audio::transport_t& transport,
                                         wormedit::audio::session_token_t token) -> bool {
    using namespace wormedit::audio;
    std::vector<float> block(32U * output_channel_count);
    while (session.timeline_frame() < maximum_read_frames)
        static_cast<void>(session.render_frames(block));
    const auto next = session.render_frames(block);
    return (next.value().status == preview_session_render_status_t::mixed ||
            next.value().status == preview_session_render_status_t::complete) &&
           any_nonzero(block) && transport.is_current(token) && sources.size() != 0;
}

auto dropped_music_segment_does_not_stop_following_segment() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    std::vector<float> pcm(3U * maximum_read_frames * output_channel_count, 0.25F);
    const auto registered = sources.create_generated_source(
        {.source = {"generated:preview-music-drop", "preview-music-drop-v1"},
         .interleaved_stereo = std::move(pcm)});
    if (!registered) return false;
    const auto source = *registered;
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
    const auto started = start_primed(session, schedule, std::span{&binding, 1}, {}, 0,
                                       {.lookahead_frames = maximum_read_frames});
    if (!started || bridge.materialize_until(2 * maximum_read_frames).status !=
                        preview_bridge_pump_status_t::ready ||
        !dropped_music_segment_is_reported(session, cache, source.source))
        return false;
    const auto pumped = pump_preview_lookahead(
        sources, transport, *started.token, session.timeline_frame(),
        {.lookahead_frames = maximum_read_frames});
    const auto active = transport.snapshot_voices(*started.token);
    const bool later_segment_scheduled = pumped && active && active.value().size() == 1 &&
                                         active.value().front().timeline_start_frame ==
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
    const auto started = start_primed(session, schedule, bindings, {}, 0, {.lookahead_frames = 512});
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());
    CHECK(started.materialize->scheduled_events == 2);
    REQUIRE(started.prime.has_value());
    CHECK(started.prime->overlapping_voices == 2);
    CHECK(session.is_active());

    std::vector<float> first(512 * output_channel_count);
    const auto first_render = session.render_frames(first);
    CHECK(first_render.value().status == preview_session_render_status_t::mixed);
    CHECK(first_render.value().first_timeline_frame == 0);
    CHECK(any_nonzero(first));
    CHECK(session.timeline_frame() == 512);

    const auto refilled = session.pump({.lookahead_frames = 512});
    REQUIRE(refilled);
    std::vector<float> tail(256 * output_channel_count);
    const auto completed = session.render_frames(tail);
    CHECK(completed.value().status == preview_session_render_status_t::complete);
    CHECK(completed.value().first_timeline_frame == 512);
    CHECK(any_nonzero(tail));
    CHECK_FALSE(session.is_active());
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
    const auto started = start_primed(session, schedule, std::span{&binding, 1});
    require_session_condition(static_cast<bool>(started));
    const auto old_token = *started.token;

    const auto sought = seek_primed(session, 200);
    require_session_condition(static_cast<bool>(sought));
    check_session_condition(sought.token != old_token);
    check_session_condition(session.timeline_frame() == 200);
    check_session_condition(!transport.is_current(old_token));
    check_session_condition(has_active_voice(transport.snapshot_voices(*sought.token), 100, 900));

    const auto stopped = session.stop();
    check_session_condition(stopped.scheduled_voices == 1);
    std::vector<float> output(128 * output_channel_count, 1.0F);
    const auto rendered = session.render_frames(output);
    check_session_condition(rendered.value().status == preview_session_render_status_t::stopped);
    check_session_condition(std::ranges::all_of(output, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("preview session mixes generated metronome sources on the chart timeline",
          "[unit][audio][preview-session][metronome]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    std::vector<float> click(64 * output_channel_count, 0.25F);
    const auto generated = sources.create_generated_source({
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
        .voice = {.source = generated->source, .frame_count = 64},
    };

    const auto started =
        start_primed(session, schedule, {}, std::span{&metronome, 1}, 0, {.lookahead_frames = 96});
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());
    CHECK(started.materialize->scheduled_events == 1);
    CHECK(session.set_metronome_volume(50));
    CHECK(session.metronome_volume() == 50);
    CHECK_FALSE(session.set_metronome_volume(101));
    CHECK(session.metronome_volume() == 50);

    std::vector<float> output(96 * output_channel_count);
    const auto rendered = session.render_frames(output);
    CHECK(rendered.value().status == preview_session_render_status_t::complete);
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
    const auto started = start_primed(session, schedule, {}, {}, 0, {}, 64);
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());

    std::vector<float> output(32 * output_channel_count, 1.0F);
    const auto first = session.render_frames(output);
    const bool first_active = session.is_active();
    const auto first_frame = session.timeline_frame();
    const bool silent = std::ranges::all_of(output, [](float sample) { return sample == 0.0F; });

    const auto final = session.render_frames(output);
    const std::array checks{
        started.materialize->missing_sources == 1,
        first.value().status == preview_session_render_status_t::mixed,
        first_active, first_frame == 32, silent,
        final.value().status == preview_session_render_status_t::complete, !session.is_active(),
    };
    CHECK(std::ranges::all_of(checks, [](bool value) { return value; }));
}

TEST_CASE("preview session remains active after an underrun and completes on its timeline",
          "[unit][audio][preview-session][underrun]") {
    CHECK(preview_session_survives_underrun_and_completes());
}

namespace {
template <typename value_t>
void expect_pending(const value_t& value) { REQUIRE(static_cast<bool>(value)); }

struct pending_preview_fixture_t {
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::audio::decoded_source_binding_t source{register_fixture(sources)};
    wormedit::audio::decoded_block_cache_t cache;
    wormedit::audio::transport_t transport{cache};
    wormedit::audio::preview_transport_bridge_t bridge{transport};
    wormedit::audio::preview_session_t session{sources, transport, bridge};
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    wormedit::audio::preview_object_source_binding_t binding;

    pending_preview_fixture_t() {
        schedule.bgm = {{.sample_frame = 0, .object = wormedit::object_id_t{1},
                         .resource = wormedit::resource_id_t{1}}};
        binding = {.object = wormedit::object_id_t{1},
                   .voices = {{.source = source.source, .frame_count = 1'000}}};
    }
};

void verify_pending_silence(wormedit::audio::preview_session_t& session, std::uint64_t frame) {
    std::array<float, 64> output;
    output.fill(1.0F);
    const auto rendered = session.render_frames(output);
    expect_pending(rendered.value().status == wormedit::audio::preview_session_render_status_t::priming);
    expect_pending(session.timeline_frame() == frame);
    expect_pending(std::ranges::all_of(output, [](float value) { return value == 0.0F; }));
}
}

TEST_CASE("preview start and rapid seeks defer decoding and prime only the latest generation",
          "[unit][audio][preview-session][async-seek]") {
    pending_preview_fixture_t fixture;
    const auto started = fixture.session.start(fixture.schedule, std::span{&fixture.binding, 1});
    expect_pending(started);
    expect_pending(!started.prime);
    verify_pending_silence(fixture.session, 0);
    for (std::uint64_t frame = 10; frame <= 100; frame += 10) {
        expect_pending(fixture.session.seek(frame));
        expect_pending(fixture.cache.stats().entries == 0);
        expect_pending(!fixture.transport.is_current(*started.token));
    }
    verify_pending_silence(fixture.session, 100);
    expect_pending(fixture.session.pump());
    std::array<float, 64> output;
    const auto rendered = fixture.session.render_frames(output);
    expect_pending(rendered.value().first_timeline_frame == 100);
    expect_pending(rendered.value().status == wormedit::audio::preview_session_render_status_t::mixed);
    expect_pending(std::ranges::any_of(output, [](float value) { return value != 0.0F; }));
}

TEST_CASE("Stop cancels pending preview work and external generation changes clear playheads",
          "[unit][audio][preview-session][async-seek]") {
    pending_preview_fixture_t fixture;
    expect_pending(fixture.session.start(fixture.schedule, std::span{&fixture.binding, 1}));
    static_cast<void>(fixture.session.stop());
    expect_pending(!fixture.session.pump());
    expect_pending(fixture.cache.stats().entries == 0);
    expect_pending(fixture.session.start(fixture.schedule, std::span{&fixture.binding, 1}));
    static_cast<void>(fixture.transport.stop());
    expect_pending(!fixture.session.is_active());
    expect_pending(fixture.session.timeline_frame() == 0);
    expect_pending(!fixture.session.playhead().active);
}

namespace {
struct in_flight_preview_fixture_t {
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::audio::decoded_block_cache_t cache;
    wormedit::audio::transport_t transport{cache};
    wormedit::audio::preview_transport_bridge_t bridge{transport};
    wormedit::audio::preview_session_t session{sources, transport, bridge};
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    std::vector<wormedit::audio::preview_object_source_binding_t> bindings;
    wormedit::audio::audio_source_identity_t source;

    in_flight_preview_fixture_t() {
        std::vector<float> pcm(2'000, -0.5F);
        std::fill_n(pcm.begin(), 1'000, 0.5F);
        const auto registered = sources.create_generated_source({
            .source = {"in-flight", "v1"}, .interleaved_stereo = std::move(pcm)});
        expect_pending(registered);
        source = registered->source;
        schedule.bgm = {{.sample_frame = 0, .object = wormedit::object_id_t{1},
                         .resource = wormedit::resource_id_t{1}},
                        {.sample_frame = 128, .object = wormedit::object_id_t{2},
                         .resource = wormedit::resource_id_t{1}}};
        bindings = {{.object = wormedit::object_id_t{1},
                     .voices = {{.source = source, .frame_count = 64}}},
                    {.object = wormedit::object_id_t{2},
                     .voices = {{.source = source, .source_start_frame = 500, .frame_count = 500}}}};
        expect_pending(session.start(schedule, bindings, {}, 0, {.lookahead_frames = 64}));
        expect_pending(session.pump());
    }

    auto wait_for_old_materialization() -> bool {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (transport.scheduled_voice_count() != 2) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::yield();
        }
        return true;
    }

    auto perform_seeks(bool stop) -> bool {
        const auto begin = std::chrono::steady_clock::now();
        for (std::uint64_t frame = 200; frame < 210; ++frame) {
            if (!session.seek(frame, {.lookahead_frames = 64})) return false;
        }
        if (stop) static_cast<void>(session.stop());
        return std::chrono::steady_clock::now() - begin < std::chrono::milliseconds{100};
    }

    auto supersede_blocked_pump(bool stop) -> bool {
        wormedit::audio::preview_session_pump_result_t old_result;
        std::jthread worker, controller;
        std::atomic<bool> finished{};
        bool materialized{}, completed_while_locked{}, control_result{};
        const auto gated = cache.with_block({source, 0}, [&](const auto&) {
            worker = std::jthread([&] { old_result = session.pump({.lookahead_frames = 256}); });
            materialized = wait_for_old_materialization();
            if (!materialized) return;
            controller = std::jthread([&] {
                control_result = perform_seeks(stop);
                finished.store(true, std::memory_order_release);
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
            while (!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            completed_while_locked = finished.load(std::memory_order_acquire);
        });
        if (worker.joinable()) worker.join();
        if (controller.joinable()) controller.join();
        return gated && materialized && completed_while_locked && control_result &&
               old_result.status == wormedit::audio::preview_session_pump_status_t::not_started;
    }

};
}

TEST_CASE("in-flight old preview pump cannot activate rapid-seek audio or undo Stop",
          "[unit][audio][preview-session][async-seek]") {
    in_flight_preview_fixture_t latest;
    expect_pending(latest.supersede_blocked_pump(false));
    verify_pending_silence(latest.session, 209);
    expect_pending(latest.session.pump());
    std::array<float, 64> output;
    const auto mixed = latest.session.render_frames(output);
    expect_pending(mixed.value().first_timeline_frame == 209);
    expect_pending(std::ranges::all_of(output, [](float value) { return value == -0.5F; }));
    in_flight_preview_fixture_t stopped;
    expect_pending(stopped.supersede_blocked_pump(true));
    expect_pending(!stopped.session.is_active());
    expect_pending(!stopped.session.pump());
}

TEST_CASE("Loop region presence is readable without the session mutex and clears on Stop",
          "[unit][audio][preview-session]") {
    using wormedit::audio::preview_loop_region_t;
    pending_preview_fixture_t fixture;
    CHECK_FALSE(fixture.session.has_loop_region());
    REQUIRE(fixture.session.set_loop_region(preview_loop_region_t{64, 4'096}));
    CHECK(fixture.session.has_loop_region());
    REQUIRE(fixture.session.set_loop_region(std::nullopt));
    CHECK_FALSE(fixture.session.has_loop_region());
    REQUIRE(fixture.session.set_loop_region(preview_loop_region_t{64, 4'096}));
    static_cast<void>(fixture.session.stop());
    CHECK_FALSE(fixture.session.has_loop_region());
    CHECK(fixture.session.has_loop_region() == fixture.session.loop_region().has_value());
}

TEST_CASE("Layer assist clicks are audible with metronome off and cancel queued travel without restarting music",
          "[unit][audio][preview-session][layer-assist]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = sources.create_generated_source({.source = {"generated:layer-tick", "v1"},
        .interleaved_stereo = std::vector<float>(512*output_channel_count, 0.1F)});
    require_session_condition(static_cast<bool>(source));
    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    constexpr object_id_t music_id{std::numeric_limits<std::uint64_t>::max()};
    schedule.music = preview_schedule_t::music_event_t{resource_id_t{1}, 0, 0, 512};
    const preview_object_source_binding_t music{.object = music_id,
        .voices = {{.source = source->source, .frame_count = 512}}};
    const preview_metronome_source_binding_t click{.accent = metronome_accent_t::regular,
        .voice = {.source = source->source, .frame_count = 8}};
    session.set_metronome_enabled(false);
    require_session_condition(session.set_layer_tick_frames(std::array<std::uint64_t, 2>{32,96}));
    const auto started = start_primed(session, schedule, std::span{&music,1}, std::span{&click,1},
                                      0, {.lookahead_frames = 128}, 512);
    require_session_condition(static_cast<bool>(started));
    std::vector<float> output(64*output_channel_count);
    check_session_condition(session.render_frames(output).value().status == preview_session_render_status_t::mixed);
    check_session_condition(std::abs(output[30*output_channel_count] - 0.1F) < 0.00001F);
    check_session_condition(std::abs(output[32*output_channel_count] - 0.2F) < 0.00001F);
    check_session_condition(std::abs(output[40*output_channel_count] - 0.1F) < 0.00001F);
    require_session_condition(session.set_layer_tick_frames({}));
    check_session_condition(transport.is_current(*started.token));
    check_session_condition(session.render_frames(output).value().status == preview_session_render_status_t::mixed);
    check_session_condition(std::ranges::all_of(output, [](float x) { return std::abs(x - 0.1F) < 0.00001F; }));
    check_session_condition(session.timeline_frame() == 128);
    require_session_condition(session.set_layer_tick_frames(std::array<std::uint64_t,2>{32,96}));
    require_session_condition(static_cast<bool>(seek_primed(session, 32, {.lookahead_frames = 128})));
    check_session_condition(session.render_frames(output).value().status == preview_session_render_status_t::mixed);
    check_session_condition(std::abs(output.front()-0.2F) < 0.00001F);
    require_session_condition(session.set_metronome_volume(50));
    require_session_condition(static_cast<bool>(seek_primed(session, 32, {.lookahead_frames = 128})));
    check_session_condition(session.render_frames(output).value().status == preview_session_render_status_t::mixed);
    check_session_condition(std::abs(output.front()-0.15F) < 0.00001F);
}
