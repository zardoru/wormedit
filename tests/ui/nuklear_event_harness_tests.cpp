#include "sys/application_command_dispatch.h"
#include "sys/compose_area_actions.h"
#include "nuklear_event_harness.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/sound_import_runtime.h"
#include "sys/compose_input.h"
#include "sys/compose_sound_selection.h"
#include "sys/compose_view.h"

#include <wormedit/core/command_registry.h>
#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <tuple>


TEST_CASE("Sound layer confirmation waits for a choice and rejects stale documents",
          "[unit][frontend-state][event-harness][sound-note-layers]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.keysounds_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sound_layer_confirmation_open = true;
    state.sound_layer_confirmation_project = view.project_id;
    state.sound_layer_confirmation_chart = view.chart_id;
    state.sound_layer_confirmation_revision = view.document_revision;
    state.sound_layer_confirmation_layer = 7;
    state.sound_layer_confirmation_resources = {1};
    CHECK(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(harness.drawn_text().find("Keep existing layers") != std::string::npos);
    CHECK(harness.drawn_text().find("Overwrite") != std::string::npos);
    ++view.document_revision;
    CHECK(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK_FALSE(state.sound_layer_confirmation_open);
}

TEST_CASE("Compare tab shows its single-difficulty hint without edit actions",
          "[unit][frontend-state][event-harness][compare]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE;
    view.setup.visible = false;
    view.metadata.visible = false;
    view.compose.visible = false;
    view.audio_slices.visible = false;
    view.compare.visible = true;
    view.compare.has_two_difficulties = false;
    const auto initial = harness.frame();
    CHECK(initial.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(harness.drawn_text().find("Choose two difficulties to compare.") != std::string::npos);
    SDL_Event press{};
    press.type = SDL_EVENT_KEY_DOWN;
    press.key.type = SDL_EVENT_KEY_DOWN;
    press.key.key = SDLK_DELETE;
    press.key.scancode = SDL_SCANCODE_DELETE;
    press.key.down = true;
    const auto result = harness.frame(std::span{&press, 1});
    CHECK(result.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(result.get().compose_input_count == 0);
}

namespace {

struct sdl_event_scope_t {
    SDL_Keymod previous{SDL_GetModState()};
    ~sdl_event_scope_t() {
        SDL_SetModState(previous);
        SDL_QuitSubSystem(SDL_INIT_EVENTS);
    }
};

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

void expect_lane_key(bool value, std::source_location source = std::source_location::current()) {
    INFO("Lane key expectation at " << source.line());
    REQUIRE(value);
}

void check_number_key_placement(bool paint_mode, int selection = 0) {
    nuklear_event_harness_t harness;
    expect_lane_key(harness.ready());
    constexpr std::array<SDL_Scancode, 10> keys{SDL_SCANCODE_1, SDL_SCANCODE_2,
        SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
        SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0};
    std::array<WormUiComposeLaneKeyBinding, 10> bindings{};
    for (std::size_t index = 0; index < keys.size(); ++index)
        bindings[index] = {.scancode = static_cast<std::uint32_t>(keys[index]), .lane_id = index + 1};
    auto& view = harness.view().compose;
    view.lane_key_bindings = bindings.data();
    view.lane_key_binding_count = bindings.size();
    view.note_paint_mode_active = paint_mode;
    view.selected_note_count = selection == 1 ? 1 : 0;
    view.selected_bgm_event_count = selection == 2 ? 1 : 0;
    (void)harness.frame();
    for (std::size_t index = 0; index < keys.size(); ++index) {
        INFO("paint mode " << paint_mode << ", lane " << index + 1);
        const auto press = key_event(SDL_EVENT_KEY_DOWN, index == 9 ? SDLK_0 : SDLK_1 + index, keys[index]);
        const auto down = harness.frame(std::span{&press, 1});
        if (!paint_mode && selection == 0) {
            expect_lane_key(down.get().compose_input_count == 0);
            expect_lane_key(!worm_ui_nuklear_compose_state().lane_hold_active);
            const auto release = key_event(SDL_EVENT_KEY_UP, press.key.key, keys[index]);
            expect_lane_key(harness.frame(std::span{&release, 1}).get().compose_input_count == 0);
            continue;
        }
        expect_lane_key(down.get().compose_input_count == 1);
        expect_lane_key(down.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
        expect_lane_key(down.get().compose_inputs[0].lane_id == index + 1);
        expect_lane_key(harness.frame().get().compose_input_count == 0);
        const auto release = key_event(SDL_EVENT_KEY_UP, press.key.key, keys[index]);
        const auto up = harness.frame(std::span{&release, 1});
        expect_lane_key(up.get().compose_input_count == 1);
        expect_lane_key(up.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
        expect_lane_key(up.get().compose_inputs[0].lane_id == index + 1);
    }
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

auto click_at(nuklear_event_harness_t& harness, float x, float y, bool twice = false)
    -> std::vector<workspace_action_snapshot_t> {
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(x, y), button_event(true, x, y)};
    script[1].events = {motion_event(x, y), button_event(false, x, y)};
    script[0].events[1].button.clicks = twice ? 2 : 1;
    script[1].events[1].button.clicks = twice ? 2 : 1;
    return harness.run_script(script);
}

void check_sound_layer_confirmation_choice(bool overwrite) {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.keysounds_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sound_layer_confirmation_open = true;
    state.sound_layer_confirmation_project = view.project_id;
    state.sound_layer_confirmation_chart = view.chart_id;
    state.sound_layer_confirmation_revision = view.document_revision;
    state.sound_layer_confirmation_layer = 7;
    state.sound_layer_confirmation_resources = {1, 2};
    (void)harness.frame();
    const auto choice = harness.drawn_text_center(overwrite ? "Overwrite" : "Keep existing layers");
    REQUIRE(choice.has_value());
    const auto frames = click_at(harness, choice->first, choice->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER;
    });
    REQUIRE(found != frames.end());
    CHECK(found->get().compose_sound_overwrite_note_layers == overwrite);
    CHECK(found->get().compose_semantic_layer_id == 7);
    CHECK(found->get().compose_sound_resource_count == 2);
    CHECK_FALSE(state.sound_layer_confirmation_open);
}

auto check_no_toolbar_document_actions(const std::vector<workspace_action_snapshot_t>& frames)
    -> void {
    for (const auto& snapshot : frames)
        CHECK(snapshot.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

auto delete_shortcut_has_compose_context(const WormUiWorkspaceAction& action) -> bool {
    return action.shortcut_event_count == 1 &&
           action.shortcut_events[0].hid_usage ==
               static_cast<std::uint16_t>(SDL_SCANCODE_DELETE) &&
           !action.shortcut_events[0].repeat && action.shortcut_window_focused &&
           !action.shortcut_text_entry_active && !action.shortcut_ime_composition_active;
}

} // namespace

TEST_CASE("Headless 10K lane keys create only with Draw and preserve Select reassignment",
          "[frontend-state][event-harness][lane-keys][regression]") {
    check_number_key_placement(false);
    check_number_key_placement(true);
    check_number_key_placement(false, 1);
    check_number_key_placement(false, 2);
}

TEST_CASE("Sound layer popup buttons dispatch keep and overwrite choices",
          "[unit][frontend-state][event-harness][sound-note-layers]") {
    check_sound_layer_confirmation_choice(false);
    check_sound_layer_confirmation_choice(true);
}

TEST_CASE("Headless frontend routes a wheel event over Compose to viewport input",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
    REQUIRE(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_NONE);

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

    REQUIRE(action.get().compose_viewport_input_count == 1);
    CHECK(action.get().compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
    CHECK(action.get().compose_viewport_inputs[0].delta_y != 0.0);
}

TEST_CASE("Headless frontend emits Delete shortcut press with focused Compose context",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto pressed = harness.frame(std::span{&press, 1});
    CHECK(delete_shortcut_has_compose_context(pressed.get()));
}

TEST_CASE("Headless frontend captures Ctrl+S as a registered shortcut",
          "[frontend-state][event-harness][save-shortcut]") {
    nuklear_event_harness_t harness;
    (void)harness.frame();
    const auto save = key_event(SDL_EVENT_KEY_DOWN, SDLK_S, SDL_SCANCODE_S);
    const auto pressed = harness.frame(std::span{&save, 1}, SDL_KMOD_CTRL);
    REQUIRE(pressed.get().shortcut_event_count == 1);
    CHECK(pressed.get().shortcut_events[0].hid_usage == SDL_SCANCODE_S);
    CHECK(pressed.get().shortcut_events[0].modifiers == WORM_UI_SHORTCUT_MODIFIER_CONTROL);
    CHECK_FALSE(pressed.get().shortcut_events[0].repeat);
}

TEST_CASE("Headless frontend marks repeated Ctrl+S for registry suppression",
          "[frontend-state][event-harness][save-shortcut]") {
    nuklear_event_harness_t harness;
    (void)harness.frame();
    const auto save = key_event(SDL_EVENT_KEY_DOWN, SDLK_S, SDL_SCANCODE_S, true);
    const auto repeated = harness.frame(std::span{&save, 1}, SDL_KMOD_CTRL);
    REQUIRE(repeated.get().shortcut_event_count == 1);
    CHECK(repeated.get().shortcut_events[0].repeat);
}

TEST_CASE("Queued Save reuses File Save and fires only once",
          "[frontend-state][event-harness][save-shortcut]") {
    nuklear_event_harness_t harness;
    auto& view = harness.view();
    view.project_has_native_path = true;
    (void)harness.frame();
    CHECK(worm_ui_frontend_request_save_project(view.project_id));
    CHECK(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    CHECK(harness.frame().get().kind != WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
}

TEST_CASE("Queued Save is discarded when its project changes",
          "[frontend-state][event-harness][save-shortcut]") {
    nuklear_event_harness_t harness;
    auto& view = harness.view();
    view.project_has_native_path = true;
    (void)harness.frame();
    CHECK(worm_ui_frontend_request_save_project(view.project_id));
    ++view.project_id;
    CHECK(harness.frame().get().kind != WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    CHECK(harness.frame().get().kind != WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
}

TEST_CASE("Queued Save is discarded when a modal intervenes",
          "[frontend-state][event-harness][save-shortcut]") {
    nuklear_event_harness_t harness;
    auto& view = harness.view();
    view.project_has_native_path = true;
    (void)harness.frame();
    CHECK(worm_ui_frontend_request_save_project(view.project_id));
    view.close_confirmation.visible = true;
    CHECK(harness.frame().get().kind != WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    view.close_confirmation.visible = false;
    CHECK(harness.frame().get().kind != WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
}

TEST_CASE("Headless frontend routes Alt digit to Compose while Select remains active",
          "[frontend-state][event-harness][palette-shortcuts]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    (void)harness.frame();
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    const auto action = harness.frame(std::span{&press, 1}, SDL_KMOD_LALT);
    REQUIRE(action.get().shortcut_event_count == 1);
    CHECK(action.get().shortcut_events[0].hid_usage == SDL_SCANCODE_2);
    CHECK_FALSE(action.get().shortcut_text_entry_active);
    CHECK_FALSE(harness.view().compose.note_paint_mode_active);
}

TEST_CASE("Headless frontend routes Alt1 after the Hold shortcut", "[frontend-state][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    (void)harness.frame();
    const auto hold = key_event(SDL_EVENT_KEY_DOWN, SDLK_3, SDL_SCANCODE_3);
    const auto painting = harness.frame(std::span{&hold, 1}, SDL_KMOD_LALT);
    REQUIRE(painting.get().shortcut_event_count == 1);
    CHECK(painting.get().shortcut_events[0].hid_usage == SDL_SCANCODE_3);
    harness.view().compose.note_paint_mode_active = true;
    (void)harness.frame();
    const auto select = key_event(SDL_EVENT_KEY_DOWN, SDLK_1, SDL_SCANCODE_1);
    const auto selecting = harness.frame(std::span{&select, 1}, SDL_KMOD_LALT);
    REQUIRE(selecting.get().shortcut_event_count == 1);
    CHECK(selecting.get().shortcut_events[0].hid_usage == SDL_SCANCODE_1);
    CHECK_FALSE(selecting.get().shortcut_text_entry_active);
}

TEST_CASE("Headless frontend ignores Delete shortcut release",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto released = harness.frame(std::span{&release, 1});
    CHECK(released.get().shortcut_event_count == 0);
}

TEST_CASE("Headless frontend marks repeated Delete shortcut press",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    const auto repeat = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE, true);
    const auto repeated = harness.frame(std::span{&repeat, 1});
    REQUIRE(repeated.get().shortcut_event_count == 1);
    CHECK(repeated.get().shortcut_events[0].repeat);
}

TEST_CASE("Headless frontend tab click emits a workspace tab action",
          "[unit][frontend-state][frontend][event-harness]") {
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
    CHECK(actions[0].get().kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB);
    CHECK(actions[0].get().tab == WORM_UI_WORKSPACE_TAB_SETUP);
}

TEST_CASE("Headless frontend suppresses Delete shortcut while new-project wizard is open",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());
    const auto delete_key = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto modal_key_action = harness.frame(std::span{&delete_key, 1});
    CHECK(modal_key_action.get().shortcut_event_count == 0);
}

TEST_CASE("Headless frontend new-project wizard cancel closes and permits reopening",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());
    static_cast<void>(harness.frame());  // The dialog fits its content on its second frame.

    const auto cancel = harness.drawn_text_center("Cancel");
    REQUIRE(cancel);
    const auto cancel_actions = click_at(harness, cancel->first, cancel->second);
    CHECK(cancel_actions[0].get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(worm_ui_frontend_request_new_project());
}

TEST_CASE("Headless frontend new-project wizard creates the default 7K layout",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());
    static_cast<void>(harness.frame());  // The dialog fits its content on its second frame.

    const auto create = harness.drawn_text_center("Create project");
    REQUIRE(create);
    const auto create_actions = click_at(harness, create->first, create->second);
    CHECK(create_actions[0].get().kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
    CHECK(create_actions[0].get().new_project_layout_index == 3);
}

namespace {

void check_wheel_zoom(const workspace_action_snapshot_t& snapshot, bool zoom) {
    const auto& action = snapshot.get();
    REQUIRE(action.compose_viewport_input_count == 1);
    CHECK(action.compose_viewport_inputs[0].zoom_modifier == zoom);
    CHECK(action.compose_viewport_inputs[0].delta_y != 0.0);
}

void check_modifier_fixture_isolation() {
    CHECK(SDL_GetModState() == SDL_KMOD_ALT);
    nuklear_event_harness_t next;
    REQUIRE(next.ready());
    static_cast<void>(next.frame());
    CHECK(SDL_GetModState() == SDL_KMOD_ALT);
}

void check_snapshot_tags(const WormUiWorkspaceAction& action) {
    REQUIRE(action.metadata_search_tag_count == 3);
    CHECK(std::string{action.metadata_search_tags[0]} == "original tag");
    CHECK(std::string{action.metadata_search_tags[1]}.empty());
    CHECK(action.metadata_search_tags[2] == nullptr);
}

} // namespace

namespace {
void expect_latched_input(bool value, std::source_location where = std::source_location::current()) {
    INFO("Latched input expectation at " << where.line());
    REQUIRE(value);
}

auto ctrl_space_frame(nuklear_event_harness_t& harness, bool down = true, bool repeat = false)
    -> workspace_action_snapshot_t {
    const auto event = key_event(down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP,
                                 SDLK_SPACE, SDL_SCANCODE_SPACE, repeat);
    return harness.frame(std::span{&event, 1}, SDL_KMOD_CTRL);
}

void check_cursor_wheel_direction(bool downscroll) {
    nuklear_event_harness_t harness;
    harness.view().compose.downscroll = downscroll;
    (void)harness.frame();
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 0.125f;
    wheel.wheel.mouse_x = 480;
    wheel.wheel.mouse_y = 400;
    const auto first = harness.frame(std::span{&wheel, 1}, SDL_KMOD_ALT);
    expect_latched_input(first.get().compose_viewport_input_count == 0);
    expect_latched_input(first.get().compose_input_count == 1);
    expect_latched_input(first.get().compose_inputs[0].kind == (downscroll
        ? WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD : WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD));
    wheel.wheel.y = -0.125f;
    expect_latched_input(harness.frame(std::span{&wheel, 1}, SDL_KMOD_ALT).get().compose_input_count == 0);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_LALT, SDL_SCANCODE_LALT);
    (void)harness.frame(std::span{&release, 1});
    const auto next = harness.frame(std::span{&wheel, 1}, SDL_KMOD_ALT);
    expect_latched_input(next.get().compose_input_count == 1);
    expect_latched_input(next.get().compose_inputs[0].kind == (downscroll
        ? WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD : WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD));
}
} // namespace

TEST_CASE("Headless Alt wheel latches fractional motion and respects field orientation",
          "[unit][frontend-state][frontend][event-harness][alt-wheel]") {
    check_cursor_wheel_direction(false);
    check_cursor_wheel_direction(true);
}

TEST_CASE("Headless Ctrl Space toggles range selection independently of key release",
          "[unit][frontend-state][frontend][event-harness][ctrl-space]") {
    nuklear_event_harness_t harness;
    (void)harness.frame();
    const auto started = ctrl_space_frame(harness);
    expect_latched_input(started.get().compose_input_count == 1);
    expect_latched_input(started.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION);
    expect_latched_input(worm_ui_nuklear_compose_state().space_range_latched);
    expect_latched_input(ctrl_space_frame(harness, true, true).get().compose_input_count == 0);
    expect_latched_input(ctrl_space_frame(harness, false).get().compose_input_count == 0);
    (void)harness.frame();
    expect_latched_input(worm_ui_nuklear_compose_state().space_range_latched);
    const auto ended = ctrl_space_frame(harness);
    expect_latched_input(ended.get().compose_input_count == 1);
    expect_latched_input(ended.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION);
    expect_latched_input(!worm_ui_nuklear_compose_state().space_range_hold_active);
}

TEST_CASE("Headless Escape and focus loss cancel latched selection",
          "[unit][frontend-state][frontend][event-harness][ctrl-space]") {
    nuklear_event_harness_t harness;
    (void)harness.frame();
    (void)ctrl_space_frame(harness);
    const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto cancelled = harness.frame(std::span{&escape, 1});
    expect_latched_input(cancelled.get().compose_input_count != 0);
    expect_latched_input(!worm_ui_nuklear_compose_state().space_range_latched);
    (void)ctrl_space_frame(harness);
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)harness.frame(std::span{&lost, 1});
    expect_latched_input(!worm_ui_nuklear_compose_state().space_range_latched);
}

TEST_CASE("Headless frontend isolates scripted Ctrl wheel zoom and release",
          "[unit][frontend-state][frontend][event-harness]") {
    REQUIRE(SDL_Init(SDL_INIT_EVENTS));
    const sdl_event_scope_t restore;
    SDL_SetModState(SDL_KMOD_ALT);
    {
        nuklear_event_harness_t harness;
        REQUIRE(harness.ready());
        static_cast<void>(harness.frame());
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = 1.0f;
        wheel.wheel.mouse_x = 480.0f;
        wheel.wheel.mouse_y = 400.0f;
        const std::array<event_step_t, 4> script{{
            {{key_event(SDL_EVENT_KEY_DOWN, SDLK_LCTRL, SDL_SCANCODE_LCTRL)}, SDL_KMOD_LCTRL},
            {{wheel}, SDL_KMOD_LCTRL},
            {{key_event(SDL_EVENT_KEY_UP, SDLK_LCTRL, SDL_SCANCODE_LCTRL)}, SDL_KMOD_NONE},
            {{wheel}, SDL_KMOD_NONE},
        }};
        const auto actions = harness.run_script(script);
        check_wheel_zoom(actions[1], true);
        check_wheel_zoom(actions[3], false);
        CHECK(SDL_GetModState() == SDL_KMOD_ALT);
        const auto normal = harness.frame(std::span{&wheel, 1});
        check_wheel_zoom(normal, false);
        CHECK(SDL_GetModState() == SDL_KMOD_ALT);
    }
    check_modifier_fixture_isolation();
}

TEST_CASE("Action snapshots own metadata payloads across copies moves and destruction",
          "[unit][frontend-state][frontend][event-harness]") {
    using text_member_t = const char* WormUiWorkspaceAction::*;
    constexpr std::array<text_member_t, 11> fields{
        &WormUiWorkspaceAction::metadata_chart_rating_numerator_text,
        &WormUiWorkspaceAction::metadata_chart_rating_denominator_text,
        &WormUiWorkspaceAction::metadata_audio_offset_numerator_text,
        &WormUiWorkspaceAction::metadata_audio_offset_denominator_text,
        &WormUiWorkspaceAction::metadata_title,
        &WormUiWorkspaceAction::metadata_artist,
        &WormUiWorkspaceAction::metadata_chart_name,
        &WormUiWorkspaceAction::metadata_chart_author,
        &WormUiWorkspaceAction::metadata_format_name,
        &WormUiWorkspaceAction::metadata_format_value,
        &WormUiWorkspaceAction::metadata_output_path,
    };
    const auto make_snapshot = [&] {
        WormUiWorkspaceAction action{};
        std::array<std::string, 11> strings;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            strings[i] = std::to_string(i) + std::string(100, 'x');
            action.*fields[i] = strings[i].c_str();
        }
        std::string tag = "original tag";
        const char* tags[]{tag.c_str(), "", nullptr};
        action.metadata_search_tags = tags;
        action.metadata_search_tag_count = 3;
        action.compose_viewport_input_count = 1;
        action.compose_viewport_inputs[0].zoom_modifier = true;
        auto snapshot = workspace_action_snapshot_t{action};
        strings.fill("overwritten");
        tag.assign("changed");
        return snapshot;
    };
    auto original = make_snapshot();
    auto copy = original;
    auto moved = std::move(copy);
    auto assigned = workspace_action_snapshot_t{{}};
    assigned = moved;
    auto move_assigned = workspace_action_snapshot_t{{}};
    move_assigned = std::move(assigned);
    std::vector<workspace_action_snapshot_t> history;
    history.push_back(std::move(move_assigned));
    original = workspace_action_snapshot_t{{}};
    moved = workspace_action_snapshot_t{{}};
    for (int i = 0; i < 32; ++i) history.push_back(workspace_action_snapshot_t{{}});
    const auto& action = history.front().get();
    for (std::size_t i = 0; i < fields.size(); ++i)
        CHECK(std::string{action.*fields[i]} == std::to_string(i) + std::string(100, 'x'));
    check_snapshot_tags(action);
    CHECK(action.compose_viewport_inputs[0].zoom_modifier);
    CHECK(history.back().get().metadata_title == nullptr);
    CHECK(history.back().get().metadata_search_tags == nullptr);
}

namespace {

auto edit_metadata_title(nuklear_event_harness_t& harness, const char* value)
    -> std::vector<workspace_action_snapshot_t> {
    // At 960x720 the title field is (287,135,631,26); Apply is (822,224,96,23).
    static_cast<void>(click_at(harness, 450.0f, 148.0f));
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    // These ASCII fixture strings use one codepoint per backend text event.
    for (const char* next = value; *next; ++next) {
        const char codepoint[]{*next, '\0'};
        text.text.text = codepoint;
        static_cast<void>(harness.frame(std::span{&text, 1}));
    }
    auto actions = click_at(harness, 870.0f, 235.0f);
    REQUIRE(actions[0].get().metadata_edit == WORM_UI_METADATA_EDIT_SONG);
    return actions;
}

} // namespace

TEST_CASE("Headless metadata text history survives later compose and frontend shutdown",
          "[unit][frontend-state][frontend][event-harness]") {
    std::vector<workspace_action_snapshot_t> history;
    {
        nuklear_event_harness_t harness;
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.compose.visible = false;
        view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        view.metadata.visible = true;
        view.metadata.song_fields_editable = true;
        view.metadata.search_tags_editable = true;
        view.metadata.title = "";
        view.metadata.artist = "Artist";
        view.metadata.artist_bytes = 6;
        static_cast<void>(harness.frame());
        history = edit_metadata_title(harness, "First");
        REQUIRE(std::string{history[0].get().metadata_title} == "First");
        // Simulate the next projected view, then edit the same frontend buffer.
        view.metadata.title = "First";
        view.metadata.title_bytes = 5;
        ++view.metadata.document_revision;
        static_cast<void>(harness.frame());
        const auto later = edit_metadata_title(harness, "Second");
        CHECK(std::string{later[0].get().metadata_title} == "FirstSecond");
        CHECK(std::string{history[0].get().metadata_title} == "First");
    }
    auto copied = history;
    history.clear();
    auto moved = std::move(copied);
    CHECK(std::string{moved[0].get().metadata_title} == "First");
    CHECK(std::string{moved[0].get().metadata_artist} == "Artist");
}

namespace {

constexpr std::array project_chart_text_fields{
    &WormUiProjectChartAction::author,
    &WormUiProjectChartAction::rating_numerator,
    &WormUiProjectChartAction::rating_denominator,
};

auto project_chart_snapshot_from_local_buffers() -> workspace_action_snapshot_t {
    WormUiWorkspaceAction source{};
    std::array<std::string, 3> strings{"Chart author", std::string(100, '9'), "7"};
    for (std::size_t i = 0; i < strings.size(); ++i)
        source.project_chart.*project_chart_text_fields[i] = strings[i].c_str();
    auto snapshot = workspace_action_snapshot_t{source};
    for (auto& value : strings) value[0] = '!';
    // Check while the source still lives: the shallow-copy negative control
    // fails safely here before any borrowed pointer could become dangling.
    for (std::size_t i = 0; i < strings.size(); ++i)
        REQUIRE((snapshot.get().project_chart.*project_chart_text_fields[i])[0] != '!');
    return snapshot;
}

void check_project_chart_snapshot(const workspace_action_snapshot_t& snapshot) {
    const auto& chart = snapshot.get().project_chart;
    CHECK(std::string{chart.author} == "Chart author");
    CHECK(std::string{chart.rating_numerator} == std::string(100, '9'));
    CHECK(std::string{chart.rating_denominator} == "7");
}

void check_project_chart_null_and_empty() {
    for (const auto member : project_chart_text_fields) {
        WormUiWorkspaceAction source{};
        const workspace_action_snapshot_t absent{source};
        CHECK(absent.get().project_chart.*member == nullptr);
        char empty[]{'\0', '\0'};
        source.project_chart.*member = empty;
        const workspace_action_snapshot_t present{source};
        empty[0] = 'x';
        REQUIRE(present.get().project_chart.*member != nullptr);
        CHECK(std::string{present.get().project_chart.*member}.empty());
    }
}

} // namespace

TEST_CASE("Action snapshots own nested project chart text through source destruction",
          "[unit][frontend-state][frontend][event-harness]") {
    auto original = project_chart_snapshot_from_local_buffers();
    auto copied = original;
    auto moved = std::move(copied);
    auto assigned = workspace_action_snapshot_t{{}};
    assigned = moved;
    auto move_assigned = workspace_action_snapshot_t{{}};
    move_assigned = std::move(assigned);
    std::vector<workspace_action_snapshot_t> history;
    history.push_back(std::move(move_assigned));
    original = workspace_action_snapshot_t{{}};
    moved = workspace_action_snapshot_t{{}};
    for (int i = 0; i < 32; ++i) history.push_back(workspace_action_snapshot_t{{}});
    check_project_chart_snapshot(history.front());
    check_project_chart_null_and_empty();
}

TEST_CASE("Compose toolbar color views change frontend state without document actions",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    (void)harness.frame();
    using View = worm_ui_nuklear_compose_color_view_t;
    const auto revision = harness.view().compose.document_revision;
    // 24 px toolbar icons: navigation, separator, six tools, separator, views.
    constexpr std::array views{View::snap, View::layer, View::normal};
    // The BGA lane visibility and Y-lock icons add two 27 px cells before the color group.
    constexpr std::array positions{396.0f, 423.0f, 369.0f};
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto clicks = click_at(harness, positions[index], 122.0f);
        CHECK(worm_ui_nuklear_compose_state().color_view == views[index]);
        check_no_toolbar_document_actions(clicks);
        CHECK(harness.view().compose.document_revision == revision);
    }
}

namespace {
void check_layer_row_action(const workspace_action_snapshot_t& snapshot,
                            WormUiComposeActionKind kind, std::uint64_t layer,
                            const std::source_location location =
                                std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(snapshot.get().compose_action == kind);
    CHECK(snapshot.get().compose_semantic_layer_id == layer);
}
} // namespace

TEST_CASE("Layers dock rows choose the target and send eye and lock actions for that row",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = false;
    state.sounds_panel_visible = false;
    state.layers_panel_visible = true;
    std::array<WormUiComposeSemanticLayerView, 2> layers{{
        {.layer_id = 7, .name = "Lead", .red = 255, .assigned_note_count = 3, .visible = true},
        {.layer_id = 9, .name = "Hidden", .blue = 255, .assigned_note_count = 2, .visible = false},
    }};
    auto& view = harness.view().compose;
    view.semantic_layers = layers.data();
    view.semantic_layer_count = layers.size();
    view.semantic_layer_total_count = layers.size();
    (void)harness.frame();
    CHECK(harness.drawn_text().find("3 objects") != std::string::npos);
    const auto row = harness.drawn_text_center("Lead");
    REQUIRE(row.has_value());
    const auto chosen = click_at(harness, row->first, row->second);
    check_layer_row_action(chosen.back(), WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER, 7);
    layers.front().selected = true;
    view.semantic_layer_target = layers.front();
    (void)harness.frame();
    const auto hidden_row = harness.drawn_text_center("Hidden");
    REQUIRE(hidden_row.has_value());
    const auto eye = click_at(harness, 1200, hidden_row->second);
    check_layer_row_action(eye[0], WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, 9);
    const auto lock = click_at(harness, 1225, hidden_row->second);
    check_layer_row_action(lock[0], WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK, 9);
}

TEST_CASE("Layers dock No layer row requests clear assignment without eye or lock actions",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = false;
    state.sounds_panel_visible = false;
    state.layers_panel_visible = true;
    auto& view = harness.view().compose;
    view.semantic_layer_unassigned_note_count = 4;
    view.selected_note_count = 2;
    const auto revision = view.document_revision;
    (void)harness.frame();
    const auto no_layer = harness.drawn_text_center("No layer");
    REQUIRE(no_layer.has_value());
    const auto clicked = click_at(harness, no_layer->first, no_layer->second);
    check_layer_row_action(clicked.back(), WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER, 0);
    CHECK(view.document_revision == revision);
    view.semantic_layer_target_none = true;
    (void)harness.frame();
    const auto no_layer_row = harness.drawn_text_center("No layer");
    REQUIRE(no_layer_row.has_value());
    const auto no_eye = click_at(harness, no_layer_row->first, no_layer_row->second);
    check_layer_row_action(no_eye.back(), WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER, 0);
}

namespace {

auto right_click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    auto down = button_event(true, x, y);
    auto up = button_event(false, x, y);
    down.button.button = SDL_BUTTON_RIGHT;
    up.button.button = SDL_BUTTON_RIGHT;
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(x, y), down};
    script[1].events = {motion_event(x, y), up};
    return harness.run_script(script);
}

void check_layer_menu(bool condition) { CHECK(condition); }

void check_layer_menu_no_actions(const std::vector<workspace_action_snapshot_t>& frames,
                                 const std::source_location location =
                                     std::source_location::current()) {
    for (const auto& frame : frames) {
        INFO("assertion caller line " << location.line() << ", compose action "
             << frame.get().compose_action << ", compose inputs "
             << frame.get().compose_input_count << ", viewport inputs "
             << frame.get().compose_viewport_input_count);
        CHECK(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        CHECK(frame.get().compose_input_count == 0);
        CHECK(frame.get().compose_viewport_input_count == 0);
    }
}

struct layer_menu_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<WormUiComposeSemanticLayerView, 2> layers{{
        {.layer_id = 7, .name = "Lead", .red = 255, .assigned_note_count = 3, .visible = true},
        {.layer_id = 9, .name = "Locked", .blue = 255, .assigned_note_count = 2,
         .visible = true, .locked = true},
    }};

    layer_menu_fixture_t() {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.inspector_visible = false;
        state.sounds_panel_visible = false;
        state.layers_panel_visible = true;
        state.color_view = worm_ui_nuklear_compose_color_view_t::normal;
        auto& view = harness.view().compose;
        view.semantic_layers = layers.data();
        view.semantic_layer_count = layers.size();
        view.semantic_layer_total_count = layers.size();
        view.selected_note_count = 3;
        (void)harness.frame();
    }

    void open(std::size_t index = 0) {
        const auto row =
            harness.drawn_text_center(index == 0 ? "Lead" : "Locked");
        REQUIRE(row.has_value());
        const auto clicks = right_click_at(harness, row->first, row->second);
        check_layer_row_action(clicks.front(), WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER,
                               layers[index].layer_id);
        layers[index].selected = true;
        harness.view().compose.semantic_layer_target = layers[index];
        (void)harness.frame();
        check_layer_menu(worm_ui_nuklear_compose_state().layers_menu.open);
    }
};

} // namespace

TEST_CASE("Layer row menu hides its captured target without assigning selected notes",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    const auto hide_center = fixture.harness.drawn_text_center("Hide");
    REQUIRE(hide_center.has_value());
    const auto hide = click_at(fixture.harness, hide_center->first, hide_center->second);
    check_layer_row_action(hide.front(), WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, 7);
    fixture.layers.front().visible = false;
    fixture.harness.view().compose.semantic_layer_target = fixture.layers.front();
    ++fixture.harness.view().compose.document_revision;
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
    const auto eye = click_at(fixture.harness, 1200, 233);
    check_layer_row_action(eye.front(), WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, 7);
}

TEST_CASE("Layer row menu carries the retired inspector layer buttons",
          "[unit][frontend-state][frontend][event-harness]") {
    const std::array<std::pair<const char*, WormUiComposeActionKind>, 2> entries{{
        {"Add/remove selected notes", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER},
        {"Stop targeting this layer", WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER}}};
    for (const auto& [label, kind] : entries) {
        INFO(label);
        layer_menu_fixture_t fixture;
        fixture.open();
        const auto center = fixture.harness.drawn_text_center(label);
        REQUIRE(center.has_value());
        const auto clicked = click_at(fixture.harness, center->first, center->second);
        check_layer_row_action(clicked.front(), kind, 7);
    }
}

TEST_CASE("Layer row menu disables Add/remove selected notes without a selection",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.harness.view().compose.selected_note_count = 0;
    fixture.open();
    const auto center = fixture.harness.drawn_text_center("Add/remove selected notes");
    REQUIRE(center.has_value());
    const auto clicked = click_at(fixture.harness, center->first, center->second);
    check_layer_menu_no_actions(clicked);
}

TEST_CASE("Layer row menu captures keyboard shortcuts and Escape closes without an action",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    const std::array undo{key_event(SDL_EVENT_KEY_DOWN, SDLK_Z, SDL_SCANCODE_Z)};
    const auto suppressed = fixture.harness.frame(undo, SDL_KMOD_CTRL);
    check_layer_menu(suppressed.get().shortcut_text_entry_active);
    check_layer_menu(suppressed.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    const std::array release{key_event(SDL_EVENT_KEY_UP, SDLK_Z, SDL_SCANCODE_Z)};
    (void)fixture.harness.frame(release);
    SDL_Event plus{};
    plus.type = SDL_EVENT_TEXT_INPUT;
    plus.text.text = "+";
    const std::array text{plus};
    const auto blocked_text = fixture.harness.frame(text);
    check_layer_menu(blocked_text.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    const std::array escape{key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    const auto closed = fixture.harness.frame(escape);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
    check_layer_menu(closed.get().shortcut_text_entry_active);
    check_layer_menu(closed.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_layer_menu(closed.get().compose_input_count == 0);
}

TEST_CASE("Layer row menu discards both halves of an outside click",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    const auto outside = click_at(fixture.harness, 500, 400);
    check_layer_menu_no_actions(outside);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.release_pending);
}

TEST_CASE("Layer row menu owns Escape and outside clicks ahead of notification toasts",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    auto& notifications = fixture.harness.view().notifications;
    notifications.toast_count = 1;
    notifications.toasts[0] = {.id = 42, .severity = WORM_UI_NOTIFICATION_ERROR,
                               .message = "Keep this notification", .repeat_count = 1};
    (void)fixture.harness.frame();
    fixture.open();
    const std::array escape{key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    const auto closed = fixture.harness.frame(escape);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
    check_layer_menu(closed.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    const std::array release{key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    (void)fixture.harness.frame(release);
    (void)fixture.harness.frame();
    fixture.open();
    const auto outside = click_at(fixture.harness, 1200, 820);
    check_layer_menu_no_actions(outside);
    for (const auto& frame : outside)
        check_layer_menu(frame.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
}

TEST_CASE("Layer row menu releases its popup after a stale revision or hidden panel",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    ++fixture.harness.view().compose.document_revision;
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
    (void)fixture.harness.frame();
    fixture.open();
    worm_ui_nuklear_compose_state().layers_panel_visible = false;
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.open);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu.close_pending);
    (void)fixture.harness.frame();
    (void)click_at(fixture.harness, 396, 122);
    check_layer_menu(worm_ui_nuklear_compose_state().color_view ==
                     worm_ui_nuklear_compose_color_view_t::snap);
}

TEST_CASE("Locked layer row menu disables Rename and Color",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open(1);
    const auto rename_center = fixture.harness.drawn_text_center("Rename…");
    REQUIRE(rename_center.has_value());
    const auto rename = click_at(fixture.harness, rename_center->first, rename_center->second);
    check_layer_menu_no_actions(rename);
    check_layer_menu(worm_ui_nuklear_compose_state().layers_menu.open);
    check_layer_menu(!worm_ui_nuklear_compose_state().semantic_layer_name_editor_visible);
    check_layer_menu(!worm_ui_nuklear_compose_state().semantic_layer_color_editor_visible);
}

TEST_CASE("Layer row Rename editor keeps its captured ID after target and window changes",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    const auto rename_center = fixture.harness.drawn_text_center("Rename…");
    REQUIRE(rename_center.has_value());
    const auto rename = click_at(fixture.harness, rename_center->first, rename_center->second);
    check_layer_menu_no_actions(rename);
    auto& state = worm_ui_nuklear_compose_state();
    check_layer_menu(state.semantic_layer_name_editor_visible);
    std::snprintf(state.semantic_layer_name.data(), state.semantic_layer_name.size(), "Renamed");
    fixture.harness.view().compose.semantic_layer_target = fixture.layers.back();
    fixture.harness.view().compose.semantic_layers = &fixture.layers.back();
    fixture.harness.view().compose.semantic_layer_first_row = 1;
    fixture.harness.view().compose.semantic_layer_count = 1;
    const auto applied = click_at(fixture.harness, 1100, 253);
    check_layer_row_action(applied.front(), WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER, 7);
    check_layer_menu(std::string{applied.front().get().compose_semantic_layer_name} == "Renamed");
    check_layer_menu(!state.semantic_layer_name_editor_visible);
}

namespace {
struct note_menu_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    WormUiComposeHoldView note{.bounds = {200, 0, 500, 900}, .note_id = 42, .lane_id = 1};
    std::array<WormUiComposeSemanticLayerView, 2> layers{{
        {.layer_id = 7, .name = "Lead", .red = 255, .visible = true},
        {.layer_id = 9, .name = "Locked", .blue = 255, .visible = false, .locked = true},
    }};
    note_menu_fixture_t() {
        initialize();
    }
    void initialize() {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.inspector_visible = state.sounds_panel_visible = state.layers_panel_visible = false;
        auto& view = harness.view().compose;
        view.hold_bodies = &note;
        view.hold_body_count = 1;
        view.note_menu_layers = layers.data();
        view.note_menu_layer_count = layers.size();
        view.note_menu_layer_total_count = layers.size();
        view.selected_note_count = 2;
        (void)harness.frame();
    }
    void open() {
        const auto clicks = right_click_at(harness, 300, 300);
        check_layer_menu(clicks.front().get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
        check_layer_menu(clicks.front().get().compose_note_context_id == 42);
        check_layer_menu(compose_note_menu_open());
        (void)click_at(harness, 350, 315);
        (void)harness.frame();
    }
};
} // namespace

auto t422_note_hint_bounds(const nuklear_event_harness_t& harness)
    -> std::optional<WormUiTestingDrawnText> {
    constexpr std::string_view hint{"Shift + lane key moves one, then deselects"};
    const auto bounds = harness.drawn_text_bounds();
    const auto found = std::ranges::find_if(bounds, [hint](const auto& item) {
        return std::string_view(item.text) == hint;
    });
    if (found == bounds.end())
        return std::nullopt;
    return *found;
}

void t422_check_note_hint_fits_view(const nuklear_event_harness_t& harness,
                                    const WormUiTestingRect& viewport) {
    const auto bounds = t422_note_hint_bounds(harness);
    REQUIRE(bounds.has_value());
    CHECK(bounds->x >= viewport.x);
    CHECK(bounds->x + bounds->width <= viewport.x + viewport.width);
    CHECK(bounds->y >= viewport.y);
    CHECK(bounds->y + bounds->height <= viewport.y + viewport.height);
}

void t422_click_hint_without_action(note_menu_fixture_t& fixture,
                                    std::pair<float, float> center) {
    const auto clicked = click_at(fixture.harness, center.first, center.second);
    CHECK(clicked.front().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(compose_note_menu_open());
    const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    (void)fixture.harness.frame(std::span{&escape, 1});
    CHECK_FALSE(compose_note_menu_open());
}

TEST_CASE("Note actions expose a nonclickable Shift lane-key move hint",
          "[frontend-state][event-harness][lane-key-move-and-deselect]") {
    note_menu_fixture_t fixture;
    fixture.note.bounds = {0, 0, 1280, 900};
    (void)fixture.harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    REQUIRE(viewport.has_value());
    const auto opened = right_click_at(fixture.harness,
        viewport->x + viewport->width - 3, viewport->y + viewport->height - 3);
    CHECK(opened.front().get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    CHECK(compose_note_menu_open());
    constexpr std::string_view hint{"Shift + lane key moves one, then deselects"};
    CHECK(fixture.harness.drawn_text().find(hint) != std::string::npos);
    t422_check_note_hint_fits_view(fixture.harness, *viewport);
    const auto center = fixture.harness.drawn_text_center(hint);
    REQUIRE(center.has_value());
    t422_click_hint_without_action(fixture, *center);
}

TEST_CASE("Note right click opens Layer submenu and assigns its captured selection",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    fixture.open();
    const auto chosen = click_at(fixture.harness, 490, 347);
    check_layer_row_action(chosen.front(), WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER, 7);
    check_layer_menu(chosen.front().get().compose_note_context_id == 42);
    check_layer_menu(!compose_note_menu_open());
    check_layer_menu(chosen.back().get().compose_input_count == 0);
}

TEST_CASE("Note Layer submenu clears through No layer and disables locked rows",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    fixture.open();
    const auto locked = click_at(fixture.harness, 490, 375);
    check_layer_menu(locked.front().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_layer_menu(compose_note_menu_open());
    const auto clear = click_at(fixture.harness, 490, 320);
    check_layer_row_action(clear.front(), WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER, 0);
}

TEST_CASE("Note menu captures shortcuts and retires stale revisions",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    fixture.open();
    const std::array undo{key_event(SDL_EVENT_KEY_DOWN, SDLK_Z, SDL_SCANCODE_Z)};
    const auto suppressed = fixture.harness.frame(undo, SDL_KMOD_CTRL);
    check_layer_menu(suppressed.get().shortcut_text_entry_active);
    check_layer_menu(suppressed.get().compose_input_count == 0);
    ++fixture.harness.view().compose.document_revision;
    (void)fixture.harness.frame();
    check_layer_menu(!compose_note_menu_open());
}

TEST_CASE("Empty area band keeps its right click menu when no note was hit",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 0;
    view.area_range_end_row = 9600;
    (void)fixture.harness.frame();
    const auto clicked = right_click_at(fixture.harness, 900, 400);
    check_layer_menu(!compose_note_menu_open());
    check_layer_menu(compose_area_capture_active());
    check_layer_menu(clicked.front().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Area band menu stays drawn and dismissal returns Compose input",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 0;
    view.area_range_end_row = 9600;
    (void)fixture.harness.frame();
    (void)right_click_at(fixture.harness, 900, 400);
    (void)fixture.harness.frame();
    (void)fixture.harness.frame();
    // The popup must survive past its opening frame, not flicker once.
    CHECK(compose_area_menu_open());
    CHECK(fixture.harness.drawn_text_center("Quantize selection").has_value());
    (void)click_at(fixture.harness, 100, 650);
    (void)fixture.harness.frame();
    CHECK_FALSE(compose_area_menu_open());
    CHECK_FALSE(compose_area_menu_consumed());
    // A dismissed menu must not leave the canvas latched.
    (void)click_at(fixture.harness, 900, 400);
    CHECK_FALSE(compose_area_menu_consumed());
}

TEST_CASE("Note submenu requests a bounded scrolling window independent of hidden Layers dock",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    std::array<WormUiComposeSemanticLayerView, WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY> rows{};
    for (std::size_t index = 0; index < rows.size(); ++index)
        rows[index] = {.layer_id = index + 1, .name = "Layer", .visible = true};
    auto& view = fixture.harness.view().compose;
    view.note_menu_layers = rows.data();
    view.note_menu_layer_count = rows.size();
    view.note_menu_layer_total_count = 30;
    view.semantic_layer_first_row = 12;
    view.semantic_layer_total_count = 30;
    fixture.open();
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -8;
    wheel.wheel.mouse_x = 490;
    wheel.wheel.mouse_y = 500;
    const std::array events{motion_event(490, 500), wheel};
    (void)fixture.harness.frame(events);
    const auto request = fixture.harness.frame();
    check_layer_menu(request.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW);
    check_layer_menu(request.get().compose_semantic_layer_first_row > 0);
    check_layer_menu(request.get().compose_semantic_layer_row_count <= WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY);
    check_layer_menu(view.semantic_layer_first_row == 12);
}

TEST_CASE("Note menu closes on Escape and focus loss without leaking canvas input",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    fixture.open();
    const std::array escape{key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    const auto closed = fixture.harness.frame(escape);
    check_layer_menu(!compose_note_menu_open());
    check_layer_menu(closed.get().compose_input_count == 0);
    check_layer_menu(closed.get().shortcut_text_entry_active);
    const std::array released{key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    (void)fixture.harness.frame(released);
    (void)fixture.harness.frame();
    fixture.open();
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    lost.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)fixture.harness.frame(std::span{&lost, 1});
    check_layer_menu(!compose_note_menu_open());
}

TEST_CASE("Note menu owns Escape and outside presses ahead of notification toasts",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    auto& notifications = fixture.harness.view().notifications;
    notifications.toast_count = 1;
    notifications.toasts[0] = {.id = 42, .severity = WORM_UI_NOTIFICATION_ERROR,
                               .message = "Keep this notification", .repeat_count = 1};
    (void)fixture.harness.frame();
    fixture.open();
    const std::array escape{key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    const auto closed = fixture.harness.frame(escape);
    check_layer_menu(!compose_note_menu_open());
    check_layer_menu(closed.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    const std::array release{key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    (void)fixture.harness.frame(release);
    (void)fixture.harness.frame();
    fixture.open();
    const auto outside = click_at(fixture.harness, 1200, 820);
    for (const auto& frame : outside) {
        check_layer_menu(frame.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
        check_layer_menu(frame.get().compose_input_count == 0);
    }
    check_layer_menu(!compose_note_menu_open());
}

TEST_CASE("Note menu retires chart and project changes without a lingering popup lock",
          "[unit][frontend-state][frontend][event-harness]") {
    note_menu_fixture_t fixture;
    fixture.open();
    ++fixture.harness.view().compose.chart_id;
    (void)fixture.harness.frame();
    check_layer_menu(!compose_note_menu_open());
    (void)fixture.harness.frame();
    fixture.open();
    ++fixture.harness.view().compose.project_id;
    (void)fixture.harness.frame();
    check_layer_menu(!compose_note_menu_open());
    (void)fixture.harness.frame();
    (void)click_at(fixture.harness, 396, 122);
    check_layer_menu(worm_ui_nuklear_compose_state().color_view ==
                     worm_ui_nuklear_compose_color_view_t::snap);
}

namespace {
void hover_at(nuklear_event_harness_t& harness, float x, float y) {
    const std::array events{motion_event(x, y)};
    (void)harness.frame(events);
}
void check_tooltip_text(const nuklear_event_harness_t& harness, const char* text) {
    INFO(harness.drawn_text());
    CHECK(harness.drawn_text().find(text) != std::string::npos);
}
void check_no_tooltip_text(const nuklear_event_harness_t& harness, const char* text) {
    CHECK(harness.drawn_text().find(text) == std::string::npos);
}
} // namespace

TEST_CASE("Unavailable overview tooltip stays on its row and preserves panel tooltips",
          "[unit][frontend-state][frontend][event-harness][tooltip]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& compose = harness.view().compose;
    compose.has_bgm_events = true;
    compose.bgm_bar_reserved = true;
    compose.bgm_bar_available = false;
    (void)harness.frame();
    // The dock toggles sit at the right edge of the toolbar (T-401); the T-268 toggle shifts them left.
    hover_at(harness, 1123, 174);
    check_tooltip_text(harness, "Hide sounds list");
    check_no_tooltip_text(harness, "Audio is unavailable for this chart overview.");
    hover_at(harness, 1177, 174);
    check_tooltip_text(harness, "Show layers");
    check_no_tooltip_text(harness, "Audio is unavailable for this chart overview.");
    hover_at(harness, 351, 120);
    check_tooltip_text(harness, "Audio is unavailable for this chart overview.");
    check_no_tooltip_text(harness, "Hide sounds list");
}

TEST_CASE("Disabled song bar icon keeps its tooltip and emits no toggle action",
          "[unit][frontend-state][frontend][event-harness][tooltip]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    harness.view().compose.bgm_bar_available = false;
    (void)harness.frame();
    // The T-268 toolbar toggle shifts the song-bar icon left.
    hover_at(harness, 1231, 122);
    check_tooltip_text(harness, "Song audio is unavailable");
    const auto clicked = click_at(harness, 1231, 122);
    check_no_toolbar_document_actions(clicked);
    for (const auto& frame : clicked) {
        const auto& action = frame.get();
        for (size_t index = 0; index < action.compose_input_count; ++index)
            CHECK(action.compose_inputs[index].kind != WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE);
    }
}

TEST_CASE("Stopped Compose seek bar emits navigation and drag seeks",
          "[unit][frontend-state][event-harness][seek-bar]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    harness.view().transport_available = true;
    harness.view().transport_end_frame = 4'410'000;
    (void)harness.frame();
    const auto clicked = click_at(harness, 640, 88);
    CHECK(clicked.front().get().kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK);
    CHECK(clicked.front().get().transport_seek_frame > 2'000'000);
    CHECK(clicked.front().get().transport_seek_frame < 2'400'000);
    const std::array down{motion_event(320, 88), button_event(true, 320, 88)};
    (void)harness.frame(down);
    const std::array moved{motion_event(960, 88)};
    const auto dragged = harness.frame(moved);
    CHECK(dragged.get().kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK);
    CHECK(dragged.get().transport_seek_frame > 3'000'000);
}

namespace {
void check_note_click_emitted_once(bool selected) {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    const std::array motion{motion_event(300, 400)};
    const auto hovered = harness.frame(motion);
    REQUIRE(hovered.get().compose_has_pointer_hover);
    const auto x = hovered.get().compose_pointer_hover_x_pixels;
    const auto y = hovered.get().compose_pointer_hover_y_pixels;
    WormUiComposeNoteView note{};
    note.bounds = {x - 15, y - 5, x + 15, y + 5};
    note.note_id = 1;
    note.lane_id = 1;
    note.sound_label = "01";
    note.sound_detail = "keysound.ogg";
    note.selected = selected;
    harness.view().compose.note_endpoints = &note;
    harness.view().compose.note_endpoint_count = 1;
    (void)harness.frame(motion);
    const auto clicked = click_at(harness, 300, 400);
    size_t count = 0;
    for (const auto& frame : clicked)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index)
            count += frame.get().compose_inputs[index].kind == WORM_UI_COMPOSE_INPUT_POINTER_CLICK;
    CHECK(count == 1);
}
} // namespace
TEST_CASE("Clicking selected and unselected notes emits one audition selection",
          "[unit][frontend-state][event-harness][audition][regression]") {
    check_note_click_emitted_once(false);
    check_note_click_emitted_once(true);
}

namespace {
void check_bga_dock(bool value) { CHECK(value); }
void check_draw_source(bool value) { CHECK(value); }
void check_sound_slot_hidden(const std::string& drawn_text) {
    CHECK(drawn_text.find("0A  kick.wav") == std::string::npos);
}
} // namespace

TEST_CASE("Selecting a sound keeps note painting armed with that sound and preserves advance",
          "[frontend-state][event-harness][sound-paint]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 2> rows{{
        {.resource_id = 1400, .slot = "01", .name = "kick.wav"},
        {.resource_id = 1500, .slot = "02", .name = "kick2.wav"}}};
    view.keysounds = rows.data(); view.keysound_count = rows.size();
    view.keysounds_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sounds_panel_visible = true;
    state.auto_increment_on_place = true;
    view.note_paint_mode_active = true;
    (void)harness.frame();
    const auto kick_row = harness.drawn_text_center("kick.wav");
    REQUIRE(kick_row);
    const auto actions = click_at(harness, kick_row->first, kick_row->second, true);
    CHECK_FALSE(std::ranges::any_of(actions, [](const auto& action) {
        return action.get().compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    }));
    CHECK(state.armed_resource_id == 1400);
    check_draw_source(state.draw_source_kind == 1);
    check_draw_source(state.draw_source_id == 1400);
    CHECK(state.armed_from_sounds_dock);
    CHECK(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    worm_ui_nuklear_advance_resource_picker(1400, 0);
    CHECK(state.armed_resource_id == 1500);
    CHECK(state.keysound_selection.primary == 1500);
}

TEST_CASE("BGA dock reuses list selection and arms resources outside the drawer window", "[frontend-state][event-harness][bga]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<std::uint64_t, 2> ids{1400, 1500};
    const std::array<WormUiComposeBgaRowView, 2> rows{{
        {.resource_id = 1400, .slot = "01", .name = "clip.png", .use_count = 2},
        {.resource_id = 1500, .slot = "02", .name = "missing.png", .missing = true}}};
    view.bga_rows = rows.data(); view.bga_row_count = rows.size();
    view.bga_total_matching = ids.size(); view.bga_matching_ids = ids.data(); view.bga_search = "";
    view.bga_advance_ids = ids.data(); view.bga_advance_count = 1;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.bga_panel_visible = true;
    (void)harness.frame();
    REQUIRE(state.bga_list_valid);
    check_bga_dock(harness.drawn_text().find("clip.png") != std::string::npos);
    check_bga_dock(harness.drawn_text().find("01  clip.png") == std::string::npos);
    INFO("BGA bounds " << state.bga_list_x << "," << state.bga_list_y << "," << state.bga_list_width << "," << state.bga_list_height);
    const auto clip = harness.drawn_text_center("clip.png");
    REQUIRE(clip);
    (void)click_at(harness, clip->first, clip->second, true);
    check_bga_dock(state.bga_selection.primary == 1400);
    check_bga_dock(state.armed_resource_id == 1400);
    check_bga_dock(state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga);
    check_bga_dock(state.draw_source_kind == 2);
    check_bga_dock(state.draw_source_id == 1400);
    check_bga_dock(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    view.bga_rows = rows.data() + 1; view.bga_row_count = 1; view.bga_first_row = 1;
    (void)harness.frame();
    check_bga_dock(state.draw_source_id == 1400);
    check_bga_dock(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    state.bga_panel_visible = false;
    (void)harness.frame();
    CHECK_FALSE(state.bga_list_valid);
}

TEST_CASE("Measures dock selects rows, jumps the cursor and applies one meter to the selection",
          "[frontend-state][event-harness][measures]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<std::uint64_t, 3> ids{11, 12, 13};
    const std::array<WormUiComposeMeasureRowView, 3> rows{{
        {.measure_id = 11, .number = 1, .length_numerator = 1, .length_denominator = 1,
         .has_meter = true, .meter_numerator = 4, .meter_denominator = 4},
        {.measure_id = 12, .number = 2, .length_numerator = 3, .length_denominator = 4,
         .has_meter = true, .meter_numerator = 3, .meter_denominator = 4},
        {.measure_id = 13, .number = 3, .length_numerator = 1, .length_denominator = 1,
         .has_meter = true, .meter_numerator = 4, .meter_denominator = 4}}};
    view.measure_rows = rows.data();
    view.measure_row_ids = ids.data();
    view.measure_row_count = rows.size();
    auto& state = worm_ui_nuklear_compose_state();
    state.measures_panel_visible = true;
    state.measure_selection = {};
    (void)harness.frame();
    const auto second = harness.drawn_text_center("2    3/4    3/4");
    REQUIRE(second);
    const auto jump = click_at(harness, second->first, second->second);
    CHECK(state.measure_selection.primary == 12);
    CHECK(std::ranges::any_of(jump, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE &&
               frame.get().compose_measure_number == 2;
    }));
    (void)harness.frame();
    const auto all = harness.drawn_text_center("Select all");
    REQUIRE(all);
    (void)click_at(harness, all->first, all->second);
    CHECK(state.measure_selection.selected.size() == 3);
    std::snprintf(state.measures_meter_numerator.data(), state.measures_meter_numerator.size(), "6");
    std::snprintf(state.measures_meter_denominator.data(), state.measures_meter_denominator.size(), "8");
    (void)harness.frame();
    const auto apply = harness.drawn_text_center("Apply");
    REQUIRE(apply);
    const auto applied = click_at(harness, apply->first, apply->second);
    const auto found = std::ranges::find_if(applied, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER;
    });
    REQUIRE(found != applied.end());
    CHECK(found->get().compose_measure_id_count == 3);
    CHECK(std::string(found->get().timing_meter_numerator_text) == "6");
    CHECK(std::string(found->get().timing_meter_denominator_text) == "8");
}

namespace {
void require_sound_renumber(bool value) { REQUIRE(value); }
void check_sound_renumber(bool value, std::string_view description) {
    INFO(description);
    CHECK(value);
}
auto has_sound_renumber_action(const std::vector<workspace_action_snapshot_t>& frames) -> bool {
    return std::ranges::any_of(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS;
    });
}
auto has_sound_audition_action(const std::vector<workspace_action_snapshot_t>& frames) -> bool {
    return std::ranges::any_of(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_AUDITION_SOUND;
    });
}
} // namespace

TEST_CASE("Sounds dock confirms renumbering the selected BMS resources", "[frontend-state][event-harness][sounds][bms]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 2> rows{{
        {.resource_id = 40, .slot = "0A", .name = "kick2.wav"},
        {.resource_id = 50, .slot = "0B", .name = "kick10.wav"}}};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = view.keysound_ids_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.keysound_search = {'k', 'i', 'c', 'k'};
    (void)harness.frame();
    const auto rendered = harness.drawn_text();
    check_sound_renumber(rendered.find("kick2.wav") < rendered.find("kick10.wav"),
                         "filtered natural name order");
    state.keysound_selection.selected = {50};
    state.keysound_selection.primary = 50;
    state.keysound_renumber_confirmation_open = true;
    state.keysound_renumber_project = view.project_id;
    state.keysound_renumber_chart = view.chart_id;
    state.keysound_renumber_revision = view.document_revision;
    state.keysound_renumber_resources = {50};
    (void)harness.frame();
    const auto choice = harness.drawn_text_center("Selected");
    REQUIRE(choice.has_value());
    const auto frames = click_at(harness, choice->first, choice->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS;
    });
    require_sound_renumber(found != frames.end());
    check_sound_renumber(found->get().compose_sound_resource_count == 1, "one selected resource");
    require_sound_renumber(found->get().compose_sound_resource_ids != nullptr);
    check_sound_renumber(found->get().compose_sound_resource_ids[0] == 50, "selected resource ID");
    check_sound_renumber(!found->get().compose_renumber_all, "selection mode");
    check_sound_renumber(!found->get().compose_renumber_bga, "WAV namespace");
    check_sound_renumber(!state.keysound_renumber_confirmation_open, "confirmation closes");
}

TEST_CASE("Sounds dock cancels stale or dismissed BMS renumber confirmations", "[frontend-state][event-harness][sounds][bms]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 1> rows{{
        {.resource_id = 50, .slot = "0B", .name = "kick10.wav"}}};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = view.keysound_ids_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.keysound_renumber_confirmation_open = true;
    state.keysound_renumber_project = view.project_id;
    state.keysound_renumber_chart = view.chart_id;
    state.keysound_renumber_revision = view.document_revision;
    state.keysound_renumber_resources = {50};
    ++view.document_revision;
    (void)harness.frame();
    check_sound_renumber(!state.keysound_renumber_confirmation_open, "stale revision closes popup");
    auto frames = click_at(harness, 760, 298);
    check_sound_renumber(!has_sound_renumber_action(frames), "stale revision sends no action");

    state.keysound_renumber_confirmation_open = true;
    state.keysound_renumber_project = view.project_id;
    state.keysound_renumber_chart = view.chart_id;
    state.keysound_renumber_revision = view.document_revision;
    state.keysound_renumber_resources = {50};
    ++view.chart_id;
    (void)harness.frame();
    check_sound_renumber(!state.keysound_renumber_confirmation_open, "stale chart closes popup");

    state.keysound_renumber_confirmation_open = true;
    state.keysound_renumber_project = view.project_id;
    state.keysound_renumber_chart = view.chart_id;
    state.keysound_renumber_revision = view.document_revision;
    state.keysound_renumber_resources = {50};
    ++view.project_id;
    (void)harness.frame();
    check_sound_renumber(!state.keysound_renumber_confirmation_open, "stale project closes popup");

    state.keysound_renumber_confirmation_open = true;
    state.keysound_renumber_project = view.project_id;
    state.keysound_renumber_chart = view.chart_id;
    state.keysound_renumber_revision = view.document_revision;
    state.keysound_renumber_resources = {50};
    (void)harness.frame();
    const auto cancel = harness.drawn_text_center("Cancel");
    REQUIRE(cancel.has_value());
    frames = click_at(harness, cancel->first, cancel->second);
    check_sound_renumber(!has_sound_renumber_action(frames), "cancel sends no action");
    check_sound_renumber(!state.keysound_renumber_confirmation_open, "cancel closes popup");
}

TEST_CASE("Sounds dock renumber release does not audition the covered row",
          "[frontend-state][event-harness][sounds][bms]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 1> rows{{
        {.resource_id = 50, .slot = "0B", .name = "kick.wav"}}};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = view.keysound_ids_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.keysound_renumber_confirmation_open = true;
    state.keysound_renumber_project = view.project_id;
    state.keysound_renumber_chart = view.chart_id;
    state.keysound_renumber_revision = view.document_revision;
    state.keysound_renumber_resources.clear();
    (void)harness.frame();
    const auto choice = harness.drawn_text_center("All sounds");
    REQUIRE(choice.has_value());
    const auto frames = click_at(harness, choice->first, choice->second);
    check_sound_renumber(has_sound_renumber_action(frames), "all sounds action dispatched");
    check_sound_renumber(!has_sound_audition_action(frames), "press and release do not audition row");
    const auto renumber = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS;
    });
    require_sound_renumber(renumber != frames.end());
    check_sound_renumber(renumber->get().compose_renumber_all, "all sounds mode");
}

TEST_CASE("BGA renumber release does not select the covered image",
          "[frontend-state][event-harness][bga][bms]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<std::uint64_t, 1> ids{80};
    const std::array<WormUiComposeBgaRowView, 1> rows{{
        {.resource_id = 80, .slot = "0A", .name = "cover.bmp"}}};
    view.bga_rows = rows.data();
    view.bga_row_count = 1;
    view.bga_total_matching = 1;
    view.bga_matching_ids = ids.data();
    view.bga_search = "";
    view.bga_ids_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.bga_panel_visible = true;
    (void)harness.frame();
    state.bga_renumber_confirmation_open = true;
    state.bga_renumber_project = view.project_id;
    state.bga_renumber_chart = view.chart_id;
    state.bga_renumber_revision = view.document_revision;
    state.bga_renumber_resources.clear();
    (void)harness.frame();
    const auto all_bga = harness.drawn_text_center("All BGA");
    REQUIRE(all_bga.has_value());
    const auto frames = click_at(harness, all_bga->first, all_bga->second);
    check_sound_renumber(has_sound_renumber_action(frames), "all BGA action dispatched");
    const auto renumber = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS;
    });
    require_sound_renumber(renumber != frames.end());
    check_sound_renumber(renumber->get().compose_renumber_all, "all images mode");
    check_sound_renumber(renumber->get().compose_renumber_bga, "BMP namespace");
    check_sound_renumber(state.bga_selection.primary == 0, "covered image stays unselected");
    check_sound_renumber(state.bga_selection.selected.empty(), "covered image absent from selection");
}

namespace {
void check_modal_blocks_chart_inputs(const std::vector<workspace_action_snapshot_t>& frames) {
    for (const auto& frame : frames) {
        for (std::size_t index = 0; index < frame.get().compose_input_count; ++index) {
            const auto kind = frame.get().compose_inputs[index].kind;
            INFO("compose input kind enum value: " << static_cast<int>(kind));
            CHECK((kind == WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE ||
                   kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL));
        }
    }
    // A modal may cancel an existing pointer gesture, but no chart action such
    // as note selection, placement, or movement can escape to the canvas.
}
} // namespace

TEST_CASE("BGA renumber popup blocks Compose note input outside its bounds",
          "[frontend-state][event-harness][bga][modal]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& workspace = harness.view();
    auto& view = workspace.compose;
    const std::array hover_motion{motion_event(300, 400)};
    const auto hover = harness.frame(hover_motion);
    REQUIRE(hover.get().compose_has_pointer_hover);
    WormUiComposeNoteView note{};
    note.bounds = {hover.get().compose_pointer_hover_x_pixels - 15,
                   hover.get().compose_pointer_hover_y_pixels - 5,
                   hover.get().compose_pointer_hover_x_pixels + 15,
                   hover.get().compose_pointer_hover_y_pixels + 5};
    note.note_id = 5;
    note.lane_id = 1;
    note.sound_label = "01";
    note.sound_detail = "covered.ogg";
    view.note_endpoints = &note;
    view.note_endpoint_count = 1;
    const std::array<std::uint64_t, 1> ids{80};
    const std::array<WormUiComposeBgaRowView, 1> rows{{
        {.resource_id = 80, .slot = "0A", .name = "cover.bmp"}}};
    view.bga_rows = rows.data();
    view.bga_row_count = 1;
    view.bga_total_matching = 1;
    view.bga_matching_ids = ids.data();
    view.bga_search = "";
    view.bga_ids_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.bga_panel_visible = true;
    (void)harness.frame();
    state.bga_renumber_confirmation_open = true;
    state.bga_renumber_project = view.project_id;
    state.bga_renumber_chart = view.chart_id;
    state.bga_renumber_revision = view.document_revision;
    state.bga_renumber_resources.clear();
    (void)harness.frame();

    const auto outside_click = click_at(harness, 300, 400);
    check_modal_blocks_chart_inputs(outside_click);
}

namespace {
auto find_compose_input(const std::vector<workspace_action_snapshot_t>& frames,
                        WormUiComposeInputKind kind) -> const WormUiComposeInputAction* {
    for (const auto& frame : frames)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index)
            if (frame.get().compose_inputs[index].kind == kind)
                return &frame.get().compose_inputs[index];
    return nullptr;
}
} // namespace

TEST_CASE("Note paint carries the Sounds dock sound whichever was picked first",
          "[unit][frontend-state][event-harness][paint][sound]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 2> rows{{
        {.resource_id = 40, .slot = "0A", .name = "kick.wav"},
        {.resource_id = 50, .slot = "0B", .name = "gone.wav", .missing = true}}};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = true;
    (void)harness.frame();
    check_sound_slot_hidden(harness.drawn_text());
    auto& state = worm_ui_nuklear_compose_state();

    // Tool first, then sound: the sound pick leaves note paint and the place mode alone.
    view.note_paint_mode_active = true;
    state.armed_resource_id = 40;
    state.draw_source_kind = 1;
    state.draw_source_id = 40;
    state.armed_from_sounds_dock = true;
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    (void)harness.frame();
    auto frames = click_at(harness, 300, 400);
    const auto* begin = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin != nullptr);
    CHECK(begin->resource_id == 40);
    CHECK(find_compose_input(frames, WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE) == nullptr);
    CHECK(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);

    // Sound first, then tool: the same armed sound rides the same stroke.
    view.note_paint_mode_active = false;
    (void)harness.frame();
    view.note_paint_mode_active = true;
    (void)harness.frame();
    frames = click_at(harness, 300, 400);
    begin = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin != nullptr);
    CHECK(begin->resource_id == 40);

    // Hold armed: the press is a click for the click-click hold gesture, never a paint stroke,
    // and the click carries the armed sound.
    view.armed_note_kind = 1;
    (void)harness.frame();
    frames = click_at(harness, 300, 400);
    CHECK(find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == nullptr);
    const auto* hold_click = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_POINTER_CLICK);
    REQUIRE(hold_click != nullptr);
    CHECK(hold_click->resource_id == 40);
    view.armed_note_kind = 0;
    (void)harness.frame();

    // Legacy arming does not change the explicit Draw source.
    state.armed_resource_id = 50;
    frames = click_at(harness, 300, 400);
    begin = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin != nullptr);
    CHECK(begin->resource_id == 40);
    state.armed_resource_id = 40;
    state.armed_from_sounds_dock = false;
    frames = click_at(harness, 300, 400);
    begin = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin != nullptr);
    CHECK(begin->resource_id == 40);
    state = {};
}

