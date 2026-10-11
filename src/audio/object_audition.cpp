#include <wormedit/audio/object_audition.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <algorithm>

namespace wormedit::audio {

namespace {
auto is_stem_audition_resource(const song_document_t& song, resource_id_t id) -> bool {
    const auto resource = std::ranges::find(song.resources, id, &resource_entry_t::id);
    if (resource == song.resources.end()) return false;
    if (resource->sound_kind == sound_kind_t::stem) return true;
    for (const auto& chart : song.charts) {
        const auto bindings = bmson_channel_resource_bindings(song, chart.id);
        const auto binding = std::ranges::find(bindings, id, &bmson_channel_resource_binding_t::resource);
        if (binding == bindings.end() || binding->ambiguous || !binding->channel) continue;
        if (std::ranges::any_of(chart.bgm_source_lanes, [&](const auto& lane) {
            return lane.stem_channel == binding->channel;
        })) return true;
    }
    return false;
}
}

auto bound_stem_audition(const song_document_t& song, std::optional<resource_id_t> resource,
    std::span<const audition_source_binding_t> bindings, std::span<audition_clip_t> clips) -> bool {
    if (!resource || !is_stem_audition_resource(song, *resource)) return true;
    for (auto& clip : clips) {
        const auto binding = std::ranges::find(bindings, clip.source, &audition_source_binding_t::source);
        if (binding == bindings.end() || !binding->output_sample_rate || clip.source_start_frame > binding->output_frame_count ||
            clip.frame_count > binding->output_frame_count - clip.source_start_frame)
            return false;
        const auto maximum = static_cast<std::uint64_t>(binding->output_sample_rate) * 2;
        clip.frame_count = std::min(clip.frame_count, maximum);
    }
    return true;
}

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
