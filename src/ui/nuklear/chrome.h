#pragma once

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/text_field.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/ui_metrics.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>
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
    style.rounding = metrics::radius_control;
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
        nk_stroke_rect(nk_window_get_canvas(context), bounds, metrics::radius_control, 1.0f,
                       color(UiColor::border_hover));
        if (tooltip && tooltip[0] != '\0' && nuklear::tooltip::hovered(context, bounds))
            nuklear::tooltip::text(context, tooltip);
    } else if (selected) {
        nk_stroke_rect(nk_window_get_canvas(context), bounds, metrics::radius_control, 1.0f,
                       color(UiColor::selected));
    }
}

// Draws a chord right-aligned inside a row already laid out at `bounds` (menus, the command palette).
inline void draw_row_shortcut(nk_context* context, struct nk_rect bounds, const char* shortcut_text,
                              struct nk_color tint) {
    if (!shortcut_text || !shortcut_text[0]) return;
    const auto* font = context->style.font;
    const auto length = static_cast<int>(std::strlen(shortcut_text));
    const float width = font->width(font->userdata, font->height, shortcut_text, length);
    nk_draw_text(nk_window_get_canvas(context),
                 nk_rect(bounds.x + bounds.w - width - metrics::gap_medium,
                         bounds.y + (bounds.h - font->height) * 0.5f, width, font->height),
                 shortcut_text, length, font, nk_rgba(0, 0, 0, 0), tint);
}

// A menu row reserves the same check gutter even when unchecked. Shortcut text is
// a separate right-aligned draw, so labels and remapped chords keep their alignment.
inline auto menu_item(nk_context* context, const char* label, const char* shortcut_text,
                      bool checked, bool enabled, bool close_popup = true) -> bool {
    const auto bounds = nk_widget_bounds(context);
    if (!enabled) nk_widget_disable_begin(context);
    auto style = context->style.menu_button;
    style.text_alignment = NK_TEXT_LEFT;
    style.padding.x += metrics::gap_large;
    const bool chosen = nk_button_label_styled(context, &style, label) != 0;
    auto* canvas = nk_window_get_canvas(context);
    const auto check_tint = nk_rgb_factor(style.text_normal, style.color_factor_text);
    if (checked) {
        const float x = bounds.x + metrics::gap_small;
        const float y = bounds.y + bounds.h * 0.5f;
        nk_stroke_line(canvas, x, y, x + 4.0f, y + 4.0f, 1.5f, check_tint);
        nk_stroke_line(canvas, x + 4.0f, y + 4.0f, x + 11.0f, y - 4.0f, 1.5f, check_tint);
    }
    draw_row_shortcut(context, bounds, shortcut_text,
                      nk_rgb_factor(color(UiColor::text_muted), style.color_factor_text));
    if (!enabled) nk_widget_disable_end(context);
    if (chosen && enabled && close_popup) nk_popup_close(context);
    return chosen && enabled;
}

// Components --------------------------------------------------------------------------------
// Each is drawn once, in components.cpp, from the tokens in ui_metrics.h. Rows that hold them
// use metrics::control_compact / control_default / control_primary. A disabled component never
// reports an action; its tooltip should say why it is disabled.

enum class ButtonKind { primary, secondary, ghost, destructive };
enum class ControlState { normal, hover, active, disabled };

struct ControlLook {
    nk_color fill;
    nk_color stroke; // alpha 0 draws no border
    nk_color text;
};

auto button_look(ButtonKind kind, ControlState state) -> ControlLook;
auto chip_look(bool chosen, ControlState state) -> ControlLook;

struct Button {
    const char* label;
    ButtonKind kind{ButtonKind::secondary};
    const char* tooltip{};
    bool enabled{true};

    // Fills the next layout cell. True when clicked.
    auto render(nk_context* context) const -> bool;
};

enum class CheckState { off, on, mixed };
constexpr auto toggled(CheckState state) -> CheckState {
    return state == CheckState::on ? CheckState::off : CheckState::on;
}

struct Checkbox {
    const char* label;
    CheckState state{CheckState::off};
    const char* tooltip{};
    bool enabled{true};

    // The whole cell is the hit area. True when toggled; the caller stores toggled(state).
    auto render(nk_context* context) const -> bool;
};

// Which of `count` equal segments of `bounds` holds x; -1 outside.
auto segment_at(struct nk_rect bounds, int count, float x) -> int;
// Left/Right step, clamped (no wrap).
auto segment_step(int selected, int count, int delta) -> int;

