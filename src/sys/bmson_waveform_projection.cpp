#include "sys/bmson_waveform_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/stem_waveform_window.h"

#include <wormedit/audio/channel_waveform.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <unordered_set>
#include <ranges>
#include <cmath>

namespace wormedit::sys {
namespace {

auto projection_ready(const bmson_waveform_projection_context_t& context) -> bool {
    return context.flattened.view.compose.visible &&
           context.flattened.view.compose.selected_bmson_bgm_event_id != 0 &&
           context.flattened.view.compose.scene_available && context.workspace.selected_chart &&
           context.interaction_state != nullptr && context.cache.marker_timing_index;
}

auto stored_source_matches(const audio_asset_t& asset,
                           const audio::decoded_source_binding_t& binding) -> bool {
    return (asset.stream.sample_rate == 0 ||
            asset.stream.sample_rate == binding.source_sample_rate) &&
           (!asset.stream.frame_count || asset.stream.frame_count == binding.source_frame_count);
}

auto analysis_matches_binding(
    const std::shared_ptr<const audio::waveform_analysis_result_t>& analysis,
    const audio::decoded_source_binding_t& binding) -> bool {
    return analysis && analysis->waveform && binding.source_sample_rate != 0 &&
           binding.output_sample_rate == analysis->waveform->sample_rate &&
           binding.output_frame_count == analysis->waveform->total_frames;
}

auto projection_needs_update(const bmson_waveform_projection_context_t& context) -> bool {
    const auto& cache = context.cache;
    return !cache.channel_waveform_projection_valid ||
           cache.channel_waveform_scroll_pixels !=
               context.interaction_state->viewport_scroll_pixels ||
           cache.channel_waveform_height_pixels !=
               context.flattened.view.compose.viewport_height_pixels ||
           cache.channel_waveform_pixels_per_measure !=
               context.flattened.view.compose.pixels_per_standard_measure;
}

// Where one channel's projection lives: the selected channel's fields in the scene cache, or one
// stem's own entry.
struct channel_projection_target_t {
    envelope_projection_ring_t& ring;
    std::vector<WormUiComposeWaveformRowView>& rows;
    const std::optional<sound_slice_result_t>& slices;
};

struct channel_ring_context_t {
    const bmson_waveform_projection_context_t& application;
    const audio::waveform_t& waveform;
    const sound_channel_t& channel;
    audio::channel_waveform_source_geometry_t source;
    double scroll{}, height{}, zoom{};
    channel_projection_target_t target;
};
auto channel_ring_slot(std::int64_t row, std::size_t count) -> std::size_t {
    const auto remainder = row % static_cast<std::int64_t>(count);
    return static_cast<std::size_t>(remainder < 0 ? remainder + static_cast<std::int64_t>(count) : remainder);
}
void project_channel_ring_range(const channel_ring_context_t& context, std::int64_t begin,
                                std::size_t count) {
    auto& cache = context.application.cache;
    auto& ring = context.target.ring;
    const auto projected = audio::project_channel_waveform(context.waveform, context.channel,
        *cache.marker_timing_index,
        {.viewport_scroll_pixels = static_cast<double>(begin),
         .viewport_height_pixels = static_cast<double>(count),
         .pixels_per_standard_measure = context.zoom,
         .maximum_rows = WORM_UI_COMPOSE_CHANNEL_WAVEFORM_ROW_CAPACITY},
        *context.target.slices, context.source);
    ring.projected_rows += count;
    ++ring.projection_calls;
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = begin + static_cast<std::int64_t>(index);
        ring.rows[channel_ring_slot(world, ring.rows.size())] = {world, {}, false, false};
    }
    for (const auto& row : projected.rows) {
        const auto world = begin + static_cast<std::int64_t>(std::floor(row.y_pixels));
        if (world < begin || world >= begin + static_cast<std::int64_t>(count)) continue;
        ring.rows[channel_ring_slot(world, ring.rows.size())] = {world,
            {.y_pixels = static_cast<double>(world) + .5,
             .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                               std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
             .rms = {std::clamp(row.rms[0], 0.0f, 1.0f), std::clamp(row.rms[1], 0.0f, 1.0f)},
             .sample_point = false, .samples = {}}, true, false};
    }
}
auto populate_channel_ring(const channel_ring_context_t& context, bool downscroll) -> bool {
    if (!std::isfinite(context.scroll) || std::abs(context.scroll) > 1e12 ||
        !std::isfinite(context.height) || context.height <= 0 ||
        context.height > WORM_UI_COMPOSE_CHANNEL_WAVEFORM_ROW_CAPACITY - 2) return false;
    auto& ring = context.target.ring;
    auto& rows = context.target.rows;
    const auto count = static_cast<std::size_t>(std::ceil(context.height)) + 2;
    const auto revision = context.application.history.document.revision;
    const auto project = context.application.history.document.id.value;
    const auto chart = context.application.workspace.selected_chart->value;
    const auto channel = context.channel.id.value;
    if (!ring.valid || ring.rows.size() != count || ring.source_identity != &context.waveform ||
        ring.source_key != context.waveform.key || ring.revision != revision || ring.zoom != context.zoom ||
        ring.project != project || ring.chart != chart || ring.channel != channel ||
        ring.source_generation != context.application.audition_source_generation)
        ring.rows.assign(count, {});
    ring.valid = true;
    ring.source_identity = &context.waveform;
    ring.source_key = context.waveform.key;
    ring.revision = revision;
    ring.project = project;
    ring.chart = chart;
    ring.channel = channel;
    ring.source_generation = context.application.audition_source_generation;
    ring.zoom = context.zoom;
    ring.projected_rows = ring.reused_rows = ring.projection_calls = 0;
    const auto first = static_cast<std::int64_t>(std::floor(context.scroll)) - 1;
    std::size_t index = 0;
    while (index < count) {
        const auto world = first + static_cast<std::int64_t>(index);
        if (ring.rows[channel_ring_slot(world, count)].world_row == world) {
            ++ring.reused_rows; ++index; continue;
        }
        const auto begin = index++;
        while (index < count && ring.rows[channel_ring_slot(first + static_cast<std::int64_t>(index), count)].world_row !=
                                    first + static_cast<std::int64_t>(index)) ++index;
        project_channel_ring_range(context, first + static_cast<std::int64_t>(begin), index - begin);
    }
    rows.reserve(count);
    for (std::size_t row = 0; row < count; ++row) {
        const auto& stored = ring.rows[channel_ring_slot(first + static_cast<std::int64_t>(row), count)];
        if (!stored.present) continue;
        auto value = stored.value;
        value.y_pixels = compose_source_y(value.y_pixels - std::floor(context.scroll), context.height, downscroll);
        if (value.y_pixels < 0 || value.y_pixels > context.height) continue;
        rows.push_back(value);
    }
    return true;
}

// One more than the visible stems: the selected channel is always projected for its field taps.
constexpr std::size_t maximum_stem_cuts = 4096;

// The BGM lane a stem lives in: the lane whose source order is the channel's position (what moving
// one of its taps to the BGM area creates), else the lane of its first BGM marker.
auto indexed_stem_lane(const stem_index_t& index, const chart_document_t& chart,
                       const sound_channel_t& channel) -> std::uint64_t {
    if (chart.sound_channels.empty()) return 0;
    const auto position = static_cast<std::size_t>(&channel - chart.sound_channels.data());
    return position < index.channels.size() ? index.channels[position].source_lane : 0;
}

auto stem_ready(const bmson_waveform_projection_context_t& context) -> bool {
    return context.flattened.view.compose.visible && context.flattened.view.compose.scene_available &&
           context.workspace.selected_chart && context.interaction_state != nullptr &&
           context.cache.marker_timing_index && context.stem_analysis;
}

auto stem_entry(compose_scene_cache_t& cache, object_id_t channel)
    -> compose_scene_cache_t::stem_waveform_cache_t& {
    auto found = std::ranges::find(cache.stem_waveforms, channel,
                                   &compose_scene_cache_t::stem_waveform_cache_t::channel);
    if (found != cache.stem_waveforms.end()) return *found;
    cache.stem_waveforms.emplace_back();
    cache.stem_waveforms.back().channel = channel;
    return cache.stem_waveforms.back();
}

// The analysis of a stem's audio: the transport's or the selected channel's when it is theirs.
auto stem_analysis_for(const bmson_waveform_projection_context_t& context, asset_id_t asset)
    -> std::shared_ptr<const audio::waveform_analysis_result_t> {
    if (asset == context.workspace.transport.audio_asset && context.transport_waveform.asset == asset)
        return context.transport_waveform.analysis;
    if (context.channel_waveform.asset == asset) return context.channel_waveform.analysis;
    return context.stem_analysis(asset);
}

// Retained slice-point event → the note or BGM marker that stands for it.
void rebuild_point_refs(const chart_document_t& chart, const sound_channel_t& channel,
                        compose_scene_cache_t::stem_waveform_cache_t& entry) {
    entry.refs.clear();
    std::unordered_set<std::uint64_t> events;
    for (const auto& event : channel.events) events.insert(event.id.value);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (note.sound_channel_event && events.contains(note.sound_channel_event->value))
                entry.refs[note.sound_channel_event->value] = {note.id.value, note.lane.value, true};
        for (const auto& bgm : measure.bgm_events)
            if (bgm.sound_channel_event && events.contains(bgm.sound_channel_event->value))
                entry.refs[bgm.sound_channel_event->value] = {bgm.id.value, bgm.source_lane.value, false};
    }
}

