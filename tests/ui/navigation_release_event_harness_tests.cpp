#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <source_location>

namespace {
void expect_navigation(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Navigation expectation at " << at.line());
    REQUIRE(condition);
}

auto arrow(bool down, bool visual_up = false) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down;
    event.key.key = visual_up ? SDLK_UP : SDLK_DOWN;
    event.key.scancode = visual_up ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN;
    return event;
}
}

TEST_CASE("Compose releases navigation when its key up is captured by a disappearing editor",
          "[frontend-state][event-harness][navigation-release]") {
    nuklear_event_harness_t harness;
    expect_navigation(harness.ready());
    (void)harness.frame();
    const auto visual_up = GENERATE(false, true);
    const auto down = arrow(true, visual_up);
    (void)harness.frame({&down, 1});
    auto& state = worm_ui_nuklear_compose_state();
    expect_navigation(state.keyboard_navigation.navigation_key_active);
    auto& view = harness.view().compose;
    view.audio_offset_editable = true;
    view.waveform_audio_offset_denominator = 1;
    expect_navigation(worm_ui_nuklear_begin_inline_offset(state, view));
    // The old capture still owns event routing. Compose closes the unavailable
    // editor later in this frame, so cancellation cannot recover the lost edge.
    view.audio_offset_editable = false;
    const auto up = arrow(false, visual_up);
    const auto released = harness.frame({&up, 1});
    expect_navigation(!state.inline_offset.inline_offset_open);
    expect_navigation(!state.keyboard_navigation.navigation_key_active);
    expect_navigation(std::ranges::any_of(released.get().compose_inputs, [](const auto& input) {
        return input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE ||
               input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL;
    }));
    auto repeat = down;
    repeat.key.repeat = true;
    expect_navigation(harness.frame({&repeat, 1}).get().compose_inputs.empty());
    expect_navigation(!state.keyboard_navigation.navigation_key_active);
}

TEST_CASE("Compose terminates an arrow hold on release or focus loss and ignores late repeats",
          "[frontend-state][event-harness][navigation-release]") {
    const auto focus_lost = GENERATE(false, true);
    nuklear_event_harness_t harness;
    expect_navigation(harness.ready());
    (void)harness.frame();
    const auto down = arrow(true);
    const auto pressed = harness.frame({&down, 1});
    expect_navigation(pressed.get().compose_inputs.size() == 1);
    expect_navigation(pressed.get().compose_inputs.front().kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS);
    auto terminal = arrow(false);
    if (focus_lost) terminal.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    const auto released = harness.frame({&terminal, 1});
    expect_navigation(!worm_ui_nuklear_compose_state().keyboard_navigation.navigation_key_active);
    const auto expected = focus_lost ? WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL
                                     : WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE;
    expect_navigation(std::ranges::count(released.get().compose_inputs, expected,
                            &wormedit::ui::compose_input_action_t::kind) == 1);
    if (focus_lost) {
        SDL_Event gained{};
        gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        (void)harness.frame({&gained, 1});
    }
    auto repeat = down;
    repeat.key.repeat = true;
    expect_navigation(harness.frame({&repeat, 1}).get().compose_inputs.empty());
    expect_navigation(harness.frame().get().compose_inputs.empty());
}
