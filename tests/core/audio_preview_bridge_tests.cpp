#include <wormedit/audio/preview_bridge.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <vector>

namespace {

using namespace wormedit;
using namespace wormedit::audio;

auto binding(object_id_t object = object_id_t{1}) -> preview_object_source_binding_t {
    return {.object = object,
            .voices = {{.source = {"asset:key", "revision:1"}, .frame_count = 64, .gain = 0.1F}}};
}

auto click_binding(metronome_accent_t accent) -> preview_metronome_source_binding_t {
    return {.accent = accent,
            .voice = {.source = {"asset:click", "revision:1"}, .frame_count = 64, .gain = 0.1F}};
}

auto audio_event(std::uint64_t frame, std::uint64_t id, resource_id_t resource = resource_id_t{1})
    -> preview_audio_event_t {
    return {.sample_frame = frame, .object = object_id_t{id}, .resource = resource};
}

auto schedule() -> preview_schedule_t {
    return {.sample_rate = output_sample_rate};
}

auto assist_grid(double phase) -> preview_assist_click_grid_t;

auto transport_for(decoded_block_cache_t& cache, std::size_t capacity = 16) -> transport_t {
    return transport_t{cache, capacity};
}

void check_audio_condition(bool condition,
                           std::source_location location = std::source_location::current()) {
    INFO("assertion at source line " << location.line());
    CHECK(condition);
}

void require_audio_condition(bool condition,
                             std::source_location location = std::source_location::current()) {
    INFO("required condition at source line " << location.line());
    REQUIRE(condition);
}

} // namespace

TEST_CASE("preview bridge windows more than 256 sequential events through bounded slots",
          "[unit][audio][preview-bridge][windowing]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 256);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    constexpr std::size_t event_count = 600;
    chart.playable.reserve(event_count);
    for (std::size_t index = 0; index < event_count; ++index) {
        chart.playable.push_back(audio_event(index * 10, index + 1));
    }
    std::vector<preview_object_source_binding_t> objects;
    objects.reserve(event_count);
    for (std::size_t index = 0; index < event_count; ++index) {
        auto short_voice = binding(object_id_t{index + 1});
        short_voice.voices.front().frame_count = 2;
        objects.push_back(std::move(short_voice));
    }
    const auto started = bridge.start(chart, objects);
    REQUIRE(started);

    std::size_t scheduled{};
    auto pumped = bridge.materialize_until(event_count * 10);
    scheduled += pumped.scheduled_events;
    CHECK(pumped.status == preview_bridge_pump_status_t::capacity_limited);
    CHECK(pumped.scheduled_events == 256);
    CHECK(transport.scheduled_voice_count() == 256);

    std::array<float, 8> output{};
    for (std::uint64_t frame = 0; frame < event_count * 10; frame += 4) {
        pumped = bridge.materialize_until(event_count * 10);
        scheduled += pumped.scheduled_events;
        const auto mixed = transport.mix(*started.token, frame, output);
        REQUIRE(mixed.status == transport_mix_status_t::mixed);
        CHECK((pumped.status == preview_bridge_pump_status_t::ready ||
               pumped.status == preview_bridge_pump_status_t::capacity_limited));
    }

    CHECK(scheduled == event_count);
    CHECK(bridge.materialize_until(event_count * 10).scheduled_events == 0);
    CHECK(transport.scheduled_voice_count() == 0);
}

TEST_CASE("preview bridge preserves playable, BGM, and metronome polyphony",
          "[unit][audio][preview-bridge][polyphony]") {
    decoded_block_cache_t cache;
    const auto pcm = std::vector<float>(64 * output_channel_count, 0.25F);
    REQUIRE(cache.store({.source = {"asset:key", "revision:1"}}, 64, pcm) ==
            block_store_status_t::stored);
    REQUIRE(cache.store({.source = {"asset:click", "revision:1"}}, 64, pcm) ==
            block_store_status_t::stored);
    auto transport = transport_for(cache, 16);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1), audio_event(0, 2), audio_event(0, 3)};
    chart.bgm = {audio_event(0, 4), audio_event(0, 5)};
    chart.metronome = {{.sample_frame = 0,
                        .measure = measure_id_t{1},
                        .pulse = 0,
                        .accent = metronome_accent_t::measure}};
    const std::array objects{binding(object_id_t{1}), binding(object_id_t{2}),
                             binding(object_id_t{3}), binding(object_id_t{4}),
                             binding(object_id_t{5})};
    const std::array clicks{click_binding(metronome_accent_t::measure)};
    const auto started = bridge.start(chart, objects, clicks);
    REQUIRE(started);

    const auto pumped = bridge.materialize_until(1);
    CHECK(pumped.status == preview_bridge_pump_status_t::ready);
    CHECK(pumped.scheduled_events == 6);
    CHECK(transport.scheduled_voice_count() == 6);

    std::array<float, 64 * output_channel_count> output{};
    const auto mixed = transport.mix(*started.token, 0, output);
    CHECK(mixed.status == transport_mix_status_t::mixed);
    CHECK(mixed.voices_mixed == 6);
    CHECK(mixed.missing_blocks == 0);
    CHECK(transport.scheduled_voice_count() == 0);
    CHECK(output.front() == Catch::Approx(0.15F));
}

