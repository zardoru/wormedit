#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <source_location>
#include <string_view>

namespace {
void require_minimap_event(bool condition) { REQUIRE(condition); }
void check_minimap_event(bool condition) { CHECK(condition); }

auto pointer_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto left_button_down(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

void prepare_overview(nuklear_event_harness_t& harness,
                      WormUiComposeWaveformOverviewRowView& row) {
    auto& workspace = harness.view();
    workspace.project_id.value = 11;
    workspace.document_revision = 5;
    auto& compose = workspace.compose;
    compose.project_id = 11;
    compose.chart_id = 7;
    compose.document_revision = 5;
    compose.scene_available = true;
    compose.bgm_bar_reserved = true;
    compose.bgm_bar_rows = &row;
    compose.bgm_bar_row_count = 1;
    compose.bgm_bar_begin_fraction = 0.2;
    compose.bgm_bar_end_fraction = 0.6;
    compose.waveform_overview_visible_begin_fraction = 0.45;
    compose.waveform_overview_visible_end_fraction = 0.55;
    compose.selected_audio_asset_id = 12;
    compose.waveform_overview_total_frames = 480'000;
    compose.waveform_overview_sample_rate = 48'000;
}

auto matches_capture(const worm_ui_nuklear_overview_capture_t& capture,
                     const wormedit::ui::workspace_view_t& workspace, double left, double width) -> bool {
    return worm_ui_nuklear_overview_capture_matches(
        capture, workspace.compose, left, width, true, true);
}

void check_capture_match(const worm_ui_nuklear_overview_capture_t& capture,
                         const wormedit::ui::workspace_view_t& workspace, double left, double width,
                         bool expected, std::string_view context,
                         const std::source_location location = std::source_location::current()) {
    INFO(context << " at " << location.file_name() << ':' << location.line());
    CHECK(matches_capture(capture, workspace, left, width) == expected);
}
} // namespace

TEST_CASE("minimap capture freezes its displayed window and navigation mapping",
          "[unit][frontend-state][event-harness][minimap-center]") {
    nuklear_event_harness_t harness{1280, 900};
    require_minimap_event(harness.ready());
    WormUiComposeWaveformOverviewRowView row{};
    prepare_overview(harness, row);
    constexpr float x = 351.0f;
    constexpr float y = 120.0f;

    const std::array press{pointer_motion(x, y), left_button_down(x, y)};
    const auto captured = harness.frame(press);
    require_minimap_event(captured.get().minimap_window_capture_active);
    check_minimap_event(captured.get().minimap_window_capture_begin_fraction == 0.2);
    check_minimap_event(captured.get().minimap_window_capture_end_fraction == 0.6);

    auto& compose = harness.view().compose;
    compose.bgm_bar_begin_fraction = 0.05;
    compose.bgm_bar_end_fraction = 0.45;
    compose.waveform_overview_visible_begin_fraction = 0.7;
    compose.waveform_overview_visible_end_fraction = 0.8;
    const auto frozen = worm_ui_nuklear_overview_window_for_capture(
        {.window_begin = captured.get().minimap_window_capture_begin_fraction,
         .window_end = captured.get().minimap_window_capture_end_fraction,
         .window_capture = true}, true, compose.bgm_bar_begin_fraction,
        compose.bgm_bar_end_fraction);
    check_minimap_event(frozen.begin == 0.2);
    check_minimap_event(frozen.end == 0.6);

    const std::array move{pointer_motion(x + 30.0f, y)};
    const auto seek = harness.frame(move);
    require_minimap_event(seek.get().compose_input_count == 1);
    check_minimap_event(seek.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK);
    check_minimap_event(seek.get().compose_inputs[0].audio_fraction > 0.2);
    check_minimap_event(seek.get().compose_inputs[0].audio_fraction < 0.6);
    check_minimap_event(seek.get().minimap_window_capture_active);
}

TEST_CASE("minimap capture matching invalidates project, chart, revision, asset, and geometry changes",
          "[unit][frontend-state][event-harness][minimap-center]") {
    nuklear_event_harness_t harness{1280, 900};
    require_minimap_event(harness.ready());
    WormUiComposeWaveformOverviewRowView row{};
    prepare_overview(harness, row);
    auto& view = harness.view().compose;
    view.document_revision = 0;
    const worm_ui_nuklear_overview_capture_t capture{
        .chart_id = view.chart_id,
        .revision = view.document_revision,
        .asset_id = view.selected_audio_asset_id,
        .total_frames = view.waveform_overview_total_frames,
        .sample_rate = view.waveform_overview_sample_rate,
        .left = 20.0,
        .width = 500.0,
        .window_begin = 0.2,
        .window_end = 0.6,
        .window_capture = true,
        .project_id = harness.view().project_id.value};
    check_capture_match(capture, harness.view(), 20.0, 500.0, true, "valid revision-zero capture");
    ++harness.view().project_id.value;
    ++view.project_id;
    check_capture_match(capture, harness.view(), 20.0, 500.0, false, "project stamp");
    harness.view().project_id.value = capture.project_id;
    view.project_id = capture.project_id;
    view.chart_id += 1;
    check_capture_match(capture, harness.view(), 20.0, 500.0, false, "chart stamp");
    view.chart_id = capture.chart_id;
    view.document_revision = 1;
    check_capture_match(capture, harness.view(), 20.0, 500.0, false, "revision stamp");
    view.document_revision = capture.revision;
    view.selected_audio_asset_id += 1;
    check_capture_match(capture, harness.view(), 20.0, 500.0, false, "asset stamp");
    view.selected_audio_asset_id = capture.asset_id;
    view.waveform_overview_total_frames += 1;
    check_capture_match(capture, harness.view(), 20.0, 500.0, false, "total frame count");
    view.waveform_overview_total_frames = capture.total_frames;
    view.waveform_overview_sample_rate += 1;
    check_capture_match(capture, harness.view(), 20.0, 500.0, false, "sample rate");
    view.waveform_overview_sample_rate = capture.sample_rate;
    check_capture_match(capture, harness.view(), 20.0, 501.0, false, "widget geometry");
}
