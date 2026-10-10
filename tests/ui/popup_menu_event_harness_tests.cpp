#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/compose_state.h"
#include "sys/menu_command_projection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <source_location>
#include <string_view>

namespace {
auto button(bool pressed, std::uint8_t button_id, float x, float y) -> SDL_Event {
    SDL_Event event{};
    const auto type = pressed ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.type = type;
    event.button.type = type;
    event.button.button = button_id;
    event.button.down = pressed;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const std::array down{motion(x, y), button(true, SDL_BUTTON_LEFT, x, y)};
    const std::array up{motion(x, y), button(false, SDL_BUTTON_LEFT, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

auto right_click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const std::array down{motion(x, y), button(true, SDL_BUTTON_RIGHT, x, y)};
    const std::array up{motion(x, y), button(false, SDL_BUTTON_RIGHT, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

auto click_button(nuklear_event_harness_t& harness, std::uint8_t button_id, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const std::array down{motion(x, y), button(true, button_id, x, y)};
    const std::array up{motion(x, y), button(false, button_id, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

auto coalesced_click(nuklear_event_harness_t& harness, float x, float y)
    -> workspace_action_snapshot_t {
    const std::array events{motion(x, y), button(true, SDL_BUTTON_LEFT, x, y),
                            button(false, SDL_BUTTON_LEFT, x, y)};
    return harness.frame(events);
}

auto escape() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.down = true;
    return event;
}

void check_popup(bool condition, const std::source_location location =
                                      std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

void require_popup(bool condition, const std::source_location location =
                                       std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

void seed_note_menu(nuklear_event_harness_t& harness, WormUiComposeHoldView& hold,
                    std::array<WormUiComposeSemanticLayerView, 2>& layers) {
    auto& view = harness.view().compose;
    view.hold_bodies = &hold;
    view.hold_body_count = 1;
    view.note_menu_layers = layers.data();
    view.note_menu_layer_count = layers.size();
    view.note_menu_layer_total_count = layers.size();
    (void)harness.frame();
}
} // namespace

TEST_CASE("Popup menu Escape and outside click release input without replay",
          "[frontend-state][event-harness][popup-lifecycle]") {
    nuklear_event_harness_t harness{1280, 900};
    require_popup(harness.ready());
    WormUiComposeHoldView hold{.bounds = {200, 0, 500, 900}, .note_id = 42, .lane_id = 1};
    std::array<WormUiComposeSemanticLayerView, 2> layers{{
        {.layer_id = 7, .name = "Lead", .visible = true},
        {.layer_id = 9, .name = "Locked", .visible = true, .locked = true}}};
    seed_note_menu(harness, hold, layers);

    const auto opened = right_click(harness, 300, 300);
    check_popup(opened.front().get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    check_popup(compose_note_menu_open());
    const auto submenu = harness.drawn_text_center("Layer");
    require_popup(submenu.has_value());
    (void)click(harness, submenu->first, submenu->second);
    (void)harness.frame();
    check_popup(harness.drawn_text().find("Lead") != std::string_view::npos);

    const auto escape_event = escape();
    const auto escaped = harness.frame(std::span{&escape_event, 1});
    check_popup(!compose_note_menu_open());
    check_popup(escaped.get().compose_input_count == 0);
    check_popup(escaped.get().compose_viewport_input_count == 0);
    const auto escape_release = escape_event;
    auto key_up = escape_release;
    key_up.type = SDL_EVENT_KEY_UP;
    key_up.key.type = SDL_EVENT_KEY_UP;
    key_up.key.down = false;
    (void)harness.frame(std::span{&key_up, 1});
    (void)harness.frame();

    (void)right_click(harness, 300, 300);
    check_popup(compose_note_menu_open());
    const auto outside = click(harness, 1100, 820);
    check_popup(!compose_note_menu_open());
    std::size_t close_actions = 0;
    for (const auto& frame : outside) {
        const auto action = frame.get().compose_action;
        check_popup(action == WORM_UI_COMPOSE_ACTION_NONE ||
                    action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
        close_actions += action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU ? 1U : 0U;
        check_popup(frame.get().compose_input_count == 0);
        check_popup(frame.get().compose_viewport_input_count == 0);
    }
    check_popup(close_actions == 1);
    (void)harness.frame();
    (void)right_click(harness, 300, 300);
    check_popup(compose_note_menu_open());
    const auto coalesced = coalesced_click(harness, 1100, 820);
    check_popup(!compose_note_menu_open());
    check_popup(coalesced.get().compose_action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
    check_popup(coalesced.get().compose_input_count == 0);
    check_popup(coalesced.get().compose_viewport_input_count == 0);
}

TEST_CASE("Global menus dismiss on Escape and right outside click and switch by title",
          "[frontend-state][event-harness][popup-lifecycle]") {
    nuklear_event_harness_t harness{1280, 900};
    require_popup(harness.ready());
    auto& view = harness.view();
    view.compose.clipboard_copy_available = true;
    wormedit::sys::project_menu_commands(wormedit::make_default_editor_command_registry({}), view);
    (void)harness.frame();

    const auto file_title = harness.drawn_text_center("File");
    require_popup(file_title.has_value());
    (void)click(harness, file_title->first, file_title->second);
    require_popup(harness.drawn_text().find("Open chart or project...") != std::string::npos);

    const auto edit_title = harness.drawn_text_center("Edit");
    require_popup(edit_title.has_value());
    (void)click(harness, edit_title->first, edit_title->second);
    INFO("title-switch frame text: " << harness.drawn_text());
    require_popup(harness.drawn_text().find("Copy") != std::string::npos);
    require_popup(harness.drawn_text().find("Open chart or project...") == std::string::npos);

    const auto copy = harness.drawn_text_center("Copy");
    require_popup(copy.has_value());
    const auto selected = click(harness, copy->first, copy->second);
    check_popup(std::string_view{selected.front().get().menu_command_id} == "edit.copy" ||
                std::string_view{selected.back().get().menu_command_id} == "edit.copy");
    check_popup(!(std::string_view{selected.front().get().menu_command_id} == "edit.copy" &&
                  std::string_view{selected.back().get().menu_command_id} == "edit.copy"));

    const auto escape_event = escape();
    const auto escaped = harness.frame(std::span{&escape_event, 1});
    check_popup(escaped.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    check_popup(escaped.get().clipboard == WORM_UI_CLIPBOARD_NONE);
    auto key_up = escape_event;
    key_up.type = SDL_EVENT_KEY_UP;
    key_up.key.type = SDL_EVENT_KEY_UP;
    key_up.key.down = false;
    (void)harness.frame(std::span{&key_up, 1});
    (void)harness.frame();

    const auto reopened = harness.drawn_text_center("Edit");
    require_popup(reopened.has_value());
    (void)click(harness, reopened->first, reopened->second);
    const auto dismissed = click_button(harness, SDL_BUTTON_RIGHT, 1200, 820);
    for (const auto& frame : dismissed) {
        check_popup(frame.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
        check_popup(frame.get().clipboard == WORM_UI_CLIPBOARD_NONE);
        check_popup(frame.get().compose_input_count == 0);
        check_popup(frame.get().compose_viewport_input_count == 0);
    }
}
