#include "../core/text_util.h"
#include <wormedit/formats/slice_spans.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace wormedit {
namespace {

auto normalized_frame(std::uint64_t frame, std::uint32_t rate) -> std::optional<std::uint64_t> {
    if (rate == 0) return std::nullopt;
    const auto whole = frame / rate;
    const auto remainder = frame % rate * slice_span_sample_rate;
    if (whole > std::numeric_limits<std::uint64_t>::max() / slice_span_sample_rate) return std::nullopt;
    const auto base = whole * slice_span_sample_rate;
    const auto fraction = remainder / rate;
    const auto residual = remainder % rate;
    const auto rounded = fraction + (residual * 2 > rate || (residual * 2 == rate && (base + fraction) % 2 != 0));
    if (rounded > std::numeric_limits<std::uint64_t>::max() - base) return std::nullopt;
    return base + rounded;
}

using text_util::ascii_lowercase;

auto filename_stem(const std::filesystem::path& path, asset_id_t asset) -> std::string {
    std::string stem;
    for (const auto byte : path.stem().string()) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9'))
            stem.push_back(byte);
        else if (!stem.empty() && stem.back() != '_') stem.push_back('_');
        if (stem.size() == 80) break;
    }
    if (stem.empty()) stem = "asset" + std::to_string(asset.value);
    return stem;
}

struct channel_binding_t {
    asset_id_t asset;
    std::optional<std::size_t> span_index;
};

struct plan_context_t {
    const song_document_t& song;
    const slice_span_options_t& options;
    slice_span_plan_outcome_t& result;
    std::map<std::uint64_t, const slice_span_source_t*> sources;
    std::map<std::uint64_t, const audio_asset_t*> assets;
    std::map<std::uint64_t, asset_id_t> resources;
    std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>, std::size_t> spans;
    std::map<std::uint64_t, channel_binding_t> channel_events;
    std::set<std::pair<slice_span_event_kind_t, std::uint64_t>> events;
};

auto fail(plan_context_t& context, std::string message, std::optional<asset_id_t> asset = {},
          std::optional<object_id_t> event = {}) -> bool {
    context.result.diagnostics.push_back({std::move(message), asset, event});
    return false;
}

auto index_sources(plan_context_t& context, std::span<const slice_span_source_t> sources) -> bool {
    for (const auto& source : sources) {
        const auto expected = normalized_frame(source.native_frame_count, source.native_sample_rate);
        const auto low = expected && *expected != 0 ? *expected - 1 : 0;
        if (!source.asset || !expected || source.native_frame_count == 0 || source.output_frame_count == 0 ||
            source.output_frame_count < low || source.output_frame_count - low > 2 ||
            !context.sources.emplace(source.asset.value, &source).second)
            return fail(context, "Invalid or duplicate decoded source geometry", source.asset);
    }
    for (const auto& asset : context.song.audio_assets)
        if (!asset.id || !context.assets.emplace(asset.id.value, &asset).second)
            return fail(context, "Invalid or duplicate audio asset identity", asset.id);
    for (const auto& resource : context.song.resources)
        if (!resource.id || !context.resources.emplace(resource.id.value, resource.asset).second)
            return fail(context, "Invalid or duplicate resource identity", resource.asset);
    return true;
}

auto source_for(plan_context_t& context, asset_id_t asset) -> const slice_span_source_t* {
    const auto found = context.sources.find(asset.value);
    if (!context.assets.contains(asset.value) || found == context.sources.end()) {
        fail(context, "Audio asset has no decoded source geometry", asset);
        return nullptr;
    }
    return found->second;
}

