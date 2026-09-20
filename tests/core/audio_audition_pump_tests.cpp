#include <wormedit/audio/audition_pump.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry, std::string_view name)
    -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.register_source({
        .asset = wormedit::asset_id_t{1},
        .path = fixture_path(name),
        .source = {"fixture:" + std::string(name), "generated-v1:" + std::string(name)},
    });
    REQUIRE(added);
    return *added.binding;
}

auto all_zero(const std::vector<float>& samples) -> bool {
    return std::ranges::all_of(samples, [](float sample) { return sample == 0.0F; });
}

} // namespace

TEST_CASE("audition lookahead fills exact blocks before the headless mixer consumes them",
          "[unit][audio][audition-pump][mix][fixture]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "polyphonic-chord.ogg");
    REQUIRE(binding.output_frame_count > maximum_read_frames + 512);
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    const audition_clip_t clip{binding.source, maximum_read_frames - 128, 640, 1.0F};
    const auto started = bridge.start(std::span{&clip, 1});
    REQUIRE(started);

    const auto pumped = pump_audition_lookahead(sources, transport, *started.token,
                                                std::span{&clip, 1}, 0, {.lookahead_frames = 512});
    REQUIRE(pumped);
    CHECK(pumped.status == audition_pump_status_t::ready);
    CHECK(pumped.requested_blocks == 2);
    CHECK(pumped.decoded_blocks == 2);
    CHECK(pumped.cached_blocks == 0);

    std::vector<float> output(512 * output_channel_count);
    const auto mixed = transport.mix(*started.token, 0, output);
    CHECK(mixed.status == transport_mix_status_t::mixed);
    CHECK(mixed.voices_mixed == 1);
    CHECK(mixed.missing_blocks == 0);
    CHECK_FALSE(all_zero(output));
}

TEST_CASE("audition lookahead plans capacity atomically and reuses cached blocks",
          "[unit][audio][audition-pump][bounds][cache]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "polyphonic-chord.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    const audition_clip_t clip{binding.source, maximum_read_frames - 128, 640, 1.0F};
    const std::array layered{clip, clip};
    const auto started = bridge.start(layered);
    REQUIRE(started);

    const auto bounded = pump_audition_lookahead(sources, transport, *started.token, layered, 0,
                                                 {.lookahead_frames = 512, .maximum_blocks = 1});
    CHECK_FALSE(bounded);
    CHECK(bounded.status == audition_pump_status_t::capacity_exceeded);
    CHECK(cache.stats().entries == 0);

    const auto first = pump_audition_lookahead(sources, transport, *started.token, layered, 0,
                                               {.lookahead_frames = 512, .maximum_blocks = 2});
    REQUIRE(first);
    CHECK(first.requested_blocks == 2);
    CHECK(first.decoded_blocks == 2);
    const auto repeated = pump_audition_lookahead(sources, transport, *started.token, layered, 0,
                                                  {.lookahead_frames = 512, .maximum_blocks = 2});
    REQUIRE(repeated);
    CHECK(repeated.decoded_blocks == 0);
    CHECK(repeated.cached_blocks == 2);
}

TEST_CASE("audition lookahead rejects stale unknown and invalid requests without decoding",
          "[unit][audio][audition-pump][generation][validation]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "keysound-g4.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    const audition_clip_t clip{binding.source, 0, binding.output_frame_count, 1.0F};
    const auto started = bridge.start(std::span{&clip, 1});
    REQUIRE(started);

    const auto complete = pump_audition_lookahead(sources, transport, *started.token,
                                                  std::span{&clip, 1}, clip.frame_count);
    REQUIRE(complete);
    CHECK(complete.status == audition_pump_status_t::complete);
    CHECK(complete.requested_blocks == 0);

    const audition_clip_t unknown{{"fixture:unknown", "missing:v1"}, 0, 512, 1.0F};
    const auto unavailable =
        pump_audition_lookahead(sources, transport, *started.token, std::span{&unknown, 1}, 0);
    CHECK_FALSE(unavailable);
    CHECK(unavailable.status == audition_pump_status_t::source_unavailable);
    REQUIRE(unavailable.failure.has_value());
    CHECK(unavailable.failure->status == block_decode_status_t::invalid_key);

    const auto invalid =
        pump_audition_lookahead(sources, transport, *started.token, std::span{&clip, 1}, 0,
                                {.lookahead_frames = maximum_read_frames + 1});
    CHECK_FALSE(invalid);
    CHECK(invalid.status == audition_pump_status_t::invalid_request);
    CHECK(cache.stats().entries == 0);

    const std::vector too_many(maximum_audition_pump_clips + 1, clip);
    const auto excessive = pump_audition_lookahead(sources, transport, *started.token, too_many, 0);
    CHECK_FALSE(excessive);
    CHECK(excessive.status == audition_pump_status_t::invalid_request);

    static_cast<void>(bridge.stop());
    const auto stale =
        pump_audition_lookahead(sources, transport, *started.token, std::span{&clip, 1}, 0);
    CHECK_FALSE(stale);
    CHECK(stale.status == audition_pump_status_t::stale_generation);
}
