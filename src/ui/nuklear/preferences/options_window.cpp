#include "ui/nuklear/preferences/options_window.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/dialog.h"
#include "ui/nuklear/render/theme.h"
#include <cstdio>
#include <string_view>
#include "ui/nuklear/preferences/shortcuts_panel.h"
#include "ui/nuklear/preferences/external_player_panel.h"
#include "wormedit/nuklear_config.h"
#include <array>
#include <algorithm>
#include <wormedit/audio/playback_rate.h>

namespace {
wormedit::ui::chrome::Dialog options_dialog;
constexpr std::array<const char*, WORM_UI_OPTIONS_CATEGORY_COUNT> category_names{
    "General", "Editor", "Playback & audio", "Appearance", "Input & shortcuts", "External players"};
void edit_integer(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action,
    const char* label, WormUiOptionsField field, uint32_t value, int minimum, int maximum) {
    nk_layout_row_dynamic(context, 28, 1);
    int draft = static_cast<int>(value);
    nk_property_int(context, label, minimum, &draft, maximum, 1, 1);
    if (draft == static_cast<int>(value) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = field, .integer = static_cast<uint64_t>(draft)};
}
void edit_toggle(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action,
    const char* label, WormUiOptionsField field, bool value) {
    nk_layout_row_dynamic(context, 28, 1);
    nk_bool draft = value;
    if (!nk_checkbox_label(context, label, &draft) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = field, .boolean = draft != 0};
}
void general_page(nk_context* context, const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
    edit_integer(context, *view, *action, "Autosave interval (minutes)", WORM_UI_OPTIONS_FIELD_AUTOSAVE,
        view->autosave_minutes, 1, 30);
    edit_integer(context, *view, *action, "Recent-files limit", WORM_UI_OPTIONS_FIELD_RECENT_FILES,
        view->recent_files_count, 1, 100);
    edit_toggle(context, *view, *action, "Confirm closing unsaved work", WORM_UI_OPTIONS_FIELD_CONFIRM_CLOSE,
        view->confirm_unsaved_close);
}
void scroll_directions(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action) {
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
void move_collision_policy(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action) {
    constexpr const char* labels[]{"Reject", "Move it to a BGM lane"};
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "When a move lands on a note", NK_TEXT_LEFT);
    const auto value = static_cast<int>(view.move_collision_policy);
    const auto selected = wormedit::ui::chrome::safe_combo(context, labels, 2, value, 24, nk_vec2(240, 100));
    if (selected == value || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY, .integer = static_cast<uint64_t>(selected)};
}
void editor_page(nk_context* context, const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
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
void audio_output_controls(nk_context* context, const wormedit::ui::options_view_t& view,
                           wormedit::ui::options_action_t& action) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Output device", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 2);
    const auto label = view.output_device.empty() ? std::string{"System default"} : std::string{view.output_device};
    if (wormedit::ui::chrome::safe_combo_begin_label(context, label.c_str(), nk_vec2(360, 220))) {
        nk_layout_row_dynamic(context, 28, 1);
        if (nk_combo_item_label(context, "System default", NK_TEXT_LEFT)) {
            action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision,
                .category = view.category, .field = WORM_UI_OPTIONS_FIELD_OUTPUT_DEVICE};
        }
        for (const auto& name : view.audio_devices)
            if (nk_combo_item_label(context, name.c_str(), NK_TEXT_LEFT)) {
                action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision,
                    .category = view.category, .field = WORM_UI_OPTIONS_FIELD_OUTPUT_DEVICE, .text = name};
            }
        wormedit::ui::chrome::safe_combo_end(context);
    }
    if (nk_button_label(context, "Refresh devices") && action.kind == WORM_UI_OPTIONS_NONE)
        action = {.kind = WORM_UI_OPTIONS_REFRESH_AUDIO_DEVICES, .revision = view.revision, .category = view.category};
    edit_integer(context, view, action, "Buffer (frames; 0 = automatic)", WORM_UI_OPTIONS_FIELD_BUFFER_FRAMES,
        view.buffer_frames, 0, 8192);
    if (view.audio_output_pending) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Output changes apply after Stop.", NK_TEXT_LEFT);
    }
}
void audio_level_controls(nk_context* context, const wormedit::ui::options_view_t& view,
                          wormedit::ui::options_action_t& action) {
    edit_integer(context, view, action, "Metronome volume (%)", WORM_UI_OPTIONS_FIELD_METRONOME_VOLUME,
        view.metronome_volume, 0, 100);
    edit_integer(context, view, action, "Accent volume (%)", WORM_UI_OPTIONS_FIELD_ACCENT_VOLUME,
        view.accent_volume, 0, 100);
    edit_integer(context, view, action, "Count-in (measures)", WORM_UI_OPTIONS_FIELD_COUNT_IN,
        view.count_in_measures, 0, 4);
    edit_integer(context, view, action, "Audition volume (%)", WORM_UI_OPTIONS_FIELD_AUDITION_VOLUME,
        view.audition_volume, 0, 100);
    nk_layout_row_dynamic(context, 28, 1);
    int latency = view.preview_latency_ms;
    nk_property_int(context, "Preview latency (ms; positive = ahead)", -1000, &latency, 1000, 1, 1);
    if (latency != view.preview_latency_ms && action.kind == WORM_UI_OPTIONS_NONE)
        action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
            .field = WORM_UI_OPTIONS_FIELD_PREVIEW_LATENCY, .signed_integer = latency};
}
void audio_rate_controls(nk_context* context, const wormedit::ui::options_view_t& view,
                         wormedit::ui::options_action_t& action) {
    using namespace wormedit::audio;
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "Default playback rate", NK_TEXT_LEFT);
    std::array<const char*, playback_rate_count> labels{};
    for (std::size_t index = 0; index < labels.size(); ++index) labels[index] = playback_rate_catalog[index].label.data();
    const int current = static_cast<int>(playback_rate_index({view.rate_numerator, view.rate_denominator}).value_or(8));
    const int selected = wormedit::ui::chrome::safe_combo(context, labels.data(), static_cast<int>(labels.size()), current, 28, nk_vec2(180, 240));
    if (selected != current && action.kind == WORM_UI_OPTIONS_NONE) {
        const auto rate = playback_rate_catalog[static_cast<std::size_t>(selected)].rate;
        action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
            .field = WORM_UI_OPTIONS_FIELD_RATE, .integer = rate.numerator, .denominator = rate.denominator};
    }
    edit_toggle(context, view, action, "Keep pitch by default", WORM_UI_OPTIONS_FIELD_KEEP_PITCH, view.keep_pitch);
}
void audio_page(nk_context* context, const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
    audio_output_controls(context, *view, *action);
    audio_level_controls(context, *view, *action);
    audio_rate_controls(context, *view, *action);
    edit_integer(context, *view, *action, "Keysound memory (MiB)", WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY,
        view->keysound_budget_mib, 256, 8192);
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Raising the budget loads skipped keysounds for the open project. Lowering it limits future loads and keeps playing sounds in memory.");
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "osu! hitsound skin folder (empty uses bundled samples)", NK_TEXT_LEFT);
    std::array<char, wormedit::maximum_preference_string_bytes + 1> folder{};
    std::snprintf(folder.data(), folder.size(), "%s", view->osu_skin_directory.empty() ? "" : view->osu_skin_directory.data());
    nk_layout_row_dynamic(context, 28, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context,
        NK_EDIT_FIELD, folder.data(), folder.size(), nk_filter_default);
    if (view->osu_skin_directory != folder.data() && action->kind == WORM_UI_OPTIONS_NONE) {
        *action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view->revision,
            .category = view->category, .field = WORM_UI_OPTIONS_FIELD_OSU_SKIN_DIRECTORY};
        action->text = folder.data();
    }
}
void field_scheme_picker(nk_context* context, const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
    const auto& schemes = wormedit::ui::theme::field_schemes();
    std::vector<const char*> labels;
    int selected = 0;
    for (std::size_t index = 0; index < schemes.size(); ++index) {
        labels.push_back(schemes[index].name.c_str());
        if (schemes[index].id == view->field_scheme) selected = static_cast<int>(index);
    }
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "Default field skin", NK_TEXT_LEFT);
    if (labels.empty()) { nk_label(context, "Wormedit", NK_TEXT_LEFT); return; }
    const auto value = wormedit::ui::chrome::safe_combo(context, labels.data(),
        static_cast<int>(labels.size()), selected, 24, nk_vec2(240, 280));
    if (value == selected || action->kind != WORM_UI_OPTIONS_NONE) return;
    *action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view->revision, .category = view->category,
        .field = WORM_UI_OPTIONS_FIELD_FIELD_SCHEME};
    action->text = schemes[static_cast<std::size_t>(value)].id;
}
void layout_field_scheme_picker(nk_context* context, const wormedit::ui::options_view_t& view,
                               wormedit::ui::options_action_t& action) {
    const auto key = view.current_field_layout_key;
    if (key.empty()) {
        nk_layout_row_dynamic(context, 28, 1);
        nk_label(context, "Open a chart to choose its layout's field skin.", NK_TEXT_LEFT);
        return;
    }
    std::vector<wormedit::ui::theme::field_lane_t> lanes;
    for (const auto& lane : std::span{view.current_field_layout_lanes})
        lanes.push_back({lane.display_order, static_cast<std::uint8_t>(lane.stage),
                static_cast<std::uint8_t>(lane.key_color), static_cast<std::uint8_t>(lane.role), lane.lane_id.value});
    std::vector<const char*> labels{"Use default"};
    std::vector<const char*> ids{""};
    int selected = 0;
    for (const auto& scheme : wormedit::ui::theme::field_schemes()) {
        if (!wormedit::ui::theme::field_scheme_supports_layout(scheme.id, lanes)) continue;
        ids.push_back(scheme.id.c_str());
        labels.push_back(scheme.name.c_str());
        if (!view.current_layout_field_scheme.empty() && scheme.id == view.current_layout_field_scheme)
            selected = static_cast<int>(ids.size() - 1);
    }
    std::string unavailable;
    if (selected == 0 && !view.current_layout_field_scheme.empty() && !view.current_layout_field_scheme.empty()) {
        unavailable = std::string(view.current_layout_field_scheme) + " (using default)";
        labels.push_back(unavailable.c_str());
        ids.push_back(view.current_layout_field_scheme.data());
        selected = static_cast<int>(ids.size() - 1);
    }
    nk_layout_row_dynamic(context, 28, 2);
    nk_labelf(context, NK_TEXT_LEFT, "Layout: %s", !view.current_field_layout_name.empty()
        ? view.current_field_layout_name.data() : key.data());
    const auto value = wormedit::ui::chrome::safe_combo(context, labels.data(),
        static_cast<int>(labels.size()), selected, 24, nk_vec2(260, 280));
    if (value == selected || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME};
    action.text = ids[static_cast<std::size_t>(value)];
}
void scratch_position_picker(nk_context* context, const wormedit::ui::options_view_t& view,
                             wormedit::ui::options_action_t& action, bool second_player) {
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
void grid_opacity_slider(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action) {
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
void field_waveform_opacity_slider(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action) {
    nk_layout_row_dynamic(context, 28, 2);
    nk_labelf(context, NK_TEXT_LEFT, "Field waveform opacity · %u%%", view.field_waveform_opacity_percent);
    int percent = static_cast<int>(view.field_waveform_opacity_percent);
    if (!nk_slider_int(context, 0, &percent, 100, 1) || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY, .integer = static_cast<std::uint64_t>(percent)};
}
void interface_theme_picker(nk_context* context, const wormedit::ui::options_view_t& view,
                            wormedit::ui::options_action_t& action) {
    std::vector<const char*> names{"Bundled default"}, ids{""};
    int selected = 0;
    for (const auto& theme : wormedit::ui::theme::interface_themes()) {
        ids.push_back(theme.id.c_str());
        names.push_back(theme.name.c_str());
        if (view.theme == theme.id) selected = static_cast<int>(ids.size() - 1);
    }
    std::string unavailable;
    if (!view.theme.empty() && selected == 0) {
        unavailable = "Unavailable: " + std::string{view.theme};
        names.push_back(unavailable.c_str());
        ids.push_back(view.theme.data());
        selected = static_cast<int>(ids.size() - 1);
    }
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "Interface theme", NK_TEXT_LEFT);
    const auto value = wormedit::ui::chrome::safe_combo(context, names.data(), static_cast<int>(names.size()),
        selected, 28, nk_vec2(260, 200));
    if (value == selected || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_THEME, .text = ids[static_cast<std::size_t>(value)]};
}
void interface_scale_picker(nk_context* context, const wormedit::ui::options_view_t& view,
                            wormedit::ui::options_action_t& action) {
    std::array<std::uint32_t, 7> values{0, 100, 125, 150, 175, 200, view.ui_scale_percent};
    const auto custom = std::to_string(view.ui_scale_percent) + "%";
    const std::array<const char*, 7> names{"Automatic", "100%", "125%", "150%", "175%", "200%", custom.c_str()};
    const auto found = std::ranges::find(values, view.ui_scale_percent);
    const auto selected = static_cast<int>(found - values.begin());
    const auto count = selected == 6 ? 7 : 6;
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, "UI scale", NK_TEXT_LEFT);
    const auto value = wormedit::ui::chrome::safe_combo(context, names.data(), count,
        selected, 28, nk_vec2(180, 220));
    if (const auto in_use = worm_ui_frontend_interface_scale_percent();
        view.ui_scale_percent > in_use) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Using %u%% to fit this window", static_cast<unsigned>(in_use));
    }
    if (value == selected || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = view.category,
        .field = WORM_UI_OPTIONS_FIELD_SCALE, .integer = values[static_cast<std::size_t>(value)]};
}
void appearance_page(nk_context* context, const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
    interface_theme_picker(context, *view, *action);
    interface_scale_picker(context, *view, *action);
    edit_toggle(context, *view, *action, "Frame timing overlay", WORM_UI_OPTIONS_FIELD_TIMING_OVERLAY, view->frame_timing_overlay);
    edit_toggle(context, *view, *action, "Reduced motion", WORM_UI_OPTIONS_FIELD_REDUCED_MOTION, view->reduced_motion);
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
void default_lane_key(nk_context* context, const wormedit::ui::options_view_t& view,
                      wormedit::ui::options_action_t& action, std::size_t index) {
    std::vector<const char*> labels;
    int selected = 0;
    for (const auto& choice : view.lane_key_choices) {
        if (choice.usage == view.default_lane_bindings[index]) selected = static_cast<int>(labels.size());
        labels.push_back(choice.label.c_str());
    }
    if (labels.empty()) return;
    nk_layout_row_dynamic(context, 28, 2);
    nk_labelf(context, NK_TEXT_LEFT, "Lane %zu", index + 1);
    const auto chosen = wormedit::ui::chrome::safe_combo(context, labels.data(), static_cast<int>(labels.size()),
        selected, 28, nk_vec2(180, 220));
    if (chosen == selected || action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = WORM_UI_OPTIONS_INPUT,
        .field = WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_KEY, .integer = view.lane_key_choices[static_cast<std::size_t>(chosen)].usage,
        .lane_count = view.default_lane_count, .lane_index = static_cast<std::uint32_t>(index)};
}
void default_lane_bindings(nk_context* context, const wormedit::ui::options_view_t& view,
                           wormedit::ui::options_action_t& action) {
    if (!nk_tree_push(context, NK_TREE_TAB, "Default lane keys", NK_MINIMIZED)) return;
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Choose bindings for new and imported charts. Unconfigured counts keep their existing defaults; saved project bindings are preserved.");
    auto count = static_cast<int>(view.default_lane_count);
    nk_layout_row_dynamic(context, 28, 1);
    nk_property_int(context, "Lane count", 1, &count, wormedit::maximum_default_lane_count, 1, 1);
    if (count != static_cast<int>(view.default_lane_count) && action.kind == WORM_UI_OPTIONS_NONE)
        action = {.kind = WORM_UI_OPTIONS_SELECT_LANE_COUNT, .revision = view.revision,
            .category = WORM_UI_OPTIONS_INPUT, .lane_count = static_cast<std::uint32_t>(count)};
    nk_layout_row_dynamic(context, 28, view.default_lane_count == 4 ? 2 : 1);
    if (nk_button_label(context, "Number row") && action.kind == WORM_UI_OPTIONS_NONE)
        action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = WORM_UI_OPTIONS_INPUT,
            .field = WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_NUMBER_ROW, .lane_count = view.default_lane_count};
    if (view.default_lane_count == 4 && nk_button_label(context, "DFJK (4K)") && action.kind == WORM_UI_OPTIONS_NONE)
        action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision, .category = WORM_UI_OPTIONS_INPUT,
            .field = WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_DFJK, .lane_count = 4};
    for (std::size_t index = 0; index < view.default_lane_bindings.size(); ++index)
        default_lane_key(context, view, action, index);
    nk_tree_pop(context);
}
void input_page(nk_context* context, const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
    default_lane_bindings(context, *view, *action);
    wormedit::ui::nuklear::render_shortcuts_page(context, view, action);
}
std::array<WormUiOptionsPageRenderer, WORM_UI_OPTIONS_CATEGORY_COUNT> pages{
    general_page, editor_page, audio_page, appearance_page, input_page, worm_ui_nuklear_render_external_players_page};
