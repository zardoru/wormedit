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
}

void level_two_dialog(nk_context* context, int width, int height) {
    const auto bounds = nk_rect(static_cast<float>(width) * 0.5f - 120.0f,
                                static_cast<float>(height) - 190.0f, 280.0f, 150.0f);
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
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_layout_row_dynamic(context, 28.0f, 1);
        note(context, "Controls: heights 24 / 28 / 32, radius 4 control, 12 chip, 6 bar, 8 surface, gaps 4 / 8 / 16");
        buttons(context);
        icons(context);
        segmented(context, state);
        chips(context, state);
        checkboxes(context, state);
        selection_bar(context, state);
        nk_layout_row_dynamic(context, 28.0f, 1);
        note(context, "Lime accent = a mode or toggle is on. Blue = the chosen value, selection or primary action.");
        level_one_menu(context, width, height);
    }
    nk_end(context);
    level_two_dialog(context, width, height);
}

} // namespace wormedit::ui::gallery
