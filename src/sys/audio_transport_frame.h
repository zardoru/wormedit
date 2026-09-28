#pragma once

#include "sys/chart_preview_state.h"

#include <wormedit/core/workspace.h>

#include <optional>
#include <string>

namespace wormedit::sys {

// Services both audio transports and projects the chart-preview playhead for
// one application frame. Call after autosave and before waveform job handling.
void service_audio_transport_frame(
    const song_document_t& document, const workspace_controller_t& workspace,
    runtime::chart_preview_t& preview_state, audio::preview_session_t& preview_session,
    sdl_audition_output_t& audition_output, sdl_preview_output_t& preview_output,
    std::optional<double>& preview_playhead_absolute_row, std::string& status);

} // namespace wormedit::sys
