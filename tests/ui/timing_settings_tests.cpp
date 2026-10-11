#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string_view>
#include "ui/nuklear/compose_state.h"

namespace {
auto click_text(nuklear_event_harness_t& harness, std::string_view label)
    -> workspace_action_snapshot_t {
    (void)harness.frame();
    (void)harness.frame();  // A dialog fits its content on its second frame.
    INFO("Click label: " << label);
    INFO(harness.drawn_text());
    const auto center = harness.drawn_text_center(label);
    REQUIRE(center);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = center->first;
    motion.motion.y = center->second;
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.x = center->first;
    button.button.y = center->second;
    const std::array down{motion, button};
    const auto pressed = harness.frame(down);
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.down = false;
    const std::array up{motion, button};
    const auto released = harness.frame(up);
    if (pressed.get().tap_timing.apply ||
        pressed.get().tap_timing.choose_bpm || pressed.get().timing_settings.toggle ||
        pressed.get().timing_settings.close || pressed.get().timing_settings.change_enable ||
        pressed.get().timing_settings.change_volume || pressed.get().timing_settings.change_count_in ||
        pressed.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE) return pressed;
    return released;
}

auto click_position(nuklear_event_harness_t& harness, float x, float y)
    -> workspace_action_snapshot_t {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.x = x;
    button.button.y = y;
    const std::array down{motion, button};
    const auto pressed = harness.frame(down);
    CHECK(pressed.get().compose_input_count == 0);
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.down = false;
    const std::array up{motion, button};
    const auto released = harness.frame(up);
    if (pressed.get().timing_settings.change_volume || pressed.get().timing_settings.change_count_in ||
        pressed.get().timing_settings.close) return pressed;
    return released;
}
}

TEST_CASE("Tap assistant Apply emits the projected revision and requires a valid proposal",
          "[event-harness][timing-panels]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view().tap_timing;
    view.visible = true;
    view.playing = true;
    view.source_available = true;
    view.estimate_available = true;
    view.stable = true;
    view.bpm = 120;
    view.apply_available = true;
    view.apply_revision = 77;
    view.proposed_bpm = 120;
    view.proposed_offset_ms = 250;
    const auto action = click_text(harness, "Apply 120 BPM");
    CHECK(action.get().tap_timing.apply);
    CHECK(action.get().tap_timing.apply_revision == 77);
    const auto half = click_text(harness, "0.5");
    CHECK(half.get().tap_timing.choose_bpm);
    CHECK(half.get().tap_timing.bpm_choice == 2);
    view.apply_available = false;
    CHECK_FALSE(click_text(harness, "Apply BPM").get().tap_timing.apply);
    view.visible = false;
    (void)harness.frame();
    CHECK(harness.drawn_text().find("First tap assigns") == std::string::npos);
}

TEST_CASE("Timing modeless panel blank body and disabled controls own pointer input",
          "[event-harness][timing-panels]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.timing_settings_visible = true;
    view.compose.scene_available = true;
    view.compose.timing_lane_visibility_mask = 0xff;
    view.timing.stage_active = true;
    view.timing.stage_lane = WORM_UI_TIMING_LANE_BPM;
    const auto focus = worm_ui_nuklear_compose_state().keyboard_focus;
    (void)harness.frame();
    CHECK_FALSE(click_text(harness, "BPM: On").get().compose_action ==
                WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY);
    CHECK(click_position(harness, 500, 450).get().compose_input_count == 0);
    view.compose.scene_available = false;
    CHECK_FALSE(click_text(harness, "ANC: On").get().compose_action ==
                WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY);
    CHECK(worm_ui_nuklear_compose_state().keyboard_focus == focus);
}

