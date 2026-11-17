// Compose waveform and envelope projection, caches and the affine waveform clock.
#include "frame_timing_fixtures.h"
#include "sys/app/application_defaults.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

TEST_CASE("Waveform runtime reports decoder failures instead of silently losing the waveform",
          "[sys][waveform-projection]") {
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    notification_center_t center;
    notification_sink_t sink{center};
    runtime::waveform_analysis_t waveform(sources, nullptr, sink);
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{42};
    compose_scene_cache_t cache;
    song_document_t document;
    waveform.service(document, workspace, cache, nullptr);
    for (std::size_t attempt = 0; attempt < 200 && waveform.waveform_job().is_busy(); ++attempt) {
        SDL_Delay(5);
        waveform.service(document, workspace, cache, nullptr);
    }
    expect_compose_test(!waveform.waveform_job().is_busy());
    center.drain_inbox(std::chrono::seconds{1});
    const auto notifications = center.snapshot(10);
    expect_compose_test(notifications.size() == 1);
    expect_compose_test(notifications.front().message == "Waveform analysis failed: the audio source is unavailable.");
    expect_compose_test(notifications.front().severity == notification_severity_t::error);
}

namespace {
void check_overview_replacement(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const editor_interaction_state_t& interaction) {
    auto replacement = overview_analysis(0.75f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &replacement, false,
                                                 true, &interaction));
    CHECK(flattened.view.compose.waveform_overview_rows.front().absolute_peak == 0.75f);
    auto scene_binding = cache.scene.index.key().value_or(compose_scene_source_key_t{});
    ++scene_binding.stamp.document.revision;
    static_cast<void>(cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &replacement, false,
                                                 true, &interaction));
}
} // namespace

TEST_CASE("Compose overview keeps cached rows while moving its visible range", "[sys][waveform-projection]") {
    auto history = document_history_t::from(dense_song());
    compose_scene_cache_t cache;
    auto timing = timing_index_t::from(history.document.charts.front());
    REQUIRE(timing.value);
    cache.timing.marker_timing_index = std::move(timing.value);
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 200;
    flattened.view.compose.pixels_per_standard_measure = 200;
    editor_interaction_state_t interaction;
    auto analysis = overview_analysis(0.25f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false, true, &interaction));
    const auto fraction = flattened.view.compose.waveform_overview_visible_begin_fraction;
    const auto* rows = cache.waveform.overview.value().rows.data();
    interaction.viewport_scroll_pixels = 200;
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false, true, &interaction));
    CHECK(cache.waveform.overview.value().rows.data() == rows);
    CHECK(flattened.view.compose.waveform_overview_visible_begin_fraction > fraction);
    check_overview_replacement(flattened, cache, interaction);
    flattened.view.compose.pixels_per_standard_measure = 0;
    CHECK_FALSE(populate_compose_waveform_projection(flattened, cache, &analysis, false,
                                                      true, &interaction));
}

TEST_CASE("Compose waveform remains visible in a padded one-measure viewport", "[sys][waveform-projection]") {
    auto song = dense_song();
    song.charts.front().measures.resize(1);
    compose_scene_cache_t cache;
    cache.timing.marker_timing_index = timing_index_t::from(song.charts.front()).value;
    REQUIRE(cache.timing.marker_timing_index);
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 800;
    flattened.view.compose.pixels_per_standard_measure = 192;
    editor_interaction_state_t interaction;
    interaction.viewport_scroll_pixels = -160;
    auto analysis = overview_analysis(0.5f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false,
                                                true, &interaction));
    REQUIRE(flattened.view.compose.waveform_rows.size() > 0);
    CHECK(flattened.view.compose.waveform_rows.front().y_pixels >= 160);
    CHECK(flattened.view.compose.waveform_rows.front().absolute_peak[0] == 0.5f);
}

