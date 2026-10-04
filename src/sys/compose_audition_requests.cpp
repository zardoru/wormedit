#include "sys/compose_audition_requests.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/audition_resolver.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace wormedit::sys {
namespace {

auto audition_diagnostic_severity(diagnostic_severity_t severity) -> notification_severity_t {
    switch (severity) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}
void post_audition_started(const notification_sink_t& notifications, bool started,
    std::string_view diagnostic, std::string_view success, std::string_view failure) {
    notifications.post(started ? notification_severity_t::hint : notification_severity_t::error,
        notification_source_t::audio, started ? success : diagnostic.empty() ? failure : diagnostic);
}

void pause_preview(audio::preview_session_t& preview_session,
                   sdl_preview_output_t& preview_output,
                   runtime::chart_preview_t& preview_state) {
    if (!preview_session.active()) {
        return;
    }
    const auto frame = preview_session.timeline_frame();
    static_cast<void>(preview_output.stop());
    preview_state.pause_at(frame);
}

void process_object_request(const editor_object_audition_request_t& request,
                            document_history_t& history,
                            audio::decoded_source_registry_t& audition_sources,
                            audio::object_audition_controller_t& object_audition,
                            audio::preview_session_t& preview_session,
                            sdl_preview_output_t& preview_output,
                            runtime::chart_preview_t& preview_state,
                            sdl_audition_output_t& audition_output,
                            const notification_sink_t& notifications) {
    if (request.object.kind != editor_selection_object_kind_t::note &&
        request.object.kind != editor_selection_object_kind_t::bgm_event) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio,
            "Only notes and BGM events can be prepared for audition.");
        return;
    }

    audition_sources.await_pending();
    const auto prepared = object_audition.prepare(
        history.document, {request.document_revision, request.chart, request.object.id},
        audition_sources.bindings());
    if (prepared) {
        pause_preview(preview_session, preview_output, preview_state);
        const auto started = audition_output.start(prepared.prepared->clips);
        post_audition_started(notifications, static_cast<bool>(started), started.message,
            "Audio preview started.", "Audio preview could not start.");
    } else if (!prepared.diagnostics.empty()) {
        notifications.post(audition_diagnostic_severity(prepared.diagnostics.front().severity),
            notification_source_t::audio, prepared.diagnostics.front().message);
    } else if (prepared.status == audio::object_audition_status_t::stale_document) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio, "Audio preview request expired after the document changed.");
    } else {
        notifications.post(notification_severity_t::error, notification_source_t::audio, "Audio preview could not be prepared.");
    }
}

void process_draft_request(const bmson_draft_audition_request_t& request,
                           document_history_t& history,
                           audio::decoded_source_registry_t& audition_sources,
                           audio::preview_session_t& preview_session,
                           sdl_preview_output_t& preview_output,
                           runtime::chart_preview_t& preview_state,
                           sdl_audition_output_t& audition_output,
                           const notification_sink_t& notifications) {
    audition_sources.await_pending();
    const auto resolved = audio::resolve_bmson_draft_audition(
        history.document,
        {.expected_revision = request.revision,
         .chart = request.chart,
         .reference_bgm_event = request.reference,
         .markers = request.markers,
         .marker_index = request.marker_index},
        audition_sources.bindings());
    if (resolved) {
        pause_preview(preview_session, preview_output, preview_state);
        const auto started = audition_output.start(resolved.clips);
        post_audition_started(notifications, static_cast<bool>(started), started.message,
            "Staged BMSON region audition started.", "Staged BMSON region audition could not start.");
    } else if (!resolved.diagnostics.empty()) {
        notifications.post(audition_diagnostic_severity(resolved.diagnostics.front().severity),
            notification_source_t::audio, resolved.diagnostics.front().message);
    } else {
        notifications.post(notification_severity_t::error, notification_source_t::audio, "Staged BMSON region audition could not be prepared.");
    }
}

} // namespace

void process_sound_audition_request(
    std::optional<resource_id_t>& request, document_history_t& history,
    audio::decoded_source_registry_t& audition_sources,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
    runtime::chart_preview_t& preview_state, sdl_audition_output_t& audition_output,
    const notification_sink_t& notifications) {
    const auto resource_id = std::exchange(request, std::nullopt);
    if (!resource_id) return;
    const auto resource = std::ranges::find(history.document.resources, *resource_id,
                                            &resource_entry_t::id);
    if (resource == history.document.resources.end()) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio, "That sound is no longer in the list.");
        return;
    }
    audition_sources.await_pending();
    const auto bindings = audition_sources.bindings();
    const auto binding = std::ranges::find(bindings, resource->asset,
                                           &audio::audition_source_binding_t::asset);
    if (binding == bindings.end() || binding->output_frame_count == 0) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio, "That sound's audio is not loaded yet.");
        return;
    }
    pause_preview(preview_session, preview_output, preview_state);
    const std::vector<audio::audition_clip_t> clips{
        {binding->source, 0, binding->output_frame_count, 1.0F}};
    const auto started = audition_output.start(clips);
    post_audition_started(notifications, static_cast<bool>(started), started.message,
        "Audio preview started.", "Audio preview could not start.");
}

void process_compose_audition_requests(
    compose_scene_cache_t& cache, document_history_t& history,
    audio::decoded_source_registry_t& audition_sources,
    audio::object_audition_controller_t& object_audition,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
    runtime::chart_preview_t& preview_state, sdl_audition_output_t& audition_output,
    const notification_sink_t& notifications) {
    auto object_request = std::exchange(cache.audition_request, std::nullopt);
    auto draft_request = std::exchange(cache.draft_audition_request, std::nullopt);
    if (object_request) {
        process_object_request(*object_request, history, audition_sources, object_audition,
                               preview_session, preview_output, preview_state, audition_output,
                               notifications);
    }
    if (draft_request) {
        process_draft_request(*draft_request, history, audition_sources, preview_session,
                              preview_output, preview_state, audition_output, notifications);
    }
}

} // namespace wormedit::sys

#else

#include <utility>

namespace wormedit::sys {

void process_sound_audition_request(std::optional<resource_id_t>& request, const notification_sink_t& notifications) {
    if (std::exchange(request, std::nullopt)) notifications.post(notification_severity_t::warning, notification_source_t::audio,
            "Audio preview is unavailable in this build.");
}

void process_compose_audition_requests(compose_scene_cache_t& cache,
                                       document_history_t& history, const notification_sink_t& notifications) {
    static_cast<void>(history);
    const auto object_request = std::exchange(cache.audition_request, std::nullopt);
    const auto draft_request = std::exchange(cache.draft_audition_request, std::nullopt);
    if (object_request || draft_request) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio,
            "Audio preview is unavailable in this build.");
    }
}

} // namespace wormedit::sys

#endif