TEST_CASE("Compare fields route wheel input as one shared read-only scroll action",
          "[unit][frontend-state][event-harness][compare]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE;
    workspace.setup.visible = false;
    workspace.metadata.visible = false;
    workspace.compose.visible = false;
    workspace.audio_slices.visible = false;
    auto& compare = workspace.compare;
    WormUiComposeView first{}, second{};
    first.viewport_width_pixels = second.viewport_width_pixels = 640;
    first.viewport_height_pixels = second.viewport_height_pixels = 600;
    first.status_text = "First field";
    second.status_text = "Second field";
    compare.visible = true;
    compare.has_two_difficulties = true;
    compare.first_pane = &first;
    compare.second_pane = &second;
    const std::array<std::uint64_t, 2> ids{10, 20};
    const std::array<const char*, 2> names{"Easy", "Hard"};
    compare.chart_ids = ids.data();
    compare.chart_names = names.data();
    compare.chart_count = ids.size();
    compare.first_chart_id = ids[0];
    compare.second_chart_id = ids[1];
    compare.first_difficulty = names[0];
    compare.second_difficulty = names[1];
    (void)harness.frame();

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -1.0f;
    wheel.wheel.mouse_x = 320.0f;
    wheel.wheel.mouse_y = 420.0f;
    const std::array<SDL_Event, 2> wheel_script{motion_event(320.0f, 420.0f), wheel};
    const auto action = harness.frame(wheel_script);
    CHECK(action.get().kind == WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL);
    CHECK(action.get().compare_scroll_delta != 0.0);
    CHECK(action.get().compose_input_count == 0);
}

namespace {
struct compare_diagnostic_fixture_t {
    std::array<const char*, 2> labels{"[3] Missing note resource", "[7] Chart warning"};
    std::array<std::uint64_t, 2> rows{17, 0};
    std::array<std::uint64_t, 2> measures{5, 0};
    std::array<std::uint8_t, 2> has_rows{1, 0};
};

void configure_compare_diagnostic_view(nuklear_event_harness_t& harness,
                                       const compare_diagnostic_fixture_t& fixture) {
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE;
    workspace.setup.visible = false;
    workspace.metadata.visible = false;
    workspace.compose.visible = false;
    workspace.audio_slices.visible = false;
    auto& compare = workspace.compare;
    compare.visible = true;
    compare.diagnostic_count = fixture.labels.size();
    compare.diagnostic_labels = fixture.labels.data();
    compare.diagnostic_rows = fixture.rows.data();
    compare.diagnostic_measures = fixture.measures.data();
    compare.diagnostic_has_rows = fixture.has_rows.data();
    compare.diagnostic_chart_id = 42;
}

auto click_compare_diagnostic(nuklear_event_harness_t& harness)
    -> std::pair<float, std::vector<workspace_action_snapshot_t>> {
    for (float y = 80; y <= 260; y += 2) {
        auto candidate = click_at(harness, 640, y);
        if (!candidate.empty() && candidate[0].get().kind ==
                                      WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP)
            return {y, std::move(candidate)};
    }
    return {-1.0f, {}};
}

void check_clickable_compare_diagnostic(const std::vector<workspace_action_snapshot_t>& clickable) {
    REQUIRE(clickable.size() == 2);
    CHECK(clickable[0].get().compare_diagnostic_row == 17);
    CHECK(clickable[0].get().compare_diagnostic_measure == 5);
    CHECK(clickable[0].get().compare_diagnostic_chart == 42);
}

void check_rowless_compare_diagnostic(nuklear_event_harness_t& harness, float y) {
    const auto rowless = click_at(harness, 640, y + 22.0f);
    REQUIRE(rowless.size() == 2);
    CHECK(rowless[0].get().kind != WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP);
}

void check_compare_diagnostic_actions(nuklear_event_harness_t& harness) {
    const auto [y, clickable] = click_compare_diagnostic(harness);
    REQUIRE(y >= 0.0f);
    check_clickable_compare_diagnostic(clickable);
    check_rowless_compare_diagnostic(harness, y);
}
} // namespace

TEST_CASE("Compare validation diagnostics jump only when a row is available",
          "[unit][frontend-state][event-harness][compare]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    const compare_diagnostic_fixture_t fixture;
    configure_compare_diagnostic_view(harness, fixture);
    (void)harness.frame();
    check_compare_diagnostic_actions(harness);
}

namespace {
struct sound_slots_fixture_t {
    std::array<WormUiComposeKeysoundRowView, 2> sounds{{
        {.resource_id = 501, .slot = "02", .name = "kick.wav"},
        {.resource_id = 502, .slot = "03", .name = "missing.wav", .missing = true}}};
    std::array<WormUiComposeSoundSlotRowView, 3> slots{{
        {.number = 1, .slot = "01", .resource_id = 0, .name = "(empty)"},
        {.number = 2, .slot = "02", .resource_id = 501, .name = "kick.wav",
         .use_count = 3, .placeable = true},
        {.number = 3, .slot = "03", .resource_id = 502, .name = "missing.wav",
         .use_count = 1, .missing = true, .placeable = true}}};
};
void require_sound_slots(bool value) { REQUIRE(value); }
void check_sound_slots(bool value) { CHECK(value); }
void configure_sound_slots(nuklear_event_harness_t& harness, const sound_slots_fixture_t& fixture,
                            bool enabled = true) {
    auto& view = harness.view().compose;
    view.keysounds = fixture.sounds.data();
    view.keysound_count = view.keysound_total_count = fixture.sounds.size();
    view.keysounds_available = view.keysound_ids_available = true;
    view.keysound_ids_total = 1295;
    view.sound_slots = fixture.slots.data();
    view.sound_slot_count = fixture.slots.size();
    view.sound_slot_total_matching = 1295;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sounds_panel_visible = true;
    state.keysound_slots_mode = enabled;
    view.sound_slots_mode = enabled;
    (void)harness.frame();
}
void stage_sound_slot(nuklear_event_harness_t& harness, std::uint16_t slot = 2) {
    auto& state = worm_ui_nuklear_compose_state();
    auto& view = harness.view().compose;
    state.keysound_selected_slot = slot;
    (void)harness.frame();
    require_sound_slots(worm_ui_frontend_stage_sound_slot_file(
        view.project_id, view.chart_id, view.document_revision, "/tmp/slot-picked.wav", 1));
}
auto find_sound_slot_action(const std::vector<workspace_action_snapshot_t>& frames)
    -> const WormUiWorkspaceAction* {
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT;
    });
    return found == frames.end() ? nullptr : &found->get();
}
} // namespace

TEST_CASE("BMS dock defaults to automatic and switches to its bounded slots view",
          "[frontend-state][event-harness][sound-slots]") {
    nuklear_event_harness_t harness{1280, 900};
    require_sound_slots(harness.ready());
    const sound_slots_fixture_t fixture;
    configure_sound_slots(harness, fixture, false);
    auto& state = worm_ui_nuklear_compose_state();
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_sound_slots(!request.slots_mode);
    (void)click_at(harness, state.sound_list_x + 210, state.sound_list_y + 36);
    worm_ui_frontend_keysound_dock_request(&request);
    check_sound_slots(request.slots_mode);
    harness.view().compose.sound_slots_mode = true;
    (void)harness.frame();
    check_sound_slots(harness.drawn_text().find("(empty)") != std::string::npos);
    check_sound_slots(request.row_limit <= 128);
    (void)click_at(harness, state.sound_list_x + 60, state.sound_list_y + 36);
    worm_ui_frontend_keysound_dock_request(&request);
    check_sound_slots(!request.slots_mode);
}

