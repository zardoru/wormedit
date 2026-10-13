#include <wormedit/audio/object_audition.h>
#include <wormedit/audio/source_registry.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <barrier>
#include <thread>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto registration(wormedit::asset_id_t asset, std::string_view name)
    -> wormedit::audio::decoded_source_registration_t {
    return {.asset = asset,
            .path = fixture_path(name),
            .source = {"fixture:" + std::string(name), "generated-v1:" + std::string(name)}};
}

} // namespace

TEST_CASE("decoded source registry exposes authoritative geometry and exact identity decoding",
          "[unit][audio][source-registry][fixture]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry;
    const auto added =
        registry.register_source(registration(wormedit::asset_id_t{7}, "keysound-c4.ogg"));
    REQUIRE(added);
    REQUIRE(added.binding.has_value());
    CHECK(added.binding->asset == wormedit::asset_id_t{7});
    CHECK(added.binding->source_sample_rate > 0);
    REQUIRE(added.binding->source_frame_count.has_value());
    CHECK(*added.binding->source_frame_count > 0);
    CHECK(added.binding->output_sample_rate == output_sample_rate);
    CHECK(added.binding->output_frame_count > 0);

    const auto bindings = registry.bindings();
    REQUIRE(bindings.size() == 1);
    CHECK(bindings.front() == *added.binding);

    decoded_block_cache_t cache;
    transport_t transport{cache};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    const audio_block_key_t key{added.binding->source, 0};
    const auto decoded = registry.decode_block(transport, *token, key);
    CHECK(decoded.status == block_decode_status_t::decoded);
    CHECK(decoded.frames > 0);
    CHECK(cache.has_block(key));

    const auto unknown = registry.decode_block(
        transport, *token, {.source = {"fixture:unknown", "missing:v1"}, .block_index = 0});
    CHECK(unknown.status == block_decode_status_t::invalid_key);
    CHECK(registry.remove(wormedit::asset_id_t{7}));
    CHECK_FALSE(registry.binding(wormedit::asset_id_t{7}));
    CHECK_FALSE(registry.remove(wormedit::asset_id_t{7}));
    CHECK(registry.size() == 0);
}

TEST_CASE("decoded source registry rejects malformed duplicate and over-cap registrations",
          "[unit][audio][source-registry][bounds]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry{1};
    CHECK(registry.register_source({}).status == source_registration_status_t::invalid_request);

    auto missing = registration(wormedit::asset_id_t{1}, "does-not-exist.ogg");
    CHECK(registry.register_source(std::move(missing)).status ==
          source_registration_status_t::open_failed);
    CHECK(registry.size() == 0);

    const auto first =
        registry.register_source(registration(wormedit::asset_id_t{1}, "keysound-c4.ogg"));
    REQUIRE(first);

    auto duplicate_asset = registration(wormedit::asset_id_t{1}, "keysound-e4.ogg");
    CHECK(registry.register_source(std::move(duplicate_asset)).status ==
          source_registration_status_t::duplicate_asset);

    auto duplicate_source = registration(wormedit::asset_id_t{2}, "keysound-g4.ogg");
    duplicate_source.source = first.binding->source;
    CHECK(registry.register_source(std::move(duplicate_source)).status ==
          source_registration_status_t::duplicate_source);

    CHECK(
        registry.register_source(registration(wormedit::asset_id_t{2}, "keysound-g4.ogg")).status ==
        source_registration_status_t::capacity_exceeded);
    CHECK(registry.size() == 1);
    registry.clear();
    CHECK(registry.size() == 0);
}

