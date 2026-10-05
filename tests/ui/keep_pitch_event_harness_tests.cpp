#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace {

auto motion_to(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

// The transport row sits at the bottom of the window; scan it for the icon whose tooltip is shown.
auto find_icon(nuklear_event_harness_t& harness, const char* tooltip)
    -> std::optional<std::pair<float, float>> {
    for (float y = 860.0f; y < 898.0f; y += 6.0f) {
        for (float x = 100.0f; x < 400.0f; x += 4.0f) {
            (void)harness.frame(std::array{motion_to(x, y)});
            if (harness.drawn_text().find(tooltip) != std::string::npos)
                return std::pair<float, float>{x, y};
        }
    }
    return std::nullopt;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    SDL_Event down = motion_to(x, y);
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.clicks = 1;
    down.button.x = x;
    down.button.y = y;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    return {harness.frame(std::array{motion_to(x, y), down}),
            harness.frame(std::array{motion_to(x, y), up})};
}

} // namespace

TEST_CASE("Keep pitch is an icon toggle that emits the set action when available",
          "[unit][frontend-state][event-harness][keep-pitch]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    harness.view().transport_keep_pitch_available = true;
    harness.view().transport_keep_pitch = false;
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Keep pitch") == std::string::npos);
    const auto icon = find_icon(harness, "Preserve pitch at reduced speed");
    REQUIRE(icon.has_value());
    const auto clicked = click(harness, icon->first, icon->second);
    bool emitted = false;
    for (const auto& frame : clicked) {
        const auto& action = frame.get();
        if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH) {
            emitted = true;
            CHECK(action.transport_keep_pitch);
        }
    }
    CHECK(emitted);
}

TEST_CASE("Unavailable Keep pitch icon keeps a tooltip that says why and emits nothing",
          "[unit][frontend-state][event-harness][keep-pitch]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    harness.view().transport_keep_pitch_available = false;
    (void)harness.frame();
    const auto icon = find_icon(harness, "Keep pitch is unavailable without audio output");
    REQUIRE(icon.has_value());
    for (const auto& frame : click(harness, icon->first, icon->second))
        CHECK(frame.get().kind != WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH);
}
