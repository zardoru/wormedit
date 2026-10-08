#include "sys/macos_menu.h"
#include <catch2/catch_test_macros.hpp>
#import <AppKit/AppKit.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

namespace {
using namespace wormedit;
using action_t = ui::workspace_menu_action_t;
struct menu_fixture_t {
    command_registry_t registry{make_default_editor_command_registry({})};
    ui::workspace_menu_t model{{"Edit", "Copy", action_t::copy, "edit.copy", true}};
    menu_fixture_t() {
        [NSApplication sharedApplication];
        if (!NSApp.mainMenu) NSApp.mainMenu = [[NSMenu alloc] initWithTitle:@"Original"];
        SDL_setenv_unsafe("WORMEDIT_IN_WINDOW_MENU", "0", 1);
        SDL_InitSubSystem(SDL_INIT_EVENTS);
    }
    ~menu_fixture_t() {
        sys::shutdown_macos_menu();
        SDL_QuitSubSystem(SDL_INIT_EVENTS);
        SDL_unsetenv_unsafe("WORMEDIT_IN_WINDOW_MENU");
    }
    NSMenuItem* initialize() {
        REQUIRE(sys::initialize_macos_menu());
        sys::refresh_macos_menu_shortcuts(registry);
        sys::refresh_macos_menu(model);
        NSMenu* edit = [NSApp.mainMenu itemWithTitle:@"Edit"].submenu;
        NSMenuItem* copy = [edit itemWithTag:static_cast<NSInteger>(action_t::copy)];
        REQUIRE(copy != nil);
        return copy;
    }
};
}

TEST_CASE("Native menus project shared titles and primary shortcuts") {
    menu_fixture_t fixture;
    NSMenuItem* copy = fixture.initialize();
    CHECK([copy.title isEqualToString:@"Copy"]);
    CHECK([copy.keyEquivalent isEqualToString:@"c"]);
    CHECK(copy.keyEquivalentModifierMask == NSEventModifierFlagCommand);
}

TEST_CASE("Native menu selection queues one shared action and rejects disabled items") {
    menu_fixture_t fixture;
    NSMenuItem* copy = fixture.initialize();
    [copy.menu performActionForItemAtIndex:[copy.menu indexOfItem:copy]];
    CHECK(sys::take_macos_menu_action() == action_t::copy);
    CHECK(sys::take_macos_menu_action() == action_t::none);
    (void)update_command_state(fixture.registry, {"edit.copy"}, false, std::nullopt);
    sys::refresh_macos_menu(fixture.model);
    CHECK_FALSE(copy.enabled);
    [copy.menu performActionForItemAtIndex:[copy.menu indexOfItem:copy]];
    CHECK(sys::take_macos_menu_action() == action_t::none);
}

TEST_CASE("Native menu updates use the edited shortcut profile and checked state") {
    menu_fixture_t fixture;
    NSMenuItem* copy = fixture.initialize();
    auto* command = const_cast<command_t*>(find_command(fixture.registry, {"edit.copy"}));
    REQUIRE(command);
    command->current_shortcuts = {{shortcut_key_t::x, shortcut_modifier_t::primary | shortcut_modifier_t::shift}};
    fixture.model[0].checked = true;
    sys::refresh_macos_menu(fixture.model);
    CHECK([copy.keyEquivalent isEqualToString:@"x"]);
    CHECK(copy.keyEquivalentModifierMask == (NSEventModifierFlagCommand | NSEventModifierFlagShift));
    CHECK(copy.state == NSControlStateValueOn);
}

TEST_CASE("Native menu lifecycle hides retired items and restores the original menu") {
    menu_fixture_t fixture;
    NSMenu* original = NSApp.mainMenu;
    NSMenuItem* copy = fixture.initialize();
    sys::refresh_macos_menu({});
    CHECK(copy.hidden);
    sys::shutdown_macos_menu();
    CHECK(NSApp.mainMenu == original);
    CHECK_FALSE(sys::macos_menu_active());
}

TEST_CASE("Native menu runtime fallback leaves the application menu unchanged") {
    menu_fixture_t fixture;
    NSMenu* original = NSApp.mainMenu;
    SDL_setenv_unsafe("WORMEDIT_IN_WINDOW_MENU", "1", 1);
    CHECK_FALSE(sys::initialize_macos_menu());
    CHECK_FALSE(sys::macos_menu_active());
    CHECK(NSApp.mainMenu == original);
}

TEST_CASE("Native menus retain model order when project-dependent items appear") {
    menu_fixture_t fixture;
    NSMenuItem* copy = fixture.initialize();
    fixture.model.insert(fixture.model.begin(), {"Edit", "Cut", action_t::cut, "edit.cut", true});
    sys::refresh_macos_menu(fixture.model);
    CHECK([copy.menu itemAtIndex:0].tag == static_cast<NSInteger>(action_t::cut));
    CHECK([copy.menu itemAtIndex:1] == copy);
    fixture.model.erase(fixture.model.begin());
    sys::refresh_macos_menu(fixture.model);
    CHECK([copy.menu itemAtIndex:0] == copy);
}
