#include "compose_scene_cache_fixture.h"
#include "../sys/application_dispatch_fixture.h"
#include "sys/compose_input.h"
#include "sys/waveform_view.h"

#include <wormedit/core/timing.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
using namespace wormedit;
using namespace wormedit::sys;

auto centered(double begin, double end) -> bgm_bar_range_t {
    return bgm_bar_center_on_visible_span({0.4, 0.6}, begin, end, 0.01);
}

void check_centered_range(bgm_bar_range_t range, double begin, double end) {
    CHECK(range.begin == Catch::Approx(begin));
    CHECK(range.end == Catch::Approx(end));
}

void require_minimap(bool condition) { REQUIRE(condition); }
void check_minimap(bool condition) { CHECK(condition); }

auto make_timing_index(const chart_document_t& chart) -> timing_index_t {
    return *timing_index_t::from(chart).value;
}

auto minimap_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{7}, .divisions_per_standard_measure = 960,
        .lane_layouts = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
            .layout = {.profile_id = "minimap-test", .display_name = "Minimap",
                       .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
        .bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                  .beats_per_minute = {120, 1}}}};
    for (std::uint64_t index = 0; index < 8; ++index)
        chart.measures.push_back({.id = measure_id_t{index + 1}});
    return chart;
}

auto waveform_analysis() -> audio::waveform_analysis_result_t {
    audio::waveform_t waveform;
    waveform.key = {{"fake:minimap-center", "content:v1"}, 256, 0, 0};
    waveform.sample_rate = audio::output_sample_rate;
    waveform.total_frames = static_cast<std::uint64_t>(waveform.sample_rate) * 16;
    waveform.bins.resize(1 + (waveform.total_frames - 1) / 256);
    auto frames_left = waveform.total_frames;
    for (auto& bin : waveform.bins) {
        bin.minimum = {-0.25f, -0.25f};
        bin.maximum = {0.25f, 0.25f};
        bin.absolute_peak = {0.25f, 0.25f};
        bin.frame_count = std::min<std::uint64_t>(256, frames_left);
        frames_left -= bin.frame_count;
    }
    return {.waveform = std::move(waveform)};
}

struct projection_fixture_t {
    chart_document_t chart{minimap_chart()};
    timing_index_t timing{make_timing_index(chart)};
    compose_scene_cache_t cache;
    frontend_workspace_view_t flattened;
    editor_interaction_state_t interaction;
    audio::waveform_analysis_result_t analysis{waveform_analysis()};
    std::optional<bgm_bar_pan_override_t> pan_override;
    std::optional<bgm_bar_window_capture_t> window_capture;
    bgm_bar_range_t range{0.2, 0.6};

    projection_fixture_t() {
        cache.timing.marker_timing_index = timing;
        static_cast<void>(cache.scene.index.get_or_build(compose_scene_source_key_t{
            .stamp = {.document = {.revision = 5}, .chart = chart.id}}, retained_scene_fixture_t{}));
        flattened.view.compose.visible = true;
        flattened.view.compose.scene_available = true;
        flattened.view.compose.project_id.value = 1;
        flattened.view.compose.chart_id.value = chart.id.value;
        flattened.view.compose.document_revision = cache.stamp().document.revision;
        flattened.view.compose.selected_audio_asset_id.value = 12;
        flattened.view.compose.viewport_height_pixels = 200;
        flattened.view.compose.pixels_per_standard_measure = 200;
    }

    auto project() -> bool {
        return populate_compose_waveform_projection(flattened, cache, &analysis, false, true,
            &interaction, nullptr, nullptr, nullptr, range, &pan_override, &window_capture);
    }
};

auto midpoint(const wormedit::ui::compose_view_t& view) -> double {
    return (view.waveform_overview_visible_begin_fraction +
            view.waveform_overview_visible_end_fraction) * 0.5;
}
} // namespace

TEST_CASE("zoomed minimap centers its span and clamps at both chart boundaries",
          "[frame-timing][minimap-center]") {
    check_centered_range(centered(0.4, 0.6), 0.4, 0.6);
    check_centered_range(centered(0.0, 0.1), 0.0, 0.2);
    check_centered_range(centered(0.9, 1.0), 0.8, 1.0);
    const auto zoomed = bgm_bar_zoom_centered({0.0, 1.0}, 0.5, 2.0, 0.01);
    CHECK((zoomed.begin + zoomed.end) * 0.5 == Catch::Approx(0.5));
    CHECK(zoomed.end - zoomed.begin == Catch::Approx(1.0 / (bgm_bar_zoom_step * bgm_bar_zoom_step)));
}

