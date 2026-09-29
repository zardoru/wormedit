#include "sys/bmson_waveform_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/channel_waveform.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <ranges>

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

} // namespace

void populate_bmson_waveform_projection(const bmson_waveform_projection_context_t& context) {
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
        cache.channel_waveform_rows.clear();
    }

    const auto scroll = interaction_state->viewport_scroll_pixels;
    const auto height = flattened.view.compose.viewport_height_pixels;
    const auto scale = flattened.view.compose.pixels_per_standard_measure;
    if (projection_needs_update(context)) {
        cache.channel_waveform_rows.clear();
        if (cache.channel_waveform_slices && *cache.channel_waveform_slices) {
            const auto projected = wormedit::audio::project_channel_waveform(
                waveform, *channel, *cache.marker_timing_index,
                {.viewport_scroll_pixels = scroll,
                 .viewport_height_pixels = height,
                 .pixels_per_standard_measure = scale,
                 .maximum_rows = WORM_UI_COMPOSE_CHANNEL_WAVEFORM_ROW_CAPACITY},
                *cache.channel_waveform_slices, source);
            if (projected) {
                cache.channel_waveform_rows.reserve(projected.rows.size());
                const auto downscroll = compose_field_downscroll(*chart);
                for (const auto& row : projected.rows) {
                    cache.channel_waveform_rows.push_back({
                        .y_pixels = compose_source_y(row.y_pixels, height, downscroll),
                        .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                                          std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
                        .rms = {std::clamp(row.rms[0], 0.0f, 1.0f),
                                std::clamp(row.rms[1], 0.0f, 1.0f)},
                    });
                }
            }
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
