#include <wormedit/audio/audition_resolver.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <set>
#include <tuple>
#include <unordered_map>

namespace wormedit::audio {
namespace {

auto error(audition_resolve_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> audition_resolve_diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

struct semantic_sound_t {
    object_id_t id;
    resource_id_t resource;
    object_id_t linked_event;
    bool playable{};
    note_kind_t kind{note_kind_t::tap};
    local_position_t position;
    lane_id_t lane;
    std::optional<local_position_t> end;
};

auto output_frame(std::uint64_t source_frame, std::uint32_t source_rate, sample_rounding_t rounding)
    -> std::optional<std::uint64_t> {
    if (source_rate == 0 ||
        source_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return sample_frame_at({static_cast<std::int64_t>(source_frame), source_rate},
                           output_sample_rate, rounding)
        .frame;
}

} // namespace

auto resolve_object_audition(const song_document_t& song, chart_id_t chart_id, object_id_t object,
                             std::span<const audition_source_binding_t> bindings,
                             audition_resolve_options_t options) -> audition_resolve_result_t {
    audition_resolve_result_t result{.document_revision = song.revision,
                                     .chart = chart_id,
                                     .object = object,
                                     .clips = {},
                                     .diagnostics = {}};
    const auto chart_it = std::ranges::find(song.charts, chart_id, &chart_document_t::id);
    if (chart_it == song.charts.end()) {
        result.diagnostics.push_back(
            error(audition_resolve_code_t::missing_chart, "audition chart does not exist"));
        return result;
    }
    const auto& chart = *chart_it;

    std::optional<semantic_sound_t> selected;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.id != object)
                continue;
            if (!note.sound.has_value()) {
                result.diagnostics.push_back(error(audition_resolve_code_t::missing_resource,
                                                   "selected note has no sound resource", object));
                return result;
            }
            selected = semantic_sound_t{
                note.id,   *note.sound, note.sound_channel_event.value_or(object_id_t{}),
                true,      note.kind,   note.position,
                note.lane, note.end};
        }
        for (const auto& event : measure.bgm_events) {
            if (event.id != object)
                continue;
            if (!event.sound.has_value()) {
                result.diagnostics.push_back(error(audition_resolve_code_t::missing_resource,
                                                   "selected BGM event has no sound resource",
                                                   object));
                return result;
            }
            selected = semantic_sound_t{
                event.id,    *event.sound,     event.sound_channel_event.value_or(object_id_t{}),
                false,       note_kind_t::tap, event.position,
                lane_id_t{}, std::nullopt};
        }
    }
    if (!selected.has_value()) {
        result.diagnostics.push_back(error(audition_resolve_code_t::missing_object,
                                           "audition object is not a note or BGM event", object));
        return result;
    }

    std::unordered_map<std::uint64_t, const resource_entry_t*> resources;
    for (const auto& resource : song.resources)
        resources.emplace(resource.id.value, &resource);
    std::unordered_map<std::uint64_t, const audio_asset_t*> assets;
    for (const auto& asset : song.audio_assets)
        assets.emplace(asset.id.value, &asset);
    std::unordered_map<std::uint64_t, const audition_source_binding_t*> sources;
    std::set<std::uint64_t> duplicate_sources;
    for (const auto& binding : bindings) {
        if (!sources.emplace(binding.asset.value, &binding).second) {
            duplicate_sources.insert(binding.asset.value);
        }
    }

