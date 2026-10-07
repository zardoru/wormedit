#include "sys/bmson_waveform_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/channel_waveform.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
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

constexpr std::size_t maximum_drawn_stems = 8;

// The BGM lane a stem's markers sit in: the source lane of its first linked marker.
auto stem_source_lane(const chart_document_t& chart, const sound_channel_t& channel) -> std::uint64_t {
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events)
            if (event.sound_channel_event &&
                std::ranges::any_of(channel.events, [&](const auto& source) {
                    return source.id == *event.sound_channel_event;
                }))
                return event.source_lane.value;
    return 0;
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

// Projects one stem whose audio is decoded; false leaves it as a placeholder.
auto project_stem(const bmson_waveform_projection_context_t& context, const chart_document_t& chart,
                  const sound_channel_t& channel, compose_scene_cache_t::stem_waveform_cache_t& entry) -> bool {
    const auto asset = channel.source_clip.source;
    const auto binding = context.audition_sources.binding(asset);
    const auto stored = std::ranges::find(context.history.document.audio_assets, asset, &audio_asset_t::id);
    const auto analysis = context.stem_analysis(asset);
    if (!binding || stored == context.history.document.audio_assets.end() ||
        !stored_source_matches(*stored, *binding) || !analysis_matches_binding(analysis, *binding)) return false;
    const audio::channel_waveform_source_geometry_t source{
        .sample_rate = binding->source_sample_rate, .total_frames = binding->source_frame_count};
    const auto& waveform = *analysis->waveform;
    auto& cache = context.cache;
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
    const auto scroll = context.interaction_state->viewport_scroll_pixels;
    const auto height = context.flattened.view.compose.viewport_height_pixels;
    const auto zoom = context.flattened.view.compose.pixels_per_standard_measure;
    if (!entry.valid || entry.scroll != scroll || entry.height != height || entry.zoom != zoom) {
        entry.rows.clear();
        if (entry.slices && *entry.slices) {
            const channel_ring_context_t projection{context, waveform, channel, source, scroll, height, zoom,
                                                    {entry.ring, entry.rows, entry.slices}};
            populate_channel_ring(projection, compose_field_downscroll(chart));
        }
        entry.scroll = scroll;
        entry.height = height;
        entry.zoom = zoom;
        entry.valid = true;
    }
    static_cast<void>(cache);
    return true;
}

// Every stem but the selected one, drawn in its own BGM lane. A stem whose audio is not ready
// yet is listed as pending so the canvas can show a placeholder band.
void populate_stem_waveforms(const bmson_waveform_projection_context_t& context) {
    auto& cache = context.cache;
    cache.stem_waveform_views.clear();
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
    cache.stem_waveforms.reserve(maximum_drawn_stems);
    struct drawn_t { const sound_channel_t* channel; std::uint64_t lane; };
    std::vector<drawn_t> drawn;
    for (const auto& channel : chart->sound_channels) {
        if (drawn.size() >= maximum_drawn_stems) break;
        if (selected && selected->channel == channel.id) continue;
        if (const auto lane = stem_source_lane(*chart, channel); lane != 0) drawn.push_back({&channel, lane});
    }
    // Entries for channels that are no longer extra stems (deleted, or now the selected one) go first.
    std::erase_if(cache.stem_waveforms, [&](const auto& entry) {
        return std::ranges::none_of(drawn, [&](const auto& item) { return item.channel->id == entry.channel; });
    });
    for (const auto& item : drawn) {
        auto& entry = stem_entry(cache, item.channel->id);
        entry.source_lane = item.lane;
        entry.pending = !project_stem(context, *chart, *item.channel, entry);
        cache.stem_waveform_views.push_back({.source_lane_id = item.lane,
            .rows = entry.pending ? nullptr : entry.rows.data(),
            .row_count = entry.pending ? 0 : entry.rows.size(), .pending = entry.pending});
    }
    context.flattened.view.compose.stem_waveforms = cache.stem_waveform_views.data();
    context.flattened.view.compose.stem_waveform_count = cache.stem_waveform_views.size();
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
