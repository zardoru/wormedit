#pragma once

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/text_field.h"
#include "ui/nuklear/tooltip.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace wormedit::ui::chrome {

inline void timing_tooltip(nk_context* context, const char* timing,
                           const char* instructions = nullptr) {
    if (!context || !context->current || !timing || !timing[0]) return;
    std::string content{timing};
    if (instructions && instructions[0]) {
        content += "\n\n";
        content += instructions;
    }
    nuklear::tooltip::text(context, content);
}

// Edits a single-line text field and selects its whole value on a double-click.
inline auto worm_ui_nuklear_text_field(nk_context* context, nk_flags flags, char* buffer,
                                       int capacity, nk_plugin_filter filter) -> nk_flags {
    if (!context || !buffer || capacity <= 0 || !context->current || !context->current->layout)
        return 0;

    const auto bounds = nk_widget_bounds(context);
    const auto edit_sequence = context->current->edit.seq;
    const bool double_clicked = (flags & NK_EDIT_MULTILINE) == 0 &&
                                nk_input_mouse_clicked(&context->input, NK_BUTTON_DOUBLE, bounds);
    const auto result = nk_edit_string_zero_terminated(context, flags, buffer, capacity, filter);
    const auto selection = worm_ui_nuklear_double_click_selection(
        (flags & NK_EDIT_MULTILINE) == 0, double_clicked, context->text_edit.string.len);
    if (selection && context->current->edit.active &&
        context->current->edit.name == edit_sequence) {
        context->current->edit.cursor = selection->end;
        context->current->edit.sel_start = selection->start;
        context->current->edit.sel_end = selection->end;
    }
    return result;
}

// Focus names the next edit widget, so it must precede drawing that widget.
inline auto autofocus_text_field(nk_context* context, nk_flags flags, char* text,
                                 int capacity, nk_plugin_filter filter,
                                 bool& focus_pending) -> nk_flags {
    if (focus_pending) {
        nk_edit_focus(context, NK_EDIT_ALWAYS_INSERT_MODE);
        if ((flags & NK_EDIT_AUTO_SELECT) != 0) {
            auto& edit = context->current->edit;
            edit.cursor = static_cast<int>(std::strlen(text));
            edit.sel_start = 0;
            edit.sel_end = edit.cursor;
        }
        focus_pending = false;
    }
    return worm_ui_nuklear_text_field(context, flags, text, capacity, filter);
}

// Finish a retained popup in its own panel so Nuklear releases the parent input lock.
inline void dismiss_popup(nk_context* context, const char* title, struct nk_rect bounds) {
    if (!nk_popup_begin(context, NK_POPUP_STATIC, title, NK_WINDOW_NO_SCROLLBAR, bounds)) return;
    nk_popup_close(context);
    nk_popup_end(context);
}

// Nuklear popups cannot contain another popup. Keep this boundary shared by
// every dropdown so a future popup caller receives a usable inline fallback.
inline auto popup_panel(const nk_context* context) -> bool {
    return context && context->current && context->current->layout &&
           (static_cast<nk_flags>(context->current->layout->type) & NK_PANEL_SET_POPUP) != 0;
}

inline auto safe_combo(nk_context* context, const char* const* items, int count,
                       int selected, int item_height, struct nk_vec2 size) -> int {
    if (!context || !context->current || !context->current->layout || !items || count <= 0)
        return selected;
    if (!popup_panel(context))
        return nk_combo(context, items, count, selected, item_height, size);
    selected = std::clamp(selected, 0, count - 1);
    for (int index = 0; index < count; ++index) {
        nk_layout_row_dynamic(context, static_cast<float>(item_height), 1);
        if (nk_select_label(context, items[index], NK_TEXT_LEFT, selected == index))
            selected = index;
    }
    return selected;
}

inline auto safe_combo_begin_label(nk_context* context, const char* label,
                                   struct nk_vec2 size) -> nk_bool {
    if (!context || !context->current || !context->current->layout) return nk_false;
    if (popup_panel(context)) {
        nk_label(context, label, NK_TEXT_LEFT);
        return nk_false;
    }
    return nk_combo_begin_label(context, label, size);
}

// Equal label/control shares keep property rows usable in narrow side panels.
inline constexpr float property_label_ratio = 0.5f;

using theme::UiColor;
inline auto color(UiColor role) -> nk_color { return theme::ui(role); }

inline auto button_style(nk_context* context, bool selected) -> nk_style_button {
    auto style = context->style.button;
    style.normal = nk_style_item_color(selected ? color(UiColor::selected)
                                                 : nk_rgba(0, 0, 0, 0));
    style.hover = nk_style_item_color(color(UiColor::surface_hover));
    style.active = nk_style_item_color(color(UiColor::surface_active));
    style.border = 0.0f;
    style.rounding = 4.0f;
    style.text_normal = selected ? color(UiColor::selected_text) : color(UiColor::text);
    style.text_hover = color(UiColor::text_bright);
    style.text_active = color(UiColor::text_bright);
    return style;
}