    std::vector<semantic_sound_t> layers{*selected};
    if (selected->playable && selected->linked_event) {
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                if (note.id == object || !note.sound || !note.sound_channel_event ||
                    note.kind != selected->kind || note.position != selected->position ||
                    note.lane != selected->lane || note.end != selected->end) {
                    continue;
                }
                layers.push_back({note.id, *note.sound, *note.sound_channel_event, true, note.kind,
                                  note.position, note.lane, note.end});
            }
        }
    }
    if (layers.size() > options.maximum_layers) {
        result.diagnostics.push_back(error(audition_resolve_code_t::capacity_exceeded,
                                           "audition exceeds the configured layer limit", object));
        return result;
    }
    const auto retained_order = [&](const semantic_sound_t& layer) {
        for (std::size_t channel_index = 0; channel_index < chart.sound_channels.size();
             ++channel_index) {
            const auto& channel = chart.sound_channels[channel_index];
            const auto found =
                std::ranges::find(channel.events, layer.linked_event, &sound_channel_event_t::id);
            if (found != channel.events.end()) {
                return std::tuple{channel_index, found->provenance.source_order, found->id.value};
            }
        }
        return std::tuple{std::numeric_limits<std::size_t>::max(),
                          std::numeric_limits<std::size_t>::max(), layer.id.value};
    };
    std::ranges::stable_sort(layers, [&](const auto& left, const auto& right) {
        return retained_order(left) < retained_order(right);
    });

    std::set<std::pair<std::uint64_t, std::uint64_t>> emitted_slices;
    for (const auto& layer : layers) {
        const auto resource = resources.find(layer.resource.value);
        if (resource == resources.end()) {
            result.diagnostics.push_back(error(audition_resolve_code_t::missing_resource,
                                               "audition sound resource does not exist", layer.id));
            result.clips.clear();
            return result;
        }
        const auto asset = assets.find(resource->second->asset.value);
        const auto source = sources.find(resource->second->asset.value);
        if (duplicate_sources.contains(resource->second->asset.value)) {
            result.diagnostics.push_back(
                error(audition_resolve_code_t::duplicate_source_binding,
                      "audio asset has more than one runtime audition binding", layer.id));
            result.clips.clear();
            return result;
        }
        if (asset == assets.end() || source == sources.end() ||
            source->second->source.asset_identity.empty() ||
            source->second->source.source_identity.empty()) {
            result.diagnostics.push_back(error(audition_resolve_code_t::missing_source_binding,
                                               "audio asset has no decoded audition binding",
                                               layer.id));
            result.clips.clear();
            return result;
        }
        if (source->second->source_sample_rate == 0 ||
            source->second->output_sample_rate != output_sample_rate ||
            source->second->output_frame_count == 0) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_source_geometry,
                                               "decoded audition binding has invalid geometry",
                                               layer.id));
            result.clips.clear();
            return result;
        }

        if (!layer.linked_event) {
            result.clips.push_back(
                {source->second->source, 0, source->second->output_frame_count, 1.0F});
            continue;
        }

        const sound_channel_t* owner{};
        const sound_channel_event_t* retained{};
        for (const auto& channel : chart.sound_channels) {
            const auto found =
                std::ranges::find(channel.events, layer.linked_event, &sound_channel_event_t::id);
            if (found == channel.events.end())
                continue;
            if (owner != nullptr) {
                result.diagnostics.push_back(error(audition_resolve_code_t::invalid_link,
                                                   "audition sound link is ambiguous", layer.id));
                result.clips.clear();
                return result;
            }
            owner = &channel;
            retained = &*found;
        }
        if (owner == nullptr || retained->playable != layer.playable ||
            owner->source_clip.source != resource->second->asset) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_link,
                                               "audition sound link is stale or mismatched",
                                               layer.id));
            result.clips.clear();
            return result;
        }
        if (asset->second->stream.sample_rate != 0 &&
            asset->second->stream.sample_rate != source->second->source_sample_rate) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_source_geometry,
                                               "stored and decoded source sample rates disagree",
                                               layer.id));
            result.clips.clear();
            return result;
        }
        if (asset->second->stream.frame_count.has_value() &&
            source->second->source_frame_count.has_value() &&
            asset->second->stream.frame_count != source->second->source_frame_count) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_source_geometry,
                                               "stored and decoded source frame counts disagree",
                                               layer.id));
            result.clips.clear();
            return result;
        }
        const auto slices = derive_sound_channel_slices(
            chart, *owner, source->second->source_sample_rate, source->second->source_frame_count,
            {.maximum_events = options.maximum_channel_events, .rounding = options.rounding});
        const auto* slice = slices ? find_sound_slice(slices, retained->id) : nullptr;
        if (slice == nullptr) {
            auto code = audition_resolve_code_t::invalid_timing;
            if (!slices.diagnostics.empty() &&
                slices.diagnostics.front().code == sound_slice_error_code_t::capacity_exceeded) {
                code = audition_resolve_code_t::capacity_exceeded;
            } else if (!slices.diagnostics.empty() &&
                       (slices.diagnostics.front().code ==
                            sound_slice_error_code_t::invalid_sample_rate ||
                        slices.diagnostics.front().code ==
                            sound_slice_error_code_t::invalid_source_range)) {
                code = audition_resolve_code_t::invalid_source_geometry;
            }
            result.diagnostics.push_back(
                error(code, "linked audition slice could not be derived", layer.id));
            result.clips.clear();
            return result;
        }
        if (!emitted_slices.emplace(owner->id.value, slice->absolute_row).second)
            continue;
        const auto begin = output_frame(slice->source_begin_frame,
                                        source->second->source_sample_rate, options.rounding);
        auto end = slice->source_end_frame
                       ? output_frame(*slice->source_end_frame, source->second->source_sample_rate,
                                      options.rounding)
                       : std::optional<std::uint64_t>{source->second->output_frame_count};
        if (end.has_value())
            end = std::min(*end, source->second->output_frame_count);
        if (!begin || !end || *begin >= *end) {
            result.diagnostics.push_back(error(audition_resolve_code_t::empty_range,
                                               "audition slice has no decoded output frames",
                                               layer.id));
            result.clips.clear();
            return result;
        }
        result.clips.push_back({source->second->source, *begin, *end - *begin, 1.0F});
    }
    return result;
}

} // namespace wormedit::audio
