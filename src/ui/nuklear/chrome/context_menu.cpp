#include "ui/nuklear/chrome/context_menu.h"
#include "ui/nuklear/compose/compose_state.h"

auto worm_ui_nuklear_compose_state_t::has_palette_context_menu() const -> bool {
    return wormedit::ui::chrome::context_menu_owns_input(snap_grid.snap_grid_menu) ||
           wormedit::ui::chrome::context_menu_owns_input(hitsound_menu.menu);
}
void worm_ui_nuklear_compose_state_t::retire_palette_context_menus(nk_context* context) {
    for (auto* menu : {&snap_grid.snap_grid_menu, &hitsound_menu.menu})
        wormedit::ui::chrome::context_menu_retire(*menu, context);
}
void worm_ui_nuklear_compose_state_t::abandon_palette_context_menus() {
    for (auto* menu : {&snap_grid.snap_grid_menu, &hitsound_menu.menu})
        wormedit::ui::chrome::context_menu_abandon(*menu);
}

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/tooltip.h"

#include <algorithm>
#include <cstring>

namespace wormedit::ui::chrome {
namespace {
ContextMenu* keyboard_menu{};
ContextMenu* rendering_menu{};
std::vector<std::uint32_t> captured_keys;
KeyboardContextTarget focused_target{};
std::uint64_t focused_object{};
KeyboardContextTarget open_request{};

auto enabled_row(const ContextMenu& menu, int row) -> bool {
    return row >= 0 && static_cast<std::size_t>(row) < menu.enabled_rows.size() &&
           menu.enabled_rows[static_cast<std::size_t>(row)];
}

void move_keyboard_selection(ContextMenu& menu, ContextMenuKey key) {
    const auto count = static_cast<int>(menu.enabled_rows.size());
    const bool backward = key == ContextMenuKey::up || key == ContextMenuKey::end;
    menu.navigation_backward = backward;
    int row = menu.selected_row;
    if (key == ContextMenuKey::home || key == ContextMenuKey::end || !enabled_row(menu, row))
        row = backward ? count : -1;
    for (int tried = 0; tried < count; ++tried) {
        row = (row + (backward ? -1 : 1) + count) % count;
        if (!enabled_row(menu, row)) continue;
        menu.selected_row = row;
        return;
    }
    menu.selected_row = -1;
}


// Popup windows carry no name; their parent records the hashed title of its popup.
auto inside_own_popup(const ContextMenu& menu, const nk_context* context) -> bool {
    if (!popup_panel(context) || !menu.title) return false;
    const auto* parent = context->current->parent;
    const auto title = nk_murmur_hash(menu.title, static_cast<int>(std::strlen(menu.title)),
                                      NK_PANEL_POPUP);
    return parent && parent->popup.win == context->current && parent->popup.name == title;
}

// Nested groups run on a temporary panel; opening a popup there would nest popups.
auto directly_in_window(const nk_context* context) -> bool {
    return context && context->current && context->current->layout &&
           context->current->layout->type == NK_PANEL_WINDOW;
}

auto begin_menu_popup(ContextMenu& menu, nk_context* context) -> bool {
    // Menubar menus use menu_padding and no stock border; menu_surface_begin() frames them.
    auto& window = context->style.window;
    const auto padding = window.popup_padding;
    window.popup_padding = nk_vec2(menu_padding, menu_padding);
    const auto began = nk_popup_begin(context, NK_POPUP_STATIC, menu.title,
                                      NK_WINDOW_NO_SCROLLBAR, menu.bounds) != 0;
    window.popup_padding = padding;
    return began;
}

} // namespace

auto context_menu_keyboard_owned() -> bool {
    return (keyboard_menu && keyboard_menu->open && keyboard_menu->shared_keyboard_navigation) || !captured_keys.empty();
}

void context_menu_reset_navigation(ContextMenu& menu) {
    menu.virtual_rows = false;
    menu.row_cursor = 0;
    menu.enabled_rows.clear();
    menu.frame_rows.clear();
    menu.selected_row = menu.activate_row = -1;
    menu.submenu_requested = menu.back_requested = menu.keyboard_dismiss = false;
}

auto context_menu_capture_key(ContextMenuKey key, std::uint32_t native_key,
                              bool down, bool repeat) -> bool {
    const auto held = std::ranges::find(captured_keys, native_key);
    const bool owned = (keyboard_menu && keyboard_menu->open && keyboard_menu->shared_keyboard_navigation) ||
                       open_request != KeyboardContextTarget::none;
    if (!owned && held == captured_keys.end()) return false;
    if (!down) {
        if (held != captured_keys.end()) captured_keys.erase(held);
        return true;
    }
    if (held == captured_keys.end()) captured_keys.push_back(native_key);
    if (!owned || (repeat && key != ContextMenuKey::up && key != ContextMenuKey::down)) return true;
    if (!keyboard_menu || !keyboard_menu->open) return true;
    auto& menu = *keyboard_menu;
    menu.keyboard_mode = true;
    switch (key) {
    case ContextMenuKey::up: case ContextMenuKey::down:
    case ContextMenuKey::home: case ContextMenuKey::end:
        move_keyboard_selection(menu, key); break;
    case ContextMenuKey::activate:
        if (enabled_row(menu, menu.selected_row)) menu.activate_row = menu.selected_row;
        break;
    case ContextMenuKey::escape: menu.keyboard_dismiss = true; break;
    case ContextMenuKey::right: menu.submenu_requested = true; break;
    case ContextMenuKey::left: menu.back_requested = true; break;
    case ContextMenuKey::none: break;
    }
    return true;
}

void context_menu_close_active_keyboard() {
    if (keyboard_menu) context_menu_close(*keyboard_menu);
    keyboard_menu = nullptr;
    open_request = KeyboardContextTarget::none;
    focused_target = KeyboardContextTarget::none;
    focused_object = 0;
}

void context_menu_cancel_keyboard() {
    context_menu_close_active_keyboard();
    captured_keys.clear();
}

void context_menu_focus(KeyboardContextTarget target, std::uint64_t object) {
    focused_target = target;
    focused_object = object;
}
auto context_menu_focused_object() -> std::uint64_t { return focused_object; }
auto context_menu_focused_target() -> KeyboardContextTarget { return focused_target; }
void context_menu_request_open(KeyboardContextTarget target) { open_request = target; }
auto context_menu_take_open_request(KeyboardContextTarget target) -> bool {
    if (open_request != target || target == KeyboardContextTarget::none) return false;
    open_request = KeyboardContextTarget::none;
    return true;
}
void context_menu_finish_frame() { open_request = KeyboardContextTarget::none; }
auto context_menu_take_back(ContextMenu& menu) -> bool {
    const auto back = menu.back_requested;
    menu.back_requested = false;
    return back;
}

void context_menu_set_row_count(ContextMenu& menu, std::size_t count) {
    menu.virtual_rows = true;
    menu.enabled_rows.resize(std::min<std::size_t>(count, 100'001), 2);
}
void context_menu_set_row_enabled(ContextMenu& menu, std::size_t row, bool enabled) {
    if (row >= menu.enabled_rows.size()) return;
    menu.enabled_rows[row] = enabled ? 1 : 0;
    if (!enabled && menu.selected_row == static_cast<int>(row))
        move_keyboard_selection(menu, menu.navigation_backward ? ContextMenuKey::up : ContextMenuKey::down);
}
void context_menu_set_row_cursor(ContextMenu& menu, int row) { menu.row_cursor = row; }
void context_menu_placeholder(ContextMenu& menu) { ++menu.row_cursor; }
void context_menu_scroll_to_selected(const ContextMenu& menu, nk_context* context,
    const char* group, float row_height, float visible_height) {
    if (!menu.keyboard_mode || menu.selected_row < 0) return;
    const auto step = row_height + std::max(0.0f, context->style.window.spacing.y);
    nk_uint x{}, y{};
    nk_group_get_scroll(context, group, &x, &y);
    const auto selected = static_cast<float>(menu.selected_row) * step;
    if (selected < y) y = static_cast<nk_uint>(selected);
    else if (selected + step > y + visible_height)
        y = static_cast<nk_uint>(std::max(0.0f, selected + step - visible_height));
    nk_group_set_scroll(context, group, x, y);
}

auto context_menu_height(float rows, float extra) -> float {
    return rows * context_menu_row_height + std::max(0.0f, rows - 1.0f) * metrics::gap_small +
           2.0f * menu_padding + extra;
}

void context_menu_open(ContextMenu& menu, struct nk_vec2 anchor, PopupContextStamp stamp, bool keyboard) {
    context_menu_reset_navigation(menu);
    menu.keyboard_mode = keyboard;
    keyboard_menu = &menu;
    menu.open = true;
    menu.opened_this_frame = true;
    menu.stamp = stamp;
    menu.anchor = anchor;
}

void context_menu_close(ContextMenu& menu, nk_context* context) {
    if (!menu.open) return;
    menu.open = false;
    if (keyboard_menu == &menu) keyboard_menu = nullptr;
    menu.opened_this_frame = false;
    popup_arm_release(menu.release_pending);
    if (context && inside_own_popup(menu, context)) {
        nk_popup_close(context);
    } else if (directly_in_window(context)) {
        dismiss_popup(context, menu.title, menu.bounds);
    } else {
        menu.close_pending = true;
    }
}

void context_menu_reset(ContextMenu& menu) {
    if (keyboard_menu == &menu) keyboard_menu = nullptr;
    if (rendering_menu == &menu) rendering_menu = nullptr;
    menu = ContextMenu{.title = menu.title};
}

void context_menu_abandon(ContextMenu& menu) {
    const auto retire = menu.open || menu.close_pending;
    const auto bounds = menu.bounds;
    context_menu_reset(menu);
    menu.close_pending = retire;
    menu.bounds = bounds;
}

auto context_menu_owns_input(const ContextMenu& menu) -> bool {
    return menu.owns_input();
}

void context_menu_retire(ContextMenu& menu, nk_context* context) {
    if (!menu.close_pending || !context || popup_panel(context)) return;
    dismiss_popup(context, menu.title, menu.bounds);
    menu.close_pending = false;
}

auto context_menu_update(ContextMenu& menu, nk_context* context) -> bool {
    menu.opened_this_frame = false;
    (void)popup_update_release_pending(context, menu.release_pending);
    context_menu_retire(menu, context);
    return context_menu_owns_input(menu);
}

auto context_menu_begin(ContextMenu& menu, nk_context* context, PopupContextStamp current,
                        struct nk_vec2 size) -> bool {
    if (!menu.open || !context || popup_panel(context)) return false;
    const auto opening = menu.opened_this_frame;
    menu.opened_this_frame = false;
    if (menu.keyboard_dismiss || !popup_context_matches(menu.stamp, current)) {
        context_menu_close(menu, context);
        return false;
    }
    menu.bounds = clamped_popup_bounds(context, menu.anchor, size);
    if (!begin_menu_popup(menu, context)) {
        context_menu_close(menu);
        menu.close_pending = false;
        return false;
    }
    if (popup_dismiss_requested(context, nk_window_get_bounds(context), !opening)) {
        context_menu_close(menu, context);
        nk_popup_end(context);
        return false;
    }
    menu.frame_rows.clear();
    menu.row_cursor = 0;
    rendering_menu = &menu;
    menu.saved_surface = menu_surface_begin(context);
    return true;
}

auto context_menu_button(nk_context* context, const char* label, const char* disabled_reason, bool submenu)
    -> bool {
    const auto bounds = nk_widget_bounds(context);
    auto* menu = rendering_menu;
    const auto row = menu ? menu->row_cursor++ : -1;
    if (menu) {
        menu->frame_rows.push_back(disabled_reason ? 0 : 1);
        if (!disabled_reason && menu->keyboard_mode && menu->selected_row < 0)
            menu->selected_row = row;
        const auto moved = context->input.mouse.delta.x != 0 || context->input.mouse.delta.y != 0;
        if (!disabled_reason && moved && nk_input_is_mouse_hovering_rect(&context->input, bounds)) {
            menu->selected_row = row;
            menu->keyboard_mode = false;
        }
    }
    const bool selected = menu && menu->selected_row == row;
    const bool keyboard_clicked = selected && !disabled_reason &&
        (menu->activate_row == row || (submenu && menu->submenu_requested));
    if (keyboard_clicked) menu->activate_row = -1;
    // Copy the style after disabling: nk_widget_disable_begin() dims it in place.
    if (disabled_reason) nk_widget_disable_begin(context);
    auto style = context->style.contextual_button;
    style.text_alignment = NK_TEXT_LEFT;
    if (selected && !disabled_reason) {
        style.normal = style.hover;
        style.text_normal = style.text_hover;
    }
    const auto clicked = nk_button_label_styled(context, &style, label) != 0;
    if (!disabled_reason) return clicked || keyboard_clicked;
    nk_widget_disable_end(context);
    if (disabled_reason[0] != '\0' && nuklear::tooltip::hovered(context, bounds))
        nuklear::tooltip::text(context, disabled_reason);
    return false;
}

auto context_menu_item(ContextMenu& menu, nk_context* context, const char* label,
                       const char* disabled_reason) -> bool {
    nk_layout_row_dynamic(context, context_menu_row_height, 1);
    if (!context_menu_button(context, label, disabled_reason)) return false;
    context_menu_close(menu, context);
    return true;
}

void context_menu_end(ContextMenu& menu, nk_context* context) {
    if (!menu.virtual_rows) menu.enabled_rows = menu.frame_rows;
    menu.activate_row = -1;
    menu.submenu_requested = false;
    rendering_menu = nullptr;
    if (menu.close_pending && inside_own_popup(menu, context)) {
        nk_popup_close(context);
        menu.close_pending = false;
    }
    nk_popup_end(context);
    menu_surface_end(context, menu.saved_surface);
}

} // namespace wormedit::ui::chrome
