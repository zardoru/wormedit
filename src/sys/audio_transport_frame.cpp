#include "sys/audio_transport_frame.h"

#include <wormedit/core/timing.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace wormedit::sys {
namespace {

void validate_preview_binding(const song_document_t& document,
                              const workspace_controller_t& workspace,
                              runtime::chart_preview_t& state,
                              audio::preview_session_t& session,
                              sdl_preview_output_t& output, std::string& status) {
    if (!state.chart || state.matches_selection(document, workspace))
        return;
    const bool interrupted = session.active() || state.paused;
    if (session.active())
        static_cast<void>(output.stop());
    state.invalidate();
    if (interrupted)
        status = "Chart preview stopped because its document or selected chart changed.";
}

void service_audition(sdl_audition_output_t& output, std::string& status) {
    const auto serviced = output.service();
    if (!serviced || *serviced)
        return;
    static_cast<void>(output.stop());
    if (serviced->status == sdl_audition_service_status_t::device_failed)
        status = "Audio preview stopped after an output-device failure.";
    else if (serviced->status == sdl_audition_service_status_t::render_failed)
        status = "Audio preview stopped after a realtime cache underrun.";
    else if (serviced->pump &&
             serviced->pump->status == audio::audition_pump_status_t::source_unavailable)
        status = "Audio preview stopped because its decoded source is unavailable.";
    else
        status = "Audio preview stopped after a bounded lookahead failure.";
}

void service_chart_preview(sdl_preview_output_t& output,
                           runtime::chart_preview_t& state, std::string& status) {
    const auto serviced = output.service();
    if (!serviced)
        return;
    if (serviced->silenced_voices > 0) {
        status = "Silenced " + std::to_string(serviced->silenced_voices) +
                 " keysound(s) that were not decoded in time.";
    }
    if (*serviced)
        return;
    static_cast<void>(output.stop());
    state.reset_transport();
    if (serviced->status == sdl_preview_service_status_t::device_failed)
        status = "Chart preview stopped after an output-device failure.";
    else if (serviced->status == sdl_preview_service_status_t::render_failed)
        status = "Chart preview stopped after a realtime mix failure.";
    else if (serviced->pump && serviced->pump->decode &&
             serviced->pump->decode->status == audio::preview_pump_status_t::source_unavailable)
        status = "Chart preview stopped because a decoded source is unavailable.";
    else
        status = "Chart preview stopped after a bounded refill failure.";
}

auto preview_playhead_row(const runtime::chart_preview_t& state,
                          const audio::preview_session_t& session) -> std::optional<double> {
    if (!state.timing || (!session.active() && !state.paused))
        return std::nullopt;
    const auto frame = state.current_frame(session);
    if (frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const auto position = musical_position_at(
        *state.timing, {static_cast<std::int64_t>(frame), audio::output_sample_rate});
    if (!position || position.absolute_row->denominator == 0)
        return std::nullopt;
    const auto row = static_cast<double>(position.absolute_row->numerator) /
                     static_cast<double>(position.absolute_row->denominator);
    return std::isfinite(row) ? std::optional<double>{row} : std::nullopt;
}

} // namespace

void service_audio_transport_frame(
    const song_document_t& document, const workspace_controller_t& workspace,
    runtime::chart_preview_t& preview_state, audio::preview_session_t& preview_session,
    sdl_audition_output_t& audition_output, sdl_preview_output_t& preview_output,
    std::optional<double>& preview_playhead_absolute_row, std::string& status) {
    validate_preview_binding(document, workspace, preview_state, preview_session,
                             preview_output, status);
    service_audition(audition_output, status);
    service_chart_preview(preview_output, preview_state, status);
    if (!preview_session.active() && !preview_state.paused)
        preview_state.resume_frame = 0;
    preview_playhead_absolute_row = preview_playhead_row(preview_state, preview_session);
}

} // namespace wormedit::sys
