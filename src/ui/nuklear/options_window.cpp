#include "ui/nuklear/options_window.h"
#include "wormedit/nuklear_config.h"
#include <array>

namespace {
constexpr std::array<const char*, WORM_UI_OPTIONS_CATEGORY_COUNT> category_names{
    "General", "Editor", "Playback & audio", "Display", "Input & shortcuts", "External players"};
constexpr std::array<const char*, WORM_UI_OPTIONS_CATEGORY_COUNT> category_descriptions{
    "Autosave, recent files, and confirmation when closing unsaved work.",
    "Defaults for editing new charts and minimap interaction.",
    "Playback rate, pitch, and audio preferences.",
    "Appearance, interface scale, and window layout.",
    "Keyboard shortcuts and shortcut profiles.",
    "External player applications and launch arguments."};
void placeholder(nk_context* context, const WormUiOptionsView* view, WormUiOptionsAction*) {
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, category_descriptions[view->category]);
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Changes stay in a draft until Apply. Revert reloads this page's current settings; Restore defaults changes only its draft.");
}
std::array<WormUiOptionsPageRenderer, WORM_UI_OPTIONS_CATEGORY_COUNT> pages{
    placeholder, placeholder, placeholder, placeholder, placeholder, placeholder};
void emit(WormUiOptionsAction& action, const WormUiOptionsView& view, WormUiOptionsActionKind kind) {
    if (action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = kind, .revision = view.revision, .category = view.category};
}
void render_categories(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    if (!nk_group_begin(context, "Options categories", NK_WINDOW_NO_SCROLLBAR)) return;
    nk_layout_row_dynamic(context, 32, 1);
    for (int category = 0; category < WORM_UI_OPTIONS_CATEGORY_COUNT; ++category) {
        nk_bool selected = view.category == category;
        const bool changed = nk_selectable_label(context, category_names[category], NK_TEXT_LEFT, &selected) != 0;
        if (!changed || !selected) continue;
        emit(action, view, WORM_UI_OPTIONS_SELECT_CATEGORY);
        action.category = static_cast<WormUiOptionsCategory>(category);
    }
    nk_group_end(context);
}
void render_page(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    if (!nk_group_begin(context, "Options page", 0)) return;
    nk_layout_row_dynamic(context, 28, 1);
    nk_label(context, category_names[view.category], NK_TEXT_LEFT);
    pages[view.category](context, &view, &action);
    nk_layout_row_dynamic(context, 32, 1);
    for (size_t i = 0; i < view.diagnostic_count; ++i)
        nk_label_wrap(context, view.diagnostics[i].message);
    nk_group_end(context);
}
void render_footer(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    nk_layout_row_dynamic(context, 28, 4);
    const bool disabled = !view.can_apply || !view.changed;
    if (disabled) nk_widget_disable_begin(context);
    const bool apply = nk_button_label(context, "Apply") != 0;
    if (disabled) nk_widget_disable_end(context);
    if (apply && !disabled) emit(action, view, WORM_UI_OPTIONS_APPLY);
    if (nk_button_label(context, "Revert")) emit(action, view, WORM_UI_OPTIONS_REVERT);
    if (nk_button_label(context, "Restore defaults")) emit(action, view, WORM_UI_OPTIONS_DEFAULTS);
    if (nk_button_label(context, "Close")) emit(action, view, WORM_UI_OPTIONS_CLOSE);
    nk_layout_row_dynamic(context, 34, 1);
    nk_label_wrap(context, view.status);
}
} // namespace
bool worm_ui_nuklear_register_options_page(WormUiOptionsCategory category, WormUiOptionsPageRenderer renderer) {
    if (category < WORM_UI_OPTIONS_GENERAL || category >= WORM_UI_OPTIONS_CATEGORY_COUNT || !renderer) return false;
    pages[category] = renderer;
    return true;
}
bool worm_ui_nuklear_options_view_valid(const WormUiOptionsView* view) {
    if (!view || !view->visible) return view != nullptr;
    if (view->category < WORM_UI_OPTIONS_GENERAL || view->category >= WORM_UI_OPTIONS_CATEGORY_COUNT ||
        !view->theme || !view->dock_layout || !view->shortcut_profile_path || !view->status ||
        view->diagnostic_count > 16 || view->player_count > 64) return false;
    if ((view->diagnostic_count && !view->diagnostics) || (view->player_count && !view->players)) return false;
    for (size_t i = 0; i < view->diagnostic_count; ++i)
        if (!view->diagnostics[i].message) return false;
    return true;
}
void worm_ui_nuklear_render_options_window(nk_context* context, const WormUiOptionsView* view,
    WormUiOptionsAction* action) {
    if (!context || !action || !worm_ui_nuklear_options_view_valid(view) || !view->visible) return;
    const auto flags = NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE;
    if (nk_begin(context, "Options", nk_rect(180, 110, 800, 450), flags)) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Program settings", NK_TEXT_LEFT);
        constexpr float ratios[]{0.28f, 0.72f};
        nk_layout_row(context, NK_DYNAMIC, 290, 2, ratios);
        render_categories(context, *view, *action);
        render_page(context, *view, *action);
        render_footer(context, *view, *action);
    }
    nk_end(context);
}