struct Segmented {
    const char* const* labels;
    int count;
    int selected;
    bool* focused{}; // owned by the caller; set by a click, cleared by a click elsewhere
    const char* tooltip{};
    bool enabled{true};

    // Fills the next layout cell with equal segments. Returns the chosen index (Left/Right
    // while focused also move it), or `selected` when nothing changed.
    auto render(nk_context* context) const -> int;
};

// One chip in the caller's current layout row (the grid uses the same visual style).
struct Chip {
    const char* label;
    bool chosen{};
    bool enabled{true};
    const char* tooltip{};
    auto render(nk_context* context) const -> bool;
};

struct ChipGrid {
    struct Result {
        int chosen{-1};
        bool add{};
    };
    const char* const* labels;
    int count;
    int chosen{-1};
    int columns{4};
    const char* add_label{}; // outlined trailing chip; the caller opens whatever it adds
    bool enabled{true};

    // Lays out its own rows of control_compact height, `columns` chips per row, so rows align.
    auto render(nk_context* context) const -> Result;
};

struct BarAction {
    const char* label;
    ButtonKind kind{ButtonKind::secondary};
    const char* tooltip{};
    bool enabled{true};
};

struct SelectionBar {
    const char* summary;
    const BarAction* actions;
    int action_count;

    static constexpr float height = metrics::control_primary + metrics::gap_medium;

    // One full-width row. Returns the clicked action index, or -1.
    auto render(nk_context* context) const -> int;
};

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

// Owns one native cell through glyph drawing and feedback. Its scope preserves an
// enclosing disable flag; disabled color factors stay in the cell's local style.
class NativeIconCell {
public:
    NativeIconCell(nk_context* context, bool selected, bool enabled, const char* tooltip);
    ~NativeIconCell();
    NativeIconCell(const NativeIconCell&) = delete;
    auto operator=(const NativeIconCell&) -> NativeIconCell& = delete;

    auto bounds() const -> struct nk_rect { return bounds_; }
    auto tint() const -> nk_color { return tint_; }
    auto is_clicked() const -> bool { return clicked_; }

private:
    nk_context* context_;
    nk_window* window_;
    struct nk_rect bounds_;
    nk_color tint_;
    bool selected_;
    const char* tooltip_;
    nk_bool was_disabled_;
    bool clicked_;
};

struct IconButton {
    lucide::Glyph glyph;
    const char* tooltip;
    bool selected{false};
    bool enabled{true};

    static constexpr float width = 34.0f;

    static void layout_columns(nk_context* context, int count = 1, float column_width = width) {
        for (int index = 0; index < count; ++index)
            nk_layout_row_template_push_static(context, column_width);
    }

    auto render(nk_context* context) const -> bool {
        const NativeIconCell cell{context, selected, enabled, tooltip};
        lucide::draw(context, cell.bounds(), glyph, cell.tint());
        return cell.is_clicked();
    }
};

// Icon button whose glyph is drawn by a callback, for shapes outside the Lucide raster pack.
struct DrawnIconButton {
    void (*draw)(struct nk_command_buffer*, struct nk_rect, struct nk_color);
    const char* tooltip;
    bool selected{false};
    bool enabled{true};

