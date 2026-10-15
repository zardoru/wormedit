#include <wormedit/audio/audition_bridge.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <limits>

namespace {

using namespace wormedit::audio;

auto source(const char* name) -> audio_source_identity_t {
    return {name, "revision:1"};
}

auto clip(const char* name, std::uint64_t start = 0, std::uint64_t count = 4, float gain = 1.0F)
    -> audition_clip_t {
    return {
        .source = source(name), .source_start_frame = start, .frame_count = count, .gain = gain};
}

} // namespace

TEST_CASE("audition bridge mixes exact source ranges and simultaneous layers",
          "[unit][audio][audition-bridge][polyphony]") {
    decoded_block_cache_t cache;
    std::array<float, 16> first{};
    std::array<float, 16> second{};
    std::fill(first.begin() + 8, first.end(), 0.2F);
    std::fill(second.begin() + 8, second.end(), 0.3F);
    REQUIRE(cache.store({.source = source("first")}, 8, first) == block_store_status_t::stored);
    REQUIRE(cache.store({.source = source("second")}, 8, second) == block_store_status_t::stored);
    transport_t transport{cache, 4};
    audition_transport_bridge_t bridge{transport};
    const std::array clips{clip("first", 4, 4), clip("second", 4, 4)};

    const auto started = bridge.start(clips);
    REQUIRE(started);
    CHECK(started.scheduled_layers == 2);
    std::array<float, 8> output{};
    const auto mixed = transport.mix_frames(*started.token, 0, output);
    CHECK(mixed.value().status == transport_mix_status_t::mixed);
    CHECK(mixed.value().voices_mixed == 2);
    CHECK(mixed.value().missing_blocks == 0);
    for (const auto sample : output) {
        CHECK(sample == Catch::Approx(0.5F));
    }
}

TEST_CASE("audition bridge replaces generations and rejects invalid input before replacement",
          "[unit][audio][audition-bridge][generation]") {
    decoded_block_cache_t cache;
    transport_t transport{cache, 4};
    audition_transport_bridge_t bridge{transport};
    const std::array initial{clip("first")};
    const auto first = bridge.start(initial);
    REQUIRE(first);

    const std::array invalid{clip("bad", std::numeric_limits<std::uint64_t>::max(), 2)};
    CHECK(bridge.start(invalid).status == audition_start_status_t::invalid_clip);
    CHECK(bridge.session_token() == first.token);
    CHECK(transport.is_current(*first.token));

    const std::array replacement{clip("second")};
    const auto second = bridge.start(replacement);
    REQUIRE(second);
    CHECK(second.token != first.token);
    CHECK_FALSE(transport.is_current(*first.token));
    CHECK(transport.is_current(*second.token));
}

TEST_CASE("audition bridge bounds layers and atomically rejects or stops sessions",
          "[unit][audio][audition-bridge][capacity][stop]") {
    decoded_block_cache_t cache;
    transport_t transport{cache, 1};
    audition_transport_bridge_t bridge{transport, 2};
    const std::array too_many{clip("one"), clip("two"), clip("three")};
    CHECK(bridge.start(too_many).status == audition_start_status_t::too_many_layers);
    CHECK_FALSE(bridge.session_token().has_value());

    const std::array over_transport_capacity{clip("one"), clip("two")};
    const auto rejected = bridge.start(over_transport_capacity);
    CHECK(rejected.status == audition_start_status_t::transport_capacity_exceeded);
    CHECK(rejected.scheduled_layers == 0);
    CHECK_FALSE(bridge.session_token().has_value());
    CHECK(transport.scheduled_voice_count() == 0);

    const std::array accepted{clip("one")};
    const auto started = bridge.start(accepted);
    REQUIRE(started);
    const auto stopped = bridge.stop();
    CHECK(stopped.scheduled_voices == 1);
    CHECK(stopped.voices_by_kind[static_cast<std::size_t>(voice_kind_t::click_preview)] == 1);
    CHECK_FALSE(bridge.session_token().has_value());
    CHECK_FALSE(transport.is_current(*started.token));
}
