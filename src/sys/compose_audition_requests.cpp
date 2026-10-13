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
    case diagnostic_severity_t::info:
        return notification_severity_t::info;
    case diagnostic_severity_t::warning:
        return notification_severity_t::warning;
    case diagnostic_severity_t::error:
        return notification_severity_t::error;
    }
    return notification_severity_t::error;
}
void post_audition_started(const notification_sink_t& notifications, bool started,
                           std::string_view diagnostic, std::string_view success,
                           std::string_view failure) {
    if (started)
        notifications.post_transient_hint(notification_source_t::audio, success);
    else
        notifications.post(notification_severity_t::error, notification_source_t::audio,
                           diagnostic.empty() ? failure : diagnostic);
}

void pause_preview(audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
                   runtime::chart_preview_t& preview_state) {
    if (!preview_session.active()) {
        return;
    }
    const auto frame = preview_session.timeline_frame();
    static_cast<void>(preview_output.stop());
    preview_state.pause_at(frame);
}

auto object_sound(const song_document_t& song, chart_id_t chart_id, object_id_t id)
    -> std::optional<resource_id_t> {
    const auto chart = std::ranges::find(song.charts, chart_id, &chart_document_t::id);
    if (chart == song.charts.end())
        return std::nullopt;
    for (const auto& measure : chart->measures) {
        const auto note = std::ranges::find(measure.notes, id, &note_t::id);
        if (note != measure.notes.end())
            return note->sound;
        const auto event = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (event != measure.bgm_events.end())
            return event->sound;
    }
    return std::nullopt;
}

void process_object_request(const editor_object_audition_request_t& request,
                            document_history_t& history, const compose_audition_inputs_t& inputs) {
    if (request.object.kind != editor_selection_object_kind_t::note &&
        request.object.kind != editor_selection_object_kind_t::bgm_event) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "Only notes and BGM events can be prepared for audition.");
        return;
    }

    inputs.audition.sources.ensure_current(history.document);
    inputs.audition.sources.registry().await_pending();
    auto prepared = inputs.audition.controller.prepare(
        history.document, {request.document_revision, request.chart, request.object.id},
        inputs.audition.sources.bindings(history.document));
    if (prepared) {
        if (!audio::bound_stem_audition(
                history.document, object_sound(history.document, request.chart, request.object.id),
                inputs.audition.sources.bindings(history.document), prepared.prepared->clips)) {
            inputs.notifications.post(notification_severity_t::warning,
                                      notification_source_t::audio,
                                      "The stem preview has invalid decoded source geometry.");
            return;
        }
        pause_preview(inputs.preview.session, inputs.preview.output, inputs.preview.state);
        const auto started = inputs.audition.output.start(prepared.prepared->clips);
        post_audition_started(inputs.notifications, static_cast<bool>(started), started.message,
                              "Audio preview started.", "Audio preview could not start.");
    } else if (!prepared.diagnostics.empty() &&
               prepared.diagnostics.front().code ==
                   audio::audition_resolve_code_t::missing_resource) {
        // A silent note or BGM event has nothing to play; clicking or placing it is not an error.
    } else if (!prepared.diagnostics.empty()) {
        inputs.notifications.post(
            audition_diagnostic_severity(prepared.diagnostics.front().severity),
            notification_source_t::audio, prepared.diagnostics.front().message);
    } else if (prepared.status == audio::object_audition_status_t::stale_document) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "Audio preview request expired after the document changed.");
    } else {
        inputs.notifications.post(notification_severity_t::error, notification_source_t::audio,
                                  "Audio preview could not be prepared.");
    }
}

