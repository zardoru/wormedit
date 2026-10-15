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

void require_transport(bool condition) { REQUIRE(condition); }

auto transport_drops_missing_voice_without_affecting_other_voices() -> bool {
    using namespace wormedit::audio;
    decoded_block_cache_t blocks;
    transport_t transport{blocks, 2};
    const auto token = transport.start();
    if (!token.has_value())
        return false;

    const auto dropped_source = source("drop-after-first-block.ogg");
    const auto other_source = source("unaffected.ogg");
    const audio_block_key_t dropped_first{.source = dropped_source, .block_index = 0};
    const audio_block_key_t dropped_second{.source = dropped_source, .block_index = 1};
    const audio_block_key_t other_first{.source = other_source, .block_index = 0};
    const audio_block_key_t other_second{.source = other_source, .block_index = 1};
    const audio_block_key_t other_third{.source = other_source, .block_index = 2};
    const std::vector<float> first_samples(maximum_read_frames * output_channel_count, 0.125F);
    const std::vector<float> other_samples(maximum_read_frames * output_channel_count, 0.25F);
    if (blocks.store(dropped_first, maximum_read_frames, first_samples) != block_store_status_t::stored ||
        blocks.store(other_first, maximum_read_frames, other_samples) != block_store_status_t::stored ||
        blocks.store(other_second, maximum_read_frames, other_samples) != block_store_status_t::stored ||
        blocks.store(other_third, maximum_read_frames, other_samples) != block_store_status_t::stored)
        return false;
    const std::array voices{
        transport_voice_t{.id = 1,
                          .source = dropped_source,
                          .frame_count = 3U * maximum_read_frames},
        transport_voice_t{.id = 2,
                          .source = other_source,
                          .frame_count = 3U * maximum_read_frames},
    };
    if (transport.schedule_voices(*token, voices) != voice_schedule_status_t::scheduled)
        return false;

    std::vector<float> output(2U * maximum_read_frames * output_channel_count);
    const auto first_mix = transport.mix_frames(*token, 0, output);
    const bool first_mix_ok = first_mix.value().missing_blocks == 1 && first_mix.value().dropped_voices == 1 &&
                              transport.scheduled_voice_count() == 1 && output[0] == 0.375F &&
                              output[maximum_read_frames * output_channel_count] == 0.25F;
    if (blocks.store(dropped_second, maximum_read_frames, first_samples) !=
        block_store_status_t::stored)
        return false;
    std::vector<float> later_output(maximum_read_frames * output_channel_count);
    const auto later_mix = transport.mix_frames(*token, 2U * maximum_read_frames, later_output);
    return first_mix_ok && later_mix.value().missing_blocks == 0 && later_mix.value().dropped_voices == 0 &&
           transport.scheduled_voice_count() == 0 && later_output[0] == 0.25F;
}

