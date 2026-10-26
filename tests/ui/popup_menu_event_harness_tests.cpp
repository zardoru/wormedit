#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_note_menu.h"
#include "ui/nuklear/compose/compose_state.h"
#include "sys/commands/menu_command_view.h"

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

void seed_note_menu(nuklear_event_harness_t& harness, wormedit::ui::compose_hold_view_t& hold,
                    std::array<wormedit::ui::compose_semantic_layer_view_t, 2>& layers) {
    auto& view = harness.view().compose;
    view.hold_bodies = {hold};
    view.note_menu_layers.assign(layers.begin(), layers.end());
    view.note_menu_layers.resize(layers.size());
    view.note_menu_layer_total_count = layers.size();
    (void)harness.frame();
}
} // namespace

TEST_CASE("Popup menu Escape and outside click release input without replay",
          "[frontend-state][event-harness][popup-lifecycle]") {
    nuklear_event_harness_t harness{1280, 900};
    require_popup(harness.ready());
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {7}, .name = "Lead", .visible = true},
        {.layer_id = {9}, .name = "Locked", .visible = true, .locked = true}}};
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
    check_popup(escaped.get().compose_inputs.size() == 0);
    check_popup(escaped.get().compose_viewport_inputs.size() == 0);
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
        check_popup(frame.get().compose_inputs.size() == 0);
        check_popup(frame.get().compose_viewport_inputs.size() == 0);
    }
    check_popup(close_actions == 1);
    (void)harness.frame();
    (void)right_click(harness, 300, 300);
    check_popup(compose_note_menu_open());
    const auto coalesced = coalesced_click(harness, 1100, 820);
    check_popup(!compose_note_menu_open());
    check_popup(coalesced.get().compose_action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
    check_popup(coalesced.get().compose_inputs.size() == 0);
    check_popup(coalesced.get().compose_viewport_inputs.size() == 0);
}

TEST_CASE("Global menus dismiss on Escape and right outside click and switch by title",
          "[frontend-state][event-harness][popup-lifecycle]") {
    nuklear_event_harness_t harness{1280, 900};
    require_popup(harness.ready());
    auto& view = harness.view();
    view.compose.clipboard_copy_available = true;
    wormedit::sys::command_shortcut_text_t menu_shortcuts_0;
    wormedit::sys::build_menu_commands_view(wormedit::command_registry_t::from({}), view, menu_shortcuts_0);
    (void)harness.frame();

    const auto file_title = harness.drawn_text_center("File");
    require_popup(file_title.has_value());
    (void)click(harness, file_title->first, file_title->second);
    require_popup(harness.drawn_text().find("Open chart or project…") != std::string::npos);

    const auto edit_title = harness.drawn_text_center("Edit");
    require_popup(edit_title.has_value());
    (void)click(harness, edit_title->first, edit_title->second);
    INFO("title-switch frame text: " << harness.drawn_text());
    require_popup(harness.drawn_text().find("Copy") != std::string::npos);
    require_popup(harness.drawn_text().find("Open chart or project…") == std::string::npos);

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
        check_popup(frame.get().compose_inputs.size() == 0);
        check_popup(frame.get().compose_viewport_inputs.size() == 0);
    }
}

namespace {
auto context_key_event(SDL_Keycode key, SDL_Scancode scan, bool down = true,
                       SDL_Keymod modifiers = SDL_KMOD_NONE, bool repeat = false) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = key;
    event.key.scancode = scan;
    event.key.down = down;
    event.key.mod = modifiers;
    event.key.repeat = repeat;
    return event;
}
auto press_context_key(nuklear_event_harness_t& harness, SDL_Keycode key, SDL_Scancode scan,
                       SDL_Keymod modifiers = SDL_KMOD_NONE) -> workspace_action_snapshot_t {
    const auto down = context_key_event(key, scan, true, modifiers);
    auto action = harness.frame(std::span{&down, 1}, modifiers);
    const auto up = context_key_event(key, scan, false, modifiers);
    (void)harness.frame(std::span{&up, 1}, modifiers);
    return action;
}
void prepare_keyboard_note(nuklear_event_harness_t& harness, wormedit::ui::compose_hold_view_t& hold,
                           std::array<wormedit::ui::compose_semantic_layer_view_t, 2>& layers) {
    hold.selected = true;
    seed_note_menu(harness, hold, layers);
    harness.view().compose.selected_note_count = 1;
    harness.view().compose.keyboard_context_note_id.value = hold.note_id.value;
    worm_ui_nuklear_compose_state().input_queue.keyboard_focus = true;
    (void)harness.frame();
}
}

