#include "ui/nuklear/options_window.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
#include "ui/nuklear/theme.h"
#include <cstdio>
#include <string_view>
#include "ui/nuklear/shortcuts_panel.h"
#include "wormedit/nuklear_config.h"
#include <array>
#include <algorithm>

namespace {
wormedit::ui::chrome::Dialog options_dialog;
constexpr std::array<const char*, WORM_UI_OPTIONS_CATEGORY_COUNT> category_names{
    "General", "Editor", "Playback & audio", "Appearance", "Input & shortcuts", "External players"};
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
void edit_integer(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action,
    const char* label, WormUiOptionsField field, uint32_t value, int minimum, int maximum) {
    nk_layout_row_dynamic(context, 28, 1);
    int draft = static_cast<int>(value);
    nk_property_int(context, label, minimum, &draft, maximum, 1, 1);
    if (draft == static_cast<int>(value) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = field, .integer = static_cast<uint64_t>(draft)};
}
void edit_toggle(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action,
    const char* label, WormUiOptionsField field, bool value) {
    nk_layout_row_dynamic(context, 28, 1);
    nk_bool draft = value;
    if (!nk_checkbox_label(context, label, &draft) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = field, .boolean = draft != 0};
}
void general_page(nk_context* context, const WormUiOptionsView* view, WormUiOptionsAction* action) {
    edit_integer(context, *view, *action, "Autosave interval (minutes)", WORM_UI_OPTIONS_FIELD_AUTOSAVE,
        view->autosave_minutes, 1, 30);
    edit_integer(context, *view, *action, "Recent-files limit", WORM_UI_OPTIONS_FIELD_RECENT_FILES,
        view->recent_files_count, 1, 100);
    edit_toggle(context, *view, *action, "Confirm closing unsaved work", WORM_UI_OPTIONS_FIELD_CONFIRM_CLOSE,
        view->confirm_unsaved_close);
}
void scroll_directions(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    constexpr std::array<const char*, 6> families{"Beat keys", "Pop’n buttons", "Dance", "Pump", "Keyboard", "Other modes"};
    constexpr const char* directions[]{"Profile default", "Up", "Down"};
    for (size_t index = 0; index < families.size(); ++index) {
        nk_layout_row_dynamic(context, 28, 2);
        nk_label(context, families[index], NK_TEXT_LEFT);
        const auto value = static_cast<int>(view.scroll_directions[index]);
        const auto selected = wormedit::ui::chrome::safe_combo(context, directions, 3, value, 24, nk_vec2(180, 120));
        if (selected == value || action.kind != WORM_UI_OPTIONS_NONE) continue;
        action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
            .field = WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION, .integer = static_cast<uint64_t>(selected),
            .family_index = static_cast<uint32_t>(index)};
    }
}
void move_collision_policy(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    constexpr const char* labels[]{"Reject", "Move it to a BGM lane"};
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "When a move lands on a note", NK_TEXT_LEFT);
    const auto value = static_cast<int>(view.move_collision_policy);
    const auto selected = wormedit::ui::chrome::safe_combo(context, labels, 2, value, 24, nk_vec2(240, 100));
    if (selected == value || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY, .integer = static_cast<uint64_t>(selected)};
}
void editor_page(nk_context* context, const WormUiOptionsView* view, WormUiOptionsAction* action) {
    edit_toggle(context, *view, *action, "Play sounds on note clicks",
        WORM_UI_OPTIONS_FIELD_AUDITION_ON_CLICK, view->audition_on_click);
    edit_integer(context, *view, *action, "Default snap denominator", WORM_UI_OPTIONS_FIELD_SNAP, view->default_snap, 1, 1920);
    move_collision_policy(context, *view, *action);
    scroll_directions(context, *view, *action);
    edit_toggle(context, *view, *action, "Invert field scroll wheel",
        WORM_UI_OPTIONS_FIELD_INVERT_SCROLL_WHEEL, view->invert_scroll_wheel);
    edit_integer(context, *view, *action, "Following anchor (%)", WORM_UI_OPTIONS_FIELD_ANCHOR, view->playhead_anchor_percent, 0, 100);
    edit_toggle(context, *view, *action, "Show note-kind palette", WORM_UI_OPTIONS_FIELD_NOTE_PALETTE, view->note_kind_palette_visible);
    edit_toggle(context, *view, *action, "Show lane legend", WORM_UI_OPTIONS_FIELD_LANE_LEGEND, view->lane_key_legend_visible);
    edit_integer(context, *view, *action, "Area-band opacity (0–255)", WORM_UI_OPTIONS_FIELD_AREA_ALPHA, view->area_band_alpha, 0, 255);
    edit_toggle(context, *view, *action, "Minimap click moves paused playhead", WORM_UI_OPTIONS_FIELD_MINIMAP_CLICK,
        view->minimap_click_moves_paused_playhead);
}
void audio_page(nk_context* context, const WormUiOptionsView* view, WormUiOptionsAction* action) {
    edit_integer(context, *view, *action, "Keysound memory (MiB)", WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY,
        view->keysound_budget_mib, 256, 8192);
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Raising the budget loads skipped keysounds for the open project. Lowering it limits future loads and keeps playing sounds in memory.");
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "osu! hitsound skin folder (empty uses bundled samples)", NK_TEXT_LEFT);
    std::array<char, WORM_UI_OPTIONS_TEXT_CAPACITY> folder{};
    std::snprintf(folder.data(), folder.size(), "%s", view->osu_skin_directory.c_str());
    nk_layout_row_dynamic(context, 28, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context,
        NK_EDIT_FIELD, folder.data(), folder.size(), nk_filter_default);
    if (view->osu_skin_directory != folder.data() && action->kind == WORM_UI_OPTIONS_NONE) {
        *action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view->revision,
            .category = view->category, .field = WORM_UI_OPTIONS_FIELD_OSU_SKIN_DIRECTORY};
        std::snprintf(action->text, sizeof(action->text), "%s", folder.data());
    }
}
void field_scheme_picker(nk_context* context, const WormUiOptionsView* view, WormUiOptionsAction* action) {
    const auto& schemes = wormedit::ui::theme::field_schemes();
    std::vector<const char*> labels;
    int selected = 0;
    for (std::size_t index = 0; index < schemes.size(); ++index) {
        labels.push_back(schemes[index].name.c_str());
        if (view->field_scheme && schemes[index].id == view->field_scheme) selected = static_cast<int>(index);
    }
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "Default field skin", NK_TEXT_LEFT);
    if (labels.empty()) { nk_label(context, "Wormedit", NK_TEXT_LEFT); return; }
    const auto value = wormedit::ui::chrome::safe_combo(context, labels.data(),
        static_cast<int>(labels.size()), selected, 24, nk_vec2(240, 280));
    if (value == selected || action->kind != WORM_UI_OPTIONS_NONE) return;
    *action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view->revision, .category = view->category,
        .field = WORM_UI_OPTIONS_FIELD_FIELD_SCHEME};
    snprintf(action->text, sizeof(action->text), "%s", schemes[static_cast<std::size_t>(value)].id.c_str());
}
void layout_field_scheme_picker(nk_context* context, const WormUiOptionsView& view,
                               WormUiOptionsAction& action) {
    const auto* key = view.current_field_layout_key;
    if (!key || !*key) {
        nk_layout_row_dynamic(context, 28, 1);
        nk_label(context, "Open a chart to choose its layout's field skin.", NK_TEXT_LEFT);
        return;
    }
    std::vector<wormedit::ui::theme::field_lane_t> lanes;
    for (const auto& lane : std::span(view.current_field_layout_lanes, view.current_field_layout_lane_count))
        lanes.push_back({lane.display_order, lane.stage, lane.key_color, lane.role, lane.lane_id});
    std::vector<const char*> labels{"Use default"};
    std::vector<const char*> ids{""};
    int selected = 0;
    for (const auto& scheme : wormedit::ui::theme::field_schemes()) {
        if (!wormedit::ui::theme::field_scheme_supports_layout(scheme.id, lanes)) continue;
        ids.push_back(scheme.id.c_str());
        labels.push_back(scheme.name.c_str());
        if (view.current_layout_field_scheme && scheme.id == view.current_layout_field_scheme)
            selected = static_cast<int>(ids.size() - 1);
    }
    std::string unavailable;
    if (selected == 0 && view.current_layout_field_scheme && *view.current_layout_field_scheme) {
        unavailable = std::string(view.current_layout_field_scheme) + " (using default)";
        labels.push_back(unavailable.c_str());
        ids.push_back(view.current_layout_field_scheme);
        selected = static_cast<int>(ids.size() - 1);
    }
    nk_layout_row_dynamic(context, 28, 2);
    nk_labelf(context, NK_TEXT_LEFT, "Layout: %s", view.current_field_layout_name
        ? view.current_field_layout_name : key);
    const auto value = wormedit::ui::chrome::safe_combo(context, labels.data(),
        static_cast<int>(labels.size()), selected, 24, nk_vec2(260, 280));
    if (value == selected || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME};
    snprintf(action.text, sizeof(action.text), "%s", ids[static_cast<std::size_t>(value)]);
}
void scratch_position_picker(nk_context* context, const WormUiOptionsView& view,
                             WormUiOptionsAction& action, bool second_player) {
    static constexpr const char* labels[]{"Skin default", "Chart layout", "Left", "Right"};
    const auto current = second_player ? view.p2_scratch_position : view.p1_scratch_position;
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, second_player ? "P2 scratch position" : "Single / P1 scratch position", NK_TEXT_LEFT);
    const auto selected = wormedit::ui::chrome::safe_combo(context, labels, 4,
        static_cast<int>(std::min(current, std::uint32_t{3})), 24, nk_vec2(220, 140));
    if (selected == static_cast<int>(current) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = second_player ? WORM_UI_OPTIONS_FIELD_P2_SCRATCH_POSITION : WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION,
        .integer = static_cast<std::uint64_t>(selected)};
}
void grid_opacity_slider(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    nk_layout_row_dynamic(context, 28, 2);
    nk_labelf(context, NK_TEXT_LEFT, "Grid opacity · %u%%", view.snap_grid_opacity_percent);
    if (!view.snap_colored_grid) nk_widget_disable_begin(context);
    int percent = static_cast<int>(view.snap_grid_opacity_percent);
    const bool changed = nk_slider_int(context, 0, &percent, 100, 1) != 0;
    if (!view.snap_colored_grid) nk_widget_disable_end(context);
    if (!changed || !view.snap_colored_grid || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_SNAP_GRID_OPACITY, .integer = static_cast<std::uint64_t>(percent)};
}
void field_waveform_opacity_slider(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action) {
    nk_layout_row_dynamic(context, 28, 2);
    nk_labelf(context, NK_TEXT_LEFT, "Field waveform opacity · %u%%", view.field_waveform_opacity_percent);
    int percent = static_cast<int>(view.field_waveform_opacity_percent);
    if (!nk_slider_int(context, 0, &percent, 100, 1) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY, .integer = static_cast<std::uint64_t>(percent)};
}
void appearance_page(nk_context* context, const WormUiOptionsView* view, WormUiOptionsAction* action) {
    field_scheme_picker(context, view, action);
    layout_field_scheme_picker(context, *view, *action);
    const auto& schemes = wormedit::ui::theme::field_schemes();
    const auto current = std::ranges::find(schemes, wormedit::ui::theme::field_scheme_id(),
        &wormedit::ui::theme::field_scheme_t::id);
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Preview: %s", current != schemes.end() ? current->name.c_str() : "Wormedit");
    scratch_position_picker(context, *view, *action, false);
    scratch_position_picker(context, *view, *action, true);
    edit_toggle(context, *view, *action, "Snap-colored grid",
        WORM_UI_OPTIONS_FIELD_SNAP_COLORED_GRID, view->snap_colored_grid);
    grid_opacity_slider(context, *view, *action);
    field_waveform_opacity_slider(context, *view, *action);
    nk_layout_row_dynamic(context, 38, 1);
    nk_label_wrap(context, "Layout skins inherit the default. Skins and opacity preview immediately; Apply saves them.");
}
std::array<WormUiOptionsPageRenderer, WORM_UI_OPTIONS_CATEGORY_COUNT> pages{
    general_page, editor_page, audio_page, appearance_page, wormedit::ui::nuklear::render_shortcuts_page, placeholder};
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
void render_page(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action,
                 wormedit::ui::lane_keys_action_t* lane_keys) {
    if (!nk_group_begin(context, "Options page", 0)) return;
    nk_layout_row_dynamic(context, 28, 1);
    nk_label(context, category_names[view.category], NK_TEXT_LEFT);
    if (view.category == WORM_UI_OPTIONS_INPUT && lane_keys) {
        nk_layout_row_dynamic(context, 30, 1);
        if (nk_button_label(context, "Lane keys…"))
            lane_keys->kind = wormedit::ui::lane_keys_action_kind_t::open;
    }
    pages[view.category](context, &view, &action);
    nk_layout_row_dynamic(context, 32, 1);
    for (size_t i = 0; i < view.diagnostic_count; ++i)
        nk_label_wrap(context, view.diagnostics[i].message);
    nk_group_end(context);
}
void render_footer(wormedit::ui::chrome::Dialog& dialog, nk_context* context,
                   const WormUiOptionsView& view, WormUiOptionsAction& action) {
    namespace chrome = wormedit::ui::chrome;
    if (view.status[0]) {
        nk_layout_row_dynamic(context, 34, 1);
        nk_label_wrap(context, view.status);
    }
    chrome::dialog_footer_begin(dialog, context, 4, false, 128);
    if (chrome::dialog_button(dialog, context, "Revert", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_REVERT);
    if (chrome::dialog_button(dialog, context, "Restore defaults", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_DEFAULTS);
    if (chrome::dialog_button(dialog, context, "Close", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_CLOSE);
    if (chrome::dialog_button(dialog, context, "Apply", chrome::DialogButtonRole::primary,
                              view.can_apply && view.changed))
        emit(action, view, WORM_UI_OPTIONS_APPLY);
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
    if (view->current_field_layout_lane_count > 4096 ||
        (view->current_field_layout_lane_count && !view->current_field_layout_lanes)) return false;
    for (size_t i = 0; i < view->diagnostic_count; ++i)
        if (!view->diagnostics[i].message) return false;
    if (view->shortcut_row_count > WORM_UI_SHORTCUT_ROW_CAPACITY || (view->shortcut_row_count && !view->shortcut_rows)) return false;
    for (size_t i = 0; i < view->shortcut_row_count; ++i)
        if (!view->shortcut_rows[i].label || !view->shortcut_rows[i].chord) return false;
    return true;
}
void worm_ui_nuklear_render_options_window(nk_context* context, const WormUiOptionsView* view,
    WormUiOptionsAction* action, wormedit::ui::lane_keys_action_t* lane_keys) {
    if (!context || !action || !worm_ui_nuklear_options_view_valid(view) || !view->visible) {
        worm_ui_nuklear_cancel_options_drag();
        return;
    }
    namespace chrome = wormedit::ui::chrome;
    auto& dialog = options_dialog;
    if (chrome::dialog_begin(dialog, context, "Options", "Options",
        {.modal = true, .closable = true, .width = 800, .escape_cancels = true,
         .enter_confirms = false, .resizable = true, .height = 500, .minimum_size = {640, 400}, .movable = true})) {
        const auto content = nk_window_get_content_region(context);
        const auto footer_height = chrome::dialog_button_height + chrome::dialog_spacing +
            (view->status[0] ? 34.0f + chrome::dialog_spacing : 0.0f);
        const auto bottom = std::min(content.y + content.h,
            dialog.bounds.y + dialog.bounds.h - chrome::dialog_padding);
        const auto body_height = std::max(1.0f, bottom -
            nk_widget_bounds(context).y - footer_height);
        constexpr float ratios[]{0.28f, 0.72f};
        nk_layout_row(context, NK_DYNAMIC, body_height, 2, ratios);
        render_categories(context, *view, *action);
        render_page(context, *view, *action, lane_keys);
        render_footer(dialog, context, *view, *action);
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel)
        emit(*action, *view, WORM_UI_OPTIONS_CLOSE);
    if (action->kind == WORM_UI_OPTIONS_CLOSE) worm_ui_nuklear_cancel_options_drag();
}

void worm_ui_nuklear_cancel_options_drag() {
    options_dialog.dragging = false;
}
