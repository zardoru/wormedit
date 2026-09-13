#include <wormedit/audio/preview_bridge.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using namespace wormedit;
using namespace wormedit::audio;

auto binding(resource_id_t resource = resource_id_t{1}) -> preview_resource_source_binding_t {
    return {.resource = resource,
            .voice = {.source = {"asset:key", "revision:1"}, .frame_count = 64, .gain = 0.1F}};
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

auto transport_for(decoded_block_cache_t& cache, std::size_t capacity = 16) -> transport_t {
    return transport_t{cache, capacity};
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
    auto short_voice = binding();
    short_voice.voice.frame_count = 2;
    const std::array resources{short_voice};
    const auto started = bridge.start(chart, resources);
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
    const std::array resources{binding()};
    const std::array clicks{click_binding(metronome_accent_t::measure)};
    const auto started = bridge.start(chart, resources, clicks);
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

TEST_CASE("preview bridge seeks and restarts with fresh session generations",
          "[unit][audio][preview-bridge][seek][generation]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 8);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(10, 1), audio_event(20, 2), audio_event(30, 3)};
    const std::array resources{binding()};
    const auto first = bridge.start(chart, resources, {}, 20);
    REQUIRE(first);
    CHECK(bridge.materialize_until(40).scheduled_events == 2);

    const auto sought = bridge.seek(30);
    REQUIRE(sought);
    CHECK(sought.token != first.token);
    CHECK(transport.scheduled_voice_count() == 0);
    CHECK(bridge.materialize_until(40).scheduled_events == 1);
    std::array<float, 2> stale_output{1.0F, 1.0F};
    CHECK(transport.mix(*first.token, 20, stale_output).status ==
          transport_mix_status_t::stale_generation);
    CHECK(stale_output[0] == 0.0F);

    const auto restarted = bridge.restart();
    REQUIRE(restarted);
    CHECK(restarted.token != sought.token);
    CHECK(bridge.materialize_until(11).scheduled_events == 1);
}

TEST_CASE("preview bridge Stop invalidates its generation and clears scheduled voices",
          "[unit][audio][preview-bridge][stop][generation]") {
    decoded_block_cache_t cache;
    auto transport = transport_for(cache, 4);
    preview_transport_bridge_t bridge{transport};
    auto chart = schedule();
    chart.playable = {audio_event(0, 1), audio_event(10, 2)};
    const std::array resources{binding()};
    const auto started = bridge.start(chart, resources);
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

    const auto new_session = bridge.start(chart, resources);
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
    const std::array resources{binding()};
    const auto started = bridge.start(chart, resources);
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
    auto short_voice = binding();
    short_voice.voice.frame_count = 2;
    const std::array resources{short_voice};
    const auto started = bridge.start(chart, resources);
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
