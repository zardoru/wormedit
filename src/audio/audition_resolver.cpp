#include <wormedit/audio/audition_resolver.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
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

// Why a linked slice is missing: the first derivation diagnostic decides the code and the message.
auto slice_derivation_error(const sound_slice_result_t& derived, object_id_t layer) -> audition_resolve_diagnostic_t {
    auto code = audition_resolve_code_t::invalid_timing;
    std::string message = "linked audition slice could not be derived";
    if (!derived.diagnostics.empty()) {
        const auto& first = derived.diagnostics.front();
        message += ": " + first.message;
        if (first.code == sound_slice_error_code_t::capacity_exceeded)
            code = audition_resolve_code_t::capacity_exceeded;
        else if (first.code == sound_slice_error_code_t::invalid_sample_rate ||
                 first.code == sound_slice_error_code_t::invalid_source_range)
            code = audition_resolve_code_t::invalid_source_geometry;
    }
    return error(code, std::move(message), layer);
}

auto is_unavailable_preview_source(const audition_resolve_result_t& resolved,
                                audition_resolve_options_t options) -> bool {
    return options.skip_unavailable_preview_sources &&
           resolved.diagnostics.size() == 1 &&
           resolved.diagnostics.front().code == audition_resolve_code_t::missing_source_binding;
}

template <typename Events>
auto collect_preview_events(const Events& events,
                            std::map<object_id_t, resource_id_t>& scheduled,
                            preview_bindings_resolve_outcome_t& result,
                            std::size_t maximum_objects) -> bool {
    for (const auto& event : events) {
        const auto [position, inserted] = scheduled.emplace(event.object, event.resource);
        if (!inserted && position->second != event.resource) {
            result.diagnostics.push_back(error(
                audition_resolve_code_t::invalid_link,
                "preview schedule contains conflicting resources for one object", event.object));
            return false;
        }
        if (scheduled.size() > maximum_objects) {
            result.diagnostics.push_back(error(
                audition_resolve_code_t::capacity_exceeded,
                "preview schedule exceeds the configured audition object limit"));
            return false;
        }
    }
    return true;
}

struct semantic_sound_t {
    object_id_t id;
    std::optional<resource_id_t> resource;
    object_id_t linked_event;
    bool playable{};
    note_kind_t kind{note_kind_t::tap};
    local_position_t position;
    lane_id_t lane;
    std::optional<local_position_t> end;
    bool has_hitsample{};
};

using fused_group_key_t = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint8_t,
                                     bool, std::uint64_t, std::uint64_t>;

auto fused_group_key(const semantic_sound_t& sound) -> fused_group_key_t {
    return {sound.position.measure.value,
            sound.position.row,
            sound.lane.value,
            static_cast<std::uint8_t>(sound.kind),
            sound.end.has_value(),
            sound.end ? sound.end->measure.value : 0,
            sound.end ? sound.end->row : 0};
}

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

struct retained_event_ref_t {
    std::size_t channel_index{};
    const sound_channel_event_t* event{};
    bool ambiguous{};
};

struct channel_slice_cache_t {
    sound_slice_result_t result;
    std::unordered_map<std::uint64_t, std::size_t> slice_by_event;
};

