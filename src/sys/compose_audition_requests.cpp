#include "sys/compose_audition_requests.h"

#include <algorithm>
#include <utility>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/audition_resolver.h>
#include <vector>

namespace wormedit::sys {
namespace {

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
    if (!preview_session.is_active()) {
        return;
    }
    const auto frame = preview_session.timeline_frame();
    static_cast<void>(preview_output.stop());
    preview_state.pause_at(frame);
}

auto object_sound(const song_document_t& song, chart_id_t chart_id, object_id_t id)
    -> std::optional<resource_id_t> {
    const auto chart = song.find_chart(chart_id);
    if (chart == nullptr)
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

} // namespace

void compose_auditions_t::process_object_request(const editor_object_audition_request_t& request,
                                                const compose_audition_inputs_t& inputs) {
    auto& history = inputs.history;
    if (request.object.kind != editor_selection_object_kind_t::note &&
        request.object.kind != editor_selection_object_kind_t::bgm_event) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "Only notes and BGM events can be prepared for audition.");
        return;
    }

    inputs.audition.sources.ensure_current(history.document);
    inputs.audition.sources.registry().await_pending();
    auto prepared = inputs.audition.controller.prepare_object(
        history.document, {request.document_revision, request.chart, request.object.id},
        inputs.audition.sources.bindings(history.document));
    if (prepared) {
        if (!audio::is_bound_stem_audition(
                history.document, object_sound(history.document, request.chart, request.object.id),
                inputs.audition.sources.bindings(history.document), prepared->prepared->clips)) {
            inputs.notifications.post(notification_severity_t::warning,
                                      notification_source_t::audio,
                                      "The stem preview has invalid decoded source geometry.");
            return;
        }
        pause_preview(inputs.preview.session, inputs.preview.output, inputs.preview.state);
        const auto started = inputs.audition.output.start(prepared->prepared->clips);
        post_audition_started(inputs.notifications, static_cast<bool>(started), started.message(),
                              "Audio preview started.", "Audio preview could not start.");
        return;
    }
    const auto& failure = prepared.error();
    if (!failure.diagnostics.empty() &&
        failure.diagnostics.front().code == audio::audition_resolve_code_t::missing_resource) {
        // A silent note or BGM event has nothing to play; clicking or placing it is not an error.
    } else if (!failure.diagnostics.empty()) {
        inputs.notifications.post(to_notification_severity(failure.diagnostics.front().severity),
                                  notification_source_t::audio,
                                  failure.diagnostics.front().message);
    } else if (failure.value.status.has_failure(failure_code_t::stale_generation)) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  stale_action_message("Audio preview request"));
    } else {
        inputs.notifications.post(notification_severity_t::error, notification_source_t::audio,
                                  "Audio preview could not be prepared.");
    }
}

void compose_auditions_t::process_draft_request(const bmson_draft_audition_request_t& request,
                                               const compose_audition_inputs_t& inputs) {
    auto& history = inputs.history;
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
        post_audition_started(inputs.notifications, static_cast<bool>(started), started.message(),
                              "Staged BMSON region audition started.",
                              "Staged BMSON region audition could not start.");
    } else if (!resolved.diagnostics.empty()) {
        inputs.notifications.post(to_notification_severity(resolved.diagnostics.front().severity),
                                  notification_source_t::audio,
                                  resolved.diagnostics.front().message);
    } else {
        inputs.notifications.post(notification_severity_t::error, notification_source_t::audio,
                                  "Staged BMSON region audition could not be prepared.");
    }
}


void compose_auditions_t::process_catalog_request(resource_id_t resource_id,
                                                 const compose_audition_inputs_t& inputs) {
    auto& history = inputs.history;
    const auto resource =
        std::ranges::find(history.document.resources, resource_id, &resource_entry_t::id);
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
    if (!audio::is_bound_stem_audition(history.document, resource_id, bindings, clips)) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "The stem preview has invalid decoded source geometry.");
        return;
    }
    pause_preview(inputs.preview.session, inputs.preview.output, inputs.preview.state);
    const auto started = inputs.audition.output.start(clips);
    post_audition_started(inputs.notifications, static_cast<bool>(started), started.message(),
                          "Audio preview started.", "Audio preview could not start.");
}

} // namespace wormedit::sys
#endif

namespace wormedit::sys {

compose_auditions_t::scope_t::scope_t(compose_auditions_t& owner,
    compose_audition_inputs_t inputs, phase_t phase)
    : owner_(owner), inputs_(inputs), phase_(phase) {
    owner_.begin(phase_);
}

compose_auditions_t::scope_t::~scope_t() { owner_.finish(phase_, inputs_); }

auto compose_auditions_t::input_scope(compose_audition_inputs_t inputs) -> scope_t {
    return {*this, inputs, phase_t::input};
}

auto compose_auditions_t::catalog_scope(compose_audition_inputs_t inputs) -> scope_t {
    return {*this, inputs, phase_t::catalog};
}

void compose_auditions_t::submit(audition_request_t request) {
    pending_[request.index()] = std::move(request);
}

auto compose_auditions_t::has_pending_request() const -> bool {
    return std::ranges::any_of(pending_, [](const auto& request) { return request.has_value(); });
}

auto compose_auditions_t::has_pending_request(const audition_request_t& request) const -> bool {
    return pending_[request.index()] == request;
}

void compose_auditions_t::begin(phase_t phase) {
    if (phase != phase_t::input) return;
    for (std::size_t slot = 0; slot < index<resource_id_t>(); ++slot)
        pending_[slot].reset();
}

void compose_auditions_t::finish(phase_t phase, const compose_audition_inputs_t& inputs) {
    const auto first = phase == phase_t::input ? std::size_t{0} : index<resource_id_t>();
    const auto after = phase == phase_t::input ? index<resource_id_t>() : pending_.size();
    pending_t requests;
    // Consume the whole phase before any notification or audio operation.
    for (auto slot = first; slot < after; ++slot)
        requests[slot] = std::exchange(pending_[slot], std::nullopt);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    for (const auto& request : requests) {
        if (!request) continue;
        std::visit([&inputs](const auto& value) {
            using request_t = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<request_t, editor_object_audition_request_t>)
                process_object_request(value, inputs);
            else if constexpr (std::same_as<request_t, bmson_draft_audition_request_t>)
                process_draft_request(value, inputs);
            else
                process_catalog_request(value, inputs);
        }, *request);
    }
#else
    if (std::ranges::any_of(requests, [](const auto& request) { return request.has_value(); }))
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::audio,
                                  "Audio preview is unavailable in this build.");
#endif
}

} // namespace wormedit::sys
