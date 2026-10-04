#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/compose_area_menu.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <source_location>
#include <utility>

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

void check_number_key_placement(bool paint_mode) {
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
    (void)harness.frame();
    for (std::size_t index = 0; index < keys.size(); ++index) {
        INFO("paint mode " << paint_mode << ", lane " << index + 1);
        const auto press = key_event(SDL_EVENT_KEY_DOWN, index == 9 ? SDLK_0 : SDLK_1 + index, keys[index]);
        const auto down = harness.frame(std::span{&press, 1});
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

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(x, y), button_event(true, x, y)};
    script[1].events = {motion_event(x, y), button_event(false, x, y)};
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
    const auto frames = click_at(harness, 760, overwrite ? 330 : 298);
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

TEST_CASE("Headless 10K number keys complete placement with Select and Tap tools",
          "[frontend-state][event-harness][lane-keys][regression]") {
    check_number_key_placement(false);
    check_number_key_placement(true);
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

    // The wizard is centered at (230,258), 500x204. Its last row begins at y=366;
    // the button centers are in the two 242px columns at y=406.
    constexpr float button_y = 406.0f;
    const auto cancel_actions = click_at(harness, 350.0f, button_y);
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

    const auto create_actions = click_at(harness, 610.0f, 406.0f);
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
    // 24 px toolbar icons: navigation, separator, five tools, separator, views.
    constexpr std::array views{View::snap, View::layer, View::normal};
    constexpr std::array positions{288.0f, 315.0f, 261.0f};
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto clicks = click_at(harness, positions[index], 122.0f);
        CHECK(worm_ui_nuklear_compose_state().color_view == views[index]);
        check_no_toolbar_document_actions(clicks);
        CHECK(harness.view().compose.document_revision == revision);
    }
}