TEST_CASE("minimap projection tracks visible field span in either scroll direction",
          "[frame-timing][minimap-center]") {
    projection_fixture_t fixture;
    require_minimap(fixture.project());
    check_minimap(fixture.flattened.view.compose.bgm_bar_begin_fraction == Catch::Approx(0.0));
    check_minimap(fixture.flattened.view.compose.bgm_bar_end_fraction == Catch::Approx(0.4));

    fixture.interaction.viewport_scroll_pixels = 800;
    require_minimap(fixture.project());
    check_minimap(fixture.flattened.view.compose.waveform_overview_visible_begin_fraction > 0.4);
    check_minimap((fixture.flattened.view.compose.bgm_bar_begin_fraction +
           fixture.flattened.view.compose.bgm_bar_end_fraction) * 0.5 ==
          Catch::Approx(midpoint(fixture.flattened.view.compose)));

    fixture.interaction.scroll_direction = editor_field_scroll_direction_t::down;
    fixture.interaction.viewport_scroll_pixels = 1'400;
    require_minimap(fixture.project());
    check_minimap(fixture.flattened.view.compose.waveform_overview_visible_end_fraction == Catch::Approx(1.0));
    check_minimap(fixture.flattened.view.compose.bgm_bar_end_fraction == Catch::Approx(1.0));
}

TEST_CASE("minimap playback and zoom transitions preserve the field position",
          "[frame-timing][minimap-center]") {
    projection_fixture_t fixture;
    fixture.interaction.viewport_scroll_pixels = 600;
    require_minimap(fixture.project());
    const auto field_scroll = fixture.interaction.viewport_scroll_pixels;
    const auto center = midpoint(fixture.flattened.view.compose);
    wormedit::ui::workspace_action_t action{};
    auto& input = action.compose_inputs.emplace_back();
    input.kind = WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM;
    input.x_pixels = 2.0;
    std::optional<bgm_bar_pan_override_t> pan;
    const bgm_bar_input_context_t context{.total_frames = fixture.analysis.waveform->total_frames,
        .visible_begin_fraction = fixture.flattened.view.compose.waveform_overview_visible_begin_fraction,
        .visible_end_fraction = fixture.flattened.view.compose.waveform_overview_visible_end_fraction,
        .project = song_id_t{1}, .chart = chart_id_t{7}, .document_revision = 5,
        .asset = asset_id_t{12}};
    require_minimap(consume_bgm_bar_inputs(action, fixture.range, pan, context));
    check_minimap(!pan.has_value());
    check_minimap((fixture.range.begin + fixture.range.end) * 0.5 == Catch::Approx(center));
    check_minimap(fixture.interaction.viewport_scroll_pixels == field_scroll);
    fixture.flattened.view.compose.source_playhead_available = true;
    fixture.flattened.view.compose.source_playhead_frame = 200'000;
    check_minimap(fixture.project());
    check_minimap((fixture.flattened.view.compose.bgm_bar_begin_fraction +
           fixture.flattened.view.compose.bgm_bar_end_fraction) * 0.5 ==
          Catch::Approx(center));
    check_minimap(fixture.interaction.viewport_scroll_pixels == field_scroll);
}

TEST_CASE("explicit minimap pan persists only while its stamped visible span is unchanged",
          "[frame-timing][minimap-center]") {
    auto range = bgm_bar_range_t{0.3, 0.7};
    std::optional<bgm_bar_pan_override_t> pan;
    wormedit::ui::workspace_action_t action{};
    action.compose_inputs.resize(1);
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN;
    action.compose_inputs[0].x_pixels = 0.1;
    const bgm_bar_input_context_t same{.total_frames = 768'000,
        .visible_begin_fraction = 0.4, .visible_end_fraction = 0.6,
        .project = song_id_t{1}, .chart = chart_id_t{7}, .document_revision = 5,
        .asset = asset_id_t{12}};
    require_minimap(consume_bgm_bar_inputs(action, range, pan, same));
    require_minimap(pan.has_value());
    check_minimap(bgm_bar_pan_override_matches(*pan, 0.4, 0.6, song_id_t{1}, chart_id_t{7}, 5,
                                               asset_id_t{12}));
    check_minimap(pan->range.begin == Catch::Approx(0.4));
    auto changed = same;
    changed.visible_begin_fraction = 0.45;
    changed.visible_end_fraction = 0.65;
    wormedit::ui::workspace_action_t idle{};
    check_minimap(!consume_bgm_bar_inputs(idle, range, pan, changed));
    check_minimap(!pan.has_value());
    check_minimap(bgm_bar_center_on_visible_span(range, changed.visible_begin_fraction,
        changed.visible_end_fraction, 0.01).begin == Catch::Approx(0.35));
}