auto transport_counts_dropped_music_voices_by_kind() -> bool {
    using namespace wormedit::audio;
    decoded_block_cache_t blocks;
    transport_t transport{blocks, 1};
    const auto token = transport.start();
    if (!token) return false;
    const std::array voices{transport_voice_t{.id = 1,
                                              .kind = voice_kind_t::music,
                                              .source = source("missing-music.ogg"),
                                              .frame_count = 64}};
    if (transport.schedule_voices(*token, voices) != voice_schedule_status_t::scheduled)
        return false;
    std::vector<float> output(64U * output_channel_count);
    const auto mixed = transport.mix_frames(*token, 0, output);
    return mixed.value().dropped_voices == 1 && mixed.value().dropped_music_voices == 1;
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
        const auto decoded = transport.decode_source_block(decoders[index], *token, key);
        REQUIRE(decoded.value().status == wormedit::audio::block_decode_status_t::decoded);
        REQUIRE(transport.schedule_voice(
                    *token, {
                                .id = index + 1U,
                                .kind = wormedit::audio::voice_kind_t::playable_note,
                                .source = key.source,
                                .timeline_start_frame = 128,
                                .source_start_frame = 0,
                                .frame_count = decoders[index].info()->output_frame_count,
                            }) == wormedit::audio::voice_schedule_status_t::scheduled);
    }

    std::vector<float> output(4096U * wormedit::audio::output_channel_count);
    const auto mixed = transport.mix_frames(*token, 128, output);
    CHECK(mixed.value().status == wormedit::audio::transport_mix_status_t::mixed);
    CHECK(mixed.value().voices_mixed == 3);
    CHECK(mixed.value().missing_blocks == 0);
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
    REQUIRE(transport.decode_source_block(decoder, *token, key).value().status ==
            wormedit::audio::block_decode_status_t::decoded);
    constexpr std::array kinds{
        wormedit::audio::voice_kind_t::music,
        wormedit::audio::voice_kind_t::bgm,
        wormedit::audio::voice_kind_t::playable_note,
        wormedit::audio::voice_kind_t::click_preview,
        wormedit::audio::voice_kind_t::layer_assist,
    };
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        REQUIRE(transport.schedule_voice(*token,
                                         {
                                             .id = index + 1U,
                                             .kind = kinds[index],
                                             .source = key.source,
                                             .frame_count = decoder.info()->output_frame_count,
                                             .gain = 0.2F,
                                         }) == wormedit::audio::voice_schedule_status_t::scheduled);
    }
    std::vector<float> before_stop(512U * wormedit::audio::output_channel_count);
    CHECK(transport.mix_frames(*token, 0, before_stop).value().voices_mixed == kinds.size());
    CHECK_FALSE(all_zero(before_stop));

    const auto stopped = transport.stop();
    CHECK(stopped.scheduled_voices == kinds.size());
    for (const auto count : stopped.voices_by_kind) {
        CHECK(count == 1);
    }
    CHECK(transport.scheduled_voice_count() == 0);

    std::vector<float> after_stop(512U * wormedit::audio::output_channel_count, 1.0F);
    const auto silent = transport.mix_frames(*token, 0, after_stop);
    require_transport(!silent);
    CHECK(silent.error().status == wormedit::audio::transport_mix_status_t::stale_generation);
    CHECK(all_zero(after_stop));
    CHECK(transport.decode_source_block(decoder, *token, key) ==
          std::unexpected(wormedit::audio::block_decode_result_t{.status = wormedit::audio::block_decode_status_t::stale_generation}));
    CHECK(blocks.has_block(key));

    const auto next_token = transport.start();
    REQUIRE(next_token.has_value());
    CHECK(next_token != token);
    CHECK(transport.schedule_voice(
              *token, {
                          .id = 99,
                          .source = key.source,
                          .frame_count = 1,
                      }) == wormedit::audio::voice_schedule_status_t::stale_generation);
}

TEST_CASE("transport metronome volume changes scheduled chart and assist clicks without changing music",
          "[unit][audio][transport][metronome-volume]") {
    using namespace wormedit::audio;
    decoded_block_cache_t cache;
    transport_t transport{cache, 4};
    const audio_source_identity_t click_source{"fixture:click", "generated:click"};
    const audio_source_identity_t music_source{"fixture:music", "generated:music"};
    const std::vector<float> click_pcm(16 * output_channel_count, 0.25F);
    const std::vector<float> music_pcm(16 * output_channel_count, 0.25F);
    REQUIRE(cache.store({.source = click_source, .block_index = 0}, 16, click_pcm) ==
            block_store_status_t::stored);
    REQUIRE(cache.store({.source = music_source, .block_index = 0}, 16, music_pcm) ==
            block_store_status_t::stored);
    const auto token = transport.start();
    REQUIRE(token);
    CHECK(transport.click_preview_volume() == 100);
    const std::array voices{
        transport_voice_t{.id = 1, .kind = voice_kind_t::click_preview,
                          .source = click_source, .frame_count = 8},
        transport_voice_t{.id = 2, .kind = voice_kind_t::music,
                          .source = music_source, .frame_count = 8},
    };
    REQUIRE(transport.schedule_voices(*token, voices) == voice_schedule_status_t::scheduled);
    std::array<float, 8> first{};
    REQUIRE(transport.mix_frames(*token, 0, first).value().status == transport_mix_status_t::mixed);
    CHECK(first.front() == 0.5F);

    REQUIRE(transport.set_click_preview_volume(50));
    CHECK_FALSE(transport.set_click_preview_volume(101));
    CHECK(transport.click_preview_volume() == 50);
    std::array<float, 8> midpoint{};
    REQUIRE(transport.mix_frames(*token, 4, midpoint).value().status == transport_mix_status_t::mixed);
    CHECK(midpoint.front() == 0.375F);

    const std::array later_voices{
        transport_voice_t{.id = 3, .kind = voice_kind_t::click_preview,
                          .source = click_source, .timeline_start_frame = 8,
                          .source_start_frame = 8, .frame_count = 4},
        transport_voice_t{.id = 4, .kind = voice_kind_t::music,
                          .source = music_source, .timeline_start_frame = 8,
                          .source_start_frame = 8, .frame_count = 4},
    };
    REQUIRE(transport.schedule_voices(*token, later_voices) == voice_schedule_status_t::scheduled);
    REQUIRE(transport.set_click_preview_volume(0));
    std::array<float, 8> silent_click{};
    REQUIRE(transport.mix_frames(*token, 8, silent_click).value().status == transport_mix_status_t::mixed);
    CHECK(silent_click.front() == 0.25F);
}