inline auto button_hovered(const nk_context* context, struct nk_rect bounds) -> bool {
    if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0) return true;
    // Disabled widgets omit Nuklear's hover bit but retain their own tooltip.
    if (!context->current || !context->current->layout || !context->current->widgets_disabled)
        return false;
    if (context->active != context->current && !popup_panel(context)) return false;
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
           nk_input_is_mouse_hovering_rect(&context->input, context->current->layout->clip);
}

inline void button_feedback(nk_context* context, struct nk_rect bounds, bool selected,
                            const char* tooltip) {
    if (button_hovered(context, bounds)) {
        nk_stroke_rect(nk_window_get_canvas(context), bounds, 4.0f, 1.0f,
                       color(UiColor::border_hover));
        if (tooltip && tooltip[0] != '\0') nuklear::tooltip::text(context, tooltip);
    } else if (selected) {
        nk_stroke_rect(nk_window_get_canvas(context), bounds, 4.0f, 1.0f,
                       color(UiColor::selected));
    }
}

inline void layout_separator(nk_context* context) {
    nk_layout_row_template_push_static(context, 9.0f);
}

inline void toolbar_separator(nk_context* context) {
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    const auto x = bounds.x + bounds.w * 0.5f;
    nk_stroke_line(nk_window_get_canvas(context), x, bounds.y + 4.0f,
                   x, bounds.y + bounds.h - 4.0f, 1.0f, color(UiColor::border));
}

inline void section_separator(nk_context* context) {
    nk_layout_row_dynamic(context, 9.0f, 1);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    const auto y = bounds.y + bounds.h * 0.5f;
    nk_stroke_line(nk_window_get_canvas(context), bounds.x, y,
                   bounds.x + bounds.w, y, 1.0f, color(UiColor::border));
}

struct IconButton {
    lucide::Glyph glyph;
    const char* tooltip;
    bool selected{false};

    static constexpr float width = 34.0f;

    static void layout_columns(nk_context* context, int count = 1, float column_width = width) {
        for (int index = 0; index < count; ++index)
            nk_layout_row_template_push_static(context, column_width);
    }

    auto render(nk_context* context) const -> bool {
        const auto bounds = nk_widget_bounds(context);
        auto style = button_style(context, selected);
        const bool clicked = nk_button_label_styled(context, &style, "") != 0;
        const auto tint = (context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0
                              ? color(UiColor::text_bright)
                              : selected ? color(UiColor::selected_text) : color(UiColor::text);
        lucide::draw(context, bounds, glyph, tint);
        button_feedback(context, bounds, selected, tooltip);
        return clicked;
    }
};

// Icon button whose glyph is drawn by a callback, for shapes outside the Lucide raster pack.
struct DrawnIconButton {
    void (*draw)(struct nk_command_buffer*, struct nk_rect, struct nk_color);
    const char* tooltip;
    bool selected{false};