auto snapshot_has_voice_count(const std::optional<active_voice_snapshot_t>& snapshot,
                              std::size_t count) -> bool {
    return snapshot && snapshot->voices.size() == count;
}

auto has_scheduled_voice(const std::optional<active_voice_snapshot_t>& snapshot,
                         std::uint64_t source_frame, std::uint64_t frame_count,
                         std::uint64_t timeline_frame) -> bool {
    return snapshot && std::ranges::any_of(snapshot->voices, [&](const auto& voice) {
        return voice.source_start_frame == source_frame && voice.frame_count == frame_count &&
               voice.timeline_start_frame == timeline_frame;
    });
}

TEST_CASE("preview bridge resumes active BMSON slices at the sought source frame",
          "[unit][audio][preview-bridge][seek][bmson]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 16);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 5)};
    chart.bgm = {audio_event(0, 1),
                 audio_event(1, 3),
                 audio_event(2, 4),
                 {.sample_frame = 5, .object = object_id_t{2}, .resource = resource_id_t{2}}};
    auto first = binding(object_id_t{1});
    first.voices.front().source_start_frame = 10;
    first.voices.front().frame_count = 1;
    auto exact = binding(object_id_t{2});
    exact.voices.front().frame_count = 20;
    auto ended = binding(object_id_t{3});
    ended.voices.front().source_start_frame = 40;
    ended.voices.front().frame_count = 1;
    auto middle = binding(object_id_t{4});
    middle.voices.front().source_start_frame = 100;
    middle.voices.front().frame_count = 6;
    auto playable = binding(object_id_t{5});
    playable.voices.front().source_start_frame = 200;
    playable.voices.front().frame_count = 10;
    const std::array objects{first, exact, ended, middle, playable};
    const auto started = bridge.start(chart, objects, {}, 5);
    require_audio_condition(static_cast<bool>(started));
    check_audio_condition(bridge.materialize_until(6).scheduled_events == 3);
    const auto active = transport.snapshot_active_voices(*started.token);
    check_audio_condition(snapshot_has_voice_count(active, 3));
    check_audio_condition(has_scheduled_voice(active, 103, 3, 5));
    check_audio_condition(has_scheduled_voice(active, 205, 5, 5));
    check_audio_condition(has_scheduled_voice(active, 0, 20, 5));

    const auto reseek = bridge.seek(10);
    require_audio_condition(static_cast<bool>(reseek));
    check_audio_condition(bridge.materialize_until(11).scheduled_events == 1);
    const auto after_end = transport.snapshot_active_voices(*reseek.token);
    check_audio_condition(snapshot_has_voice_count(after_end, 1));
    check_audio_condition(has_scheduled_voice(after_end, 5, 15, 10));
    check_audio_condition(bridge.stop().scheduled_voices == 1);
    check_audio_condition(bridge.materialize_until(11).status == preview_bridge_pump_status_t::not_started);
}

