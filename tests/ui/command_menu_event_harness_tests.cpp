#include "nuklear_event_harness.h"
#include "ui/nuklear/app/command_menu.h"
#include "ui/nuklear/compose/compose_state.h"
#include <wormedit/core/command_registry.h>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cmath>
#include <source_location>
#include <string_view>

namespace {
void expect_menu(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Command menu expectation at " << at.line());
    REQUIRE(condition);
}
auto key(SDL_Keycode code, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down; event.key.key = code; event.key.scancode = scan;
    return event;
}
struct fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    fixture_t() {
        auto& view = harness.view();
        view.command_menu_rows.resize(4);
        view.command_menu_rows[0] = {.id = "disabled", .label = "Disabled alpha", .enabled = false};
        view.command_menu_rows[1] = {.id = "first", .label = "First alpha", .enabled = true};
        view.command_menu_rows[2] = {.id = "second", .label = "Second beta", .enabled = true};
        view.command_menu_rows[3] = {.id = "last", .label = "Last alpha", .enabled = true};
        (void)harness.frame();
    }
    void open() {
        expect_menu(worm_ui_frontend_open_command_menu());
        (void)harness.frame(); (void)harness.frame();
    }
    auto press(SDL_Keycode code, SDL_Scancode scan) -> workspace_action_snapshot_t {
        const auto event = key(code, scan);
        auto result = harness.frame({&event, 1});
        const auto up = key(code, scan, false);
        (void)harness.frame({&up, 1});
        return result;
    }
    void type(const char* text) {
        SDL_Event event{}; event.type = SDL_EVENT_TEXT_INPUT; event.text.text = text;
        (void)harness.frame({&event, 1});
    }
};
}

TEST_CASE("Command menu searches all labels case-insensitively and dispatches enabled selection",
          "[frontend-state][event-harness][command-menu]") {
    fixture_t fixture;
    fixture.open();
    expect_menu(fixture.harness.drawn_text().find("Disabled alpha") != std::string::npos);
    fixture.type("BETA");
    expect_menu(fixture.harness.drawn_text().find("Second beta") != std::string::npos);
    expect_menu(fixture.harness.drawn_text().find("First alpha") == std::string::npos);
    const auto result = fixture.press(SDLK_RETURN, SDL_SCANCODE_RETURN);
    expect_menu(result.get().command_menu_invoked);
    expect_menu(std::string_view{result.get().command_menu_command_id} == "second");
    expect_menu(result.get().compose_input_project_id.value == 1);
    expect_menu(result.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Command menu skips disabled rows and Escape never invokes or leaks its release",
          "[frontend-state][event-harness][command-menu]") {
    fixture_t fixture;
    fixture.open();
    (void)fixture.press(SDLK_UP, SDL_SCANCODE_UP);
    const auto result = fixture.press(SDLK_RETURN, SDL_SCANCODE_RETURN);
    expect_menu(std::string_view{result.get().command_menu_command_id} == "last");
    (void)fixture.harness.frame();
    fixture.open();
    const auto escape = fixture.press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    expect_menu(!escape.get().command_menu_invoked);
    expect_menu(escape.get().compose_inputs.size() == 0);
    expect_menu(fixture.harness.drawn_text().find("Command menu") == std::string::npos);
}

TEST_CASE("Command menu cancels stale contexts and modal ownership",
          "[frontend-state][event-harness][command-menu]") {
    for (int change = 0; change < 3; ++change) {
        fixture_t fixture;
        fixture.open();
        if (change == 0) ++fixture.harness.view().document_revision;
        else if (change == 1) fixture.harness.view().options.visible = true;
        else {
            SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            (void)fixture.harness.frame({&lost, 1});
        }
        (void)fixture.harness.frame();
        const auto result = fixture.press(SDLK_RETURN, SDL_SCANCODE_RETURN);
        expect_menu(!result.get().command_menu_invoked);
        expect_menu(fixture.harness.drawn_text().find("Command menu") == std::string::npos);
    }
}

TEST_CASE("Command menu finds commands beyond the retired 512-row storage limit",
          "[frontend-state][event-harness][command-menu]") {
    fixture_t fixture;
    auto& view = fixture.harness.view();
    view.command_menu_rows.resize(513);
    for (std::size_t i = 0; i < 512; ++i) view.command_menu_rows[i] = {.id = "other", .label = "Other", .enabled = true};
    view.command_menu_rows[512] = {.id = "needle", .label = "Needle late command", .enabled = true};
    (void)fixture.harness.frame();
    fixture.open(); fixture.type("needle");
    const auto result = fixture.press(SDLK_RETURN, SDL_SCANCODE_RETURN);
    expect_menu(std::string_view{result.get().command_menu_command_id} == "needle");
}

TEST_CASE("Command menu rejects and cancels inline editors",
          "[frontend-state][event-harness][command-menu]") {
    for (int editor = 0; editor < 5; ++editor) {
        fixture_t fixture;
        auto& compose = worm_ui_nuklear_compose_state();
        bool* active[] = {&compose.inline_timing.inline_timing_open, &compose.inline_offset.inline_offset_open,
            &compose.dialogs.go_to_measure_open, &compose.bookmark_ui.bookmark_rename.active,
            &compose.bookmark_ui.bookmark_rename.release_pending};
        *active[editor] = true;
        expect_menu(!worm_ui_frontend_open_command_menu());
        *active[editor] = false;
        fixture.open();
        *active[editor] = true;
        (void)fixture.harness.frame();
        expect_menu(fixture.harness.drawn_text().find("Command menu") == std::string::npos);
        *active[editor] = false;
    }
}

TEST_CASE("Command menu blocks IME activation and consumes held Enter after closing",
          "[frontend-state][event-harness][command-menu]") {
    fixture_t fixture;
    fixture.open();
    SDL_Event editing{}; editing.type = SDL_EVENT_TEXT_EDITING; editing.edit.text = "ime";
    (void)fixture.harness.frame({&editing, 1});
    expect_menu(!fixture.press(SDLK_RETURN, SDL_SCANCODE_RETURN).get().command_menu_invoked);
    editing.edit.text = "";
    (void)fixture.harness.frame({&editing, 1});
    auto enter = key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    expect_menu(fixture.harness.frame({&enter, 1}).get().command_menu_invoked);
    enter.key.repeat = true;
    const auto held = fixture.harness.frame({&enter, 1});
    expect_menu(!held.get().command_menu_invoked);
    expect_menu(held.get().compose_inputs.size() == 0);
    const auto up = key(SDLK_RETURN, SDL_SCANCODE_RETURN, false);
    expect_menu(fixture.harness.frame({&up, 1}).get().compose_inputs.size() == 0);
}

TEST_CASE("Command palette keeps title and keycaps together above search", "[frontend-state][event-harness][command-menu]") {
    fixture_t fixture;
    fixture.open();
    const auto title = fixture.harness.drawn_text_center("Command menu");
    const auto enter = fixture.harness.drawn_text_center("Enter");
    const auto escape = fixture.harness.drawn_text_center("Esc");
    expect_menu(title.has_value() && enter.has_value() && escape.has_value());
    expect_menu(enter->first > title->first && escape->first > enter->first);
    expect_menu(std::abs(enter->second - title->second) < 12.0f);
    expect_menu(fixture.harness.drawn_text().find("Up/Down to choose") == std::string::npos);
}