TEST_CASE("Empty slot file assignment sends its exact ID once without automatic allocation",
          "[frontend-state][event-harness][sound-slots]") {
    nuklear_event_harness_t harness{1280, 900};
    const sound_slots_fixture_t fixture;
    configure_sound_slots(harness, fixture);
    stage_sound_slot(harness, 1);
    const auto snapshot = harness.frame();
    const auto& action = snapshot.get();
    check_sound_slots(action.compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT);
    check_sound_slots(action.compose_sound_slot == 1);
    check_sound_slots(!action.compose_sound_slot_confirmed);
    require_sound_slots(action.compose_import_path_count == 1 && action.compose_import_paths);
    check_sound_slots(std::string_view{action.compose_import_paths[0]} == "/tmp/slot-picked.wav");
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(std::string_view{snapshot.get().compose_import_paths[0]} == "/tmp/slot-picked.wav");
    const auto& view = harness.view().compose;
    check_sound_slots(worm_ui_frontend_stage_sound_slot_file(
        view.project_id, view.chart_id, view.document_revision, "/tmp/one.wav", 2));
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(!worm_ui_nuklear_compose_state().keysound_slot_feedback.empty());
}

TEST_CASE("Occupied slot selection arms its sound and replacement requires explicit confirmation",
          "[frontend-state][event-harness][sound-slots]") {
    nuklear_event_harness_t harness{1280, 900};
    const sound_slots_fixture_t fixture;
    configure_sound_slots(harness, fixture);
    auto& state = worm_ui_nuklear_compose_state();
    const auto occupied_row = harness.drawn_text_center("02  kick.wav");
    REQUIRE(occupied_row);
    (void)click_at(harness, occupied_row->first, occupied_row->second);
    check_sound_slots(state.keysound_selected_slot == 2);
    check_sound_slots(state.armed_resource_id == 501);
    check_sound_slots(state.keysound_selection.primary == 501);
    stage_sound_slot(harness);
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(state.keysound_slot_confirmation_open);
    check_sound_slots(harness.drawn_text().find("Replace occupied WAV slot 02") != std::string::npos);
    const auto choice = harness.drawn_text_center("Replace");
    REQUIRE(choice.has_value());
    const auto frames = click_at(harness, choice->first, choice->second);
    const auto* action = find_sound_slot_action(frames);
    require_sound_slots(action != nullptr);
    check_sound_slots(action->compose_sound_slot == 2);
    check_sound_slots(action->compose_sound_slot_confirmed);
    check_sound_slots(action->compose_import_path_count == 1);
    check_sound_slots(!state.keysound_slot_confirmation_open);
    check_sound_slots(!has_sound_audition_action(frames));
}

TEST_CASE("Slot replacement Escape cancels and does not play or assign audio",
          "[frontend-state][event-harness][sound-slots]") {
    nuklear_event_harness_t harness{1280, 900};
    const sound_slots_fixture_t fixture;
    configure_sound_slots(harness, fixture);
    stage_sound_slot(harness);
    (void)harness.frame();
    const auto space = key_event(SDL_EVENT_KEY_DOWN, SDLK_SPACE, SDL_SCANCODE_SPACE);
    const auto blocked = harness.frame(std::span{&space, 1});
    check_sound_slots(blocked.get().kind != WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE);
    check_sound_slots(blocked.get().compose_input_count == 0);
    check_sound_slots(blocked.get().shortcut_event_count == 0);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_SPACE, SDL_SCANCODE_SPACE);
    (void)harness.frame(std::span{&release, 1});
    const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto action = harness.frame(std::span{&escape, 1});
    check_sound_slots(!worm_ui_nuklear_compose_state().keysound_slot_confirmation_open);
    check_sound_slots(action.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(action.get().kind != WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE);
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Slot replacement rejects stale revisions and a mode change without fallback",
          "[frontend-state][event-harness][sound-slots]") {
    nuklear_event_harness_t harness{1280, 900};
    const sound_slots_fixture_t fixture;
    configure_sound_slots(harness, fixture);
    stage_sound_slot(harness);
    (void)harness.frame();
    ++harness.view().compose.document_revision;
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(!worm_ui_nuklear_compose_state().keysound_slot_confirmation_open);
    stage_sound_slot(harness, 1);
    worm_ui_nuklear_compose_state().keysound_slots_mode = false;
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(!worm_ui_nuklear_compose_state().keysound_pending_file);
}

TEST_CASE("Missing and empty slots disarm painting and non-BMS charts retain automatic mode",
          "[frontend-state][event-harness][sound-slots]") {
    nuklear_event_harness_t harness{1280, 900};
    const sound_slots_fixture_t fixture;
    configure_sound_slots(harness, fixture);
    auto& state = worm_ui_nuklear_compose_state();
    state.armed_resource_id = 501;
    const auto missing_row = harness.drawn_text_center("03  missing.wav · missing");
    REQUIRE(missing_row);
    (void)click_at(harness, missing_row->first, missing_row->second);
    check_sound_slots(state.keysound_selected_slot == 3);
    check_sound_slots(state.armed_resource_id == 0);
    state.armed_resource_id = 501;
    const auto empty_row = harness.drawn_text_center("01  (empty)");
    REQUIRE(empty_row);
    (void)click_at(harness, empty_row->first, empty_row->second);
    check_sound_slots(state.keysound_selected_slot == 1);
    check_sound_slots(state.armed_resource_id == 0);
    harness.view().compose.keysound_ids_available = false;
    harness.view().compose.sound_slots_mode = false;
    (void)harness.frame();
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_sound_slots(!request.slots_mode);
    check_sound_slots(harness.drawn_text().find("BMS slots") == std::string::npos);
    const auto& view = harness.view().compose;
    check_sound_slots(!worm_ui_frontend_stage_sound_slot_file(
        view.project_id, view.chart_id, view.document_revision, "/tmp/auto.wav", 1));
}

namespace {
void expect_filtered_sound_rows(const nuklear_event_harness_t& harness) {
    CHECK(harness.drawn_text_center("Kick  ·  Keysound").has_value());
    CHECK_FALSE(harness.drawn_text_center("Snare  ·  Keysound").has_value());
    CHECK(harness.drawn_text().find("Snare") != std::string::npos);
}

void expect_sound_sections(const nuklear_event_harness_t& harness) {
    CHECK(harness.drawn_text().find("Sounds") != std::string::npos);
    CHECK(harness.drawn_text().find("Waveform") != std::string::npos);
    CHECK(harness.drawn_text().find("Properties") != std::string::npos);
    CHECK(harness.drawn_text().find("Kick") != std::string::npos);
    CHECK(harness.drawn_text().find("Snare") != std::string::npos);
}

void select_and_filter_sound(nuklear_event_harness_t& harness) {
    const auto snare = harness.drawn_text_center("Snare  ·  Keysound");
    REQUIRE(snare);
    const auto selected = click_at(harness, snare->first, snare->second);
    const auto action = std::ranges::find_if(selected, [](const auto& frame) {
        return frame.get().kind == WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET;
    });
    REQUIRE(action != selected.end());
    CHECK(action->get().audio_asset_id == 22);
    CHECK(action->get().audio_resource_id == 102);
    auto& audio_view = harness.view().audio_slices;
    audio_view.selected_resource_id = action->get().audio_resource_id;
    audio_view.has_selected_asset = true;
    audio_view.selected_asset_id = action->get().audio_asset_id;
    std::snprintf(worm_ui_nuklear_audio_state().search_text, 128, "kick");
    CHECK(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    expect_filtered_sound_rows(harness);
    CHECK(worm_ui_nuklear_audio_state().selected_resource_id == 102);
}

void check_separated_sound_columns(int width) {
    nuklear_event_harness_t harness{width, 720};
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
    workspace.compose.visible = false;
    auto& view = workspace.audio_slices;
    view.visible = true;
    const std::array<WormUiSoundRowView, 2> rows{{
        {.resource_id = 101, .asset_id = 11, .name = "Kick", .kind = 2},
        {.resource_id = 102, .asset_id = 22, .name = "Snare", .kind = 2}}};
    view.sound_rows = rows.data();
    view.sound_row_count = rows.size();
    CHECK(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    expect_sound_sections(harness);
    select_and_filter_sound(harness);
}
} // namespace

TEST_CASE("Sounds section separators preserve row selection and search at normal width",
          "[frontend-state][event-harness][sounds-separators]") {
    check_separated_sound_columns(1280);
}

TEST_CASE("Sounds section separators preserve row selection and search at narrow width",
          "[frontend-state][event-harness][sounds-separators]") {
    check_separated_sound_columns(800);
}

namespace {
void stage_grouped_import(nuklear_event_harness_t& harness) {
    const std::array<WormUiSoundImportItem, 3> items{{
        {"/tmp/kick01.wav", "kick01.wav", WORM_UI_SOUND_IMPORT_KEYSOUND},
        {"/tmp/kick02.wav", "kick02.wav", WORM_UI_SOUND_IMPORT_KEYSOUND},
        {"/tmp/loose.wav", "loose.wav", WORM_UI_SOUND_IMPORT_SAMPLE}}};
    const auto& view = harness.view();
    REQUIRE(worm_ui_frontend_stage_sound_import(view.project_id, view.selected_chart_id,
        view.document_revision, items.data(), items.size(), nullptr, 0, true, ""));
    (void)harness.frame();  // A dialog fits its content on its second frame.
}

void check_retained_import_after_failure() {
    const auto& state = worm_ui_nuklear_pending_sound_import();
    CHECK(state.items.size() == 3);
    CHECK(state.groups.size() == 1);
    CHECK(state.message == "No contiguous WAV run is available.");
    CHECK(state.phase == worm_ui_sound_import_phase_t::review_groups);
}
} // namespace

TEST_CASE("Sound group import review captures shortcuts and Escape cancels without an action",
          "[frontend-state][event-harness][sound-import]") {
    nuklear_event_harness_t harness;
    (void)harness.frame();
    stage_grouped_import(harness);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Groups") != std::string::npos);
    CHECK(harness.drawn_text().find("kick") != std::string::npos);
    const auto save = key_event(SDL_EVENT_KEY_DOWN, SDLK_S, SDL_SCANCODE_S);
    CHECK(harness.frame(std::span{&save, 1}, SDL_KMOD_CTRL).get().shortcut_event_count == 0);
    const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    CHECK(harness.frame(std::span{&escape, 1}).get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK_FALSE(worm_ui_nuklear_sound_import_active());
}

TEST_CASE("Sound group name field replaces selected text from captured keyboard and text input",
          "[frontend-state][event-harness][sound-import]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    (void)harness.frame();
    stage_grouped_import(harness);
    (void)harness.frame();
    REQUIRE(worm_ui_nuklear_pending_sound_import().groups.size() == 1);
    CHECK(worm_ui_nuklear_pending_sound_import().groups.front().name == "kick");

    // The group-name field shows the group name; click into it.
    const auto name_field = harness.drawn_text_center("kick");
    REQUIRE(name_field);
    (void)click_at(harness, name_field->first, name_field->second);
    const auto select_all = key_event(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_SCANCODE_A);
    (void)harness.frame(std::span{&select_all, 1}, SDL_KMOD_CTRL);
    const auto release_a = key_event(SDL_EVENT_KEY_UP, SDLK_A, SDL_SCANCODE_A);
    (void)harness.frame(std::span{&release_a, 1}, SDL_KMOD_CTRL);
    SDL_Event replacement{};
    replacement.type = SDL_EVENT_TEXT_INPUT;
    replacement.text.type = SDL_EVENT_TEXT_INPUT;
    for (const char character : std::string_view{"Drums"}) {
        const char codepoint[]{character, '\0'};
        replacement.text.text = codepoint;
        (void)harness.frame(std::span{&replacement, 1});
    }

    CHECK(worm_ui_nuklear_pending_sound_import().groups.front().name == "Drums");
}

TEST_CASE("Sound import failure retains group edits and changing projects discards the review",
          "[frontend-state][event-harness][sound-import]") {
    nuklear_event_harness_t harness;
    (void)harness.frame();
    stage_grouped_import(harness);
    worm_ui_frontend_sound_import_result(false, "No contiguous WAV run is available.");
    check_retained_import_after_failure();
    ++harness.view().project_id;
    CHECK(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK_FALSE(worm_ui_nuklear_sound_import_active());
}

TEST_CASE("Import action snapshots own item strings and group member indexes",
          "[frontend-state][event-harness][sound-import]") {
    std::string path = "/tmp/kick01.wav", name = "Kick", group_name = "Drums";
    WormUiSoundImportItem item{path.c_str(), name.c_str(), WORM_UI_SOUND_IMPORT_KEYSOUND};
    std::array<std::size_t, 1> members{0};
    WormUiSoundImportGroup group{group_name.c_str(), 22, members.data(), members.size()};
    WormUiWorkspaceAction action{};
    action.sound_import_items = &item; action.sound_import_item_count = 1;
    action.sound_import_groups = &group; action.sound_import_group_count = 1;
    workspace_action_snapshot_t snapshot{action};
    path = name = group_name = "overwritten";
    members[0] = 99;
    CHECK(std::string{snapshot.get().sound_import_items[0].path} == "/tmp/kick01.wav");
    CHECK(std::string{snapshot.get().sound_import_items[0].name} == "Kick");
    CHECK(std::string{snapshot.get().sound_import_groups[0].name} == "Drums");
    CHECK(snapshot.get().sound_import_groups[0].member_indices[0] == 0);
    CHECK(snapshot.get().sound_import_groups[0].existing_group_id == 22);
}

TEST_CASE("Headless Compose keeps backward wheel navigation available during trailing audio",
          "[frontend-state][event-harness][playhead-tail]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.transport_available = true;
    view.transport_playing = true;
    view.transport_frame = 9 * 44100;
    view.transport_end_frame = 12 * 44100;
    view.transport_sample_rate = 44100;
    static_cast<void>(harness.frame());
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0f;
    wheel.wheel.mouse_x = 480.0f;
    wheel.wheel.mouse_y = 400.0f;
    const auto tail_action = harness.frame(std::span{&wheel, 1});
    REQUIRE(tail_action.get().compose_viewport_input_count == 1);
    CHECK(tail_action.get().compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
    CHECK(tail_action.get().compose_viewport_inputs[0].delta_y != 0.0);
    view.transport_playing = false;
    const auto stopped_action = harness.frame(std::span{&wheel, 1});
    REQUIRE(stopped_action.get().compose_viewport_input_count == 1);
    CHECK(stopped_action.get().compose_viewport_inputs[0].delta_y != 0.0);
}

namespace {
void check_tooltip_rectangle(const WormUiTestingDrawnText& text, int width, int height) {
    CHECK(text.x >= 0);
    CHECK(text.y >= 0);
    CHECK(text.x + text.width <= width);
    CHECK(text.y + text.height <= height);
}
}

TEST_CASE("Sounds clear selection hover remains readable near the application right edge",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    nuklear_event_harness_t harness{960, 720};
    REQUIRE(harness.ready());
    harness.view().compose.keysounds_available = true;
    static_cast<void>(harness.frame());
    auto& state = worm_ui_nuklear_compose_state();
    state.sounds_panel_visible = true;
    state.inspector_visible = state.layers_panel_visible = false;
    static_cast<void>(harness.frame());
    REQUIRE(state.sound_list_valid);
    const auto title = harness.drawn_text_center("Sounds  0  ·  0 selected");
    REQUIRE(title);
    // Clear selection is the eighth 27-pixel toolbar button, separated by two pixels.
    const auto mouse_x = static_cast<float>(state.sound_list_x) + 7.0f * 29.0f + 13.0f;
    const auto mouse_y = title->second + 27.0f;
    hover_at(harness, mouse_x, mouse_y);
    check_tooltip_text(harness, "Clear the selection");
    const auto text = std::ranges::find_if(harness.drawn_text_bounds(), [](const auto& item) {
        return std::string_view{item.text} == "Clear the selection";
    });
    REQUIRE(text != harness.drawn_text_bounds().end());
    check_tooltip_rectangle(*text, 960, 720);
}

namespace {
template <typename T>
void require_lane_key_sound(const T& value, std::source_location source = std::source_location::current()) {
    expect_lane_key(static_cast<bool>(value), source);
}
}

TEST_CASE("Lane keys carry the clicked Sounds dock source and leave selected notes alone",
          "[frontend-state][event-harness][lane-key-sound]") {
    nuklear_event_harness_t harness{1600, 900};
    require_lane_key_sound(harness.ready());
    auto& view = harness.view().compose;
    view.note_paint_mode_active = true;
    const std::array<WormUiComposeKeysoundRowView, 2> rows{{
        {.resource_id = 1400, .slot = "01", .name = "kick.wav"},
        {.resource_id = 1500, .slot = "02", .name = "kick2.wav"}}};
    const WormUiComposeLaneKeyBinding binding{.scancode = SDL_SCANCODE_1, .lane_id = 1};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = true;
    view.lane_key_bindings = &binding;
    view.lane_key_binding_count = 1;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sounds_panel_visible = true;
    (void)harness.frame();
    const auto row = harness.drawn_text_center("kick.wav");
    require_lane_key_sound(row);
    (void)click_at(harness, row->first, row->second, true);
    require_lane_key_sound(state.armed_resource_id == 1400);
    require_lane_key_sound(state.armed_from_sounds_dock);
    (void)harness.frame();
    require_lane_key_sound(harness.drawn_text().find("armed") != std::string::npos);
    // Return the pointer to the field so the dock does not own keyboard input.
    const auto motion = motion_event(300, 400);
    (void)harness.frame(std::span{&motion, 1});
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_1, SDL_SCANCODE_1);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_1, SDL_SCANCODE_1);
    const auto inserted = harness.frame(std::span{&press, 1});
    require_lane_key_sound(inserted.get().compose_input_count == 1);
    expect_lane_key(inserted.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_lane_key(inserted.get().compose_inputs[0].resource_id == 1400);
    expect_lane_key(harness.frame(std::span{&release, 1}).get().compose_inputs[0].kind ==
          WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
    view.selected_note_count = 1;
    const auto moved = harness.frame(std::span{&press, 1}, SDL_KMOD_SHIFT);
    require_lane_key_sound(moved.get().compose_input_count == 1);
    expect_lane_key(moved.get().compose_inputs[0].successive);
    expect_lane_key(moved.get().compose_inputs[0].resource_id == 0);
    (void)harness.frame(std::span{&release, 1});
    view.selected_note_count = 0;
    state.armed_resource_id = 0;
    const auto silent = harness.frame(std::span{&press, 1});
    require_lane_key_sound(silent.get().compose_input_count == 1);
    expect_lane_key(silent.get().compose_inputs[0].resource_id == 0);
    (void)harness.frame(std::span{&release, 1});
}

TEST_CASE("Selecting a converted BMSON stem arms lane-key entry without a double click",
          "[frontend-state][event-harness][lane-key-sound][converted-stem]") {
    nuklear_event_harness_t harness{1600, 900};
    require_lane_key_sound(harness.ready());
    auto& view = harness.view().compose;
    view.note_paint_mode_active = true;
    const auto missing = GENERATE(false, true);
    const auto modifiers = GENERATE(SDL_KMOD_NONE, SDL_KMOD_CTRL, SDL_KMOD_SHIFT);
    const WormUiComposeKeysoundRowView row{.resource_id = 1400, .slot = "",
        .name = "converted.wav", .missing = missing, .stem_source_lane_id = 71};
    const WormUiComposeLaneKeyBinding binding{.scancode = SDL_SCANCODE_2, .lane_id = 2};
    view.keysounds = &row;
    view.keysound_count = view.keysound_total_count = 1;
    view.keysounds_available = true;
    view.lane_key_bindings = &binding;
    view.lane_key_binding_count = 1;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sounds_panel_visible = true;
    (void)harness.frame();
    const auto center = harness.drawn_text_center("converted.wav");
    require_lane_key_sound(center);
    const std::array down{motion_event(center->first, center->second),
                          button_event(true, center->first, center->second)};
    (void)harness.frame(down, modifiers);
    const auto up = button_event(false, center->first, center->second);
    (void)harness.frame(std::span{&up, 1}, modifiers);
    const bool armed = !missing && modifiers == SDL_KMOD_NONE;
    expect_lane_key(state.armed_resource_id == (armed ? 1400 : 0));
    expect_lane_key(state.armed_from_sounds_dock == armed);
    const auto motion = motion_event(300, 400);
    (void)harness.frame(std::span{&motion, 1});
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    const auto action = harness.frame(std::span{&press, 1});
    require_lane_key_sound(action.get().compose_input_count == 1);
    expect_lane_key(action.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_lane_key(action.get().compose_inputs[0].resource_id == (armed ? 1400 : 0));
}

namespace {
using namespace wormedit;
using namespace wormedit::sys;

void expect_t422(bool condition, std::source_location source = std::source_location::current()) {
    INFO("T422 expectation at " << source.line());
    REQUIRE(condition);
}

auto t422_song(bool occupied_target = false, bool target_missing_in_second_measure = false)
    -> song_document_t {
    const auto profile = make_osu_mania_profile(4);
    if (!profile)
        throw std::runtime_error("osu-mania-4k profile unavailable");
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "t422-sample.wav"}};
    song.resources = {{.id = resource_id_t{1},
                       .name = "T422 sample",
                       .asset = asset_id_t{1},
                       .roles = {resource_role_t::sample}}};
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
                      {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}};
    chart.lane_layouts = {
        {.id = object_id_t{1}, .position = {measure_id_t{1}, 0}, .layout = profile->layout}};
    if (target_missing_in_second_measure) {
        auto second_layout = profile->layout;
        second_layout.profile_id += "-without-lane-2";
        std::erase_if(second_layout.lanes,
                      [](const auto& lane) { return lane.id == lane_id_t{2}; });
        chart.lane_layouts.push_back({.id = object_id_t{4},
                                      .position = {measure_id_t{2}, 0},
                                      .layout = std::move(second_layout)});
    }
    chart.input_profile = profile->input;
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "T422 layer"}};
    chart.bpms = {
        {.id = object_id_t{2}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    chart.measures[0].notes = {{.id = object_id_t{102},
                                .position = {measure_id_t{1}, 360},
                                .lane = lane_id_t{1},
                                .kind = note_kind_t::hold,
                                .end = local_position_t{measure_id_t{1}, 600},
                                .sound = resource_id_t{1},
                                .semantic_layers = {layer_id_t{7}}},
                               {.id = object_id_t{101},
                                .position = {measure_id_t{1}, 120},
                                .lane = lane_id_t{1},
                                .sound = resource_id_t{1},
                                .semantic_layers = {layer_id_t{7}}}};
    chart.measures[1].notes = {{.id = object_id_t{103},
                                .position = {measure_id_t{2}, 120},
                                .lane = lane_id_t{1},
                                .sound = resource_id_t{1},
                                .semantic_layers = {layer_id_t{7}}}};
    if (occupied_target)
        chart.measures[0].notes.push_back(
            {.id = object_id_t{104}, .position = {measure_id_t{1}, 600}, .lane = lane_id_t{2}});
    song.charts.push_back(std::move(chart));
    return song;
}

auto t422_note(document_history_t& history, std::uint64_t id) -> note_t* {
    for (auto& measure : history.document.charts.front().measures)
        for (auto& note : measure.notes)
            if (note.id == object_id_t{id})
                return &note;
    return nullptr;
}

struct t422_fixture_t {
    document_history_t history;
    using workspace_binding_t =
        decltype(make_workspace_controller(std::declval<document_history_t&>()));
    workspace_binding_t binding;
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    flattened_workspace_t flattened;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    nuklear_event_harness_t harness{1800, 1400};

    explicit t422_fixture_t(song_document_t song)
        : history(make_document_history(std::move(song))),
          binding(make_workspace_controller(history)) {
        expect_t422(harness.ready());
        expect_t422(binding.workspace.has_value());
        binding.workspace->active_tab = workspace_tab_t::compose;
        binding.workspace->selected_chart = chart_id_t{1};
        project();
        if (interaction) {
            interaction->selected_notes = {object_id_t{103}, object_id_t{101}, object_id_t{102}};
            interaction->selected_objects = {
                {editor_selection_object_kind_t::note, object_id_t{103}},
                {editor_selection_object_kind_t::note, object_id_t{101}},
                {editor_selection_object_kind_t::note, object_id_t{102}}};
        }
        project();
        (void)harness.frame();
    }

    void project() {
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *binding.workspace, cache, 1800, 1400,
                              interaction, application, nullptr);
        CAPTURE(flattened.projection_feedback.size(), cache.projection_error,
                history.document.revision, interaction.has_value());
        for (const auto& feedback : flattened.projection_feedback)
            UNSCOPED_INFO(feedback.message);
        expect_t422(interaction.has_value());
        expect_t422(flattened.view.compose.scene_available);
        harness.view().active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
        harness.view().project_loaded = true;
        harness.view().project_id = history.document.id.value;
        harness.view().selected_chart_id = chart_id_t{1}.value;
        harness.view().document_revision = history.document.revision;
        harness.view().compose = flattened.view.compose;
        harness.view().compose.visible = true;
    }

    auto dispatch(const workspace_action_snapshot_t& snapshot) -> std::string {
        auto action = snapshot.get();
        return dispatch_action(action);
    }

    auto dispatch_action(WormUiWorkspaceAction& action) -> std::string {
        return dispatch_compose_input_action(action, history, *binding.workspace, *interaction,
                                             viewport, navigation, application, cache, 0.0, &sink);
    }
};

auto t422_lane_press(t422_fixture_t& fixture, SDL_Keymod modifiers) -> std::string {
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    const auto down = fixture.harness.frame(std::span{&press, 1}, modifiers);
    CAPTURE(down.get().compose_input_count, fixture.harness.view().compose.visible,
            fixture.harness.view().compose.scene_available,
            fixture.harness.view().compose.input_occluded,
            fixture.harness.view().compose.lane_key_binding_count,
            fixture.harness.view().compose.selected_note_count,
            fixture.harness.view().active_tab, down.get().shortcut_window_focused,
            down.get().shortcut_text_entry_active, down.get().shortcut_ime_composition_active);
    expect_t422(down.get().compose_input_count == 1);
    expect_t422(down.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_t422(down.get().compose_inputs[0].lane_id == 2);
    expect_t422(down.get().compose_inputs[0].successive == ((modifiers & SDL_KMOD_SHIFT) != 0));
    auto status = fixture.dispatch(down);
    fixture.project();
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_2, SDL_SCANCODE_2);
    const auto up = fixture.harness.frame(std::span{&release, 1}, modifiers);
    (void)fixture.dispatch(up);
    fixture.project();
    return status;
}

auto t422_shift_press(t422_fixture_t& fixture) -> std::string {
    return t422_lane_press(fixture, SDL_KMOD_SHIFT);
}

void t422_check_note_identity(const note_t& actual, const note_t& original) {
    CHECK(actual.id == original.id);
    CHECK(actual.position == original.position);
    CHECK(actual.end == original.end);
}

void t422_check_note_payload(const note_t& actual, const note_t& original,
                             lane_id_t expected_lane) {
    CHECK(actual.kind == original.kind);
    CHECK(actual.sound == original.sound);
    CHECK(actual.semantic_layers == original.semantic_layers);
    CHECK(actual.lane == expected_lane);
}

void t422_check_note_contents(const note_t& actual, const note_t& original,
                              lane_id_t expected_lane) {
    t422_check_note_identity(actual, original);
    t422_check_note_payload(actual, original, expected_lane);
}

void t422_check_selection(const t422_fixture_t& fixture,
                          const std::vector<object_id_t>& expected_notes,
                          const std::vector<editor_selection_object_t>& expected_objects) {
    CHECK(fixture.interaction->selected_notes == expected_notes);
    CHECK(fixture.interaction->selected_objects == expected_objects);
}

auto t422_native_bytes(const song_document_t& song) -> std::string {
    const auto encoded = write_native_project(song);
    if (!encoded.bytes)
        throw std::runtime_error("T422 native snapshot serialization failed");
    return *encoded.bytes;
}

void t422_check_unmodified(const t422_fixture_t& fixture, std::string_view bytes,
                           std::uint64_t revision, std::size_t undo_depth,
                           const std::vector<object_id_t>& selected_notes,
                           const std::vector<editor_selection_object_t>& selected_objects) {
    CHECK(t422_native_bytes(fixture.history.document) == bytes);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.size() == undo_depth);
    t422_check_selection(fixture, selected_notes, selected_objects);
    CHECK_FALSE(fixture.interaction->pending_hold.has_value());
    CHECK(fixture.interaction->additional_keyboard_holds.empty());
}

void t422_check_native_binding(const t422_fixture_t& fixture) {
    const auto profile = make_osu_mania_profile(4);
    expect_t422(profile.has_value());
    const auto expected =
        std::ranges::find(profile->input.bindings, lane_id_t{2}, &lane_binding_t::lane);
    expect_t422(expected != profile->input.bindings.end());
    expect_t422(expected->key == physical_key_t::digit_2);
    const auto& chart = fixture.history.document.charts.front();
    expect_t422(chart.lane_layouts.front().layout.profile_id == "osu-mania-4k");
    expect_t422(chart.input_profile.profile_id == profile->input.profile_id);
    const auto& view = fixture.flattened.view.compose;
    const auto projected =
        std::ranges::find_if(std::span{view.lane_key_bindings, view.lane_key_binding_count},
                             [](const auto& binding) { return binding.lane_id == 2; });
    expect_t422(projected != std::span{view.lane_key_bindings, view.lane_key_binding_count}.end());
    expect_t422(projected->scancode == SDL_SCANCODE_2);
}

auto t422_note_screen_center(t422_fixture_t& fixture, std::uint64_t note_id)
    -> std::optional<std::pair<float, float>> {
    (void)fixture.harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    if (!viewport)
        return std::nullopt;
    const auto& view = fixture.harness.view().compose;
    for (std::size_t index = 0; index < view.note_endpoint_count; ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id != note_id || note.tail)
            continue;
        const auto x = (note.bounds.left + note.bounds.right) * 0.5;
        const auto y = (note.bounds.top + note.bounds.bottom) * 0.5;
        return std::pair{
            viewport->x + static_cast<float>(x * viewport->width / view.viewport_width_pixels),
            viewport->y + static_cast<float>(y * viewport->height / view.viewport_height_pixels)};
    }
    return std::nullopt;
}

void t422_check_all_notes_in_lane(t422_fixture_t& fixture,
                                  const std::array<note_t, 3>& original,
                                  lane_id_t expected_lane) {
    for (const auto& source : original) {
        const auto* note = t422_note(fixture.history, source.id.value);
        REQUIRE(note);
        t422_check_note_contents(*note, source, expected_lane);
    }
}

void t422_set_expected_lane(song_document_t& expected, object_id_t id, lane_id_t lane) {
    for (auto& measure : expected.charts.front().measures)
        for (auto& note : measure.notes)
            if (note.id == id)
                note.lane = lane;
}

void t422_update_expected_prefix(song_document_t& expected,
                                 const std::array<object_id_t, 3>& ids, std::size_t step) {
    for (std::size_t index = 0; index <= step; ++index)
        t422_set_expected_lane(expected, ids[index], lane_id_t{2});
}

auto t422_expected_remaining_notes(std::size_t step) -> std::vector<object_id_t> {
    if (step == 0)
        return {object_id_t{103}, object_id_t{102}};
    if (step == 1)
        return {object_id_t{103}};
    return {};
}

auto t422_expected_remaining_objects(std::size_t step)
    -> std::vector<editor_selection_object_t> {
    if (step == 0)
        return {{editor_selection_object_kind_t::note, object_id_t{103}},
                {editor_selection_object_kind_t::note, object_id_t{102}}};
    if (step == 1)
        return {{editor_selection_object_kind_t::note, object_id_t{103}}};
    return {};
}

void t422_check_step_note_lanes(t422_fixture_t& fixture,
                                const std::array<note_t, 3>& originals,
                                std::size_t step) {
    const auto ids = std::array{object_id_t{101}, object_id_t{102}, object_id_t{103}};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto lane = index <= step ? lane_id_t{2} : lane_id_t{1};
        const auto* note = t422_note(fixture.history, ids[index].value);
        REQUIRE(note);
        t422_check_note_contents(*note, originals[index], lane);
    }
}

void t422_check_shift_step(t422_fixture_t& fixture, song_document_t& expected_document,
                           const std::array<note_t, 3>& originals, std::size_t step,
                           std::string_view original_bytes) {
    const auto ids = std::array{object_id_t{101}, object_id_t{102}, object_id_t{103}};
    const auto revision = fixture.history.document.revision;
    CHECK(t422_shift_press(fixture) == "Next selected object assigned to lane.");
    CHECK(fixture.history.document.revision == revision + 1);
    CHECK(fixture.history.undo.size() == step + 1);
    t422_check_selection(fixture, t422_expected_remaining_notes(step),
                         t422_expected_remaining_objects(step));
    t422_check_step_note_lanes(fixture, originals, step);
    t422_update_expected_prefix(expected_document, ids, step);
    expected_document.revision = fixture.history.document.revision;
    CHECK(t422_native_bytes(fixture.history.document) == t422_native_bytes(expected_document));
    CHECK(t422_native_bytes(fixture.history.document) != original_bytes);
    CHECK_FALSE(fixture.interaction->pending_hold.has_value());
}

void t422_check_undo_result(t422_fixture_t& fixture,
                            const std::array<note_t, 3>& originals) {
    CHECK(fixture.history.undo.size() == 2);
    t422_check_note_contents(*t422_note(fixture.history, 101), originals[0], lane_id_t{2});
    t422_check_note_contents(*t422_note(fixture.history, 102), originals[1], lane_id_t{2});
    t422_check_note_contents(*t422_note(fixture.history, 103), originals[2], lane_id_t{1});
    CHECK(fixture.interaction->selected_notes.empty());
    CHECK(fixture.interaction->selected_objects.empty());
    CHECK_FALSE(fixture.interaction->pending_hold.has_value());
}

void t422_apply_shift_sequence(t422_fixture_t& fixture, song_document_t& expected_document,
                               const std::array<note_t, 3>& originals,
                               std::string_view original_bytes) {
    for (std::size_t step = 0; step < originals.size(); ++step)
        t422_check_shift_step(fixture, expected_document, originals, step, original_bytes);
    CHECK(fixture.history.undo.size() == 3);
}

void t422_undo_last_shift(t422_fixture_t& fixture, song_document_t& expected_document,
                          const std::array<note_t, 3>& originals) {
    WormUiWorkspaceAction undo{};
    undo.compose_input_project_id = fixture.history.document.id.value;
    undo.compose_input_chart_id = fixture.history.document.charts.front().id.value;
    undo.compose_input_document_revision = fixture.history.document.revision;
    undo.compose_input_viewport_width_pixels = fixture.harness.view().compose.viewport_width_pixels;
    undo.compose_input_count = 1;
    undo.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_UNDO;
    const auto revision = fixture.history.document.revision;
    CHECK(fixture.dispatch_action(undo) == "Undo applied.");
    fixture.project();
    CHECK(fixture.history.document.revision == revision - 1);
    t422_set_expected_lane(expected_document, object_id_t{103}, lane_id_t{1});
    expected_document.revision = fixture.history.document.revision;
    CHECK(t422_native_bytes(fixture.history.document) == t422_native_bytes(expected_document));
    t422_check_undo_result(fixture, originals);
}

void t422_open_note_actions(t422_fixture_t& fixture) {
    const auto center = t422_note_screen_center(fixture, 101);
    REQUIRE(center.has_value());
    const auto opened = right_click_at(fixture.harness, center->first, center->second);
    REQUIRE(opened.size() == 2);
    REQUIRE(opened.front().get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    REQUIRE(compose_note_menu_open());
}
} // namespace

TEST_CASE(
    "Shift plus the profile lane key moves one note per real dispatch and undo restores the last",
    "[frontend-state][event-harness][lane-key-move-and-deselect]") {
    t422_fixture_t fixture{t422_song()};
    t422_check_native_binding(fixture);
    const std::array<note_t, 3> originals{*t422_note(fixture.history, 101),
                                          *t422_note(fixture.history, 102),
                                          *t422_note(fixture.history, 103)};
    auto expected_document = fixture.history.document;
    const auto original_bytes = t422_native_bytes(expected_document);
    t422_apply_shift_sequence(fixture, expected_document, originals, original_bytes);
    CHECK(fixture.interaction->selected_notes.empty());
    CHECK(fixture.interaction->selected_objects.empty());
    t422_undo_last_shift(fixture, expected_document, originals);
}

TEST_CASE("A plain profile lane key moves all selected notes and preserves their selection",
          "[frontend-state][event-harness][lane-key-move-and-deselect]") {
    t422_fixture_t fixture{t422_song()};
    const std::array<note_t, 3> originals{*t422_note(fixture.history, 101),
                                          *t422_note(fixture.history, 102),
                                          *t422_note(fixture.history, 103)};
    const std::vector<object_id_t> selection{object_id_t{103}, object_id_t{101}, object_id_t{102}};
    const std::vector<editor_selection_object_t> selected_objects{
        {editor_selection_object_kind_t::note, object_id_t{103}},
        {editor_selection_object_kind_t::note, object_id_t{101}},
        {editor_selection_object_kind_t::note, object_id_t{102}}};
    const auto revision = fixture.history.document.revision;
    CHECK(t422_lane_press(fixture, SDL_KMOD_NONE) == "Selected objects assigned to lane.");
    CHECK(fixture.history.document.revision == revision + 1);
    CHECK(fixture.history.undo.size() == 1);
    t422_check_selection(fixture, selection, selected_objects);
    t422_check_all_notes_in_lane(fixture, originals, lane_id_t{2});
    CHECK_FALSE(fixture.interaction->pending_hold.has_value());
    CHECK(fixture.interaction->additional_keyboard_holds.empty());
}

auto t422_copy_notes(const document_history_t& history) -> std::vector<note_t> {
    std::vector<note_t> result;
    for (const auto& measure : history.document.charts.front().measures)
        result.insert(result.end(), measure.notes.begin(), measure.notes.end());
    return result;
}

void t422_check_notes_unchanged(const t422_fixture_t& fixture,
                                const std::vector<note_t>& original) {
    const auto current = t422_copy_notes(fixture.history);
    REQUIRE(current.size() == original.size());
    for (std::size_t index = 0; index < original.size(); ++index)
        t422_check_note_contents(current[index], original[index], original[index].lane);
}

