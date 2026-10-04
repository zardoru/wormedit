#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

namespace {
auto pointer_event(SDL_EventType type, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.type = type;
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.type = type;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = x;
        event.button.y = y;
    }
    return event;
}

auto wheel_event(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = 1.0F;
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    return event;
}

auto click_notification_close_button(nuklear_event_harness_t& harness) -> bool {
    // Default Nuklear title-bar close center for this 12,40 / 760-wide panel.
    constexpr float x = 756.0F;
    constexpr float y = 56.0F;
    const auto down = std::array{pointer_event(SDL_EVENT_MOUSE_MOTION, x, y),
                                 pointer_event(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y)};
    const auto pressed = harness.frame(down);
    const auto up = std::array{pointer_event(SDL_EVENT_MOUSE_MOTION, x, y),
                               pointer_event(SDL_EVENT_MOUSE_BUTTON_UP, x, y)};
    const auto released = harness.frame(up);
    return pressed.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY ||
           released.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY;
}

void open_history_with_transport(nuklear_event_harness_t& harness) {
    REQUIRE(harness.ready());
    harness.view().notifications.history.visible = true;
    harness.view().transport_available = true;
    harness.view().transport_end_frame = 4'410'000;
    harness.view().transport_sample_rate = 48'000;
    harness.view().compose.selected_audio_asset_id = 12;
    REQUIRE(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(harness.drawn_text().find("No notifications") != std::string::npos);
}

void verify_real_close(nuklear_event_harness_t& harness) {
    REQUIRE(click_notification_close_button(harness));
}

void verify_compose_motion_and_wheel(nuklear_event_harness_t& harness) {
    const auto motion = pointer_event(SDL_EVENT_MOUSE_MOTION, 640.0F, 88.0F);
    const auto motion_result = harness.frame(std::span{&motion, 1});
    CHECK(motion_result.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    CHECK(motion_result.get().hover_source_frame_valid);
    const auto wheel = wheel_event(480.0F, 400.0F);
    const auto compose = harness.frame(std::span{&wheel, 1});
    REQUIRE(compose.get().compose_viewport_input_count == 1);
    CHECK(compose.get().compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
}

void verify_history_reopens(nuklear_event_harness_t& harness) {
    harness.view().notifications.history.visible = true;
    (void)harness.frame();
    WormUiTestingRect reopened_bounds{};
    CHECK(worm_ui_frontend_window_bounds_for_testing("Notifications", &reopened_bounds));
    CHECK(harness.drawn_text().find("No notifications") != std::string::npos);
}

void setup_stale_hidden_history(nuklear_event_harness_t& harness) {
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.notifications.history.visible = true;
    view.transport_available = true;
    view.transport_end_frame = 4'410'000;
    view.transport_sample_rate = 48'000;
    view.compose.selected_audio_asset_id = 12;
    (void)harness.frame();
    REQUIRE(worm_ui_frontend_hide_notification_history_for_testing());
}

void verify_stale_hidden_motion(nuklear_event_harness_t& harness) {
    const auto motion = pointer_event(SDL_EVENT_MOUSE_MOTION, 640.0F, 88.0F);
    const auto result = harness.frame(std::span{&motion, 1});
    CHECK(result.get().hover_source_frame_valid);
    CHECK(result.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY);
}

void reopen_and_hide_stale_history(nuklear_event_harness_t& harness) {
    harness.view().notifications.history.visible = false;
    (void)harness.frame();
    harness.view().notifications.history.visible = true;
    (void)harness.frame();
    REQUIRE(worm_ui_frontend_hide_notification_history_for_testing());
}

void verify_stale_hidden_wheel(nuklear_event_harness_t& harness) {
    const auto wheel = wheel_event(480.0F, 400.0F);
    const auto result = harness.frame(std::span{&wheel, 1});
    CHECK(result.get().compose_viewport_input_count == 1);
    CHECK(result.get().compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
    CHECK(result.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY);
}

void verify_absent_window_wheel(nuklear_event_harness_t& harness) {
    const auto wheel = wheel_event(480.0F, 400.0F);
    const auto result = harness.frame(std::span{&wheel, 1});
    CHECK(result.get().compose_viewport_input_count == 1);
    CHECK(result.get().compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
}

void verify_visible_history_capture(nuklear_event_harness_t& harness) {
    REQUIRE(harness.ready());
    harness.view().notifications.history.visible = true;
    (void)harness.frame();
    const auto wheel = wheel_event(100.0F, 100.0F);
    const auto result = harness.frame(std::span{&wheel, 1});
    CHECK(result.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    CHECK(result.get().compose_viewport_input_count == 0);
}
} // namespace

TEST_CASE("Closing notification history releases its old pointer bounds",
          "[frontend-state][event-harness][notifications][history]") {
    nuklear_event_harness_t harness{960, 720};
    open_history_with_transport(harness);
    verify_real_close(harness);
    harness.view().notifications.history.visible = false;
    (void)harness.frame();
    verify_compose_motion_and_wheel(harness);
    verify_history_reopens(harness);
}

TEST_CASE("A hidden Nuklear history window cannot capture a stale visible model",
          "[frontend-state][event-harness][notifications][history]") {
    nuklear_event_harness_t harness{1280, 900};
    setup_stale_hidden_history(harness);
    verify_stale_hidden_motion(harness);
    reopen_and_hide_stale_history(harness);
    verify_stale_hidden_wheel(harness);
    verify_absent_window_wheel(harness);
}

TEST_CASE("Visible notification history retains pointer capture",
          "[frontend-state][event-harness][notifications][history]") {
    nuklear_event_harness_t harness{960, 720};
    verify_visible_history_capture(harness);
}
