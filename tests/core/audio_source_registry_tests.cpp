#include <wormedit/audio/object_audition.h>
#include <wormedit/audio/source_registry.h>

#include <catch2/catch_test_macros.hpp>

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
    CHECK(cache.contains(key));

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

TEST_CASE("pending registration opens no decoder until a probe runs",
          "[unit][audio][source-registry][lazy]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry{2'048};
    for (std::uint64_t asset = 1; asset <= 1'000; ++asset) {
        const auto added = registry.register_source_pending(pending_registration(asset));
        REQUIRE(added.status == source_registration_status_t::pending);
        CHECK_FALSE(added.binding.has_value());
    }
    CHECK(registry.size() == 1'000);
    CHECK(registry.pending_count() == 1'000);
    CHECK(registry.open_decoder_count() == 0);
    CHECK(registry.bindings().empty());
    CHECK_FALSE(registry.binding(wormedit::asset_id_t{1}));

    CHECK(registry.register_source_pending(pending_registration(1)).status ==
          source_registration_status_t::duplicate_asset);

    std::size_t probed = 0;
    while (registry.probe_next_pending()) {
        ++probed;
        CHECK(registry.open_decoder_count() == 0);
    }
    CHECK(probed == 1'000);
    CHECK(registry.pending_count() == 0);
    CHECK(registry.bindings().size() == 1'000);
    CHECK(registry.binding(wormedit::asset_id_t{500})->output_frame_count > 0);
    CHECK(registry.take_probe_failures().empty());
}

TEST_CASE("probe failures are reported once and keep the source out of bindings",
          "[unit][audio][source-registry][lazy]") {
    using namespace wormedit::audio;
    decoded_source_registry_t registry;
    REQUIRE(registry.register_source_pending(pending_registration(1)).status ==
            source_registration_status_t::pending);
    REQUIRE(registry.register_source_pending(pending_registration(2, "missing-file.ogg")).status ==
            source_registration_status_t::pending);
    registry.await_pending();

    CHECK(registry.binding(wormedit::asset_id_t{1}).has_value());
    CHECK_FALSE(registry.binding(wormedit::asset_id_t{2}).has_value());
    CHECK(registry.bindings().size() == 1);
    const auto failures = registry.take_probe_failures();
    REQUIRE(failures.size() == 1);
    CHECK(failures.front().asset == wormedit::asset_id_t{2});
    CHECK_FALSE(failures.front().message.empty());
    CHECK(registry.take_probe_failures().empty());
    CHECK(registry.probe(wormedit::asset_id_t{2}).status == source_registration_status_t::open_failed);
    CHECK(registry.take_probe_failures().empty());
}

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
        const audio_block_key_t key{{"fixture:asset:" + std::to_string(asset),
                                     "pending-v1:" + std::to_string(asset)},
                                    0};
        const auto decoded = registry.decode_block(transport, *token, key);
        CHECK(decoded.status == block_decode_status_t::decoded);
        CHECK(registry.binding(wormedit::asset_id_t{asset}).has_value());
        CHECK(registry.open_decoder_count() <= 4);
    }
    CHECK(registry.open_decoder_count() == 4);
    registry.clear();
    CHECK(registry.open_decoder_count() == 0);
}