struct stem_split_context_t {
    const chart_document_t& chart;
    compose_scene_cache_t::stem_waveform_cache_t& entry;
    double scroll{}, height{}, zoom{};
    bool downscroll{};
};

auto slice_lane_ref(const stem_split_context_t& context, const derived_sound_slice_t& slice)
    -> std::optional<compose_scene_cache_t::stem_waveform_cache_t::point_ref_t> {
    for (const auto event : slice.events) {
        const auto found = context.entry.refs.find(event.value);
        if (found != context.entry.refs.end()) return found->second;
    }
    return std::nullopt;
}

// Lists the stem's cut lines and keeps its rows for the BGM lane. The slice points come from
// notes in the field as well as BGM markers, but all of them are drawn in the stem's own lane.
void split_stem_by_lane(const stem_split_context_t& context) {
    auto& entry = context.entry;
    entry.pieces.clear();
    entry.cuts.clear();
    if (!entry.slices || !*entry.slices || context.chart.divisions_per_standard_measure == 0 ||
        entry.source_lane == 0) return;
    const auto& slices = entry.slices->slices;
    const auto base = std::floor(context.scroll);
    for (const auto& slice : slices) {
        const auto ref = slice_lane_ref(context, slice);
        if (!ref) continue;
        const auto world = static_cast<double>(slice.absolute_row) * context.zoom /
                           static_cast<double>(context.chart.divisions_per_standard_measure);
        const auto y = compose_source_y(world - base, context.height, context.downscroll);
        if (y >= 0.0 && y <= context.height)
            entry.cuts.push_back({.y_pixels = y, .bgm_lane_id = entry.source_lane, .restart = slice.restart,
                                  .object_is_note = ref->playable, .object_id = ref->object});
    }
    // The selected channel's waveform is drawn by its own path; everything else goes in its lane.
    if (!entry.is_selected && !entry.rows.empty())
        entry.pieces.push_back({.lane = entry.source_lane, .playable = false, .rows = entry.rows});
}

