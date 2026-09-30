#include "sys/waveform_view_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/aligned_waveform.h>

#include <algorithm>
#include <cmath>

namespace wormedit::sys {
namespace {
auto overview_fraction(const audio::waveform_t& waveform, const timing_index_t& timing,
                       long double row, timing_boundary_side_t side, bool round_up)
    -> std::optional<double> {
    row = std::clamp(row, 0.0L, static_cast<long double>(timing.total_rows));
    const auto time = timing_at(timing, static_cast<std::uint64_t>(row), side);
    if (!time || !time.seconds || time.seconds->denominator == 0) return std::nullopt;
    const auto frames = static_cast<long double>(time.seconds->numerator) * waveform.sample_rate /
                        time.seconds->denominator;
    const auto bounded = std::clamp(round_up ? std::ceil(frames) : std::floor(frames),
                                   0.0L, static_cast<long double>(waveform.total_frames));
    return static_cast<double>(bounded / waveform.total_frames);
}
auto populate_cached_overview(flattened_workspace_t& flattened, compose_scene_cache_t& cache,
                              const audio::waveform_t& waveform,
                              const audio::aligned_waveform_request_t& request) -> bool {
    if (!std::isfinite(request.viewport_scroll_pixels) ||
        !std::isfinite(request.viewport_height_pixels) ||
        !std::isfinite(request.pixels_per_standard_measure) ||
        request.viewport_height_pixels <= 0 || request.pixels_per_standard_measure <= 0)
        return false;
    auto& view = flattened.view.compose;
    const auto& timing = *cache.marker_timing_index;
    if (!cache.waveform_overview_key || *cache.waveform_overview_key != waveform.key ||
        cache.waveform_overview_identity != &waveform ||
        cache.waveform_overview_revision != cache.revision) {
        const auto overview = audio::project_waveform_overview(waveform, timing, request);
        if (!overview) return false;
        cache.waveform_overview_rows.clear();
        for (const auto& row : overview.rows)
            cache.waveform_overview_rows.push_back({row.audio_fraction,
                                                   std::clamp(row.absolute_peak, 0.0f, 1.0f)});
        cache.waveform_overview_key = waveform.key;
        cache.waveform_overview_identity = &waveform;
        cache.waveform_overview_revision = cache.revision;
    }
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                               request.pixels_per_standard_measure;
    const auto begin = overview_fraction(waveform, timing,
        std::floor(request.viewport_scroll_pixels * rows_per_pixel),
        timing_boundary_side_t::before_delays, false);
    const auto end = overview_fraction(waveform, timing,
        std::ceil((request.viewport_scroll_pixels + request.viewport_height_pixels) * rows_per_pixel),
        timing_boundary_side_t::after_stops, true);
    if (!begin || !end) return false;
    flattened.compose.waveform.overview_row = cache.waveform_overview_rows;
    view.waveform_overview_rows = flattened.compose.waveform.overview_row.data();
    view.waveform_overview_row_count = flattened.compose.waveform.overview_row.size();
    view.waveform_overview_visible_begin_fraction = *begin;
    view.waveform_overview_visible_end_fraction = *end;
    const auto zero = timing_at(timing, 0, timing_boundary_side_t::before_delays);
    const auto zero_fraction = zero && zero.seconds && zero.seconds->denominator != 0
        ? static_cast<long double>(zero.seconds->numerator) * waveform.sample_rate /
          zero.seconds->denominator / waveform.total_frames : -1.0L;
    view.waveform_beat_zero_visible = std::isfinite(zero_fraction) && zero_fraction >= 0 && zero_fraction <= 1;
    view.waveform_beat_zero_fraction = view.waveform_beat_zero_visible ? static_cast<double>(zero_fraction) : 0.0;
    return true;
}
} // namespace


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
    audio::decoded_block_cache_t* decoded_blocks,
    const audio::aligned_waveform_style_t* stable_style) -> bool {
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
    auto aligned = stable_style
        ? audio::project_composed_aligned_waveform(waveform,
            detail_analysis && detail_analysis->waveform ? &*detail_analysis->waveform : nullptr,
            *cache.marker_timing_index, request, decoded_blocks, *stable_style)
        : (decoded_blocks ? audio::project_aligned_waveform_samples(waveform,
            *cache.marker_timing_index, request, *decoded_blocks) : audio::aligned_waveform_result_t{});
    if (!stable_style && (!aligned || aligned.rows.empty()))
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

    if (!populate_cached_overview(flattened, cache, waveform, request)) return false;
    compose.waveform_overview_sample_rate = waveform.sample_rate;
    compose.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform_overview_sample_rate = waveform.sample_rate;
    return true;
}

} // namespace wormedit::sys
#endif