class audition_resolver_index_t {
  public:
    audition_resolver_index_t(const song_document_t& song, const chart_document_t& chart,
                              std::span<const audition_source_binding_t> bindings,
                              audition_resolve_options_t options)
        : song_(song), chart_(chart), options_(options), slice_cache_(chart.sound_channels.size()) {
        for (const auto& resource : song.resources)
            resources_.emplace(resource.id.value, &resource);
        for (const auto& asset : song.audio_assets)
            assets_.emplace(asset.id.value, &asset);
        for (const auto& binding : bindings) {
            if (!sources_.emplace(binding.asset.value, &binding).second)
                duplicate_sources_.insert(binding.asset.value);
        }
        for (std::size_t channel_index = 0; channel_index < chart.sound_channels.size();
             ++channel_index) {
            for (const auto& event : chart.sound_channels[channel_index].events) {
                const auto [position, inserted] = retained_events_.emplace(
                    event.id.value, retained_event_ref_t{channel_index, &event, false});
                if (!inserted)
                    position->second.ambiguous = true;
            }
        }
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                add_semantic({note.id, note.sound, note.sound_channel_event.value_or(object_id_t{}),
                              true, note.kind, note.position, note.lane, note.end, note.osu_hitsample.has_value()});
            }
            for (const auto& event : measure.bgm_events) {
                add_semantic({event.id, event.sound,
                              event.sound_channel_event.value_or(object_id_t{}), false,
                              note_kind_t::tap, event.position, lane_id_t{}, std::nullopt, false});
            }
        }
    }

    [[nodiscard]] auto semantic_count() const noexcept -> std::size_t {
        return semantics_.size();
    }

    [[nodiscard]] auto resource_for(object_id_t object) const -> std::optional<resource_id_t> {
        const auto found = semantic_by_id_.find(object.value);
        if (found == semantic_by_id_.end())
            return std::nullopt;
        return semantics_[found->second].resource;
    }
    [[nodiscard]] auto has_hitsample(object_id_t object) const -> bool {
        const auto found = semantic_by_id_.find(object.value);
        return found != semantic_by_id_.end() && semantics_[found->second].has_hitsample;
    }

    [[nodiscard]] auto resolve(object_id_t object) -> audition_resolve_result_t {
        auto result = audition_resolve_result_t{.document_revision = song_.revision,
                                                .chart = chart_.id,
                                                .object = object,
                                                .clips = {},
                                                .diagnostics = {}};
        const auto selected_it = semantic_by_id_.find(object.value);
        if (selected_it == semantic_by_id_.end()) {
            result.diagnostics.push_back(error(audition_resolve_code_t::missing_object,
                                               "audition object is not a note or BGM event",
                                               object));
            return result;
        }
        const auto& selected = semantics_[selected_it->second];
        if (!selected.resource.has_value()) {
            result.diagnostics.push_back(error(audition_resolve_code_t::missing_resource,
                                               selected.playable
                                                   ? "selected note has no sound resource"
                                                   : "selected BGM event has no sound resource",
                                               object));
            return result;
        }

        std::vector<std::size_t> layers{selected_it->second};
        if (selected.playable && selected.linked_event) {
            const auto group = fused_groups_.find(fused_group_key(selected));
            if (group != fused_groups_.end())
                layers = group->second;
        }
        if (layers.size() > options_.maximum_layers) {
            result.diagnostics.push_back(error(audition_resolve_code_t::capacity_exceeded,
                                               "audition exceeds the configured layer limit",
                                               object));
            return result;
        }
        std::ranges::stable_sort(layers, [&](std::size_t left, std::size_t right) {
            return retained_order(semantics_[left]) < retained_order(semantics_[right]);
        });

        std::set<std::pair<std::uint64_t, std::uint64_t>> emitted_slices;
        for (const auto layer_index : layers) {
            if (!append_layer(semantics_[layer_index], emitted_slices, result)) {
                result.clips.clear();
                return result;
            }
        }
        return result;
    }

  private:
    struct layer_source_t {
        asset_id_t asset_id;
        const audio_asset_t* asset;
        const audition_source_binding_t* binding;
    };

    [[nodiscard]] auto validated_layer_source(const semantic_sound_t& layer,
                                               audition_resolve_result_t& result) const
        -> std::optional<layer_source_t> {
        const auto resource = resources_.find(layer.resource->value);
        if (resource == resources_.end()) {
            result.diagnostics.push_back(error(audition_resolve_code_t::missing_resource,
                                               "audition sound resource does not exist", layer.id));
            return std::nullopt;
        }
        const auto asset_id = resource->second->asset;
        const auto asset = assets_.find(asset_id.value);
        const auto source = sources_.find(asset_id.value);
        if (duplicate_sources_.contains(asset_id.value)) {
            result.diagnostics.push_back(error(
                audition_resolve_code_t::duplicate_source_binding,
                "audio asset has more than one runtime audition binding", layer.id));
            return std::nullopt;
        }
        if (asset == assets_.end() || source == sources_.end() ||
            source->second->source.asset_identity.empty() ||
            source->second->source.source_identity.empty()) {
            result.diagnostics.push_back(error(audition_resolve_code_t::missing_source_binding,
                                               "audio asset has no decoded audition binding",
                                               layer.id));
            return std::nullopt;
        }
        const auto& binding = *source->second;
        if (binding.source_sample_rate == 0 || binding.output_sample_rate != output_sample_rate ||
            binding.output_frame_count == 0) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_source_geometry,
                                               "decoded audition binding has invalid geometry",
                                               layer.id));
            return std::nullopt;
        }
        return layer_source_t{asset_id, asset->second, &binding};
    }

    auto cached_channel_slices(std::size_t channel_index, const sound_channel_t& owner,
                               const audition_source_binding_t& binding)
        -> channel_slice_cache_t& {
        auto& cached = slice_cache_[channel_index];
        if (!cached.has_value()) {
            channel_slice_cache_t built;
            built.result = derive_sound_channel_slices(
                chart_, owner, binding.source_sample_rate, binding.source_frame_count,
                {.maximum_events = options_.maximum_channel_events, .rounding = options_.rounding});
            for (std::size_t slice_index = 0; slice_index < built.result.slices.size();
                 ++slice_index) {
                for (const auto event : built.result.slices[slice_index].events) {
                    built.slice_by_event.emplace(event.value, slice_index);
                }
            }
            cached = std::move(built);
        }
        return *cached;
    }

    void add_semantic(semantic_sound_t sound) {
        const auto index = semantics_.size();
        semantic_by_id_.emplace(sound.id.value, index);
        semantics_.push_back(std::move(sound));
        const auto& stored = semantics_.back();
        if (stored.playable && stored.resource.has_value() && stored.linked_event)
            fused_groups_[fused_group_key(stored)].push_back(index);
    }

    [[nodiscard]] auto retained_order(const semantic_sound_t& layer) const
        -> std::tuple<std::size_t, std::size_t, std::uint64_t> {
        const auto retained = retained_events_.find(layer.linked_event.value);
        if (retained == retained_events_.end() || retained->second.ambiguous) {
            return std::tuple{std::numeric_limits<std::size_t>::max(),
                              std::numeric_limits<std::size_t>::max(), layer.id.value};
        }
        return std::tuple{retained->second.channel_index,
                          retained->second.event->provenance.source_order,
                          retained->second.event->id.value};
    }

    [[nodiscard]] auto
    append_layer(const semantic_sound_t& layer,
                 std::set<std::pair<std::uint64_t, std::uint64_t>>& emitted_slices,
                 audition_resolve_result_t& result) -> bool {
        const auto source = validated_layer_source(layer, result);
        if (!source.has_value()) {
            return false;
        }
        const auto asset_id = source->asset_id;
        const auto& binding = *source->binding;
        if (!layer.linked_event) {
            result.clips.push_back({binding.source, 0, binding.output_frame_count, 1.0F});
            return true;
        }

        const auto retained = retained_events_.find(layer.linked_event.value);
        if (retained == retained_events_.end() || retained->second.ambiguous ||
            retained->second.event->playable != layer.playable) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_link,
                                               "audition sound link is stale or ambiguous",
                                               layer.id));
            return false;
        }
        const auto& owner = chart_.sound_channels[retained->second.channel_index];
        if (owner.source_clip.source != asset_id) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_link,
                                               "audition sound link is stale or mismatched",
                                               layer.id));
            return false;
        }
        if (source->asset->stream.sample_rate != 0 &&
            source->asset->stream.sample_rate != binding.source_sample_rate) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_source_geometry,
                                               "stored and decoded source sample rates disagree",
                                               layer.id));
            return false;
        }
        if (source->asset->stream.frame_count.has_value() &&
            binding.source_frame_count.has_value() &&
            source->asset->stream.frame_count != binding.source_frame_count) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_source_geometry,
                                               "stored and decoded source frame counts disagree",
                                               layer.id));
            return false;
        }

        const auto& cached = cached_channel_slices(retained->second.channel_index, owner, binding);
        const auto slice_position = cached.slice_by_event.find(retained->second.event->id.value);
        const auto* slice = slice_position == cached.slice_by_event.end()
                                ? nullptr
                                : &cached.result.slices[slice_position->second];
        if (slice == nullptr) {
            result.diagnostics.push_back(slice_derivation_error(cached.result, layer.id));
            return false;
        }
        if (!emitted_slices.emplace(owner.id.value, slice->absolute_row).second)
            return true;
        const auto begin =
            output_frame(slice->source_begin_frame, binding.source_sample_rate, options_.rounding);
        auto end = slice->source_end_frame
                       ? output_frame(*slice->source_end_frame, binding.source_sample_rate,
                                      options_.rounding)
                       : std::optional<std::uint64_t>{binding.output_frame_count};
        if (end.has_value())
            end = std::min(*end, binding.output_frame_count);
        // A point past the end of its sample plays nothing; that is not an error.
        if (slice->source_end_frame && slice->source_begin_frame >= *slice->source_end_frame) return true;
        if (!begin || !end || *begin >= *end) {
            result.diagnostics.push_back(error(audition_resolve_code_t::empty_range,
                                               "audition slice has no decoded output frames",
                                               layer.id));
            return false;
        }
        result.clips.push_back({binding.source, *begin, *end - *begin, 1.0F});
        return true;
    }

    const song_document_t& song_;
    const chart_document_t& chart_;
    audition_resolve_options_t options_;
    std::unordered_map<std::uint64_t, const resource_entry_t*> resources_;
    std::unordered_map<std::uint64_t, const audio_asset_t*> assets_;
    std::unordered_map<std::uint64_t, const audition_source_binding_t*> sources_;
    std::set<std::uint64_t> duplicate_sources_;
    std::unordered_map<std::uint64_t, retained_event_ref_t> retained_events_;
    std::vector<semantic_sound_t> semantics_;
    std::unordered_map<std::uint64_t, std::size_t> semantic_by_id_;
    std::map<fused_group_key_t, std::vector<std::size_t>> fused_groups_;
    std::vector<std::optional<channel_slice_cache_t>> slice_cache_;
};