// Projects one stem whose audio is decoded; false leaves it as a placeholder.
auto project_stem(const bmson_waveform_projection_context_t& context, const chart_document_t& chart,
                  const sound_channel_t& channel, compose_scene_cache_t::stem_waveform_cache_t& entry) -> bool {
    const auto asset = channel.source_clip.source;
    const auto binding = context.audition_sources.binding(asset);
    const auto stored = std::ranges::find(context.history.document.audio_assets, asset, &audio_asset_t::id);
    const auto analysis = stem_analysis_for(context, asset);
    if (!binding || stored == context.history.document.audio_assets.end() ||
        !stored_source_matches(*stored, *binding) || !analysis_matches_binding(analysis, *binding)) return false;
    const audio::channel_waveform_source_geometry_t source{
        .sample_rate = binding->source_sample_rate, .total_frames = binding->source_frame_count};
    const auto& waveform = *analysis->waveform;
    const auto revision = context.history.document.revision;
    if (entry.source != &waveform || entry.source_generation != context.audition_source_generation ||
        entry.revision != revision) {
        entry.source = &waveform;
        entry.source_generation = context.audition_source_generation;
        entry.revision = revision;
        entry.slices = derive_sound_channel_slices(chart, channel, source.sample_rate, source.total_frames);
        entry.valid = false;
        entry.ring.valid = false;
        entry.rows.clear();
    }
    if (entry.refs_revision != revision || entry.refs.empty()) {
        rebuild_point_refs(chart, channel, entry);
        entry.refs_revision = revision;
    }
    const auto scroll = context.interaction_state->viewport_scroll_pixels;
    const auto height = context.flattened.view.compose.viewport_height_pixels;
    const auto zoom = context.flattened.view.compose.pixels_per_standard_measure;
    if (!entry.valid || entry.scroll != scroll || entry.height != height || entry.zoom != zoom) {
        entry.rows.clear();
        const auto downscroll = compose_field_downscroll(chart);
        if (entry.slices && *entry.slices) {
            const channel_ring_context_t projection{context, waveform, channel, source, scroll, height, zoom,
                                                    {entry.ring, entry.rows, entry.slices}};
            populate_channel_ring(projection, downscroll);
        }
        split_stem_by_lane(stem_split_context_t{chart, entry, scroll, height, zoom, downscroll});
        entry.scroll = scroll;
        entry.height = height;
        entry.zoom = zoom;
        entry.valid = true;
    }
    return true;
}

