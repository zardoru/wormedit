#pragma once
#include "ui/frontend/actions.h"
#include "ui/nuklear/compose/compose_ui_window.h"

namespace wormedit::ui {
[[nodiscard]] inline auto compose_ui_window_of(workspace_action_t& action) -> compose_ui_window_t {
    return {action.compose, action.stamp, action.input, action.timing, action.audio_offset_drag,
            action.sound_import, action.kind, action.menu_command_id, action.bga_usage,
            action.preview_mix, action.preview_audio_export, action.timing_settings};
}
} // namespace wormedit::ui