TEST_CASE("active minimap drag freezes projected rows and rejects stale capture context",
          "[frame-timing][minimap-center]") {
    projection_fixture_t fixture;
    fixture.window_capture = bgm_bar_window_capture_t{
        .range = {0.2, 0.6}, .project = song_id_t{1}, .chart = chart_id_t{7},
        .document_revision = 5, .asset = asset_id_t{12},
        .total_frames = fixture.analysis.waveform->total_frames,
        .sample_rate = fixture.analysis.waveform->sample_rate};
    fixture.interaction.viewport_scroll_pixels = 800;
    require_minimap(fixture.project());
    check_minimap(fixture.flattened.view.compose.bgm_bar_begin_fraction == Catch::Approx(0.2));
    check_minimap(fixture.flattened.view.compose.bgm_bar_end_fraction == Catch::Approx(0.6));

    fixture.flattened.view.compose.selected_audio_asset_id.value = 13;
    require_minimap(fixture.project());
    check_minimap(!fixture.window_capture.has_value());
    check_minimap(fixture.flattened.view.compose.bgm_bar_begin_fraction != Catch::Approx(0.2));
    check_minimap((fixture.flattened.view.compose.bgm_bar_begin_fraction +
           fixture.flattened.view.compose.bgm_bar_end_fraction) * 0.5 ==
          Catch::Approx(midpoint(fixture.flattened.view.compose)));

    fixture.flattened.view.compose.selected_audio_asset_id.value = 12;
    fixture.window_capture = bgm_bar_window_capture_t{
        .range = {0.2, 0.6}, .project = song_id_t{1}, .chart = chart_id_t{7},
        .document_revision = 5, .asset = asset_id_t{12},
        .total_frames = fixture.analysis.waveform->total_frames + 1,
        .sample_rate = fixture.analysis.waveform->sample_rate};
    require_minimap(fixture.project());
    check_minimap(!fixture.window_capture.has_value());
}

TEST_CASE("song bar uses a BMS default and preserves explicit session choices across formats",
          "[frame-timing][minimap-center][song-bar]") {
    auto chart = minimap_chart();
    chart.source_family = format_family_t::bms;
    auto history = document_history_t::from(song_document_t{.id = song_id_t{1}, .charts = {chart}});
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    binding.value->selected_chart = chart.id;
    binding.value->active_tab = workspace_tab_t::compose;
    compose_scene_cache_t cache;
    cache.frame.bgm_bar_audio_seen = true;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    frontend_workspace_view_t flattened;
    const auto project = [&] {
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *binding.value, cache, 1280, 800,
                              interaction, application, nullptr);
        REQUIRE(flattened.view.compose.scene_available);
        CHECK(flattened.view.compose.bgm_bar_available);
        return flattened.view.compose.bgm_bar_reserved;
    };
    CHECK_FALSE(project());
    history.document.charts.front().source_family = format_family_t::osu_mania;
    CHECK(project());
    history.document.charts.front().source_family = format_family_t::bms;
    REQUIRE_FALSE(project());
    REQUIRE(interaction);
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto toggle = [&](bool currently_visible) {
        wormedit::ui::workspace_action_t action{};
        action.compose_inputs.resize(1);
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE;
        action.compose_inputs[0].overview_primary = currently_visible;
        (void)dispatch_compose_input_action(action, history, *binding.value, *interaction,
            viewport, navigation, application, cache, 0.0);
    };
    toggle(false);
    CHECK(project());
    CHECK(project());
    toggle(true);
    CHECK_FALSE(project());
    history.document.charts.front().source_family = format_family_t::osu_mania;
    CHECK_FALSE(project());
    CHECK(history.undo.empty());
}
