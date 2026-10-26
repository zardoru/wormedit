#include "ui/frontend/workspace.h"
#include "ui/nuklear/app/menu_bar.h"
#include "ui/nuklear/chrome/chrome.h"
#include <algorithm>
#include <cstring>

namespace wormedit::ui {
namespace {
using Action = workspace_menu_action_t;

void append_file(workspace_menu_t& menu, const wormedit::ui::workspace_view_t& view, bool blocked) {
    menu.push_back({"File", "New project", Action::new_project, "document.new_project", !blocked});
    menu.push_back({"File", "Open chart or project…", Action::open, "document.open", !blocked});
    menu.push_back({"File", "Save project", Action::save, "document.save_project", !blocked && view.project_loaded});
    menu.push_back({"File", "Save project as…", Action::save_as, "document.save_as", !blocked && view.project_loaded});
    menu.push_back({"File", "Close project", Action::close, "document.close", !blocked && view.project_loaded});
    menu.push_back({"File", "Export…", Action::export_output_group, "document.export_output_group",
                    !blocked && view.project_loaded && view.selected_chart_id.value != 0});
    menu.push_back({"File", "Options…", Action::options, "workspace.options", !blocked});
    menu.push_back({"File", "Quit", Action::quit, "app.quit", !blocked});
    if (view.recent_files.size()) menu.push_back({"File", "", Action::none, nullptr, false});
    for (std::size_t index = 0; index < view.recent_files.size(); ++index) {
        const auto& entry = view.recent_files[index];
        menu.push_back({"File", entry.label.data(), static_cast<Action>(static_cast<int>(Action::recent_first) + index),
                        nullptr, !blocked, false, nullptr, entry.path.data()});
    }
}

void append_edit(workspace_menu_t& menu, bool enabled) {
    menu.push_back({"Edit", "Undo", Action::undo, "history.undo", enabled});
    menu.push_back({"Edit", "Redo", Action::redo, "history.redo", enabled});
    menu.push_back({"Edit", "Copy", Action::copy, "edit.copy", enabled});
    menu.push_back({"Edit", "Cut", Action::cut, "edit.cut", enabled});
    menu.push_back({"Edit", "Paste", Action::paste, "edit.paste", enabled});
    menu.push_back({"Edit", "Delete", Action::delete_selection, "edit.delete", enabled});
    menu.push_back({"Edit", "Select all", Action::select_all, "selection.select_all", enabled});
    menu.push_back({"Edit", "Select all objects", Action::select_all_objects, "selection.select_all_objects", enabled});
    menu.push_back({"Edit", "Select all after cursor", Action::select_all_after_cursor,
                    "selection.select_all_after_cursor", enabled});
    menu.push_back({"Edit", "Select all objects after cursor", Action::select_all_objects_after_cursor,
                    "selection.select_all_objects_after_cursor", enabled});
    menu.push_back({"Edit", "Invert selection", Action::invert_selection, "selection.invert", enabled});
    menu.push_back({"Edit", "Clear selection", Action::clear_selection, "selection.clear", enabled});
    menu.push_back({"Edit", "Select same sound (whole chart)", Action::select_same_sound,
                    "selection.select_same_sound_chart", enabled});
    menu.push_back({"Edit", "Move selection to BGM", Action::move_to_bgm, "edit.move_to_bgm", enabled});
}

void append_view(workspace_menu_t& menu, const wormedit::ui::workspace_view_t& view, bool enabled, bool timing) {
    menu.push_back({"View", "Player preview", Action::preview, nullptr, enabled && view.project_loaded, view.player_preview.visible});
    menu.push_back({"View", "Reset editor view", Action::reset_view, nullptr, enabled && view.compose.visible});
    menu.push_back({"View", "Sounds panel", Action::sounds, "view.toggle_sounds", enabled});
    menu.push_back({"View", "Layers panel", Action::layers, "view.open_layers", enabled});
    menu.push_back({"View", "Frame timing", Action::frame_timing, nullptr, enabled, timing});
    menu.push_back({"View", "Tap timing", Action::tap_timing, "timing.tap_assistant", enabled, view.tap_timing.visible});
    menu.push_back({"View", "Timing settings…", Action::timing_settings, nullptr, enabled, view.timing_settings_visible});
    menu.push_back({"View", "Notifications", Action::notifications, "view.notifications", enabled && view.notifications.history_available});
}

void append_chart(workspace_menu_t& menu, bool enabled) {
    menu.push_back({"Chart", "New chart", Action::new_chart, "document.new_chart", enabled});
    menu.push_back({"Chart", "Timing lock", Action::timing_lock, "timing.toggle_lock", enabled});
    menu.push_back({"Chart", "Insert BPM change", Action::insert_bpm, "timing.insert_bpm", enabled});
    menu.push_back({"Chart", "Insert scroll velocity", Action::insert_sv, "timing.insert_sv", enabled});
}

void append_playback(workspace_menu_t& menu, bool enabled) {
    menu.push_back({"Playback", "Play / stop", Action::play_stop, "transport.play_stop", enabled});
    menu.push_back({"Playback", "Play from cursor / pause", Action::play_pause, "transport.play_pause", enabled});
    menu.push_back({"Playback", "Play from start", Action::play_start, "transport.play_from_start", enabled});
    menu.push_back({"Playback", "Stop", Action::stop, "transport.stop", enabled});
    menu.push_back({"Playback", "Metronome", Action::metronome, "transport.toggle_metronome", enabled});
    menu.push_back({"Playback", "Play hitsounds on playback", Action::playback_audition,
                    "audio.toggle_playback_audition", enabled});
    menu.push_back({"Playback", "", Action::none, nullptr, false});
    menu.push_back({"Playback", "External player preferences…", Action::external_preferences, nullptr, enabled});
    menu.push_back({"Playback", "External play from start", Action::external_start, "transport.external_play_start", enabled});
    menu.push_back({"Playback", "External play from here", Action::external_here, "transport.external_play_here", enabled});
    menu.push_back({"Playback", "External stop", Action::external_stop, "transport.external_stop", enabled});
}

void apply_command_rows(workspace_menu_t& menu, const wormedit::ui::workspace_view_t& view) {
    for (auto& item : menu) {
        if (!item.command_id) continue;
        const auto end = view.menu_commands.end();
        const auto row = std::find_if(view.menu_commands.begin(), end, [&](const auto& command) {
            return command.id == item.command_id;
        });
        item.enabled = item.enabled && row != end && row->enabled;
        if (row == end) continue;
        item.checked = row->checked;
        item.shortcut = row->shortcut.data();
    }
}

auto begin_menu(nk_context* context, const char* title, float width, float height) -> bool {
    auto& parent = *context->current;
    auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto name = nk_murmur_hash(title, static_cast<int>(std::strlen(title)), NK_PANEL_MENU);
    const bool switching = parent.popup.win && parent.popup.type == NK_PANEL_MENU &&
        parent.popup.name != name && nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
        nk_input_is_mouse_hovering_rect(&context->input, nk_widget_bounds(context));
    if (!switching) return nk_menu_begin_label(context, title, NK_TEXT_LEFT, nk_vec2(width, height));
    parent.popup.name = name;
    const auto clicked = button.clicked;
    button.clicked = 0;
    const auto opened = nk_menu_begin_label(context, title, NK_TEXT_LEFT, nk_vec2(width, height));
    button.clicked = clicked;
    return opened;
}

auto menu_size(nk_context* context, const workspace_menu_t& model, const char* title) -> struct nk_vec2 {
    float width = 210.0f;
    float height = 2.0f * chrome::menu_padding;
    for (const auto& item : model) {
        if (std::strcmp(item.menu, title) != 0) continue;
        height += (item.action == Action::none ? metrics::menu_separator_height : metrics::menu_item_height) +
                  context->style.window.spacing.y;
        const auto* font = context->style.font;
        const auto text_width = [&](const char* text) {
            return text ? font->width(font->userdata, font->height, text, static_cast<int>(std::strlen(text))) : 0.0f;
        };
        width = std::max(width, text_width(item.title) + text_width(item.shortcut) + 64.0f);
    }
    const auto viewport = nuklear::tooltip::viewport();
    const auto available = viewport.w > 0 ? viewport : nk_window_get_bounds(context);
    return nk_vec2(std::min(width, available.w),
                   std::min(height + chrome::menu_padding,
                            std::max(80.0f, available.h - 64.0f)));
}
auto render_menu_entries(nk_context* context, const workspace_menu_t& model,
                         const char* title, bool scrollable, bool& release_pending,
                         const char*& tooltip) -> Action {
    auto chosen = Action::none;
    for (const auto& item : model) {
        if (std::strcmp(item.menu, title) != 0) continue;
        if (item.action == Action::none) {
            chrome::menu_separator(context);
            continue;
        }
        nk_layout_row_dynamic(context, metrics::menu_item_height, 1);
        const auto row_bounds = nk_widget_bounds(context);
        if (chrome::menu_item(context, item.title, item.shortcut, item.checked, item.enabled, !scrollable)) {
            chosen = item.action;
            release_pending = true;
        }
        if (item.recent_path && nuklear::tooltip::hovered(context, row_bounds)) tooltip = item.recent_path;
        if (item.enabled || !nuklear::tooltip::hovered(context, row_bounds)) continue;
        if (item.action == Action::move_to_bgm) tooltip = "Requires selected playable notes on an editable BMS chart";
        if (item.action == Action::select_same_sound) tooltip = "Hover a note or select a sound first";
    }
    return chosen;
}
} // namespace

auto build_workspace_menu(const wormedit::ui::workspace_view_t& view, bool blocked, bool text_owner,
                          bool frame_timing) -> workspace_menu_t {
    workspace_menu_t menu;
    menu.reserve(40);
    append_file(menu, view, blocked);
    append_edit(menu, !blocked && !text_owner);
    append_view(menu, view, !blocked, frame_timing);
    append_chart(menu, !blocked && !text_owner);
    append_playback(menu, !blocked && !text_owner);
    menu.push_back({"Help", "User manual", Action::manual, nullptr, !blocked});
    menu.push_back({"Help", "Shortcut cheat sheet", Action::shortcuts, nullptr, !blocked});
    menu.push_back({"Help", "About WormEdit", Action::about, nullptr, !blocked});
    apply_command_rows(menu, view);
    if (text_owner) for (auto& entry : menu) if (entry.command_id) entry.enabled = false;
    return menu;
}

auto render_workspace_menu(nk_context* context, const workspace_menu_t& model,
                           const char* title, bool focused, bool& popup_open,
                           bool& release_pending) -> workspace_menu_action_t {
    const auto size = menu_size(context, model, title);
    if (!begin_menu(context, title, size.x, size.y)) return Action::none;
    const auto bounds = nk_window_get_bounds(context);
    const bool over_titles = context->input.mouse.pos.y < bounds.y;
    if (!focused || chrome::popup_dismiss_requested(context, bounds, !over_titles)) {
        nk_popup_close(context);
        nk_menu_end(context);
        release_pending = true;
        return Action::none;
    }
    popup_open = true;
    const auto saved = chrome::menu_surface_begin(context);
    const bool scrollable = std::strcmp(title, "File") == 0 &&
        std::ranges::any_of(model, [](const auto& item) { return item.recent_path != nullptr; });
    bool group_open = false;
    if (scrollable) {
        nk_layout_row_dynamic(context, std::max(40.0f, size.y - 12.0f), 1);
        group_open = nk_group_begin(context, "file-menu-entries", 0);
    }
    const char* tooltip = nullptr;
    const auto chosen = (!scrollable || group_open)
        ? render_menu_entries(context, model, title, scrollable, release_pending, tooltip) : Action::none;
    if (group_open) nk_group_end(context);
    if (scrollable && chosen != Action::none) nk_popup_close(context);
    nk_menu_end(context);
    chrome::menu_surface_end(context, saved);
    if (tooltip) nuklear::tooltip::text(context, tooltip);
    return chosen;
}
} // namespace wormedit::ui
