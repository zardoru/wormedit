#include <wormedit/audio/transport.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto source(std::string name) -> wormedit::audio::audio_source_identity_t {
    return {"fixture:" + name, "generated-v1:" + name};
}

auto all_zero(const std::vector<float>& samples) -> bool {
    return std::ranges::all_of(samples, [](float sample) { return sample == 0.0F; });
}

} // namespace

TEST_CASE("transport mixes same-frame keysound polyphony from decoded cache blocks",
          "[unit][audio][transport][fixture]") {
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::transport_t transport(blocks);
    const auto token = transport.start();
    REQUIRE(token.has_value());

    std::array<wormedit::audio::decoder_t, 3> decoders;
    const std::array names{"keysound-c4.ogg", "keysound-e4.ogg", "keysound-g4.ogg"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        std::string error;
        REQUIRE(decoders[index].open(fixture_path(names[index]), &error));
        INFO("decoder open error: " << error);
        const wormedit::audio::audio_block_key_t key{
            .source = source(names[index]),
            .block_index = 0,
        };
        const auto decoded = transport.decode_block(decoders[index], *token, key);
        REQUIRE(decoded.status == wormedit::audio::block_decode_status_t::decoded);
        REQUIRE(transport.schedule_voice(*token, {
                    .id = index + 1U,
                    .kind = wormedit::audio::voice_kind_t::playable_note,
                    .source = key.source,
                    .timeline_start_frame = 128,
                    .source_start_frame = 0,
                    .frame_count = decoders[index].info()->output_frame_count,
                }) == wormedit::audio::voice_schedule_status_t::scheduled);
    }

    std::vector<float> output(4096U * wormedit::audio::output_channel_count);
    const auto mixed = transport.mix(*token, 128, output);
    CHECK(mixed.status == wormedit::audio::transport_mix_status_t::mixed);
    CHECK(mixed.voices_mixed == 3);
    CHECK(mixed.missing_blocks == 0);
    CHECK(transport.scheduled_voice_count() == 3);
    CHECK_FALSE(all_zero(output));

    double square_sum{};
    for (const auto sample : output) {
        square_sum += static_cast<double>(sample) * sample;
    }
    CHECK(std::sqrt(square_sum / output.size()) > 0.05);
}

TEST_CASE("transport Stop clears every voice class and silences the next callback",
          "[unit][audio][transport][stop][fixture]") {
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::transport_t transport(blocks);
    const auto token = transport.start();
    REQUIRE(token.has_value());
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("polyphonic-chord.ogg"), &error));
    INFO("decoder open error: " << error);
    const wormedit::audio::audio_block_key_t key{
        .source = source("polyphonic-chord.ogg"),
        .block_index = 0,
    };
    REQUIRE(transport.decode_block(decoder, *token, key).status ==
            wormedit::audio::block_decode_status_t::decoded);
    constexpr std::array kinds{
        wormedit::audio::voice_kind_t::music,
        wormedit::audio::voice_kind_t::bgm,
        wormedit::audio::voice_kind_t::playable_note,
        wormedit::audio::voice_kind_t::click_preview,
    };
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        REQUIRE(transport.schedule_voice(*token, {
                    .id = index + 1U,
                    .kind = kinds[index],
                    .source = key.source,
                    .frame_count = decoder.info()->output_frame_count,
                    .gain = 0.2F,
                }) == wormedit::audio::voice_schedule_status_t::scheduled);
    }
    std::vector<float> before_stop(512U * wormedit::audio::output_channel_count);
    CHECK(transport.mix(*token, 0, before_stop).voices_mixed == 4);
    CHECK_FALSE(all_zero(before_stop));

    const auto stopped = transport.stop();
    CHECK(stopped.scheduled_voices == 4);
    for (const auto count : stopped.voices_by_kind) {
        CHECK(count == 1);
    }
    CHECK(transport.scheduled_voice_count() == 0);

    std::vector<float> after_stop(512U * wormedit::audio::output_channel_count, 1.0F);
    const auto silent = transport.mix(*token, 0, after_stop);
    CHECK(silent.status == wormedit::audio::transport_mix_status_t::stale_generation);
    CHECK(all_zero(after_stop));
    CHECK(transport.decode_block(decoder, *token, key).status ==
          wormedit::audio::block_decode_status_t::stale_generation);
    CHECK(blocks.contains(key));

    const auto next_token = transport.start();
    REQUIRE(next_token.has_value());
    CHECK(next_token != token);
    CHECK(transport.schedule_voice(*token, {
              .id = 99,
              .source = key.source,
              .frame_count = 1,
          }) == wormedit::audio::voice_schedule_status_t::stale_generation);
}

TEST_CASE("transport reports missing source blocks without emitting garbage",
          "[unit][audio][transport]") {
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::transport_t transport(blocks, 1);
    const auto token = transport.start();
    REQUIRE(token.has_value());
    REQUIRE(transport.schedule_voice(*token, {
                .id = 1,
                .source = source("missing.ogg"),
                .frame_count = 128,
            }) == wormedit::audio::voice_schedule_status_t::scheduled);
    CHECK(transport.schedule_voice(*token, {
              .id = 2,
              .source = source("other.ogg"),
              .frame_count = 128,
          }) == wormedit::audio::voice_schedule_status_t::capacity_exceeded);

    std::vector<float> output(128U * wormedit::audio::output_channel_count, 1.0F);
    const auto mixed = transport.mix(*token, 0, output);
    CHECK(mixed.status == wormedit::audio::transport_mix_status_t::mixed);
    CHECK(mixed.voices_mixed == 1);
    CHECK(mixed.missing_blocks == 1);
    CHECK(all_zero(output));
    CHECK(transport.scheduled_voice_count() == 0);

    CHECK(transport.schedule_voice(*token, {
              .id = 2,
              .source = source("other.ogg"),
              .timeline_start_frame = 256,
              .frame_count = 128,
          }) == wormedit::audio::voice_schedule_status_t::scheduled);
    CHECK(transport.scheduled_voice_count() == 1);
    std::ranges::fill(output, 1.0F);
    CHECK(transport.mix(*token, 0, output).status ==
          wormedit::audio::transport_mix_status_t::non_monotonic_callback);
    CHECK(all_zero(output));
}