void emit(wormedit::ui::options_action_t& action, const wormedit::ui::options_view_t& view, WormUiOptionsActionKind kind) {
    if (action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = kind, .revision = view.revision, .category = view.category};
}
void render_categories(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action) {
    if (!nk_group_begin(context, "Options categories", 0)) return;
    nk_layout_row_dynamic(context, 32, 1);
    for (int category = 0; category < WORM_UI_OPTIONS_CATEGORY_COUNT; ++category) {
        nk_bool selected = view.category == category;
        std::array<char, 64> label{};
        std::snprintf(label.data(), label.size(), "%s%s", category_names[category],
                      view.changed_categories[category] ? " *" : "");
        const bool changed = nk_selectable_label(context, label.data(), NK_TEXT_LEFT, &selected) != 0;
        if (!changed || !selected) continue;
        emit(action, view, WORM_UI_OPTIONS_SELECT_CATEGORY);
        action.category = static_cast<WormUiOptionsCategory>(category);
    }
    nk_group_end(context);
}
void render_page(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action,
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
    for (size_t i = 0; i < view.diagnostics.size(); ++i)
        nk_label_wrap(context, view.diagnostics[i].message.data());
    nk_group_end(context);
}
void render_footer(wormedit::ui::chrome::Dialog& dialog, nk_context* context,
                   const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action) {
    namespace chrome = wormedit::ui::chrome;
    if (!view.status.empty()) {
        nk_layout_row_dynamic(context, 34, 1);
        nk_label_wrap(context, view.status.data());
    }
    chrome::dialog_footer_begin(dialog, context, 4, false, 128);
    if (chrome::dialog_button(dialog, context, "Revert all", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_REVERT);
    if (chrome::dialog_button(dialog, context, "Restore defaults", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_DEFAULTS);
    if (chrome::dialog_button(dialog, context, "Close", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_CLOSE);
    if (chrome::dialog_button(dialog, context, "Apply all", chrome::DialogButtonRole::primary,
                              view.can_apply && view.changed))
        emit(action, view, WORM_UI_OPTIONS_APPLY);
}
void render_discard_confirmation(wormedit::ui::chrome::Dialog& dialog, nk_context* context,
                                const wormedit::ui::options_view_t& view,
                                wormedit::ui::options_action_t& action) {
    namespace chrome = wormedit::ui::chrome;
    chrome::dialog_text(dialog, context, "Discard unapplied changes from all Options pages?",
                        chrome::color(chrome::UiColor::text));
    chrome::dialog_footer_begin(dialog, context, 2, true, 140);
    if (chrome::dialog_button(dialog, context, "Discard changes", chrome::DialogButtonRole::destructive))
        emit(action, view, WORM_UI_OPTIONS_DISCARD_AND_CLOSE);
    if (chrome::dialog_button(dialog, context, "Keep editing", chrome::DialogButtonRole::secondary))
        emit(action, view, WORM_UI_OPTIONS_KEEP_EDITING);
}
} // namespace
bool worm_ui_nuklear_register_options_page(WormUiOptionsCategory category, WormUiOptionsPageRenderer renderer) {
    if (category < WORM_UI_OPTIONS_GENERAL || category >= WORM_UI_OPTIONS_CATEGORY_COUNT || !renderer) return false;
    pages[category] = renderer;
    return true;
}
bool worm_ui_nuklear_options_view_valid(const wormedit::ui::options_view_t* view) {
    if (!view || !view->visible) return view != nullptr;
    constexpr std::size_t maximum_options_layout_lanes = 4096;
    return view->category >= WORM_UI_OPTIONS_GENERAL && view->category < WORM_UI_OPTIONS_CATEGORY_COUNT &&
           view->players.size() <= wormedit::maximum_external_players &&
           view->default_lane_count > 0 && view->default_lane_count <= wormedit::maximum_default_lane_count &&
           view->default_lane_bindings.size() <= wormedit::maximum_default_lane_count &&
           view->current_field_layout_lanes.size() <= maximum_options_layout_lanes;
}

void worm_ui_nuklear_render_options_window(nk_context* context, const wormedit::ui::options_view_t* view,
    wormedit::ui::options_action_t* action, wormedit::ui::lane_keys_action_t* lane_keys) {
    if (!context || !action || !worm_ui_nuklear_options_view_valid(view) || !view->visible) {
        worm_ui_nuklear_cancel_options_drag();
        wormedit::ui::nuklear::reset_shortcuts_filter();
        return;
    }
    namespace chrome = wormedit::ui::chrome;
    auto& dialog = options_dialog;
    if (chrome::dialog_begin(dialog, context, "Options", "Options",
        {.modal = true, .closable = true, .width = 800, .escape_cancels = true,
         .enter_confirms = false, .resizable = true, .height = 500, .minimum_size = {640, 400}, .movable = true})) {
        if (view->confirm_discard) {
            render_discard_confirmation(dialog, context, *view, *action);
        } else {
            const auto content = nk_window_get_content_region(context);
            const auto footer_height = chrome::dialog_button_height + chrome::dialog_spacing +
                (!view->status.empty() ? 34.0f + chrome::dialog_spacing : 0.0f);
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
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel)
        emit(*action, *view, view->confirm_discard ? WORM_UI_OPTIONS_KEEP_EDITING : WORM_UI_OPTIONS_CLOSE);
    if (action->kind == WORM_UI_OPTIONS_CLOSE) worm_ui_nuklear_cancel_options_drag();
}

void worm_ui_nuklear_cancel_options_drag() {
    options_dialog.dragging = false;
}
