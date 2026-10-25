#pragma once

#include "sys/audio/chart_preview_state.h"

#include <wormedit/core/workspace.h>
#include <wormedit/core/notifications.h>

#include <optional>
#include <string>

namespace wormedit::sys {

// Producer metadata is chosen from service enums; these helpers also preserve
// fallback ordering before a terminal failure.
void post_audition_service_notifications(const sdl_audition_service_result_t& result,
                                         const notification_sink_t& notifications);
void post_preview_service_notifications(const sdl_preview_service_result_t& result,
                                        const notification_sink_t& notifications);

// Services both audio transports and projects the chart-preview playhead for
// one application frame. Call after autosave and before waveform job handling.
void service_audio_transport_frame(
    const song_document_t& document, const workspace_controller_t& workspace,
    runtime::chart_preview_t& preview_state, audio::preview_session_t& preview_session,
    sdl_audition_output_t& audition_output, sdl_preview_output_t& preview_output,
    std::optional<double>& preview_playhead_absolute_row, const notification_sink_t& notifications,
    std::uint64_t expected_presentation_ns = 0);

} // namespace wormedit::sys