TEST_CASE("Compose envelope projects only exposed stable world rows during follow", "[sys][waveform-projection]") {
    auto history = document_history_t::from(dense_song());
    compose_scene_cache_t cache;
    cache.timing.marker_timing_index = timing_index_t::from(history.document.charts.front()).value;
    expect_gpu_work(cache.timing.marker_timing_index.has_value());
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 200;
    flattened.view.compose.pixels_per_standard_measure = 200;
    editor_interaction_state_t interaction;
    interaction.viewport_scroll_pixels = 10;
    auto analysis = overview_analysis(.25f);
    audio::decoded_block_cache_t blocks;
    const audio::aligned_waveform_style_t style{};
    const auto project = [&] {
        flattened.view.compose.waveform_rows.clear();
        return populate_compose_waveform_projection(flattened, cache, &analysis, false, true,
                                                    &interaction, nullptr, &blocks, &style);
    };
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projection_calls == 1);
    interaction.viewport_scroll_pixels = 10.75;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().reused_rows == 202);
    interaction.viewport_scroll_pixels = 11.25;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 1);
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().reused_rows == 201);
    const audio::audio_block_key_t key{{"unrelated", "immutable"}, 0};
    std::array<float, 16> pcm{};
    expect_gpu_work(blocks.store(key, 8, pcm).has_value());
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    interaction.viewport_scroll_pixels = 10.75;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 1);
    interaction.viewport_scroll_pixels = 2000;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
    auto scene_binding = cache.scene.index.key().value_or(compose_scene_source_key_t{});
    ++scene_binding.stamp.document.revision;
    static_cast<void>(cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
}

namespace {
void check_waveform_axis(envelope_projection_fixture_t& fixture, bool sample_points) {
    auto& view = fixture.flattened.view.compose;
    view.downscroll = false;
    fixture.project();
    const auto& source = fixture.flattened.view.compose.waveform_rows;
    const std::vector<wormedit::ui::compose_waveform_row_view_t> upscroll(source.begin(), source.end());
    expect_gpu_work(!upscroll.empty());
    view.downscroll = true;
    fixture.project();
    const auto& downscroll = fixture.flattened.view.compose.waveform_rows;
    expect_gpu_work(downscroll.size() == upscroll.size());
    for (std::size_t index = 0; index < upscroll.size(); ++index) {
        const auto& before = upscroll[index];
        const auto& after = downscroll[index];
        expect_gpu_work(before.sample_point == sample_points && after.sample_point == sample_points);
        expect_gpu_work(std::ranges::equal(after.absolute_peak, before.absolute_peak) &&
                        std::ranges::equal(after.samples, before.samples));
        expect_gpu_work(std::abs(after.y_pixels + before.y_pixels - view.viewport_height_pixels) < 1e-8);
    }
}

void check_waveform_scroll(envelope_projection_fixture_t& fixture) {
    const auto before = fixture.flattened.view.compose.waveform_rows[80];
    const auto& view = fixture.flattened.view.compose;
    const auto pixels_per_row = view.pixels_per_standard_measure /
        fixture.cache.timing.marker_timing_index->divisions_per_standard_measure;
    const auto world_row = (fixture.interaction.viewport_scroll_pixels +
        view.viewport_height_pixels - before.y_pixels) / pixels_per_row;
    fixture.interaction.viewport_scroll_pixels += 25;
    fixture.project();
    const auto& after = fixture.flattened.view.compose.waveform_rows[55];
    expect_gpu_work(std::ranges::equal(after.absolute_peak, before.absolute_peak));
    expect_gpu_work(after.y_pixels == before.y_pixels + 25);
    const auto chart_y = compose_project_y(world_row, pixels_per_row,
        fixture.interaction.viewport_scroll_pixels, fixture.flattened.view.compose.viewport_height_pixels, true);
    expect_gpu_work(std::abs(chart_y - after.y_pixels) < 1e-8);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 25);
}