    auto render(nk_context* context) const -> bool {
        const NativeIconCell cell{context, selected, enabled, tooltip};
        draw(nk_window_get_canvas(context), cell.bounds(), cell.tint());
        return cell.is_clicked();
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

inline constexpr float menu_padding = metrics::gap_medium;
inline constexpr float menu_radius = metrics::radius_surface;

// Region a surface's shadow may paint: its reach around `bounds`, clamped to `limit`.
// Backends reject scissors that reach outside the render target, so top-level windows pass the
// app viewport (tooltip::viewport()), including popups that extend beyond their host.
auto surface_shadow_clip(struct nk_rect bounds, int elevation, struct nk_rect limit)
    -> struct nk_rect;

// Paints one surface level: the shadow steps, then the rounded fill and 1 px border_hover
// stroke, then restores the buffer's previous clip. Draw it before the surface's content.
void paint_surface(struct nk_command_buffer* canvas, struct nk_rect bounds, int elevation,
                   struct nk_rect limit);

// Call right after a successful nk_menu_begin_*: paints the menu as a rounded panel with a soft
// drop shadow. Popup content has an independent clip and may extend beyond a short
// toolbar or header host; its surface must use the render target, not that host.
inline void menu_panel(nk_context* context) {
    auto* popup = context->current;
    const auto& viewport = nuklear::tooltip::viewport();
    paint_surface(&popup->buffer, popup->bounds, 1,
                  viewport.w > 0.0f && viewport.h > 0.0f ? viewport : popup->bounds);
}

// Menubar menus and context menus share this surface: menu_panel() plus transparent
// window fills, so neither the square fill Nuklear adds below the last row nor a
// nested group's body covers the rounded panel.
// Call right after the menu or popup begins; pass the result to menu_surface_end().
inline auto menu_surface_begin(nk_context* context) -> MenuSurface {
    menu_panel(context);
    auto& window = context->style.window;
    const MenuSurface saved{window.background, window.fixed_background};
    window.background = nk_rgba(0, 0, 0, 0);
    window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    return saved;
}

// Call right after nk_menu_end()/nk_popup_end().
inline void menu_surface_end(nk_context* context, MenuSurface saved) {
    context->style.window.background = saved.background;
    context->style.window.fixed_background = saved.fixed_background;
}

// Keep both the popup body and its end-of-panel padding transparent while the
// shared rounded popup surface supplies the fill. Every successful begin pairs with end.
inline auto safe_combo_begin_label(nk_context* context, const char* label,
                                   struct nk_vec2 size) -> nk_bool {
    if (!context || !context->current || !context->current->layout) return nk_false;
    if (popup_panel(context)) {
        nk_label(context, label, NK_TEXT_LEFT);
        return nk_false;
    }
    auto& window = context->style.window;
    if (!nk_style_push_color(context, &window.background, nk_rgba(0, 0, 0, 0))) return nk_false;
    if (!nk_style_push_style_item(context, &window.fixed_background,
                                  nk_style_item_color(nk_rgba(0, 0, 0, 0)))) {
        nk_style_pop_color(context);
        return nk_false;
    }
    if (nk_combo_begin_label(context, label, size)) {
        menu_panel(context);
        return nk_true;
    }
    nk_style_pop_style_item(context);
    nk_style_pop_color(context);
    return nk_false;
}

inline void safe_combo_end(nk_context* context) {
    nk_combo_end(context);
    nk_style_pop_style_item(context);
    nk_style_pop_color(context);
}

template<class Row, class Label>
inline auto safe_combo(nk_context* context, std::span<const Row> items,
                       int selected, int item_height, struct nk_vec2 size, Label label) -> int {
    const auto count = static_cast<int>(items.size());
    if (!context || !context->current || !context->current->layout || items.empty())
        return selected;
    selected = std::clamp(selected, 0, count - 1);
    const auto height = item_height <= metrics::control_compact
        ? metrics::control_compact : metrics::control_default;
    if (popup_panel(context)) {
        for (int index = 0; index < count; ++index) {
            nk_layout_row_dynamic(context, height, 1);
            if (nk_select_label(context, std::string_view{std::invoke(label, items[index])}.data(),
                                NK_TEXT_LEFT, selected == index))
                selected = index;
        }
        return selected;
    }
    const auto& window = context->style.window;
    // Padding surrounds the rows; spacing belongs only between adjacent rows.
    size.y = std::min(size.y, count * height + (count - 1) * window.spacing.y +
                      2.0f * window.combo_padding.y);
    const auto selected_label = std::string_view{std::invoke(label, items[selected])};
    if (safe_combo_begin_label(context, selected_label.data(), size)) {
        nk_layout_row_dynamic(context, height, 1);
        for (int index = 0; index < count; ++index)
            if (nk_combo_item_label(context,
                    std::string_view{std::invoke(label, items[index])}.data(), NK_TEXT_LEFT))
                selected = index;
        // Nuklear includes the final row's spacing in its scroll extent, although
        // there is no following row. Keep it consistent with the fitted height.
        context->current->layout->row.height -= window.spacing.y;
        safe_combo_end(context);
    }
    return selected;
}

inline auto safe_combo(nk_context* context, const char* const* items, int count,
                       int selected, int item_height, struct nk_vec2 size) -> int {
    if (!items || count <= 0) return selected;
    return safe_combo(context, std::span<const char* const>{items, static_cast<std::size_t>(count)},
                      selected, item_height, size, std::identity{});
}

// Top-level windows (dialogs, floating tools) that cast a shadow over the workspace. The
// window's own fill is transparent and paint_surface() draws the shadow, fill and border.
// Always pass the result to surface_window_end(), which also calls nk_end().
struct SurfaceWindow {
    MenuSurface saved;
    bool open{};
};

inline auto surface_window_begin(nk_context* context, const char* name, struct nk_rect bounds,
                                 nk_flags flags, int elevation) -> SurfaceWindow {
    auto& window = context->style.window;
    SurfaceWindow surface{MenuSurface{window.background, window.fixed_background}, false};
    window.background = nk_rgba(0, 0, 0, 0);
    window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    surface.open = nk_begin(context, name, bounds, flags) != 0;
    if (surface.open) {
        const auto& viewport = nuklear::tooltip::viewport();
        paint_surface(&context->current->buffer, context->current->bounds, elevation,
                      viewport.w > 0.0f && viewport.h > 0.0f ? viewport : context->current->bounds);
    }
    return surface;
}

inline void surface_window_end(nk_context* context, const SurfaceWindow& surface) {
    nk_end(context);
    context->style.window.background = surface.saved.background;
    context->style.window.fixed_background = surface.saved.fixed_background;
}

// A thin rule between menu groups, with the same token gap as the menu padding.
inline void menu_separator(nk_context* context) {
    nk_layout_row_dynamic(context, metrics::menu_separator_height, 1);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    const auto y = bounds.y + bounds.h * 0.5f;
    nk_stroke_line(nk_window_get_canvas(context), bounds.x + metrics::gap_small, y, bounds.x + bounds.w - metrics::gap_small, y,
                   1.0f, color(UiColor::separator));
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
    style.window.contextual_border = 1.0f;
    style.window.combo_border = 0.0f; // safe_combo paints the rounded popup border.
    style.window.popup_border = 1.0f;
    style.window.popup_padding = style.window.combo_padding = nk_vec2(metrics::gap_small, metrics::gap_small);
    style.window.contextual_padding = nk_vec2(metrics::gap_small, metrics::gap_small);
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
    style.combo.rounding = metrics::radius_control;
    for (auto* button : {&style.combo.button, &style.contextual_button}) {
        button->normal = surface;
        button->hover = hover;
        button->active = active;
        button->border_color = nk_rgba(0, 0, 0, 0);
        button->text_normal = color(UiColor::text);
        button->text_hover = color(UiColor::text_bright);
        button->text_active = color(UiColor::text_bright);
        button->rounding = metrics::radius_control;
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
    style.menu_button.rounding = metrics::radius_control;
    style.menu_button.text_normal = color(UiColor::text);
    style.menu_button.text_hover = color(UiColor::text_bright);
    style.menu_button.text_active = color(UiColor::text_bright);
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
    style.selectable.rounding = metrics::radius_control;
}

// Edit fields, property boxes, sliders and progress bars share one control surface, border and
// radius. Sizes and padding stay as Nuklear set them, so only colors and rounding change here.
inline void apply_field_theme(nk_context* context) {
    auto& style = context->style;
    const auto surface = nk_style_item_color(color(UiColor::surface));
    const auto hover = nk_style_item_color(color(UiColor::surface_hover));
    const auto active = nk_style_item_color(color(UiColor::surface_active));
    const auto border = color(UiColor::border);
    const auto text = color(UiColor::text);
    const auto text_bright = color(UiColor::text_bright);
    const auto accent = color(UiColor::selected);
    auto& edit = style.edit;
    edit.normal = surface;
    edit.hover = hover;
    edit.active = active;
    edit.border_color = border;
    edit.rounding = metrics::radius_control;
    edit.cursor_normal = text_bright;
    edit.cursor_hover = text_bright;
    edit.cursor_text_normal = text;
    edit.cursor_text_hover = text_bright;
    edit.text_normal = text;
    edit.text_hover = text_bright;
    edit.text_active = text_bright;
    edit.selected_normal = accent;
    edit.selected_hover = accent;
    edit.selected_text_normal = text_bright;
    edit.selected_text_hover = text_bright;
    auto& property = style.property;
    property.normal = surface;
    property.hover = hover;
    property.active = active;
    property.border_color = border;
    property.label_normal = text;
    property.label_hover = text_bright;
    property.label_active = text_bright;
    property.rounding = metrics::radius_control;
    property.edit = edit;
    // Property values are already inside a control. Nuklear sizes their editor
    // from the measured number, so standalone-field insets would clip its text.
    property.edit.padding = nk_vec2(0, 0);
    property.edit.border = 0.0f;
    auto& slider = style.slider;
    slider.normal = surface;
    slider.hover = hover;
    slider.active = active;
    slider.border_color = border;
    slider.bar_normal = color(UiColor::surface_hover);
    slider.bar_hover = color(UiColor::surface_active);
    slider.bar_active = color(UiColor::surface_active);
    slider.bar_filled = accent;
    slider.cursor_normal = nk_style_item_color(text);
    slider.cursor_hover = nk_style_item_color(text_bright);
    slider.cursor_active = nk_style_item_color(text_bright);
    slider.rounding = metrics::radius_control;
    auto& progress = style.progress;
    progress.normal = surface;
    progress.hover = hover;
    progress.active = active;
    progress.border_color = border;
    progress.cursor_normal = nk_style_item_color(accent);
    progress.cursor_hover = nk_style_item_color(accent);
    progress.cursor_active = nk_style_item_color(accent);
    progress.rounding = metrics::radius_control;
    progress.cursor_rounding = metrics::radius_control;
}

// Scrollbars sit flat on their panel; the thumb is a rounded surface that lights up on hover.
inline void apply_scrollbar_theme(nk_context* context) {
    const auto clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    for (auto* scroll : {&context->style.scrollh, &context->style.scrollv}) {
        scroll->normal = clear;
        scroll->hover = clear;
        scroll->active = clear;
        scroll->border_color = nk_rgba(0, 0, 0, 0);
        scroll->border = 0.0f;
        scroll->rounding = metrics::radius_control;
        scroll->cursor_normal = nk_style_item_color(color(UiColor::surface_active));
        scroll->cursor_hover = nk_style_item_color(color(UiColor::surface_hover));
        scroll->cursor_active = nk_style_item_color(color(UiColor::border_hover));
        scroll->cursor_border_color = nk_rgba(0, 0, 0, 0);
        scroll->border_cursor = 0.0f;
        scroll->rounding_cursor = metrics::radius_control;
    }
}

// Stock checkboxes and option buttons: the same surface and border as the fields, with the
// selected mark in the accent. Spacing and sizes stay as Nuklear set them.
inline void apply_toggle_theme(nk_context* context) {
    auto& style = context->style;
    const auto surface = nk_style_item_color(color(UiColor::surface));
    const auto hover = nk_style_item_color(color(UiColor::surface_hover));
    const auto active = nk_style_item_color(color(UiColor::surface_active));
    for (auto* toggle : {&style.checkbox, &style.option}) {
        toggle->normal = surface;
        toggle->hover = hover;
        toggle->active = active;
        toggle->border_color = color(UiColor::border);
        toggle->border = 1.0f;
        toggle->cursor_normal = nk_style_item_color(color(UiColor::selected));
        toggle->cursor_hover = nk_style_item_color(color(UiColor::selected));
        toggle->text_normal = color(UiColor::text);
        toggle->text_hover = color(UiColor::text_bright);
        toggle->text_active = color(UiColor::text_bright);
    }
}

// Tooltips take the border used by menus, so hints read as part of the same surface family.
inline void apply_tooltip_theme(nk_context* context) {
    context->style.window.tooltip_border_color = color(UiColor::border_hover);
    context->style.window.tooltip_border = 1.0f;
}

// Tree headers and tab rows: flat on the panel, with the same hover and active fills as the
// menus. Node and tab buttons are the chevrons that open and close the section.
inline void apply_header_theme(nk_context* context) {
    auto& style = context->style;
    const auto clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    const auto hover = nk_style_item_color(color(UiColor::surface_hover));
    const auto active = nk_style_item_color(color(UiColor::surface_active));
    style.tab.background = clear;
    style.tab.border_color = color(UiColor::border);
    style.tab.text = color(UiColor::text);
    style.tab.rounding = metrics::radius_control;
    for (auto* button : {&style.tab.tab_maximize_button, &style.tab.tab_minimize_button,
                         &style.tab.node_maximize_button, &style.tab.node_minimize_button}) {
        button->normal = clear;
        button->hover = hover;
        button->active = active;
        button->border_color = nk_rgba(0, 0, 0, 0);
        button->border = 0.0f;
        button->text_normal = color(UiColor::text_muted);
        button->text_hover = color(UiColor::text_bright);
        button->text_active = color(UiColor::text_bright);
        button->rounding = metrics::radius_control;
    }
}

inline void apply_theme(nk_context* context) {
    nk_style_default(context);
    context->style.window.spacing = nk_vec2(metrics::gap_small, metrics::gap_small);
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
    context->style.button.rounding = metrics::radius_control;
    context->style.tab.background = nk_style_item_color(color(UiColor::surface));
    apply_dropdown_theme(context);
    apply_field_theme(context);
    apply_scrollbar_theme(context);
    apply_toggle_theme(context);
    apply_tooltip_theme(context);
    apply_header_theme(context);
}

} // namespace wormedit::ui::chrome