TEST_CASE("registered decoder bindings prepare a revision-bound semantic object audition",
          "[unit][audio][source-registry][object-audition][integration]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t registry;
    const auto added = registry.register_source(registration(asset_id_t{7}, "keysound-c4.ogg"));
    REQUIRE(added);
    REQUIRE(registry.binding(asset_id_t{7}));
    CHECK(registry.binding(asset_id_t{7}) == added.binding);
    CHECK_FALSE(registry.binding(asset_id_t{8}));

    song_document_t song{.id = song_id_t{1}, .revision = 4};
    song.audio_assets.push_back({.id = asset_id_t{7}});
    song.resources.push_back({.id = resource_id_t{8}, .asset = asset_id_t{7}});
    chart_document_t chart{.id = chart_id_t{2}};
    measure_chunk_t measure{.id = measure_id_t{3}};
    measure.notes.push_back({.id = object_id_t{9},
                             .position = {measure.id, 0},
                             .lane = lane_id_t{1},
                             .sound = resource_id_t{8}});
    chart.measures.push_back(std::move(measure));
    song.charts.push_back(std::move(chart));

    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    object_audition_controller_t controller{bridge};
    const auto bindings = registry.bindings();
    const auto prepared =
        controller.prepare(song, {song.revision, chart_id_t{2}, object_id_t{9}}, bindings);
    REQUIRE(prepared);
    REQUIRE(prepared.prepared.has_value());
    REQUIRE(prepared.prepared->clips.size() == 1);
    CHECK(prepared.prepared->clips.front().source == added.binding->source);
    CHECK(prepared.prepared->clips.front().source_start_frame == 0);
    CHECK(prepared.prepared->clips.front().frame_count == added.binding->output_frame_count);
    CHECK_FALSE(bridge.session_token().has_value());
}

TEST_CASE("generated decoded sources feed transport blocks without becoming project bindings",
          "[unit][audio][source-registry][generated]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry;
    const std::vector<float> pcm{0.25F, -0.25F, 0.5F, -0.5F, 0.75F, -0.75F};
    const auto added = registry.register_generated_source(
        {.source = {"internal:metronome", "click:v1"}, .interleaved_stereo = pcm});
    REQUIRE(added);
    CHECK_FALSE(added.binding->asset);
    CHECK(added.binding->output_frame_count == 3);
    CHECK(registry.bindings().empty());

    decoded_block_cache_t cache;
    transport_t transport{cache};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    const audio_block_key_t key{added.binding->source, 0};
    const auto decoded = registry.decode_block(transport, *token, key);
    REQUIRE(decoded.status == block_decode_status_t::decoded);
    CHECK(decoded.frames == 3);
    REQUIRE(cache.with_block(key, [&](const decoded_audio_block_t& block) {
        CHECK(block.frame_count == 3);
        CHECK(block.interleaved_stereo == pcm);
    }));

    CHECK(
        registry
            .register_generated_source({.source = added.binding->source, .interleaved_stereo = pcm})
            .status == source_registration_status_t::duplicate_source);
}

TEST_CASE("decoded source registry retains project decoders for waveform analysis",
          "[unit][audio][source-registry][waveform][fixture]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t registry;
    const auto added = registry.register_source(registration(asset_id_t{7}, "keysound-c4.ogg"));
    REQUIRE(added);
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;

    const auto analyzed = registry.analyze_waveform(asset_id_t{7}, blocks, waveforms, 128);
    REQUIRE(analyzed.status == waveform_analysis_status_t::generated);
    REQUIRE(analyzed.waveform.has_value());
    CHECK(analyzed.waveform->key.source == added.binding->source);
    CHECK(analyzed.waveform->total_frames == added.binding->output_frame_count);
    CHECK(registry.analyze_waveform(asset_id_t{7}, blocks, waveforms, 128).status ==
          waveform_analysis_status_t::cache_hit);
    CHECK(registry.analyze_waveform(asset_id_t{99}, blocks, waveforms, 128).status ==
          waveform_analysis_status_t::decoder_unavailable);
    CHECK(registry.analyze_waveform({}, blocks, waveforms, 128).status ==
          waveform_analysis_status_t::invalid_request);
}

namespace {

auto pending_registration(std::uint64_t asset, std::string_view name = "keysound-c4.ogg")
    -> wormedit::audio::decoded_source_registration_t {
    return {.asset = wormedit::asset_id_t{asset},
            .path = fixture_path(name),
            .source = {"fixture:asset:" + std::to_string(asset), "pending-v1:" + std::to_string(asset)}};
}

} // namespace

