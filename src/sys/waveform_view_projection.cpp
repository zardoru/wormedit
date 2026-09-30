#include "sys/waveform_view_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/aligned_waveform.h>

#include <algorithm>

namespace wormedit::sys {

void populate_audio_waveform_views(
    flattened_workspace_t& flattened,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    int window_width) {
    if (!flattened.view.audio_slices.visible && !flattened.view.metadata.visible)
        return;

    auto& audio = flattened.view.audio_slices;
    audio.analysis_pending = analysis_pending;
    flattened.view.metadata.audio_waveform_analysis_pending = analysis_pending;
    if (!analysis || !analysis->waveform)
        return;

    const auto& waveform = *analysis->waveform;
    const auto available_width = std::max(window_width - 80, 1);
    const auto pixel_width =
        std::min(static_cast<std::size_t>(available_width),
                 static_cast<std::size_t>(WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY));
    const auto projection = wormedit::audio::project_waveform(
        waveform, {.begin_frame = 0,
                   .end_frame = waveform.total_frames,
                   .pixel_width = pixel_width,
                   .maximum_columns = WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY});
    if (!projection)
        return;

    flattened.audio.waveform.reserve(projection.columns.size());
    for (const auto& column : projection.columns) {
        flattened.audio.waveform.push_back({
            .minimum = {column.minimum[0], column.minimum[1]},
            .maximum = {column.maximum[0], column.maximum[1]},
            .absolute_peak = {column.absolute_peak[0], column.absolute_peak[1]},
            .rms = {column.rms[0], column.rms[1]},
        });
    }
    audio.waveform_available = true;
    audio.total_frames = waveform.total_frames;
    audio.sample_rate = waveform.sample_rate;
    audio.waveform_columns = flattened.audio.waveform.data();
    audio.waveform_column_count = flattened.audio.waveform.size();

    auto& metadata = flattened.view.metadata;
    metadata.audio_waveform_available = true;
    metadata.audio_waveform_total_frames = waveform.total_frames;
    metadata.audio_waveform_sample_rate = waveform.sample_rate;
    metadata.audio_waveform_columns = flattened.audio.waveform.data();
    metadata.audio_waveform_column_count = flattened.audio.waveform.size();
}

auto populate_compose_waveform_projection(
    flattened_workspace_t& flattened, compose_scene_cache_t& cache,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    bool has_selected_audio_asset,
    const wormedit::editor_interaction_state_t* interaction_state,
    const audio::waveform_analysis_result_t* detail_analysis,
    audio::decoded_block_cache_t* decoded_blocks) -> bool {
    cache.waveform_overview_total_frames = 0;
    cache.waveform_overview_sample_rate = 0;
    auto& compose = flattened.view.compose;
    if (!compose.visible || !has_selected_audio_asset)
        return false;

    compose.waveform_analysis_pending = analysis_pending;
    if (!compose.scene_available || !interaction_state || !cache.marker_timing_index ||
        !analysis || !analysis->waveform)
        return false;

    const auto& waveform = *analysis->waveform;
    const audio::aligned_waveform_request_t request{
        .viewport_scroll_pixels = interaction_state->viewport_scroll_pixels,
        .viewport_height_pixels = compose.viewport_height_pixels,
        .pixels_per_standard_measure = compose.pixels_per_standard_measure,
        .maximum_rows = WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY};
    const auto window = audio::aligned_waveform_window(waveform, *cache.marker_timing_index, request);
    const auto* selected_waveform = &waveform;
    if (window && detail_analysis && detail_analysis->waveform &&
        detail_analysis->waveform->key.source == waveform.key.source &&
        detail_analysis->waveform->key.begin_frame <= window->begin_frame &&
        detail_analysis->waveform->key.end_frame >= window->end_frame)
        selected_waveform = &*detail_analysis->waveform;
    auto aligned = decoded_blocks
        ? audio::project_aligned_waveform_samples(waveform, *cache.marker_timing_index,
                                                  request, *decoded_blocks)
        : audio::aligned_waveform_result_t{};
    if (!aligned || aligned.rows.empty())
        aligned = audio::project_aligned_waveform(*selected_waveform, *cache.marker_timing_index, request);
    if (aligned) {
        flattened.compose.waveform.row.reserve(aligned.rows.size());
        for (const auto& row : aligned.rows) {
            flattened.compose.waveform.row.push_back({
                .y_pixels = row.y_pixels,
                .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                                  std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
                .rms = {std::clamp(row.rms[0], 0.0f, 1.0f),
                        std::clamp(row.rms[1], 0.0f, 1.0f)},
                .sample_point = row.sample_point,
                .samples = {row.samples[0], row.samples[1]},
            });
        }
        compose.waveform_rows = flattened.compose.waveform.row.data();
        compose.waveform_row_count = flattened.compose.waveform.row.size();
    }

    const auto overview = wormedit::audio::project_waveform_overview(
        waveform, *cache.marker_timing_index,
        {.viewport_scroll_pixels = interaction_state->viewport_scroll_pixels,
         .viewport_height_pixels = compose.viewport_height_pixels,
         .pixels_per_standard_measure = compose.pixels_per_standard_measure});
    if (!overview)
        return false;

    flattened.compose.waveform.overview_row.reserve(overview.rows.size());
    for (const auto& row : overview.rows) {
        flattened.compose.waveform.overview_row.push_back({
            .audio_fraction = row.audio_fraction,
            .absolute_peak = std::clamp(row.absolute_peak, 0.0f, 1.0f),
        });
    }
    compose.waveform_overview_rows = flattened.compose.waveform.overview_row.data();
    compose.waveform_overview_row_count = flattened.compose.waveform.overview_row.size();
    compose.waveform_overview_visible_begin_fraction = overview.visible_begin_fraction;
    compose.waveform_overview_visible_end_fraction = overview.visible_end_fraction;
    compose.waveform_beat_zero_visible = overview.beat_zero_fraction.has_value();
    compose.waveform_beat_zero_fraction = overview.beat_zero_fraction.value_or(0.0);
    compose.waveform_overview_sample_rate = waveform.sample_rate;
    compose.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform_overview_sample_rate = waveform.sample_rate;
    return true;
}

} // namespace wormedit::sys
#endif