auto reference_sound_event(const chart_document_t& chart, object_id_t reference_bgm_event)
    -> std::optional<object_id_t> {
    for (const auto& measure : chart.measures) {
        const auto reference =
            std::ranges::find(measure.bgm_events, reference_bgm_event, &bgm_event_t::id);
        if (reference != measure.bgm_events.end() && reference->sound_channel_event) {
            return reference->sound_channel_event;
        }
    }
    return std::nullopt;
}

auto transient_audition_event(const chart_document_t& chart, local_position_t target,
                              object_id_t source_event) -> object_id_t {
    object_id_t result{};
    for (const auto& measure : chart.measures) {
        if (measure.id != target.measure) {
            continue;
        }
        for (const auto& note : measure.notes) {
            if (note.sound_channel_event == source_event) {
                result = note.id;
                break;
            }
        }
        for (const auto& event : measure.bgm_events) {
            if (event.sound_channel_event == source_event) {
                result = event.id;
                break;
            }
        }
        break;
    }
    return result;
}

struct preview_binding_builder_t {
    audition_resolver_index_t& index;
    const audition_resolve_options_t& options;
    preview_bindings_resolve_outcome_t& result;
    std::unordered_map<std::uint64_t, const hitsound_binding_t*> hitsounds;
    std::size_t total_clips{};