TEST_CASE("transport reports missing source blocks without emitting garbage",
          "[unit][audio][transport]") {
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::transport_t transport(blocks, 1);
    const auto token = transport.start();
    REQUIRE(token.has_value());
    REQUIRE(
        transport.schedule_voice(*token, {
                                             .id = 1,
                                             .source = source("missing.ogg"),
                                             .frame_count = 128,
                                         }) == wormedit::audio::voice_schedule_status_t::scheduled);
    CHECK(transport.schedule_voice(
              *token, {
                          .id = 2,
                          .source = source("other.ogg"),
                          .frame_count = 128,
                      }) == wormedit::audio::voice_schedule_status_t::capacity_exceeded);

    std::vector<float> output(128U * wormedit::audio::output_channel_count, 1.0F);
    const auto mixed = transport.mix_frames(*token, 0, output);
    CHECK(mixed.value().status == wormedit::audio::transport_mix_status_t::mixed);
    CHECK(mixed.value().voices_mixed == 1);
    CHECK(mixed.value().missing_blocks == 1);
    CHECK(all_zero(output));
    CHECK(transport.scheduled_voice_count() == 0);

    CHECK(
        transport.schedule_voice(*token, {
                                             .id = 2,
                                             .source = source("other.ogg"),
                                             .timeline_start_frame = 256,
                                             .frame_count = 128,
                                         }) == wormedit::audio::voice_schedule_status_t::scheduled);
    CHECK(transport.scheduled_voice_count() == 1);
    std::ranges::fill(output, 1.0F);
    const auto repeated = transport.mix_frames(*token, 0, output);
    require_transport(!repeated);
    CHECK(repeated.error().status ==
          wormedit::audio::transport_mix_status_t::non_monotonic_callback);
    CHECK(all_zero(output));
}

TEST_CASE("transport drops a voice at its first missing block without affecting other voices",
          "[unit][audio][transport][underrun]") {
    CHECK(transport_drops_missing_voice_without_affecting_other_voices());
}

TEST_CASE("transport identifies dropped music voices by kind", "[unit][audio][transport][underrun]") {
    CHECK(transport_counts_dropped_music_voices_by_kind());
}

TEST_CASE("transport publishes layered voice batches atomically",
          "[unit][audio][transport][batch][capacity]") {
    using namespace wormedit::audio;
    decoded_block_cache_t blocks;
    transport_t transport(blocks, 2);
    const auto token = transport.start();
    REQUIRE(token.has_value());

    const std::array too_many{
        transport_voice_t{.id = 1, .source = source("one.ogg"), .frame_count = 64},
        transport_voice_t{.id = 2, .source = source("two.ogg"), .frame_count = 64},
        transport_voice_t{.id = 3, .source = source("three.ogg"), .frame_count = 64},
    };
    CHECK(transport.schedule_voices(*token, too_many) ==
          voice_schedule_status_t::capacity_exceeded);
    CHECK(transport.scheduled_voice_count() == 0);

    const std::array duplicate_ids{
        transport_voice_t{.id = 7, .source = source("one.ogg"), .frame_count = 64},
        transport_voice_t{.id = 7, .source = source("two.ogg"), .frame_count = 64},
    };
    CHECK(transport.schedule_voices(*token, duplicate_ids) ==
          voice_schedule_status_t::duplicate_id);
    CHECK(transport.scheduled_voice_count() == 0);

    const auto accepted = std::span{too_many}.first<2>();
    CHECK(transport.schedule_voices(*token, accepted) == voice_schedule_status_t::scheduled);
    CHECK(transport.scheduled_voice_count() == 2);
}