void t422_reject_without_mutation(t422_fixture_t& fixture, std::string_view reason,
                                  const std::vector<object_id_t>& selection,
                                  const std::vector<editor_selection_object_t>& objects) {
    const auto revision = fixture.history.document.revision;
    const auto undo_depth = fixture.history.undo.size();
    const auto original = t422_copy_notes(fixture.history);
    const auto original_bytes = t422_native_bytes(fixture.history.document);
    const auto status = t422_shift_press(fixture);
    CHECK(status.find(reason) != std::string::npos);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.size() == undo_depth);
    t422_check_notes_unchanged(fixture, original);
    CHECK(t422_native_bytes(fixture.history.document) == original_bytes);
    t422_check_selection(fixture, selection, objects);
    CHECK_FALSE(fixture.interaction->pending_hold.has_value());
    CHECK(fixture.interaction->additional_keyboard_holds.empty());
}

TEST_CASE("A lane overlap rejects Shift assignment without changing document or selection",
          "[frontend-state][event-harness][lane-key-move-and-deselect]") {
    t422_fixture_t fixture{t422_song(true)};
    CHECK(t422_shift_press(fixture) == "Next selected object assigned to lane.");
    const std::vector<object_id_t> selection{object_id_t{103}, object_id_t{102}};
    const std::vector<editor_selection_object_t> objects{
        {editor_selection_object_kind_t::note, object_id_t{103}},
        {editor_selection_object_kind_t::note, object_id_t{102}}};
    t422_reject_without_mutation(fixture, "lane assignment overlaps another target-lane note",
                                 selection, objects);
}

TEST_CASE("A lane missing from the effective layout rejects the final successive assignment",
          "[frontend-state][event-harness][lane-key-move-and-deselect]") {
    t422_fixture_t fixture{t422_song(false, true)};
    CHECK(t422_shift_press(fixture) == "Next selected object assigned to lane.");
    CHECK(t422_shift_press(fixture) == "Next selected object assigned to lane.");
    const std::vector<object_id_t> selection{object_id_t{103}};
    const std::vector<editor_selection_object_t> objects{
        {editor_selection_object_kind_t::note, object_id_t{103}}};
    t422_reject_without_mutation(fixture,
                                 "target lane is unavailable at a selected note endpoint",
                                 selection, objects);
}

auto t422_initial_notes() -> std::vector<object_id_t> {
    return {object_id_t{103}, object_id_t{101}, object_id_t{102}};
}

auto t422_initial_objects() -> std::vector<editor_selection_object_t> {
    return {{editor_selection_object_kind_t::note, object_id_t{103}},
            {editor_selection_object_kind_t::note, object_id_t{101}},
            {editor_selection_object_kind_t::note, object_id_t{102}}};
}

void t422_check_suppressed_event(t422_fixture_t& fixture,
                                 const workspace_action_snapshot_t& snapshot,
                                 std::string_view original_bytes) {
    CHECK(snapshot.get().compose_input_count == 0);
    (void)fixture.dispatch(snapshot);
    fixture.project();
    t422_check_unmodified(fixture, original_bytes, 1, 0, t422_initial_notes(),
                          t422_initial_objects());
}

void t422_suppress_repeat_and_unbound_keys() {
    t422_fixture_t fixture{t422_song()};
    const auto bytes = t422_native_bytes(fixture.history.document);
    const auto repeat = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2, true);
    t422_check_suppressed_event(
        fixture, fixture.harness.frame(std::span{&repeat, 1}, SDL_KMOD_SHIFT), bytes);
    const auto unbound = key_event(SDL_EVENT_KEY_DOWN, SDLK_5, SDL_SCANCODE_5);
    t422_check_suppressed_event(
        fixture, fixture.harness.frame(std::span{&unbound, 1}, SDL_KMOD_SHIFT), bytes);
}

void t422_suppress_go_to_measure() {
    t422_fixture_t fixture{t422_song()};
    const auto bytes = t422_native_bytes(fixture.history.document);
    const auto go = key_event(SDL_EVENT_KEY_DOWN, SDLK_G, SDL_SCANCODE_G);
    const auto opened = fixture.harness.frame(std::span{&go, 1}, SDL_KMOD_CTRL);
    (void)fixture.dispatch(opened);
    CHECK(worm_ui_nuklear_compose_state().go_to_measure_open);
    const auto shifted = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    t422_check_suppressed_event(
        fixture, fixture.harness.frame(std::span{&shifted, 1}, SDL_KMOD_SHIFT), bytes);
}

void t422_suppress_ime() {
    t422_fixture_t fixture{t422_song()};
    const auto bytes = t422_native_bytes(fixture.history.document);
    constexpr char preedit_text[]{"typing"};
    SDL_Event editing{};
    editing.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.text = preedit_text;
    const auto shifted = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    const std::array events{editing, shifted};
    t422_check_suppressed_event(fixture, fixture.harness.frame(events, SDL_KMOD_SHIFT), bytes);
}

void t422_suppress_focus_loss() {
    t422_fixture_t fixture{t422_song()};
    const auto bytes = t422_native_bytes(fixture.history.document);
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    lost.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)fixture.dispatch(fixture.harness.frame(std::span{&lost, 1}));
    const auto shifted = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    t422_check_suppressed_event(
        fixture, fixture.harness.frame(std::span{&shifted, 1}, SDL_KMOD_SHIFT), bytes);
}

void t422_suppress_note_actions() {
    t422_fixture_t fixture{t422_song()};
    const auto bytes = t422_native_bytes(fixture.history.document);
    t422_open_note_actions(fixture);
    const auto shifted = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    t422_check_suppressed_event(
        fixture, fixture.harness.frame(std::span{&shifted, 1}, SDL_KMOD_SHIFT), bytes);
}

void t422_suppress_actual_text_editor() {
    t422_fixture_t fixture{t422_song()};
    const auto bytes = t422_native_bytes(fixture.history.document);
    worm_ui_nuklear_compose_state().inspector_visible = true;
    (void)fixture.harness.frame();
    const auto header = fixture.harness.drawn_text_center("Find note label");
    REQUIRE(header.has_value());
    (void)click_at(fixture.harness, header->first, header->second + 27);
    CHECK(fixture.harness.frame().get().shortcut_text_entry_active);
    CHECK_FALSE(worm_ui_nuklear_compose_state().go_to_measure_open);
    const auto shifted = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    t422_check_suppressed_event(
        fixture, fixture.harness.frame(std::span{&shifted, 1}, SDL_KMOD_SHIFT), bytes);
}

TEST_CASE("Repeat, text input, IME, focus loss and Note actions suppress Shift lane mutation",
          "[frontend-state][event-harness][lane-key-move-and-deselect]") {
    t422_suppress_repeat_and_unbound_keys();
    t422_suppress_go_to_measure();
    t422_suppress_ime();
    t422_suppress_focus_loss();
    t422_suppress_note_actions();
    t422_suppress_actual_text_editor();
}

TEST_CASE("BMS branch panel shows control nodes as a tree and unparsed statements per node",
          "[frontend-state][event-harness][bms-branch]") {
    nuklear_event_harness_t harness{1600, 1000};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeBranchRowView, 5> rows{{
        {.display_text = "BMS program", .depth = 0, .active = true, .node_id = 1},
        {.display_text = "#TITLE odd", .depth = 1, .active = true, .node_id = 9,
         .raw_statement = true, .statement_id = 9},
        {.display_text = "#RANDOM 2", .depth = 1, .active = true, .has_source_order = true,
         .source_order = 4, .node_id = 2, .choice_block = true},
        {.display_text = "#IF 1", .depth = 2, .active = true, .has_source_order = true,
         .source_order = 5, .node_id = 3},
        {.display_text = "#UNKNOWN x", .depth = 3, .active = true, .node_id = 10,
         .raw_statement = true, .statement_id = 10}}};
    const std::array<WormUiComposeBranchChoiceView, 1> choices{{
        {.branch_id = 2, .kind = WORM_UI_COMPOSE_BRANCH_RANDOM, .has_source_order = true,
         .source_order = 4, .active = true, .editable = true,
         .has_next_value = true, .next_value = 1}}};
    view.branch_outline_available = true;
    view.branch_rows = rows.data(); view.branch_row_count = rows.size();
    view.branch_choices = choices.data(); view.branch_choice_count = choices.size();
    worm_ui_nuklear_compose_state().inspector_visible = true;
    (void)harness.frame();
    (void)harness.frame();
    const auto& text = harness.drawn_text();
    INFO(text);
    CHECK(text.find("#RANDOM 2") != std::string::npos);
    CHECK(text.find("#IF 1") != std::string::npos);
    CHECK(text.find("RANDOM #4") != std::string::npos);
    // The root is selected first, so only its own unparsed statement is listed.
    CHECK(text.find("#TITLE odd") != std::string::npos);
    CHECK(text.find("#UNKNOWN x") == std::string::npos);
    const auto node = harness.drawn_text_center("#5 #IF 1");
    REQUIRE(node);
    (void)click_at(harness, node->first, node->second);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("#UNKNOWN x") != std::string::npos);
    CHECK(harness.drawn_text().find("#TITLE odd") == std::string::npos);
}

namespace {

auto block_action_from(const std::vector<workspace_action_snapshot_t>& frames,
                       WormUiComposeActionKind kind) -> const WormUiWorkspaceAction* {
    for (const auto& frame : frames)
        if (frame.get().compose_action == kind) return &frame.get();
    return nullptr;
}

void click_label(nuklear_event_harness_t& harness, const char* label) {
    const auto center = harness.drawn_text_center(label);
    REQUIRE(center);
    (void)click_at(harness, center->first, center->second);
    (void)harness.frame();
}

auto click_button(nuklear_event_harness_t& harness, const char* label)
    -> std::vector<workspace_action_snapshot_t> {
    const auto center = harness.drawn_text_center(label);
    REQUIRE(center);
    return click_at(harness, center->first, center->second);
}

} // namespace

TEST_CASE("BMS branch outline buttons emit create, remove and leaf move actions",
          "[frontend-state][event-harness][bms-branch]") {
    nuklear_event_harness_t harness{1600, 1000};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.chart_id = 7;
    view.document_revision = 21;
    const std::array<WormUiComposeBranchRowView, 7> rows{{
        {.display_text = "BMS program", .depth = 0, .active = true, .node_id = 1},
        {.display_text = "#TITLE odd", .depth = 1, .active = true, .node_id = 9,
         .raw_statement = true, .statement_id = 9},
        {.display_text = "#RANDOM 2", .depth = 1, .active = true, .has_source_order = true,
         .source_order = 4, .node_id = 2, .choice_block = true},
        {.display_text = "#IF group", .depth = 2, .active = true, .has_source_order = true,
         .source_order = 5, .node_id = 3},
        {.display_text = "#IF 1", .depth = 3, .active = true, .has_source_order = true,
         .source_order = 6, .node_id = 4},
        {.display_text = "#ELSEIF 2", .depth = 3, .active = false, .has_source_order = true,
         .source_order = 7, .node_id = 5},
        {.display_text = "#ENDIF end", .depth = 2, .active = true, .has_source_order = true,
         .source_order = 8, .node_id = 6}}};
    const std::array<WormUiComposeBranchChoiceView, 1> choices{{
        {.branch_id = 2, .kind = WORM_UI_COMPOSE_BRANCH_RANDOM, .has_source_order = true,
         .source_order = 4, .active = true, .editable = true, .has_next_value = true, .next_value = 1}}};
    view.branch_outline_available = true;
    view.branch_rows = rows.data(); view.branch_row_count = rows.size();
    view.branch_choices = choices.data(); view.branch_choice_count = choices.size();
    worm_ui_nuklear_compose_state().inspector_visible = true;
    (void)harness.frame();
    (void)harness.frame();

    // Nothing selected that the buttons can act on: the root is selected, no statement chosen.
    for (const auto* label : {"New block", "Remove block", "Leaf up", "Leaf down"})
        CHECK(block_action_from(click_button(harness, label), WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK) == nullptr);

    click_label(harness, " ▶ #TITLE odd");
    const auto create = click_button(harness, "New block");
    const auto* created = block_action_from(create, WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK);
    REQUIRE(created);
    CHECK(created->compose_branch_statement_id == 9);
    CHECK(created->compose_input_chart_id == 7);
    CHECK(created->compose_input_document_revision == 21);

    click_label(harness, "#4 #RANDOM 2");
    const auto* removed = block_action_from(click_button(harness, "Remove block"),
                                            WORM_UI_COMPOSE_ACTION_REMOVE_BMS_BRANCH_BLOCK);
    REQUIRE(removed);
    CHECK(removed->compose_branch_id == 2);

    click_label(harness, "#6 #IF 1");
    CHECK(block_action_from(click_button(harness, "Leaf up"), WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF) == nullptr);
    const auto* down = block_action_from(click_button(harness, "Leaf down"),
                                         WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF);
    REQUIRE(down);
    CHECK(down->compose_branch_id == 4);
    CHECK(down->compose_branch_value == 1);

    click_label(harness, "(inactive) #7 #ELSEIF 2");
    const auto* up = block_action_from(click_button(harness, "Leaf up"),
                                       WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF);
    REQUIRE(up);
    CHECK(up->compose_branch_id == 5);
    CHECK(up->compose_branch_value == -1);
    CHECK(block_action_from(click_button(harness, "Leaf down"), WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF) == nullptr);
}

TEST_CASE("BMS branch dock pushes the field over and hides on a narrow window",
          "[frontend-state][event-harness][bms-branch]") {
    const auto field_left = [](int width, bool with_branches) {
        nuklear_event_harness_t harness{width, 900};
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.branch_outline_available = with_branches;
        worm_ui_nuklear_compose_state().branch_panel_visible = true;
        (void)harness.frame();
        WormUiTestingRect rect{};
        REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&rect));
        return std::pair{rect.x, worm_ui_frontend_compose_left_dock_pixels()};
    };
    const auto plain = field_left(1280, false);
    const auto docked = field_left(1280, true);
    CHECK(plain.second == 0);
    CHECK(docked.second > 0);
    CHECK(docked.first >= plain.first + static_cast<float>(docked.second) - 1.0f);
    const auto narrow = field_left(800, true);
    CHECK(narrow.second == 0);
}

namespace {
void check_keysound_edit_action(const WormUiOptionsAction& edited) {
    CHECK(edited.kind == WORM_UI_OPTIONS_EDIT_FIELD);
    CHECK(edited.field == WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY);
    CHECK(edited.integer == 513);
}
}
TEST_CASE("Options Audio exposes and edits the keysound memory field", "[options][budget][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    harness.view().options = {.visible = true, .revision = 91, .category = WORM_UI_OPTIONS_AUDIO,
        .keysound_budget_mib = 512, .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = ""};
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Keysound memory (MiB)");
    REQUIRE(label);
    const auto value = harness.drawn_text_center("512");
    REQUIRE(value);
    const auto clicks = click_at(harness, value->first + 28, value->second);
    const auto edited = std::ranges::find_if(clicks, [](const auto& frame) {
        return frame.get().options.kind == WORM_UI_OPTIONS_EDIT_FIELD;
    });
    REQUIRE(edited != clicks.end());
    check_keysound_edit_action(edited->get().options);
}

TEST_CASE("Input shortcuts page lists rows and Change requests a capture", "[options][shortcuts][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    static const WormUiShortcutRowView rows[]{{"Play", "Space", false}, {"Undo", "Ctrl+Z", true}};
    harness.view().options = {.visible = true, .revision = 33, .category = WORM_UI_OPTIONS_INPUT,
        .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = "",
        .shortcut_rows = rows, .shortcut_row_count = 2};
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Undo"));
    REQUIRE(harness.drawn_text_center("Ctrl+Z  (conflict)"));
    // Rows draw top to bottom, so the last "Change" label belongs to the second row.
    const auto change = harness.drawn_text_center("Change");
    REQUIRE(change);
    const auto clicks = click_at(harness, change->first, change->second);
    const auto requested = std::ranges::find_if(clicks, [](const auto& frame) {
        return frame.get().options.kind == WORM_UI_OPTIONS_SHORTCUT_CAPTURE;
    });
    REQUIRE(requested != clicks.end());
    CHECK(requested->get().options.shortcut_row == 1);
}

TEST_CASE("Key presses during a shortcut capture reach the frame's shortcut events", "[options][shortcuts][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    static const WormUiShortcutRowView rows[]{{"Play", "Space", false}};
    harness.view().options = {.visible = true, .revision = 34, .category = WORM_UI_OPTIONS_INPUT,
        .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = "",
        .shortcut_rows = rows, .shortcut_row_count = 1, .shortcut_capturing = true, .shortcut_capture_row = 0};
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Press a chord..."));
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_K, SDL_SCANCODE_K);
    const std::array<SDL_Event, 1> events{press};
    const auto snapshot = harness.frame(events);
    REQUIRE(snapshot.get().shortcut_event_count == 1);
    CHECK(snapshot.get().shortcut_events[0].hid_usage == 0x0e);
}

namespace {
void section_import_check(bool value) { CHECK(value); }
void section_import_require(bool value) { REQUIRE(value); }
void section_add_emits_picker_kind(std::string_view label, std::uint8_t kind) {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.bmson_tools_available = true;
    view.keysounds_available = true;
    (void)harness.frame();
    const auto button = harness.drawn_text_center(label);
    section_import_require(button.has_value());
    const auto actions = click_at(harness, button->first, button->second);
    const auto found = std::ranges::find_if(actions, [](const auto& a) {
        return a.get().sound_import_pick_requested;
    });
    section_import_require(found != actions.end());
    section_import_check(found->get().sound_import_kind == kind);
}
void section_review_submits_fixed_kind(std::uint8_t kind, bool stale = false) {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view();
    const auto bound_revision = view.document_revision;
    const WormUiSoundImportItem item{"/music/new.ogg", "New sound", kind, true};
    section_import_require(worm_ui_frontend_stage_sound_import(view.project_id,
        view.selected_chart_id, view.document_revision, &item, 1, nullptr, 0, false, ""));
    if (stale) ++view.document_revision;
    (void)harness.frame();
    (void)harness.frame();  // A dialog fits its content on its second frame.
    section_import_check(worm_ui_nuklear_pending_sound_import().fixed_kind == kind);
    section_import_check(harness.drawn_text().find(kind == 1 ? "Stem" : "Keysound") != std::string::npos);
    const auto confirm = harness.drawn_text_center("Confirm import");
    section_import_require(confirm.has_value());
    const auto actions = click_at(harness, confirm->first, confirm->second);
    const auto found = std::ranges::find_if(actions, [](const auto& a) {
        return a.get().compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
    });
    section_import_require(found != actions.end());
    section_import_check(found->get().sound_import_kind == kind);
    section_import_check(found->get().compose_input_document_revision == bound_revision);
    section_import_check(found->get().sound_import_item_count == 1);
    section_import_check(found->get().sound_import_items[0].kind == kind);
    section_import_check(found->get().sound_import_items[0].kind_fixed);
}
}
TEST_CASE("sound_import BMSON section buttons preserve exact picker kind", "[event-harness][sound_import]") {
    section_add_emits_picker_kind("Add stem…", WORM_UI_SOUND_IMPORT_STEM);
    section_add_emits_picker_kind("Add keysound…", WORM_UI_SOUND_IMPORT_KEYSOUND);
}
TEST_CASE("sound_import section reviews submit fixed kind", "[event-harness][sound_import]") {
    section_review_submits_fixed_kind(WORM_UI_SOUND_IMPORT_STEM);
    section_review_submits_fixed_kind(WORM_UI_SOUND_IMPORT_KEYSOUND);
    section_review_submits_fixed_kind(WORM_UI_SOUND_IMPORT_STEM, true);
}

TEST_CASE("sound_import section controls stay BMSON only and fixed review cancels changed chart",
          "[event-harness][sound_import]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view();
    view.compose.bmson_tools_available = false;
    view.compose.keysounds_available = true;
    (void)harness.frame();
    section_import_check(!harness.drawn_text_center("Add stem…").has_value());
    section_import_check(!harness.drawn_text_center("Add keysound…").has_value());
    const WormUiSoundImportItem item{"/music/new.ogg", "New sound", WORM_UI_SOUND_IMPORT_STEM, true};
    section_import_require(worm_ui_frontend_stage_sound_import(view.project_id,
        view.selected_chart_id, view.document_revision, &item, 1, nullptr, 0, false, ""));
    ++view.selected_chart_id;
    const auto action = harness.frame();
    section_import_check(action.get().compose_action != WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT);
    section_import_check(!worm_ui_nuklear_sound_import_active());
}

namespace {

auto has_draw_source_payload(const std::vector<workspace_action_snapshot_t>& frames,
                             std::uint8_t kind, std::uint64_t id) -> bool {
    for (const auto& frame : frames)
        for (std::size_t index = 0; index < frame.get().compose_input_count; ++index) {
            const auto& input = frame.get().compose_inputs[index];
            if (input.kind == WORM_UI_COMPOSE_INPUT_DRAW_SOURCE &&
                input.resource_role == kind && input.resource_id == id) return true;
        }
    return false;
}

auto has_draw_input(const std::vector<workspace_action_snapshot_t>& frames,
                    WormUiComposeInputKind kind, std::uint8_t intent) -> bool {
    const auto* input = find_compose_input(frames, kind);
    return input && input->paint_intent == intent;
}

auto draw_right_event(bool down, float x, float y) -> SDL_Event {
    auto event = button_event(down, x, y);
    event.button.button = SDL_BUTTON_RIGHT;
    return event;
}

auto right_draw_script(nuklear_event_harness_t& harness, float x, float y,
                       float end_x, float end_y) -> std::vector<workspace_action_snapshot_t> {
    const auto press = draw_right_event(true, x, y);
    const auto motion = motion_event(end_x, end_y);
    const auto release = draw_right_event(false, end_x, end_y);
    std::array<event_step_t, 3> steps{};
    steps[0].events = {motion_event(x, y), press};
    steps[1].events = {motion};
    steps[2].events = {motion_event(end_x, end_y), release};
    return harness.run_script(steps);
}

void expect_draw(bool condition, std::source_location source = std::source_location::current()) {
    INFO("Assertion caller line " << source.line());
    CHECK(condition);
}

} // namespace

TEST_CASE("Sound dock row sets the Draw source without activating a brush",
          "[frontend-state][event-harness][draw]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 1> rows{{
        {.resource_id = 1400, .slot = "01", .name = "kick.wav"}}};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.sounds_panel_visible = true;
    (void)harness.frame();
    const auto row = harness.drawn_text_center("kick.wav");
    REQUIRE(row.has_value());
    const auto selected = click_at(harness, row->first, row->second, true);
    expect_draw(has_draw_source_payload(selected, 1, 1400));
    expect_draw(state.draw_source_kind == 1 && state.draw_source_id == 1400);
    expect_draw(!view.note_paint_mode_active);
    expect_draw(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
}