struct drawn_stem_t {
    const sound_channel_t* channel{};
    std::uint64_t lane{};
    bool selected{};
};

// The stems to project: those near the cursor, plus the selected channel wherever it is.
auto drawn_stems(const bmson_waveform_projection_context_t& context, const chart_document_t& chart,
                 const std::optional<compose_scene_cache_t::channel_waveform_selection_t>& selected)
    -> std::vector<drawn_stem_t> {
    std::vector<drawn_stem_t> drawn;
    const bool cache_matches = context.cache.chart == chart.id &&
                               context.cache.revision == context.history.document.revision;
    const auto* timing = cache_matches && context.cache.marker_timing_index
                             ? &*context.cache.marker_timing_index
                             : nullptr;
    std::vector<const sound_channel_t*> fallback_visible;
    std::span<const sound_channel_t* const> visible;
    if (context.visible_stems) visible = *context.visible_stems;
    else {
        fallback_visible = indexed_visible_stem_channels(context.cache.stem_index,
            context.history.document, chart, context.interaction_state->cursor.measure,
            stem_window_measures(context.flattened.view.compose.viewport_height_pixels,
                                 context.flattened.view.compose.pixels_per_standard_measure),
            context.audition_sources.bindings(), timing);
        visible = fallback_visible;
    }
    for (const auto* channel : visible)
        drawn.push_back({channel, indexed_stem_lane(context.cache.stem_index, chart, *channel), selected && selected->channel == channel->id});
    if (selected && std::ranges::none_of(drawn, [](const auto& item) { return item.selected; })) {
        const auto found = std::ranges::find(chart.sound_channels, selected->channel, &sound_channel_t::id);
        if (found != chart.sound_channels.end()) drawn.push_back({&*found, indexed_stem_lane(context.cache.stem_index, chart, *found), true});
    }
    return drawn;
}

// Publishes one stem: a placeholder band while its audio is not ready, otherwise its lane pieces
// (the selected channel's BGM lane stays with the old path) and cut lines.
void publish_stem(compose_scene_cache_t& cache, const drawn_stem_t& item,
                  const compose_scene_cache_t::stem_waveform_cache_t& entry) {
    if (entry.pending) {
        if (!item.selected && item.lane != 0)
            cache.stem_waveform_views.push_back({.stem_key = item.channel->id.value, .source_lane_id = item.lane, .rows = nullptr, .row_count = 0,
                                                 .pending = true});
        return;
    }
    for (const auto& piece : entry.pieces)
        if (!piece.rows.empty())
            cache.stem_waveform_views.push_back({.stem_key = item.channel->id.value, .source_lane_id = piece.lane, .rows = piece.rows.data(),
                                                 .row_count = piece.rows.size(), .pending = false});
    for (const auto& cut : entry.cuts)
        if (cache.stem_cut_views.size() < maximum_stem_cuts) cache.stem_cut_views.push_back(cut);
}

// The stems near the cursor, each drawn along its slices in the lane of the note or BGM marker
// that starts the slice, with cut lines at the slice points.
void populate_stem_waveforms(const bmson_waveform_projection_context_t& context) {
    auto& cache = context.cache;
    cache.stem_waveform_views.clear();
    cache.stem_cut_views.clear();
    if (!stem_ready(context)) return;
    const auto chart = std::ranges::find(context.history.document.charts, *context.workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end() || chart->source_family != format_family_t::bmson) {
        cache.stem_waveforms.clear();
        return;
    }
    const auto selected = context.flattened.view.compose.selected_bmson_bgm_event_id != 0
        ? selected_bmson_waveform_source(cache, *chart, context.history.document.revision,
              object_id_t{context.flattened.view.compose.selected_bmson_bgm_event_id})
        : std::nullopt;
    const auto drawn = drawn_stems(context, *chart, selected);
    cache.stem_waveforms.reserve(drawn.size());
    // Entries for channels that are no longer drawn go first.
    std::unordered_set<std::uint64_t> drawn_ids;
    drawn_ids.reserve(drawn.size());
    for (const auto& item : drawn) drawn_ids.insert(item.channel->id.value);
    std::erase_if(cache.stem_waveforms, [&](const auto& entry) { return !drawn_ids.contains(entry.channel.value); });
    for (const auto& item : drawn) {
        auto& entry = stem_entry(cache, item.channel->id);
        entry.is_selected = item.selected;
        entry.source_lane = item.lane;
        entry.pending = !project_stem(context, *chart, *item.channel, entry);
        publish_stem(cache, item, entry);
    }
    context.flattened.view.compose.stem_waveforms = cache.stem_waveform_views.data();
    context.flattened.view.compose.stem_waveform_count = cache.stem_waveform_views.size();
    context.flattened.view.compose.stem_cuts = cache.stem_cut_views.data();
    context.flattened.view.compose.stem_cut_count = cache.stem_cut_views.size();
}

} // namespace

