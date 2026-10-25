#include "sys/audio/bmson_waveform_view.h"
#include "sys/project/workspace_view.h"
#include "project_audio_sources_fixture.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
auto bmson_row_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    auto& chart = song.charts.emplace_back();
    chart.id = chart_id_t{1};
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}};
    chart.bpms = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
        .beats_per_minute = {120, 1}}};
    chart.sound_channels = {{.id = object_id_t{10}, .source_clip = {.source = asset_id_t{1}},
        .events = {{.id = object_id_t{11}, .position = {measure_id_t{1}, 0}, .playable = false}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{12}, .stem_channel = object_id_t{10}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{13}, .position = {measure_id_t{1}, 0},
        .source_lane = object_id_t{12}, .sound_channel_event = object_id_t{11}}};
    return song;
}

struct bmson_rows_fixture_t {
    document_history_t history{document_history_t::from(bmson_row_song())};
    workspace_controller_t workspace;
    editor_interaction_state_t interaction;
    compose_scene_cache_t cache;
    frontend_workspace_view_t flattened;
    project_audio_sources_t sources;
    std::optional<asset_id_t> asset{asset_id_t{1}};
    std::shared_ptr<const audio::waveform_analysis_result_t> analysis;
    std::vector<const sound_channel_t*> visible;

    bmson_rows_fixture_t() {
        workspace.selected_chart = chart_id_t{1};
        interaction.chart = chart_id_t{1};
        interaction.cursor = {measure_id_t{1}, 0};
        auto& chart = history.document.charts.front();
        cache.timing.marker_timing_index = timing_index_t::from(chart).value;
        auto& view = flattened.view.compose;
        view.visible = view.scene_available = true;
        view.project_id = history.document.id;
        view.viewport_height_pixels = 200;
        view.pixels_per_standard_measure = 200;
        visible = {&chart.sound_channels.front()};
        const auto path = std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio/keysound-c4.ogg";
        const auto binding = sources.sources.create_source({asset_id_t{1}, path, {"bmson-row", "v1"}});
        REQUIRE(binding);
        history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = path,
            .stream = {binding->source_sample_rate, 2, binding->source_frame_count, true}});
        retained_sources_for(history.document, sources);
        cache.stems.source_bindings = {*binding};
        auto ready = std::make_shared<audio::waveform_analysis_result_t>();
        ready->waveform.emplace();
        ready->waveform->key = {.source = binding->source, .frames_per_bin = 256};
        ready->waveform->total_frames = binding->output_frame_count;
        ready->waveform->sample_rate = binding->output_sample_rate;
        ready->waveform->bins.resize((binding->output_frame_count + 255) / 256,
            {.absolute_peak = {.25F, .25F}, .rms = {.125F, .125F}, .frame_count = 256});
        analysis = std::move(ready);
    }

    void project() {
        populate_bmson_waveform_projection({flattened, history, workspace, &interaction, cache,
            sources, {asset, analysis}, {asset, analysis}, sources.generation,
            [this](asset_id_t) { return analysis; },
            std::span<const sound_channel_t* const>{visible}});
    }
};
} // namespace

TEST_CASE("BMSON outer waveform rows remain bound through stable and playhead frames", "[bmson][projection-cache]") {
    bmson_rows_fixture_t fixture;
    fixture.project();
    REQUIRE_FALSE(fixture.flattened.view.compose.stem_waveforms.empty());
    REQUIRE_FALSE(fixture.flattened.view.compose.stem_waveforms.front().rows.empty());
    const auto* rows = fixture.flattened.view.compose.stem_waveforms.front().rows.data();
    const auto refills = fixture.cache.stems.row_refills;
    for (unsigned frame = 0; frame < 144; ++frame) {
        fixture.flattened.view.transport_frame = frame * 300;
        fixture.project();
    }
    CHECK(fixture.cache.stems.row_refills == refills);
    CHECK(fixture.flattened.view.compose.stem_waveforms.front().rows.data() == rows);
}

TEST_CASE("BMSON outer rows observe asynchronous immutable analysis replacement", "[bmson][projection-cache]") {
    bmson_rows_fixture_t fixture;
    fixture.analysis.reset();
    fixture.project();
    const auto pending = fixture.cache.stems.row_refills;
    const auto ready = std::make_shared<audio::waveform_analysis_result_t>();
    fixture.analysis = ready;
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == pending + 1);
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == pending + 1);
    fixture.analysis = std::make_shared<audio::waveform_analysis_result_t>(*ready);
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == pending + 2);
}

TEST_CASE("BMSON outer rows invalidate actual geometry binding selection and lifecycle changes", "[bmson][projection-cache]") {
    bmson_rows_fixture_t fixture;
    fixture.project();
    auto refills = fixture.cache.stems.row_refills;
    fixture.interaction.viewport_scroll_pixels = 1;
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == ++refills);
    ++fixture.cache.stems.source_bindings.front().output_frame_count;
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == ++refills);
    fixture.flattened.view.compose.selected_bmson_bgm_event_id = object_id_t{13};
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == ++refills);
    ++fixture.flattened.compose.bmson_rows_epoch;
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == ++refills);
    ++fixture.history.document.revision;
    retained_sources_for(fixture.history.document, fixture.sources);
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == ++refills);
}

TEST_CASE("BMSON retained selected-channel rows republish the reset lane scalar", "[bmson][projection-cache]") {
    bmson_rows_fixture_t fixture;
    fixture.flattened.view.compose.selected_bmson_bgm_event_id = object_id_t{13};
    fixture.project();
    REQUIRE_FALSE(fixture.flattened.view.compose.channel_waveform_rows.empty());
    const auto lane = fixture.flattened.view.compose.channel_waveform_source_lane_id;
    REQUIRE(lane.value != 0);
    const auto refills = fixture.cache.stems.row_refills;
    fixture.flattened.view.compose.channel_waveform_source_lane_id = {};
    fixture.project();
    CHECK(fixture.cache.stems.row_refills == refills);
    CHECK(fixture.flattened.view.compose.channel_waveform_source_lane_id == lane);
}