    auto append_sources(object_id_t object, resource_id_t resource,
                         preview_object_source_binding_t& output) -> bool {
        if (!resource) return true;
        auto resolved = index.resolve(object);
        if (resolved.clips.empty() && resolved.diagnostics.empty()) return true;
        if (!resolved) {
            if (is_unavailable_preview_source(resolved, options)) {
                ++result.value.skipped_unavailable_objects;
                return true;
            }
            result.diagnostics.insert(result.diagnostics.end(),
                std::make_move_iterator(resolved.diagnostics.begin()),
                std::make_move_iterator(resolved.diagnostics.end()));
            return false;
        }
        for (auto& clip : resolved.clips)
            output.voices.push_back({.source = std::move(clip.source),
                .source_start_frame = clip.source_start_frame, .frame_count = clip.frame_count, .gain = clip.gain});
        return true;
    }

    auto append(object_id_t object, resource_id_t resource) -> bool {
        const auto semantic = index.resource_for(object);
        if ((!resource && !index.has_hitsample(object)) ||
            (resource && (!semantic || *semantic != resource))) {
            result.diagnostics.push_back(error(audition_resolve_code_t::invalid_link,
                "preview schedule object is stale or mismatched", object));
            return false;
        }
        preview_object_source_binding_t output{.object = object};
        if (!append_sources(object, resource, output)) return false;
        if (const auto extra = hitsounds.find(object.value); extra != hitsounds.end()) {
            output.voices.insert(output.voices.end(), extra->second->binding.voices.begin(),
                                  extra->second->binding.voices.end());
            for (auto& voice : output.voices) voice.gain *= extra->second->gain;
        }
        if (output.voices.size() > options.maximum_total_clips - total_clips) {
            result.diagnostics.push_back(error(audition_resolve_code_t::capacity_exceeded,
                "preview schedule exceeds the configured audition clip limit", object));
            return false;
        }
        total_clips += output.voices.size();
        if (!output.voices.empty()) result.value.bindings.push_back(std::move(output));
        return true;
    }
};

} // namespace

