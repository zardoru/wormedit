#include "sys/audio_transport_frame.h"
#include <wormedit/audio/audition_bridge.h>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

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

TEST_CASE("Preview audio tail retains the final editable playhead row", "[audio][playhead-tail]") {
    timing_index_t timing{.divisions_per_standard_measure = 960, .total_rows = 1920};
    timing.rows = {{.absolute_row = 0, .before_delays_seconds = {0, 1},
                   .event_seconds = {0, 1}, .after_stops_seconds = {0, 1}, .effective_bpm = {120, 1}},
                  {.absolute_row = 1920, .before_delays_seconds = {4, 1},
                   .event_seconds = {4, 1}, .after_stops_seconds = {4, 1}, .effective_bpm = {120, 1}}};
    timing.segments = {{.begin_row = 0, .end_row = 1920, .begin_seconds = {0, 1},
                        .end_seconds = {4, 1}, .effective_bpm = {120, 1}}};
    CHECK(preview_playhead_row_at(timing, audio::output_sample_rate * 2) == 960.0);
    CHECK(preview_playhead_row_at(timing, audio::output_sample_rate * 4) == 1919.0);
    CHECK(preview_playhead_row_at(timing, audio::output_sample_rate * 4 + 1) == 1919.0);
    CHECK(preview_playhead_row_at(timing, audio::output_sample_rate * 30) == 1919.0);
    timing.rows.back().after_stops_seconds = {4, 0};
    CHECK_FALSE(preview_playhead_row_at(timing, audio::output_sample_rate * 30));
    CHECK_FALSE(preview_playhead_row_at({}, audio::output_sample_rate));
}

namespace {
auto deferred_seek_failure_preserves_position() -> bool {
    audio::decoded_source_registry_t sources;
    const auto source = sources.register_generated_source({
        .source = {"notification-seek", "v1"}, .interleaved_stereo = std::vector<float>(88'200, 0.5F)});
    if (!source) return false;
    audio::decoded_block_cache_t blocks;
    audio::transport_t transport{blocks};
    audio::preview_transport_bridge_t preview_bridge{transport};
    audio::preview_session_t session{sources, transport, preview_bridge};
    audio::audition_transport_bridge_t audition_bridge{transport};
    audio::audition_session_t audition_session{sources, transport, audition_bridge};
    sdl_audition_output_t audition{audition_session};
    sdl_preview_output_t output{session};
    if (!output.open()) return false;
    preview_schedule_t schedule{.sample_rate = audio::output_sample_rate};
    schedule.bgm = {{.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const audio::preview_object_source_binding_t binding{
        .object = object_id_t{1}, .voices = {{.source = source.binding->source, .frame_count = 44'100}}};
    if (!output.start(schedule, std::span{&binding, 1})) return false;
    sources.clear();
    if (!output.seek(1'234)) return false;
    notification_center_t center{[] { return notification_time_t{}; }};
    notification_sink_t sink{center};
    runtime::chart_preview_t state;
    song_document_t document;
    workspace_controller_t workspace;
    std::optional<double> playhead;
    const auto deadline = SDL_GetTicks() + 500;
    while (!state.paused && SDL_GetTicks() < deadline) {
        service_audio_transport_frame(document, workspace, state, session, audition, output,
                                      playhead, sink);
        SDL_Delay(1);
    }
    const auto rows = drained(center);
    return state.paused && state.resume_frame == 1'234 && !output.playing() &&
           rows.size() == 1 && rows.front().severity == notification_severity_t::error &&
           rows.front().message == "Chart preview stopped because a decoded source is unavailable.";
}
}

TEST_CASE("Deferred chart seek decode failure notifies and retains the requested position",
          "[audio][notifications][audio-notifications][async-seek]") {
    verify_audio_notification(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    verify_audio_notification(SDL_InitSubSystem(SDL_INIT_AUDIO));
    verify_audio_notification(deferred_seek_failure_preserves_position());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

TEST_CASE("Preview mix identities follow BGM lanes and their explicitly owned linked stem notes",
          "[audio][bgm-mix]") {
    chart_document_t chart;
    chart.bgm_source_lanes = {{.id = object_id_t{10}},
        {.id = object_id_t{20}, .stem_channel = object_id_t{7}}};
    chart.sound_channels = {{.id = object_id_t{7}, .events = {{.id = object_id_t{70}}}}};
    chart.measures = {{.id = measure_id_t{1},
        .notes = {{.id = object_id_t{1}, .sound_channel_event = object_id_t{70}}, {.id = object_id_t{2}}},
        .bgm_events = {{.id = object_id_t{3}, .source_lane = object_id_t{10}},
            {.id = object_id_t{4}, .source_lane = object_id_t{20}}}}};
    std::vector<audio::preview_object_source_binding_t> bindings;
    for (std::uint64_t id = 1; id <= 5; ++id)
        bindings.push_back({.object = object_id_t{id}, .voices = {{}, {}}});
    runtime::assign_preview_bgm_mix_groups(chart, bindings);
    const std::array<std::uint64_t, 5> expected{20, 0, 10, 20, 0};
    for (std::size_t index = 0; index < bindings.size(); ++index)
        for (const auto& voice : bindings[index].voices)
            verify_audio_notification(voice.mix_group == expected[index]);
    std::ranges::reverse(chart.bgm_source_lanes);
    runtime::assign_preview_bgm_mix_groups(chart, bindings);
    verify_audio_notification(bindings.front().voices.front().mix_group == 20);
    chart.bgm_source_lanes.front().stem_channel.reset();
    runtime::assign_preview_bgm_mix_groups(chart, bindings);
    verify_audio_notification(bindings.front().voices.front().mix_group == 0);
}
