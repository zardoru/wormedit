#include <wormedit/audio/object_audition.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <string>

namespace {

auto fixture_song() -> wormedit::song_document_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                             "published-vox-slicing.bmson",
                         std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    const auto parsed =
        wormedit::parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    REQUIRE(parsed.value.has_value());
    return *parsed.value;
}

auto binding(wormedit::asset_id_t asset) -> wormedit::audio::audition_source_binding_t {
    return {.asset = asset,
            .source = {"fixture:vox", "decoded:v1"},
            .source_sample_rate = 48'000,
            .source_frame_count = std::nullopt,
            .output_sample_rate = wormedit::audio::output_sample_rate,
            .output_frame_count = 200'000};
}

auto first_note_id(const wormedit::chart_document_t& chart) -> wormedit::object_id_t {
    for (const auto& measure : chart.measures) {
        if (!measure.notes.empty())
            return measure.notes.front().id;
    }
    return {};
}

void require_stem_preview(bool condition) { REQUIRE(condition); }
void expect_stem_preview(bool condition) { CHECK(condition); }
void reject_stem_preview(bool condition) { CHECK_FALSE(condition); }

void check_rejected_object_audition(
    const wormedit::audio::object_audition_outcome_t& rejected,
    const wormedit::audio::prepared_object_audition_t& expected) {
    using namespace wormedit::audio;
    CHECK(rejected.error().value.status == object_audition_status_t::transport_failed);
    REQUIRE(rejected.error().value.prepared.has_value());
    CHECK(*rejected.error().value.prepared == expected);
    REQUIRE(rejected.error().value.transport.has_value());
    CHECK(rejected.error().value.transport->status == audition_start_status_t::invalid_clip);
    CHECK(rejected.error().diagnostics.empty());
}

} // namespace

