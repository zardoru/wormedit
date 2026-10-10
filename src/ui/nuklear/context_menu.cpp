#include "ui/nuklear/context_menu.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/tooltip.h"

#include <algorithm>
#include <cstring>

namespace wormedit::ui::chrome {
namespace {

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

auto clamped_bounds(nk_context* context, struct nk_vec2 anchor, struct nk_vec2 size)
    -> struct nk_rect {
    const auto region = nk_window_get_content_region(context);
    const auto width = std::min(size.x, region.w);
    const auto height = std::min(size.y, region.h);
    return nk_rect(std::clamp(anchor.x - region.x, 0.0f, std::max(0.0f, region.w - width)),
                   std::clamp(anchor.y - region.y, 0.0f, std::max(0.0f, region.h - height)),
                   width, height);
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

auto context_menu_height(float rows, float extra) -> float {
    return rows * context_menu_row_height + std::max(0.0f, rows - 1.0f) * metrics::gap_small +
           2.0f * menu_padding + extra;
}

void context_menu_open(ContextMenu& menu, struct nk_vec2 anchor, PopupContextStamp stamp) {
    menu.open = true;
    menu.opened_this_frame = true;
    menu.stamp = stamp;
    menu.anchor = anchor;
}

void context_menu_close(ContextMenu& menu, nk_context* context) {
    if (!menu.open) return;
    menu.open = false;
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
    return popup_owns_input(menu.open, menu.release_pending);
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
    if (!popup_context_matches(menu.stamp, current)) {
        context_menu_close(menu, context);
        return false;
    }
    menu.bounds = clamped_bounds(context, menu.anchor, size);
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
    menu.saved_surface = menu_surface_begin(context);
    return true;
}

auto context_menu_button(nk_context* context, const char* label, const char* disabled_reason)
    -> bool {
    const auto bounds = nk_widget_bounds(context);
    // Copy the style after disabling: nk_widget_disable_begin() dims it in place.
    if (disabled_reason) nk_widget_disable_begin(context);
    auto style = context->style.contextual_button;
    style.text_alignment = NK_TEXT_LEFT;
    const auto clicked = nk_button_label_styled(context, &style, label) != 0;
    if (!disabled_reason) return clicked;
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
    if (menu.close_pending && inside_own_popup(menu, context)) {
        nk_popup_close(context);
        menu.close_pending = false;
    }
    nk_popup_end(context);
    menu_surface_end(context, menu.saved_surface);
}

} // namespace wormedit::ui::chrome
