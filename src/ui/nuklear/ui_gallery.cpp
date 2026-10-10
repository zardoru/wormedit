#include "ui/nuklear/ui_gallery.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/vector_icons.h"

#include <algorithm>
#include <cstdlib>

namespace wormedit::ui::gallery {
namespace {

namespace metrics = wormedit::ui::metrics;
using chrome::ButtonKind;
using theme::UiColor;

constexpr float label_column = 90.0f;

void row_label(nk_context* context, const char* text) {
    nk_label_colored(context, text, NK_TEXT_LEFT, chrome::color(UiColor::text_muted));
}

void note(nk_context* context, const char* text) {
    nk_label_colored(context, text, NK_TEXT_LEFT, metrics::text_faint);
}

void section_row(nk_context* context, const char* label, float height, int controls,
                 const float* widths) {
    nk_layout_row_begin(context, NK_STATIC, height, controls + 1);
    nk_layout_row_push(context, label_column);
    row_label(context, label);
    for (int index = 0; index < controls; ++index) nk_layout_row_push(context, widths[index]);
}

void buttons(nk_context* context) {
    const float widths[] = {110, 110, 110, 110, 110};
    section_row(context, "Button", metrics::control_default, 5, widths);
    chrome::Button{"Primary", ButtonKind::primary, "Primary action"}.render(context);
    chrome::Button{"Secondary", ButtonKind::secondary, "Secondary action"}.render(context);
    chrome::Button{"Ghost", ButtonKind::ghost, "Flat until hovered"}.render(context);
    chrome::Button{"Delete...", ButtonKind::destructive, "Destructive action"}.render(context);
    chrome::Button{"Disabled", ButtonKind::secondary, "Nothing is selected", false}.render(context);
    nk_layout_row_end(context);
}

void icons(nk_context* context) {
    const float widths[] = {34, 34, 34, 300};
    section_row(context, "Icon", metrics::control_default, 4, widths);
    chrome::DrawnIconButton{vector_icons::grid, "Icon button"}.render(context);
    chrome::DrawnIconButton{vector_icons::timer, "Active icon button", true}.render(context);
    chrome::DrawnIconButton{vector_icons::plus, "Icon button"}.render(context);
    note(context, "28 px tall, selected = filled, tooltip always");
    nk_layout_row_end(context);
}

void segmented(nk_context* context, state_t& state) {
    static const char* const labels[] = {"One", "Two", "Three"};
    const float widths[] = {240, 360};
    section_row(context, "Segmented", metrics::control_default, 2, widths);
    state.segmented = chrome::Segmented{labels, 3, state.segmented, &state.segmented_focus,
                                        "Click, then Left/Right"}.render(context);
    note(context, "mutually exclusive choice, 2-4 options");
    nk_layout_row_end(context);
}

void chips(nk_context* context, state_t& state) {
    static const char* const labels[] = {"1/16", "1/8", "1/4", "1/3"};
    nk_layout_row_begin(context, NK_STATIC, metrics::control_compact * 2.0f + 16.0f, 3);
    nk_layout_row_push(context, label_column);
    row_label(context, "Chip");
    nk_layout_row_push(context, 360);
    // The grid owns its rows, so it sits in a group that the label row reserves space for.
    if (nk_group_begin(context, "gallery-chips", NK_WINDOW_NO_SCROLLBAR)) {
        const auto result = chrome::ChipGrid{labels, 4, state.chip, 4, "+"}.render(context);
        if (result.chosen >= 0) state.chip = result.chosen;
        nk_group_end(context);
    }
    nk_layout_row_push(context, 300);
    note(context, "fixed columns; outlined = adds or opens");
    nk_layout_row_end(context);
}

void checkboxes(nk_context* context, state_t& state) {
    const float widths[] = {110, 110, 110, 300};
    section_row(context, "Checkbox", metrics::control_default, 4, widths);
    using chrome::CheckState;
    chrome::Checkbox{"Off", CheckState::off, "Whole cell clicks"}.render(context);
    if (chrome::Checkbox{"On", CheckState::on, "Whole cell clicks"}.render(context))
        ++state.check_cycle;
    chrome::Checkbox{"Mixed", CheckState::mixed}.render(context);
    note(context, "16 px box, whole cell clickable");
    nk_layout_row_end(context);
}

void selection_bar(nk_context* context, state_t& state) {
    static const chrome::BarAction actions[] = {
        {"Group", ButtonKind::secondary, "Group the selected sounds"},
        {"Move", ButtonKind::secondary, "Not available yet", false},
        {"Delete", ButtonKind::destructive, "Delete the selected sounds"}};
    nk_layout_row_dynamic(context, metrics::gap_medium, 1);
    nk_spacing(context, 1);
    const auto clicked = chrome::SelectionBar{"3 selected", actions, 3}.render(context);
    if (clicked >= 0) ++state.selection_actions_clicks;
}

// The preview columns show the real themed ramps without synthesizing input or
// mutating persistent widget focus. Normal remains interactive; disabled uses Nuklear's guard.
void preview_colors(nk_style& style, bool active) {
    const auto surface = [active](auto& control) {
        control.normal = active ? control.active : control.hover;
    };
    surface(style.combo);
    surface(style.property);
    surface(style.edit);
    surface(style.slider);
    surface(style.progress);
    surface(style.checkbox);
    surface(style.option);
    surface(style.scrollv);
    style.combo.label_normal = active ? style.combo.label_active : style.combo.label_hover;
    style.property.label_normal = active ? style.property.label_active : style.property.label_hover;
    style.property.edit = style.edit;
    style.edit.text_normal = active ? style.edit.text_active : style.edit.text_hover;
    style.slider.cursor_normal = active ? style.slider.cursor_active : style.slider.cursor_hover;
    style.scrollv.cursor_normal = active ? style.scrollv.cursor_active : style.scrollv.cursor_hover;
    style.selectable.normal = active ? style.selectable.pressed : style.selectable.hover;
    style.selectable.text_normal = active ? style.selectable.text_pressed : style.selectable.text_hover;
    style.tab.background = active ? style.combo.active : style.combo.hover;
    style.contextual_button.normal = active ? style.contextual_button.active : style.contextual_button.hover;
    style.contextual_button.text_normal = active ? style.contextual_button.text_active : style.contextual_button.text_hover;
}

void stock_widget(nk_context* context, int kind, int column) {
    static int choices[4]{};
    static nk_bool selected[4]{};
    static int values[4]{12, 12, 12, 12};
    static float sliders[4]{0.4f, 0.4f, 0.4f, 0.4f};
    static nk_size progress[4]{40, 40, 40, 40};
    static nk_uint scroll[4]{}, scroll_x[4]{};
    static const char* const scroll_names[]{"scroll-normal", "scroll-hover", "scroll-active", "scroll-disabled"};
    static const char* const tree_names[]{"tree-normal", "tree-hover", "tree-active", "tree-disabled"};
    static char edits[4][64]{"Editable text", "Editable text", "Editable text", "Editable text"};
    static int lengths[4]{13, 13, 13, 13};
    static nk_bool checks[4]{nk_true, nk_true, nk_true, nk_true};
    static const char* const items[]{"Alpha", "Beta", "Gamma"};
    switch (kind) {
    case 0:
        choices[column] = chrome::safe_combo(context, items, 3, choices[column],
            static_cast<int>(metrics::menu_item_height), nk_vec2(nk_widget_width(context), 120));
        break;
    case 1: nk_selectable_label(context, "Selectable row", NK_TEXT_LEFT, &selected[column]); break;
    case 2: nk_property_int(context, "Count", 0, &values[column], 99, 1, 1); break;
    case 3: nk_edit_string(context, NK_EDIT_FIELD, edits[column], &lengths[column], 64, nk_filter_default); break;
    case 4: nk_slider_float(context, 0.0f, &sliders[column], 1.0f, 0.01f); break;
    case 5: nk_progress(context, &progress[column], 100, NK_MODIFIABLE); break;
    case 6: nk_checkbox_label(context, "Stock checkbox", &checks[column]); break;
    case 7: nk_option_label(context, "Stock option", nk_true); break;
    case 8:
        if (nk_group_scrolled_offset_begin(context, &scroll_x[column], &scroll[column], scroll_names[column], 0)) {
            nk_layout_row_dynamic(context, metrics::control_default, 1);
            for (int row = 0; row < 3; ++row) nk_label(context, "Scroll content", NK_TEXT_LEFT);
            nk_group_scrolled_end(context);
        }
        break;
    case 9:
        if (nk_group_begin(context, tree_names[column], NK_WINDOW_NO_SCROLLBAR)) {
            if (nk_tree_push_id(context, NK_TREE_NODE, "Tree header", NK_MINIMIZED, column))
                nk_tree_pop(context);
            nk_group_end(context);
        }
        break;
    case 10: nk_button_label_styled(context, &context->style.contextual_button, "Menu item"); break;
    }
}

void stock_row(nk_context* context, const char* label, int kind) {
    nk_layout_row_begin(context, NK_DYNAMIC, kind == 8 ? metrics::control_primary * 2.0f
                                                     : metrics::control_default, 5);
    nk_layout_row_push(context, 0.12f);
    row_label(context, label);
    for (int column = 0; column < 4; ++column) {
        nk_layout_row_push(context, 0.22f);
        const auto saved = context->style;
        if (column == 1 || column == 2) preview_colors(context->style, column == 2);
        if (column == 3) nk_widget_disable_begin(context);
        stock_widget(context, kind, column);
        if (column == 3) nk_widget_disable_end(context);
        context->style = saved;
    }
    nk_layout_row_end(context);
}

void stock_widgets(nk_context* context) {
    static const char* const states[]{"Normal", "Hover colors", "Active colors", "Disabled"};
    static const char* const widgets[]{"Combo", "Selectable", "Property", "Edit", "Slider",
        "Progress", "Checkbox", "Option", "Scrollbar", "Tree", "Menu item"};
    nk_layout_row_begin(context, NK_DYNAMIC, metrics::control_default, 5);
    nk_layout_row_push(context, 0.12f);
    row_label(context, "Stock");
    for (const auto* label : states) {
        nk_layout_row_push(context, 0.22f);
        row_label(context, label);
    }
    nk_layout_row_end(context);
    for (int kind = 0; kind < static_cast<int>(std::size(widgets)); ++kind)
        stock_row(context, widgets[kind], kind);
    nk_layout_row_dynamic(context, metrics::control_default, 1);
    chrome::Button{"Hover for tooltip", ButtonKind::secondary,
                    "Tooltip uses the same font and popup border"}.render(context);
}

void level_one_menu(nk_context* context, int width, int height) {
    // A level-1 surface pinned against the right-hand window edge, to show the clamped shadow.
    const auto bounds = nk_rect(static_cast<float>(width) - 190.0f, static_cast<float>(height) - 130.0f,
                                190.0f, 96.0f);
    auto* canvas = nk_window_get_canvas(context);
    chrome::paint_surface(canvas, bounds, 1,
                          nk_rect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));
    nk_draw_text(canvas, nk_rect(bounds.x + 12.0f, bounds.y + 12.0f, 150.0f, 15.0f), "Menu surface", 12,
                 context->style.font, nk_rgba(0, 0, 0, 0), chrome::color(UiColor::text));
    nk_draw_text(canvas, nk_rect(bounds.x + 12.0f, bounds.y + 34.0f, 160.0f, 15.0f), "elevation 1", 11,
                 context->style.font, nk_rgba(0, 0, 0, 0), metrics::text_faint);
    // Menus separate their groups with the separator role, the same rule menu_separator() draws.
    const auto rule_y = bounds.y + 58.0f;
    nk_stroke_line(canvas, bounds.x + 8.0f, rule_y, bounds.x + bounds.w - 8.0f, rule_y, 1.0f,
                   chrome::color(UiColor::separator));
}

void level_two_dialog(nk_context* context, int width, int height) {
    const auto bounds = nk_rect(static_cast<float>(width) * 0.5f - 170.0f,
                                static_cast<float>(height) - 190.0f, 340.0f, 150.0f);
    const auto window = chrome::surface_window_begin(
        context, "UI gallery dialog", bounds,
        NK_WINDOW_NO_SCROLLBAR, 2);
    if (window.open) {
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        nk_label_colored(context, "Dialog surface", NK_TEXT_LEFT, chrome::color(UiColor::text));
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        note(context, "elevation 2, shadow falls on the page");
        nk_layout_row_dynamic(context, metrics::control_primary, 2);
        chrome::Button{"Cancel", ButtonKind::secondary}.render(context);
        chrome::Button{"Apply", ButtonKind::primary}.render(context);
    }
    chrome::surface_window_end(context, window);
}

} // namespace

auto requested() -> bool {
    const auto* value = std::getenv("WORMEDIT_UI_GALLERY");
    return value && value[0] != '\0' && value[0] != '0';
}

void render(nk_context* context, state_t& state, int width, int height) {
    if (!context || width <= 0 || height <= 0) return;
    if (nk_begin(context, "UI gallery",
                 nk_rect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                 NK_WINDOW_BACKGROUND)) {
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        note(context, "Controls: heights 24 / 28 / 32, radius 4 control, 12 chip, 6 bar, 8 surface, gaps 4 / 8 / 16");
        buttons(context);
        icons(context);
        segmented(context, state);
        chips(context, state);
        checkboxes(context, state);
        selection_bar(context, state);
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        note(context, "Lime accent = a mode or toggle is on. Blue = the chosen value, selection or primary action.");
        note(context, "Stock widgets: Nuklear's own controls with the same heights, radii and state colors");
        stock_widgets(context);
        level_one_menu(context, width, height);
    }
    nk_end(context);
    level_two_dialog(context, width, height);
}

} // namespace wormedit::ui::gallery
