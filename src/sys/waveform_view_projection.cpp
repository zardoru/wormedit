#include "sys/waveform_view_projection.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/aligned_waveform.h>

#include <algorithm>
#include <atomic>
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
        cache.waveform_overview_identity != &waveform) {
        const auto overview = audio::project_waveform_overview(waveform, timing, request);
        if (!overview) return false;
        cache.waveform_overview_rows.clear();
        for (const auto& row : overview.rows)
            cache.waveform_overview_rows.push_back({row.audio_fraction,
                                                   std::clamp(row.absolute_peak, 0.0f, 1.0f)});
        cache.waveform_overview_key = waveform.key;
        cache.waveform_overview_identity = &waveform;
        static std::atomic<std::uint64_t> next_generation{1};
        cache.waveform_overview_generation = next_generation.fetch_add(1, std::memory_order_relaxed);
    }
    cache.waveform_overview_revision = cache.revision;
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
    view.waveform_overview_generation = cache.waveform_overview_generation;
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
struct envelope_projection_context_t {
    envelope_projection_ring_t& ring;
    const audio::waveform_t& overview;
    const audio::waveform_t* detail;
    const timing_index_t& timing;
    audio::aligned_waveform_request_t request;
    audio::decoded_block_cache_t* blocks;
    audio::aligned_waveform_style_t style;
    std::uint64_t revision{};
    std::uint64_t project{}, chart{};
};
auto cpu_row_slot(std::int64_t row, std::size_t count) -> std::size_t {
    const auto remainder = row % static_cast<std::int64_t>(count);
    return static_cast<std::size_t>(remainder < 0 ? remainder + static_cast<std::int64_t>(count) : remainder);
}
auto prepare_cpu_envelope(const envelope_projection_context_t& context, std::size_t count) -> bool {
    auto& ring = context.ring;
    const bool reset = !ring.valid || ring.rows.size() != count ||
        ring.source_key != context.overview.key || ring.source_identity != &context.overview ||
        ring.project != context.project || ring.chart != context.chart ||
        ring.revision != context.revision || ring.zoom != context.request.pixels_per_standard_measure ||
        ring.detail_level != context.style.detail_level;
    if (reset) ring.rows.assign(count, {});
    ring.valid = true;
    ring.source_key = context.overview.key;
    ring.source_identity = &context.overview;
    ring.revision = context.revision;
    ring.project = context.project;
    ring.chart = context.chart;
    ring.zoom = context.request.pixels_per_standard_measure;
    ring.detail_level = context.style.detail_level;
    const auto stats = context.blocks ? context.blocks->stats() : audio::block_cache_stats_t{};
    const bool changed = reset || ring.detail_identity != context.detail ||
        (context.detail && ring.detail_key != context.detail->key) ||
        ring.availability_entries != stats.entries || ring.availability_frames != stats.cached_frames ||
        ring.availability_evictions != stats.evictions;
    ring.detail_identity = context.detail;
    if (context.detail) ring.detail_key = context.detail->key;
    else ring.detail_key.reset();
    ring.availability_entries = stats.entries;
    ring.availability_frames = stats.cached_frames;
    ring.availability_evictions = stats.evictions;
    ring.projected_rows = ring.reused_rows = ring.projection_calls = 0;
    return changed;
}
void project_cpu_envelope_range(const envelope_projection_context_t& context,
                                std::int64_t first, std::size_t count) {
    auto& ring = context.ring;
    auto request = context.request;
    request.viewport_scroll_pixels = static_cast<double>(first);
    request.viewport_height_pixels = static_cast<double>(count);
    request.maximum_rows = audio::maximum_aligned_waveform_rows;
    auto projected = audio::project_composed_aligned_waveform(context.overview, context.detail,
        context.timing, request, context.blocks, context.style);
    ++ring.projection_calls;
    ring.projected_rows += count;
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = first + static_cast<std::int64_t>(index);
        ring.rows[cpu_row_slot(world, ring.rows.size())] = {world, {}, false, false};
    }
    const bool incomplete = context.style.detail_level && projected.overview_spans != 0;
    for (const auto& row : projected.rows) {
        const auto world = first + static_cast<std::int64_t>(std::floor(row.y_pixels));
        if (world < first || world >= first + static_cast<std::int64_t>(count)) continue;
        auto& stored = ring.rows[cpu_row_slot(world, ring.rows.size())];
        stored = {world, {.y_pixels = static_cast<double>(world) + .5,
            .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                              std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
            .rms = {std::clamp(row.rms[0], 0.0f, 1.0f), std::clamp(row.rms[1], 0.0f, 1.0f)},
            .sample_point = false, .samples = {}},
            true, incomplete};
    }
}
auto cpu_row_needs_projection(const envelope_projection_ring_t& ring, std::int64_t world,
                              bool changed) -> bool {
    const auto& row = ring.rows[cpu_row_slot(world, ring.rows.size())];
    return row.world_row != world || (row.incomplete && changed);
}
auto populate_cpu_envelope(const envelope_projection_context_t& context,
                            std::vector<WormUiComposeWaveformRowView>& output) -> bool {
    const auto& request = context.request;
    if (!std::isfinite(request.viewport_scroll_pixels) || std::abs(request.viewport_scroll_pixels) > 1e12 ||
        !std::isfinite(request.viewport_height_pixels) || request.viewport_height_pixels <= 0 ||
        request.viewport_height_pixels > audio::maximum_aligned_waveform_rows - 2) return false;
    const auto count = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels)) + 2;
    const auto first = static_cast<std::int64_t>(std::floor(request.viewport_scroll_pixels)) - 1;
    const bool changed = prepare_cpu_envelope(context, count);
    auto& ring = context.ring;
    std::size_t index = 0;
    while (index < count) {
        const auto world = first + static_cast<std::int64_t>(index);
        if (!cpu_row_needs_projection(ring, world, changed)) { ++ring.reused_rows; ++index; continue; }
        const auto begin = index++;
        while (index < count && cpu_row_needs_projection(ring, first + static_cast<std::int64_t>(index), changed))
            ++index;
        project_cpu_envelope_range(context, first + static_cast<std::int64_t>(begin), index - begin);
    }
    output.reserve(count);
    for (std::size_t row = 0; row < count; ++row) {
        const auto world = first + static_cast<std::int64_t>(row);
        const auto& stored = ring.rows[cpu_row_slot(world, count)];
        if (!stored.present) continue;
        auto value = stored.value;
        value.y_pixels -= std::floor(request.viewport_scroll_pixels);
        if (value.y_pixels < 0 || value.y_pixels > request.viewport_height_pixels) continue;
        output.push_back(value);
    }
    return true;
}

