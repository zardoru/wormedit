#include "sys/bmson_waveform_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/channel_waveform.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <ranges>

namespace wormedit::sys {

void populate_bmson_waveform_projection(
    flattened_workspace_t& flattened, wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    wormedit::editor_interaction_state_t* interaction_state,
    compose_scene_cache_t& cache,
    wormedit::audio::decoded_source_registry_t& audition_sources,
    const std::optional<wormedit::asset_id_t>& waveform_job_asset,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& waveform_analysis,
    const std::optional<wormedit::asset_id_t>& channel_waveform_job_asset,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>&
        channel_waveform_analysis,
    std::uint64_t audition_source_generation) {
    if (!(flattened.view.compose.visible &&
          flattened.view.compose.selected_bmson_bgm_event_id != 0 &&
          flattened.view.compose.scene_available && workspace.selected_chart &&
          interaction_state && cache.marker_timing_index))
        return;

    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &wormedit::chart_document_t::id);
    if (chart == history.document.charts.end())
        return;

    const auto selection = selected_bmson_waveform_source(
        cache, *chart, history.document.revision,
        wormedit::object_id_t{flattened.view.compose.selected_bmson_bgm_event_id});
    const auto channel = selection
                             ? std::ranges::find(chart->sound_channels, selection->channel,
                                                 &wormedit::sound_channel_t::id)
                             : chart->sound_channels.end();
    if (!selection || channel == chart->sound_channels.end())
        return;

    const auto asset = selection->asset;
    const auto binding = audition_sources.binding(asset);
    const auto stored_asset = std::ranges::find(history.document.audio_assets, asset,
                                                &wormedit::audio_asset_t::id);
    const auto geometry_matches =
        binding && stored_asset != history.document.audio_assets.end() &&
        (stored_asset->stream.sample_rate == 0 ||
         stored_asset->stream.sample_rate == binding->source_sample_rate) &&
        (!stored_asset->stream.frame_count ||
         stored_asset->stream.frame_count == binding->source_frame_count);
    const auto& analysis =
        asset == workspace.transport.audio_asset && waveform_job_asset == asset
            ? waveform_analysis
        : channel_waveform_job_asset == asset
            ? channel_waveform_analysis
            : std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>{};
    if (!(analysis && analysis->waveform && geometry_matches &&
          binding->source_sample_rate != 0 &&
          binding->output_sample_rate == analysis->waveform->sample_rate &&
          binding->output_frame_count == analysis->waveform->total_frames))
        return;

    const auto& waveform = *analysis->waveform;
    const wormedit::audio::channel_waveform_source_geometry_t source{
        .sample_rate = binding->source_sample_rate,
        .total_frames = binding->source_frame_count};
    if (cache.channel_waveform_channel != channel->id ||
        cache.channel_waveform_source != &*analysis->waveform ||
        cache.channel_waveform_source_generation != audition_source_generation) {
        cache.channel_waveform_channel = channel->id;
        cache.channel_waveform_source = &*analysis->waveform;
        cache.channel_waveform_source_generation = audition_source_generation;
        cache.channel_waveform_slices = wormedit::derive_sound_channel_slices(
            *chart, *channel, source.sample_rate, source.total_frames);
        cache.channel_waveform_projection_valid = false;
        cache.channel_waveform_rows.clear();
    }

    const auto scroll = interaction_state->viewport_scroll_pixels;
    const auto height = flattened.view.compose.viewport_height_pixels;
    const auto scale = flattened.view.compose.pixels_per_standard_measure;
    if (!cache.channel_waveform_projection_valid ||
        cache.channel_waveform_scroll_pixels != scroll ||
        cache.channel_waveform_height_pixels != height ||
        cache.channel_waveform_pixels_per_measure != scale) {
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
