#include <wormedit/audio/preview_bridge.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {
using namespace wormedit;
using namespace wormedit::audio;
void check_spliced_pcm(const std::array<float, 16>& output) {
    CHECK(output[0] == Catch::Approx(0.1F));
    CHECK(output[6] == Catch::Approx(0.1F));
    CHECK(output[8] == Catch::Approx(0.3F));
    CHECK(output[14] == Catch::Approx(0.3F));
}
}
TEST_CASE("Preview callback traverses cut music segments on one continuous output clock", "[audio][export-cut][bridge]") {
    decoded_block_cache_t cache;
    std::array<float, 24> pcm;
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = i < 8 ? 0.1F : i < 16 ? 0.9F : 0.3F;
    REQUIRE(cache.store({.source = {"cut-song", "1"}}, 12, pcm));
    transport_t transport{cache, 8};
    preview_transport_bridge_t bridge{transport};
    preview_schedule_t schedule{.sample_rate = output_sample_rate, .music_segments = {
        {.resource = resource_id_t{1}, .sample_frame = 0, .source_start_frame = 0, .frame_count = 4},
        {.resource = resource_id_t{1}, .sample_frame = 4, .source_start_frame = 8, .frame_count = 4}}};
    const std::array bindings{preview_object_source_binding_t{.object = preview_music_object,
        .voices = {{.source = {"cut-song", "1"}, .frame_count = 12}}}};
    const auto started = bridge.start_preview(schedule, bindings);
    REQUIRE(started);
    REQUIRE(bridge.materialize_preview(8).scheduled_events == 2);
    std::array<float, 16> output{};
    REQUIRE(transport.mix_frames(*started, 0, output).status == std::monostate{});
    check_spliced_pcm(output);
}

TEST_CASE("Assist lookahead crosses a music cut with the retained source phase", "[audio][export-cut][bridge][assist]") {
    decoded_block_cache_t cache;
    transport_t transport{cache, 8};
    preview_transport_bridge_t bridge{transport};
    preview_schedule_t schedule{.sample_rate = output_sample_rate, .music_segments = {
        {.resource = resource_id_t{1}, .sample_frame = 0, .source_start_frame = 0, .frame_count = 4},
        {.resource = resource_id_t{1}, .sample_frame = 4, .source_start_frame = 8, .frame_count = 4}}};
    const std::array clicks{
        preview_metronome_source_binding_t{.accent = metronome_accent_t::regular,
            .voice = {.source = {"click", "1"}, .frame_count = 1}},
        preview_metronome_source_binding_t{.accent = metronome_accent_t::measure,
            .voice = {.source = {"click", "1"}, .frame_count = 1}}};
    const auto started = bridge.start_preview(schedule, {}, clicks);
    REQUIRE(started);
    REQUIRE(bridge.set_assist_grid(true, preview_assist_click_grid_t{.bpm = 264600, .phase_source_frame = 0, .beats_per_measure = 4}, 0));
    CHECK(bridge.materialize_preview(8, 0).scheduled_events == 2);
    const auto voices = transport.snapshot_voices(*started);
    REQUIRE(voices);
    REQUIRE(voices->size() == 2);
    CHECK(voices->back().timeline_start_frame == 6);
}
