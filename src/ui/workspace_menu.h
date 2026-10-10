#pragma once

#include <vector>
#include <array>

namespace wormedit::ui {

enum class workspace_menu_action_t {
    none, new_project, open, save, save_as, close, options, quit,
    copy, cut, paste, select_same_sound, move_to_bgm, reset_view, frame_timing, tap_timing, timing_settings,
    notifications, preview, external_preferences, external_start, external_here, external_stop,
    undo, redo, delete_selection, select_all, invert_selection, clear_selection,
    new_chart, timing_lock, insert_bpm, insert_sv, play_stop, play_pause, play_start, stop, metronome,
    sounds, layers, about
};

struct workspace_menu_item_t {
    const char* menu;
    const char* title;
    workspace_menu_action_t action;
    const char* command_id{};
    bool enabled{true};
    bool checked{};
    const char* shortcut{};
};

inline constexpr std::array workspace_menu_title_widths{56.0f, 56.0f, 56.0f, 60.0f, 90.0f, 56.0f};
inline constexpr float workspace_menu_icon_width = 24.0f;
// The title-row window padding and inter-cell gaps are 4 logical pixels. Keep
// the native drag exclusion in sync with every interactive menu cell.
inline constexpr int workspace_menu_drag_begin = [] {
    int width = 8 + static_cast<int>(workspace_menu_icon_width) +
        4 * (static_cast<int>(workspace_menu_title_widths.size()) + 1);
    for (const auto cell : workspace_menu_title_widths) width += static_cast<int>(cell);
    return width;
}();

inline constexpr std::array workspace_menu_titles{"File", "Edit", "View", "Chart", "Playback", "Help"};

// The bounded projection includes only commands exposed by the shared menu model.
inline constexpr std::array workspace_menu_command_ids{
    "document.new_project", "document.save_project", "workspace.options",
    "history.undo", "history.redo", "edit.copy", "edit.cut", "edit.paste", "edit.delete",
    "edit.move_to_bgm", "selection.select_all", "selection.invert", "selection.clear",
    "selection.select_same_sound_chart", "view.notifications", "view.toggle_sounds", "view.open_layers",
    "timing.tap_assistant", "document.new_chart", "timing.toggle_lock", "timing.insert_bpm",
    "timing.insert_sv", "transport.play_stop", "transport.play_pause", "transport.play_from_start",
    "transport.stop", "transport.toggle_metronome", "transport.external_play_start",
    "transport.external_play_here", "transport.external_stop"};

using workspace_menu_t = std::vector<workspace_menu_item_t>;

} // namespace wormedit::ui