void process_draft_request(const bmson_draft_audition_request_t& request,
                           document_history_t& history, const compose_audition_inputs_t& inputs) {
    inputs.audition.sources.ensure_current(history.document);
    inputs.audition.sources.registry().await_pending();
    const auto resolved =
        audio::resolve_bmson_draft_audition(history.document,
                                            {.expected_revision = request.revision,
                                             .chart = request.chart,
                                             .reference_bgm_event = request.reference,
                                             .markers = request.markers,
                                             .marker_index = request.marker_index},
                                            inputs.audition.sources.bindings(history.document));
    if (resolved) {
        pause_preview(inputs.preview.session, inputs.preview.output, inputs.preview.state);
        const auto started = inputs.audition.output.start(resolved.clips);
        post_audition_started(inputs.notifications, static_cast<bool>(started), started.message,
                              "Staged BMSON region audition started.",
                              "Staged BMSON region audition could not start.");
    } else if (!resolved.diagnostics.empty()) {
        inputs.notifications.post(
            audition_diagnostic_severity(resolved.diagnostics.front().severity),
            notification_source_t::audio, resolved.diagnostics.front().message);
    } else {
        inputs.notifications.post(notification_severity_t::error, notification_source_t::audio,
                                  "Staged BMSON region audition could not be prepared.");
    }
}

} // namespace

void process_sound_audition_request(std::optional<resource_id_t>& request,
                                    document_history_t& history,
                                    const compose_audition_inputs_t& inputs) {
    const auto resource_id = std::exchange(request, std::nullopt);
    if (!resource_id)
        return;
    const auto resource =
        std::ranges::find(history.document.resources, *resource_id, &resource_entry_t::id);
    if (resource == history.document.resources.end()) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "That sound is no longer in the list.");
        return;
    }
    inputs.audition.sources.ensure_current(history.document);
    inputs.audition.sources.registry().await_pending();
    const auto bindings = inputs.audition.sources.bindings(history.document);
    const auto binding =
        std::ranges::find(bindings, resource->asset, &audio::audition_source_binding_t::asset);
    if (binding == bindings.end() || binding->output_frame_count == 0) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "That sound's audio is not loaded yet.");
        return;
    }
    std::vector<audio::audition_clip_t> clips{
        {binding->source, 0, binding->output_frame_count, 1.0F}};
    if (!audio::bound_stem_audition(history.document, resource_id, bindings, clips)) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "The stem preview has invalid decoded source geometry.");
        return;
    }
    pause_preview(inputs.preview.session, inputs.preview.output, inputs.preview.state);
    const auto started = inputs.audition.output.start(clips);
    post_audition_started(inputs.notifications, static_cast<bool>(started), started.message,
                          "Audio preview started.", "Audio preview could not start.");
}

void process_compose_audition_requests(compose_scene_cache_t& cache, document_history_t& history,
                                       const compose_audition_inputs_t& inputs) {
    auto object_request = std::exchange(cache.frame.audition_request, std::nullopt);
    auto draft_request = std::exchange(cache.frame.draft_audition_request, std::nullopt);
    if (object_request) {
        process_object_request(*object_request, history, inputs);
    }
    if (draft_request) {
        process_draft_request(*draft_request, history, inputs);
    }
}

} // namespace wormedit::sys

#else

#include <utility>

namespace wormedit::sys {

void process_sound_audition_request(std::optional<resource_id_t>& request,
                                    const notification_sink_t& notifications) {
    if (std::exchange(request, std::nullopt))
        notifications.post(notification_severity_t::warning, notification_source_t::audio,
                           "Audio preview is unavailable in this build.");
}

void process_compose_audition_requests(compose_scene_cache_t& cache, document_history_t& history,
                                       const notification_sink_t& notifications) {
    static_cast<void>(history);
    const auto object_request = std::exchange(cache.frame.audition_request, std::nullopt);
    const auto draft_request = std::exchange(cache.frame.draft_audition_request, std::nullopt);
    if (object_request || draft_request) {
        notifications.post(notification_severity_t::warning, notification_source_t::audio,
                           "Audio preview is unavailable in this build.");
    }
}

} // namespace wormedit::sys

#endif
