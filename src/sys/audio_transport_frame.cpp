#include "sys/audio_transport_frame.h"

#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace wormedit::sys {
namespace {

void validate_preview_binding(const song_document_t& document,
                              const workspace_controller_t& workspace,
                              runtime::chart_preview_t& state,
                              sdl_preview_output_t& output, const notification_sink_t& notifications) {
    if (!state.chart || state.matches_selection(document, workspace))
        return;
    const bool interrupted = output.playing() || state.paused;
    if (output.playing())
        static_cast<void>(output.stop());
    state.invalidate();
    if (interrupted)
        notifications.post(notification_severity_t::hint, notification_source_t::audio,
            "Chart preview stopped because its document or selected chart changed.");
}

void service_audition(sdl_audition_output_t& output, const notification_sink_t& notifications) {
    const auto serviced = output.service();
    if (!serviced || !serviced->status.has_failure())
        return;
    static_cast<void>(output.stop());
    post_audition_service_notifications(*serviced, notifications);
}

void service_chart_preview(sdl_preview_output_t& output,
                           runtime::chart_preview_t& state, const notification_sink_t& notifications) {
    const auto serviced = output.service();
    if (!serviced)
        return;
    post_preview_service_notifications(*serviced, notifications);
    if (!serviced->status.has_failure()) return;
    const auto failed_frame = output.timeline_frame();
    static_cast<void>(output.stop());
    if (serviced->status.has_failure(failure_code_t::pump_failed))
        state.pause_at(failed_frame);
    else
        state.reset_transport();
}

auto audition_failure_message(const sdl_audition_service_result_t& result) -> const char* {
    if (result.status.has_failure(failure_code_t::device_failed))
        return "Audio preview stopped after an output-device failure.";
    else if (result.status.has_failure(failure_code_t::render_failed))
        return "Audio preview stopped after a realtime cache underrun.";
    else if (result.pump) {
        const auto& pumped = *result.pump;
        if (pumped.status == failure_code_t::unavailable)
            return "Audio preview stopped because its decoded source is unavailable.";
    }
    return "Audio preview stopped after a bounded lookahead failure.";
}

auto preview_failure_message(const sdl_preview_service_result_t& result) -> const char* {
    if (result.status.has_failure(failure_code_t::device_failed))
        return "Chart preview stopped after an output-device failure.";
    else if (result.status.has_failure(failure_code_t::render_failed))
        return "Chart preview stopped after a realtime mix failure.";
    else if (result.pump) {
        const auto& pumped = *result.pump;
        if (pumped.decode) {
            const auto& decoded = *pumped.decode;
            if (decoded.status == failure_code_t::unavailable)
                return "Chart preview stopped because a decoded source is unavailable.";
        }
    }
    return "Chart preview stopped after a bounded refill failure.";
}

auto preview_playhead_row(const runtime::chart_preview_t& state,
                          const audio::preview_session_t& session) -> std::optional<double> {
    if (!state.timing || (!state.playing(session) && !state.paused))
        return std::nullopt;
    return state.timing->row_at_frame(state.current_frame(session), audio::output_sample_rate,
                                     chart_end_policy_t::clamp_to_last_row);
}

} // namespace

void post_audition_service_notifications(const sdl_audition_service_result_t& result,
                                         const notification_sink_t& notifications) {
    if (!result.status.has_failure()) return;
    notifications.post(notification_severity_t::error, notification_source_t::audio,
                       audition_failure_message(result));
}

void post_preview_service_notifications(const sdl_preview_service_result_t& result,
                                        const notification_sink_t& notifications) {
    const auto& effect = result;
    if (effect.telemetry.rate_unavailable)
        notifications.post(notification_severity_t::warning, notification_source_t::audio,
            "Playback mode change is unavailable; preview keeps its previous mode.");
    if (effect.telemetry.silenced_voices > 0) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio,
            "Silenced " + std::to_string(effect.telemetry.silenced_voices) +
                (effect.telemetry.silenced_music_voices > 0
                    ? " preview voice(s), including music, that were not decoded in time."
                    : " preview voice(s) that were not decoded in time."));
    }
    if (result.status.has_failure())
        notifications.post(notification_severity_t::error, notification_source_t::audio,
                           preview_failure_message(effect));
}

void service_audio_transport_frame(
    const song_document_t& document, const workspace_controller_t& workspace,
    runtime::chart_preview_t& preview_state, audio::preview_session_t& preview_session,
    sdl_audition_output_t& audition_output, sdl_preview_output_t& preview_output,
    std::optional<double>& preview_playhead_absolute_row, const notification_sink_t& notifications) {
    validate_preview_binding(document, workspace, preview_state,
                             preview_output, notifications);
    service_audition(audition_output, notifications);
    service_chart_preview(preview_output, preview_state, notifications);
    if (!preview_output.playing() && !preview_state.paused)
        preview_state.resume_frame = 0;
    preview_playhead_absolute_row = preview_playhead_row(preview_state, preview_session);
}

} // namespace wormedit::sys
