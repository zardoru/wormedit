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