namespace {
void check_bgm_mix(bool condition) { CHECK(condition); }
}

TEST_CASE("BGM mix groups silence active and future voices without losing their source position",
          "[audio][transport][bgm-mix]") {
    using namespace wormedit::audio;
    decoded_block_cache_t blocks;
    transport_t transport{blocks};
    const auto token = transport.start();
    require_transport(token.has_value());
    const auto shared = source("shared-mix-source");
    std::vector<float> samples(64 * output_channel_count);
    for (std::size_t frame = 0; frame < 64; ++frame)
        for (std::size_t channel = 0; channel < output_channel_count; ++channel)
            samples[frame * output_channel_count + channel] = static_cast<float>(frame + 1) / 1024;
    require_transport(blocks.store({.source = shared, .block_index = 0}, 64, samples) == block_store_status_t::stored);
    const std::array voices{
        transport_voice_t{.id = 1, .kind = voice_kind_t::bgm, .source = shared, .frame_count = 64, .mix_group = 11},
        transport_voice_t{.id = 2, .kind = voice_kind_t::bgm, .source = shared, .frame_count = 64, .mix_group = 22},
        transport_voice_t{.id = 3, .source = shared, .frame_count = 64, .mix_group = 11},
        transport_voice_t{.id = 4, .kind = voice_kind_t::music, .source = shared, .frame_count = 64}};
    require_transport(transport.schedule_voices(*token, voices) == voice_schedule_status_t::scheduled);
    transport.set_muted_mix_groups(std::array<std::uint64_t, 2>{0, 11});
    std::array<float, 16> output{};
    check_bgm_mix(transport.mix_frames(*token, 0, output).value().voices_mixed == 2);
    check_bgm_mix(output.front() == 2.0F / 1024);
    check_bgm_mix(transport.scheduled_voice_count() == 4);
    transport.set_muted_mix_groups({});
    check_bgm_mix(transport.mix_frames(*token, 8, output).value().voices_mixed == 4);
    check_bgm_mix(output.front() == 36.0F / 1024);
    transport.set_muted_mix_groups(std::array<std::uint64_t, 1>{22});
    require_transport(transport.schedule_voice(*token, {.id = 5, .kind = voice_kind_t::bgm,
        .source = shared, .timeline_start_frame = 16, .frame_count = 8, .mix_group = 22}) == voice_schedule_status_t::scheduled);
    check_bgm_mix(transport.mix_frames(*token, 16, output).value().voices_mixed == 3);
    check_bgm_mix(output.front() == 51.0F / 1024);
    check_bgm_mix(transport.scheduled_voice_count() == 4);
    std::array<float, 80> tail{};
    check_bgm_mix(transport.mix_frames(*token, 24, tail).value().voices_mixed == 3);
    check_bgm_mix(transport.scheduled_voice_count() == 0);
}

namespace {
auto stale_transport_outcomes_keep_frames_and_silence() -> bool {
    using namespace wormedit::audio;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    const auto token = transport.start();
    if (!token) return false;
    static_cast<void>(transport.stop());
    std::array<float, 64> output;
    output.fill(1.0F);
    const auto mixed = transport.mix_frames(*token, 0, output);
    if (mixed.has_value()) return false;
    const auto snapshot = transport.snapshot_voices(*token, 4);
    return mixed.error().status == transport_mix_status_t::stale_generation &&
           mixed.error().frames == 32 &&
           std::ranges::count(output, 0.0F) == static_cast<std::ptrdiff_t>(output.size()) &&
           !snapshot.has_value() && snapshot.error() == active_voice_snapshot_status_t::stale_generation;
}
} // namespace

TEST_CASE("transport expected stale outcomes retain requested frames and silence",
          "[unit][audio][transport][result][generation]") {
    CHECK(stale_transport_outcomes_keep_frames_and_silence());
}