auto audition_resolution_t::from(audition_resolve_result_t&& report) -> audition_resolve_outcome_t {
    return {.value = {.document_revision = report.document_revision,
                      .chart = report.chart, .object = report.object,
                      .clips = std::move(report.clips)},
            .diagnostics = std::move(report.diagnostics)};
}

auto audition_resolve_result_t::from(audition_resolve_outcome_t&& outcome) -> audition_resolve_result_t {
    return {.document_revision = outcome.value.document_revision,
            .chart = outcome.value.chart, .object = outcome.value.object,
            .clips = std::move(outcome.value.clips), .diagnostics = std::move(outcome.diagnostics)};
}

auto audition_resolution_t::from(const song_document_t& song, chart_id_t chart_id, object_id_t object,
                             std::span<const audition_source_binding_t> bindings,
                             audition_resolve_options_t options) -> audition_resolve_outcome_t {
    const auto* chart = song.find_chart(chart_id);
    if (chart == nullptr) {
        audition_resolve_outcome_t result{
            .value = {.document_revision = song.revision,
                      .chart = chart_id, .object = object, .clips = {}},
            .diagnostics = {}};
        result.diagnostics.push_back(
            error(audition_resolve_code_t::missing_chart, "audition chart does not exist"));
        return result;
    }
    audition_resolver_index_t index{song, *chart, bindings, options};
    return from(index.resolve(object));
}

void preview_bindings_resolution_t::append_music(resource_id_t resource,
    const decoded_source_binding_t& source, preview_schedule_t& schedule) {
    schedule.music = preview_schedule_t::music_event_t::from(resource, source.output_frame_count);
    if (!schedule.music) return;
    bindings.push_back({.object = preview_music_object,
        .voices = {{.source = source.source, .frame_count = source.output_frame_count}}});
}

auto preview_bindings_resolution_t::from(const song_document_t& song, chart_id_t chart_id,
                              const preview_schedule_t& schedule,
                              std::span<const audition_source_binding_t> bindings,
                              audition_resolve_options_t options)
    -> preview_bindings_resolve_outcome_t {
    preview_bindings_resolve_outcome_t result{
        .value = {.document_revision = song.revision, .chart = chart_id, .bindings = {}},
        .diagnostics = {}};
    const auto* chart = song.find_chart(chart_id);
    if (chart == nullptr) {
        result.diagnostics.push_back(
            error(audition_resolve_code_t::missing_chart, "audition chart does not exist"));
        return result;
    }
    audition_resolver_index_t index{song, *chart, bindings, options};
    if (index.semantic_count() > options.maximum_objects) {
        result.diagnostics.push_back(error(audition_resolve_code_t::capacity_exceeded,
                                           "chart exceeds the configured audition object limit"));
        return result;
    }

    std::map<object_id_t, resource_id_t> scheduled;
    if (!collect_preview_events(schedule.playable, scheduled, result, options.maximum_objects) ||
        !collect_preview_events(schedule.bgm, scheduled, result, options.maximum_objects))
        return result;

    result.value.bindings.reserve(scheduled.size());
    preview_binding_builder_t builder{.index = index, .options = options, .result = result};
    for (const auto& binding : options.hitsounds)
        builder.hitsounds.emplace(binding.binding.object.value, &binding);
    for (const auto& [object, scheduled_resource] : scheduled) {
        if (!builder.append(object, scheduled_resource)) {
            result.value.bindings.clear();
            return result;
        }
    }
    return result;
}