TEST_CASE("object audition prepares before replacing transport and rejects stale requests",
          "[unit][audio][object-audition][generation]") {
    using namespace wormedit::audio;
    auto song = fixture_song();
    auto& chart = song.charts.front();
    const auto asset = chart.sound_channels.front().source_clip.source;
    const std::array bindings{binding(asset)};
    decoded_block_cache_t cache;
    transport_t transport{cache, 4};
    audition_transport_bridge_t bridge{transport};
    object_audition_controller_t controller{bridge};
    const object_audition_request_t request{song.revision, chart.id, first_note_id(chart)};

    auto prepared = controller.prepare(song, request, bindings);
    REQUIRE(prepared);
    CHECK(prepared.status == object_audition_status_t::prepared);
    CHECK_FALSE(bridge.session_token().has_value());
    REQUIRE(prepared.prepared.has_value());
    const auto started = controller.start(std::move(*prepared.prepared));
    REQUIRE(started);
    CHECK(started.status == object_audition_status_t::started);
    REQUIRE(bridge.session_token().has_value());
    const auto active = *bridge.session_token();

    const auto stale =
        controller.prepare(song, {song.revision + 1, chart.id, request.object}, bindings);
    CHECK_FALSE(stale);
    CHECK(stale.status == object_audition_status_t::stale_document);
    CHECK(bridge.session_token() == active);
    CHECK(transport.is_current(active));

    const auto missing =
        controller.start(song, {song.revision, chart.id, wormedit::object_id_t{999'999}}, bindings);
    CHECK_FALSE(missing);
    CHECK(missing.status == object_audition_status_t::resolve_failed);
    CHECK(bridge.session_token() == active);
    CHECK(transport.is_current(active));
}

TEST_CASE("object audition forwards exact BGM clips and Stop through the bridge",
          "[unit][audio][object-audition][bgm][stop]") {
    using namespace wormedit::audio;
    auto song = fixture_song();
    auto& chart = song.charts.front();
    const auto asset = chart.sound_channels.front().source_clip.source;
    const std::array bindings{binding(asset)};
    const auto bgm_measure = std::ranges::find_if(
        chart.measures, [](const auto& measure) { return !measure.bgm_events.empty(); });
    REQUIRE(bgm_measure != chart.measures.end());

    decoded_block_cache_t cache;
    transport_t transport{cache, 2};
    audition_transport_bridge_t bridge{transport};
    object_audition_controller_t controller{bridge};
    const auto started = controller.start(
        song, {song.revision, chart.id, bgm_measure->bgm_events.front().id}, bindings);
    REQUIRE(started);
    REQUIRE(started.prepared.has_value());
    REQUIRE(started.prepared->clips.size() == 1);
    CHECK(started.prepared->clips.front().source_start_frame == 88'200);
    CHECK(started.prepared->clips.front().frame_count == 111'800);
    CHECK(controller.stop().scheduled_voices == 1);
    CHECK_FALSE(bridge.session_token().has_value());
}

TEST_CASE("object audition expected rejection retains prepared clips and transport details",
          "[unit][audio][object-audition][result]") {
    using namespace wormedit::audio;
    decoded_block_cache_t cache;
    transport_t transport{cache, 2};
    audition_transport_bridge_t bridge{transport};
    object_audition_controller_t controller{bridge};
    prepared_object_audition_t prepared{
        .request = {7, wormedit::chart_id_t{2}, wormedit::object_id_t{3}},
        .clips = {{{"fixture:source", "decoded:v1"}, 12, 0, 0.5F}}};
    const auto expected = prepared;
    auto rejected = controller.start_prepared(std::move(prepared));
    REQUIRE_FALSE(rejected.has_value());
    check_rejected_object_audition(rejected, expected);
    CHECK_FALSE(bridge.session_token().has_value());
    const auto legacy = object_audition_result_t::from(std::move(rejected));
    CHECK_FALSE(legacy);
    CHECK(legacy.prepared == expected);
    CHECK(legacy.transport->status == audition_start_status_t::invalid_clip);
}

TEST_CASE("Stem click audition bounds decoded frames without changing offsets or shorter clips",
          "[unit][audio][object-audition][stem-preview]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    song_document_t song;
    song.resources = {{.id = resource_id_t{1}, .asset = asset_id_t{1}, .sound_kind = sound_kind_t::stem}};
    auto source = binding(asset_id_t{1});
    const std::array sources{source};
    std::vector<audition_clip_t> clips{{source.source, 1000, 199000, 1.0F},
                                      {source.source, 500, 1500, 0.5F}};
    require_stem_preview(is_bound_stem_audition(song, resource_id_t{1}, sources, clips));
    expect_stem_preview(clips[0].source_start_frame == 1000);
    expect_stem_preview(clips[0].frame_count == 2ULL * source.output_sample_rate);
    expect_stem_preview(clips[1].source_start_frame == 500);
    expect_stem_preview(clips[1].frame_count == 1500);
    expect_stem_preview(clips[1].gain == 0.5F);
    song.resources.front().sound_kind = sound_kind_t::keysound;
    clips[0].frame_count = 200000;
    require_stem_preview(is_bound_stem_audition(song, resource_id_t{1}, sources, clips));
    expect_stem_preview(clips[0].frame_count == 200000);
}

TEST_CASE("Stem click audition rejects missing or zero-rate decoded sources",
          "[unit][audio][object-audition][stem-preview]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    song_document_t song;
    song.resources = {{.id = resource_id_t{1}, .sound_kind = sound_kind_t::stem}};
    auto source = binding(asset_id_t{1});
    std::array sources{source};
    std::array clips{audition_clip_t{source.source, 100, 199900, 1.0F}};
    reject_stem_preview(is_bound_stem_audition(song, resource_id_t{1}, {}, clips));
    sources[0].output_sample_rate = 0;
    reject_stem_preview(is_bound_stem_audition(song, resource_id_t{1}, sources, clips));
    sources[0].output_sample_rate = 8000;
    require_stem_preview(is_bound_stem_audition(song, resource_id_t{1}, sources, clips));
    expect_stem_preview(clips[0].frame_count == 16000);
}
