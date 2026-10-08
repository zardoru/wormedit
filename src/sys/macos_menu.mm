#include "sys/macos_menu.h"

#import <AppKit/AppKit.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include <deque>
#include <string>

namespace {
using action_t = wormedit::ui::workspace_menu_action_t;
std::deque<action_t> pending_actions;
NSMenu* previous_menu;
NSMenu* main_menu;
NSMenu* previous_window_menu;
id menu_target;
const wormedit::command_registry_t* current_registry;

NSString* menu_text(const char* text) {
    return [NSString stringWithUTF8String:text] ?: @"";
}

NSEventModifierFlags native_modifiers(wormedit::shortcut_modifiers_t modifiers) {
    using wormedit::shortcut_modifier_t;
    const auto has = [&](shortcut_modifier_t value) {
        return (modifiers & static_cast<wormedit::shortcut_modifiers_t>(value)) != 0;
    };
    NSEventModifierFlags result = 0;
    if (has(shortcut_modifier_t::primary) || has(shortcut_modifier_t::meta)) result |= NSEventModifierFlagCommand;
    if (has(shortcut_modifier_t::control)) result |= NSEventModifierFlagControl;
    if (has(shortcut_modifier_t::alt)) result |= NSEventModifierFlagOption;
    if (has(shortcut_modifier_t::shift)) result |= NSEventModifierFlagShift;
    return result;
}

std::string native_key(wormedit::shortcut_key_t key) {
    const auto code = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(key), SDL_KMOD_NONE, false);
    if (code >= 0x20 && code < 0x7f) return std::string(1, static_cast<char>(code));
    if (code >= SDLK_F1 && code <= SDLK_F12) {
        const unichar character = static_cast<unichar>(NSF1FunctionKey + code - SDLK_F1);
        return [[NSString stringWithCharacters:&character length:1] UTF8String];
    }
    return {};
}

NSMenu* submenu(NSString* title) {
    for (NSMenuItem* item in main_menu.itemArray)
        if ([item.title isEqualToString:title]) return item.submenu;
    NSMenuItem* parent = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    parent.submenu = [[NSMenu alloc] initWithTitle:title];
    parent.submenu.autoenablesItems = NO;
    const auto window_index = [main_menu indexOfItemWithSubmenu:NSApp.windowsMenu];
    if (window_index >= 0) [main_menu insertItem:parent atIndex:window_index];
    else [main_menu addItem:parent];
    return parent.submenu;
}

void update_item(NSMenuItem* item, const wormedit::ui::workspace_menu_item_t& model) {
    if (model.action == action_t::none) { item.hidden = NO; return; }
    item.title = menu_text(model.title);
    item.enabled = model.enabled;
    item.state = model.checked ? NSControlStateValueOn : NSControlStateValueOff;
    item.hidden = NO;
    item.keyEquivalent = @"";
    item.keyEquivalentModifierMask = 0;
    const auto* command = model.command_id && current_registry
        ? wormedit::find_command(*current_registry, {model.command_id}) : nullptr;
    if (!command) return;
    item.enabled = item.enabled && command->enabled;
    if (command->current_shortcuts.empty()) return;
    const auto& chord = command->current_shortcuts.front();
    item.keyEquivalent = menu_text(native_key(chord.key).c_str());
    item.keyEquivalentModifierMask = native_modifiers(chord.modifiers);
}
} // namespace

@interface WormEditMenuTarget : NSObject
- (void)choose:(NSMenuItem*)sender;
@end
@implementation WormEditMenuTarget
- (void)choose:(NSMenuItem*)sender {
    if (NSApp.currentEvent.type == NSEventTypeKeyDown && NSApp.currentEvent.isARepeat) return;
    if (sender.enabled && pending_actions.size() < 32)
        pending_actions.push_back(static_cast<action_t>(sender.tag));
}
@end

namespace wormedit::sys {
bool initialize_macos_menu() {
    const char* fallback = SDL_getenv("WORMEDIT_IN_WINDOW_MENU");
    if (fallback && std::string_view{fallback} == "1") return false;
    if (!NSApp || !NSApp.mainMenu || ![NSThread isMainThread]) return false;
    @try {
        previous_menu = NSApp.mainMenu;
        previous_window_menu = NSApp.windowsMenu;
        main_menu = [[NSMenu alloc] initWithTitle:@"WormEdit"];
        main_menu.autoenablesItems = NO;
        menu_target = [[WormEditMenuTarget alloc] init];
        // Keep SDL's application menu (Hide, Services and safe SDL Quit event).
        NSMenuItem* app = previous_menu.numberOfItems > 0 ? [previous_menu itemAtIndex:0] : nil;
        if (app) [main_menu addItem:[app copy]];
        NSMenuItem* windows = [previous_menu itemWithTitle:@"Window"];
        if (windows) {
            NSMenuItem* copy = [windows copy];
            [main_menu addItem:copy];
            NSApp.windowsMenu = copy.submenu;
        }
        NSApp.mainMenu = main_menu;
        return NSApp.mainMenu == main_menu;
    } @catch (NSException*) {
        shutdown_macos_menu();
        return false;
    }
}

void shutdown_macos_menu() {
    if (main_menu && NSApp.mainMenu == main_menu) NSApp.mainMenu = previous_menu;
    if (previous_window_menu) NSApp.windowsMenu = previous_window_menu;
    previous_window_menu = nil;
    main_menu = nil;
    previous_menu = nil;
    menu_target = nil;
    current_registry = nullptr;
    pending_actions.clear();
}

bool macos_menu_active() { return main_menu && NSApp.mainMenu == main_menu; }

void refresh_macos_menu_shortcuts(const command_registry_t& registry) {
    current_registry = &registry;
}

void refresh_macos_menu(const ui::workspace_menu_t& model) {
    if (!macos_menu_active()) return;
    @try {
        for (NSMenuItem* parent in main_menu.itemArray) {
            if (![@[@"File", @"Edit", @"View", @"Preview"] containsObject:parent.title]) continue;
            for (NSMenuItem* item in parent.submenu.itemArray) item.hidden = YES;
        }
        NSMenu* current_menu = nil;
        NSInteger position = 0;
        for (const auto& entry : model) {
            NSMenu* menu = submenu(menu_text(entry.menu));
            if (menu != current_menu) { current_menu = menu; position = 0; }
            const auto tag = static_cast<NSInteger>(entry.action);
            NSMenuItem* item = [menu itemWithTag:tag];
            if (!item) {
                item = entry.action == action_t::none ? [NSMenuItem separatorItem]
                    : [[NSMenuItem alloc] initWithTitle:menu_text(entry.title)
                                                  action:@selector(choose:) keyEquivalent:@""];
                item.target = menu_target;
                item.tag = tag;
                [menu addItem:item];
            }
            if ([menu indexOfItem:item] != position) {
                [menu removeItem:item];
                [menu insertItem:item atIndex:position];
            }
            ++position;
            update_item(item, entry);
        }
    } @catch (NSException*) {
        shutdown_macos_menu();
    }
}

action_t take_macos_menu_action() {
    if (pending_actions.empty()) return action_t::none;
    const auto result = pending_actions.front();
    pending_actions.pop_front();
    return result;
}
} // namespace wormedit::sys