TEST_CASE("BGA dock row carries its source payload without enabling resource placement",
          "[frontend-state][event-harness][draw][bga]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<std::uint64_t, 1> ids{1400};
    const std::array<WormUiComposeBgaRowView, 1> rows{{
        {.resource_id = 1400, .slot = "01", .name = "clip.png"}}};
    view.bga_rows = rows.data();
    view.bga_row_count = view.bga_total_matching = rows.size();
    view.bga_matching_ids = ids.data();
    view.bga_advance_ids = ids.data();
    view.bga_advance_count = ids.size();
    view.bga_search = "";
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.bga_panel_visible = true;
    (void)harness.frame();
    REQUIRE(state.bga_list_valid);
    const auto clip = harness.drawn_text_center("clip.png");
    REQUIRE(clip);
    const auto selected = click_at(harness, clip->first, clip->second, true);
    expect_draw(has_draw_source_payload(selected, 2, 1400));
    expect_draw(!view.note_paint_mode_active);
    expect_draw(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
}

TEST_CASE("Selecting the silent source emits a source payload and keeps Draw active",
          "[frontend-state][event-harness][draw]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.note_paint_mode_active = true;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.draw_source_kind = 1;
    state.draw_source_id = 40;
    WormUiWorkspaceAction picked{};
    worm_ui_nuklear_pick_draw_source(state, picked, 0, 0);
    expect_draw(picked.compose_input_count == 1);
    expect_draw(picked.compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_DRAW_SOURCE);
    expect_draw(picked.compose_inputs[0].resource_role == 0 &&
                picked.compose_inputs[0].resource_id == 0);
    const auto click = click_at(harness, 300, 400);
    expect_draw(has_draw_input(click, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 1));
    const auto* begin = find_compose_input(click, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    expect_draw(begin && begin->resource_id == 0);
    expect_draw(view.note_paint_mode_active);
}

TEST_CASE("Draw opens the note menu on right click but erases only after right drag",
          "[frontend-state][event-harness][draw]") {
    {
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.note_paint_mode_active = true;
    (void)fixture.harness.frame();
    const auto click = right_click_at(fixture.harness, 300, 300);
    expect_draw(click.size() == 2);
    expect_draw(click[0].get().compose_action != WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    expect_draw(click[1].get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    expect_draw(find_compose_input(click, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == nullptr);
    expect_draw(compose_note_menu_open());

    }
    note_menu_fixture_t dragged_fixture;
    auto& dragged_view = dragged_fixture.harness.view().compose;
    dragged_view.note_paint_mode_active = true;
    dragged_view.viewport_width_pixels = 1600.5;
    dragged_view.viewport_height_pixels = 1100.25;
    (void)dragged_fixture.harness.frame();
    const auto drag = right_draw_script(dragged_fixture.harness, 300, 300, 304, 340);
    expect_draw(has_draw_input(drag, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 2));
    expect_draw(has_draw_input(drag, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE, 2));
    expect_draw(has_draw_input(drag, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 2));
    expect_draw(!compose_note_menu_open());
}

TEST_CASE("Right drag threshold uses screen pixels with fractional scene dimensions",
          "[frontend-state][event-harness][draw]") {
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.note_paint_mode_active = true;
    view.viewport_width_pixels = 1600.5;
    view.viewport_height_pixels = 1100.25;
    (void)fixture.harness.frame();
    const auto press = draw_right_event(true, 300, 300);
    (void)fixture.harness.frame(std::span{&press, 1});
    const auto small_motion = motion_event(302.5f, 300);
    const auto under_threshold = fixture.harness.frame(std::span{&small_motion, 1});
    expect_draw(find_compose_input(std::vector<workspace_action_snapshot_t>{under_threshold},
                                   WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == nullptr);
    const auto release = draw_right_event(false, 302.5f, 300);
    const auto clicked = fixture.harness.frame(std::span{&release, 1});
    expect_draw(clicked.get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
}

TEST_CASE("Draw rejects Timing focus and cancels a stroke on focus, tab, or occlusion changes",
          "[frontend-state][event-harness][draw]") {
    {
    nuklear_event_harness_t timing{1280, 900};
    REQUIRE(timing.ready());
    timing.view().compose.timing_focus = true;
    (void)timing.frame();
    hover_at(timing, 163, 120);
    INFO(timing.drawn_text());
    expect_draw(timing.drawn_text().find("Draw unavailable:") != std::string::npos);
    const auto timed_click = click_at(timing, 300, 400);
    expect_draw(find_compose_input(timed_click, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == nullptr);

    }
    const auto cancel_capture = [](int transition) {
        INFO("Transition " << transition);
        nuklear_event_harness_t harness;
        REQUIRE(harness.ready());
        auto& workspace = harness.view();
        workspace.compose.note_paint_mode_active = true;
        (void)harness.frame();
        const auto down = button_event(true, 300, 400);
        const auto started = harness.frame(std::span{&down, 1});
        expect_draw(has_draw_input(std::vector<workspace_action_snapshot_t>{started},
                                   WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 1));
        if (transition == 0) {
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            lost.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            (void)harness.frame(std::span{&lost, 1});
        } else if (transition == 1) {
            workspace.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
            workspace.compose.visible = false;
            workspace.metadata.visible = true;
            (void)harness.frame();
        } else {
            workspace.compose.input_occluded = true;
            (void)harness.frame();
        }
        expect_draw(!worm_ui_nuklear_compose_state().note_paint_pointer_active);
        expect_draw(!worm_ui_nuklear_compose_state().draw_erase_pointer_active);
        const auto up = button_event(false, 300, 400);
        const auto finished = harness.frame(std::span{&up, 1});
        expect_draw(find_compose_input(std::vector<workspace_action_snapshot_t>{finished},
                                       WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) == nullptr);
    };
    for (int transition = 0; transition != 3; ++transition) cancel_capture(transition);
}

TEST_CASE("Batched right press motion and release erases from the original press without opening a menu",
          "[frontend-state][event-harness][draw]") {
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.note_paint_mode_active = true;
    view.viewport_width_pixels = 1600.5;
    view.viewport_height_pixels = 1100.25;
    (void)fixture.harness.frame();
    const std::array events{draw_right_event(true, 300, 300), motion_event(304, 340),
        draw_right_event(false, 304, 340)};
    const auto frame = fixture.harness.frame(events);
    const auto& action = frame.get();
    const std::vector frames{frame};
    expect_draw(action.compose_action != WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    expect_draw(has_draw_input(frames,
        WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, 2));
    expect_draw(has_draw_input(frames,
        WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, 2));
    const auto* begin = find_compose_input(frames,
        WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin);
    const auto begin_y = begin->y_pixels;
    const auto release = std::ranges::find(std::span(action.compose_inputs, action.compose_input_count),
        WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, &WormUiComposeInputAction::kind);
    REQUIRE(release != std::span(action.compose_inputs, action.compose_input_count).end());
    expect_draw(begin_y < release->y_pixels);
    expect_draw(!compose_note_menu_open());
}

namespace {

auto route_harness_shortcut(const WormUiWorkspaceAction& action,
                            const wormedit::command_registry_t& registry)
    -> wormedit::command_dispatch_result_t {
    if (action.shortcut_event_count != 1) {
        return {};
    }
    const auto& source = action.shortcut_events[0];
    auto focus = wormedit::command_focus_t{
        .text_entry_active = action.shortcut_text_entry_active,
        .ime_composition_active = action.shortcut_ime_composition_active,
        .workspace_active = action.shortcut_window_focused,
        .active_tab = wormedit::workspace_tab_t::compose,
        .visible_tabs = {wormedit::workspace_tab_t::compose},
    };
    return wormedit::dispatch_shortcut(
        registry,
        {.chord = {.key = static_cast<wormedit::shortcut_key_t>(source.hid_usage),
                   .modifiers = source.modifiers},
         .auto_repeat = source.repeat},
        focus, wormedit::shortcut_primary_modifier_t::control);
}

auto harness_tool_chord(const WormUiWorkspaceAction& action, SDL_Scancode scancode) -> bool {
    return action.shortcut_event_count == 1 &&
           action.shortcut_events[0].hid_usage == static_cast<std::uint16_t>(scancode) &&
           action.shortcut_events[0].modifiers == WORM_UI_SHORTCUT_MODIFIER_ALT &&
           !action.shortcut_events[0].repeat && action.shortcut_window_focused &&
           !action.shortcut_text_entry_active && !action.shortcut_ime_composition_active;
}

auto tool_route_matches(const WormUiWorkspaceAction& action, SDL_Scancode scancode,
                        std::string_view expected,
                        const wormedit::command_registry_t& registry) -> bool {
    const auto dispatched = route_harness_shortcut(action, registry);
    return harness_tool_chord(action, scancode) &&
           dispatched.status == wormedit::command_dispatch_status_t::invoked &&
           dispatched.command == wormedit::command_id_t{std::string(expected)} &&
           action.compose_action == WORM_UI_COMPOSE_ACTION_NONE &&
           action.compose_input_count == 0;
}

} // namespace

TEST_CASE("Compose Alt tool shortcuts route through the ready command registry",
          "[frontend-state][event-harness][command-registry][tools]") {
    auto registry = wormedit::make_default_editor_command_registry(
        [](const wormedit::command_invocation_context_t&) {
            return wormedit::command_invocation_result_t{.accepted = true};
        });
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    (void)harness.frame();

    constexpr std::array routes{
        std::tuple{SDL_SCANCODE_1, SDLK_1, "edit.tool_select"},
        std::tuple{SDL_SCANCODE_2, SDLK_2, "edit.tool_draw"},
        std::tuple{SDL_SCANCODE_3, SDLK_3, "edit.tool_layer"},
    };
    for (const auto& [scan, key, expected] : routes) {
        const auto press = key_event(SDL_EVENT_KEY_DOWN, key, scan);
        const auto action = harness.frame(std::span{&press, 1}, SDL_KMOD_LALT);
        expect_draw(tool_route_matches(action.get(), scan, expected, registry));
    }
}

TEST_CASE("Unavailable Draw tool readiness rejects the routed shortcut",
          "[frontend-state][event-harness][command-registry][tools]") {
    auto registry = wormedit::make_default_editor_command_registry(
        [](const wormedit::command_invocation_context_t&) {
            return wormedit::command_invocation_result_t{.accepted = true};
        });
    REQUIRE(wormedit::update_command_state(registry, {"edit.tool_draw"}, false,
                                            std::nullopt).applied);
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    (void)harness.frame();
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    const auto action = harness.frame(std::span{&press, 1}, SDL_KMOD_LALT);
    const auto dispatched = route_harness_shortcut(action.get(), registry);
    expect_draw(harness_tool_chord(action.get(), SDL_SCANCODE_2) &&
          dispatched.status == wormedit::command_dispatch_status_t::disabled &&
          dispatched.command == wormedit::command_id_t{"edit.tool_draw"});
}

namespace {
auto click_quantize_label(nuklear_event_harness_t& harness, std::string_view label)
    -> workspace_action_snapshot_t {
    (void)harness.frame();
    const auto center = harness.drawn_text_center(label);
    REQUIRE(center.has_value());
    const auto frames = click_at(harness, center->first, center->second);
    for (const auto& frame : frames)
        if (frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE)
            return frame;
    return frames.back();
}

void seed_quantize_controls(nuklear_event_harness_t& harness) {
    auto& view = harness.view().compose;
    view.project_id = 419;
    view.chart_id = 419;
    view.document_revision = 7;
    view.selected_note_count = 3;
    view.quantize_divisions = 4;
    view.quantize_preview_moved = 2;
    view.quantize_preview_halfway_notes = 2;
    view.quantize_preview_holds = 1;
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = true;
    state.sounds_panel_visible = state.layers_panel_visible = false;
}

void check_quantize_settings(const WormUiWorkspaceAction& action, std::uint64_t grid,
                             std::uint8_t space, std::uint8_t tie) {
    CHECK(action.compose_action == WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS);
    CHECK(action.compose_quantize_divisions == grid);
    CHECK(action.compose_quantize_space == space);
    CHECK(action.compose_quantize_tie == tie);
    CHECK(action.compose_input_chart_id == 419);
    CHECK(action.compose_input_document_revision == 7);
}

auto quantize_popup_field_has_text(const nuklear_event_harness_t& harness,
                                   std::pair<float, float> heading, std::string_view text) -> bool {
    for (const auto& item : harness.drawn_text_bounds()) {
        if (std::string_view(item.text) == text && item.y > heading.second + 12 &&
            item.y < heading.second + 40 && item.x > heading.first - 145 &&
            item.x < heading.first + 145)
            return true;
    }
    return false;
}

void replace_quantize_popup_field(nuklear_event_harness_t& harness, std::string_view heading,
                                  std::string_view text, std::string_view current = {}) {
    (void)harness.frame();
    const auto title = harness.drawn_text_center(heading);
    REQUIRE(title.has_value());
    if (!current.empty())
        REQUIRE(quantize_popup_field_has_text(harness, *title, current));
    (void)click_at(harness, title->first, title->second + 27);
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_SCANCODE_A);
    (void)harness.frame(std::span{&press, 1}, SDL_KMOD_CTRL);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_A, SDL_SCANCODE_A);
    (void)harness.frame(std::span{&release, 1}, SDL_KMOD_CTRL);
    for (const char character : text) {
        const char value[]{character, '\0'};
        SDL_Event event{};
        event.type = event.text.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = value;
        (void)harness.frame(std::span{&event, 1});
    }
    INFO(harness.drawn_text());
    REQUIRE(quantize_popup_field_has_text(harness, *title, text));
}

void check_quantize_input_blocked(const std::vector<workspace_action_snapshot_t>& frames) {
    for (const auto& frame : frames) {
        CHECK(frame.get().compose_input_count == 0);
        CHECK(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    }
}

void open_quantize_preset_menu(nuklear_event_harness_t& harness) {
    (void)harness.frame();
    const auto chip = harness.drawn_text_center("Default");
    REQUIRE(chip.has_value());
    (void)right_click_at(harness, chip->first, chip->second);
    (void)harness.frame();
    REQUIRE(compose_quantize_owns_input());
    REQUIRE(harness.drawn_text_center("Rename").has_value());
}

void seed_quantize_presets(nuklear_event_harness_t& harness) {
    seed_quantize_controls(harness);
    static const std::array<WormUiComposeQuantizePresetView, 2> presets{
        {{.name = "Default", .divisions = 16}, {.name = "Trills", .divisions = 24}}};
    auto& view = harness.view().compose;
    view.quantize_presets = presets.data();
    view.quantize_preset_count = presets.size();
}

void check_quantize_preset_action(const WormUiWorkspaceAction& action, WormUiComposeActionKind kind,
                                  const char* name = nullptr) {
    CHECK(action.compose_action == kind);
    CHECK(action.compose_quantize_preset_index == 0);
    if (name)
        CHECK(std::string_view(action.compose_quantize_preset_name) == name);
}

void wheel_compose_inspector(nuklear_event_harness_t& harness, float x, float y,
                             float direction) {
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.mouse_x = static_cast<int>(x);
    wheel.wheel.mouse_y = static_cast<int>(y);
    wheel.wheel.y = direction;
    const std::array events{motion_event(x, y), wheel};
    (void)harness.frame(events);
}

auto scroll_compose_inspector_to(nuklear_event_harness_t& harness, std::string_view label,
                                 float x, float y, float direction, int limit) -> bool {
    for (int tick = 0; tick < limit; ++tick) {
        if (harness.drawn_text_center(label)) return true;
        wheel_compose_inspector(harness, x, y, direction);
    }
    return harness.drawn_text_center(label).has_value();
}

void check_quantize_ghost_initial_state(nuklear_event_harness_t& harness) {
    const auto grid = click_quantize_label(harness, "1/16");
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    const auto amber_draw = compose_quantize_ghost_draw_state_for_testing();
    expect_draw(amber_draw.emitted_segments > 40 && amber_draw.red == 255 &&
                amber_draw.green == 190 && amber_draw.blue == 56 && amber_draw.alpha == 190);
    check_quantize_settings(grid.get(), 16, 0, 0);
}

void check_quantize_ghost_visibility(nuklear_event_harness_t& harness,
                                    WormUiComposeView& view) {
    auto& inspector_state = worm_ui_nuklear_compose_state();
    inspector_state.inspector_visible = false;
    (void)harness.frame();
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    inspector_state.inspector_visible = true;
    (void)harness.frame();
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
    view.visible = false;
    (void)harness.frame();
    expect_draw(!compose_quantize_ghosts_drawn_for_testing());
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
    view.visible = true;
    (void)harness.frame();
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
}

void check_quantize_ghost_scroll_clipping(nuklear_event_harness_t& harness) {
    const auto quantize_label = harness.drawn_text_center("1/16");
    expect_draw(quantize_label.has_value());
    REQUIRE(scroll_compose_inspector_to(harness, "Transform", quantize_label->first,
                                        quantize_label->second, -1.0f, 80));
    auto transform = harness.drawn_text_center("Transform");
    REQUIRE(transform.has_value());
    (void)click_at(harness, transform->first, transform->second);
    REQUIRE(scroll_compose_inspector_to(harness, "Measure at cursor", quantize_label->first,
                                        quantize_label->second, -1.0f, 80));
    const auto measure = harness.drawn_text_center("Measure at cursor");
    REQUIRE(measure.has_value());
    (void)click_at(harness, measure->first, measure->second);
    REQUIRE(scroll_compose_inspector_to(harness, "1/16", quantize_label->first,
                                        quantize_label->second, 1.0f, 120));
    for (int tick = 0; tick < 8; ++tick)
        wheel_compose_inspector(harness, quantize_label->first, quantize_label->second, -1.0f);
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    harness.view().compose.quantize_preview_enabled = false;
    (void)harness.frame();
    expect_draw(!compose_quantize_ghosts_drawn_for_testing());
    harness.view().compose.quantize_preview_enabled = true;
    (void)harness.frame();
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
}

void check_quantize_ghost_bounds_and_collision(nuklear_event_harness_t& harness,
                                               WormUiComposeQuantizeGhostView& ghost) {
    const auto original_before_y = ghost.before_head_anchor_y;
    const auto original_after_y = ghost.after_head_anchor_y;
    ghost.before_head_anchor_y = -300.0;
    ghost.after_head_anchor_y = 1700.0;
    ghost.before_head.top = -310;
    ghost.before_head.bottom = -294;
    ghost.after_head.top = 1690;
    ghost.after_head.bottom = 1706;
    (void)harness.frame();
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    ghost.before_head_anchor_y = -3000.0;
    ghost.after_head_anchor_y = -2800.0;
    ghost.before_head.top = -3010;
    ghost.before_head.bottom = -2994;
    ghost.after_head.top = -2810;
    ghost.after_head.bottom = -2794;
    (void)harness.frame();
    expect_draw(!compose_quantize_ghosts_drawn_for_testing());
    ghost.before_head_anchor_y = original_before_y;
    ghost.after_head_anchor_y = original_after_y;
    ghost.before_head = {100, 100, 500, 116};
    ghost.after_head = {600, 130, 1000, 146};
    ghost.collision = true;
    (void)harness.frame();
    const auto red_draw = compose_quantize_ghost_draw_state_for_testing();
    const auto conflict = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict);
    expect_draw(red_draw.emitted_segments > 0 && red_draw.red == conflict.r &&
                red_draw.green == conflict.g && red_draw.blue == conflict.b && red_draw.alpha == 153);
    ghost.collision = false;
}

void check_quantize_ghost_overlay(nuklear_event_harness_t& harness,
                                  WormUiComposeView& view,
                                  WormUiComposeQuantizeGhostView& ghost) {
    check_quantize_ghost_initial_state(harness);
    check_quantize_ghost_visibility(harness, view);
    check_quantize_ghost_scroll_clipping(harness);
    check_quantize_ghost_bounds_and_collision(harness, ghost);
}
} // namespace

TEST_CASE("Quantize sets 1/16 Authored and Later in three clicks then applies",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    REQUIRE(harness.ready());
    seed_quantize_controls(harness);
    auto& view = harness.view().compose;
    static WormUiComposeQuantizeGhostView ghosts[]{{
        .object_id = 77, .lane_id = 1,
        .before_head_glyph = static_cast<std::uint32_t>(wormedit::lane_glyph_t::arrow_up),
        .after_head_glyph = static_cast<std::uint32_t>(wormedit::lane_glyph_t::arrow_up),
        .before_head = {100, 100, 500, 116}, .after_head = {600, 130, 1000, 146},
        .before_head_anchor_x = 115, .before_head_anchor_y = 108,
        .after_head_anchor_x = 175, .after_head_anchor_y = 138,
        .moved_head = true, .head_halfway = true}};
    view.scene_available = true;
    view.viewport_width_pixels = 1800;
    view.viewport_height_pixels = 1400;
    view.quantize_ghosts = ghosts;
    view.quantize_ghost_count = 1;
    view.quantize_ghosts_drawable = true;
    view.quantize_preview_enabled = true;
    const auto grid = click_quantize_label(harness, "1/16");
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    check_quantize_settings(grid.get(), 16, 0, 0);
    view.quantize_divisions = 16;
    const auto space = click_quantize_label(harness, "Authored");
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    check_quantize_settings(space.get(), 16, 1, 0);
    view.quantize_space = 1;
    const auto later = click_quantize_label(harness, "Later");
    expect_draw(compose_quantize_ghosts_drawn_for_testing());
    check_quantize_settings(later.get(), 16, 1, 1);
    view.quantize_tie = 1;
    const auto applied = click_quantize_label(harness, "Quantize");
    CHECK(applied.get().compose_action == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE);
    CHECK(applied.get().compose_quantize_divisions == 16);
    CHECK(applied.get().compose_quantize_space == 1);
    CHECK(applied.get().compose_quantize_tie == 1);
    CHECK(harness.drawn_text().find("Presets and settings") == std::string::npos);
}

TEST_CASE("Quantize ghost overlay follows visible section, scroll clipping and collision color",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 500};
    REQUIRE(harness.ready());
    seed_quantize_controls(harness);
    auto& view = harness.view().compose;
    static WormUiComposeQuantizeGhostView ghost{
        .object_id = 77, .lane_id = 1,
        .before_head_glyph = static_cast<std::uint32_t>(wormedit::lane_glyph_t::arrow_up),
        .after_head_glyph = static_cast<std::uint32_t>(wormedit::lane_glyph_t::arrow_up),
        .before_head = {100, 100, 500, 116}, .after_head = {600, 130, 1000, 146},
        .before_head_anchor_x = 300, .before_head_anchor_y = 108,
        .after_head_anchor_x = 800, .after_head_anchor_y = 138,
        .moved_head = true, .head_halfway = true};
    view.scene_available = true;
    view.viewport_width_pixels = 1800;
    view.viewport_height_pixels = 600;
    view.quantize_ghosts = &ghost;
    view.quantize_ghost_count = 1;
    view.quantize_ghosts_drawable = true;
    view.quantize_preview_enabled = true;
    auto& timing = harness.view().timing;
    timing.stage_active = true;
    timing.stage_lane = WORM_UI_TIMING_LANE_BPM;
    timing.stage_marker_y_pixels = 1.0;
    timing.stage_value = 120.0;
    timing.stage_pause_unit = WORM_UI_TIMING_PAUSE_SECONDS;
    (void)harness.frame();
    check_quantize_ghost_overlay(harness, view, ghost);
}

TEST_CASE("Quantize headings retain counts and only advertise an actual Q binding",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_controls(harness);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Halfway objects · 2 in selection") != std::string::npos);
    CHECK(harness.drawn_text().find("Hold tails · 1 hold in selection") != std::string::npos);
    CHECK(harness.drawn_text().find("Quantize (Q)") == std::string::npos);
    harness.view().compose.quantize_q_bound = true;
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Quantize (Q)") != std::string::npos);
    harness.view().compose.quantize_preview_blocked = 1;
    const auto applied = click_quantize_label(harness, "Quantize (Q)");
    CHECK(applied.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Quantize custom grid rejects zero and overflow then applies corrected input",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_controls(harness);
    (void)click_quantize_label(harness, "custom…");
    replace_quantize_popup_field(harness, "Grid divisions", "0", "4");
    const auto invalid = click_quantize_label(harness, "Apply custom grid");
    INFO(harness.drawn_text());
    CHECK(invalid.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(compose_quantize_owns_input());
    CHECK(harness.drawn_text().find("Grid must be a positive whole number") != std::string::npos);
    replace_quantize_popup_field(harness, "Grid divisions", "18446744073709551616", "0");
    CHECK(click_quantize_label(harness, "Apply custom grid").get().compose_action ==
          WORM_UI_COMPOSE_ACTION_NONE);
    replace_quantize_popup_field(harness, "Grid divisions", "12", "18446744073709551616");
    const auto applied = click_quantize_label(harness, "Apply custom grid");
    check_quantize_settings(applied.get(), 12, 0, 0);
}

TEST_CASE("Quantize preset chips select and expose Rename and Delete on right click",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    const auto selected = click_quantize_label(harness, "Default");
    check_quantize_preset_action(selected.get(), WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET);
    open_quantize_preset_menu(harness);
    (void)click_quantize_label(harness, "Rename");
    replace_quantize_popup_field(harness, "Rename preset", "Renamed", "Default");
    const auto renamed = click_quantize_label(harness, "Rename");
    check_quantize_preset_action(renamed.get(), WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET,
                                 "Renamed");
    (void)harness.frame();
    open_quantize_preset_menu(harness);
    const auto deleted = click_quantize_label(harness, "Delete");
    check_quantize_preset_action(deleted.get(), WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET);
}

TEST_CASE("Quantize plus chip saves a unique named preset with unchanged settings payload",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    (void)click_quantize_label(harness, "+");
    replace_quantize_popup_field(harness, "Preset name", "Default");
    CHECK(click_quantize_label(harness, "Save preset").get().compose_action ==
          WORM_UI_COMPOSE_ACTION_NONE);
    replace_quantize_popup_field(harness, "Preset name", "New preset", "Default");
    const auto saved = click_quantize_label(harness, "Save preset");
    CHECK(saved.get().compose_action == WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET);
    CHECK(std::string_view(saved.get().compose_quantize_preset_name) == "New preset");
    CHECK(saved.get().compose_quantize_divisions == 4);
    CHECK(saved.get().compose_quantize_space == 0);
    CHECK(saved.get().compose_quantize_tie == 0);
}

TEST_CASE("Quantize popups consume outside dismissal releases and global shortcuts",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    open_quantize_preset_menu(harness);
    const auto key = key_event(SDL_EVENT_KEY_DOWN, SDLK_Z, SDL_SCANCODE_Z);
    const auto keyboard = harness.frame(std::span{&key, 1}, SDL_KMOD_CTRL);
    CHECK(keyboard.get().shortcut_text_entry_active);
    CHECK(keyboard.get().compose_input_count == 0);
    check_quantize_input_blocked(click_at(harness, 500, 500));
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
    open_quantize_preset_menu(harness);
    const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    CHECK(harness.frame(std::span{&escape, 1}).get().compose_input_count == 0);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    CHECK(harness.frame(std::span{&release, 1}).get().compose_input_count == 0);
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
}

TEST_CASE("Quantize popups reject stale revision chart and preset identity",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    open_quantize_preset_menu(harness);
    auto& view = harness.view().compose;
    std::array<WormUiComposeQuantizePresetView, 2> changed{view.quantize_presets[0],
                                                           view.quantize_presets[1]};
    SECTION("revision") {
        ++view.document_revision;
    }
    SECTION("chart") {
        ++view.chart_id;
    }
    SECTION("preset identity without revision") {
        view.quantize_preset_count = 0;
    }
    SECTION("preset settings without revision") {
        ++changed[0].divisions;
        view.quantize_presets = changed.data();
    }
    CHECK(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(harness.drawn_text_center("Rename") == std::nullopt);
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
}

TEST_CASE("Quantize popups close for a hidden inspector tab switch and lost focus",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    open_quantize_preset_menu(harness);
    SECTION("hidden inspector") {
        worm_ui_nuklear_compose_state().inspector_visible = false;
    }
    SECTION("tab switch") {
        harness.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        harness.view().compose.visible = false;
    }
    SECTION("lost focus") {
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)harness.frame(std::span{&event, 1});
    }
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
}

TEST_CASE("Frontend reset clears quantize popup state even when project IDs repeat",
          "[frontend-state][event-harness][quantize]") {
    {
        nuklear_event_harness_t harness{1800, 1400};
        seed_quantize_presets(harness);
        open_quantize_preset_menu(harness);
    }
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
    CHECK(harness.drawn_text_center("Rename") == std::nullopt);
}

TEST_CASE("Cancelling an invalid custom quantize draft restores current settings Apply",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_controls(harness);
    (void)click_quantize_label(harness, "custom…");
    replace_quantize_popup_field(harness, "Grid divisions", "0", "4");
    REQUIRE(click_quantize_label(harness, "Apply custom grid").get().compose_action ==
            WORM_UI_COMPOSE_ACTION_NONE);
    REQUIRE(harness.drawn_text().find("Grid must be a positive whole number") != std::string::npos);
    SECTION("outside dismissal") {
        check_quantize_input_blocked(click_at(harness, 500, 500));
    }
    SECTION("Escape dismissal") {
        const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        (void)harness.frame(std::span{&press, 1});
        const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        (void)harness.frame(std::span{&release, 1});
    }
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
    const auto applied = click_quantize_label(harness, "Quantize");
    CHECK(applied.get().compose_action == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE);
    CHECK(applied.get().compose_quantize_divisions == 4);
}

TEST_CASE("Quantize popup owns right clicks over underlying preset chips through dismissal",
          "[frontend-state][event-harness][quantize]") {
    nuklear_event_harness_t harness{1800, 1400};
    seed_quantize_presets(harness);
    (void)harness.frame();
    const auto preset = harness.drawn_text_center("Default");
    REQUIRE(preset.has_value());
    (void)click_quantize_label(harness, "custom…");
    check_quantize_input_blocked(right_click_at(harness, preset->first, preset->second));
    CHECK(harness.drawn_text_center("Rename") == std::nullopt);
    (void)harness.frame();
    CHECK_FALSE(compose_quantize_owns_input());
    open_quantize_preset_menu(harness);
}

namespace {
auto move_event_input(const workspace_action_snapshot_t& frame, WormUiComposeInputKind kind)
    -> const WormUiComposeInputAction& {
    const auto& action = frame.get();
    for (std::size_t index = 0; index < action.compose_input_count; ++index)
        if (action.compose_inputs[index].kind == kind)
            return action.compose_inputs[index];
    FAIL("Expected move input kind " << static_cast<int>(kind));
    return action.compose_inputs[0];
}
struct move_event_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<WormUiComposeNoteView, 3> notes{};
    std::array<WormUiComposeHoldView, 1> holds{};
    explicit move_event_fixture_t(bool hide_panels = false) {
        if (hide_panels) {
            auto& state = worm_ui_nuklear_compose_state();
            state.inspector_visible = state.sounds_panel_visible = state.layers_panel_visible = false;
            state.bga_panel_visible = false;
        }
        expect_lane_key(harness.ready());
        const std::array motion{motion_event(300, 400)};
        const auto hover = harness.frame(motion);
        expect_lane_key(hover.get().compose_has_pointer_hover);
        const auto x = hover.get().compose_pointer_hover_x_pixels;
        const auto y = hover.get().compose_pointer_hover_y_pixels;
        notes[0] = {.bounds = {x - 15, y - 5, x + 15, y + 5},
                    .note_id = 101,
                    .lane_id = 1,
                    .kind = 1,
                    .selected = true};
        auto& view = harness.view().compose;
        view.note_endpoints = notes.data();
        view.note_endpoint_count = 1;
        view.selected_note_count = 1;
        (void)harness.frame();
    }
    auto start() -> workspace_action_snapshot_t {
        const std::array down{motion_event(300, 400), button_event(true, 300, 400)};
        (void)harness.frame(down);
        return move(420);
    }
    auto move(float y) -> workspace_action_snapshot_t {
        const std::array motion{motion_event(300, y)};
        return harness.frame(motion);
    }
    auto release(float y) -> workspace_action_snapshot_t {
        const std::array up{motion_event(300, y), button_event(false, 300, y)};
        return harness.frame(up);
    }
    void project(const workspace_action_snapshot_t& frame, bool conflict) {
        const auto& input = move_event_input(frame, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE);
        notes[0].proposal_origin = true;
        notes[1] = notes[0];
        notes[1].proposal_origin = false;
        notes[1].proposal_ghost = true;
        notes[1].bounds = {input.x_pixels - 15, input.y_pixels - 5, input.x_pixels + 15,
                           input.y_pixels + 5};
        notes[1].move_conflict = conflict ? 1 : 0;
        notes[2] = {.bounds = notes[1].bounds,
                    .note_id = 201,
                    .lane_id = 1,
                    .move_conflict_occupant = conflict};
        holds[0] = {
            .bounds = {input.x_pixels - 5, input.y_pixels - 45, input.x_pixels + 5, input.y_pixels},
            .note_id = 101,
            .lane_id = 1,
            .proposal_ghost = true,
            .move_conflict = static_cast<uint8_t>(conflict ? 1 : 0)};
        auto& view = harness.view().compose;
        view.note_endpoint_count = notes.size();
        view.hold_bodies = holds.data();
        view.hold_body_count = holds.size();
        view.move_conflict_count = conflict ? 1 : 0;
        (void)harness.frame();
    }
};
} // namespace

TEST_CASE("Native note drag continues through red hold ghosts and recovers before release",
          "[frontend-state][event-harness][move][t420]") {
    move_event_fixture_t fixture;
    const auto begun = fixture.start();
    (void)move_event_input(begun, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN);
    const auto first_y = move_event_input(begun, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE).y_pixels;
    fixture.project(begun, true);
    expect_lane_key(fixture.harness.drawn_text().find("1 notes would overlap") !=
                    std::string::npos);
    const auto through = fixture.move(440);
    expect_lane_key(move_event_input(through, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE).y_pixels !=
                    first_y);
    fixture.project(through, true);
    expect_lane_key(worm_ui_nuklear_compose_state().note_move_active);
    const auto recovered = fixture.move(460);
    fixture.project(recovered, false);
    expect_lane_key(fixture.harness.drawn_text().find("notes would overlap") == std::string::npos);
    const auto released = fixture.release(460);
    const auto& final = move_event_input(released, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE);
    expect_lane_key(final.y_pixels ==
                    move_event_input(recovered, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE).y_pixels);
    expect_lane_key(!worm_ui_nuklear_compose_state().note_move_active);
}

TEST_CASE("Native note drag releases while red without substituting a prior valid target",
          "[frontend-state][event-harness][move][t420]") {
    move_event_fixture_t fixture;
    const auto begun = fixture.start();
    fixture.project(begun, true);
    const auto moved = fixture.move(440);
    fixture.project(moved, true);
    const auto released = fixture.release(440);
    const auto& final = move_event_input(released, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE);
    expect_lane_key(final.y_pixels ==
                    move_event_input(moved, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE).y_pixels);
    expect_lane_key(!worm_ui_nuklear_compose_state().note_move_active);
}

namespace {

struct bgm_draw_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<WormUiComposeBgmLaneView, 2> lanes{};
    float left{}, top{}, lane_x{};

    explicit bgm_draw_fixture_t(bool bmson = false, bool stem = false) {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.inspector_visible = state.sounds_panel_visible = state.layers_panel_visible = false;
        state.bga_panel_visible = false;
        auto& view = harness.view().compose;
        view.scene_available = true;
        view.note_paint_mode_active = true;
        view.bmson_tools_available = bmson;
        (void)harness.frame();
        const auto viewport = compose_canvas_viewport_for_testing();
        REQUIRE(viewport.has_value());
        left = viewport->x;
        top = viewport->y;
        view.viewport_width_pixels = viewport->width;
        view.viewport_height_pixels = viewport->height;
        lane_x = viewport->width * 0.6f;
        lanes[0] = {.bounds = {lane_x, 0, lane_x + 40, viewport->height}, .source_lane_id = 71,
                    .stem = stem};
        lanes[1] = {.bounds = {lane_x + 44, 0, lane_x + 84, viewport->height},
                    .source_lane_id = 72};
        view.bgm_lanes = lanes.data();
        view.bgm_lane_count = lanes.size();
        view.draw_source_kind = state.draw_source_kind = 1;
        view.draw_source_id = state.draw_source_id = 40;
        state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        (void)harness.frame();
    }
    [[nodiscard]] auto point(float dx, float y) const -> std::pair<float, float> {
        return {left + lane_x + dx, top + y};
    }
};

auto count_inputs(const std::vector<workspace_action_snapshot_t>& frames,
                  WormUiComposeInputKind kind) -> std::size_t {
    std::size_t count{};
    for (const auto& frame : frames)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index)
            count += frame.get().compose_inputs[index].kind == kind;
    return count;
}

auto bgm_draw_drag(bgm_draw_fixture_t& f, float start_dx, float end_dx)
    -> std::vector<workspace_action_snapshot_t> {
    const auto from = f.point(start_dx, 100);
    const auto mid = f.point(start_dx, 140);
    const auto to = f.point(end_dx, 180);
    std::array<event_step_t, 3> steps{};
    steps[0].events = {motion_event(from.first, from.second), button_event(true, from.first, from.second)};
    steps[1].events = {motion_event(mid.first, mid.second)};
    steps[2].events = {motion_event(to.first, to.second), button_event(false, to.first, to.second)};
    return f.harness.run_script(steps);
}

} // namespace

TEST_CASE("Draw with a sound drags one BGM stroke tagged with the pressed lane",
          "[frontend-state][event-harness][draw][bgm-draw]") {
    bgm_draw_fixture_t f;
    const auto frames = bgm_draw_drag(f, 20, 60);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == 1);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) == 1);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE) >= 1);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN) == 0);
    for (const auto& frame : frames)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index) {
            const auto& input = frame.get().compose_inputs[index];
            if (input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN &&
                input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE &&
                input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) continue;
            expect_draw(input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM);
            expect_draw(input.lane_id == 71 && input.resource_id == 40 && input.paint_intent == 1);
        }
}

TEST_CASE("Draw right-drag on a BGM lane erases that lane in one stroke",
          "[frontend-state][event-harness][draw][bgm-draw]") {
    bgm_draw_fixture_t f;
    const auto from = f.point(60, 100);
    const auto to = f.point(20, 180);
    const auto frames = right_draw_script(f.harness, from.first, from.second, to.first, to.second);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == 1);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) == 1);
    for (const auto& frame : frames)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index) {
            const auto& input = frame.get().compose_inputs[index];
            if (input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN &&
                input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE &&
                input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) continue;
            expect_draw(input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM);
            expect_draw(input.lane_id == 72 && input.resource_id == 0 && input.paint_intent == 2);
        }
}

TEST_CASE("Draw without a sound source does nothing on a BGM lane",
          "[frontend-state][event-harness][draw][bgm-draw]") {
    bgm_draw_fixture_t f;
    auto& state = worm_ui_nuklear_compose_state();
    state.draw_source_kind = 0;
    state.draw_source_id = 0;
    f.harness.view().compose.draw_source_kind = 0;
    f.harness.view().compose.draw_source_id = 0;
    (void)f.harness.frame();
    const auto frames = bgm_draw_drag(f, 20, 60);
    for (const auto kind : {WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE,
                            WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                            WORM_UI_COMPOSE_INPUT_POINTER_CLICK, WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE})
        expect_draw(count_inputs(frames, kind) == 0);
}

TEST_CASE("Draw on a BMSON stem lane never starts a BGM stroke",
          "[frontend-state][event-harness][draw][bgm-draw][bmson-stem]") {
    bgm_draw_fixture_t f{true, true};
    const auto frames = bgm_draw_drag(f, 20, 20);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == 0);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE) == 0);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) == 0);
}

TEST_CASE("Draw on a BMSON packed column drags one BGM stroke and right-drag erases it",
          "[frontend-state][event-harness][draw][bgm-draw][bmson-stem]") {
    bgm_draw_fixture_t f{true, true};
    const auto frames = bgm_draw_drag(f, 60, 60);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == 1);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) == 1);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW) == 0);
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE) == 0);
    for (const auto& frame : frames)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index) {
            const auto& input = frame.get().compose_inputs[index];
            if (input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN &&
                input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE &&
                input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) continue;
            expect_draw(input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM);
            expect_draw(input.lane_id == 72 && input.resource_id == 40 && input.paint_intent == 1);
        }
    const auto from = f.point(60, 100);
    const auto to = f.point(60, 180);
    const auto erase = right_draw_script(f.harness, from.first, from.second, to.first, to.second);
    expect_draw(count_inputs(erase, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) == 1);
    const auto* begin = find_compose_input(erase, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    expect_draw(begin && begin->paint_intent == 2 && begin->lane_id == 72 &&
                begin->resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM);
}

TEST_CASE("Draw on a BMSON packed column with no usable sound does nothing",
          "[frontend-state][event-harness][draw][bgm-draw][bmson-stem]") {
    bgm_draw_fixture_t f{true, true};
    f.harness.view().compose.draw_source_is_stem = true;
    f.harness.view().compose.draw_source_stem_lane_id = 71;
    (void)f.harness.frame();
    const auto stem_source = bgm_draw_drag(f, 60, 60);
    f.harness.view().compose.draw_source_is_stem = false;
    f.harness.view().compose.draw_source_stem_lane_id = 0;
    worm_ui_nuklear_compose_state().draw_source_kind = 0;
    f.harness.view().compose.draw_source_kind = 0;
    (void)f.harness.frame();
    const auto silent = bgm_draw_drag(f, 60, 60);
    for (const auto* frames : {&stem_source, &silent})
        for (const auto kind : {WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE,
                                WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW,
                                WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN})
            expect_draw(count_inputs(*frames, kind) == 0);
}

namespace {

struct stem_edge_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    WormUiComposeBgmLaneView lane{};
    float left{}, top{}, edge_x{};

    stem_edge_fixture_t() {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.inspector_visible = state.sounds_panel_visible = state.layers_panel_visible = false;
        state.bga_panel_visible = false;
        state.draw_source_kind = 0;
        state.draw_source_id = 0;
        harness.view().project_id = 123;
        auto& view = harness.view().compose;
        view.visible = true;
        view.scene_available = true;
        view.bmson_tools_available = true;
        view.project_id = 123;
        view.chart_id = 7;
        view.document_revision = 11;
        (void)harness.frame();
        const auto viewport = compose_canvas_viewport_for_testing();
        REQUIRE(viewport.has_value());
        left = viewport->x;
        top = viewport->y;
        view.viewport_width_pixels = viewport->width;
        view.viewport_height_pixels = viewport->height;
        edge_x = viewport->width * 0.6f + 36;
        lane = {.bounds = {edge_x - 36, 0, edge_x, viewport->height}, .source_lane_id = 44,
                .stem = true, .width_pixels = 36, .resize_edge_pixels = edge_x};
        view.bgm_lanes = &lane;
        view.bgm_lane_count = 1;
        (void)harness.frame();
    }
    [[nodiscard]] auto point(float x, float y) const -> std::pair<float, float> {
        return {left + x, top + y};
    }
    [[nodiscard]] auto width() const { return worm_ui_frontend_compose_bgm_lane_width(123, 7, 44); }
    void sync_lane() {
        lane.width_pixels = width();
        lane.bounds.right = lane.bounds.left + lane.width_pixels;
        lane.resize_edge_pixels = lane.bounds.right;
        edge_x = static_cast<float>(lane.resize_edge_pixels);
    }
    void hover(float x, float y) {
        const auto p = point(x, y);
        const auto move = motion_event(p.first, p.second);
        (void)harness.frame(std::span{&move, 1});
    }
    auto press(float x, float y) {
        const auto p = point(x, y);
        const std::array events{motion_event(p.first, p.second), button_event(true, p.first, p.second)};
        return harness.frame(events);
    }
    void drag_to(float x, float y) {
        const auto p = point(x, y);
        const auto move = motion_event(p.first, p.second);
        (void)harness.frame(std::span{&move, 1});
    }
    void release(float x, float y) {
        const auto p = point(x, y);
        const std::array events{motion_event(p.first, p.second), button_event(false, p.first, p.second)};
        (void)harness.frame(events);
        sync_lane();
    }
};

} // namespace

TEST_CASE("Stem lane edge resizes from any height with a hover highlight",
          "[frontend-state][event-harness][bmson-stem][resize]") {
    stem_edge_fixture_t f;
    auto& state = worm_ui_nuklear_compose_state();
    for (const float y : {6.0f, 300.0f, 560.0f}) {
        f.hover(f.edge_x - 40, y);
        expect_draw(state.stem_lane_resize_hover_lane == 0);
        f.hover(f.edge_x, y);
        expect_draw(state.stem_lane_resize_hover_lane == 44 && state.stem_lane_resize_cursor_requested);
        (void)f.press(f.edge_x, y);
        f.drag_to(f.edge_x + 60, y);
        expect_draw(state.stem_lane_resize.has_value() && state.stem_lane_resize_hover_lane == 44);
        expect_draw(std::abs(f.width() - 96.0) < 1.0);
        f.release(f.edge_x + 60, y);
        expect_draw(!state.stem_lane_resize.has_value());
        const auto resized = f.width();
        (void)f.press(f.edge_x, y);
        f.drag_to(f.edge_x - 1000, y);
        expect_draw(f.width() == 36.0);
        f.release(f.edge_x - 1000, y);
        expect_draw(resized > 36.0);
    }
    f.hover(f.edge_x - 100, 300);
    expect_draw(state.stem_lane_resize_hover_lane == 0);
}

TEST_CASE("Stem lane edge band leaves stem point presses inside the lane alone",
          "[frontend-state][event-harness][bmson-stem][resize]") {
    stem_edge_fixture_t f;
    auto& state = worm_ui_nuklear_compose_state();
    f.harness.view().compose.note_paint_mode_active = true;
    (void)f.harness.frame();
    const auto from = f.point(f.edge_x - 12, 300);
    const std::array down{motion_event(from.first, from.second), button_event(true, from.first, from.second)};
    const std::array up{motion_event(from.first, from.second), button_event(false, from.first, from.second)};
    const std::vector<workspace_action_snapshot_t> frames{f.harness.frame(down), f.harness.frame(up)};
    expect_draw(!state.stem_lane_resize.has_value());
    expect_draw(count_inputs(frames, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE) == 1);
    expect_draw(f.width() == 36.0);
}

TEST_CASE("Stem lane edge double click resets the width at any height",
          "[frontend-state][event-harness][bmson-stem][resize]") {
    stem_edge_fixture_t f;
    (void)f.press(f.edge_x, 300);
    f.drag_to(f.edge_x + 80, 300);
    f.release(f.edge_x + 80, 300);
    expect_draw(f.width() > 100.0);
    const auto p = f.point(f.edge_x, 420);
    auto down = button_event(true, p.first, p.second);
    auto up = button_event(false, p.first, p.second);
    down.button.clicks = 2;
    up.button.clicks = 2;
    const std::array events{motion_event(p.first, p.second), down, up};
    (void)f.harness.frame(events);
    expect_draw(f.width() == 36.0);
}

TEST_CASE("Quantize chips outside the chart's snap list explain they are quantize-only",
          "[ui][event-harness][quantize-snap]") {
    nuklear_event_harness_t harness;
    seed_quantize_controls(harness);
    auto& view = harness.view().compose;
    view.supported_snap_denominators[0] = 4;
    view.supported_snap_denominators[1] = 8;
    view.supported_snap_denominator_count = 2;
    (void)harness.frame();
    const auto center = harness.drawn_text_center("1/16");
    REQUIRE(center.has_value());
    hover_at(harness, center->first, center->second);
    check_tooltip_text(harness, "Quantize only");
    const auto supported = harness.drawn_text_center("1/8");
    REQUIRE(supported.has_value());
    hover_at(harness, supported->first, supported->second);
    INFO(harness.drawn_text());
    check_no_tooltip_text(harness, "Quantize only");
}

namespace {
auto t424_song() -> wormedit::song_document_t {
    auto song = t422_song();
    song.charts.front().source_family = wormedit::format_family_t::bms;
    return song;
}

}

TEST_CASE("Backquote shortcuts invoke the registered BGM verbs without legacy inputs",
          "[frontend-state][event-harness][t424]") {
    nuklear_event_harness_t harness;
    std::string invoked;
    const auto registry = wormedit::make_default_editor_command_registry([&](const auto& context) {
        invoked = context.command.value;
        return wormedit::command_invocation_result_t{.accepted = true};
    });
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_GRAVE, SDL_SCANCODE_GRAVE);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_GRAVE, SDL_SCANCODE_GRAVE);
    for (const auto mods : {SDL_KMOD_NONE, SDL_KMOD_SHIFT}) {
        const auto frame = harness.frame(std::span{&press, 1}, mods);
        expect_t422(frame.get().compose_input_count == 0);
        expect_t422(route_harness_shortcut(frame.get(), registry).status == wormedit::command_dispatch_status_t::invoked);
        expect_t422(invoked == (mods == SDL_KMOD_NONE ? "edit.move_to_bgm" : "edit.move_next_to_bgm"));
        (void)harness.frame(std::span{&release, 1});
    }
}

TEST_CASE("Note menu exposes the BGM action with its projected shortcut and disabled gate",
          "[frontend-state][event-harness][t424]") {
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.move_to_bgm_available = true;
    std::snprintf(view.move_to_bgm_shortcut, sizeof(view.move_to_bgm_shortcut), "`");
    (void)right_click_at(fixture.harness, 300, 300);
    const auto center = fixture.harness.drawn_text_center("Move to BGM  `");
    REQUIRE(center);
    const auto clicked = click_at(fixture.harness, center->first, center->second);
    CHECK(std::ranges::any_of(clicked, [](const auto& frame) {
        return std::string_view{frame.get().menu_command_id} == "edit.move_to_bgm";
    }));
}

TEST_CASE("Native selected-note drag sends live BGM targets and can return to the playfield", "[frontend-state][event-harness][move][bgm-move]") {
    move_event_fixture_t fixture{true};
    auto& view = fixture.harness.view().compose;
    const auto viewport = compose_canvas_viewport_for_testing();
    expect_lane_key(viewport.has_value());
    const auto bgm_x = static_cast<double>(view.viewport_width_pixels) * .75;
    std::array<WormUiComposeBgmLaneView, 1> lanes{{{.bounds = {bgm_x, 0, bgm_x + 36, view.viewport_height_pixels},
        .source_lane_id = 71}}};
    view.bgm_lanes = lanes.data();
    view.bgm_lane_count = 1;
    view.playable_field_right_pixels = bgm_x;
    (void)fixture.harness.frame();
    (void)fixture.start();
    const auto screen_x = viewport->x + static_cast<float>((bgm_x + 18) * viewport->width / view.viewport_width_pixels);
    const std::array over{motion_event(screen_x, 460)};
    const auto entered = fixture.harness.frame(over);
    const auto& input = move_event_input(entered, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE);
    INFO("BGM input " << input.note_move_to_bgm << " lane " << input.lane_id << " x " << input.x_pixels << " BGM " << bgm_x << " fieldright " << view.playable_field_right_pixels);
    expect_lane_key(input.note_move_to_bgm && input.lane_id == 71);
    const auto returned = fixture.move(440);
    expect_lane_key(!move_event_input(returned, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE).note_move_to_bgm);
    (void)fixture.harness.frame(over);
    const std::array up{button_event(false, screen_x, 460)};
    const auto released = fixture.harness.frame(up);
    expect_lane_key(move_event_input(released, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE).note_move_to_bgm);
    expect_lane_key(!worm_ui_nuklear_compose_state().note_move_active);
}

TEST_CASE("BGM displacement status reports the affected notes during a drag", "[frontend-state][event-harness][move][bgm-move]") {
    move_event_fixture_t fixture;
    fixture.harness.view().compose.move_to_bgm_count = 2;
    (void)fixture.harness.frame();
    expect_lane_key(fixture.harness.drawn_text().find("2 notes will move to BGM") != std::string::npos);
}

TEST_CASE("Combined primary clicks toggle matching sounds twice without editing", "[frontend-state][event-harness][select-same-sound]") {
    const bool visible = GENERATE(true, false);
    t422_fixture_t fixture{t422_song()};
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    fixture.project();
    const auto center = t422_note_screen_center(fixture, 101);
    REQUIRE(center);
#if defined(__APPLE__)
    const auto primary = SDL_KMOD_GUI;
#else
    const auto primary = SDL_KMOD_CTRL;
#endif
    const auto modifiers = static_cast<SDL_Keymod>(primary | (visible ? SDL_KMOD_SHIFT : SDL_KMOD_ALT));
    const auto kind = visible ? WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE : WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART;
    const auto before = t422_native_bytes(fixture.history.document);
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto [x, y] = *center;
        const std::array down{motion_event(x, y), button_event(true, x, y)};
        const auto pressed = fixture.harness.frame(down, modifiers);
        for (std::size_t i = 0; i < pressed.get().compose_input_count; ++i)
            expect_t422(pressed.get().compose_inputs[i].kind == kind);
        (void)fixture.dispatch(pressed);
        const std::array up{button_event(false, x, y)};
        const auto released = fixture.harness.frame(up, modifiers);
        for (std::size_t i = 0; i < released.get().compose_input_count; ++i)
            expect_t422(released.get().compose_inputs[i].kind == kind);
        (void)fixture.dispatch(released);
        expect_t422(repeat == 0 ? !fixture.interaction->selected_notes.empty() : fixture.interaction->selected_notes.empty());
        expect_t422(t422_native_bytes(fixture.history.document) == before);
        fixture.project();
    }
    expect_t422(!undo_document_command(fixture.history));
}

TEST_CASE("Same sound hover travels with the remappable add shortcut", "[frontend-state][event-harness][select-same-sound]") {
    move_event_fixture_t fixture;
    const std::array motion{motion_event(300, 400)};
    const auto hovered = fixture.harness.frame(motion);
    expect_lane_key(hovered.get().compose_sound_hover_object_id == 101);
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_SCANCODE_A);
#if defined(__APPLE__)
    const auto modifiers = static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_SHIFT);
#else
    const auto modifiers = static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
#endif
    const auto frame = fixture.harness.frame(std::span{&press, 1}, modifiers);
    expect_lane_key(frame.get().compose_sound_hover_object_id == 101);
    expect_lane_key(frame.get().shortcut_event_count == 1);
    expect_lane_key(frame.get().shortcut_events[0].hid_usage == static_cast<std::uint16_t>(wormedit::shortcut_key_t::a));
}