TEST_CASE("preview bridge seeks and restarts with fresh session generations",
          "[unit][audio][preview-bridge][seek][generation]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 8);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(10, 1), audio_event(20, 2), audio_event(30, 3)};
    const std::array objects{binding(object_id_t{1}), binding(object_id_t{2}),
                             binding(object_id_t{3})};
    const auto first = bridge.start(chart, objects, {}, 20);
    require_audio_condition(static_cast<bool>(first));
    check_audio_condition(bridge.materialize_until(40).scheduled_events == 3);
    const auto at_first_seek = transport.snapshot_active_voices(*first.token);
    check_audio_condition(has_scheduled_voice(at_first_seek, 10, 54, 20));
    check_audio_condition(has_scheduled_voice(at_first_seek, 0, 64, 20));

    const auto sought = bridge.seek(30);
    require_audio_condition(static_cast<bool>(sought));
    check_audio_condition(sought.token != first.token);
    check_audio_condition(transport.scheduled_voice_count() == 0);
    check_audio_condition(bridge.materialize_until(40).scheduled_events == 3);
    const auto at_second_seek = transport.snapshot_active_voices(*sought.token);
    check_audio_condition(has_scheduled_voice(at_second_seek, 20, 44, 30));
    check_audio_condition(has_scheduled_voice(at_second_seek, 10, 54, 30));
    std::array<float, 2> stale_output{1.0F, 1.0F};
    check_audio_condition(transport.mix(*first.token, 20, stale_output).status ==
          transport_mix_status_t::stale_generation);
    check_audio_condition(stale_output[0] == 0.0F);

    const auto restarted = bridge.restart();
    require_audio_condition(static_cast<bool>(restarted));
    check_audio_condition(restarted.token != sought.token);
    check_audio_condition(bridge.materialize_until(11).scheduled_events == 1);
    check_audio_condition(has_scheduled_voice(transport.snapshot_active_voices(*restarted.token), 0, 64, 10));
    check_audio_condition(bridge.stop().scheduled_voices == 1);
    std::array<float, 2> stopped_output{1.0F, 1.0F};
    check_audio_condition(transport.mix(*restarted.token, 0, stopped_output).status ==
          transport_mix_status_t::stale_generation);
    check_audio_condition(std::ranges::all_of(stopped_output, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("preview bridge Stop invalidates its generation and clears scheduled voices",
          "[unit][audio][preview-bridge][stop][generation]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 4);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1), audio_event(10, 2)};
    const std::array objects{binding(object_id_t{1}), binding(object_id_t{2})};
    const auto started = bridge.start(chart, objects);
    REQUIRE(started);
    REQUIRE(bridge.materialize_until(20).scheduled_events == 2);

    const auto stopped = bridge.stop();
    CHECK(stopped.scheduled_voices == 2);
    CHECK(transport.scheduled_voice_count() == 0);
    CHECK(bridge.materialize_until(20).status == preview_bridge_pump_status_t::not_started);
    std::array<float, 8> output{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    CHECK(transport.mix(*started.token, 0, output).status ==
          transport_mix_status_t::stale_generation);
    CHECK(std::ranges::all_of(output, [](float sample) { return sample == 0.0F; }));

    const auto new_session = bridge.start(chart, objects);
    REQUIRE(new_session);
    CHECK(new_session.token != started.token);
}

TEST_CASE("preview bridge skips missing sources and advances only consumed queue heads",
          "[unit][audio][preview-bridge][queue]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 4);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(10, 1, resource_id_t{2}), audio_event(11, 2, resource_id_t{1})};
    const std::array objects{binding(object_id_t{2})};
    const auto started = bridge.start(chart, objects);
    REQUIRE(started);

    const auto first = bridge.materialize_until(11);
    CHECK(first.status == preview_bridge_pump_status_t::ready);
    CHECK(first.missing_sources == 1);
    CHECK(first.scheduled_events == 0);
    const auto second = bridge.materialize_until(12);
    CHECK(second.status == preview_bridge_pump_status_t::ready);
    CHECK(second.missing_sources == 0);
    CHECK(second.scheduled_events == 1);

    const auto backwards = bridge.materialize_until(11);
    CHECK(backwards.status == preview_bridge_pump_status_t::non_monotonic_window);
}

TEST_CASE("preview bridge leaves an over-capacity same-frame event at the queue head",
          "[unit][audio][preview-bridge][capacity][queue]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 2);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1), audio_event(0, 2), audio_event(10, 3)};
    auto first_voice = binding(object_id_t{1});
    auto second_voice = binding(object_id_t{2});
    auto third_voice = binding(object_id_t{3});
    first_voice.voices.front().frame_count = 2;
    second_voice.voices.front().frame_count = 2;
    third_voice.voices.front().frame_count = 2;
    const std::array objects{first_voice, second_voice, third_voice};
    const auto started = bridge.start(chart, objects);
    REQUIRE(started);

    const auto initial = bridge.materialize_until(11);
    CHECK(initial.status == preview_bridge_pump_status_t::capacity_limited);
    CHECK(initial.scheduled_events == 2);
    CHECK(transport.scheduled_voice_count() == 2);
    std::array<float, 8> output{};
    REQUIRE(transport.mix(*started.token, 0, output).status == transport_mix_status_t::mixed);
    const auto resumed = bridge.materialize_until(11);
    CHECK(resumed.status == preview_bridge_pump_status_t::ready);
    CHECK(resumed.scheduled_events == 1);
    CHECK(transport.scheduled_voice_count() == 1);
}

TEST_CASE("preview bridge schedules every layer of one object atomically",
          "[unit][audio][preview-bridge][layers][capacity]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 2);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1)};
    auto layered = binding(object_id_t{1});
    layered.voices.push_back(
        {.source = {"asset:layer", "revision:1"}, .frame_count = 64, .gain = 0.1F});
    layered.voices.push_back(
        {.source = {"asset:third", "revision:1"}, .frame_count = 64, .gain = 0.1F});
    const std::array objects{layered};
    const auto started = bridge.start(chart, objects);
    REQUIRE(started);

    const auto pumped = bridge.materialize_until(1);
    CHECK(pumped.status == preview_bridge_pump_status_t::capacity_limited);
    CHECK(pumped.scheduled_events == 0);
    CHECK(transport.scheduled_voice_count() == 0);
    CHECK(bridge.materialize_until(1).status == preview_bridge_pump_status_t::capacity_limited);
}

TEST_CASE("preview bridge rejects duplicate and unbounded object-layer bindings",
          "[unit][audio][preview-bridge][layers][validation]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 8);
    auto chart = schedule();
    chart.playable = {audio_event(0, 1)};

    const std::array duplicates{binding(object_id_t{1}), binding(object_id_t{1})};
    preview_transport_bridge_t bridge{transport};
    CHECK(bridge.start(chart, duplicates).status ==
          preview_bridge_start_status_t::duplicate_source_binding);

    auto layered = binding(object_id_t{1});
    layered.voices.push_back({.source = {"asset:layer", "revision:1"}, .frame_count = 64});
    const std::array objects{layered};
    preview_transport_bridge_t bounded{transport, 1, 1, 1};
    CHECK(bounded.start(chart, objects).status ==
          preview_bridge_start_status_t::source_binding_capacity_exceeded);
    CHECK(transport.scheduled_voice_count() == 0);
}

namespace {
void require_metronome(bool condition) {
    REQUIRE(condition);
}

auto four_measure_clicks() -> preview_schedule_t {
    auto chart = schedule();
    for (std::uint64_t beat = 0; beat < 16; ++beat) {
        chart.metronome.push_back({.sample_frame = beat * 100,
                                  .measure = measure_id_t{beat / 4 + 1},
                                  .pulse = static_cast<std::uint32_t>(beat % 4),
                                  .accent = metronome_accent_t::regular});
    }
    return chart;
}
}

TEST_CASE("preview bridge disabled metronome skips four measures and enables at next beat",
          "[unit][audio][preview-bridge][metronome]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = four_measure_clicks();
    const std::array clicks{click_binding(metronome_accent_t::regular)};
    bridge.set_metronome_enabled(false, 0);
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.materialize_until(1'600).scheduled_events == 0);
    require_metronome(transport.scheduled_voice_count() == 0);
    bridge.set_metronome_enabled(true, 450);
    require_metronome(bridge.materialize_until(1'600).scheduled_events == 11);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(static_cast<bool>(voices));
    require_metronome(voices.voices.front().timeline_start_frame == 500);
    require_metronome(voices.voices.back().timeline_start_frame == 1'500);
}

TEST_CASE("preview bridge volume scales chart and assist clicks through one click control",
          "[unit][audio][preview-bridge][metronome-volume]") {
    decoded_block_cache_t cache;
    transport_t transport{cache, 8};
    preview_transport_bridge_t bridge{transport};
    const audio_source_identity_t source{"asset:click", "revision:volume"};
    const std::vector<float> pcm(128 * output_channel_count, 0.25F);
    REQUIRE(cache.store({.source = source, .block_index = 0}, 128, pcm) == block_store_status_t::stored);
    auto chart = schedule();
    chart.metronome = {{.sample_frame = 0, .accent = metronome_accent_t::regular}};
    const std::array clicks{
        preview_metronome_source_binding_t{
            .accent = metronome_accent_t::regular,
            .voice = {.source = source, .frame_count = 128, .gain = 1.0F}},
        preview_metronome_source_binding_t{
            .accent = metronome_accent_t::measure,
            .voice = {.source = source, .frame_count = 128, .gain = 1.0F}},
    };
    REQUIRE(bridge.set_metronome_volume(50));
    CHECK(bridge.metronome_volume() == 50);
    CHECK_FALSE(bridge.set_metronome_volume(101));
    const auto started = bridge.start(chart, {}, clicks);
    REQUIRE(started);
    REQUIRE(bridge.materialize_until(32).scheduled_events == 1);
    std::array<float, 16> chart_mix{};
    REQUIRE(transport.mix(*started.token, 0, chart_mix).status == transport_mix_status_t::mixed);
    CHECK(chart_mix.front() == 0.125F);

    REQUIRE(bridge.set_assist_grid(true, assist_grid(32.0), 32));
    REQUIRE(bridge.materialize_until(96, 32).scheduled_events == 1);
    std::array<float, 16> assist_mix{};
    REQUIRE(transport.mix(*started.token, 32, assist_mix).status == transport_mix_status_t::mixed);
    CHECK(assist_mix.front() == 0.125F);

    const auto sought = bridge.seek(0);
    REQUIRE(sought);
    CHECK(bridge.metronome_volume() == 50);
}

TEST_CASE("preview bridge disabling retires queued clicks while preserving music",
          "[unit][audio][preview-bridge][metronome]") {
    decoded_block_cache_t cache;
    const auto pcm = std::vector<float>(64 * output_channel_count, 0.25F);
    require_metronome(cache.store({.source = {"asset:key", "revision:1"}}, 64, pcm) ==
                       block_store_status_t::stored);
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = four_measure_clicks();
    const std::array clicks{click_binding(metronome_accent_t::regular)};
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.materialize_until(128).scheduled_events == 2);
    require_metronome(transport.schedule_voice(*started.token,
        {.id = 999, .kind = voice_kind_t::music,
         .source = {"asset:key", "revision:1"}, .frame_count = 64}) ==
        voice_schedule_status_t::scheduled);
    bridge.set_metronome_enabled(false, 0);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(static_cast<bool>(voices));
    require_metronome(voices.voices.size() == 1);
    require_metronome(voices.voices.front().kind == voice_kind_t::music);
    std::array<float, 32 * output_channel_count> output{};
    const auto mixed = transport.mix(*started.token, 0, output);
    require_metronome(mixed.status == transport_mix_status_t::mixed);
    require_metronome(mixed.voices_mixed == 1 && mixed.missing_blocks == 0);
    require_metronome(output.front() == 0.25F);
    bridge.set_metronome_enabled(true, 32);
    require_metronome(bridge.materialize_until(128).scheduled_events == 1);
    const auto resumed = transport.snapshot_active_voices(*started.token);
    require_metronome(resumed.voices.size() == 2);
    require_metronome(std::any_of(resumed.voices.begin(), resumed.voices.end(), [](const auto& voice) {
        return voice.kind == voice_kind_t::click_preview && voice.timeline_start_frame == 100;
    }));
}

namespace {
auto assist_bindings() -> std::array<preview_metronome_source_binding_t, 2> {
    return {{{.accent = metronome_accent_t::regular,
              .voice = {.source = {"assist:regular", "1"}, .frame_count = 64}},
             {.accent = metronome_accent_t::measure,
              .voice = {.source = {"assist:downbeat", "1"}, .frame_count = 64}}}};
}
auto assist_grid(double phase = 0) -> preview_assist_click_grid_t {
    return {.bpm = 120, .phase_source_frame = phase, .beats_per_measure = 3};
}
}

TEST_CASE("preview bridge assist grid predicts exact 120 BPM frames and meter accents past its horizon",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.set_assist_grid(true, assist_grid(), 0));
    require_metronome(bridge.materialize_until(44'100, 0).scheduled_events == 1);
    require_metronome(bridge.materialize_until(44'100, 20'000).scheduled_events == 1);
    require_metronome(bridge.materialize_until(70'000, 60'000).scheduled_events == 1);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(static_cast<bool>(voices) && voices.voices.size() == 3);
    require_metronome(voices.voices[0].timeline_start_frame == 0);
    require_metronome(voices.voices[0].source.asset_identity == "assist:downbeat");
    require_metronome(voices.voices[1].timeline_start_frame == 22'050);
    require_metronome(voices.voices[1].source.asset_identity == "assist:regular");
    require_metronome(voices.voices[2].timeline_start_frame == 66'150);
    require_metronome(voices.voices[2].source.asset_identity == "assist:downbeat");
}

TEST_CASE("preview bridge assist source phase maps through music origin and rounds fractional frames",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.music = preview_schedule_t::music_event_t::from(resource_id_t{1}, 100'000);
    chart.music->sample_frame = 500;
    chart.music->source_start_frame = 2'000;
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.set_assist_grid(true, assist_grid(2'000.25), 450));
    require_metronome(bridge.materialize_until(10'000, 450).scheduled_events == 1);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(voices.voices.size() == 1 && voices.voices.front().timeline_start_frame == 500);
    require_metronome(bridge.set_assist_grid(true, assist_grid(2'099.7), 600));
    require_metronome(bridge.materialize_until(10'000, 600).scheduled_events == 1);
    const auto rounded = transport.snapshot_active_voices(*started.token);
    require_metronome(rounded.voices.back().timeline_start_frame == 600);
}

TEST_CASE("preview bridge assist estimate updates keep handed voices and replace blocked predictions",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    require_metronome(cache.store({.source = {"asset:key", "revision:1"}}, 64,
        std::vector<float>(64 * output_channel_count, 0.25F)) == block_store_status_t::stored);
    auto transport = transport_for(cache, 1);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1)};
    const std::array objects{binding()};
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, objects, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.set_assist_grid(true, assist_grid(100), 0));
    require_metronome(bridge.materialize_until(10'000, 0).status == preview_bridge_pump_status_t::capacity_limited);
    require_metronome(bridge.set_assist_grid(true, assist_grid(200), 0));
    std::array<float, 64 * output_channel_count> output{};
    require_metronome(transport.mix(*started.token, 0, output).status == transport_mix_status_t::mixed);
    require_metronome(bridge.materialize_until(10'000, 64).scheduled_events == 1);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(voices.voices.size() == 1 && voices.voices.front().timeline_start_frame == 200);
    require_metronome(bridge.set_assist_grid(true, assist_grid(300), 64));
    require_metronome(bridge.materialize_until(10'000, 64).status == preview_bridge_pump_status_t::capacity_limited);
    const auto retained = transport.snapshot_active_voices(*started.token);
    require_metronome(retained.voices.front() == voices.voices.front());
}

TEST_CASE("preview bridge assist mode silences chart grid without estimate and obeys metronome toggle",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.metronome = {{.sample_frame = 0, .accent = metronome_accent_t::regular},
                       {.sample_frame = 100, .accent = metronome_accent_t::regular}};
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.materialize_until(10'000).scheduled_events == 2);
    require_metronome(bridge.set_assist_grid(true, std::nullopt, 50));
    require_metronome(transport.scheduled_voice_count() == 0);
    require_metronome(bridge.materialize_until(10'000, 50).scheduled_events == 0);
    require_metronome(bridge.set_assist_grid(true, assist_grid(150), 100));
    bridge.set_metronome_enabled(false, 100);
    require_metronome(bridge.materialize_until(10'000, 100).scheduled_events == 0);
    bridge.set_metronome_enabled(true, 100);
    require_metronome(bridge.materialize_until(10'000, 100).scheduled_events == 1);
    require_metronome(bridge.set_assist_grid(false, std::nullopt, 100));
    require_metronome(transport.scheduled_voice_count() == 0);
    require_metronome(bridge.materialize_until(10'000, 100).scheduled_events == 1);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(voices.voices.front().timeline_start_frame == 100);
}

TEST_CASE("preview bridge assist slow rate bounds output horizon with an exclusive source endpoint",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    auto grid = assist_grid(882);
    grid.playback_rate = rational_t{1, 10};
    require_metronome(bridge.set_assist_grid(true, grid, 0));
    require_metronome(bridge.materialize_until(10'000, 0).scheduled_events == 0);
    require_metronome(transport.scheduled_voice_count() == 0);
    require_metronome(bridge.materialize_until(10'000, 1).scheduled_events == 1);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(voices.voices.size() == 1 && voices.voices.front().timeline_start_frame == 882);
}

TEST_CASE("preview bridge assist rejects invalid estimates without changing handed or pending clicks",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.set_assist_grid(true, assist_grid(100), 0));
    require_metronome(bridge.materialize_until(10'000, 0).scheduled_events == 1);
    const auto before = transport.snapshot_active_voices(*started.token);
    auto invalid = assist_grid(200);
    invalid.bpm = 0;
    require_metronome(!bridge.set_assist_grid(true, invalid, 0));
    const auto after = transport.snapshot_active_voices(*started.token);
    require_metronome(after.voices == before.voices);
    require_metronome(bridge.materialize_until(30'000, 20'000).scheduled_events == 1);
    const auto continued = transport.snapshot_active_voices(*started.token);
    require_metronome(continued.voices.back().timeline_start_frame == 22'150);
}

TEST_CASE("preview bridge assist excludes music EOF and cannot extend completed playback",
          "[unit][audio][preview-bridge][assist]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.music = preview_schedule_t::music_event_t::from(resource_id_t{1}, 22'050);
    const auto clicks = assist_bindings();
    const auto started = bridge.start(chart, {}, clicks);
    require_metronome(static_cast<bool>(started));
    require_metronome(bridge.set_assist_grid(true, assist_grid(), 0));
    require_metronome(bridge.materialize_until(30'000, 0).scheduled_events == 1);
    require_metronome(bridge.materialize_until(30'000, 20'000).scheduled_events == 0);
    require_metronome(bridge.exhausted());
    require_metronome(bridge.materialize_until(40'000, 22'050).scheduled_events == 0);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_metronome(voices.voices.size() == 1 && voices.voices.front().timeline_start_frame == 0);
}

TEST_CASE("Layer assist queue toggles independently and seeks without replacing other voices",
          "[unit][audio][preview-bridge][layer-assist]") {
    decoded_block_cache_t cache;
    transport_t transport{cache, 16};
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1)};
    chart.metronome = {{.sample_frame = 10}};
    const auto key = binding();
    const auto click = click_binding(metronome_accent_t::regular);
    require_audio_condition(bridge.set_layer_tick_frames(std::array<std::uint64_t, 2>{10, 20}, 0));
    bridge.set_metronome_enabled(false, 0);
    const auto started = bridge.start(chart, std::span{&key, 1}, std::span{&click, 1});
    require_audio_condition(static_cast<bool>(started));
    check_audio_condition(bridge.materialize_until(64).scheduled_events == 3);
    auto active = transport.snapshot_active_voices(*started.token);
    require_audio_condition(static_cast<bool>(active));
    check_audio_condition(active.voices.size() == 3);
    require_audio_condition(bridge.set_layer_tick_frames({}, 0));
    active = transport.snapshot_active_voices(*started.token);
    check_audio_condition(active.voices.size() == 1);
    check_audio_condition(transport.is_current(*started.token));
    require_audio_condition(bridge.set_layer_tick_frames(std::array<std::uint64_t, 2>{10, 20}, 15));
    check_audio_condition(bridge.materialize_until(64, 15).scheduled_events == 1);
    check_audio_condition(!bridge.set_layer_tick_frames(std::array<std::uint64_t, 2>{20, 10}, 15));
    check_audio_condition(!bridge.set_layer_tick_frames(std::array<std::uint64_t, 2>{20, 20}, 15));
    const auto sought = bridge.seek(10);
    require_audio_condition(static_cast<bool>(sought));
    check_audio_condition(bridge.materialize_until(64, 10).scheduled_events == 3);
    active = transport.snapshot_active_voices(*sought.token);
    check_audio_condition(std::ranges::count(active.voices, voice_kind_t::layer_assist,
                                            &transport_voice_t::kind) == 2);
}

TEST_CASE("Preview seek preserves every layer's stable BGM mix group",
          "[audio][preview-bridge][bgm-mix]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.bgm = {audio_event(0, 1)};
    auto layered = binding();
    layered.voices.front().mix_group = 77;
    layered.voices.push_back(layered.voices.front());
    const std::array objects{layered};
    const auto started = bridge.start(chart, objects, {}, 5);
    require_audio_condition(static_cast<bool>(started));
    check_audio_condition(bridge.materialize_until(6).scheduled_events == 1);
    const auto voices = transport.snapshot_active_voices(*started.token);
    require_audio_condition(static_cast<bool>(voices));
    require_audio_condition(voices.voices.size() == 2);
    for (const auto& voice : voices.voices) {
        check_audio_condition(voice.mix_group == 77);
        check_audio_condition(voice.source_start_frame == 5);
    }
}