auto add_span(plan_context_t& context, asset_id_t asset, std::uint64_t begin, std::uint64_t end)
    -> std::optional<std::size_t> {
    const auto source = source_for(context, asset);
    if (!source) return std::nullopt;
    if (end <= begin || end > source->output_frame_count) {
        fail(context, "Audio span has no valid decoded frame range", asset);
        return std::nullopt;
    }
    const auto key = std::tuple{asset.value, begin, end};
    if (const auto found = context.spans.find(key); found != context.spans.end()) return found->second;
    if (context.result.value.spans.size() >= context.options.maximum_spans) {
        fail(context, "Distinct audio span count exceeds the configured limit", asset);
        return std::nullopt;
    }
    const auto index = context.result.value.spans.size();
    context.spans.emplace(key, index);
    context.result.value.spans.push_back({asset, begin, end, {}});
    return index;
}

auto add_mapping(plan_context_t& context, slice_span_event_kind_t kind, object_id_t event, std::optional<std::size_t> span) -> bool {
    if (!event || context.result.value.mappings.size() >= context.options.maximum_events ||
        !context.events.emplace(kind, event.value).second)
        return fail(context, "Invalid, duplicate or over-limit audio event mapping", {}, event);
    context.result.value.mappings.push_back({kind, event, span});
    return true;
}

auto append_channel_slice(plan_context_t& context, const derived_sound_slice_t& slice,
                          const slice_span_source_t& source) -> bool {
    const auto native_begin = normalized_frame(slice.source_begin_frame, source.native_sample_rate);
    const auto native_end = slice.source_end_frame ? normalized_frame(*slice.source_end_frame, source.native_sample_rate) : std::nullopt;
    if (!native_begin || !native_end) return fail(context, "Channel span boundary overflows decoded frames", source.asset);
    // Complete-source EOF follows the decoder's legitimate final resampler rounding.
    const auto begin = slice.source_begin_frame == source.native_frame_count ? source.output_frame_count : *native_begin;
    const auto end = *slice.source_end_frame == source.native_frame_count ? source.output_frame_count : *native_end;
    if (begin > end || end > source.output_frame_count)
        return fail(context, "Channel span has invalid normalized frame bounds", source.asset);
    std::optional<std::size_t> index;
    if (begin < end) {
        index = add_span(context, source.asset, begin, end);
        if (!index) return false;
    }
    for (const auto event : slice.events) {
        if (!context.channel_events.emplace(event.value, channel_binding_t{source.asset, index}).second)
            return fail(context, "Ambiguous retained channel event identity", source.asset, event);
        if (!add_mapping(context, slice_span_event_kind_t::sound_channel_event, event, index)) return false;
    }
    return true;
}

auto append_packed_channel(plan_context_t& context, const chart_document_t& chart,
                           const sound_channel_t& channel, const slice_span_source_t& source) -> bool {
    const auto end = channel.source_clip.end_frame.value_or(source.native_frame_count);
    if (end <= channel.source_clip.begin_frame)
        return fail(context, "Packed channel clip has no playable frames", source.asset, channel.id);
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures))
        return fail(context, "Packed channel requires valid measure geometry", source.asset, channel.id);
    derived_sound_slice_t slice{.source_begin_frame = channel.source_clip.begin_frame, .source_end_frame = end, .restart = true, .events = {}};
    for (const auto& event : channel.events) {
        if (!absolute_row(std::get<measure_index_t>(measures), event.position))
            return fail(context, "Packed channel event is outside chart geometry", source.asset, event.id);
        slice.events.push_back(event.id);
    }
    return append_channel_slice(context, slice, source);
}

auto is_stem_channel(const chart_document_t& chart, const sound_channel_t& channel) -> bool {
    return std::ranges::any_of(chart.bgm_source_lanes, [&](const auto& lane) { return lane.stem_channel == channel.id; }) ||
        std::ranges::any_of(channel.events, [](const auto& event) { return event.continuation; });
}

