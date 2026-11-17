#include <wormedit/audio/scrub_reader.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <source_location>

#include <cmath>
#include <vector>

using namespace wormedit;
using namespace wormedit::audio;

namespace {

constexpr double ramp_scale = 1.0 / 200'000.0;
constexpr std::size_t block_count = 3;

auto source_named(const char* name) -> audio_source_identity_t { return {name, name}; }

// Stores `blocks` blocks of a linear ramp: sample value = source frame / 200000.
void store_ramp(decoded_block_cache_t& cache, const audio_source_identity_t& source,
                std::size_t blocks, std::size_t skip_block = ~std::size_t{}) {
    for (std::size_t block = 0; block < blocks; ++block) {
        if (block == skip_block) continue;
        std::vector<float> pcm(maximum_read_frames * output_channel_count);
        for (std::size_t frame = 0; frame < maximum_read_frames; ++frame) {
            const auto value = static_cast<float>(static_cast<double>(block * maximum_read_frames + frame) * ramp_scale);
            pcm[frame * 2] = value;
            pcm[frame * 2 + 1] = value;
        }
        REQUIRE(cache.store({source, block}, maximum_read_frames, pcm));
    }
}

auto cache_for(std::size_t blocks) -> block_cache_limits_t {
    const auto frames = blocks * maximum_read_frames;
    return {.maximum_bytes = frames * 8, .maximum_frames = frames,
            .maximum_bytes_per_asset = frames * 8, .maximum_frames_per_asset = frames};
}

// Renders until the speed and gain settle.
void settle(scrub_reader_t& reader, double speed, int renders = 8) {
    std::vector<float> scratch(2048 * 2);
    for (int i = 0; i < renders; ++i) static_cast<void>(reader.render(speed, false, scratch));
}

} // namespace