TEST_CASE("Note menu sends same sound scope and stable source ID", "[frontend-state][event-harness][select-same-sound]") {
    const bool visible = GENERATE(true, false);
    note_menu_fixture_t fixture;
    (void)right_click_at(fixture.harness, 300, 300);
    (void)fixture.harness.frame();
    const auto center = fixture.harness.drawn_text_center(visible ? "Select same sound (visible)" : "Select same sound (whole chart)");
    REQUIRE(center);
    const auto frames = click_at(fixture.harness, center->first, center->second);
    bool found{};
    for (const auto& frame : frames) {
        for (std::size_t i = 0; i < frame.get().compose_input_count; ++i) {
            const auto& input = frame.get().compose_inputs[i];
            if (input.kind != (visible ? WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE : WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART)) continue;
            expect_lane_key(input.object_id == 42);
            expect_lane_key(input.selection_mode == WORM_UI_COMPOSE_SELECTION_ADD);
            found = true;
        }
    }
    expect_lane_key(found);
}

TEST_CASE("Same sound combined clicks route BGM markers and stem cut sources", "[frontend-state][event-harness][select-same-sound][bmson]") {
    const bool cut = GENERATE(true, false);
    move_event_fixture_t fixture{true};
    auto& view = fixture.harness.view().compose;
    const auto x = fixture.notes[0].bounds.left + 200;
    const auto y = fixture.notes[0].bounds.top;
    WormUiComposeBgmLaneView lane{.bounds = {x - 15, 0, x + 15, view.viewport_height_pixels}, .source_lane_id = 90, .stem = true};
    WormUiComposeBgmEventView marker{.bounds = {x - 10, y - 5, x + 10, y + 5}, .event_id = 201, .source_lane_id = 90};
    WormUiComposeStemCutView point{.y_pixels = y, .bgm_lane_id = 90, .object_is_note = true, .object_id = 101};
    view.bgm_lanes = &lane;
    view.bgm_lane_count = 1;
    view.bgm_events = cut ? nullptr : &marker;
    view.bgm_event_count = cut ? 0 : 1;
    view.stem_cuts = cut ? &point : nullptr;
    view.stem_cut_count = cut ? 1 : 0;
    view.bmson_tools_available = true;
    (void)fixture.harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    REQUIRE(viewport);
    const auto screen_x = viewport->x + static_cast<float>(x * viewport->width / view.viewport_width_pixels);
    const auto screen_y = viewport->y + static_cast<float>(y * viewport->height / view.viewport_height_pixels);
#if defined(__APPLE__)
    const auto modifiers = static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_ALT);
#else
    const auto modifiers = static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT);
#endif
    const std::array down{motion_event(screen_x, screen_y), button_event(true, screen_x, screen_y)};
    const auto pressed = fixture.harness.frame(down, modifiers);
    const std::array up{button_event(false, screen_x, screen_y)};
    const auto released = fixture.harness.frame(up, modifiers);
    bool found{};
    for (const auto* frame : {&pressed, &released}) {
        for (std::size_t i = 0; i < frame->get().compose_input_count; ++i) {
            const auto& input = frame->get().compose_inputs[i];
            expect_lane_key(input.kind == WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART);
            expect_lane_key(input.object_id == (cut ? 101 : 201));
            found = true;
        }
    }
    expect_lane_key(found);
}

TEST_CASE("Same sound add shortcut keeps selection and Sounds dock fallbacks", "[frontend-state][event-harness][select-same-sound]") {
    auto song = t422_song();
    song.audio_assets.push_back({.id = asset_id_t{2}, .source_path = "snare.wav"});
    song.resources.push_back({.id = resource_id_t{2}, .name = "snare", .asset = asset_id_t{2}, .roles = {resource_role_t::sample}});
    song.charts[0].measures[0].notes[0].sound = resource_id_t{2};
    t422_fixture_t fixture{std::move(song)};
    auto& ui = worm_ui_nuklear_compose_state();
    ui.sounds_panel_visible = true;
    const std::array rows{WormUiComposeKeysoundRowView{.resource_id = 1, .slot = "01", .name = "kick"},
        WormUiComposeKeysoundRowView{.resource_id = 2, .slot = "02", .name = "snare"}};
    const std::array<std::uint64_t, 2> resources{1, 2};
    auto& view = fixture.harness.view().compose;
    view.keysounds_available = true;
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysound_listed_resource_ids = view.keysound_all_resource_ids = resources.data();
    view.keysound_listed_resource_count = view.keysound_all_resource_count = resources.size();
    (void)fixture.harness.frame();
    ui.keysound_selection.selected = {2};
    ui.keysound_selection.primary = 2;
    expect_lane_key(ui.sound_list_valid);
    const std::array motion{motion_event(ui.sound_list_x + ui.sound_list_width / 2,
        ui.sound_list_y + ui.sound_list_height / 2)};
    (void)fixture.harness.frame(motion);
    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_SCANCODE_A);
#if defined(__APPLE__)
    const auto modifiers = static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_SHIFT);
#else
    const auto modifiers = static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
#endif
    const auto frame = fixture.harness.frame(std::span{&press, 1}, modifiers);
    expect_lane_key(frame.get().shortcut_event_count == 1);
    expect_lane_key(frame.get().compose_sound_hover_object_id == 0);
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    expect_lane_key(dock.selected_resource_count == 1 && dock.selected_resource_ids[0] == 2);
    fixture.application.sound_selection_chart = chart_id_t{frame.get().compose_input_chart_id};
    fixture.application.sound_selection_revision = frame.get().compose_input_document_revision;
    fixture.application.sound_selection_dock = {resource_id_t{dock.selected_resource_ids[0]}};
    fixture.interaction->selected_notes = {object_id_t{101}};
    fixture.interaction->selected_objects.clear();
    const auto& chart = fixture.history.document.charts[0];
    auto keys = compose_sound_selection_keys(fixture.history.document, chart, *fixture.interaction, fixture.application);
    expect_lane_key(keys.size() == 1 && keys[0].value == 1);
    fixture.interaction->selected_notes.clear();
    keys = compose_sound_selection_keys(fixture.history.document, chart, *fixture.interaction, fixture.application);
    expect_lane_key(keys.size() == 1 && keys[0].value == 2);
}

TEST_CASE("Appearance field colors picker emits the selected scheme as a typed draft edit",
          "[theme][options][event-harness]") {
    using namespace wormedit::ui::theme;
    const auto themes=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"assets/themes";
    load(themes);
    nuklear_event_harness_t harness{1280,900};
    harness.view().options={.visible=true,.revision=92,.category=WORM_UI_OPTIONS_DISPLAY,
        .field_scheme="wormedit",.theme="",.dock_layout="",.shortcut_profile_path="",.status=""};
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Default field skin"));
    const auto picker=harness.drawn_text_center("Wormedit");
    REQUIRE(picker);
    (void)click_at(harness,picker->first,picker->second);
    const auto choice=harness.drawn_text_center("iBMSC · IIDX");
    REQUIRE(choice);
    const auto clicks=click_at(harness,choice->first,choice->second);
    const auto edited=std::ranges::find_if(clicks,[](const auto& frame) {
        return frame.get().options.kind==WORM_UI_OPTIONS_EDIT_FIELD;
    });
    REQUIRE(edited!=clicks.end());
    CHECK(edited->get().options.field==WORM_UI_OPTIONS_FIELD_FIELD_SCHEME);
    CHECK(std::string(edited->get().options.text)=="ibmsc-iidx");
    CHECK(edited->get().options.revision==92);
    CHECK(field_scheme_id()=="wormedit");
}

TEST_CASE("Mixed move conflict status describes the whole selection", "[frontend-state][event-harness][mixed-move]") {
    move_event_fixture_t fixture;
    fixture.harness.view().compose.move_conflict_count = 1;
    fixture.harness.view().compose.selected_bgm_event_count = 1;
    (void)fixture.harness.frame();
    expect_lane_key(fixture.harness.drawn_text().find("Selected objects cannot move here") != std::string::npos);
}

