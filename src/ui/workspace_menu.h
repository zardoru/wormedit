#pragma once

#include <vector>

namespace wormedit::ui {

enum class workspace_menu_action_t {
    none, new_project, open, save, save_as, close, options, quit,
    copy, cut, paste, reset_view, frame_timing, tap_timing, timing_settings,
    notifications, preview, external_preferences, external_start, external_here, external_stop
};

struct workspace_menu_item_t {
    const char* menu;
    const char* title;
    workspace_menu_action_t action;
    const char* command_id{};
    bool enabled{true};
    bool checked{};
};

using workspace_menu_t = std::vector<workspace_menu_item_t>;

} // namespace wormedit::ui
