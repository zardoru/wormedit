#include "nuklear_event_harness.h"
#include <wormedit/nuklear_config.h>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string_view>

namespace {
void settle(nuklear_event_harness_t& harness) { (void)harness.frame(); (void)harness.frame(); }
auto click(nuklear_event_harness_t& harness, std::string_view label) -> workspace_action_snapshot_t {
    settle(harness);
    const auto center = harness.drawn_text_center(label);
    REQUIRE(center);
    SDL_Event move{}; move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = center->first; move.motion.y = center->second;
    SDL_Event button{}; button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_LEFT; button.button.down = true;
    button.button.x = center->first; button.button.y = center->second;
    const std::array down{move, button};
    const auto pressed = harness.frame(down);
    button.type = SDL_EVENT_MOUSE_BUTTON_UP; button.button.down = false;
    const std::array up{move, button};
    const auto released = harness.frame(up);
    if (pressed.get().tap_timing.apply || pressed.get().tap_timing.reset || pressed.get().tap_timing.choose_bpm)
        return pressed;
    return released;
}
void ready(wormedit::ui::tap_timing_view_t& view) {
    view.visible = view.music_available = view.source_available = view.playing = true;
    view.estimate_available = view.stable = view.apply_available = true;
    view.tap_count = 9; view.tap_capacity = 16; view.beats_per_measure = 4;
    view.bpm = 174.02; view.deviation_ms = 1.8; view.offset_ms = 12.3;
    view.proposed_bpm = 174.5; view.apply_revision = 81;
}
}

namespace { void expect_tap(bool condition) { REQUIRE(condition); } }

TEST_CASE("Compact Tap tempo shows the readout hierarchy and content fit", "[tap-timing][event-harness]") {
    nuklear_event_harness_t harness;
    ready(harness.view().tap_timing);
    settle(harness);
    const auto& text = harness.drawn_text();
    expect_tap(text.find("Tap tempo") != std::string::npos);
    expect_tap(text.find("174.02") != std::string::npos);
    expect_tap(text.find("±1.8 ms · stable") != std::string::npos);
    expect_tap(text.find("beat 0 at 12.3 ms") != std::string::npos);
    expect_tap(text.find("9 / 16 taps") != std::string::npos);
    expect_tap(text.find("First tap assigns") == std::string::npos);
    wormedit::ui::testing_rect_view_t bounds{};
    expect_tap(worm_ui_frontend_window_bounds_for_testing("Tap timing", &bounds));
    expect_tap(bounds.width == 420);
    expect_tap(bounds.height < 350);
    expect_tap(bounds.height > 200);
}

TEST_CASE("Tap tempo hint explains unavailable stopped early and stale states", "[tap-timing][event-harness]") {
    nuklear_event_harness_t harness;
    auto& view = harness.view().tap_timing;
    view.visible = true;
    settle(harness);
    expect_tap(harness.drawn_text().find("Add a music track to tap") != std::string::npos);
    view.music_available = true;
    settle(harness);
    expect_tap(harness.drawn_text().find("Start playback to tap") != std::string::npos);
    view.playing = true;
    settle(harness);
    expect_tap(harness.drawn_text().find("Play the song and tap T or Space on the beat") != std::string::npos);
    expect_tap(harness.drawn_text().find("—") != std::string::npos);
    view.status = "Tap result expired; play and tap again before Apply.";
    settle(harness);
    expect_tap(harness.drawn_text().find("Tap result expired") != std::string::npos);
}

TEST_CASE("Tap tempo rounding Reset and Apply preserve action semantics", "[tap-timing][event-harness]") {
    nuklear_event_harness_t harness;
    ready(harness.view().tap_timing);
    const auto apply = click(harness, "Apply 174.5 BPM");
    expect_tap(apply.get().tap_timing.apply);
    expect_tap(apply.get().tap_timing.apply_revision == 81);
    const auto half = click(harness, "0.5");
    expect_tap(half.get().tap_timing.choose_bpm);
    expect_tap(half.get().tap_timing.bpm_choice == 2);
    expect_tap(click(harness, "Reset").get().tap_timing.reset);
    harness.view().tap_timing.apply_available = false;
    harness.view().tap_timing.timing_locked = true;
    expect_tap(!click(harness, "Apply BPM").get().tap_timing.apply);
}

TEST_CASE("Tap tempo Escape closes without leaking a workspace command", "[tap-timing][event-harness]") {
    nuklear_event_harness_t harness;
    ready(harness.view().tap_timing);
    settle(harness);
    SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.type = SDL_EVENT_KEY_DOWN;
    escape.key.down = true; escape.key.key = SDLK_ESCAPE; escape.key.scancode = SDL_SCANCODE_ESCAPE;
    const auto action = harness.frame({&escape, 1});
    expect_tap(action.get().tap_timing.close);
    expect_tap(action.get().shortcut_event_count == 0);
    expect_tap(action.get().compose_input_count == 0);
}

TEST_CASE("Options owns Escape while the Tap tempo tool is visible", "[tap-timing][event-harness]") {
    nuklear_event_harness_t harness;
    ready(harness.view().tap_timing);
    settle(harness);
    harness.view().options.visible = true;
    settle(harness);
    SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.type = SDL_EVENT_KEY_DOWN;
    escape.key.down = true; escape.key.key = SDLK_ESCAPE; escape.key.scancode = SDL_SCANCODE_ESCAPE;
    const auto action = harness.frame({&escape, 1});
    expect_tap(!action.get().tap_timing.close);
}