    auto render(nk_context* context) const -> bool {
        const auto bounds = nk_widget_bounds(context);
        auto style = button_style(context, selected);
        const bool clicked = nk_button_label_styled(context, &style, "") != 0;
        const auto tint = (context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0
                              ? color(UiColor::text_bright)
                              : selected ? color(UiColor::selected_text) : color(UiColor::text);
        draw(nk_window_get_canvas(context), bounds, tint);
        button_feedback(context, bounds, selected, tooltip);
        return clicked;
    }
};

inline auto mode_button(nk_context* context, const char* label, const char* tooltip,
                        bool selected) -> bool {
    const auto bounds = nk_widget_bounds(context);
    const auto style = button_style(context, selected);
    const bool clicked = nk_button_label_styled(context, &style, label) != 0;
    button_feedback(context, bounds, selected, tooltip);
    return clicked;
}

inline constexpr float menu_padding = 8.0f;
inline constexpr float menu_radius = 8.0f;

// Call right after a successful nk_menu_begin_*: paints the menu as a rounded panel with a soft
// drop shadow. The shadow lies outside the popup, so the clip is widened while it is drawn.
inline void menu_panel(nk_context* context) {
    auto* popup = context->current;
    auto* canvas = &popup->buffer;
    const auto body = popup->bounds;
    const auto clip = popup->layout->clip;
    // Keep the widened clip inside the parent window: backends reject scissors that reach
    // outside the render target, which would drop the whole panel.
    const auto outer = popup->parent->bounds;
    const auto left = std::max(body.x - 24.0f, outer.x);
    const auto top = std::max(body.y - 16.0f, outer.y);
    const auto right = std::min(body.x + body.w + 24.0f, outer.x + outer.w);
    const auto bottom = std::min(body.y + body.h + 32.0f, outer.y + outer.h);
    nk_push_scissor(canvas, nk_rect(left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)));
    for (int step = 1; step <= 8; ++step) {
        const auto grow = static_cast<float>(step);
        nk_fill_rect(canvas,
                     nk_rect(body.x - grow, body.y - grow + 4, body.w + 2 * grow, body.h + 2 * grow),
                     menu_radius + grow, nk_rgba(0, 0, 0, 9));
    }
    nk_fill_rect(canvas, body, menu_radius, color(UiColor::surface));
    nk_stroke_rect(canvas, body, menu_radius, 1.0f, color(UiColor::border_hover));
    nk_push_scissor(canvas, clip);
}

// Dropdowns, context menus and popups share one surface language with the buttons:
// surface fill, 1px border, 4px rounding and the same hover/active ramp.
inline void apply_dropdown_theme(nk_context* context) {
    auto& style = context->style;
    const auto surface = nk_style_item_color(color(UiColor::surface));
    const auto hover = nk_style_item_color(color(UiColor::surface_hover));
    const auto active = nk_style_item_color(color(UiColor::surface_active));
    style.window.combo_border_color = color(UiColor::border_hover);
    style.window.contextual_border_color = color(UiColor::border_hover);
    style.window.menu_border_color = color(UiColor::border_hover);
    style.window.popup_border_color = color(UiColor::border_hover);
    style.window.combo_border = style.window.contextual_border = 1.0f;
    style.window.popup_border = 1.0f;
    style.window.popup_padding = style.window.combo_padding = nk_vec2(4.0f, 4.0f);
    style.window.contextual_padding = nk_vec2(4.0f, 4.0f);
    // Menus are framed by menu_panel(); the stock square border stays off.
    style.window.menu_padding = nk_vec2(menu_padding, menu_padding);
    style.window.menu_border = 0.0f;
    style.combo.normal = surface;
    style.combo.hover = hover;
    style.combo.active = active;
    style.combo.border_color = color(UiColor::border);
    style.combo.label_normal = color(UiColor::text);
    style.combo.label_hover = color(UiColor::text_bright);
    style.combo.label_active = color(UiColor::text_bright);
    style.combo.symbol_normal = color(UiColor::text_muted);
    style.combo.symbol_hover = color(UiColor::text_bright);
    style.combo.symbol_active = color(UiColor::text_bright);
    style.combo.border = 1.0f;
    style.combo.rounding = 4.0f;
    for (auto* button : {&style.combo.button, &style.contextual_button}) {
        button->normal = surface;
        button->hover = hover;
        button->active = active;
        button->border_color = nk_rgba(0, 0, 0, 0);
        button->text_normal = color(UiColor::text);
        button->text_hover = color(UiColor::text_bright);
        button->text_active = color(UiColor::text_bright);
        button->rounding = 4.0f;
    }
    // Menu items sit flat on the menu panel and only light up on hover.
    style.contextual_button.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    // Menubar titles (File/Edit/View) sit flat on the bar and only light up on hover.
    style.menu_button = style.button;
    style.menu_button.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    style.menu_button.hover = hover;
    style.menu_button.active = active;
    style.menu_button.border = 0.0f;
    style.menu_button.border_color = nk_rgba(0, 0, 0, 0);
    style.menu_button.rounding = 4.0f;
    style.selectable.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    style.selectable.hover = hover;
    style.selectable.pressed = active;
    style.selectable.normal_active = nk_style_item_color(color(UiColor::selected));
    style.selectable.hover_active = nk_style_item_color(color(UiColor::selected));
    style.selectable.pressed_active = nk_style_item_color(color(UiColor::selected));
    style.selectable.text_normal = color(UiColor::text);
    style.selectable.text_hover = color(UiColor::text_bright);
    style.selectable.text_pressed = color(UiColor::text_bright);
    style.selectable.text_normal_active = color(UiColor::selected_text);
    style.selectable.text_hover_active = color(UiColor::selected_text);
    style.selectable.text_pressed_active = color(UiColor::selected_text);
    style.selectable.rounding = 3.0f;
}

inline void apply_theme(nk_context* context) {
    nk_style_default(context);
    context->style.window.background = color(UiColor::background);
    context->style.window.fixed_background = nk_style_item_color(color(UiColor::background));
    context->style.window.group_border_color = color(UiColor::border);
    context->style.window.border_color = color(UiColor::border);
    context->style.text.color = color(UiColor::text);
    context->style.button.normal = nk_style_item_color(color(UiColor::surface));
    context->style.button.hover = nk_style_item_color(color(UiColor::surface_hover));
    context->style.button.active = nk_style_item_color(color(UiColor::surface_active));
    context->style.button.border_color = color(UiColor::border);
    context->style.button.text_normal = color(UiColor::text);
    context->style.button.text_hover = color(UiColor::text_bright);
    context->style.button.text_active = color(UiColor::text_bright);
    context->style.button.rounding = 4.0f;
    context->style.tab.background = nk_style_item_color(color(UiColor::surface));
    apply_dropdown_theme(context);
}

} // namespace wormedit::ui::chrome
