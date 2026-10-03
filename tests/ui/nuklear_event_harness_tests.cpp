#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace {

auto key_event(SDL_EventType type, SDL_Keycode key, SDL_Scancode scancode, bool repeat = false)
    -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    event.key.type = type;
    event.key.windowID = 0;
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.down = type == SDL_EVENT_KEY_DOWN;
    event.key.repeat = repeat;
    return event;
}

auto button_event(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = 0;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto motion_event(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = 0;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<WormUiWorkspaceAction> {
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(x, y), button_event(true, x, y)};
    script[1].events = {motion_event(x, y), button_event(false, x, y)};
    return harness.run_script(script);
}

auto delete_shortcut_has_compose_context(const WormUiWorkspaceAction& action) -> bool {
    return action.shortcut_event_count == 1 &&
           action.shortcut_events[0].hid_usage ==
               static_cast<std::uint16_t>(SDL_SCANCODE_DELETE) &&
           !action.shortcut_events[0].repeat && action.shortcut_window_focused &&
           !action.shortcut_text_entry_active && !action.shortcut_ime_composition_active;
}

} // namespace

TEST_CASE("Headless frontend routes a wheel event over Compose to viewport input",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
    REQUIRE(harness.frame().kind == WORM_UI_WORKSPACE_ACTION_NONE);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = 0;
    wheel.wheel.x = 0.0f;
    wheel.wheel.y = 1.0f;
    wheel.wheel.mouse_x = 480.0f;
    wheel.wheel.mouse_y = 400.0f;
    const std::array events{wheel};
    const auto action = harness.frame(events);

    REQUIRE(action.compose_viewport_input_count == 1);
    CHECK(action.compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
    CHECK(action.compose_viewport_inputs[0].delta_y != 0.0);
}

TEST_CASE("Headless frontend emits Delete shortcut press with focused Compose context",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto pressed = harness.frame(std::span{&press, 1});
    CHECK(delete_shortcut_has_compose_context(pressed));
}

TEST_CASE("Headless frontend ignores Delete shortcut release",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto released = harness.frame(std::span{&release, 1});
    CHECK(released.shortcut_event_count == 0);
}

TEST_CASE("Headless frontend marks repeated Delete shortcut press",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    const auto repeat = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE, true);
    const auto repeated = harness.frame(std::span{&repeat, 1});
    REQUIRE(repeated.shortcut_event_count == 1);
    CHECK(repeated.shortcut_events[0].repeat);
}

TEST_CASE("Headless frontend tab click emits a workspace tab action",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    constexpr float tab_x = 80.0f;
    constexpr float tab_y = 54.0f;
    std::array<event_step_t, 2> script{};
    script[0].events.push_back(motion_event(tab_x, tab_y));
    script[0].events.push_back(button_event(true, tab_x, tab_y));
    script[1].events.push_back(motion_event(tab_x, tab_y));
    script[1].events.push_back(button_event(false, tab_x, tab_y));
    const auto actions = harness.run_script(script);

    REQUIRE(actions.size() == script.size());
    CHECK(actions[0].kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB);
    CHECK(actions[0].tab == WORM_UI_WORKSPACE_TAB_SETUP);
}

TEST_CASE("Headless frontend suppresses Delete shortcut while new-project wizard is open",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());
    const auto delete_key = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto modal_key_action = harness.frame(std::span{&delete_key, 1});
    CHECK(modal_key_action.shortcut_event_count == 0);
}

TEST_CASE("Headless frontend new-project wizard cancel closes and permits reopening",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());

    // The wizard is centered at (230,258), 500x204. Its last row begins at y=366;
    // the button centers are in the two 242px columns at y=406.
    constexpr float button_y = 406.0f;
    const auto cancel_actions = click_at(harness, 350.0f, button_y);
    CHECK(cancel_actions[0].kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(worm_ui_frontend_request_new_project());
}

TEST_CASE("Headless frontend new-project wizard creates the default 7K layout",
          "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());

    const auto create_actions = click_at(harness, 610.0f, 406.0f);
    CHECK(create_actions[0].kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
    CHECK(create_actions[0].new_project_layout_index == 3);
}
