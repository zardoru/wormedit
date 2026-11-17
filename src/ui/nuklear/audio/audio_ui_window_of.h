#pragma once
#include "ui/frontend/actions.h"
#include "ui/nuklear/audio/audio_ui_window.h"

namespace wormedit::ui {
[[nodiscard]] inline auto audio_ui_window_of(workspace_action_t& action) -> audio_ui_window_t {
    return {action.asset_selection, action.audio_offset_drag, action.audio_slicer, action.compose,
            action.kind, action.sound_properties, action.sound_usage, action.sound_import,
            action.stamp, action.metadata, action.timing_link};
}
} // namespace wormedit::ui