void populate_direct_envelope(std::vector<WormUiComposeWaveformRowView>& output,
    const audio::waveform_t& waveform, const audio::waveform_t* selected_waveform,
    const timing_index_t& timing, const audio::aligned_waveform_request_t& request,
    audio::decoded_block_cache_t* decoded_blocks,
    const audio::waveform_analysis_result_t* detail_analysis,
    const audio::aligned_waveform_style_t* stable_style) {
    audio::aligned_waveform_result_t aligned;
    aligned = stable_style
        ? audio::project_composed_aligned_waveform(waveform,
            detail_analysis && detail_analysis->waveform ? &*detail_analysis->waveform : nullptr,
            timing, request, decoded_blocks, *stable_style)
        : (decoded_blocks ? audio::project_aligned_waveform_samples(waveform,
            timing, request, *decoded_blocks) : audio::aligned_waveform_result_t{});
    if (!stable_style && (!aligned || aligned.rows.empty()))
        aligned = audio::project_aligned_waveform(*selected_waveform, timing, request);
    if (aligned) {
        output.reserve(aligned.rows.size());
        for (const auto& row : aligned.rows) {
            output.push_back({
                .y_pixels = row.y_pixels,
                .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                                  std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
                .rms = {std::clamp(row.rms[0], 0.0f, 1.0f),
                        std::clamp(row.rms[1], 0.0f, 1.0f)},
                .sample_point = row.sample_point,
                .samples = {row.samples[0], row.samples[1]},
            });
        }
    }
}
auto envelope_style(const audio::aligned_waveform_style_t* playback,
                     std::optional<audio::aligned_waveform_window_t> window)
    -> audio::aligned_waveform_style_t {
    if (playback) return *playback;
    if (!window) return {};
    return {.sample_points = window->frames_per_pixel < 1,
            .detail_level = window->frames_per_pixel < 256};
}
void orient_compose_waveform_rows(std::vector<WormUiComposeWaveformRowView>& rows,
                                  const WormUiComposeView& view) {
    for (auto& row : rows)
        row.y_pixels = compose_source_y(row.y_pixels, view.viewport_height_pixels, view.downscroll);
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
    flattened.compose.waveform.row.clear();
    compose.waveform_rows = nullptr;
    compose.waveform_row_count = 0;
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
    bool cached_envelope = false;
    const auto style = envelope_style(stable_style, window);
    if (!style.sample_points) {
        const envelope_projection_context_t projection{cache.waveform_envelope_ring, waveform,
            detail_analysis && detail_analysis->waveform ? &*detail_analysis->waveform : nullptr,
            *cache.marker_timing_index, request, decoded_blocks, style, cache.revision,
            compose.project_id, cache.chart.value};
        cached_envelope = populate_cpu_envelope(projection, flattened.compose.waveform.row);
    } else {
        cache.waveform_envelope_ring.valid = false;
    }
    if (!cached_envelope)
        populate_direct_envelope(flattened.compose.waveform.row, waveform, selected_waveform,
                                 *cache.marker_timing_index, request, decoded_blocks,
                                 detail_analysis, stable_style);
    orient_compose_waveform_rows(flattened.compose.waveform.row, compose);
    compose.waveform_rows = flattened.compose.waveform.row.data();
    compose.waveform_row_count = flattened.compose.waveform.row.size();

    if (!populate_cached_overview(flattened, cache, waveform, request)) return false;
    compose.waveform_overview_sample_rate = waveform.sample_rate;
    compose.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform_overview_sample_rate = waveform.sample_rate;
    return true;
}

} // namespace wormedit::sys
#endif