void prepare_seven_key_mp3_project(envelope_projection_fixture_t& fixture) {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "fixtures" / "audio" / "sliced-seek-source.mp3";
    audio::decoder_t decoder;
    expect_gpu_work(decoder.open(path));
    const auto info = decoder.info();
    expect_gpu_work(info.has_value());
    const auto song = make_audio_project_document(song_id_t{42}, path, "7K MP3", "", 7,
        {info->source_sample_rate, info->source_channel_count, info->source_frame_count, info->seekable});
    expect_gpu_work(song.has_value());
    fixture.history = document_history_t::from(*song);
    const auto& chart = fixture.history.document.charts.front();
    expect_gpu_work(chart.lane_layouts.front().layout.lanes.size() == 7);
    fixture.cache = {};
    static_cast<void>(fixture.cache.scene.index.get_or_build(compose_scene_source_key_t{
        .stamp = {.document = {.revision = fixture.history.document.revision}, .chart = chart.id}},
        retained_scene_fixture_t{}));
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    fixture.flattened.view.compose.downscroll = compose_field_downscroll(chart);
    expect_gpu_work(fixture.flattened.view.compose.downscroll);
    audio::waveform_cache_t waveforms;
    fixture.analysis = audio::waveform_analysis_result_t::from(
        audio::waveform_analysis_t::from(decoder, fixture.blocks, waveforms,
            {"fixture:7k-mp3", "immutable:v1"}, 256));
    expect_gpu_work(fixture.analysis.waveform.has_value());
}
} // namespace

TEST_CASE("MP3 project 7K waveform follows downscroll chart movement while reusing cached rows",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    prepare_seven_key_mp3_project(fixture);
    check_waveform_axis(fixture, false);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    check_waveform_scroll(fixture);
}

TEST_CASE("Compose waveform sample points follow the downscroll chart axis",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    prepare_waveform_sample_points(fixture);
    check_waveform_axis(fixture, true);
}

TEST_CASE("Incomplete envelope detail retries after PCM arrives and retains resolved rows",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 32;
    fixture.flattened.view.compose.pixels_per_standard_measure = 32000;
    fixture.style.detail_level = true;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 34);
    expect_gpu_work(std::ranges::any_of(fixture.cache.waveform.waveform_envelope_ring.value().rows,
        [](const auto& row) { return row.incomplete; }));
    const auto coarse_peak = fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0];
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    const audio::audio_block_key_t key{fixture.analysis.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .8f);
    expect_gpu_work(fixture.blocks.store(key, 16384, pcm).has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 34);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projection_calls == 1);
    expect_gpu_work(std::ranges::all_of(fixture.cache.waveform.waveform_envelope_ring.value().rows,
        [](const auto& row) { return row.present && !row.incomplete; }));
    const auto& resolved = fixture.flattened.view.compose.waveform_rows.front();
    expect_gpu_work(std::abs(resolved.absolute_peak[0] - .8f) < .00001f);
    expect_gpu_work(std::abs(resolved.rms[0] - .8f) < .00001f);
    expect_gpu_work(resolved.absolute_peak[0] != coarse_peak);
    const audio::audio_block_key_t unrelated{{"elsewhere", "unchanged"}, 0};
    expect_gpu_work(fixture.blocks.store(unrelated, 16384, pcm).has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().reused_rows == 34);
    expect_gpu_work(std::abs(fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0] - .8f) < .00001f);
}

TEST_CASE("Compose envelope chart identity changes recompute actual source timing at unchanged revision",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.project();
    compare_cached_envelope_source(fixture);
    expect_gpu_work(!fixture.flattened.view.compose.waveform_rows.empty());
    const auto peak = fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0];
    auto second = fixture.history.document.charts.front();
    second.id = chart_id_t{2};
    second.bpms.front().beats_per_minute = {240, 1};
    auto scene_binding = fixture.cache.scene.index.key().value_or(compose_scene_source_key_t{});
    scene_binding.stamp.chart = second.id;
    static_cast<void>(fixture.cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    fixture.cache.timing.marker_timing_index = timing_index_t::from(second).value;
    expect_gpu_work(fixture.cache.timing.marker_timing_index.has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
    compare_cached_envelope_source(fixture);
    expect_gpu_work(fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0] < peak);
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    ++fixture.flattened.view.compose.project_id.value;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
}

