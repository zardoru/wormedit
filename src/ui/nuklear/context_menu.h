#pragma once

#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/ui_metrics.h"
#include "wormedit/nuklear_config.h"

#include <vector>

namespace wormedit::ui::chrome {

// One right-click action menu. It owns the whole popup lifecycle: opening at the
// pointer for one context stamp, closing on a stale stamp, an outside press or
// Escape, the release latch that keeps the closing gesture away from covered
// widgets, and retiring a popup that was closed outside its parent's frame.
// It draws like the menubar menus (see menu_surface_begin()).
struct ContextMenu : wormedit::ui::chrome::retained_popup_t {
    const char* title{};
    bool opened_this_frame{};
    PopupContextStamp stamp{};
    struct nk_vec2 anchor{};
    struct nk_rect bounds{}; // Parent content-region coordinates, as nk_popup_begin takes them.
    MenuSurface saved_surface{};
    std::vector<unsigned char> enabled_rows;
    std::vector<unsigned char> frame_rows;
    int selected_row{-1};
    int activate_row{-1};
    bool keyboard_mode{};
    // Forms and specialized grid popups keep their own text/key handling.
    bool shared_keyboard_navigation{true};
    bool submenu_requested{};
    bool back_requested{};
    bool keyboard_dismiss{};
    bool virtual_rows{};
    bool navigation_backward{};
    int row_cursor{};
};

enum class ContextMenuKey { none, up, down, home, end, activate, escape, left, right };
enum class KeyboardContextTarget { none, compose, sounds, layers, charts };

// Native event routing: an open action menu owns keyboard input, including the
// releases/repeats of keys held when it closes. No SDL dependency in this component.
auto context_menu_capture_key(ContextMenuKey key, std::uint32_t native_key,
                              bool down, bool repeat) -> bool;
auto context_menu_keyboard_owned() -> bool;
void context_menu_cancel_keyboard();
void context_menu_close_active_keyboard();
void context_menu_focus(KeyboardContextTarget target, std::uint64_t object = 0);
auto context_menu_focused_object() -> std::uint64_t;
auto context_menu_focused_target() -> KeyboardContextTarget;
void context_menu_request_open(KeyboardContextTarget target);
auto context_menu_take_open_request(KeyboardContextTarget target) -> bool;
void context_menu_finish_frame();
void context_menu_reset_navigation(ContextMenu& menu);
auto context_menu_take_back(ContextMenu& menu) -> bool;
// Absolute item indices keep keyboard focus stable across virtualized pages.
void context_menu_set_row_count(ContextMenu& menu, std::size_t count);
void context_menu_set_row_enabled(ContextMenu& menu, std::size_t row, bool enabled);
void context_menu_set_row_cursor(ContextMenu& menu, int row);
void context_menu_placeholder(ContextMenu& menu);
void context_menu_scroll_to_selected(const ContextMenu& menu, nk_context* context,
    const char* group, float row_height, float visible_height);

inline constexpr float context_menu_row_height = metrics::menu_item_height;
inline constexpr float context_menu_width = 220.0f;

// Popup height for `rows` items plus `extra` px, with the menubar menus' padding.
auto context_menu_height(float rows, float extra = 0.0f) -> float;

// Open at a screen-space anchor (normally the pointer) for the given context.
void context_menu_open(ContextMenu& menu, struct nk_vec2 anchor, PopupContextStamp stamp, bool keyboard = false);

// Close and arm the release latch. Directly inside the menu's own popup this closes
// the Nuklear popup, and directly in the parent window it retires the popup. Anywhere
// else (no context, or inside a nested group) the popup is retired by
// context_menu_end() or the next context_menu_update().
void context_menu_close(ContextMenu& menu, nk_context* context = nullptr);

// Forget all state, including a pending release (tab switch, project close).
void context_menu_reset(ContextMenu& menu);

// Drop the menu without a release latch because the area that owned it is gone;
// a retained popup is still retired by the next context_menu_update()/retire().
void context_menu_abandon(ContextMenu& menu);

// True while the menu is open or its closing gesture is still held.
auto context_menu_owns_input(const ContextMenu& menu) -> bool;

// Call once per frame from the parent window, before handling the opening press.
// Advances the release latch and retires a popup closed from outside. Returns
// context_menu_owns_input().
auto context_menu_update(ContextMenu& menu, nk_context* context) -> bool;

// Retire a popup closed outside its frame; context_menu_update() already does this.
void context_menu_retire(ContextMenu& menu, nk_context* context);

// Begin the open menu with the given size, clamped into the parent content region.
// Closes the menu and returns false when `current` no longer matches the stamp, or
// on an outside press or Escape (the opening press itself never dismisses).
// After true, draw items and call context_menu_end().
auto context_menu_begin(ContextMenu& menu, nk_context* context, PopupContextStamp current,
                        struct nk_vec2 size) -> bool;

// A flat menubar-style button for the current layout slot. A non-null
// `disabled_reason` dims the item and shows the reason on hover; disabled items
// never activate. Does not close the menu (submenu headers, custom rows).
auto context_menu_button(nk_context* context, const char* label,
                         const char* disabled_reason = nullptr, bool submenu = false) -> bool;

// A full-width row holding context_menu_button(); activation closes the menu.
auto context_menu_item(ContextMenu& menu, nk_context* context, const char* label,
                       const char* disabled_reason = nullptr) -> bool;

// Separate item groups with chrome::menu_separator().

void context_menu_end(ContextMenu& menu, nk_context* context);

} // namespace wormedit::ui::chrome