TEST_CASE("audio.scrub_reader plays a ramp forwards at the requested speed", "[audio][scrub]") {
    decoded_block_cache_t cache(cache_for(block_count));
    const auto source = source_named("a");
    store_ramp(cache, source, block_count);
    scrub_reader_t reader(cache);
    reader.set_segments({{source, 0, 0, block_count * maximum_read_frames}});
    reader.reset(16'384.0);
    settle(reader, 1.0);
    std::vector<float> out(200 * 2);
    const auto result = reader.render(1.0, false, out);
    REQUIRE(result.missing_blocks == 0);
    for (std::size_t i = 1; i < 200; ++i)
        REQUIRE(out[i * 2] - out[(i - 1) * 2] == Catch::Approx(ramp_scale).epsilon(0.02));
    REQUIRE(out[0] == out[1]);
}

TEST_CASE("audio.scrub_reader runs backwards and at other speeds", "[audio][scrub]") {
    decoded_block_cache_t cache(cache_for(block_count));
    const auto source = source_named("a");
    store_ramp(cache, source, block_count);
    scrub_reader_t reader(cache);
    reader.set_segments({{source, 0, 0, block_count * maximum_read_frames}});
    for (const auto speed : {-1.0, -3.0, 0.5, 2.0}) {
        reader.reset(speed < 0 ? 40'000.0 : 10'000.0);
        settle(reader, speed, 2);
        std::vector<float> out(100 * 2);
        const auto before = reader.position_frames();
        static_cast<void>(reader.render(speed, false, out));
        REQUIRE(reader.position_frames() - before == Catch::Approx(speed * 100.0).epsilon(0.02));
        const auto slope = (out[99 * 2] - out[0]) / 99.0;
        // Quiet speeds are attenuated, so compare only the sign there.
        if (std::abs(speed) >= 1.0) REQUIRE(slope == Catch::Approx(speed * ramp_scale).epsilon(0.03));
        else REQUIRE((slope > 0) == (speed > 0));
    }
}

TEST_CASE("audio.scrub_reader reads across block borders without a seam", "[audio][scrub]") {
    decoded_block_cache_t cache(cache_for(block_count));
    const auto source = source_named("a");
    store_ramp(cache, source, block_count);
    scrub_reader_t reader(cache);
    reader.set_segments({{source, 0, 0, block_count * maximum_read_frames}});
    // Settle first (the speed slew costs a few hundred frames), then cross the block border.
    reader.reset(250.0);
    settle(reader, 1.0);
    REQUIRE(reader.position_frames() > 15'500.0);
    REQUIRE(reader.position_frames() < 16'384.0 - 100.0);
    std::vector<float> out(300 * 2);
    static_cast<void>(reader.render(1.0, false, out));
    REQUIRE(reader.position_frames() > 16'384.0 + 100.0);
    for (std::size_t i = 1; i < 300; ++i)
        REQUIRE(out[i * 2] - out[(i - 1) * 2] == Catch::Approx(ramp_scale).epsilon(0.02));
}

TEST_CASE("audio.scrub_reader maps timeline frames through the segment", "[audio][scrub]") {
    decoded_block_cache_t cache(cache_for(block_count));
    const auto source = source_named("a");
    store_ramp(cache, source, block_count);
    scrub_reader_t reader(cache);
    // Timeline 1000.. plays source 20000..; before 1000 is silent.
    reader.set_segments({{source, 1'000, 20'000, 10'000}});
    reader.reset(1'500.0);
    settle(reader, 0.0001);
    reader.reset(1'500.0);
    std::vector<float> out(64 * 2);
    static_cast<void>(reader.render(1.0, false, out));
    // Gain is still fading in at the start; the value at the end tracks source frame 20500+.
    const auto expected = (20'000.0 + (reader.position_frames() - 1'000.0)) * ramp_scale;
    REQUIRE(out[63 * 2] > 0.0F);
    REQUIRE(out[63 * 2] <= static_cast<float>(expected) + 1e-4F);
}

TEST_CASE("audio.scrub_reader is silent outside segments and reports missing blocks", "[audio][scrub]") {
    decoded_block_cache_t cache(cache_for(block_count));
    const auto source = source_named("a");
    store_ramp(cache, source, block_count, 1);
    scrub_reader_t reader(cache);
    reader.set_segments({{source, 0, 0, block_count * maximum_read_frames}});
    reader.reset(20'000.0);
    std::vector<float> out(512 * 2);
    const auto result = reader.render(1.0, false, out);
    REQUIRE(result.missing_blocks >= 1);
    for (const auto sample : out) REQUIRE(sample == 0.0F);

    reader.reset(-5'000.0);
    std::vector<float> before(512 * 2);
    const auto outside = reader.render(1.0, false, before);
    REQUIRE(outside.missing_blocks == 0);
    for (const auto sample : before) REQUIRE(sample == 0.0F);
}

TEST_CASE("audio.scrub_reader holding fades to silence without a click", "[audio][scrub]") {
    decoded_block_cache_t cache(cache_for(block_count));
    const auto source = source_named("a");
    store_ramp(cache, source, block_count);
    scrub_reader_t reader(cache);
    reader.set_segments({{source, 0, 0, block_count * maximum_read_frames}});
    reader.reset(10'000.0);
    settle(reader, 1.0);
    REQUIRE_FALSE(reader.is_silent());
    std::vector<float> out(2048 * 2);
    static_cast<void>(reader.render(1.0, true, out));
    // No step bigger than a small multiple of the normal sample-to-sample change.
    for (std::size_t i = 1; i < 2048; ++i) REQUIRE(std::abs(out[i * 2] - out[(i - 1) * 2]) < 1e-3F);
    REQUIRE(reader.is_silent());
    for (std::size_t i = 1900; i < 2048; ++i) REQUIRE(std::abs(out[i * 2]) < 1e-3F);
}

void check_scrub_reader(bool value, std::source_location where = std::source_location::current()) {
    INFO("check at line " << where.line()); CHECK(value);
}
void require_scrub_reader(bool value, std::source_location where = std::source_location::current()) {
    INFO("check at line " << where.line()); REQUIRE(value);
}

TEST_CASE("audio.scrub_reader wanted blocks run nearest first and lean along the motion", "[audio][scrub]") {
    const auto source = source_named("a");
    const std::vector<scrub_segment_t> segments{{source, 0, 0, 10 * maximum_read_frames}};
    const auto position = 5.5 * maximum_read_frames;
    std::vector<audio_block_key_t> wanted;
    scrub_reader_t::plan_blocks(segments, position, 2 * maximum_read_frames, maximum_read_frames / 2, 1.0, wanted);
    require_scrub_reader(!(wanted.empty()));
    require_scrub_reader(wanted.front().block_index == 5);
    for (const auto& key : wanted) require_scrub_reader(key.block_index >= 5);
    require_scrub_reader(wanted.back().block_index == 7);
    scrub_reader_t::plan_blocks(segments, position, 2 * maximum_read_frames, maximum_read_frames / 2, -1.0, wanted);
    require_scrub_reader(wanted.front().block_index == 5);
    for (const auto& key : wanted) require_scrub_reader(key.block_index <= 6);
    require_scrub_reader(wanted.back().block_index == 3);
}
