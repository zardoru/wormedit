#include <wormedit/audio/object_audition.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <algorithm>

namespace wormedit::audio {

namespace {
auto is_generated_hitsound_clip(const audition_clip_t& clip,
    std::span<const hitsound_binding_t> hitsounds) -> bool {
    for (const auto& binding : hitsounds) {
        for (const auto& voice : binding.binding.voices) {
            if (voice.source != clip.source || clip.source_start_frame < voice.source_start_frame) continue;
            const auto offset = clip.source_start_frame - voice.source_start_frame;
            if (offset <= voice.frame_count && clip.frame_count <= voice.frame_count - offset) return true;
        }
    }
    return false;
}

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

auto is_bound_stem_audition(const song_document_t& song, std::optional<resource_id_t> resource,
    std::span<const audition_source_binding_t> bindings, std::span<audition_clip_t> clips,
    std::span<const hitsound_binding_t> hitsounds) -> bool {
    if (!resource || !is_stem_audition_resource(song, *resource)) return true;
    for (auto& clip : clips) {
        if (is_generated_hitsound_clip(clip, hitsounds)) continue;
        const auto binding = std::ranges::find(bindings, clip.source, &audition_source_binding_t::source);
        if (binding == bindings.end() || !binding->output_sample_rate || clip.source_start_frame > binding->output_frame_count ||
            clip.frame_count > binding->output_frame_count - clip.source_start_frame)
            return false;
        const auto maximum = static_cast<std::uint64_t>(binding->output_sample_rate) * 2;
        clip.frame_count = std::min(clip.frame_count, maximum);
    }
    return true;
}

auto object_audition_controller_t::prepare_object(
    const song_document_t& song, object_audition_request_t request,
    std::span<const audition_source_binding_t> bindings, audition_resolve_options_t options)
    -> object_audition_outcome_t {
    if (request.document_revision != song.revision) {
        return std::unexpected(
            diagnosed_t<object_audition_effect_t, std::vector<audition_resolve_diagnostic_t>>{
                .value = {.status = failure_code_t::stale_generation,
                          .prepared = std::nullopt,
                          .transport = std::nullopt},
                .diagnostics = {}});
    }
    auto resolved = audition_resolution_t::from(song, request.chart, request.object, bindings, options);
    if (resolved.value.clips.empty() || !resolved.diagnostics.empty()) {
        return std::unexpected(
            diagnosed_t<object_audition_effect_t, std::vector<audition_resolve_diagnostic_t>>{
                .value = {.status = failure_code_t::resolve_failed,
                          .prepared = std::nullopt,
                          .transport = std::nullopt},
                .diagnostics = std::move(resolved.diagnostics)});
    }
    return object_audition_effect_t{
        .status = object_audition_phase_t::prepared,
        .prepared = prepared_object_audition_t{request, std::move(resolved.value.clips)},
        .transport = std::nullopt};
}

auto object_audition_controller_t::start_prepared(prepared_object_audition_t prepared)
    -> object_audition_outcome_t {
    const auto transport = bridge_.start_clips(prepared.clips);
    if (!transport) {
        return std::unexpected(
            diagnosed_t<object_audition_effect_t, std::vector<audition_resolve_diagnostic_t>>{
                .value = {.status = failure_code_t::transport_failed,
                          .prepared = std::move(prepared),
                          .transport = transport},
                .diagnostics = {}});
    }
    return object_audition_effect_t{.status = object_audition_phase_t::started,
                                    .prepared = std::move(prepared),
                                    .transport = transport};
}

auto object_audition_controller_t::start_object(const song_document_t& song,
                                                object_audition_request_t request,
                                                std::span<const audition_source_binding_t> bindings,
                                                audition_resolve_options_t options)
    -> object_audition_outcome_t {
    auto prepared = prepare_object(song, request, bindings, options);
    if (!prepared)
        return prepared;
    return start_prepared(std::move(*prepared->prepared));
}

auto object_audition_controller_t::stop() noexcept -> transport_stop_result_t {
    return bridge_.stop();
}

} // namespace wormedit::audio