TEST_CASE("Compose envelope padding stays private at zero fractional and normal viewport scroll",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 532;
    for (const auto scroll : {0.0, .75, 10.0, 10.75, 100.25}) {
        fixture.interaction.viewport_scroll_pixels = scroll;
        fixture.project();
        expect_published_waveform_rows(fixture.flattened.view.compose.waveform_rows, 532);
        expect_gpu_work(fixture.flattened.view.compose.waveform_rows.size() <= 532);
    }
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
}

namespace {
auto reference_frame(const timing_index_t& timing, long double row, bool end,
                     const audio::waveform_t& waveform) -> std::uint64_t {
    const auto integer = static_cast<std::uint64_t>(std::floor(row));
    const auto fraction = row - integer;
    const auto first = timing.seconds_at(integer,
        fraction == 0 && !end ? timing_boundary_side_t::before_delays
                             : timing_boundary_side_t::after_stops);
    auto seconds = static_cast<long double>(first.seconds->numerator) / first.seconds->denominator;
    if (fraction != 0) {
        const auto next = timing.seconds_at(integer + 1, timing_boundary_side_t::before_delays);
        seconds += (static_cast<long double>(next.seconds->numerator) / next.seconds->denominator - seconds) * fraction;
    }
    return static_cast<std::uint64_t>(std::clamp(end ? std::ceil(seconds * waveform.sample_rate)
                                                   : std::floor(seconds * waveform.sample_rate),
                                               0.0L, static_cast<long double>(waveform.total_frames)));
}

auto discontinuous_timing_fixture() -> timing_index_t {
    auto chart = dense_song().charts.front();
    chart.pauses = {{.id = object_id_t{60000}, .position = {measure_id_t{1}, 240},
                     .duration = {1, 3}},
                    {.id = object_id_t{60001}, .position = {measure_id_t{1}, 480},
                     .kind = pause_kind_t::delay, .duration = {1, 7}}};
    chart.bpms.push_back({.id = object_id_t{60002}, .position = {measure_id_t{1}, 600},
                         .beats_per_minute = {137, 1}});
    chart.warps = {{.id = object_id_t{60003},
                    .span = {{measure_id_t{1}, 720}, {measure_id_t{2}, 120}}}};
    return *timing_index_t::from(chart).value;
}

void check_clock_projection(const audio::waveform_t& waveform, const timing_index_t& timing,
                            double scroll, bool descending = false) {
    audio::aligned_waveform_request_t request{.viewport_scroll_pixels = scroll,
        .viewport_height_pixels = 64, .pixels_per_standard_measure = 192};
    const auto result = audio::aligned_waveform_view_result_t::from(waveform, nullptr, timing,
                                                               request, nullptr, {});
    REQUIRE(!result.status.has_failure());
    for (const auto& row : result.rows) {
        const auto pixel = std::floor(row.y_pixels);
        const auto begin = reference_frame(timing, (scroll + pixel) * 5, false, waveform);
        const auto end = reference_frame(timing, (scroll + pixel + 1) * 5, true, waveform);
        REQUIRE(end > begin);
        const auto peak_bin = (descending ? begin : end - 1) / waveform.key.frames_per_bin;
        CHECK(row.absolute_peak[0] == Catch::Approx(waveform.bins[peak_bin].absolute_peak[0]).margin(0.000001));
    }
}
} // namespace

TEST_CASE("Affine waveform clock preserves BPM pause warp boundaries during600 playback frames", "[sys][waveform-projection]") {
    const auto timing = discontinuous_timing_fixture();
    auto analysis = overview_analysis(0.5f);
    auto& waveform = *analysis.waveform;
    for (std::size_t index = 0; index < waveform.bins.size(); ++index)
        waveform.bins[index].absolute_peak = {static_cast<float>(index + 1) / waveform.bins.size(), 0};
    for (unsigned frame = 0; frame < 600; ++frame)
        check_clock_projection(waveform, timing, frame * 0.75);
    for (std::size_t index = 0; index < waveform.bins.size(); ++index)
        waveform.bins[index].absolute_peak = {1.0f - static_cast<float>(index) / waveform.bins.size(), 0};
    for (unsigned frame = 0; frame < 600; ++frame)
        check_clock_projection(waveform, timing, frame * 0.75, true);
}
#endif