TEST_CASE("Area menu loops a committed selection and rejects virtual or unavailable audio", "[frontend-state][event-harness][loop-selection]") {
    const bool virtual_range = GENERATE(false, true);
    const bool output_available = GENERATE(false, true);
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 0;
    view.area_range_end_row = 9600;
    view.area_range_virtual = virtual_range;
    view.transport_loop_selection_available = output_available;
    (void)fixture.harness.frame();
    (void)right_click_at(fixture.harness, 900, 400);
    (void)fixture.harness.frame();
    const auto center = fixture.harness.drawn_text_center("Loop playback");
    REQUIRE(center.has_value());
    bool emitted{};
    for (const auto& snapshot : click_at(fixture.harness, center->first, center->second)) {
        const auto& action = snapshot.get();
        if (std::string_view{action.menu_command_id} != "transport.loop_selection") continue;
        emitted = true;
        check_layer_menu(action.compose_input_chart_id == view.chart_id);
        check_layer_menu(action.compose_input_document_revision == view.document_revision);
        check_layer_menu(action.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    }
    check_layer_menu(emitted == (output_available && !virtual_range));
}

TEST_CASE("Real Shift Space events route to the selection loop command", "[frontend-state][event-harness][loop-selection]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    unsigned calls{};
    const auto registry = wormedit::make_default_editor_command_registry([&](const auto& context) {
        ++calls;
        check_layer_menu(context.command == wormedit::command_id_t{"transport.loop_selection"});
        return wormedit::command_invocation_result_t{.accepted = true};
    });
    (void)harness.frame();
    const std::array keys{key_event(SDL_EVENT_KEY_DOWN, SDLK_SPACE, SDL_SCANCODE_SPACE)};
    const auto action = harness.frame(keys, SDL_KMOD_SHIFT);
    check_layer_menu(action.get().compose_input_count == 0);
    check_layer_menu(route_harness_shortcut(action.get(), registry).status == wormedit::command_dispatch_status_t::invoked);
    check_layer_menu(calls == 1);
}

namespace {
// A compose view whose status-bar overview has no audio row, so the overview label is drawn.
void configure_empty_overview(nuklear_event_harness_t& harness) {
    auto& compose = harness.view().compose;
    compose.visible = true;
    compose.scene_available = true;
    compose.has_bgm_events = true;
    compose.bgm_bar_row_count = 0;
    compose.bgm_bar_reserved = true;
}

// Moves the pointer onto the label twice so Nuklear registers hover before the tooltip pass.
void hover_overview_label(nuklear_event_harness_t& harness, std::pair<float, float> point) {
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(point.first, point.second)};
    script[1].events = {motion_event(point.first + 1.0f, point.second)};
    (void)harness.run_script(script);
}

void cover_with_tap_tool(nuklear_event_harness_t& harness, std::pair<float, float> point,
                         float width, float height) {
    WormUiTestingRect tap{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Tap timing", &tap));
    const auto title_x = tap.x + 60;
    const auto title_y = tap.y + 30;
    const auto destination_x = std::clamp(point.first - 150.0f, 0.0f, width - tap.width) + 60;
    const auto destination_y = std::clamp(point.second - 100.0f, 0.0f, height - tap.height) + 30;
    std::array<event_step_t, 3> drag{};
    drag[0].events = {motion_event(title_x, title_y), button_event(true, title_x, title_y)};
    drag[1].events = {motion_event(destination_x, destination_y)};
    drag[2].events = {motion_event(destination_x, destination_y),
                      button_event(false, destination_x, destination_y)};
    (void)harness.run_script(drag);
    (void)harness.frame();
}

constexpr char overview_tooltip[] = "Audio is unavailable for this chart overview.";
}

TEST_CASE("Overview tooltip shows on an uncovered overview label", "[tooltip][event-harness]") {
    nuklear_event_harness_t harness{1280, 720};
    configure_empty_overview(harness);
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Overview unavailable");
    REQUIRE(label);
    hover_overview_label(harness, *label);
    CHECK(harness.drawn_text_center(overview_tooltip).has_value());
}

TEST_CASE("Overview tooltip stays hidden under a covering Tap timing window", "[tooltip][event-harness]") {
    nuklear_event_harness_t harness{1280, 720};
    configure_empty_overview(harness);
    harness.view().tap_timing.visible = true;
    (void)harness.frame();
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Overview unavailable");
    REQUIRE(label);
    cover_with_tap_tool(harness, *label, 1280, 720);
    WormUiTestingRect tap{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Tap timing", &tap));
    // The label must lie inside the covering window for this case to test coverage.
    const bool inside_x = label->first >= tap.x && label->first <= tap.x + tap.width;
    const bool inside_y = label->second >= tap.y && label->second <= tap.y + tap.height;
    REQUIRE(inside_x);
    REQUIRE(inside_y);
    hover_overview_label(harness, *label);
    CHECK_FALSE(harness.drawn_text_center(overview_tooltip).has_value());
}

namespace {
// One chart row on the Project tab. Its "Open" strip button carries the tooltip "Open in
// Compose" and sits under the Tap timing window's rectangle at this window size.
void configure_project_row(nuklear_event_harness_t& harness, WormUiProjectChartRow* row) {
    harness.view().setup.visible = true;
    harness.view().setup.stepmania_policy_status = "Ready";
    harness.view().setup.stepmania_policy_valid = true;
    harness.view().setup.charts = row;
    harness.view().setup.chart_row_count = 1;
    harness.view().setup.chart_count = 1;
}
}

TEST_CASE("Chart row button tooltip shows when no window covers it", "[tooltip][event-harness][components]") {
    WormUiProjectChartRow row{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    nuklear_event_harness_t harness{1280, 720};
    configure_project_row(harness, &row);
    (void)harness.frame();
    const auto open = harness.drawn_text_center("Open");
    REQUIRE(open);
    hover_overview_label(harness, *open);
    CHECK(harness.drawn_text_center("Open in Compose").has_value());
}

TEST_CASE("Chart row button tooltip stays hidden under a covering Tap timing window", "[tooltip][event-harness][components]") {
    WormUiProjectChartRow row{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    nuklear_event_harness_t harness{800, 720};
    configure_project_row(harness, &row);
    (void)harness.frame();
    // Clicking empty workspace makes the workspace the active window before Tap timing opens.
    std::array<event_step_t, 2> click{};
    click[0].events = {motion_event(790.0f, 400.0f), button_event(true, 790.0f, 400.0f)};
    click[1].events = {motion_event(790.0f, 400.0f), button_event(false, 790.0f, 400.0f)};
    (void)harness.run_script(click);
    harness.view().tap_timing.visible = true;
    (void)harness.frame();
    (void)harness.frame();
    const auto open = harness.drawn_text_center("Open");
    REQUIRE(open);
    WormUiTestingRect tap{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Tap timing", &tap));
    // Move the compact shared dialog by its header over the current action.
    cover_with_tap_tool(harness, *open, 800, 720);
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Tap timing", &tap));
    const bool inside_x = open->first >= tap.x && open->first <= tap.x + tap.width;
    const bool inside_y = open->second >= tap.y && open->second <= tap.y + tap.height;
    REQUIRE(inside_x);
    REQUIRE(inside_y);
    hover_overview_label(harness, *open);
    CHECK_FALSE(harness.drawn_text_center("Open in Compose").has_value());
}

#include "ui/nuklear/compose_waveform_gaps.h"

TEST_CASE("waveform strips leave out Stop and Delay bands",
          "[unit][frontend-state][compose-waveform-gap]") {
    // Bands out of order, one overlapping the next, one outside the viewport.
    const auto strips = compose_waveform_strips(0, 400, {{250, 264}, {100, 114}, {110, 120}, {500, 514}});
    REQUIRE(strips.size() == 3);
    CHECK(strips[0].top == 0);   CHECK(strips[0].bottom == 100);
    CHECK(strips[1].top == 120); CHECK(strips[1].bottom == 250);
    CHECK(strips[2].top == 264); CHECK(strips[2].bottom == 400);
    const auto none = compose_waveform_strips(0, 400, {});
    REQUIRE(none.size() == 1);
    CHECK(none[0].bottom == 400);
    CHECK(compose_waveform_strips(0, 400, {{-5, 450}}).empty());
}

TEST_CASE("Tab measure chord routes one immediate step suppresses OS repeat and retires on focus loss",
          "[frontend-state][event-harness][tab-repeat]") {
    const auto reverse_order = GENERATE(false, true);
    const auto release_first = GENERATE(false, true);
    nuklear_event_harness_t h{1280, 900};
    expect_lane_key(h.ready());
    (void)h.frame();
    const auto tab = key_event(SDL_EVENT_KEY_DOWN, SDLK_TAB, SDL_SCANCODE_TAB);
    const auto arrow = key_event(SDL_EVENT_KEY_DOWN, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
    const auto first = reverse_order ? arrow : tab;
    const auto second = reverse_order ? tab : arrow;
    (void)h.frame(std::span{&first, 1});
    const auto step = h.frame(std::span{&second, 1});
    std::size_t pages{};
    for (std::size_t i = 0; i < step.get().compose_input_count; ++i) {
        const auto& input = step.get().compose_inputs[i];
        if (input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP) {
            expect_lane_key(input.navigation_action == WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD);
            ++pages;
        }
    }
    expect_lane_key(pages == 1);
    const auto repeat = key_event(SDL_EVENT_KEY_DOWN, SDLK_RIGHT, SDL_SCANCODE_RIGHT, true);
    const auto repeated = h.frame(std::span{&repeat, 1});
    for (std::size_t i = 0; i < repeated.get().compose_input_count; ++i)
        expect_lane_key(repeated.get().compose_inputs[i].kind != WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP);
    if (release_first) {
        const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_TAB, SDL_SCANCODE_TAB);
        (void)h.frame(std::span{&release, 1});
    }
    SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    const auto lost = h.frame(std::span{&focus, 1});
    bool canceled{};
    for (std::size_t i = 0; i < lost.get().compose_input_count; ++i)
        canceled = canceled || lost.get().compose_inputs[i].kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL;
    expect_lane_key(canceled);
    expect_lane_key(!worm_ui_nuklear_compose_state().tab_navigation.direction);
    expect_lane_key(!worm_ui_nuklear_compose_state().tab_navigation.tab_down);
}

TEST_CASE("Page keys route one measure and Tab doubles their distance in either scroll direction",
          "[frontend-state][event-harness][tab-repeat]") {
    const auto tab = GENERATE(false, true);
    const auto downscroll = GENERATE(false, true);
    const auto visual_up = GENERATE(false, true);
    nuklear_event_harness_t h{1280, 900};
    h.view().compose.downscroll = downscroll;
    (void)h.frame();
    if (tab) {
        const auto event = key_event(SDL_EVENT_KEY_DOWN, SDLK_TAB, SDL_SCANCODE_TAB);
        (void)h.frame(std::span{&event, 1});
    }
    const auto key = visual_up ? SDLK_PAGEUP : SDLK_PAGEDOWN;
    const auto scancode = visual_up ? SDL_SCANCODE_PAGEUP : SDL_SCANCODE_PAGEDOWN;
    const auto event = key_event(SDL_EVENT_KEY_DOWN, key, scancode);
    const auto output = h.frame(std::span{&event, 1});
    const auto forward = downscroll == visual_up;
    const auto expected = tab
        ? (forward ? WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_FORWARD
                   : WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_BACKWARD)
        : (forward ? WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD
                   : WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD);
    std::size_t count{};
    for (std::size_t i = 0; i < output.get().compose_input_count; ++i) {
        const auto& input = output.get().compose_inputs[i];
        if (input.kind != WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP) continue;
        expect_lane_key(input.navigation_action == expected);
        ++count;
    }
    expect_lane_key(count == 1);
}

TEST_CASE("Held Page repeats through the measure step path and releases without queued steps",
          "[frontend-state][event-harness][tab-repeat]") {
    const auto tab = GENERATE(false, true);
    const auto release_tab = GENERATE(false, true);
    nuklear_event_harness_t h{1280, 900};
    (void)h.frame();
    if (tab) {
        const auto event = key_event(SDL_EVENT_KEY_DOWN, SDLK_TAB, SDL_SCANCODE_TAB);
        (void)h.frame(std::span{&event, 1});
    }
    const auto page = key_event(SDL_EVENT_KEY_DOWN, SDLK_PAGEUP, SDL_SCANCODE_PAGEUP);
    (void)h.frame(std::span{&page, 1});
    for (int repeat = 0; repeat < 4; ++repeat) {
        worm_ui_nuklear_compose_state().tab_navigation.next_repeat_ms = SDL_GetTicks();
        const auto output = h.frame();
        std::size_t count{};
        for (std::size_t i = 0; i < output.get().compose_input_count; ++i) {
            const auto& input = output.get().compose_inputs[i];
            expect_lane_key(input.kind != WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS);
            if (input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP) ++count;
        }
        expect_lane_key(count == 1);
    }
    const auto os_repeat = key_event(SDL_EVENT_KEY_DOWN, SDLK_PAGEUP, SDL_SCANCODE_PAGEUP, true);
    const auto ignored = h.frame(std::span{&os_repeat, 1});
    for (std::size_t i = 0; i < ignored.get().compose_input_count; ++i)
        expect_lane_key(ignored.get().compose_inputs[i].kind != WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP);
    const bool release_modifier = tab && release_tab;
    const auto release = key_event(SDL_EVENT_KEY_UP,
        release_modifier ? SDLK_TAB : SDLK_PAGEUP,
        release_modifier ? SDL_SCANCODE_TAB : SDL_SCANCODE_PAGEUP);
    const auto output = h.frame(std::span{&release, 1});
    bool released{};
    for (std::size_t i = 0; i < output.get().compose_input_count; ++i) {
        const auto& input = output.get().compose_inputs[i];
        if (input.kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE) released = true;
        expect_lane_key(input.kind != WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP);
    }
    expect_lane_key(released);
    if (!release_modifier)
        expect_lane_key(!worm_ui_nuklear_compose_state().tab_navigation.direction);
}

namespace {

struct close_dialog_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    close_dialog_fixture_t(bool application = false, const char* status = "") {
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.close_confirmation = {.visible = true, .application = application, .has_native_path = true,
                                   .project_id = 5, .revision = 9, .status_text = status};
        (void)harness.frame();
        (void)harness.frame();
    }
    // The first non-empty close decision across the frames of one gesture.
    static auto decision_of(const std::vector<workspace_action_snapshot_t>& frames)
        -> const WormUiWorkspaceAction* {
        for (const auto& frame : frames)
            if (frame.get().close_decision != WORM_UI_CLOSE_NONE) return &frame.get();
        return nullptr;
    }
    auto click_button(const char* label) {
        const auto center = harness.drawn_text_center(label);
        REQUIRE(center);
        return click_at(harness, center->first, center->second);
    }
    auto press_key(SDL_Keycode key, SDL_Scancode scancode) {
        const auto down = key_event(SDL_EVENT_KEY_DOWN, key, scancode);
        std::vector<workspace_action_snapshot_t> frames{harness.frame(std::span{&down, 1})};
        const auto up = key_event(SDL_EVENT_KEY_UP, key, scancode);
        frames.push_back(harness.frame(std::span{&up, 1}));
        return frames;
    }
};

} // namespace

TEST_CASE("Unsaved changes shows the shared dialog text and no notification text",
          "[frontend-state][event-harness][dialog][close-confirmation]") {
    {
    close_dialog_fixture_t project;
    const auto& text = project.harness.drawn_text();
    CHECK(text.find("Unsaved changes") != std::string::npos);
    CHECK(text.find("Save your changes before closing this project?") != std::string::npos);
    CHECK(text.find("Discard changes") != std::string::npos);
    CHECK(text.find("Cancel") != std::string::npos);
    CHECK(text.find("Save failed") == std::string::npos);
    }
    close_dialog_fixture_t application{true};
    CHECK(application.harness.drawn_text().find("Save your changes before quitting WormEdit?") !=
          std::string::npos);
}

TEST_CASE("Unsaved changes buttons emit their decisions with the stamps",
          "[frontend-state][event-harness][dialog][close-confirmation]") {
    {
        close_dialog_fixture_t f;
        const auto* decided = close_dialog_fixture_t::decision_of(f.click_button("Cancel"));
        REQUIRE(decided);
        CHECK(decided->close_decision == WORM_UI_CLOSE_CANCEL);
        CHECK(decided->close_project_id == 5);
        CHECK(decided->close_revision == 9);
    }
    {
        close_dialog_fixture_t f;
        const auto* decided = close_dialog_fixture_t::decision_of(f.click_button("Discard changes"));
        REQUIRE(decided);
        CHECK(decided->close_decision == WORM_UI_CLOSE_DISCARD);
    }
    {
        close_dialog_fixture_t f;
        const auto* decided = close_dialog_fixture_t::decision_of(f.click_button("Save"));
        REQUIRE(decided);
        CHECK(decided->close_decision == WORM_UI_CLOSE_SAVE);
        CHECK(decided->kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
        CHECK(decided->close_project_id == 5);
        CHECK(decided->close_revision == 9);
    }
}

TEST_CASE("Unsaved changes maps Escape to cancel and Enter to save",
          "[frontend-state][event-harness][dialog][close-confirmation]") {
    {
        close_dialog_fixture_t f;
        const auto* decided = close_dialog_fixture_t::decision_of(f.press_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE));
        REQUIRE(decided);
        CHECK(decided->close_decision == WORM_UI_CLOSE_CANCEL);
    }
    {
        close_dialog_fixture_t f;
        const auto* decided = close_dialog_fixture_t::decision_of(f.press_key(SDLK_RETURN, SDL_SCANCODE_RETURN));
        REQUIRE(decided);
        CHECK(decided->close_decision == WORM_UI_CLOSE_SAVE);
        CHECK(decided->kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    }
}

TEST_CASE("Unsaved changes shows a status line only for a failed save",
          "[frontend-state][event-harness][dialog][close-confirmation]") {
    close_dialog_fixture_t f{false, "Save failed: disk full"};
    CHECK(f.harness.drawn_text().find("Save failed: disk full") != std::string::npos);
}

namespace {

struct external_player_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<WormUiExternalPlayerDefinitionView, 2> players{{
        {"Alpha", "/usr/bin/alpha", "--start", "--here <measure>", "--stop"},
        {"Beta", "/usr/bin/beta", "", "", ""}}};
    external_player_fixture_t() {
        REQUIRE(harness.ready());
        harness.view().external_player = {.visible = 1, .revision = 3, .players = players.data(),
                                          .player_count = players.size(), .selected = 0};
        (void)harness.frame();
        (void)harness.frame();
    }
    // The first non-empty external-player action across the frames of a gesture.
    static auto action_of(const std::vector<workspace_action_snapshot_t>& frames)
        -> const WormUiExternalPlayerAction* {
        for (const auto& frame : frames)
            if (frame.get().external_player.kind != WORM_UI_EXTERNAL_PLAYER_NONE)
                return &frame.get().external_player;
        return nullptr;
    }
    auto click_label(const char* label) {
        const auto center = harness.drawn_text_center(label);
        REQUIRE(center);
        return click_at(harness, center->first, center->second);
    }
};

} // namespace

TEST_CASE("External player preferences dialog keeps every action",
          "[frontend-state][event-harness][dialog][external-player]") {
    {
        external_player_fixture_t f;
        const auto& text = f.harness.drawn_text();
        CHECK(text.find("External player preferences") != std::string::npos);
        CHECK(text.find("Templates:") == std::string::npos);
        const auto* saved = external_player_fixture_t::action_of(f.click_label("Save"));
        REQUIRE(saved);
        CHECK(saved->kind == WORM_UI_EXTERNAL_PLAYER_SAVE);
        CHECK(saved->selected == 0);
        CHECK(std::string(saved->name) == "Alpha");
        CHECK(std::string(saved->path) == "/usr/bin/alpha");
        CHECK(std::string(saved->here_arguments) == "--here <measure>");
    }
    {
        external_player_fixture_t f;
        const auto* added = external_player_fixture_t::action_of(f.click_label("Add"));
        REQUIRE(added);
        CHECK(added->kind == WORM_UI_EXTERNAL_PLAYER_ADD);
    }
    {
        external_player_fixture_t f;
        const auto* removed = external_player_fixture_t::action_of(f.click_label("Remove"));
        REQUIRE(removed);
        CHECK(removed->kind == WORM_UI_EXTERNAL_PLAYER_REMOVE);
    }
    {
        external_player_fixture_t f;
        const auto* closed = external_player_fixture_t::action_of(f.click_label("Close"));
        REQUIRE(closed);
        CHECK(closed->kind == WORM_UI_EXTERNAL_PLAYER_CLOSE);
    }
    {
        external_player_fixture_t f;
        const auto* selected = external_player_fixture_t::action_of(f.click_label("Beta"));
        REQUIRE(selected);
        CHECK(selected->kind == WORM_UI_EXTERNAL_PLAYER_SELECT);
        CHECK(selected->selected == 1);
    }
}

TEST_CASE("Review sound import dialog has Cancel and Confirm with Enter and no help sentence",
          "[frontend-state][event-harness][sound-import][dialog]") {
    {
        nuklear_event_harness_t harness{1280, 900};
        REQUIRE(harness.ready());
        (void)harness.frame();
        stage_grouped_import(harness);
        (void)harness.frame();
        CHECK(harness.drawn_text().find("Review sound import") != std::string::npos);
        CHECK(harness.drawn_text().find("Review names, kinds") == std::string::npos);
        const auto cancel = harness.drawn_text_center("Cancel");
        REQUIRE(cancel);
        (void)click_at(harness, cancel->first, cancel->second);
        CHECK_FALSE(worm_ui_nuklear_sound_import_active());
    }
    {
        nuklear_event_harness_t harness{1280, 900};
        REQUIRE(harness.ready());
        (void)harness.frame();
        stage_grouped_import(harness);
        (void)harness.frame();
        const auto enter = key_event(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_SCANCODE_RETURN);
        const auto action = harness.frame(std::span{&enter, 1});
        CHECK(action.get().compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT);
    }
}

namespace {

struct project_modal_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    explicit project_modal_fixture_t(const char* audio_path) {
        REQUIRE(harness.ready());
        harness.view().pending_new_project_audio_path = audio_path;
        (void)harness.frame();
        (void)harness.frame();
        (void)harness.frame();
    }
    auto click_button(const char* label) {
        const auto center = harness.drawn_text_center(label);
        REQUIRE(center);
        return click_at(harness, center->first, center->second);
    }
    static auto kind_of(const std::vector<workspace_action_snapshot_t>& frames) -> WormUiWorkspaceActionKind {
        for (const auto& frame : frames)
            if (frame.get().kind != WORM_UI_WORKSPACE_ACTION_NONE) return frame.get().kind;
        return WORM_UI_WORKSPACE_ACTION_NONE;
    }
};

} // namespace

TEST_CASE("Create project dialog keeps its actions and drops the repeated prompt",
          "[frontend-state][event-harness][dialog][project-modal]") {
    {
        project_modal_fixture_t f{"/music/song.ogg"};
        const auto& text = f.harness.drawn_text();
        CHECK(text.find("Import audio as a new project?") != std::string::npos);
        CHECK(text.find("/music/song.ogg") != std::string::npos);
        CHECK(text.find("Choose the initial chart layout") == std::string::npos);
        const auto frames = f.click_button("Create project");
        CHECK(project_modal_fixture_t::kind_of(frames) == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
        for (const auto& frame : frames)
            if (frame.get().kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT) {
                CHECK(std::string(frame.get().project_path) == "/music/song.ogg");
                CHECK(frame.get().new_project_layout_index == 0);
            }
    }
    {
        project_modal_fixture_t f{"/music/song.ogg"};
        CHECK(project_modal_fixture_t::kind_of(f.click_button("Cancel")) ==
              WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT);
    }
    {
        project_modal_fixture_t f{"/music/song.ogg"};
        const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        CHECK(project_modal_fixture_t::kind_of({f.harness.frame(std::span{&escape, 1})}) ==
              WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT);
    }
    {
        project_modal_fixture_t f{"/music/song.ogg"};
        const auto enter = key_event(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_SCANCODE_RETURN);
        CHECK(project_modal_fixture_t::kind_of({f.harness.frame(std::span{&enter, 1})}) ==
              WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
    }
}

TEST_CASE("BMS text encoding dialog keeps its actions and drops the repeated prompt",
          "[frontend-state][event-harness][dialog][project-modal]") {
    const auto make = [](nuklear_event_harness_t& harness) {
        REQUIRE(harness.ready());
        harness.view().pending_bms_encoding_path = "/charts/song.bms";
        (void)harness.frame();
        (void)harness.frame();
        (void)harness.frame();
    };
    {
        nuklear_event_harness_t harness{1280, 900};
        make(harness);
        const auto& text = harness.drawn_text();
        CHECK(text.find("BMS text encoding") != std::string::npos);
        CHECK(text.find("/charts/song.bms") != std::string::npos);
        CHECK(text.find("Choose the BMS text encoding") == std::string::npos);
        const auto open = harness.drawn_text_center("Open chart");
        REQUIRE(open);
        const auto frames = click_at(harness, open->first, open->second);
        bool opened = false;
        for (const auto& frame : frames)
            if (frame.get().kind == WORM_UI_WORKSPACE_ACTION_OPEN_FILE) {
                opened = true;
                CHECK(std::string(frame.get().project_path) == "/charts/song.bms");
                CHECK(frame.get().bms_encoding_index == 1);
            }
        CHECK(opened);
    }
    {
        nuklear_event_harness_t harness{1280, 900};
        make(harness);
        const auto cancel = harness.drawn_text_center("Cancel");
        REQUIRE(cancel);
        bool canceled = false;
        for (const auto& frame : click_at(harness, cancel->first, cancel->second))
            canceled |= frame.get().kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING;
        CHECK(canceled);
    }
    {
        nuklear_event_harness_t harness{1280, 900};
        make(harness);
        const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        CHECK(harness.frame(std::span{&escape, 1}).get().kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING);
    }
}

namespace {

struct chart_import_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<WormUiChartImportRowView, 2> rows{{
        {.chart_id = 31, .label = "Hard · StepMania · 412 notes", .selected = false},
        {.chart_id = 32, .label = "Easy · StepMania · 120 notes", .selected = true}}};
    explicit chart_import_fixture_t(bool can_import = true, const char* error = "") {
        REQUIRE(harness.ready());
        auto& view = harness.view().chart_import;
        view.visible = true;
        view.path = "/charts/song.sm";
        view.error = error;
        view.rows = rows.data();
        view.row_count = rows.size();
        view.summary = "1 sounds reused · 2 added · 1 missing";
        view.notes[0] = "Missing file: hat.wav";
        view.notes[1] = "Easy: starts at 120 BPM, the song's chart starts at 150";
        view.note_count = 2;
        view.can_import = can_import;
        view.document_revision = 7;
        (void)harness.frame();
        (void)harness.frame();
        (void)harness.frame();
    }
    static auto action_of(const std::vector<workspace_action_snapshot_t>& frames)
        -> const WormUiChartImportAction* {
        for (const auto& frame : frames)
            if (frame.get().chart_import.kind != WORM_UI_CHART_IMPORT_NONE) return &frame.get().chart_import;
        return nullptr;
    }
    auto click_label(const char* label) {
        const auto center = harness.drawn_text_center(label);
        REQUIRE(center);
        return click_at(harness, center->first, center->second);
    }
};

} // namespace

TEST_CASE("Import chart dialog shows the difficulties, resource summary and timing notes",
          "[frontend-state][event-harness][dialog][chart-import]") {
    chart_import_fixture_t f;
    const auto& text = f.harness.drawn_text();
    CHECK(text.find("Import chart") != std::string::npos);
    CHECK(text.find("Hard · StepMania · 412 notes") != std::string::npos);
    CHECK(text.find("1 sounds reused · 2 added · 1 missing") != std::string::npos);
    CHECK(text.find("Missing file: hat.wav") != std::string::npos);
    CHECK(text.find("starts at 120 BPM") != std::string::npos);
    CHECK(text.find("/charts/song.sm") != std::string::npos);
}

TEST_CASE("Import chart dialog emits toggle, read, import and cancel actions",
          "[frontend-state][event-harness][dialog][chart-import]") {
    {
        chart_import_fixture_t f;
        const auto* toggled = chart_import_fixture_t::action_of(f.click_label("Hard · StepMania · 412 notes"));
        REQUIRE(toggled);
        CHECK(toggled->kind == WORM_UI_CHART_IMPORT_TOGGLE);
        CHECK(toggled->chart_id == 31);
    }
    {
        chart_import_fixture_t f;
        const auto frames = f.click_label("Read");
        const auto* read = chart_import_fixture_t::action_of(frames);
        REQUIRE(read);
        CHECK(read->kind == WORM_UI_CHART_IMPORT_LOAD);
        for (const auto& frame : frames)
            if (frame.get().chart_import.kind == WORM_UI_CHART_IMPORT_LOAD)
                CHECK(std::string(frame.get().project_path) == "/charts/song.sm");
    }
    {
        chart_import_fixture_t f;
        const auto* imported = chart_import_fixture_t::action_of(f.click_label("Import"));
        REQUIRE(imported);
        CHECK(imported->kind == WORM_UI_CHART_IMPORT_CONFIRM);
        CHECK(imported->document_revision == 7);
    }
    {
        chart_import_fixture_t f;
        const auto* canceled = chart_import_fixture_t::action_of(f.click_label("Cancel"));
        REQUIRE(canceled);
        CHECK(canceled->kind == WORM_UI_CHART_IMPORT_CANCEL);
    }
    {
        chart_import_fixture_t f;
        const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        const auto* canceled = chart_import_fixture_t::action_of({f.harness.frame(std::span{&escape, 1})});
        REQUIRE(canceled);
        CHECK(canceled->kind == WORM_UI_CHART_IMPORT_CANCEL);
    }
}

TEST_CASE("Import chart dialog cannot import until a difficulty is ticked and shows read errors",
          "[frontend-state][event-harness][dialog][chart-import]") {
    chart_import_fixture_t f{false, "The file contains no charts."};
    CHECK(f.harness.drawn_text().find("The file contains no charts.") != std::string::npos);
    CHECK(chart_import_fixture_t::action_of(f.click_label("Import")) == nullptr);
}

TEST_CASE("Project tab Import chart button starts the import dialog",
          "[frontend-state][event-harness][chart-import]") {
    WormUiProjectChartRow row{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    harness.view().setup.visible = true;
    harness.view().setup.stepmania_policy_status = "Ready";
    harness.view().setup.stepmania_policy_valid = true;
    harness.view().setup.charts = &row;
    harness.view().setup.chart_row_count = 1;
    harness.view().setup.chart_count = 1;
    (void)harness.frame();
    const auto button = harness.drawn_text_center("Import chart…");
    REQUIRE(button);
    bool began = false;
    for (const auto& frame : click_at(harness, button->first, button->second))
        began |= frame.get().chart_import.kind == WORM_UI_CHART_IMPORT_BEGIN;
    CHECK(began);
}

TEST_CASE("Project charts use compact row menus and a cancellable delete dialog",
          "[frontend-state][event-harness][project-charts]") {
    WormUiProjectChartRow row{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    configure_project_row(harness, &row);
    (void)harness.frame();
    CHECK_FALSE(harness.drawn_text_center("Apply to selected charts"));
    CHECK_FALSE(harness.drawn_text_center("Duplicate"));
    const auto more = harness.drawn_text_center("⋯");
    REQUIRE(more);
    for (const auto& frame : click_at(harness, more->first, more->second))
        CHECK(frame.get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    (void)harness.frame();
    const auto erase = harness.drawn_text_center("Delete…");
    REQUIRE(erase);
    for (const auto& frame : click_at(harness, erase->first, erase->second))
        CHECK(frame.get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    (void)harness.frame();
    CHECK(harness.drawn_text_center("Delete chart?"));
    CHECK_FALSE(worm_ui_frontend_request_new_project());
    CHECK_FALSE(worm_ui_frontend_request_save_project(harness.view().project_id));
    const auto save = key_event(SDL_EVENT_KEY_DOWN, SDLK_S, SDL_SCANCODE_S);
    CHECK(harness.frame(std::span{&save, 1}, SDL_KMOD_CTRL).get().shortcut_event_count == 0);
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_S, SDL_SCANCODE_S);
    (void)harness.frame(std::span{&release, 1});
    const auto cancel = harness.drawn_text_center("Cancel");
    REQUIRE(cancel);
    for (const auto& frame : click_at(harness, cancel->first, cancel->second))
        CHECK(frame.get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    (void)harness.frame();
    CHECK_FALSE(harness.drawn_text_center("Delete chart?"));
}

TEST_CASE("Project selection exposes metadata controls below compact chart rows",
          "[frontend-state][event-harness][project-charts]") {
    WormUiProjectChartRow row{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    configure_project_row(harness, &row);
    (void)harness.frame();
    CHECK_FALSE(harness.drawn_text_center("Set author…"));
    const auto name = harness.drawn_text_center("Chart");
    REQUIRE(name);
    (void)click_at(harness, name->first, name->second);
    (void)harness.frame();
    const auto author = harness.drawn_text_center("Set author…");
    REQUIRE(author);
    (void)click_at(harness, author->first, author->second);
    (void)harness.frame();
    const auto apply = harness.drawn_text_center("Apply author");
    REQUIRE(apply);
    bool applied = false;
    for (const auto& frame : click_at(harness, apply->first, apply->second)) {
        if (frame.get().project_chart.kind != WORM_UI_PROJECT_CHART_BATCH_METADATA) continue;
        applied = true;
        CHECK(frame.get().project_chart.edit_author);
        CHECK_FALSE(frame.get().project_chart.edit_rating);
        CHECK(frame.get().project_chart.selected_count == 1);
        CHECK(frame.get().project_chart.selected_chart_ids[0] == 10);
    }
    CHECK(applied);
}

TEST_CASE("Project chart popups expire when the tab or project session changes",
          "[frontend-state][event-harness][project-charts]") {
    WormUiProjectChartRow row{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    configure_project_row(harness, &row);
    (void)harness.frame();
    const auto more = harness.drawn_text_center("⋯");
    REQUIRE(more);
    (void)click_at(harness, more->first, more->second);
    (void)harness.frame();
    REQUIRE(worm_ui_nuklear_setup_popup_owns_input());
    harness.view().setup.visible = false;
    (void)harness.frame();
    CHECK_FALSE(worm_ui_nuklear_setup_popup_owns_input());
    harness.view().setup.visible = true;
    (void)harness.frame();
    const auto again = harness.drawn_text_center("⋯");
    REQUIRE(again);
    (void)click_at(harness, again->first, again->second);
    (void)harness.frame();
    REQUIRE(worm_ui_nuklear_setup_popup_owns_input());
    ++harness.view().setup.session_generation;
    (void)harness.frame();
    CHECK_FALSE(worm_ui_nuklear_setup_popup_owns_input());
}

TEST_CASE("Appearance scratch dropdown emits a typed independent stage override", "[options][scratch-presentation][event-harness]") {
    nuklear_event_harness_t harness{1280, 900};
    harness.view().options = {.visible = true, .revision = 93, .category = WORM_UI_OPTIONS_DISPLAY,
        .p1_scratch_position = 1, .p2_scratch_position = 0,
        .field_scheme = "wormedit", .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = ""};
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Single / P1 scratch position"));
    REQUIRE(harness.drawn_text_center("P2 scratch position"));
    const auto picker = harness.drawn_text_center("Chart layout");
    REQUIRE(picker);
    (void)click_at(harness, picker->first, picker->second);
    const auto choice = harness.drawn_text_center("Right");
    REQUIRE(choice);
    const auto clicks = click_at(harness, choice->first, choice->second);
    const auto edited = std::ranges::find_if(clicks, [](const auto& frame) {
        return frame.get().options.kind == WORM_UI_OPTIONS_EDIT_FIELD;
    });
    REQUIRE(edited != clicks.end());
    expect_lane_key(edited->get().options.field == WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION);
    expect_lane_key(edited->get().options.integer == 3);
    expect_lane_key(edited->get().options.revision == 93);
}

TEST_CASE("Options expands its settings and anchors actions when resized",
          "[options][dialog][resize][event-harness]") {
    nuklear_event_harness_t harness{1600, 1000};
    harness.view().options = {.visible = true, .revision = 94, .category = WORM_UI_OPTIONS_EDITOR,
        .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = ""};
    (void)harness.frame();
    (void)harness.frame();
    WormUiTestingRect before{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options", &before));
    const auto close = harness.drawn_text_center("Close");
    REQUIRE(close);
    CHECK_FALSE(harness.drawn_text_center("Minimap click moves paused playhead"));
    const auto x = before.x + before.width - 5;
    const auto y = before.y + before.height - 5;
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
    (void)harness.frame(std::array{motion, button});
    motion.motion.x += 240;
    motion.motion.y += 200;
    motion.motion.state = SDL_BUTTON_LMASK;
    (void)harness.frame(std::array{motion});
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.down = false;
    button.button.x = motion.motion.x;
    button.button.y = motion.motion.y;
    (void)harness.frame(std::array{button});
    (void)harness.frame();
    WormUiTestingRect after{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options", &after));
    const auto moved_close = harness.drawn_text_center("Close");
    REQUIRE(moved_close);
    expect_lane_key(after.width > before.width + 200);
    expect_lane_key(after.height > before.height + 150);
    expect_lane_key(std::abs((moved_close->second - close->second) - (after.height - before.height)) < 2);
    expect_lane_key(std::abs(after.y + after.height - moved_close->second - 30) < 3);
    expect_lane_key(harness.drawn_text_center("Minimap click moves paused playhead").has_value());
}

namespace {
struct chart_reorder_fixture_t {
    WormUiProjectChartRow rows[3]{
        {.chart_id = 10, .name = "First chart", .author = "Author", .rating = "Unset", .mode = "7K",
         .output_groups = "Unassigned", .length = "2 s", .diagnostic = ""},
        {.chart_id = 20, .name = "Second chart", .author = "Author", .rating = "Unset", .mode = "7K",
         .output_groups = "Unassigned", .length = "2 s", .diagnostic = ""},
        {.chart_id = 30, .name = "Third chart", .author = "Author", .rating = "Unset", .mode = "7K",
         .output_groups = "Unassigned", .length = "2 s", .diagnostic = ""}};
    nuklear_event_harness_t harness{1280, 900};
    chart_reorder_fixture_t() {
        configure_project_row(harness, rows);
        auto& view = harness.view().setup;
        view.song_id = 42;
        view.session_generation = 17;
        view.document_revision = 53;
        view.chart_row_count = 3;
        view.chart_count = 3;
        (void)harness.frame();
    }
    auto name_bounds(const char* name) -> WormUiTestingDrawnText {
        const auto texts = harness.drawn_text_bounds();
        const auto found = std::ranges::find_if(texts, [name](const auto& text) {
            return std::string_view{text.text} == name;
        });
        REQUIRE(found != texts.end());
        return *found;
    }
    auto press_grip() -> std::pair<float, float> {
        const auto name = name_bounds("First chart");
        // The grip precedes the26px checkbox and the name cell with their row spacing.
        const float x = name.x - 44.0f;
        const float y = name.y + name.height * 0.5f;
        const std::array events{motion_event(x, y), button_event(true, x, y)};
        CHECK(harness.frame(events).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
        return {x, y};
    }
    auto release_at(float x, float y) -> workspace_action_snapshot_t {
        const auto moved = motion_event(x, y);
        CHECK(harness.frame(std::span{&moved, 1}).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
        const auto released = button_event(false, x, y);
        return harness.frame(std::span{&released, 1});
    }
};
}

TEST_CASE("Project charts with empty imported names remain selectable",
          "[event-harness][project-charts][chart-reorder]") {
    chart_reorder_fixture_t fixture;
    fixture.rows[1].name = "";
    (void)fixture.harness.frame();
    const auto center = fixture.harness.drawn_text_center("Untitled chart");
    REQUIRE(center);
    (void)click_at(fixture.harness, center->first, center->second);
    (void)fixture.harness.frame();
    expect_lane_key(fixture.harness.drawn_text_center("Set author…").has_value());
}

TEST_CASE("Project visible grip drops relative to a stable chart row", "[event-harness][project-charts][chart-reorder]") {
    chart_reorder_fixture_t f;
    const auto third = f.name_bounds("Third chart");
    const auto start = f.press_grip();
    const auto released = f.release_at(start.first, third.y + third.height * 0.5f + 7.0f);
    const auto& action = released.get().project_chart;
    CHECK(action.kind == WORM_UI_PROJECT_CHART_REORDER);
    CHECK((action.chart_id == 10 && action.target_chart_id == 30 && action.insert_after));
    CHECK((action.document_revision == 53 && action.session_generation == 17));
    CHECK(f.harness.frame().get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
}

TEST_CASE("Project chart drag cancels on Escape outside release and stale revision", "[event-harness][project-charts][chart-reorder]") {
    for (int cancellation = 0; cancellation < 3; ++cancellation) {
        chart_reorder_fixture_t f;
        const auto third = f.name_bounds("Third chart");
        const auto start = f.press_grip();
        if (cancellation == 0) {
            const auto escape = key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
            (void)f.harness.frame(std::span{&escape, 1});
        }
        if (cancellation == 1) ++f.harness.view().setup.document_revision;
        const auto y = cancellation == 2 ? 880.0f : third.y + third.height * 0.5f + 7.0f;
        CHECK(f.release_at(start.first, y).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    }
}

namespace {
void drag_options_header(nuklear_event_harness_t& harness, float dx, float dy) {
    const auto title = harness.drawn_text_center("Options");
    REQUIRE(title);
    const auto [x, y] = *title;
    (void)harness.frame(std::array{motion_event(x, y), button_event(true, x, y)});
    (void)harness.frame(std::array{motion_event(x + dx, y + dy)});
    (void)harness.frame(std::array{button_event(false, x + dx, y + dy)});
    (void)harness.frame();
}
}

TEST_CASE("Options custom grippy header moves the tool and preserves its close action",
          "[options][dialog][options-drag][event-harness]") {
    nuklear_event_harness_t harness{1600, 1000};
    harness.view().options = {.visible = true, .revision = 94, .category = WORM_UI_OPTIONS_EDITOR,
        .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = ""};
    (void)harness.frame();
    (void)harness.frame();
    WormUiTestingRect before{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options", &before));
    WormUiTestingRect backdrop{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options##dialog-backdrop", &backdrop));
    expect_lane_key(backdrop.x == 0 && backdrop.y == 0);
    expect_lane_key(backdrop.width == 1600 && backdrop.height == 1000);
    drag_options_header(harness, 80, 45);
    WormUiTestingRect after{};
    REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options", &after));
    expect_lane_key(std::abs(after.x - before.x - 80) < 1);
    expect_lane_key(std::abs(after.y - before.y - 45) < 1);
    expect_lane_key(after.width == before.width && after.height == before.height);
    const auto close = harness.drawn_text_center("Close");
    REQUIRE(close);
    bool closed = false;
    for (const auto& frame : click_at(harness, close->first, close->second))
        closed = closed || frame.get().options.kind == WORM_UI_OPTIONS_CLOSE;
    expect_lane_key(closed);
}

TEST_CASE("Options header movement cancels on focus loss and while hidden",
          "[options][dialog][options-drag][event-harness]") {
    for (const auto lose_focus : {true, false}) {
        nuklear_event_harness_t harness{1600, 1000};
        harness.view().options = {.visible = true, .revision = 94, .category = WORM_UI_OPTIONS_EDITOR,
            .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = ""};
        (void)harness.frame();
        (void)harness.frame();
        const auto title = harness.drawn_text_center("Options");
        REQUIRE(title);
        const auto [x, y] = *title;
        (void)harness.frame(std::array{motion_event(x, y), button_event(true, x, y)});
        WormUiTestingRect before{};
        REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options", &before));
        if (lose_focus) {
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            (void)harness.frame(std::span{&lost, 1});
            SDL_Event gained{};
            gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            (void)harness.frame(std::span{&gained, 1});
        } else {
            harness.view().options.visible = false;
            (void)harness.frame();
            harness.view().options.visible = true;
        }
        (void)harness.frame(std::array{motion_event(x + 80, y + 45)});
        WormUiTestingRect after{};
        REQUIRE(worm_ui_frontend_window_bounds_for_testing("Options", &after));
        expect_lane_key(after.x == before.x && after.y == before.y);
        (void)harness.frame(std::array{button_event(false, x + 80, y + 45)});
    }
}

namespace {
auto skin_test_lanes(const wormedit::lane_layout_t& layout) -> std::vector<WormUiComposeLaneView> {
    std::vector<WormUiComposeLaneView> result;
    for (const auto& lane : layout.lanes)
        result.push_back({.lane_id = lane.id.value, .display_order = lane.display_order,
            .role = static_cast<std::uint8_t>(lane.role), .stage = static_cast<std::uint8_t>(lane.stage),
            .key_color = static_cast<std::uint8_t>(lane.key_color)});
    return result;
}
void click_skin_label(nuklear_event_harness_t& harness, const char* text) {
    const auto center = harness.drawn_text_center(text);
    REQUIRE(center);
    (void)click_at(harness, center->first, center->second);
    (void)harness.frame();
}
void expect_skin_choice(nuklear_event_harness_t& harness, const char* label, const char* id) {
    const auto center = harness.drawn_text_center(label);
    REQUIRE(center);
    bool chosen = false;
    for (const auto& frame : click_at(harness, center->first, center->second)) {
        const auto& action = frame.get().options;
        if (action.kind != WORM_UI_OPTIONS_EDIT_FIELD) continue;
        chosen = true;
        expect_lane_key(action.field == WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME);
        expect_lane_key(action.revision == 97);
        expect_lane_key(std::string_view(action.text) == id);
    }
    expect_lane_key(chosen);
}
}

TEST_CASE("Appearance chooses a layout skin and can restore default inheritance",
          "[layout-skins][options][event-harness]") {
    wormedit::ui::theme::load(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/themes");
    nuklear_event_harness_t harness{1600, 1000};
    const auto lanes = skin_test_lanes(wormedit::make_bms_10k_standard_profile().layout);
    harness.view().options = {.visible = true, .revision = 97, .category = WORM_UI_OPTIONS_DISPLAY,
        .field_scheme = "wormedit", .current_field_layout_key = "bms-dp5",
        .current_field_layout_name = "BMS DP5 / 10K", .current_layout_field_scheme = "",
        .current_field_layout_lanes = lanes.data(), .current_field_layout_lane_count = lanes.size(),
        .theme = "", .dock_layout = "", .shortcut_profile_path = "", .status = ""};
    (void)harness.frame();
    (void)harness.frame();
    click_skin_label(harness, "Use default");
    expect_lane_key(harness.drawn_text().find("MyO2 4K") == std::string::npos);
    expect_skin_choice(harness, "iBMSC · BMSE", "ibmsc-bmse");
    harness.view().options.current_layout_field_scheme = "ibmsc-bmse";
    (void)harness.frame();
    click_skin_label(harness, "iBMSC · BMSE");
    expect_skin_choice(harness, "Use default", "");
}

TEST_CASE("Frontend skin bridge resolves compatibility without repeated cache invalidation",
          "[layout-skins][event-harness]") {
    wormedit::ui::theme::load(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/themes");
    nuklear_event_harness_t harness{1280, 900};
    const auto profile = wormedit::make_osu_mania_profile(4);
    REQUIRE(profile);
    auto lanes = skin_test_lanes(profile->layout);
    REQUIRE(worm_ui_frontend_configure_field_scheme("ibmsc-myo2-4k", "ibmsc-bmse", lanes.data(), lanes.size()));
    const auto generation = wormedit::ui::theme::field_generation();
    expect_lane_key(wormedit::ui::theme::field_scheme_id() == "ibmsc-myo2-4k");
    REQUIRE(worm_ui_frontend_configure_field_scheme("ibmsc-myo2-4k", "ibmsc-bmse", lanes.data(), lanes.size()));
    expect_lane_key(wormedit::ui::theme::field_generation() == generation);
    lanes[0].key_color = static_cast<std::uint8_t>(wormedit::lane_key_color_t::pms_red);
    expect_lane_key(!worm_ui_frontend_configure_field_scheme("ibmsc-myo2-4k", "ibmsc-bmse", lanes.data(), lanes.size()));
    expect_lane_key(wormedit::ui::theme::field_scheme_id() == "ibmsc-bmse");
    expect_lane_key(!worm_ui_frontend_configure_field_scheme("missing", "ibmsc-myo2-4k", lanes.data(), lanes.size()));
    expect_lane_key(wormedit::ui::theme::field_scheme_id() == "wormedit");
    const auto dp5 = skin_test_lanes(wormedit::make_bms_10k_standard_profile().layout);
    REQUIRE(worm_ui_frontend_configure_field_scheme("ibmsc-bmse", "wormedit", dp5.data(), dp5.size()));
    expect_lane_key(worm_ui_frontend_field_scratch_side(2) == 0);
    const auto dp7 = skin_test_lanes(wormedit::make_bms_14k_profile().layout);
    REQUIRE(worm_ui_frontend_configure_field_scheme("ibmsc-bmse", "wormedit", dp7.data(), dp7.size()));
    expect_lane_key(worm_ui_frontend_field_scratch_side(2) == -1);
    (void)worm_ui_frontend_configure_field_scheme("wormedit", "wormedit", nullptr, 0);
}

namespace {
auto measure_navigation_song() -> song_document_t {
    auto song = t422_song();
    auto& chart = song.charts.front();
    for (auto& measure : chart.measures) {
        measure.notes.clear();
        measure.definition.length = {1, 16};
        measure.definition.meter.reset();
    }
    chart.measures.push_back({.id = measure_id_t{3},
                             .definition = {{1, 1}, meter_t{4, 4}}});
    return song;
}

void measure_navigation_key(t422_fixture_t& fixture, bool forward) {
    const bool visual_up = forward == fixture.harness.view().compose.downscroll;
    const auto key = visual_up ? SDLK_UP : SDLK_DOWN;
    const auto scan = visual_up ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN;
    const auto press = key_event(SDL_EVENT_KEY_DOWN, key, scan);
    const auto action = fixture.harness.frame(std::span{&press, 1});
    expect_t422(action.get().compose_input_count == 1);
    expect_t422(action.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS);
    INFO(fixture.dispatch(action));
    const auto release = key_event(SDL_EVENT_KEY_UP, key, scan);
    (void)fixture.dispatch(fixture.harness.frame(std::span{&release, 1}));
    fixture.project();
}

void measure_navigation_wheel(t422_fixture_t& fixture, bool forward) {
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = forward == fixture.harness.view().compose.downscroll ? 1.0f : -1.0f;
    wheel.wheel.mouse_x = 480;
    wheel.wheel.mouse_y = 400;
    const auto action = fixture.harness.frame(std::span{&wheel, 1}, SDL_KMOD_ALT);
    expect_t422(action.get().compose_input_count == 1);
    INFO(fixture.dispatch(action));
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_LALT, SDL_SCANCODE_LALT);
    (void)fixture.harness.frame(std::span{&release, 1});
    fixture.project();
}
}

TEST_CASE("Production keyboard and Alt wheel stay on each shortened measure grid",
          "[measure-local-snap][event-harness]") {
    t422_fixture_t fixture{measure_navigation_song()};
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    fixture.interaction->active_snap_denominator = 4;
    fixture.project();
    const auto revision = fixture.history.document.revision;
    measure_navigation_key(fixture, true);
    expect_t422(fixture.interaction->cursor == local_position_t{measure_id_t{2}, 0});
    measure_navigation_key(fixture, true);
    expect_t422(fixture.interaction->cursor == local_position_t{measure_id_t{3}, 0});
    measure_navigation_wheel(fixture, true);
    expect_t422(fixture.interaction->cursor == local_position_t{measure_id_t{3}, 240});
    measure_navigation_wheel(fixture, false);
    expect_t422(fixture.interaction->cursor == local_position_t{measure_id_t{3}, 0});
    measure_navigation_key(fixture, false);
    expect_t422(fixture.interaction->cursor == local_position_t{measure_id_t{2}, 0});
    expect_t422(fixture.history.document.revision == revision);
    expect_t422(fixture.history.undo.empty());
}

TEST_CASE("Lane-less Sounds fallback menu emits the complete captured session", "[event-harness][sound-fallback]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.project_id = 991;
    view.chart_id = 77;
    view.document_revision = 23;
    view.scene_available = false;
    view.lanes = nullptr;
    view.lane_count = 0;
    const std::array<WormUiComposeKeysoundRowView, 1> rows{{
        {.resource_id = 1400, .slot = "01", .name = "missing.wav", .missing = true}}};
    const std::array<WormUiKeysoundFallbackView, 1> fallbacks{{
        {.resource_id = 1400, .outcome = 2, .candidate_count = 2,
         .candidates = {"missing.ogg", "missing.flac"}}}};
    view.keysounds = rows.data();
    view.keysound_count = view.keysound_total_count = rows.size();
    view.keysounds_available = true;
    view.keysound_fallbacks = fallbacks.data();
    view.keysound_fallback_count = fallbacks.size();
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = false;
    state.bga_panel_visible = false;
    state.layers_panel_visible = false;
    state.sounds_panel_visible = true;
    (void)harness.frame();
    const auto row = harness.drawn_text_center("missing.wav");
    REQUIRE(row);
    (void)right_click_at(harness, row->first, row->second);
    (void)harness.frame();
    const auto candidate = harness.drawn_text_center("Use missing.flac");
    REQUIRE(candidate);
    const auto actions = click_at(harness, candidate->first, candidate->second);
    const auto action = std::ranges::find_if(actions, [](const auto& snapshot) {
        return snapshot.get().compose_action == WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK;
    });
    REQUIRE(action != actions.end());
    CHECK(action->get().compose_input_project_id == 991);
    CHECK(action->get().compose_input_chart_id == 77);
    CHECK(action->get().compose_input_document_revision == 23);
    CHECK(action->get().compose_resource_id == 1400);
    CHECK(action->get().compose_fallback_candidate == 1);
}

TEST_CASE("Quantize preview control emits a revision checked toggle without Apply", "[frontend-state][event-harness][quantize-intent]") {
    nuklear_event_harness_t harness{1600, 900};
    seed_quantize_controls(harness);
    (void)harness.frame();
    expect_draw(!harness.view().compose.quantize_preview_enabled);
    const auto toggled = click_quantize_label(harness, "Preview");
    expect_draw(toggled.get().compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_QUANTIZE_PREVIEW);
    expect_draw(toggled.get().compose_input_chart_id == 419);
    expect_draw(toggled.get().compose_input_document_revision == 7);
}

TEST_CASE("Stem lane resize retains width while projected geometry updates on every held frame", "[frontend-state][event-harness][bmson-stem][resize]") {
    stem_edge_fixture_t f;
    const auto start=f.edge_x;
    (void)f.press(start,300);
    for (const auto distance : {20.0f,40.0f,80.0f}) {
        f.drag_to(start+distance,300);
        f.sync_lane();
        (void)f.harness.frame();
        expect_draw(std::abs(f.width()-(36+distance))<1);
        expect_draw(worm_ui_nuklear_compose_state().stem_lane_resize.has_value());
    }
    f.release(start+80,300);
    for (int i=0;i<3;++i) { f.sync_lane(); (void)f.harness.frame(); }
    expect_draw(std::abs(f.width()-116)<1);
    expect_draw(!worm_ui_nuklear_compose_state().stem_lane_resize.has_value());
}

TEST_CASE("Area conversion menu offers both pause sides and explicit measure policies", "[frontend][event-harness][range-pause][T560]") {
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP,
        WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY,
        WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP_RESIZE,
        WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY_RESIZE);
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 0;
    view.area_range_end_row = 9600;
    (void)fixture.harness.frame();
    (void)right_click_at(fixture.harness, 900, 400);
    (void)fixture.harness.frame();
    const std::array labels{"Convert to Stop", "Convert to Delay", "Convert to Stop · shorten measures", "Convert to Delay · shorten measures"};
    const auto target = fixture.harness.drawn_text_center(labels[kind - WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP]);
    REQUIRE(target.has_value());
    const auto clicked = click_at(fixture.harness, target->first, target->second);
    const auto emitted = std::ranges::find_if(clicked, [kind](const auto& snapshot) { return snapshot.get().compose_action == kind; });
    REQUIRE(emitted != clicked.end());
    check_layer_menu(emitted->get().compose_input_project_id == view.project_id);
    check_layer_menu(emitted->get().compose_input_chart_id == view.chart_id);
    check_layer_menu(emitted->get().compose_input_document_revision == view.document_revision);
}

TEST_CASE("Area pause conversion is disabled while locked or virtual and retires stale menus", "[frontend-state][event-harness][range-pause][T560]") {
    const auto guard = GENERATE(0, 1, 2);
    note_menu_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 0;
    view.area_range_end_row = 9600;
    view.waveform_timing_locked = guard == 0;
    view.area_range_virtual = guard == 1;
    (void)fixture.harness.frame();
    (void)right_click_at(fixture.harness, 900, 400);
    (void)fixture.harness.frame();
    const auto target = fixture.harness.drawn_text_center("Convert to Stop");
    REQUIRE(target);
    if (guard == 2) { ++view.document_revision; (void)fixture.harness.frame(); }
    const auto actions = click_at(fixture.harness, target->first, target->second);
    check_layer_menu(std::ranges::none_of(actions, [](const auto& snapshot) {
        return snapshot.get().compose_action == WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP;
    }));
}

TEST_CASE("Quantize inspector groups settings and keeps Preview beside Apply",
          "[frontend-state][event-harness][quantize][T561]") {
    nuklear_event_harness_t harness{1600, 900};
    seed_quantize_controls(harness);
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Quantize / Snap"));
    const auto preview = harness.drawn_text_center("Preview");
    const auto apply = harness.drawn_text_center("Quantize");
    REQUIRE(preview);
    REQUIRE(apply);
    CHECK(std::abs(preview->second - apply->second) < 2);
    CHECK(preview->first < apply->first);
    const auto space = harness.drawn_text_center("Space");
    const auto halfway = harness.drawn_text_center("Halfway objects · 2 in selection");
    const auto tails = harness.drawn_text_center("Hold tails · 1 hold in selection");
    const auto presets = harness.drawn_text_center("Presets");
    REQUIRE(space);
    REQUIRE(halfway);
    REQUIRE(tails);
    REQUIRE(presets);
    CHECK(halfway->second - space->second > 50);
    CHECK(tails->second - halfway->second > 50);
    CHECK(presets->second - tails->second > 50);
    (void)click_quantize_label(harness, "Quantize / Snap");
    CHECK_FALSE(harness.drawn_text_center("Preview"));
    CHECK_FALSE(compose_quantize_section_visible());
    (void)click_quantize_label(harness, "Quantize / Snap");
    CHECK(harness.drawn_text_center("Preview"));
}

TEST_CASE("Timing marker double clicks use the drawn per-kind partition with or without BGA",
          "[frontend-state][event-harness][timing-geometry][T563]") {
    const auto ordinal = GENERATE(0, 1, 2, 3, 4, 5, 7);
    const auto all_lanes = GENERATE(true, false);
    const auto with_bga = GENERATE(true, false);
    nuklear_event_harness_t harness{1600, 900};
    auto& view = harness.view().compose;
    view.timing_lane_visibility_mask = all_lanes ? 0xff : static_cast<std::uint8_t>(1U << ordinal);
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    const auto right = 42 + wormedit::ui::compose_timing_width(view.timing_lane_visibility_mask);
    view.playable_field_left_pixels = right + (with_bga ? 48 : 0);
    view.playable_field_right_pixels = 850;
    view.bga_lane_visible = with_bga;
    view.bga_lane_bounds = {right, 0, right + 48, 600};
    const auto lane = static_cast<WormUiTimingLaneKind>(ordinal);
    WormUiTimingMarkerView marker{.object_id = 991, .marker_y_pixels = 200,
        .marker_left_fraction = 0, .marker_right_fraction = 1, .lane = lane, .short_label = "123.45"};
    view.timing_markers = &marker;
    view.timing_marker_count = 1;
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = state.layers_panel_visible = state.sounds_panel_visible = false;
    (void)harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    REQUIRE(viewport);
    const auto extent = wormedit::ui::compose_timing_lane_extent(view, lane);
    REQUIRE(extent);
    const auto x = viewport->x + static_cast<float>((extent->first + extent->second) * .5 /
                                                    view.viewport_width_pixels) * viewport->width;
    const auto y = viewport->y + 200.0f / 600 * viewport->height;
    auto down = button_event(true, x, y), up = button_event(false, x, y);
    down.button.clicks = up.button.clicks = 2;
    const auto action = harness.frame(std::array{motion_event(x, y), down, up});
    expect_draw(action.get().timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN);
    expect_draw(action.get().timing_lane == lane);
    expect_draw(action.get().timing_object_id == 991);
    expect_draw(action.get().timing_chart_id == harness.view().timing.chart_id);
    expect_draw(action.get().timing_document_revision == harness.view().timing.document_revision);
    view.timing_lane_visibility_mask = 0;
    expect_draw(!(compose_timing_marker_at(view, 0, 0)));
    expect_draw(!(compose_timing_marker_at(view, (extent->first + extent->second) * .5, 200)));
}

TEST_CASE("Staged timing drags begin inside the timing lane and reject neighboring BGA",
          "[frontend-state][event-harness][timing-geometry][T563]") {
    const bool bga_press = GENERATE(false, true);
    nuklear_event_harness_t harness{1600, 900};
    auto& view = harness.view().compose;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.timing_lane_visibility_mask = 3;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {170, 0, 218, 600};
    view.playable_field_left_pixels = 218;
    view.playable_field_right_pixels = 800;
    view.timing_stage_active = true;
    view.timing_stage_lane = WORM_UI_TIMING_LANE_SCROLL;
    view.timing_stage_y_pixels = 200;
    auto& timing = harness.view().timing;
    timing.stage_active = true;
    timing.stage_lane = WORM_UI_TIMING_LANE_SCROLL;
    timing.stage_existing_object_id = 991;
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = state.layers_panel_visible = state.sounds_panel_visible = false;
    (void)harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    REQUIRE(viewport);
    const auto x = viewport->x + (bga_press ? 194.0f : 138.0f) / 900 * viewport->width;
    const auto y = viewport->y + 200.0f / 600 * viewport->height;
    (void)harness.frame(std::array{motion_event(x, y), button_event(true, x, y)});
    expect_draw(state.timing_stage_drag_active == !bga_press);
    const auto action = harness.frame(std::array{motion_event(x, y + 20)});
    expect_draw(action.get().timing_stage_action == (bga_press ? WORM_UI_TIMING_STAGE_NONE
                                                        : WORM_UI_TIMING_STAGE_MOVE));
    if (!bga_press) expect_draw(action.get().timing_object_id == 991);
}

namespace {
void require_project_access(bool condition, std::source_location source = std::source_location::current()) {
    INFO("Project access expectation at " << source.line());
    REQUIRE(condition);
}
}

TEST_CASE("Shared export dialog opens from non-Compose tabs and stamps the chosen project group",
          "[unit][frontend-state][event-harness][project-access]") {
    const auto tab = GENERATE(WORM_UI_WORKSPACE_TAB_SETUP, WORM_UI_WORKSPACE_TAB_METADATA,
        WORM_UI_WORKSPACE_TAB_COMPOSE, WORM_UI_WORKSPACE_TAB_AUDIO_SLICES,
        WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE);
    INFO("Project tab " << tab);
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view();
    view.active_tab = tab;
    view.setup.visible = tab == WORM_UI_WORKSPACE_TAB_SETUP;
    view.setup.stepmania_policy_status = "Ready";
    view.metadata.visible = tab == WORM_UI_WORKSPACE_TAB_METADATA;
    view.compose.visible = tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    view.audio_slices.visible = tab == WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
    view.compare.visible = tab == WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE;
    const WormUiComposeOutputGroupView group{.document_index = 7, .name = "Project export",
        .relative_output_path = "exports", .relative_asset_directory = "exports"};
    view.compose.output_groups = &group;
    view.compose.output_group_count = 1;
    view.setup.chart_count = 1;
    (void)harness.frame();
    if (tab == WORM_UI_WORKSPACE_TAB_SETUP) {
        const auto export_button = harness.drawn_text_center("Export…");
        require_project_access(export_button.has_value());
        const auto frames = click_at(harness, export_button->first, export_button->second);
        require_project_access(std::ranges::count_if(frames, [](const auto& frame) {
            return std::string_view{frame.get().menu_command_id} == "document.export_output_group";
        }) == 1);
    } else if (tab == WORM_UI_WORKSPACE_TAB_COMPOSE) {
        require_project_access(harness.drawn_text().find("Export…") == std::string::npos);
    }
    require_project_access(!worm_ui_frontend_open_project_export(view.project_id + 1,
        view.selected_chart_id, view.document_revision));
    require_project_access(worm_ui_frontend_open_project_export(view.project_id,
        view.selected_chart_id, view.document_revision));
    (void)harness.frame();
    (void)harness.frame();
    INFO(harness.drawn_text());
    require_project_access(harness.drawn_text().find("Project export") != std::string::npos);
    const auto button = harness.drawn_text_center("Export group");
    require_project_access(button.has_value());
    const auto frames = click_at(harness, button->first, button->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
    });
    require_project_access(found != frames.end());
    const auto& action = found->get();
    check_layer_menu(action.compose_output_group_index == 7);
    check_layer_menu(action.compose_input_project_id == view.project_id);
    check_layer_menu(action.compose_input_chart_id == view.selected_chart_id);
    check_layer_menu(action.compose_input_document_revision == view.document_revision);
}

TEST_CASE("Metadata wheel scroll leaves its embedded waveform zoom and audio offset unchanged",
          "[unit][frontend-state][event-harness][project-access]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
    view.compose.visible = false;
    auto& metadata = view.metadata;
    metadata.visible = metadata.has_chart = metadata.audio_waveform_has_selected_asset = true;
    metadata.chart_id = metadata.document_revision = metadata.audio_waveform_selected_asset_id = 1;
    metadata.audio_offset_denominator = 1;
    metadata.audio_waveform_available = true;
    metadata.audio_waveform_total_frames = 441000;
    metadata.audio_waveform_sample_rate = 44100;
    const std::array<WormUiAudioWaveformColumnView, 2> columns{};
    metadata.audio_waveform_columns = columns.data();
    metadata.audio_waveform_column_count = columns.size();
    (void)harness.frame();
    for (const auto heading : {"Song"}) {
        if (const auto center = harness.drawn_text_center(heading))
            (void)click_at(harness, center->first, center->second);
    }
    (void)harness.frame();
    auto& audio = worm_ui_nuklear_audio_state();
    const auto before = audio.waveform_viewport.frames_per_pixel;
    require_project_access(audio.waveform_asset_id == 1);
    require_project_access(audio.waveform_viewport.total_frames == 441000);
    const auto labels = harness.drawn_text_bounds();
    const auto overview = std::ranges::find_if(labels, [](const auto& label) {
        return std::string_view{label.text}.find("Stereo overview") != std::string_view::npos;
    });
    require_project_access(overview != labels.end());
    const float wheel_y = overview->y + 80.0f;
    require_project_access(wheel_y < 840);
    SDL_Event wheel{};
    wheel.type = wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -5;
    wheel.wheel.mouse_x = 640;
    wheel.wheel.mouse_y = wheel_y;
    const std::array events{motion_event(640, wheel_y), wheel};
    const auto frame = harness.frame(events);
    check_layer_menu(audio.waveform_viewport.frames_per_pixel == before);
    check_layer_menu(frame.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    check_layer_menu(frame.get().metadata_edit == WORM_UI_METADATA_EDIT_NONE);
    (void)harness.frame();
    check_layer_menu(harness.drawn_text().find("Audio and export") != std::string::npos);
}

namespace {
struct output_group_fixture_t {
    nuklear_event_harness_t harness{1280, 1000};
    std::array<WormUiProjectChartRow, 2> charts{{
        {.chart_id = 10, .name = "First difficulty", .author = "Author", .rating = "Unset", .mode = "7K", .output_groups = "SSC", .length = "1 s", .diagnostic = ""},
        {.chart_id = 11, .name = "Second difficulty", .author = "Author", .rating = "Unset", .mode = "7K", .output_groups = "Unassigned", .length = "1 s", .diagnostic = ""}}};
    const uint64_t included{10};
    WormUiProjectOutputGroupView group{.document_index = 3, .name = "SSC release", .family = 2,
        .path = "exports/song.ssc", .chart_ids = &included, .chart_count = 1, .preview_status = "Preview not run"};
    explicit output_group_fixture_t(bool existing) {
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
        view.compose.visible = false;
        view.setup.visible = true;
        view.setup.song_id = view.project_id;
        view.setup.document_revision = view.document_revision;
        view.setup.session_generation = 8;
        view.setup.selected_chart_id = 10;
        view.setup.charts = charts.data();
        view.setup.chart_count = charts.size();
        view.setup.chart_row_count = charts.size();
        view.setup.output_groups = existing ? &group : nullptr;
        view.setup.output_group_count = existing ? 1 : 0;
        view.setup.stepmania_policy_status = "Ready";
        (void)harness.frame();
    }
    auto click(const char* label) {
        const auto point = harness.drawn_text_center(label);
        require_project_access(point.has_value());
        return click_at(harness, point->first, point->second);
    }
};
}

TEST_CASE("Project output groups remain visible when empty and create a stamped group with selected membership",
          "[frontend-state][event-harness][output-groups]") {
    output_group_fixture_t f{false};
    require_project_access(f.harness.drawn_text().find("Output groups · 0") != std::string::npos);
    require_project_access(f.harness.drawn_text_center("Export…").has_value());
    (void)f.click("New group");
    (void)f.harness.frame();
    const auto frames = f.click("Save group");
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_CREATE;
    });
    require_project_access(found != frames.end());
    const auto& action = found->get().project_output_group;
    require_project_access(action.project_id == f.harness.view().project_id);
    require_project_access(action.document_revision == f.harness.view().document_revision);
    require_project_access(action.session_generation == 8);
    require_project_access(std::string_view{action.name} == "New output group");
    require_project_access(std::string_view{action.path} == "exports/chart.bmson");
    require_project_access(action.family == 4 && action.chart_count == 1 && action.chart_ids[0] == 10);
}

TEST_CASE("Project output group editor updates checkbox membership and confirms deletion",
          "[frontend-state][event-harness][output-groups]") {
    output_group_fixture_t f{true};
    (void)f.click("Edit");
    (void)f.harness.frame();
    (void)f.click("Second difficulty");
    const auto saved = f.click("Save group");
    const auto found = std::ranges::find_if(saved, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE;
    });
    require_project_access(found != saved.end());
    const auto& action = found->get().project_output_group;
    require_project_access(action.group_index == 3 && action.family == 2);
    require_project_access(action.chart_count == 2 && action.chart_ids[0] == 10 && action.chart_ids[1] == 11);
    const auto disabled = f.click("Delete group");
    require_project_access(std::ranges::none_of(disabled, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_DELETE;
    }));
    (void)f.click("Confirm group deletion");
    const auto deleted = f.click("Delete group");
    require_project_access(std::ranges::any_of(deleted, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_DELETE &&
            frame.get().project_output_group.group_index == 3;
    }));
}

TEST_CASE("Project output group rows expose worker previews and the shared export action",
          "[frontend-state][event-harness][output-groups]") {
    output_group_fixture_t f{true};
    const auto preview = f.click("Preview");
    require_project_access(std::ranges::any_of(preview, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW &&
            frame.get().project_output_group.group_index == 3;
    }));
    const auto exported = f.click("Export");
    const auto found = std::ranges::find_if(exported, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
    });
    require_project_access(found != exported.end());
    const auto& action = found->get();
    require_project_access(action.compose_output_group_index == 3);
    require_project_access(action.compose_input_project_id == f.harness.view().project_id);
    require_project_access(action.compose_input_document_revision == f.harness.view().document_revision);
    require_project_access(action.compose_input_chart_id == 10);
    require_project_access(!action.compose_export_overwrite);
}

TEST_CASE("Double-clicking a stem note arms it without opening its label editor",
          "[frontend-state][event-harness][scratchpad-stem-arm]") {
    const auto kind = GENERATE(0, 1, 2);
    nuklear_event_harness_t harness{1280, 900};
    section_import_require(harness.ready());
    auto& view = harness.view().compose;
    const std::array motion{motion_event(300, 400)};
    const auto hover = harness.frame(motion);
    section_import_require(hover.get().compose_has_pointer_hover);
    const auto x = hover.get().compose_pointer_hover_x_pixels;
    const auto y = hover.get().compose_pointer_hover_y_pixels;
    WormUiComposeNoteView note{};
    note.bounds = {x - 15, y - 5, x + 15, y + 5};
    note.note_id = 5;
    note.lane_id = 1;
    note.sound_resource_id = 40;
    note.sound_label = "01";
    note.sound_detail = "stem.wav";
    note.sound_is_stem = kind != 0;
    note.sound_missing = kind == 2;
    view.note_endpoints = &note;
    view.note_endpoint_count = 1;
    (void)harness.frame();
    const auto clicks = click_at(harness, 300, 400, true);
    const auto* source = find_compose_input(clicks, WORM_UI_COMPOSE_INPUT_DRAW_SOURCE);
    const auto* label = find_compose_input(clicks, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK);
    if (kind == 1) {
        section_import_require(source != nullptr);
        expect_draw(source->resource_id == 40);
        expect_draw(!label);
        view.draw_source_kind = 1;
        view.draw_source_id = 40;
        const auto& state = worm_ui_nuklear_compose_state();
        expect_draw(worm_ui_nuklear_armed_dock_sound(state, view) == 40);
        view.draw_source_missing = true;
        expect_draw(worm_ui_nuklear_armed_dock_sound(state, view) == 0);
    } else {
        expect_draw(!source);
        expect_draw(label != nullptr);
    }
}

TEST_CASE("Compose reports the canvas width left uncovered by side docks",
          "[frontend-state][event-harness][scratchpad-specialstyle]") {
    nuklear_event_harness_t harness{1600, 900};
    section_import_require(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = true;
    auto covered = harness.frame();
    expect_draw(covered.get().compose_input_editor_right_pixels > 0);
    expect_draw(covered.get().compose_input_editor_right_pixels <
                covered.get().compose_input_viewport_width_pixels);
    state.inspector_visible = false;
    state.sounds_panel_visible = false;
    state.layers_panel_visible = false;
    state.bga_panel_visible = false;
    state.measures_panel_visible = false;
    const auto open = harness.frame();
    expect_draw(open.get().compose_input_editor_right_pixels > covered.get().compose_input_editor_right_pixels);
    expect_draw(open.get().compose_input_editor_right_pixels <= open.get().compose_input_viewport_width_pixels);
}

TEST_CASE("SpecialStyle rightmost note remains pickable with side docks open",
          "[frontend-state][event-harness][scratchpad-specialstyle]") {
    auto song = t422_song();
    auto profile = make_osu_mania_profile(16, true);
    section_import_require(profile.has_value());
    song.charts.front().lane_layouts.front().layout = profile->layout;
    song.charts.front().input_profile = profile->input;
    song.charts.front().measures.front().notes[1].lane = lane_id_t{16};
    t422_fixture_t fixture{std::move(song)};
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    auto& ui = worm_ui_nuklear_compose_state();
    ui.inspector_visible = true;
    ui.sounds_panel_visible = true;
    for (int frame = 0; frame < 3; ++frame) {
        fixture.project();
        (void)fixture.dispatch(fixture.harness.frame());
    }
    fixture.project();
    const auto center = t422_note_screen_center(fixture, 101);
    section_import_require(center.has_value());
    for (const auto& frame : click_at(fixture.harness, center->first, center->second))
        (void)fixture.dispatch(frame);
    expect_draw(fixture.interaction->selected_notes == std::vector{object_id_t{101}});
}

TEST_CASE("BGM header mute and solo buttons capture press and release over canvas tools",
          "[frontend-state][event-harness][bgm-mix]") {
    const auto solo = GENERATE(false, true);
    const auto stem = GENERATE(false, true);
    nuklear_event_harness_t harness{1600, 900};
    section_import_require(harness.ready());
    auto& view = harness.view().compose;
    WormUiComposeBgmLaneView lane{};
    lane.bounds = {300, 0, 372, view.viewport_height_pixels};
    lane.source_lane_id = 77;
    lane.stem = stem;
    lane.filename = stem ? "音楽stem.wav" : nullptr;
    view.bgm_lanes = &lane;
    view.bgm_lane_count = 1;
    view.bgm_mix_available = true;
    view.note_paint_mode_active = true;
    (void)harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    section_import_require(viewport.has_value());
    const auto rect = compose_bgm_mix_button_rect(lane, view,
        nk_rect(viewport->x, viewport->y, viewport->width, viewport->height), solo);
    const auto frames = click_at(harness, rect.x + rect.w / 2, rect.y + rect.h / 2);
    const auto* input = find_compose_input(frames,
        solo ? WORM_UI_COMPOSE_INPUT_BGM_SOLO : WORM_UI_COMPOSE_INPUT_BGM_MUTE);
    section_import_require(input != nullptr);
    expect_draw(input->lane_id == 77);
    expect_draw(frames.front().get().compose_input_count == 1);
    expect_draw(frames.back().get().compose_input_count == 0);
    expect_draw(!worm_ui_nuklear_compose_state().bgm_mix_pointer_active);
    view.bgm_mix_available = false;
    const auto disabled = click_at(harness, rect.x + rect.w / 2, rect.y + rect.h / 2);
    expect_draw(disabled.front().get().compose_input_count == 0);
    expect_draw(disabled.back().get().compose_input_count == 0);
}


TEST_CASE("Double-clicking a BGM stem head arms it outside the playable field",
          "[frontend-state][event-harness][scratchpad-stem-arm]") {
    const auto kind = GENERATE(0, 1, 2, 3);
    bgm_draw_fixture_t fixture{true, true};
    auto& view = fixture.harness.view().compose;
    view.note_paint_mode_active = false;
    view.playable_field_right_pixels = fixture.lane_x;
    WormUiComposeBgmEventView event{};
    event.bounds = {fixture.lane_x + 2, 100, fixture.lane_x + 38, 400};
    event.head_top_pixels = 190;
    event.head_bottom_pixels = 206;
    event.has_tail = true;
    event.event_id = 73;
    event.source_lane_id = 71;
    event.has_sound = true;
    event.sound_id = 40;
    event.armable_stem = kind != 1;
    event.proposal_ghost = kind == 2;
    view.bgm_events = &event;
    view.bgm_event_count = 1;
    (void)fixture.harness.frame();
    const auto [x, y] = fixture.point(20, kind == 3 ? 300 : 198);
    (void)click_at(fixture.harness, x, y);
    const auto clicks = click_at(fixture.harness, x, y, true);
    const auto* source = find_compose_input(clicks, WORM_UI_COMPOSE_INPUT_DRAW_SOURCE);
    expect_draw((source != nullptr) == (kind == 0));
    expect_draw(!find_compose_input(clicks, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK));
    if (source) expect_draw(source->resource_id == 40);
}


TEST_CASE("File Save project can queue through its own menu release capture",
          "[frontend-state][event-harness][save-shortcut][save-menu]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view();
    view.project_has_native_path = true;
    view.menu_command_count = 1;
    view.menu_commands[0] = {.id = "document.save_project", .enabled = true};
    (void)harness.frame();
    const auto file = harness.drawn_text_center("File");
    section_import_require(file.has_value());
    (void)click_at(harness, file->first, file->second);
    const auto save = harness.drawn_text_center("Save project");
    section_import_require(save.has_value());
    const auto frames = click_at(harness, save->first, save->second);
    section_import_require(std::ranges::any_of(frames, [](const auto& frame) {
        return std::string_view{frame.get().menu_command_id} == "document.save_project";
    }));
    expect_draw(worm_ui_frontend_request_save_project(view.project_id));
    expect_draw(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
}

TEST_CASE("Rejected Alt duration release retires the production gesture and preserves the document",
          "[frontend-state][event-harness][gesture-registry][duration]") {
    auto song = t422_song();
    song.charts.front().measures.front().notes.push_back({.id = object_id_t{104},
        .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}});
    t422_fixture_t f{std::move(song)};
    f.interaction->selected_notes = {object_id_t{102}};
    f.interaction->selected_objects = {{editor_selection_object_kind_t::note, object_id_t{102}}};
    f.project();
    (void)f.harness.frame();
    const auto head = t422_note_screen_center(f, 102);
    const auto target = t422_note_screen_center(f, 104);
    section_import_require(head.has_value());
    section_import_require(target.has_value());
    const auto original = t422_native_bytes(f.history.document);
    const auto alt = key_event(SDL_EVENT_KEY_DOWN, SDLK_LALT, SDL_SCANCODE_LALT);
    (void)f.dispatch(f.harness.frame(std::span{&alt, 1}, SDL_KMOD_ALT));
    const std::array press{motion_event(head->first, head->second),
        button_event(true, head->first, head->second)};
    (void)f.dispatch(f.harness.frame(press, SDL_KMOD_ALT));
    section_import_require(f.interaction->pending_note_duration_gesture.has_value());
    const auto move = motion_event(target->first, target->second);
    (void)f.dispatch(f.harness.frame(std::span{&move, 1}, SDL_KMOD_ALT));
    const auto release = button_event(false, target->first, target->second);
    (void)f.dispatch(f.harness.frame(std::span{&release, 1}, SDL_KMOD_ALT));
    expect_draw(!has_pending_editor_gesture(*f.interaction));
    expect_draw(f.history.undo.empty());
    expect_draw(t422_native_bytes(f.history.document) == original);
}

TEST_CASE("A production lane chord survives its own revision refresh but cancels an external edit",
          "[frontend-state][event-harness][gesture-registry][lane-key]") {
    auto song = t422_song();
    for (auto& measure : song.charts.front().measures) measure.notes.clear();
    t422_fixture_t f{std::move(song)};
    f.interaction->selected_notes.clear();
    f.interaction->selected_objects.clear();
    f.application.note_paint_mode_active = true;
    f.interaction->armed_note_kind = note_kind_t::hold;
    f.project();
    (void)f.harness.frame();
    const auto first = key_event(SDL_EVENT_KEY_DOWN, SDLK_1, SDL_SCANCODE_1);
    const auto second = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    (void)f.dispatch(f.harness.frame(std::span{&first, 1}));
    (void)f.dispatch(f.harness.frame(std::span{&second, 1}));
    section_import_require(f.interaction->additional_keyboard_holds.size() == 1);
    const auto forward = key_event(SDL_EVENT_KEY_DOWN, SDLK_UP, SDL_SCANCODE_UP);
    (void)f.dispatch(f.harness.frame(std::span{&forward, 1}));
    const auto first_up = key_event(SDL_EVENT_KEY_UP, SDLK_1, SDL_SCANCODE_1);
    (void)f.dispatch(f.harness.frame(std::span{&first_up, 1}));
    section_import_require(f.interaction->pending_hold.has_value());
    f.project();
    (void)f.dispatch(f.harness.frame());
    expect_draw(f.interaction->pending_hold.has_value());
    auto changed = f.history.document;
    changed.metadata.title = "External edit";
    section_import_require(static_cast<bool>(commit_document_command(f.history, std::move(changed), "External edit")));
    // Stale captures can temporarily make scene projection unavailable; pass that real view
    // through the frontend rather than requiring a healthy scene before cancellation.
    f.flattened = {};
    populate_compose_view(f.flattened, f.history, *f.binding.workspace, f.cache, 1800, 1400,
                          f.interaction, f.application, nullptr);
    f.harness.view().compose = f.flattened.view.compose;
    f.harness.view().document_revision = f.history.document.revision;
    (void)f.dispatch(f.harness.frame());
    expect_draw(!has_pending_editor_gesture(*f.interaction));
    f.project();
    const auto second_up = key_event(SDL_EVENT_KEY_UP, SDLK_2, SDL_SCANCODE_2);
    (void)f.dispatch(f.harness.frame(std::span{&second_up, 1}));
    expect_draw(f.history.undo.size() == 2);
}

TEST_CASE("Production area Delete enablement agrees with its scoped dispatch",
          "[frontend-state][event-harness][gesture-delete][area]") {
    const bool timing = GENERATE(false, true);
    note_menu_fixture_t fixture;
    auto song = t422_song();
    if (timing) song.charts.front().bpms.push_back({.id = object_id_t{400},
        .position = {measure_id_t{1}, 240}, .beats_per_minute = {150, 1}});
    auto history = make_document_history(std::move(song));
    const std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 120}, snaps, 4);
    section_import_require(setup.state.has_value());
    auto state = *setup.state;
    state.retained_selection_range = position_span_t{{measure_id_t{1}, 120}, {measure_id_t{2}, 0}};
    const auto& chart = history.document.charts.front();
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    auto& view = fixture.harness.view().compose;
    view.project_id = history.document.id.value; view.chart_id = state.chart.value;
    view.document_revision = history.document.revision;
    view.area_range_available = true; view.area_rows_per_measure = 960; view.area_canvas_rows = 9600;
    view.area_range_begin_row = 120; view.area_range_end_row = 960;
    view.area_delete_supported = compose_area_delete_supported(chart, index, state);
    expect_draw(view.area_delete_supported == !timing);
    expect_draw(view.area_delete_supported == compose_area_delete_supported(history, state));
    (void)fixture.harness.frame();
    WormUiTestingRect viewport{};
    section_import_require(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    const auto y = viewport.y + static_cast<float>(compose_area_y(view, 540) *
        viewport.height / view.viewport_height_pixels);
    (void)right_click_at(fixture.harness, 900, y);
    (void)fixture.harness.frame();
    const auto target = fixture.harness.drawn_text_center("Delete contents");
    section_import_require(target.has_value());
    const auto actions = click_at(fixture.harness, target->first, target->second);
    const auto emitted = std::ranges::find_if(actions, [](const auto& snapshot) {
        return snapshot.get().compose_action == WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    });
    expect_draw((emitted != actions.end()) == !timing);
    if (emitted == actions.end()) return;
    const auto status = dispatch_compose_area_action(emitted->get(), history, state);
    section_import_require(status.has_value());
    expect_draw(*status == "Selected area contents deleted.");
    expect_draw(history.undo.size() == 1);
    expect_draw(history.document.charts.front().measures.front().notes.empty());
}

TEST_CASE("Area Quantize activates the registered verb instead of a fallback grid",
          "[frontend-state][event-harness][gesture-verbs]") {
    note_menu_fixture_t f;
    auto& v = f.harness.view().compose;
    v.area_range_available = true;
    v.area_rows_per_measure = 960;
    v.area_canvas_rows = v.area_range_end_row = 9600;
    (void)f.harness.frame();
    (void)right_click_at(f.harness, 900, 400);
    const auto point = f.harness.drawn_text_center("Quantize selection");
    REQUIRE(point);
    bool emitted{};
    for (const auto& frame : click_at(f.harness, point->first, point->second))
        if (std::string_view{frame.get().menu_command_id} == "edit.apply_quantize") {
            emitted = true;
            auto action = frame.get();
            auto history = wormedit::make_document_history(t422_song());
            const auto binding = wormedit::make_workspace_controller(history);
            auto workspace = *binding.workspace;
            workspace.active_tab = wormedit::workspace_tab_t::compose;
            check_layer_menu(action.compose_menu_command);
            check_layer_menu(wormedit::sys::compose_menu_command_is_current(action, history, workspace));
            ++history.document.revision;
            check_layer_menu(!wormedit::sys::compose_menu_command_is_current(action, history, workspace));
            --history.document.revision;
            action.shortcut_ime_composition_active = true;
            check_layer_menu(!wormedit::sys::compose_menu_command_is_current(action, history, workspace));
        }
    check_layer_menu(emitted);
}

TEST_CASE("Timing focus projects the same Move to BGM capability as its command",
          "[frontend-state][event-harness][gesture-verbs]") {
    t422_fixture_t f{t424_song()};
    expect_t422(f.harness.view().compose.move_to_bgm_available);
    f.application.compose_timing_focus = true;
    f.project();
    (void)f.harness.frame();
    expect_t422(!f.harness.view().compose.move_to_bgm_available);
}

TEST_CASE("Measure insert buttons emit the registered guarded verb",
          "[frontend-state][event-harness][gesture-verbs]") {
    nuklear_event_harness_t harness{1600, 2600};
    worm_ui_nuklear_compose_state().inspector_visible = true;
    (void)harness.frame();
    const auto tree = harness.drawn_text_center("Measure at cursor");
    REQUIRE(tree);
    (void)click_at(harness, tree->first, tree->second);
    const auto point = harness.drawn_text_center("Insert measure before");
    REQUIRE(point);
    bool emitted{};
    for (const auto& frame : click_at(harness, point->first, point->second))
        emitted |= std::string_view{frame.get().menu_command_id} == "measures.insert_before";
    check_layer_menu(emitted);
}

TEST_CASE("Actual same-sound clicks reject a note without a sound without clearing selection",
          "[frontend-state][event-harness][gesture-verbs]") {
    auto song = t422_song();
    for (auto& note : song.charts.front().measures.front().notes)
        if (note.id == wormedit::object_id_t{101}) note.sound.reset();
    t422_fixture_t f{std::move(song)};
    const auto selected = f.interaction->selected_notes;
    const auto point = t422_note_screen_center(f, 101);
    REQUIRE(point);
#if defined(__APPLE__)
    const auto primary = SDL_KMOD_GUI;
#else
    const auto primary = SDL_KMOD_CTRL;
#endif
    const auto mods = static_cast<SDL_Keymod>(primary | SDL_KMOD_SHIFT);
    const std::array down{motion_event(point->first, point->second), button_event(true, point->first, point->second)};
    const auto pressed = f.harness.frame(down, mods);
    const std::array up{button_event(false, point->first, point->second)};
    const auto released = f.harness.frame(up, mods);
    const auto& action = pressed.get().compose_input_count ? pressed : released;
    expect_t422(action.get().compose_input_count == 1);
    expect_t422(action.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE);
    expect_t422(f.dispatch(action) == "Hover a note or select a sound first");
    expect_t422(f.interaction->selected_notes == selected);
    expect_t422(f.history.undo.empty());
}

namespace {
auto placement_point(t422_fixture_t& f, std::uint64_t row, std::uint64_t lane)
    -> std::pair<float, float> {
    (void)f.harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    expect_t422(viewport.has_value());
    const auto& view = f.harness.view().compose;
    const auto lanes = std::span{view.lanes, view.lane_count};
    const auto found = std::ranges::find(lanes, lane, &WormUiComposeLaneView::lane_id);
    expect_t422(found != lanes.end());
    const auto x = (found->bounds.left + found->bounds.right) / 2;
    const auto y = row * f.interaction->pixels_per_standard_measure / 960.0 -
                   f.interaction->viewport_scroll_pixels;
    return {viewport->x + static_cast<float>(x * viewport->width / view.viewport_width_pixels),
            viewport->y + static_cast<float>((view.downscroll ? view.viewport_height_pixels - y : y) * viewport->height / view.viewport_height_pixels)};
}

void placement_press(t422_fixture_t& f, std::pair<float, float> point) {
    const std::array events{motion_event(point.first, point.second),
        button_event(true, point.first, point.second)};
    (void)f.dispatch(f.harness.frame(events));
}

void placement_release(t422_fixture_t& f, std::pair<float, float> point) {
    const std::array events{motion_event(point.first, point.second),
        button_event(false, point.first, point.second)};
    (void)f.dispatch(f.harness.frame(events));
}

void prepare_placement(t422_fixture_t& f) {
    f.interaction->selected_notes.clear();
    f.interaction->selected_objects.clear();
    f.application.note_paint_mode_active = true;
    f.project();
    (void)f.harness.frame();
}
}

TEST_CASE("Production paint release outside the field commits its visited cells",
          "[frontend-state][event-harness][row-placement]") {
    t422_fixture_t f{t422_song()};
    prepare_placement(f);
    placement_press(f, placement_point(f, 120, 2));
    expect_t422(f.interaction->pending_note_paint_gesture.has_value());
    placement_release(f, {1790, 1390});
    expect_t422(f.history.undo.size() == 1);
    expect_t422(!(has_pending_editor_gesture(*f.interaction)));
}

TEST_CASE("Production paint captures kind and layers at its beginning",
          "[frontend-state][event-harness][row-placement]") {
    t422_fixture_t f{t422_song()};
    f.application.semantic_layer_target_none = true;
    prepare_placement(f);
    const auto point = placement_point(f, 120, 2);
    placement_press(f, point);
    expect_t422(f.interaction->pending_note_paint_gesture.has_value());
    f.interaction->armed_note_kind = note_kind_t::mine;
    f.application.semantic_layer_target_none = false;
    f.application.semantic_layer_target = layer_id_t{7};
    placement_release(f, point);
    const auto& notes = f.history.document.charts.front().measures.front().notes;
    const auto placed = std::ranges::find(notes, lane_id_t{2}, &note_t::lane);
    expect_t422(placed != notes.end());
    expect_t422(placed->kind == note_kind_t::tap);
    expect_t422(placed->semantic_layers.empty());
}

TEST_CASE("Pause event rows permit production pointer and lane-key placement at every zoom",
          "[frontend-state][event-harness][row-placement]") {
    const auto kind = GENERATE(pause_kind_t::stop, pause_kind_t::delay);
    const auto pixels = GENERATE(192.0, 768.0);
    const auto keyboard = GENERATE(false, true);
    auto song = t422_song();
    song.charts.front().pauses.push_back({.id = object_id_t{200},
        .position = {measure_id_t{1}, 240}, .kind = kind, .duration = {1, 2}});
    t422_fixture_t f{std::move(song)};
    f.interaction->pixels_per_standard_measure = pixels;
    f.interaction->cursor = {measure_id_t{1}, 240};
    prepare_placement(f);
    if (keyboard) {
        (void)t422_lane_press(f, SDL_KMOD_NONE);
    } else {
        const auto point = placement_point(f, 240, 2);
        placement_press(f, point);
        placement_release(f, point);
    }
    const auto& notes = f.history.document.charts.front().measures.front().notes;
    const auto placed = std::ranges::find(notes, lane_id_t{2}, &note_t::lane);
    expect_t422(placed != notes.end());
    expect_t422(placed->position == local_position_t{measure_id_t{1}, 240});
    expect_t422(f.history.undo.size() == 1);
}

TEST_CASE("Virtual Hold and Roll clicks share the authored gesture owner and commit once",
          "[frontend-state][event-harness][row-placement]") {
    const auto kind = GENERATE(note_kind_t::hold, note_kind_t::roll);
    const auto same_row = GENERATE(false, true);
    auto song = t424_song();
    if (kind == note_kind_t::roll) {
        song.charts.front().source_family = format_family_t::native_project;
        song.charts.front().virtual_tail_measures = 1000;
    }
    t422_fixture_t f{std::move(song)};
    f.interaction->armed_note_kind = kind;
    prepare_placement(f);
    const auto point = placement_point(f, 2400, 2);
    for (const auto& frame : click_at(f.harness, point.first, point.second))
        (void)f.dispatch(frame);
    expect_t422(f.interaction->pending_hold.has_value());
    expect_t422(f.history.undo.empty());
    expect_t422(f.history.document.charts.front().measures.size() == 2);
    f.project();
    const auto end = placement_point(f, same_row ? 2400 : 2640, 2);
    for (const auto& frame : click_at(f.harness, end.first, end.second))
        (void)f.dispatch(frame);
    expect_t422(!(has_pending_editor_gesture(*f.interaction)));
    expect_t422(f.history.undo.size() == (same_row ? 0 : 1));
    if (!same_row) {
        const auto& notes = f.history.document.charts.front().measures.back().notes;
        expect_t422(notes.size() == 1);
        expect_t422(notes.front().kind == kind);
        expect_t422(notes.front().end == local_position_t{measure_id_t{3}, 720});
        expect_t422(static_cast<bool>(undo_document_command(f.history)));
        expect_t422(f.history.document.charts.front().measures.size() == 2);
    }
}

TEST_CASE("Virtual lane keys reject same-row Hold and Roll without publishing or retaining a gesture",
          "[frontend-state][event-harness][row-placement]") {
    const auto kind = GENERATE(note_kind_t::hold, note_kind_t::roll);
    auto song = t424_song();
    if (kind == note_kind_t::roll) {
        song.charts.front().source_family = format_family_t::native_project;
        song.charts.front().virtual_tail_measures = 1000;
    }
    t422_fixture_t f{std::move(song)};
    f.interaction->armed_note_kind = kind;
    f.interaction->virtual_bms_cursor_row = 2400;
    prepare_placement(f);
    (void)t422_lane_press(f, SDL_KMOD_NONE);
    expect_t422(f.history.undo.empty());
    expect_t422(f.history.document.charts.front().measures.size() == 2);
    expect_t422(!(has_pending_editor_gesture(*f.interaction)));
}

namespace {
auto placement_stem_song() -> song_document_t {
    auto song = t422_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    for (auto& measure : chart.measures) measure.notes.clear();
    song.resources.front().sound_kind = sound_kind_t::stem;
    song.resources.front().roles.push_back(resource_role_t::bgm);
    chart.sound_channels = {{.id = object_id_t{300},
        .source_clip = {clip_id_t{300}, asset_id_t{1}, 0, std::nullopt},
        .events = {{.id = object_id_t{400}, .position = {measure_id_t{1}, 0}, .playable = false}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{600}, .stem_channel = object_id_t{300}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{500},
        .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{600},
        .sound = resource_id_t{1}, .sound_channel_event = object_id_t{400}}};
    return song;
}
}

TEST_CASE("Production click-click stem holds retain decoded geometry through release",
          "[frontend-state][event-harness][row-placement]") {
    const auto short_source = GENERATE(false, true);
    t422_fixture_t f{placement_stem_song()};
    f.interaction->armed_note_kind = note_kind_t::hold;
    f.application.draw_source_kind = 1;
    f.application.draw_source_id = 1;
    prepare_placement(f);
    auto& ui = worm_ui_nuklear_compose_state();
    ui.armed_resource_id = 1;
    ui.armed_from_sounds_dock = true;
    f.cache.stem_index.built = true;
    f.cache.stem_index.document = f.history.document.id;
    f.cache.stem_index.chart = chart_id_t{1};
    f.cache.stem_index.revision = f.history.document.revision;
    f.cache.stem_index.bindings = {{.asset = asset_id_t{1}, .source_sample_rate = 48000,
        .source_frame_count = short_source ? 1000U : 480000U}};
    const auto start = placement_point(f, 240, 2);
    for (const auto& frame : click_at(f.harness, start.first, start.second))
        (void)f.dispatch(frame);
    expect_t422(f.interaction->pending_hold.has_value());
    expect_t422(f.interaction->pending_hold->source_geometry != nullptr);
    f.project();
    f.cache.stem_index.bindings.clear();
    const auto end = placement_point(f, 480, 2);
    for (const auto& frame : click_at(f.harness, end.first, end.second))
        (void)f.dispatch(frame);
    const auto& chart = f.history.document.charts.front();
    expect_t422(chart.measures.front().notes.size() == 1);
    expect_t422(chart.measures.front().notes.front().sound_channel_event.has_value());
    expect_t422(chart.measures.front().notes.front().end == local_position_t{measure_id_t{1}, 480});
    expect_t422(chart.sound_channels.front().events.back().continuation == !short_source);
    expect_t422(f.history.undo.size() == 1);
    expect_t422(!(has_pending_editor_gesture(*f.interaction)));
    expect_t422(f.history.document.audio_assets.front().stream.sample_rate == 0);
}

TEST_CASE("Production virtual lane chords survive independent materialization and releases",
          "[frontend-state][event-harness][row-placement]") {
    t422_fixture_t f{t424_song()};
    f.interaction->armed_note_kind = note_kind_t::hold;
    f.interaction->virtual_bms_cursor_row = 2400;
    prepare_placement(f);
    const auto first = key_event(SDL_EVENT_KEY_DOWN, SDLK_1, SDL_SCANCODE_1);
    const auto second = key_event(SDL_EVENT_KEY_DOWN, SDLK_2, SDL_SCANCODE_2);
    (void)f.dispatch(f.harness.frame(std::span{&first, 1}));
    (void)f.dispatch(f.harness.frame(std::span{&second, 1}));
    expect_t422(f.interaction->additional_keyboard_holds.size() == 1);
    const auto forward = key_event(SDL_EVENT_KEY_DOWN, SDLK_UP, SDL_SCANCODE_UP);
    (void)f.dispatch(f.harness.frame(std::span{&forward, 1}));
    const auto first_up = key_event(SDL_EVENT_KEY_UP, SDLK_1, SDL_SCANCODE_1);
    (void)f.dispatch(f.harness.frame(std::span{&first_up, 1}));
    expect_t422(f.interaction->pending_hold.has_value());
    expect_t422(f.history.undo.size() == 1);
    f.project();
    const auto second_up = key_event(SDL_EVENT_KEY_UP, SDLK_2, SDL_SCANCODE_2);
    (void)f.dispatch(f.harness.frame(std::span{&second_up, 1}));
    expect_t422(f.history.undo.size() == 2);
    expect_t422(!(has_pending_editor_gesture(*f.interaction)));
    expect_t422(f.history.document.charts.front().measures.back().notes.size() == 2);
}

TEST_CASE("Production unsnapped paint walks adjacent rows with the shared grid path",
          "[frontend-state][event-harness][row-placement]") {
    t422_fixture_t f{t422_song()};
    f.application.edit_snap_enabled = false;
    f.interaction->pixels_per_standard_measure = 960;
    prepare_placement(f);
    placement_press(f, placement_point(f, 240, 2));
    const auto end = placement_point(f, 248, 2);
    const auto motion = motion_event(end.first, end.second);
    (void)f.dispatch(f.harness.frame(std::span{&motion, 1}));
    placement_release(f, end);
    std::vector<std::uint64_t> rows;
    for (const auto& note : f.history.document.charts.front().measures.front().notes)
        if (note.lane == lane_id_t{2}) rows.push_back(note.position.row);
    std::ranges::sort(rows);
    expect_t422(rows.size() >= 2);
    for (std::size_t i = 1; i < rows.size(); ++i) expect_t422(rows[i] == rows[i - 1] + 1);
    expect_t422(f.history.undo.size() == 1);
}

TEST_CASE("Production pointer paste and note drag use the valid Stop event row",
          "[frontend-state][event-harness][row-placement]") {
    const auto paste = GENERATE(false, true);
    auto song = t422_song();
    song.charts.front().pauses.push_back({.id = object_id_t{200},
        .position = {measure_id_t{1}, 240}, .duration = {1, 2}});
    t422_fixture_t f{std::move(song)};
    f.interaction->selected_notes = {object_id_t{101}};
    f.interaction->selected_objects = {{editor_selection_object_kind_t::note, object_id_t{101}}};
    if (paste) {
        auto copied = copy_chart_selection(f.history.document, *f.interaction, f.history.document.revision);
        expect_t422(static_cast<bool>(copied));
        f.application.chart_clipboard = std::move(copied.clipboard);
        std::string status;
        expect_t422(begin_compose_paste(f.application, f.history.document, *f.interaction, status));
        f.project();
        const auto target = placement_point(f, 240, 1);
        for (const auto& frame : click_at(f.harness, target.first, target.second))
            (void)f.dispatch(frame);
    } else {
        f.project();
        const auto source = t422_note_screen_center(f, 101);
        expect_t422(source.has_value());
        placement_press(f, *source);
        const auto target = placement_point(f, 240, 1);
        const auto motion = motion_event(target.first, target.second);
        (void)f.dispatch(f.harness.frame(std::span{&motion, 1}));
        placement_release(f, target);
    }
    expect_t422(f.history.undo.size() == 1);
    const auto& notes = f.history.document.charts.front().measures.front().notes;
    expect_t422(std::ranges::any_of(notes, [](const auto& note) {
        return note.position == local_position_t{measure_id_t{1}, 240} && note.lane == lane_id_t{1};
    }));
    expect_t422(static_cast<bool>(undo_document_command(f.history)));
    expect_t422(t422_note(f.history, 101)->position == local_position_t{measure_id_t{1}, 120});
}

namespace {
auto timing_pointer(t422_fixture_t& f, WormUiTimingLaneKind lane, std::uint64_t row,
                    double lane_fraction = .5) -> std::pair<float, float> {
    (void)f.harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    expect_t422(viewport.has_value());
    const auto& view = f.harness.view().compose;
    const auto extent = compose_timing_lane_inner_extent(view, lane);
    expect_t422(extent.has_value());
    const auto x = extent->first + (extent->second - extent->first) * lane_fraction;
    const auto y = row * f.interaction->pixels_per_standard_measure / 960.0 -
                   f.interaction->viewport_scroll_pixels;
    return {viewport->x + static_cast<float>(x * viewport->width / view.viewport_width_pixels),
            viewport->y + static_cast<float>((view.downscroll ? view.viewport_height_pixels - y : y) *
                                             viewport->height / view.viewport_height_pixels)};
}
}

TEST_CASE("Production timing span release edits once and rejected release retires capture",
          "[frontend-state][event-harness][drag-capture]") {
    const auto locked_at_release = GENERATE(false, true);
    const auto downscroll = GENERATE(false, true);
    auto song = t422_song();
    song.charts.front().speeds.push_back({.id = object_id_t{220},
        .position = {measure_id_t{1}, 240}, .multiplier = {2, 1}, .transition_beats = {1, 1}});
    t422_fixture_t f{std::move(song)};
    f.interaction->selected_objects.clear(); f.interaction->selected_notes.clear();
    f.interaction->pixels_per_standard_measure = 768;
    f.application.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    f.project();
    const auto before = t422_native_bytes(f.history.document);
    const auto p = timing_pointer(f, WORM_UI_TIMING_LANE_SPEED, 480);
    placement_press(f, p);
    expect_t422(worm_ui_nuklear_compose_state().kiai_drag_active);
    if (locked_at_release) f.application.timing_locked = true;
    // Do not reproject here: exercise the application's rejection after a real UI release.
    const auto end = timing_pointer(f, WORM_UI_TIMING_LANE_SPEED, 720);
    placement_release(f, end);
    expect_t422(!worm_ui_nuklear_compose_state().kiai_drag_active);
    expect_t422(!worm_ui_nuklear_compose_state().span_capture.active);
    expect_t422(f.history.undo.size() == (locked_at_release ? 0 : 1));
    if (locked_at_release) expect_t422(t422_native_bytes(f.history.document) == before);
    else {
        const auto& speed = f.history.document.charts.front().speeds.front();
        expect_t422(speed.position == local_position_t{measure_id_t{1}, 240});
        expect_t422(speed.transition_beats == rational_t{2, 1});
        expect_t422(static_cast<bool>(undo_document_command(f.history)));
        expect_t422(f.history.document.charts.front().speeds.front().transition_beats == rational_t{1, 1});
    }
}

TEST_CASE("Speed marker heads translate while tail handles resize through production input",
          "[frontend-state][event-harness][drag-capture]") {
    auto song = t422_song();
    song.charts.front().speeds.push_back({.id = object_id_t{220},
        .position = {measure_id_t{1}, 240}, .multiplier = {2, 1}, .transition_beats = {1, 1}});
    t422_fixture_t f{std::move(song)};
    f.interaction->selected_objects.clear(); f.interaction->selected_notes.clear();
    f.interaction->pixels_per_standard_measure = 768;
    f.project();
    const auto p = timing_pointer(f, WORM_UI_TIMING_LANE_SPEED, 240);
    placement_press(f, p);
    const auto& s = worm_ui_nuklear_compose_state();
    expect_t422(s.timing_marker_candidate_active && !s.kiai_drag_active);
    f.project();
    const auto end_point = timing_pointer(f, WORM_UI_TIMING_LANE_SPEED, 480);
    const auto released = f.harness.frame(std::array{motion_event(end_point.first, end_point.second),
        button_event(false, end_point.first, end_point.second)});
    const auto status = f.dispatch(released);
    INFO(status);
    CAPTURE(released.get().compose_input_count, f.interaction->selected_objects.size(),
            s.timing_marker_candidate_active, f.history.document.revision);
    expect_t422(f.history.undo.size() == 1);
    const auto& speed = f.history.document.charts.front().speeds.front();
    expect_t422(speed.position == local_position_t{measure_id_t{1}, 480});
    expect_t422(speed.transition_beats == rational_t{1, 1});
    expect_t422(!s.timing_marker_capture.active);
}
