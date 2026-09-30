#include <wormedit/audio/preview_pump.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry, std::string_view name,
                      std::uint64_t asset) -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.register_source({
        .asset = wormedit::asset_id_t{asset},
        .path = fixture_path(name),
        .source = {"fixture:" + std::string(name), "preview-v1:" + std::string(name)},
    });
    REQUIRE(added);
    return *added.binding;
}

} // namespace

TEST_CASE("preview lookahead decodes only active timeline overlaps and reuses shared blocks",
          "[unit][audio][preview-pump][timeline][cache]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "polyphonic-chord.ogg", 1);
    decoded_block_cache_t cache;
    transport_t transport{cache, 8};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    const std::array voices{
        transport_voice_t{.id = 1,
                          .source = binding.source,
                          .timeline_start_frame = 100,
                          .source_start_frame = maximum_read_frames - 100,
                          .frame_count = 800},
        transport_voice_t{.id = 2,
                          .source = binding.source,
                          .timeline_start_frame = 200,
                          .source_start_frame = maximum_read_frames,
                          .frame_count = 200},
        transport_voice_t{
            .id = 3, .source = binding.source, .timeline_start_frame = 2'000, .frame_count = 200},
    };
    REQUIRE(transport.schedule_voices(*token, voices) == voice_schedule_status_t::scheduled);

    const auto first =
        pump_preview_lookahead(sources, transport, *token, 0, {.lookahead_frames = 512});
    REQUIRE(first);
    CHECK(first.active_voices == 3);
    CHECK(first.overlapping_voices == 2);
    CHECK(first.requested_blocks == 2);
    CHECK(first.decoded_blocks == 2);

    const auto repeated =
        pump_preview_lookahead(sources, transport, *token, 0, {.lookahead_frames = 512});
    REQUIRE(repeated);
    CHECK(repeated.decoded_blocks == 0);
    CHECK(repeated.cached_blocks == 2);
}

TEST_CASE("preview lookahead rejects bounds and stale generations before cache publication",
          "[unit][audio][preview-pump][bounds][generation]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto first_binding = register_fixture(sources, "polyphonic-chord.ogg", 1);
    const auto second_binding = register_fixture(sources, "keysound-g4.ogg", 2);
    decoded_block_cache_t cache;
    transport_t transport{cache, 4};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    const std::array voices{
        transport_voice_t{.id = 1,
                          .source = first_binding.source,
                          .source_start_frame = maximum_read_frames - 64,
                          .frame_count = 128},
        transport_voice_t{.id = 2, .source = second_binding.source, .frame_count = 128},
    };
    REQUIRE(transport.schedule_voices(*token, voices) == voice_schedule_status_t::scheduled);

    const auto voice_bound = pump_preview_lookahead(sources, transport, *token, 0,
                                                    {.lookahead_frames = 128, .maximum_voices = 1});
    CHECK_FALSE(voice_bound);
    CHECK(voice_bound.status == preview_pump_status_t::capacity_exceeded);
    CHECK(cache.stats().entries == 0);

    const auto block_bound =
        pump_preview_lookahead(sources, transport, *token, 0,
                               {.lookahead_frames = 128, .maximum_voices = 2, .maximum_blocks = 1});
    CHECK_FALSE(block_bound);
    CHECK(block_bound.status == preview_pump_status_t::capacity_exceeded);
    CHECK(cache.stats().entries == 0);

    static_cast<void>(transport.stop());
    const auto stale = pump_preview_lookahead(sources, transport, *token, 0);
    CHECK_FALSE(stale);
    CHECK(stale.status == preview_pump_status_t::stale_generation);
}

namespace {

using namespace wormedit::audio;

constexpr std::size_t overlapping_source_count = 24;

auto schedule_overlapping_generated_sources(decoded_source_registry_t& sources,
                                             transport_t& transport, session_token_t token)
    -> voice_schedule_status_t {
    std::vector<transport_voice_t> voices;
    voices.reserve(overlapping_source_count);
    const std::vector<float> samples(maximum_read_frames * output_channel_count, 0.125F);
    for (std::size_t index = 0; index < overlapping_source_count; ++index) {
        const auto identity = "generated:preview-overlap:" + std::to_string(index);
        const auto generated = sources.register_generated_source({
            .source = {identity, "preview-overlap-v1"},
            .interleaved_stereo = samples,
        });
        if (!generated) {
            return voice_schedule_status_t::invalid_voice;
        }
        voices.push_back({
            .id = index + 1U,
            .source = generated.binding->source,
            .frame_count = maximum_read_frames,
        });
    }
    return transport.schedule_voices(token, voices);
}

} // namespace

TEST_CASE("preview cache limits hold two maximum-size pump plans globally and per asset",
          "[unit][audio][preview-pump][cache]") {
    using namespace wormedit::audio;
    constexpr auto maximum_plan_frames = maximum_preview_pump_blocks * maximum_read_frames;
    constexpr auto two_plan_frames = 2U * maximum_plan_frames;
    constexpr auto two_plan_bytes = two_plan_frames * output_channel_count * sizeof(float);
    const auto limits = preview_block_cache_limits();

    CHECK(limits.maximum_frames == two_plan_frames);
    CHECK(limits.maximum_bytes == two_plan_bytes);
    CHECK(limits.maximum_frames_per_asset == two_plan_frames);
    CHECK(limits.maximum_bytes_per_asset == two_plan_bytes);
}

TEST_CASE("preview pump retains many overlapping sources through chunked transport rendering",
          "[unit][audio][preview-pump][transport][cache]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache, overlapping_source_count};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    REQUIRE(schedule_overlapping_generated_sources(sources, transport, *token) ==
            voice_schedule_status_t::scheduled);

    const auto pumped = pump_preview_lookahead(
        sources, transport, *token, 0, {.lookahead_frames = maximum_read_frames});
    REQUIRE(pumped.status == preview_pump_status_t::ready);
    CHECK(pumped.requested_blocks == overlapping_source_count);
    CHECK(pumped.decoded_blocks == overlapping_source_count);

    constexpr std::size_t render_chunk_frames = 4'096;
    static_assert(maximum_read_frames % render_chunk_frames == 0);
    std::vector<float> chunk(render_chunk_frames * output_channel_count);
    for (std::size_t frame = 0; frame < maximum_read_frames; frame += render_chunk_frames) {
        const auto mixed = transport.mix(*token, frame, chunk);
        INFO("render chunk starts at frame " << frame);
        CHECK(mixed.status == transport_mix_status_t::mixed);
        CHECK(mixed.missing_blocks == 0);
    }
}

TEST_CASE("default preview cache rejects a pump plan larger than its capacity",
          "[unit][audio][preview-pump][transport][cache]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t cache;
    transport_t transport{cache, overlapping_source_count};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    REQUIRE(schedule_overlapping_generated_sources(sources, transport, *token) ==
            voice_schedule_status_t::scheduled);

    const auto pumped = pump_preview_lookahead(
        sources, transport, *token, 0, {.lookahead_frames = maximum_read_frames});
    CHECK(pumped.status == preview_pump_status_t::capacity_exceeded);
    CHECK(pumped.requested_blocks == overlapping_source_count);
    CHECK(cache.stats().entries == 0);
}