auto resolve_object_audition(const song_document_t& song, chart_id_t chart, object_id_t object,
                            std::span<const audition_source_binding_t> bindings,
                            audition_resolve_options_t options) -> audition_resolve_result_t {
    return audition_resolve_result_t::from(audition_resolution_t::from(song, chart, object, bindings, options));
}

auto resolve_bmson_draft_audition(const song_document_t& song,
                                  bmson_draft_audition_request_t request,
                                  std::span<const audition_source_binding_t> bindings,
                                  audition_resolve_options_t options) -> audition_resolve_result_t {
    const auto chart_id = request.chart;
    const auto reference_bgm_event = request.reference_bgm_event;
    const auto markers = request.markers;
    const auto marker_index = request.marker_index;
    audition_resolve_result_t result{
        .document_revision = song.revision, .chart = chart_id, .object = {},
        .clips = {}, .diagnostics = {}};
    if (marker_index >= markers.size()) {
        result.diagnostics.push_back(error(
            audition_resolve_code_t::invalid_link,
            "BMSON draft audition has a stale revision or unknown marker"));
        return result;
    }
    if (!document_stamp_t{.project = song.id, .revision = request.expected_revision}
             .is_current(song)) {
        result.diagnostics.push_back(error(audition_resolve_code_t::invalid_link,
                                           stale_action_message("BMSON draft audition")));
        return result;
    }
    const auto* original = song.find_chart(chart_id);
    if (!original || original->source_family != format_family_t::bmson) {
        result.diagnostics.push_back(error(
            audition_resolve_code_t::missing_chart,
            "BMSON draft audition requires a known BMSON chart"));
        return result;
    }
    const auto reference_source = reference_sound_event(*original, reference_bgm_event);
    if (!reference_source) {
        result.diagnostics.push_back(error(
            audition_resolve_code_t::invalid_link,
            "BMSON draft audition reference has no retained sound-channel event",
            reference_bgm_event));
        return result;
    }
    const auto source_channel =
        std::ranges::find_if(original->sound_channels, [&](const auto& channel) {
            return std::ranges::any_of(
                channel.events, [&](const auto& event) { return event.id == *reference_source; });
        });
    if (source_channel == original->sound_channels.end()) {
        result.diagnostics.push_back(
            error(audition_resolve_code_t::invalid_link,
                  "BMSON draft audition reference source channel is unavailable"));
        return result;
    }
    const auto channel_index =
        static_cast<std::size_t>(source_channel - original->sound_channels.begin());

    auto temporary = document_history_t::from(song, 1);
    const auto inserted = insert_bmson_sound_markers(temporary,
                                                     chart_id, reference_bgm_event, markers);
    if (!inserted) {
        const auto message = inserted.diagnostics.empty()
            ? "BMSON draft markers cannot be materialized for audition"
            : inserted.diagnostics.front().message;
        result.diagnostics.push_back(error(
            audition_resolve_code_t::invalid_link, message, reference_bgm_event));
        return result;
    }
    const auto* transient_chart = temporary.document.find_chart(chart_id);
    if (!transient_chart) {
        result.diagnostics.push_back(error(
            audition_resolve_code_t::missing_chart,
            "BMSON draft chart disappeared during audition preparation"));
        return result;
    }
    const auto target = markers[marker_index].position;
    const auto& transient_channel = transient_chart->sound_channels[channel_index];
    const auto source = std::ranges::find_if(
        transient_channel.events, [&](const auto& event) { return event.position == target; });
    if (source == transient_channel.events.end()) {
        result.diagnostics.push_back(
            error(audition_resolve_code_t::invalid_link,
                  "BMSON draft marker has no retained transient sound event"));
        return result;
    }
    const auto transient_event = transient_audition_event(*transient_chart, target, source->id);
    if (!transient_event) {
        result.diagnostics.push_back(
            error(audition_resolve_code_t::invalid_link,
                  "BMSON draft marker has no linked transient note or BGM event"));
        return result;
    }
    result = resolve_object_audition(temporary.document, chart_id, transient_event,
                                     bindings, options);
    result.document_revision = song.revision;
    return result;
}

} // namespace wormedit::audio
