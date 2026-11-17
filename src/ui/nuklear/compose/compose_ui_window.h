#pragma once
#include "ui/frontend/actions.h"

namespace wormedit::ui {
/* What the Compose UI may write (or read) of the frame: its own request and stamp, the sampled
   input, and the few requests it raises in other domains. Names match workspace_action_t so
   Compose code reads the same; any other domain's request is simply not reachable from here.
   - timing: the staged timing-event editor opened from the Compose lanes and inspector.
   - audio_offset_drag: the waveform offset handle drag.
   - kind: shell requests Compose raises (marker navigation, BGA usage jump) and checks as "busy".
   - sound_import: sound import review. menu_command_id: Compose popup commands.
   - preview_mix, preview_audio_export, timing_settings: Mixer dock, preview export, timing lane toggle. */
struct compose_ui_window_t {
    compose_request_t& compose;
    request_stamp_t& stamp;
    frame_input_t& input;
    timing_edit_request_t& timing;
    audio_offset_drag_request_t& audio_offset_drag;
    sound_import_request_t& sound_import;
    WormUiWorkspaceActionKind& kind;
    std::string& menu_command_id;
    bga_usage_activation_t& bga_usage;
    std::optional<preview_mix_t>& preview_mix;
    std::optional<preview_audio_export_action_t>& preview_audio_export;
    timing_settings_action_t& timing_settings;
};

[[nodiscard]] inline auto compose_ui_window_of(workspace_action_t& action) -> compose_ui_window_t {
    return {action.compose, action.stamp, action.input, action.timing, action.audio_offset_drag,
            action.sound_import, action.kind, action.menu_command_id, action.bga_usage,
            action.preview_mix, action.preview_audio_export, action.timing_settings};
}
} // namespace wormedit::ui
