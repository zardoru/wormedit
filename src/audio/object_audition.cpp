#include <wormedit/audio/object_audition.h>

namespace wormedit::audio {

auto object_audition_controller_t::prepare(const song_document_t& song,
                                           object_audition_request_t request,
                                           std::span<const audition_source_binding_t> bindings,
                                           audition_resolve_options_t options)
    -> object_audition_result_t {
    if (request.document_revision != song.revision) {
        return {.status = object_audition_status_t::stale_document,
                .prepared = std::nullopt,
                .transport = std::nullopt,
                .diagnostics = {}};
    }
    auto resolved = resolve_object_audition(song, request.chart, request.object, bindings, options);
    if (!resolved) {
        return {.status = object_audition_status_t::resolve_failed,
                .prepared = std::nullopt,
                .transport = std::nullopt,
                .diagnostics = std::move(resolved.diagnostics)};
    }
    return {.status = object_audition_status_t::prepared,
            .prepared = prepared_object_audition_t{request, std::move(resolved.clips)},
            .transport = std::nullopt,
            .diagnostics = {}};
}

auto object_audition_controller_t::start(prepared_object_audition_t prepared)
    -> object_audition_result_t {
    const auto transport = bridge_.start(prepared.clips);
    if (!transport) {
        return {.status = object_audition_status_t::transport_failed,
                .prepared = std::move(prepared),
                .transport = transport,
                .diagnostics = {}};
    }
    return {.status = object_audition_status_t::started,
            .prepared = std::move(prepared),
            .transport = transport,
            .diagnostics = {}};
}

auto object_audition_controller_t::start(const song_document_t& song,
                                         object_audition_request_t request,
                                         std::span<const audition_source_binding_t> bindings,
                                         audition_resolve_options_t options)
    -> object_audition_result_t {
    auto prepared = prepare(song, request, bindings, options);
    if (!prepared)
        return prepared;
    return start(std::move(*prepared.prepared));
}

auto object_audition_controller_t::stop() noexcept -> transport_stop_result_t {
    return bridge_.stop();
}

} // namespace wormedit::audio