TEST_CASE("Menu and Shift F10 open selected note context without pointer hover",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {7}, .name = "Lead", .visible = true},
        {.layer_id = {9}, .name = "Locked", .visible = true, .locked = true}}};
    prepare_keyboard_note(harness, hold, layers);
    harness.view().compose.selected_note_count = 2;
    harness.view().compose.hold_bodies.clear();
    const auto opened = press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(opened.get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
    check_popup(opened.get().compose_note_context_id.value == 42);
    check_popup(opened.get().shortcut_events.size() == 0 && opened.get().compose_inputs.size() == 0);
    check_popup(compose_note_menu_open());
    const auto closed = press_context_key(harness, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    check_popup(closed.get().compose_action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
    check_popup(!compose_note_menu_open());
    (void)harness.frame();
    const auto shifted = press_context_key(harness, SDLK_F10, SDL_SCANCODE_F10, SDL_KMOD_LSHIFT);
    check_popup(shifted.get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
}

TEST_CASE("Note Layer submenu supports arrows disabled rows Space and closing key capture",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {7}, .name = "Lead", .visible = true},
        {.layer_id = {9}, .name = "Locked", .visible = true, .locked = true}}};
    prepare_keyboard_note(harness, hold, layers);
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    (void)press_context_key(harness, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
    check_popup(harness.drawn_text().find("No layer") != std::string::npos);
    (void)press_context_key(harness, SDLK_LEFT, SDL_SCANCODE_LEFT);
    check_popup(harness.drawn_text().find("Select same sound (visible)") != std::string::npos);
    (void)press_context_key(harness, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
    (void)press_context_key(harness, SDLK_END, SDL_SCANCODE_END);
    const auto space = context_key_event(SDLK_SPACE, SDL_SCANCODE_SPACE);
    const auto assigned = harness.frame(std::span{&space, 1});
    check_popup(assigned.get().compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER);
    check_popup(assigned.get().compose_semantic_layer_id.value == 7);
    check_popup(assigned.get().shortcut_events.size() == 0 && assigned.get().compose_inputs.size() == 0);
    const auto repeat = context_key_event(SDLK_SPACE, SDL_SCANCODE_SPACE, true, SDL_KMOD_NONE, true);
    const auto repeated = harness.frame(std::span{&repeat, 1});
    check_popup(repeated.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_popup(repeated.get().shortcut_events.size() == 0 && repeated.get().compose_inputs.size() == 0);
    const auto release = context_key_event(SDLK_SPACE, SDL_SCANCODE_SPACE, false);
    const auto released = harness.frame(std::span{&release, 1});
    check_popup(released.get().shortcut_events.size() == 0 && released.get().compose_inputs.size() == 0);
}

TEST_CASE("Context invocation rejects IME modals lost focus and stale revisions",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {7}, .name = "Lead", .visible = true},
        {.layer_id = {9}, .name = "Locked", .visible = true, .locked = true}}};
    prepare_keyboard_note(harness, hold, layers);
    harness.view().options.visible = true;
    (void)harness.frame();
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(!compose_note_menu_open());
    harness.view().options.visible = false;
    (void)harness.frame();
    SDL_Event ime{};
    ime.type = SDL_EVENT_TEXT_EDITING;
    ime.edit.type = SDL_EVENT_TEXT_EDITING;
    ime.edit.text = "composing";
    (void)harness.frame(std::span{&ime, 1});
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(!compose_note_menu_open());
    ime.edit.text = "";
    (void)harness.frame(std::span{&ime, 1});
    worm_ui_nuklear_compose_state().input_queue.keyboard_focus = true;
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(compose_note_menu_open());
    ++harness.view().compose.document_revision;
    (void)harness.frame();
    check_popup(!compose_note_menu_open());
}

TEST_CASE("Opening project modals retires context menus and keeps closing keys captured",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {7}, .name = "Lead", .visible = true},
        {.layer_id = {9}, .name = "Locked", .visible = true, .locked = true}}};
    prepare_keyboard_note(harness, hold, layers);
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(compose_note_menu_open());
    const auto held = context_key_event(SDLK_DOWN, SDL_SCANCODE_DOWN);
    (void)harness.frame(std::span{&held, 1});
    SECTION("Options") { harness.view().options.visible = true; }
    SECTION("Unsaved changes") { harness.view().close_confirmation.visible = true; }
    SECTION("Chart import") { harness.view().chart_import.visible = true; }
    (void)harness.frame();
    check_popup(!compose_note_menu_open());
    const auto up = context_key_event(SDLK_DOWN, SDL_SCANCODE_DOWN, false);
    const auto released = harness.frame(std::span{&up, 1});
    check_popup(released.get().shortcut_events.size() == 0 && released.get().compose_inputs.size() == 0);
    const auto reopening = press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(!compose_note_menu_open());
    check_popup(reopening.get().compose_action != WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU);
}