namespace {
void check_pending_inventory(wormedit::audio::decoded_source_registry_t& registry) {
    using namespace wormedit::audio;
    CHECK(registry.size() == 1'000);
    CHECK(registry.pending_count() == 1'000);
    CHECK(registry.open_decoder_count() == 0);
    CHECK(registry.bindings().empty());
    CHECK_FALSE(registry.binding(wormedit::asset_id_t{1}));

    CHECK(registry.register_source_pending(pending_registration(1)).status ==
          source_registration_status_t::duplicate_asset);

}
} // namespace

namespace {
void check_probed_inventory(wormedit::audio::decoded_source_registry_t& registry, std::size_t probed) {
    CHECK(probed == 1'000);
    CHECK(registry.pending_count() == 0);
    CHECK(registry.bindings().size() == 1'000);
    CHECK(registry.binding(wormedit::asset_id_t{500})->output_frame_count > 0);
    CHECK(registry.take_probe_failures().empty());
}
} // namespace

TEST_CASE("pending registration opens no decoder until a probe runs",
          "[unit][audio][source-registry][lazy]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry{2'048};
    for (std::uint64_t asset = 1; asset <= 1'000; ++asset) {
        const auto added = registry.register_source_pending(pending_registration(asset));
        REQUIRE(added.status == source_registration_status_t::pending);
        CHECK_FALSE(added.binding.has_value());
    }
    check_pending_inventory(registry);

    std::size_t probed = 0;
    while (registry.probe_next_pending()) {
        ++probed;
        CHECK(registry.open_decoder_count() == 0);
    }
    check_probed_inventory(registry, probed);
}

namespace {
void check_failure_is_reported_once(wormedit::audio::decoded_source_registry_t& registry) {
    using namespace wormedit::audio;
    CHECK(registry.take_probe_failures().empty());
    CHECK(registry.probe(wormedit::asset_id_t{2}).status == source_registration_status_t::open_failed);
    CHECK(registry.take_probe_failures().empty());
}
void check_failure_queue(wormedit::audio::decoded_source_registry_t& registry) {
    using namespace wormedit::audio;
    const auto failures = registry.take_probe_failures();
    REQUIRE(failures.size() == 1);
    CHECK(failures.front().asset == wormedit::asset_id_t{2});
    CHECK_FALSE(failures.front().message.empty());

}
void check_failed_inventory(wormedit::audio::decoded_source_registry_t& registry) {
    using namespace wormedit::audio;
    CHECK(registry.binding(wormedit::asset_id_t{1}).has_value());
    CHECK_FALSE(registry.binding(wormedit::asset_id_t{2}).has_value());
    CHECK(registry.bindings().size() == 1);
    check_failure_queue(registry);
    check_failure_is_reported_once(registry);
}
} // namespace

TEST_CASE("probe failures are reported once and keep the source out of bindings",
          "[unit][audio][source-registry][lazy]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry;
    REQUIRE(registry.register_source_pending(pending_registration(1)).status ==
            source_registration_status_t::pending);
    REQUIRE(registry.register_source_pending(pending_registration(2, "missing-file.ogg")).status ==
            source_registration_status_t::pending);
    registry.await_pending();

    check_failed_inventory(registry);
}

namespace {
void check_pending_decode(wormedit::audio::decoded_source_registry_t& registry, wormedit::audio::transport_t& transport, wormedit::audio::session_token_t token, std::uint64_t asset) {
    using namespace wormedit::audio;
        const audio_block_key_t key{{"fixture:asset:" + std::to_string(asset),
                                     "pending-v1:" + std::to_string(asset)},
                                    0};
        const auto decoded = registry.decode_block(transport, token, key);
        CHECK(decoded.status == block_decode_status_t::decoded);
        CHECK(registry.binding(wormedit::asset_id_t{asset}).has_value());
        CHECK(registry.open_decoder_count() <= 4);
}
} // namespace

TEST_CASE("decoding a pending source probes it on demand and bounds idle open decoders",
          "[unit][audio][source-registry][lazy]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry{64, 4};
    for (std::uint64_t asset = 1; asset <= 12; ++asset) {
        REQUIRE(registry.register_source_pending(pending_registration(asset)).status ==
                source_registration_status_t::pending);
    }
    decoded_block_cache_t cache;
    transport_t transport{cache};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    for (std::uint64_t asset = 1; asset <= 12; ++asset) {
        check_pending_decode(registry, transport, *token, asset);
    }
    CHECK(registry.open_decoder_count() == 4);
    registry.clear();
    CHECK(registry.open_decoder_count() == 0);
}

namespace {
// A silent 16-bit mono WAV, just over the long-source threshold.
auto write_long_wav() -> std::filesystem::path {
    constexpr std::uint32_t rate = 44'100;
    constexpr std::uint32_t frames = 31 * rate;
    constexpr std::uint32_t bytes = frames * 2;
    const auto path = std::filesystem::temp_directory_path() / "wormedit-long-source-test.wav";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const auto u32 = [&](std::uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); };
    const auto u16 = [&](std::uint16_t value) { out.write(reinterpret_cast<const char*>(&value), 2); };
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    out.write("data", 4); u32(bytes);
    const std::string silence(bytes, '\0');
    out.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    return path;
}
}

TEST_CASE("long streaming sources keep their decoder open when idle decoders are trimmed",
          "[unit][audio][source-registry][streaming]") {
    using namespace wormedit::audio;
    const auto long_path = write_long_wav();
    decoded_source_registry_t registry{64, 1};
    REQUIRE(registry.register_source({.asset = {100}, .path = long_path,
                                      .source = {"fixture:long", "generated-v1:long"}}).status ==
            source_registration_status_t::registered);
    decoded_block_cache_t cache;
    transport_t transport{cache};
    const auto token = transport.start();
    REQUIRE(token.has_value());
    const audio_source_identity_t long_source{"fixture:long", "generated-v1:long"};
    REQUIRE(registry.decode_block(transport, *token, {long_source, 0}).status ==
            block_decode_status_t::decoded);
    for (std::uint64_t asset = 1; asset <= 4; ++asset) {
        REQUIRE(registry.register_source_pending(pending_registration(asset)).status ==
                source_registration_status_t::pending);
        check_pending_decode(registry, transport, *token, asset);
    }
    // The cap of one idle decoder trims the short sources but never the long one.
    CHECK(registry.open_decoder_count() == 1);
    CHECK(registry.streaming_decoder_count() == 1);
    registry.clear();
    std::filesystem::remove(long_path);
}

namespace {
void race_removed_probe(wormedit::audio::decoded_source_registry_t& registry, std::uint64_t asset) {
    REQUIRE(registry.register_source_pending(pending_registration(asset, "missing-file.ogg")).status ==
            wormedit::audio::source_registration_status_t::pending);
    std::barrier start{2};
    std::thread worker([&] { start.arrive_and_wait(); registry.probe_next_pending(); });
    start.arrive_and_wait();
    registry.clear();
    worker.join();
    CHECK(registry.take_probe_failures().empty());
    CHECK(registry.size() == 0);
}
}
TEST_CASE("cleared pending probes cannot report failures into the replacement inventory",
          "[unit][audio][source-registry][integration-fix]") {
    wormedit::audio::decoded_source_registry_t registry;
    for (std::uint64_t asset = 1; asset <= 200; ++asset) race_removed_probe(registry, asset);
}
TEST_CASE("synchronous source registration tolerates concurrent registry clearing",
          "[unit][audio][source-registry][integration-fix]") {
    wormedit::audio::decoded_source_registry_t registry;
    std::atomic<bool> done{};
    std::thread clearing([&] { while (!done.load()) registry.clear(); });
    for (std::uint64_t asset = 1; asset <= 500; ++asset)
        static_cast<void>(registry.register_source(pending_registration(asset)));
    done = true;
    clearing.join();
    registry.clear();
    CHECK(registry.size() == 0);
    CHECK(registry.open_decoder_count() == 0);
}