auto append_channels(plan_context_t& context, const chart_document_t& chart) -> bool {
    for (const auto& channel : chart.sound_channels) {
        if (channel.events.empty()) continue;
        const auto source = source_for(context, channel.source_clip.source);
        if (!source) return false;
        if (channel.source_clip.end_frame && *channel.source_clip.end_frame > source->native_frame_count)
            return fail(context, "Channel clip extends beyond the decoded asset", source->asset, channel.id);
        const auto stem = is_stem_channel(chart, channel);
        if (!stem) {
            if (!append_packed_channel(context, chart, channel, *source)) return false;
            continue;
        }
        const auto derived = derive_sound_channel_slices(chart, channel, source->native_sample_rate,
            source->native_frame_count, {.maximum_events = context.options.maximum_events});
        if (!derived) return fail(context, derived.diagnostics.front().message, source->asset, channel.id);
        for (const auto& slice : derived.slices)
            if (!append_channel_slice(context, slice, *source)) return false;
    }
    return true;
}

auto resource_asset(plan_context_t& context, resource_id_t resource, object_id_t event) -> std::optional<asset_id_t> {
    const auto found = context.resources.find(resource.value);
    if (found == context.resources.end()) {
        fail(context, "Audible event resource is missing", {}, event);
        return std::nullopt;
    }
    return found->second;
}

template <typename Event>
auto append_semantic(plan_context_t& context, const Event& event, slice_span_event_kind_t kind) -> bool {
    if (event.sound_channel_event) {
        const auto found = context.channel_events.find(event.sound_channel_event->value);
        if (found == context.channel_events.end()) return fail(context, "Audible event has a stale channel link", {}, event.id);
        const auto& binding = found->second;
        if (event.sound) {
            const auto asset = resource_asset(context, *event.sound, event.id);
            if (!asset) return false;
            if (*asset != binding.asset) return fail(context, "Linked event resource disagrees with its channel asset", *asset, event.id);
        }
        return add_mapping(context, kind, event.id, binding.span_index);
    }
    if (!event.sound) return add_mapping(context, kind, event.id, std::nullopt);
    const auto asset = resource_asset(context, *event.sound, event.id);
    if (!asset) return false;
    const auto source = source_for(context, *asset);
    if (!source) return false;
    const auto span = add_span(context, *asset, 0, source->output_frame_count);
    return span && add_mapping(context, kind, event.id, *span);
}

auto append_semantics(plan_context_t& context, const chart_document_t& chart) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (!append_semantic(context, note, slice_span_event_kind_t::note)) return false;
        for (const auto& bgm : measure.bgm_events)
            if (!append_semantic(context, bgm, slice_span_event_kind_t::bgm_event)) return false;
    }
    return true;
}

void name_spans(plan_context_t& context) {
    std::set<std::string> occupied;
    for (const auto& name : context.options.occupied_filenames) occupied.insert(ascii_lowercase(name));
    std::size_t number{};
    for (auto& span : context.result.value.spans) {
        const auto stem = filename_stem(context.assets.at(span.asset.value)->source_path, span.asset);
        do {
            auto suffix = std::to_string(++number);
            suffix.insert(0, suffix.size() < 3 ? 3 - suffix.size() : 0, '0');
            span.filename = stem + "_" + suffix + ".wav";
        } while (!occupied.insert(ascii_lowercase(span.filename)).second);
    }
}

} // namespace

auto slice_span_plan_payload_t::from(const song_document_t& song, chart_id_t chart_id,
    std::span<const slice_span_source_t> sources, const slice_span_options_t& options) -> slice_span_plan_outcome_t {
    slice_span_plan_outcome_t result{};
    plan_context_t context{song, options, result, {}, {}, {}, {}, {}, {}};
    const auto chart = song.find_chart(chart_id);
    if (!chart_id || chart == nullptr || std::ranges::count(song.charts, chart_id, &chart_document_t::id) != 1) {
        fail(context, "Slice spans require one unambiguous chart");
        return result;
    }
    if (!index_sources(context, sources) || !append_channels(context, *chart) || !append_semantics(context, *chart)) {
        result.value.spans.clear();
        result.value.mappings.clear();
        return result;
    }
    result.value.sources.assign(sources.begin(), sources.end());
    name_spans(context);
    return result;
}

} // namespace wormedit