TEST_CASE("Context menu row focus clears across project tab and focus changes",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {7}, .name = "Lead", .visible = true},
        {.layer_id = {9}, .name = "Locked", .visible = true, .locked = true}}};
    prepare_keyboard_note(harness, hold, layers);
    namespace chrome = wormedit::ui::chrome;
    chrome::context_menu_focus(chrome::KeyboardContextTarget::layers, 7);
    ++harness.view().project_id.value;
    (void)harness.frame();
    check_popup(chrome::context_menu_focused_target() == chrome::KeyboardContextTarget::none);
    check_popup(chrome::context_menu_focused_object() == 0);
    worm_ui_nuklear_compose_state().input_queue.keyboard_focus = true;
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(compose_note_menu_open());
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    lost.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)harness.frame(std::span{&lost, 1});
    check_popup(!compose_note_menu_open());
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(!compose_note_menu_open());
}

TEST_CASE("Project chart keyboard menu opens the focused row and respects Options",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::project_chart_row_view_t row{.chart_id = wormedit::chart_id_t{10}, .name = "Keyboard chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""};
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    workspace.compose.visible = false;
    auto& setup = workspace.setup;
    setup.visible = true;
    setup.song_id = wormedit::song_id_t{1};
    setup.session_generation = 1;
    setup.charts = {row};
    setup.chart_count = 1;
    (void)harness.frame();
    const auto name = harness.drawn_text_center("Keyboard chart");
    require_popup(name.has_value());
    (void)click(harness, name->first, name->second);
    const auto opened = press_context_key(harness, SDLK_F10, SDL_SCANCODE_F10, SDL_KMOD_RSHIFT);
    check_popup(opened.get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    check_popup(harness.drawn_text_center("Duplicate").has_value());
    (void)press_context_key(harness, SDLK_HOME, SDL_SCANCODE_HOME);
    const auto activated = press_context_key(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_popup(activated.get().project_chart.kind == WORM_UI_PROJECT_CHART_OPEN);
    check_popup(activated.get().project_chart.chart_id == wormedit::chart_id_t{10});
    (void)harness.frame();
    workspace.options.visible = true;
    (void)harness.frame();
    (void)press_context_key(harness, SDLK_F10, SDL_SCANCODE_F10, SDL_KMOD_RSHIFT);
    check_popup(!harness.drawn_text_center("Open in Compose").has_value());
    workspace.options.visible = false;
    ++workspace.project_id.value;
    ++setup.song_id.value;
    ++setup.session_generation;
    (void)harness.frame();
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    check_popup(!harness.drawn_text_center("Duplicate").has_value());
}

TEST_CASE("Keyboard Layer End loads a distant window and skips its locked last row",
          "[frontend-state][event-harness][keyboard-context]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_hold_view_t hold{.bounds = {200, 0, 500, 900}, .note_id = {42}, .lane_id = {1}};
    std::array<wormedit::ui::compose_semantic_layer_view_t, 2> layers{{
        {.layer_id = {1}, .name = "First layer", .visible = true},
        {.layer_id = {2}, .name = "Second layer", .visible = true}}};
    prepare_keyboard_note(harness, hold, layers);
    auto& view = harness.view().compose;
    view.note_menu_layer_total_count = 200;
    (void)press_context_key(harness, SDLK_APPLICATION, SDL_SCANCODE_APPLICATION);
    (void)press_context_key(harness, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
    const auto request = press_context_key(harness, SDLK_END, SDL_SCANCODE_END);
    check_popup(request.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW);
    const auto first = request.get().compose_semantic_layer_first_row;
    require_popup(first > wormedit::ui::note_layer_window_limit && first < 200);
    std::vector<wormedit::ui::compose_semantic_layer_view_t> window(200 - first);
    for (std::size_t index = 0; index < window.size(); ++index)
        window[index] = {.layer_id = {first + index + 1}, .name = "Distant layer", .visible = true};
    window.back().locked = true;
    view.note_menu_layers.assign(window.begin(), window.end());
    view.note_menu_layer_first_row = first;
    view.note_menu_layers.resize(window.size());
    (void)harness.frame();
    const auto assigned = press_context_key(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_popup(assigned.get().compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER);
    check_popup(assigned.get().compose_semantic_layer_id.value == 199);
}
