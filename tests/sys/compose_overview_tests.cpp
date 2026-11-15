// Compose overview cache, generation and short-chart anchoring.
#include "frame_timing_fixtures.h"
#include "sys/compose/compose_playhead_cursor.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <variant>

using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("Compose overview BGM presence follows the full cached chart index",
          "[sys][compose-overview]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes = {{.id = object_id_t{100001}, .name = "Autoplay"}};
    for (std::uint64_t id = 2; id <= 20; ++id)
        chart.measures.push_back({.id = measure_id_t{id}});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.has_bgm_events);

    auto& retained_chart = fixture.history.document.charts.front();
    retained_chart.measures.back().bgm_events.push_back({
        .id = object_id_t{100002}, .position = {measure_id_t{20}, 0},
        .source_lane = object_id_t{100001}});
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.has_bgm_events);
    expect_compose_test(fixture.flattened.view.compose.bgm_events.size() == 0);
    const auto* full_index = &*fixture.cache.scene.index.value();
    fixture.project();
    expect_compose_test(&*fixture.cache.scene.index.value() == full_index);
    expect_compose_test(fixture.flattened.view.compose.has_bgm_events);
    expect_compose_test(fixture.flattened.view.compose.bgm_events.size() == 0);

    retained_chart.measures.back().bgm_events.clear();
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.has_bgm_events);
}

namespace {
void project_anchor_scene(compose_test_fixture_t& fixture, int height) {
    fixture.flattened = {};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, height, fixture.interaction, fixture.application, nullptr);
    INFO(fixture.flattened.projection_feedback.size());
    expect_compose_test(fixture.flattened.view.compose.scene_available);
}

void check_short_chart_anchor_frame(compose_test_fixture_t& fixture, int height,
                                     double row, bool playing) {
    project_anchor_scene(fixture, height);
    expect_compose_test(follow_compose_playhead(fixture.history, *fixture.interaction,
                                                row, playing, {}, 0.35));
    project_anchor_scene(fixture, height);
    expect_compose_test(fixture.interaction->cursor.row == (row == 0 ? 0 : 720));
    const auto source_anchor = fixture.interaction->scroll_direction == editor_field_scroll_direction_t::down
        ? fixture.interaction->viewport_height_pixels - *fixture.interaction->fixed_cursor_screen_y_pixels
        : *fixture.interaction->fixed_cursor_screen_y_pixels;
    if (playing && row == 0) {
        INFO("height " << height << " scroll " << fixture.interaction->viewport_scroll_pixels << " anchor " << source_anchor);
        expect_compose_test(std::abs(fixture.interaction->viewport_scroll_pixels + source_anchor) < 1e-9);
    }
    expect_compose_test(fixture.history.undo.empty());
}
} // namespace

TEST_CASE("Configured anchor projects a short chart through audio-tail rows stop and resize",
          "[sys][anchor-scene]") {
    for (const std::uint32_t direction : {1U, 2U}) {
        compose_test_fixture_t fixture;
        fixture.application.preferences.editor_preferences.scroll_directions.fill(direction);
        for (const int height : {980, 640, 1100}) {
            check_short_chart_anchor_frame(fixture, height, 0, true);
            check_short_chart_anchor_frame(fixture, height, 9600, true);
            check_short_chart_anchor_frame(fixture, height, 9600, false);
            check_short_chart_anchor_frame(fixture, height, 0, false);
        }
    }
}

namespace {
struct overview_generation_fixture_t {
    song_document_t song = dense_song();
    compose_scene_cache_t cache;
    frontend_workspace_view_t flattened;
    editor_interaction_state_t interaction;
    audio::waveform_analysis_result_t analysis = overview_analysis(.25f);
    overview_generation_fixture_t() {
        update_timing();
        flattened.view.compose.visible = true;
        flattened.view.compose.scene_available = true;
        flattened.view.compose.viewport_height_pixels = 200;
        flattened.view.compose.pixels_per_standard_measure = 200;
    }
    void update_timing() {
        auto timing = timing_index_t::from(song.charts.front());
        expect_compose_test(timing.value.has_value());
        cache.timing.marker_timing_index = std::move(timing.value);
    }
    auto project(frontend_workspace_view_t& output,
                 const audio::waveform_analysis_result_t* selected = nullptr) -> std::uint64_t {
        expect_compose_test(populate_compose_waveform_projection(output, cache,
            selected ? selected : &analysis, false, true, &interaction));
        expect_compose_test(output.view.compose.waveform_overview_generation != 0);
        return output.view.compose.waveform_overview_generation;
    }
    auto project() -> std::uint64_t { return project(flattened); }
};
} // namespace

TEST_CASE("Compose overview generation ignores playback scroll timing offsets and snapshot row pointers",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto generation = fixture.project();
    const auto fraction = fixture.flattened.view.compose.waveform_overview_visible_begin_fraction;
    fixture.flattened.view.compose.source_playhead_available = true;
    fixture.flattened.view.compose.source_playhead_frame = 96'000;
    fixture.flattened.view.compose.waveform_offset_drag_active = true;
    expect_compose_test(fixture.project() == generation);
    fixture.interaction.viewport_scroll_pixels = 200;
    expect_compose_test(fixture.project() == generation);
    expect_compose_test(fixture.flattened.view.compose.waveform_overview_visible_begin_fraction > fraction);
    auto scene_binding = fixture.cache.scene.index.key().value_or(compose_scene_source_key_t{});
    ++scene_binding.stamp.document.revision;
    static_cast<void>(fixture.cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    fixture.song.charts.front().beat_zero_audio_offset_seconds = {1, 10};
    fixture.update_timing();
    expect_compose_test(fixture.project() == generation);
    frontend_workspace_view_t second;
    second.view.compose = fixture.flattened.view.compose;
    expect_compose_test(fixture.project(second) == generation);
    expect_compose_test(second.view.compose.waveform_overview_rows.data() !=
        fixture.flattened.view.compose.waveform_overview_rows.data());
    expect_compose_test(second.view.compose.waveform_overview_rows ==
        fixture.flattened.view.compose.waveform_overview_rows);
}

TEST_CASE("Compose overview source revision changes generation even when waveform address is reused",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto before = fixture.project();
    const auto* identity = std::get<1>(*fixture.cache.waveform.overview.key());
    fixture.analysis.waveform->key.source.source_identity = "content:v2";
    for (auto& bin : fixture.analysis.waveform->bins) bin.absolute_peak = {.75f, .75f};
    const auto replacement = fixture.project();
    expect_compose_test(std::get<1>(*fixture.cache.waveform.overview.key()) == identity);
    expect_compose_test(replacement > before);
    expect_compose_test(fixture.flattened.view.compose.waveform_overview_rows.front().absolute_peak == .75f);
    expect_compose_test(fixture.project() == replacement);
}

TEST_CASE("Compose overview detail identity and cleared cache publish unique generations",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto initial = fixture.project();
    fixture.analysis.waveform->key.frames_per_bin = 128;
    fixture.analysis.waveform->total_frames = 128 * 4096;
    for (auto& bin : fixture.analysis.waveform->bins) bin.frame_count = 128;
    const auto detail = fixture.project();
    expect_compose_test(detail > initial);
    auto replacement = fixture.analysis;
    const auto identity = fixture.project(fixture.flattened, &replacement);
    expect_compose_test(identity > detail);
    fixture.cache = {};
    fixture.update_timing();
    const auto cleared = fixture.project(fixture.flattened, &replacement);
    expect_compose_test(cleared > identity);
    expect_compose_test(fixture.project(fixture.flattened, &replacement) == cleared);
}