namespace {
void check_layer_row_action(const workspace_action_snapshot_t& snapshot,
                            WormUiComposeActionKind kind, std::uint64_t layer) {
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
    const auto chosen = click_at(harness, 1060, 233);
    check_layer_row_action(chosen.back(), WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER, 7);
    layers.front().selected = true;
    view.semantic_layer_target = layers.front();
    (void)harness.frame();
    const auto eye = click_at(harness, 1200, 263);
    check_layer_row_action(eye[0], WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, 9);
    const auto lock = click_at(harness, 1225, 263);
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
    const auto clicked = click_at(harness, 1060, 203);
    check_layer_row_action(clicked.back(), WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER, 0);
    CHECK(view.document_revision == revision);
    view.semantic_layer_target_none = true;
    (void)harness.frame();
    const auto no_eye = click_at(harness, 1200, 203);
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

void check_layer_menu_no_actions(const std::vector<workspace_action_snapshot_t>& frames) {
    for (const auto& frame : frames) {
        check_layer_menu(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        check_layer_menu(frame.get().compose_input_count == 0);
        check_layer_menu(frame.get().compose_viewport_input_count == 0);
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
        const auto clicks = right_click_at(harness, 1060, 233 + static_cast<float>(index) * 30);
        check_layer_row_action(clicks.front(), WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER,
                               layers[index].layer_id);
        layers[index].selected = true;
        harness.view().compose.semantic_layer_target = layers[index];
        (void)harness.frame();
        check_layer_menu(worm_ui_nuklear_compose_state().layers_menu_open);
    }
};

} // namespace

TEST_CASE("Layer row menu hides its captured target without assigning selected notes",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    const auto hide = click_at(fixture.harness, 1100, 305);
    check_layer_row_action(hide.front(), WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, 7);
    fixture.layers.front().visible = false;
    fixture.harness.view().compose.semantic_layer_target = fixture.layers.front();
    ++fixture.harness.view().compose.document_revision;
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
    const auto eye = click_at(fixture.harness, 1200, 233);
    check_layer_row_action(eye.front(), WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, 7);
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
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
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
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_release_pending);
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
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
    check_layer_menu(closed.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    const std::array release{key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    (void)fixture.harness.frame(release);
    fixture.open();
    const auto outside = click_at(fixture.harness, 1200, 820);
    check_layer_menu_no_actions(outside);
    for (const auto& frame : outside)
        check_layer_menu(frame.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
}

TEST_CASE("Layer row menu releases its popup after a stale revision or hidden panel",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    ++fixture.harness.view().compose.document_revision;
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
    fixture.open();
    worm_ui_nuklear_compose_state().layers_panel_visible = false;
    (void)fixture.harness.frame();
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_open);
    check_layer_menu(!worm_ui_nuklear_compose_state().layers_menu_close_pending);
    (void)click_at(fixture.harness, 288, 122);
    check_layer_menu(worm_ui_nuklear_compose_state().color_view ==
                     worm_ui_nuklear_compose_color_view_t::snap);
}

TEST_CASE("Locked layer row menu disables Rename and Color",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open(1);
    const auto rename = click_at(fixture.harness, 1100, 275);
    check_layer_menu_no_actions(rename);
    check_layer_menu(worm_ui_nuklear_compose_state().layers_menu_open);
    check_layer_menu(!worm_ui_nuklear_compose_state().semantic_layer_name_editor_visible);
    check_layer_menu(!worm_ui_nuklear_compose_state().semantic_layer_color_editor_visible);
}

TEST_CASE("Layer row Rename editor keeps its captured ID after target and window changes",
          "[unit][frontend-state][frontend][event-harness]") {
    layer_menu_fixture_t fixture;
    fixture.open();
    const auto rename = click_at(fixture.harness, 1100, 249);
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
    (void)click_at(fixture.harness, 288, 122);
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
}

TEST_CASE("Unavailable overview tooltip stays on its row and preserves panel tooltips",
          "[unit][frontend-state][frontend][event-harness][tooltip]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& compose = harness.view().compose;
    compose.has_bgm_events = true;
    compose.bgm_bar_reserved = true;
    compose.bgm_bar_available = false;
    (void)harness.frame();
    hover_at(harness, 381, 174);
    check_tooltip_text(harness, "Hide sounds list");
    check_no_tooltip_text(harness, "Audio is unavailable for this chart overview.");
    hover_at(harness, 408, 174);
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
    hover_at(harness, 460, 122);
    check_tooltip_text(harness, "Song audio is unavailable");
    const auto clicked = click_at(harness, 460, 122);
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
}
TEST_CASE("Clicking selected and unselected notes emits one audition selection",
          "[unit][frontend-state][event-harness][audition][regression]") {
    check_note_click_emitted_once(false);
    check_note_click_emitted_once(true);
}

namespace {
void check_bga_dock(bool value) { CHECK(value); }
void check_sound_slot_hidden(const std::string& drawn_text) {
    CHECK(drawn_text.find("0A  kick.wav") == std::string::npos);
}
}

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
    const auto actions = click_at(harness, state.sound_list_x + 70, state.sound_list_y + 110);
    CHECK_FALSE(std::ranges::any_of(actions, [](const auto& action) {
        return action.get().compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    }));
    CHECK(state.armed_resource_id == 1400);
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
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.bga_panel_visible = true;
    (void)harness.frame();
    REQUIRE(state.bga_list_valid);
    check_bga_dock(harness.drawn_text().find("clip.png") != std::string::npos);
    check_bga_dock(harness.drawn_text().find("01  clip.png") == std::string::npos);
    INFO("BGA bounds " << state.bga_list_x << "," << state.bga_list_y << "," << state.bga_list_width << "," << state.bga_list_height);
    (void)click_at(harness, state.bga_list_x + 70, state.bga_list_y + 112);
    check_bga_dock(state.bga_selection.primary == 1400);
    check_bga_dock(state.armed_resource_id == 1400);
    check_bga_dock(state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga);
    (void)click_at(harness, state.bga_list_x + 22, state.bga_list_y + 43);
    check_bga_dock(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place);
    view.bga_rows = rows.data() + 1; view.bga_row_count = 1; view.bga_first_row = 1;
    (void)harness.frame();
    check_bga_dock(state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place);
    state.bga_panel_visible = false;
    (void)harness.frame();
    CHECK_FALSE(state.bga_list_valid);
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
}

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
    const auto frames = click_at(harness, 760, 298);
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
    frames = click_at(harness, 760, 330);
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
    const auto frames = click_at(harness, 760, 298);
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
    const auto frames = click_at(harness, 760, 298);
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
auto find_compose_input(const std::vector<workspace_action_snapshot_t>& frames,
                        WormUiComposeInputKind kind) -> const WormUiComposeInputAction* {
    for (const auto& frame : frames)
        for (size_t index = 0; index < frame.get().compose_input_count; ++index)
            if (frame.get().compose_inputs[index].kind == kind)
                return &frame.get().compose_inputs[index];
    return nullptr;
}
}

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

    // A missing or non-dock sound never rides along; the stroke stays silent.
    state.armed_resource_id = 50;
    frames = click_at(harness, 300, 400);
    begin = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin != nullptr);
    CHECK(begin->resource_id == 0);
    state.armed_resource_id = 40;
    state.armed_from_sounds_dock = false;
    frames = click_at(harness, 300, 400);
    begin = find_compose_input(frames, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN);
    REQUIRE(begin != nullptr);
    CHECK(begin->resource_id == 0);
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
    (void)click_at(harness, state.sound_list_x + 70, state.sound_list_y + 165);
    check_sound_slots(state.keysound_selected_slot == 2);
    check_sound_slots(state.armed_resource_id == 501);
    check_sound_slots(state.keysound_selection.primary == 501);
    stage_sound_slot(harness);
    check_sound_slots(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_slots(state.keysound_slot_confirmation_open);
    check_sound_slots(harness.drawn_text().find("Replace occupied WAV slot 02") != std::string::npos);
    const auto frames = click_at(harness, 760, 298);
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
    (void)click_at(harness, state.sound_list_x + 70, state.sound_list_y + 196);
    check_sound_slots(state.keysound_selected_slot == 3);
    check_sound_slots(state.armed_resource_id == 0);
    state.armed_resource_id = 501;
    (void)click_at(harness, state.sound_list_x + 70, state.sound_list_y + 134);
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