void populate_bmson_waveform_projection(const bmson_waveform_projection_context_t& context) {
    populate_stem_waveforms(context);
    if (!projection_ready(context))
        return;

    auto& flattened = context.flattened;
    auto& history = context.history;
    const auto& workspace = context.workspace;
    const auto* interaction_state = context.interaction_state;
    auto& cache = context.cache;

    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &wormedit::chart_document_t::id);
    if (chart == history.document.charts.end())
        return;

    const auto selection = selected_bmson_waveform_source(
        cache, *chart, history.document.revision,
        wormedit::object_id_t{flattened.view.compose.selected_bmson_bgm_event_id});
    const auto channel = selection ? std::ranges::find(chart->sound_channels, selection->channel,
                                                       &wormedit::sound_channel_t::id)
                                   : chart->sound_channels.end();
    if (!selection || channel == chart->sound_channels.end())
        return;

    const auto asset = selection->asset;
    const auto binding = context.audition_sources.binding(asset);
    const auto stored_asset =
        std::ranges::find(history.document.audio_assets, asset, &wormedit::audio_asset_t::id);
    const auto& analysis =
        asset == workspace.transport.audio_asset && context.transport_waveform.asset == asset
            ? context.transport_waveform.analysis
        : context.channel_waveform.asset == asset
            ? context.channel_waveform.analysis
            : std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>{};
    if (!binding || stored_asset == history.document.audio_assets.end() ||
        !stored_source_matches(*stored_asset, *binding) ||
        !analysis_matches_binding(analysis, *binding))
        return;

    const auto& waveform = *analysis->waveform;
    const wormedit::audio::channel_waveform_source_geometry_t source{
        .sample_rate = binding->source_sample_rate, .total_frames = binding->source_frame_count};
    if (cache.channel_waveform_channel != channel->id ||
        cache.channel_waveform_source != &*analysis->waveform ||
        cache.channel_waveform_source_generation != context.audition_source_generation) {
        cache.channel_waveform_channel = channel->id;
        cache.channel_waveform_source = &*analysis->waveform;
        cache.channel_waveform_source_generation = context.audition_source_generation;
        cache.channel_waveform_slices = wormedit::derive_sound_channel_slices(
            *chart, *channel, source.sample_rate, source.total_frames);
        cache.channel_waveform_projection_valid = false;
        cache.channel_envelope_ring.valid = false;
        cache.channel_waveform_rows.clear();
    }

    const auto scroll = interaction_state->viewport_scroll_pixels;
    const auto height = flattened.view.compose.viewport_height_pixels;
    const auto scale = flattened.view.compose.pixels_per_standard_measure;
    if (projection_needs_update(context)) {
        cache.channel_waveform_rows.clear();
        if (cache.channel_waveform_slices && *cache.channel_waveform_slices) {
            const channel_ring_context_t projection{context, waveform, *channel, source, scroll, height, scale,
                {cache.channel_envelope_ring, cache.channel_waveform_rows, cache.channel_waveform_slices}};
            populate_channel_ring(projection, compose_field_downscroll(*chart));
        }
        cache.channel_waveform_scroll_pixels = scroll;
        cache.channel_waveform_height_pixels = height;
        cache.channel_waveform_pixels_per_measure = scale;
        cache.channel_waveform_projection_valid = true;
    }

    if (!cache.channel_waveform_rows.empty()) {
        flattened.view.compose.channel_waveform_rows = cache.channel_waveform_rows.data();
        flattened.view.compose.channel_waveform_row_count = cache.channel_waveform_rows.size();
        flattened.view.compose.channel_waveform_source_lane_id = selection->source_lane.value;
    }
}

} // namespace wormedit::sys

#endif
