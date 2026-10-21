#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstdio>
#include <string>
#include <source_location>

namespace {

using wormedit::test::mouse_button;
using wormedit::test::mouse_motion;

auto hover(nuklear_event_harness_t& harness, float x, float y)
    -> workspace_action_snapshot_t {
    const std::array events{mouse_motion(x, y)};
    return harness.frame(events);
}

} // namespace

TEST_CASE("Seek bar reports the clicked source frame without seeking and accepts only a fresh label",
          "[unit][frontend-state][event-harness][hover-timing]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.transport_available = true;
    view.transport_end_frame = 4'410'000;
    view.transport_sample_rate = 48'000;
    view.compose.selected_audio_asset_id = 12;

    const auto first = hover(harness, 640, 88);
    REQUIRE(first.get().hover_source_frame_valid);
    CHECK(first.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(first.get().hover_source_total_frames == view.transport_end_frame);
    CHECK(first.get().hover_source_sample_rate == view.transport_sample_rate);

    auto& timing = view.compose;
    timing.hover_timing_valid = true;
    timing.hover_timing_source_frame = first.get().hover_source_frame;
    timing.hover_timing_total_frames = view.transport_end_frame;
    timing.hover_timing_sample_rate = view.transport_sample_rate;
    std::snprintf(timing.hover_timing_label, sizeof(timing.hover_timing_label),
                  "Measure 12 · beat 3 · 1:23.456");
    const auto matched = hover(harness, 640, 88);
    CHECK(harness.drawn_text().find("Measure 12 · beat 3 · 1:23.456") != std::string::npos);
    const std::array down{mouse_motion(640, 88), mouse_button(true, 640, 88)};
    const auto clicked = harness.frame(down);
    CHECK(clicked.get().kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK);
    CHECK(clicked.get().transport_seek_frame == first.get().hover_source_frame);

    timing.hover_timing_source_frame += 1;
    (void)hover(harness, 640, 88);
    CHECK(harness.drawn_text().find("Measure 12 · beat 3 · 1:23.456") == std::string::npos);
    CHECK(matched.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    const auto outside = hover(harness, 10, 500);
    CHECK_FALSE(outside.get().hover_source_frame_valid);
}

TEST_CASE("Overview hover uses the click frame and rejects labels from another audio context",
          "[unit][frontend-state][event-harness][hover-timing]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view();
    auto& compose = view.compose;
    wormedit::ui::compose_waveform_overview_row_view_t bar_row{};
    compose.bgm_bar_reserved = true;

    compose.bgm_bar_rows = {bar_row};
    compose.bgm_bar_begin_fraction = 0.0;
    compose.bgm_bar_end_fraction = 1.0;
    compose.selected_audio_asset_id = 5;
    compose.waveform_overview_total_frames = 480'001;
    compose.waveform_overview_sample_rate = 48'000;

    const auto first = hover(harness, 351, 120);
    REQUIRE(first.get().hover_source_frame_valid);
    CHECK(first.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(first.get().hover_source_total_frames == compose.waveform_overview_total_frames);
    CHECK(first.get().hover_source_sample_rate == compose.waveform_overview_sample_rate);

    compose.hover_timing_valid = true;
    compose.hover_timing_source_frame = first.get().hover_source_frame;
    compose.hover_timing_total_frames = compose.waveform_overview_total_frames;
    compose.hover_timing_sample_rate = compose.waveform_overview_sample_rate;
    std::snprintf(compose.hover_timing_label, sizeof(compose.hover_timing_label),
                  "Measure 2 · beat 4 · 0:08.000");
    (void)hover(harness, 351, 120);
    CHECK(harness.drawn_text().find("Measure 2 · beat 4 · 0:08.000") != std::string::npos);

    compose.hover_timing_sample_rate += 1;
    (void)hover(harness, 351, 120);
    CHECK(harness.drawn_text().find("Measure 2 · beat 4 · 0:08.000") == std::string::npos);

    compose.selected_audio_asset_id = 0;
    const auto no_audio = hover(harness, 351, 120);
    CHECK_FALSE(no_audio.get().hover_source_frame_valid);
    CHECK(harness.drawn_text().find("Measure 2 · beat 4 · 0:08.000") == std::string::npos);
}

TEST_CASE("Seek bar hover uses Nuklear's padded one-third click position",
          "[unit][frontend-state][event-harness][hover-timing]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.transport_available = true;
    view.transport_end_frame = 9'600'000;
    view.transport_sample_rate = 48'000;
    view.compose.selected_audio_asset_id = 12;
    constexpr float x = 430.0f;
    const auto hovered = hover(harness, x, 88);
    REQUIRE(hovered.get().hover_source_frame_valid);
    const std::array down{mouse_motion(x, 88), mouse_button(true, x, 88)};
    const auto clicked = harness.frame(down);
    CHECK(clicked.get().kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK);
    CHECK(clicked.get().transport_seek_frame == hovered.get().hover_source_frame);
}

namespace {
void check_native_toolbar(bool value, std::source_location location = std::source_location::current()) {
    INFO("Toolbar expectation at " << location.line());
    CHECK(value);
}
}

TEST_CASE("Song bar native toolbar availability controls exactly one toggle",
          "[frontend-state][event-harness][native-icon-toolbar]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    const bool available = GENERATE(false, true);
    harness.view().compose.bgm_bar_available = available;
    (void)harness.frame();
    const auto* tooltip = available ? "Show song bar" : "Song audio is unavailable";
    (void)harness.hover_widget(tooltip);
    const auto [x, y] = harness.widget_center(tooltip);
    if (!available)
        check_native_toolbar(harness.drawn_text().find("Song audio is unavailable") != std::string::npos);
    const std::array down{mouse_motion(x, y), mouse_button(true, x, y)};
    const auto clicked = harness.frame(down);
    const auto& action = clicked.get();
    check_native_toolbar(action.compose_inputs.size() == (available ? 1 : 0));
    if (available)
        check_native_toolbar(action.compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE);
    check_native_toolbar(action.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_native_toolbar(action.kind == WORM_UI_WORKSPACE_ACTION_NONE);
    const std::array up{mouse_button(false, x, y)};
    const auto released = harness.frame(up);
    check_native_toolbar(released.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    for (std::size_t index = 0; index < released.get().compose_inputs.size(); ++index)
        check_native_toolbar(released.get().compose_inputs[index].kind != WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE);
}