TEST_CASE("Timing Settings exposes live enable volume and bounded count-in controls",
          "[event-harness][timing-panels]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.timing_settings_visible = true;
    view.transport_metronome_available = true;
    view.transport_metronome_enabled = false;
    view.metronome_volume = 50;
    const auto enabled = click_text(harness, "Metronome");
    CHECK(enabled.get().timing_settings.change_enable);
    CHECK(enabled.get().timing_settings.enabled);
    (void)harness.frame();
    const auto volume = click_text(harness, "Raise volume");
    CHECK(volume.get().timing_settings.change_volume);
    CHECK(volume.get().timing_settings.volume == 55);
    const auto count = click_text(harness, "More count-in");
    CHECK(count.get().timing_settings.change_count_in);
    CHECK(count.get().timing_settings.count_in == 1);
    // The count-in explanation lives in a tooltip, not in the body.
    CHECK(harness.drawn_text().find("No preroll") == std::string::npos);
    CHECK(harness.drawn_text().find("Count-in before beat zero") == std::string::npos);
}

TEST_CASE("Timing Settings owns blank-body clicks, cannot be dragged, and close then reopen keeps controls",
          "[event-harness][timing-panels]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.timing_settings_visible = true;
    (void)harness.frame();
    (void)harness.frame();
    WormUiTestingRect before{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Timing Settings", &before));
    // A drag from the title row does not move the dialog.
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = before.x + 60;
    down.button.y = before.y + 22;
    SDL_Event initial_motion{};
    initial_motion.type = SDL_EVENT_MOUSE_MOTION;
    initial_motion.motion.x = down.button.x;
    initial_motion.motion.y = down.button.y;
    (void)harness.frame(std::array{initial_motion, down});
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = down.button.x - 200;
    motion.motion.y = down.button.y;
    motion.motion.state = SDL_BUTTON_LMASK;
    const auto moved = harness.frame(std::array{motion});
    CHECK(moved.get().compose_input_count == 0);
    down.type = SDL_EVENT_MOUSE_BUTTON_UP;
    down.button.down = false;
    down.button.x = motion.motion.x;
    (void)harness.frame(std::array{down});
    WormUiTestingRect bounds{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Timing Settings", &bounds));
    CHECK(bounds.x == before.x);
    CHECK(bounds.y == before.y);
    CHECK(click_position(harness, bounds.x + 180, bounds.y + bounds.height - 6).get().compose_input_count == 0);
    const auto closed = click_position(harness, bounds.x + bounds.width - 28, bounds.y + 28);
    CHECK(closed.get().timing_settings.close);
    view.timing_settings_visible = false;
    (void)harness.frame();
    view.timing_settings_visible = true;
    (void)harness.frame();
    CHECK(harness.drawn_text_center("More count-in"));
}

TEST_CASE("Timing Settings opens from the menu and timing inspector without editing the chart",
          "[event-harness][timing-panels]") {
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    harness.view().compose.timing_focus = true;
    (void)click_text(harness, "View");
    const auto menu = click_text(harness, "Timing Settings...");
    CHECK(menu.get().timing_settings.toggle);
    CHECK(menu.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    harness.view().compose.timing_focus = false;
    (void)harness.frame();
    for (int step = 0; step < 12 && !harness.drawn_text_center("Timing Settings..."); ++step) {
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = 1150;
        motion.motion.y = 700;
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = -2;
        wheel.wheel.mouse_x = 1150;
        wheel.wheel.mouse_y = 700;
        (void)harness.frame(std::array{motion, wheel});
    }
    const auto inspector = click_text(harness, "Timing Settings...");
    CHECK(inspector.get().timing_settings.toggle);
    CHECK(inspector.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Timing Settings moves all timing lane controls into a modeless panel",
          "[event-harness][timing-panels]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.timing_settings_visible = true;
    view.compose.scene_available = true;
    view.compose.timing_lane_visibility_mask = 0xff;
    constexpr std::array labels{"BPM: On", "SV: On", "SPD: On", "STP: On",
                                "DLY: On", "WRP: On", "KIA: On", "ANC: On"};
    for (std::size_t lane = 0; lane < labels.size(); ++lane) {
        const auto action = click_text(harness, labels[lane]);
        CHECK(action.get().compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY);
        CHECK(action.get().compose_timing_lane == lane);
    }
    view.timing_settings_visible = false;
    (void)harness.frame();
    CHECK(harness.drawn_text().find("ANC: On") == std::string::npos);
    view.timing_settings_visible = true;
    (void)harness.frame();
    CHECK(harness.drawn_text().find("ANC: On") != std::string::npos);
}
