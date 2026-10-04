#include "sys/audio_transport_frame.h"
#include <wormedit/audio/audition_bridge.h>
#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
void verify_audio_notification(bool value) { REQUIRE(value); }
auto drained(notification_center_t& center) -> std::vector<notification_entry_t> {
    center.drain_inbox(std::chrono::seconds{1});
    return center.snapshot(notification_capacity);
}
void check_audio_error(const notification_entry_t& entry, std::string_view text) {
    verify_audio_notification(entry.severity == notification_severity_t::error);
    verify_audio_notification(entry.source == notification_source_t::audio);
    verify_audio_notification(entry.message == text);
}
}

TEST_CASE("Audio service producer preserves fallbacks before the terminal device failure",
          "[audio][notifications][audio-notifications]") {
    notification_center_t center{[] { return notification_time_t{}; }};
    notification_sink_t sink{center};
    const sdl_preview_service_result_t result{
        .status = sdl_preview_service_status_t::device_failed,
        .rate_unavailable = true, .silenced_voices = 3, .silenced_music_voices = 1};
    post_preview_service_notifications(result, sink);
    const auto rows = drained(center);
    verify_audio_notification(rows.size() == 3);
    check_audio_error(rows[0], "Chart preview stopped after an output-device failure.");
    verify_audio_notification(rows[1].severity == notification_severity_t::warning);
    verify_audio_notification(rows[1].message == "Silenced 3 preview voice(s), including music, that were not decoded in time.");
    verify_audio_notification(rows[2].severity == notification_severity_t::warning);
    verify_audio_notification(rows[2].message == "Playback mode change is unavailable; preview keeps its previous mode.");
}

TEST_CASE("Audio underruns dedupe while ready service results stay quiet",
          "[audio][notifications][audio-notifications]") {
    notification_center_t center{[] { return notification_time_t{}; }};
    notification_sink_t sink{center};
    post_audition_service_notifications({}, sink);
    post_preview_service_notifications({}, sink);
    verify_audio_notification(drained(center).empty());
    const sdl_audition_service_result_t underrun{.status = sdl_audition_service_status_t::render_failed};
    post_audition_service_notifications(underrun, sink);
    post_audition_service_notifications(underrun, sink);
    const auto rows = drained(center);
    verify_audio_notification(rows.size() == 1);
    verify_audio_notification(rows.front().count == 2);
    check_audio_error(rows.front(), "Audio preview stopped after a realtime cache underrun.");
}

TEST_CASE("Audio producer source and bounded failures preserve explicit error metadata",
          "[audio][notifications][audio-notifications]") {
    notification_center_t center{[] { return notification_time_t{}; }};
    notification_sink_t sink{center};
    sdl_audition_service_result_t audition{.status = sdl_audition_service_status_t::pump_failed,
        .pump = audio::audition_pump_result_t{.status = audio::audition_pump_status_t::source_unavailable}};
    post_audition_service_notifications(audition, sink);
    auto rows = drained(center);
    check_audio_error(rows[0], "Audio preview stopped because its decoded source is unavailable.");
    sdl_preview_service_result_t preview{.status = sdl_preview_service_status_t::pump_failed,
        .pump = audio::preview_session_pump_result_t{
            .status = audio::preview_session_pump_status_t::decode_failed,
            .decode = audio::preview_pump_result_t{.status = audio::preview_pump_status_t::source_unavailable}}};
    post_preview_service_notifications(preview, sink);
    rows = drained(center);
    check_audio_error(rows[0], "Chart preview stopped because a decoded source is unavailable.");
    preview.pump.reset();
    post_preview_service_notifications(preview, sink);
    rows = drained(center);
    check_audio_error(rows[0], "Chart preview stopped after a bounded refill failure.");
    preview.status = sdl_preview_service_status_t::render_failed;
    post_preview_service_notifications(preview, sink);
    rows = drained(center);
    check_audio_error(rows[0], "Chart preview stopped after a realtime mix failure.");
}

TEST_CASE("Idle audio service and consumed selection interruption never repost",
          "[audio][notifications][audio-notifications]") {
    audio::decoded_source_registry_t sources;
    audio::decoded_block_cache_t blocks;
    audio::transport_t transport{blocks};
    audio::audition_transport_bridge_t audition_bridge{transport};
    audio::audition_session_t audition_session{sources, transport, audition_bridge};
    audio::preview_transport_bridge_t preview_bridge{transport};
    audio::preview_session_t preview_session{sources, transport, preview_bridge};
    sdl_audition_output_t audition{audition_session};
    sdl_preview_output_t preview{preview_session};
    notification_center_t center{[] { return notification_time_t{}; }};
    notification_sink_t sink{center};
    song_document_t document;
    workspace_controller_t workspace;
    runtime::chart_preview_t state;
    std::optional<double> playhead;
    service_audio_transport_frame(document, workspace, state, preview_session,
                                  audition, preview, playhead, sink);
    verify_audio_notification(drained(center).empty());
    state.chart = chart_id_t{42}; state.paused = true; state.resume_frame = 300;
    sink.post(notification_severity_t::success, notification_source_t::edit, "Applied edit.");
    service_audio_transport_frame(document, workspace, state, preview_session,
                                  audition, preview, playhead, sink);
    auto rows = drained(center);
    verify_audio_notification(rows.size() == 2);
    verify_audio_notification(rows[0].severity == notification_severity_t::hint);
    verify_audio_notification(center.current_line()->message == "Applied edit.");
    verify_audio_notification(!state.chart && !state.paused && state.resume_frame == 0);
    verify_audio_notification(!playhead);
    service_audio_transport_frame(document, workspace, state, preview_session,
                                  audition, preview, playhead, sink);
    verify_audio_notification(drained(center).size() == 2);
}
