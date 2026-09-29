#include "sys/compose_audition_requests.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/audition_resolver.h>

#include <utility>

namespace wormedit::sys {
namespace {

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
                            std::string& status) {
    if (request.object.kind != editor_selection_object_kind_t::note &&
        request.object.kind != editor_selection_object_kind_t::bgm_event) {
        status = "Only notes and BGM events can be prepared for audition.";
        return;
    }

    const auto prepared = object_audition.prepare(
        history.document, {request.document_revision, request.chart, request.object.id},
        audition_sources.bindings());
    if (prepared) {
        pause_preview(preview_session, preview_output, preview_state);
        const auto started = audition_output.start(prepared.prepared->clips);
        status = started                   ? "Audio preview started."
                 : started.message.empty() ? "Audio preview could not start."
                                           : started.message;
    } else if (!prepared.diagnostics.empty()) {
        status = prepared.diagnostics.front().message;
    } else if (prepared.status == audio::object_audition_status_t::stale_document) {
        status = "Audio preview request expired after the document changed.";
    } else {
        status = "Audio preview could not be prepared.";
    }
}

void process_draft_request(const bmson_draft_audition_request_t& request,
                           document_history_t& history,
                           audio::decoded_source_registry_t& audition_sources,
                           audio::preview_session_t& preview_session,
                           sdl_preview_output_t& preview_output,
                           runtime::chart_preview_t& preview_state,
                           sdl_audition_output_t& audition_output,
                           std::string& status) {
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
        status = started ? "Staged BMSON region audition started."
                         : started.message.empty()
                             ? "Staged BMSON region audition could not start."
                             : started.message;
    } else if (!resolved.diagnostics.empty()) {
        status = resolved.diagnostics.front().message;
    } else {
        status = "Staged BMSON region audition could not be prepared.";
    }
}

} // namespace

void process_compose_audition_requests(
    compose_scene_cache_t& cache, document_history_t& history,
    audio::decoded_source_registry_t& audition_sources,
    audio::object_audition_controller_t& object_audition,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
    runtime::chart_preview_t& preview_state, sdl_audition_output_t& audition_output,
    std::string& status) {
    auto object_request = std::exchange(cache.audition_request, std::nullopt);
    auto draft_request = std::exchange(cache.draft_audition_request, std::nullopt);
    if (object_request) {
        process_object_request(*object_request, history, audition_sources, object_audition,
                               preview_session, preview_output, preview_state, audition_output,
                               status);
    }
    if (draft_request) {
        process_draft_request(*draft_request, history, audition_sources, preview_session,
                              preview_output, preview_state, audition_output, status);
    }
}

} // namespace wormedit::sys

#else

#include <utility>

namespace wormedit::sys {

void process_compose_audition_requests(compose_scene_cache_t& cache,
                                       document_history_t& history, std::string& status) {
    static_cast<void>(history);
    const auto object_request = std::exchange(cache.audition_request, std::nullopt);
    const auto draft_request = std::exchange(cache.draft_audition_request, std::nullopt);
    if (object_request || draft_request) {
        status = "Audio preview is unavailable in this build.";
    }
}

} // namespace wormedit::sys

#endif
