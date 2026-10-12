#include "sys/waveform_view_projection.h"
#include <wormedit/audio/waveform_viewport.h>
#include "sys/bmson_waveform_projection.h"

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
    const auto& timing = *cache.timing.marker_timing_index;
    if (!cache.waveform.waveform_overview_key || *cache.waveform.waveform_overview_key != waveform.key ||
        cache.waveform.waveform_overview_identity != &waveform) {
        const auto overview = audio::project_waveform_overview(waveform, timing, request);
        if (!overview) return false;
        cache.waveform.waveform_overview_rows.clear();
        for (const auto& row : overview.rows)
            cache.waveform.waveform_overview_rows.push_back({row.audio_fraction,
                                                   std::clamp(row.absolute_peak, 0.0f, 1.0f)});
        cache.waveform.waveform_overview_key = waveform.key;
        cache.waveform.waveform_overview_identity = &waveform;
        static std::atomic<std::uint64_t> next_generation{1};
        cache.waveform.waveform_overview_generation = next_generation.fetch_add(1, std::memory_order_relaxed);
    }
    cache.waveform.waveform_overview_revision = cache.scene.revision;
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                               request.pixels_per_standard_measure;
    const auto begin = overview_fraction(waveform, timing,
        std::floor(request.viewport_scroll_pixels * rows_per_pixel),
        timing_boundary_side_t::before_delays, false);
    const auto end = overview_fraction(waveform, timing,
        std::ceil((request.viewport_scroll_pixels + request.viewport_height_pixels) * rows_per_pixel),
        timing_boundary_side_t::after_stops, true);
    if (!begin || !end) return false;
    for (const auto& row : cache.waveform.waveform_overview_rows) {
        if (!flattened.compose.waveform.overview_row.push_back(row)) {
            flattened.compose.waveform.overview_row.clear();
            return false;
        }
    }
    view.waveform_overview_rows = flattened.compose.waveform.overview_row.data();
    view.waveform_overview_row_count = flattened.compose.waveform.overview_row.size();
    view.waveform_overview_generation = cache.waveform.waveform_overview_generation;
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
void clear_frame_waveform_publication(flattened_workspace_t& flattened) {
    flattened.compose.waveform.row = {};
    flattened.compose.waveform.overview_row = {};
    flattened.compose.waveform.bar_row = {};
    flattened.compose.waveform.bar_mark = {};
    flattened.compose.waveform.bar_bookmark = {};
    auto& view = flattened.view.compose;
    view.waveform_rows = nullptr;
    view.waveform_row_count = 0;
    view.waveform_overview_rows = nullptr;
    view.waveform_overview_row_count = 0;
    view.bgm_bar_rows = nullptr;
    view.bgm_bar_row_count = 0;
    view.bgm_bar_begin_fraction = 0;
    view.bgm_bar_end_fraction = 1;
    view.bgm_bar_marks = nullptr;
    view.bgm_bar_mark_count = 0;
    view.bgm_bar_bookmarks = nullptr;
    view.bgm_bar_bookmark_count = 0;
    view.waveform_overview_generation = 0;
    view.waveform_overview_total_frames = 0;
    view.waveform_overview_sample_rate = 0;
    view.waveform_overview_visible_begin_fraction = 0;
    view.waveform_overview_visible_end_fraction = 0;
    view.waveform_beat_zero_visible = false;
    view.waveform_beat_zero_fraction = 0;
}
auto prepare_frame_waveform_storage(flattened_workspace_t& flattened) -> bool {
    auto* arena = flattened.arena();
    if (!arena) return false;
    auto rows = arena_array<WormUiComposeWaveformRowView>(*arena,
        WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY);
    auto overview = arena_array<WormUiComposeWaveformOverviewRowView>(*arena,
        WORM_UI_COMPOSE_WAVEFORM_OVERVIEW_CAPACITY);
    auto bar_rows = arena_array<WormUiComposeWaveformOverviewRowView>(*arena,
        WORM_UI_COMPOSE_BGM_BAR_ROW_CAPACITY);
    auto bar_marks = arena_array<WormUiComposeBgmBarMarkView>(*arena,
        WORM_UI_COMPOSE_BGM_BAR_MARK_CAPACITY);
    auto bar_bookmarks = arena_array<WormUiComposeBgmBarBookmarkView>(*arena,
        WORM_UI_COMPOSE_BGM_BAR_BOOKMARK_CAPACITY);
    if (rows.capacity() == 0 || overview.capacity() == 0 || bar_rows.capacity() == 0 ||
        bar_marks.capacity() == 0 || bar_bookmarks.capacity() == 0) return false;
    flattened.compose.waveform.row = rows;
    flattened.compose.waveform.overview_row = overview;
    flattened.compose.waveform.bar_row = bar_rows;
    flattened.compose.waveform.bar_mark = bar_marks;
    flattened.compose.waveform.bar_bookmark = bar_bookmarks;
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
    const audio::display_row_table_t& row_table;
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
        context.row_table, request, context.blocks, context.style);
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
                            frame_span<WormUiComposeWaveformRowView>& output) -> bool {
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
    for (std::size_t row = 0; row < count; ++row) {
        const auto world = first + static_cast<std::int64_t>(row);
        const auto& stored = ring.rows[cpu_row_slot(world, count)];
        if (!stored.present) continue;
        auto value = stored.value;
        value.y_pixels -= std::floor(request.viewport_scroll_pixels);
        if (value.y_pixels < 0 || value.y_pixels > request.viewport_height_pixels) continue;
        if (!output.push_back(value)) {
            output.clear();
            return false;
        }
    }
    return true;
}

void populate_direct_envelope(frame_span<WormUiComposeWaveformRowView>& output,
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
        for (const auto& row : aligned.rows) {
            if (!output.push_back({
                .y_pixels = row.y_pixels,
                .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                                  std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
                .rms = {std::clamp(row.rms[0], 0.0f, 1.0f),
                        std::clamp(row.rms[1], 0.0f, 1.0f)},
                .sample_point = row.sample_point,
                .samples = {row.samples[0], row.samples[1]},
            })) {
                output.clear();
                return;
            }
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
void orient_compose_waveform_rows(frame_span<WormUiComposeWaveformRowView>& rows,
                                  const WormUiComposeView& view) {
    for (auto& row : rows)
        row.y_pixels = compose_source_y(row.y_pixels, view.viewport_height_pixels, view.downscroll);
}
// Rows are cached per (source, window): projecting the whole song is the costly case.
auto refresh_bgm_bar_rows(compose_scene_cache_t::bgm_bar_cache_t& bar,
                          const audio::waveform_t& waveform, bgm_bar_range_t range) -> bool {
    if (bar.key && *bar.key == waveform.key && bar.identity == &waveform && bar.rows_range == range)
        return true;
    const auto projected = audio::project_waveform_range(waveform, range.begin, range.end,
                                                         WORM_UI_COMPOSE_BGM_BAR_ROW_CAPACITY);
    if (!projected) return false;
    bar.rows.clear();
    for (const auto& row : projected.rows)
        bar.rows.push_back({row.audio_fraction, std::clamp(row.absolute_peak, 0.0f, 1.0f)});
    bar.key = waveform.key;
    bar.identity = &waveform;
    bar.rows_range = range;
    return true;
}
void refresh_bgm_bar_marks(compose_scene_cache_t& cache, const audio::waveform_t& waveform,
                           bgm_bar_range_t range) {
    auto& bar = cache.waveform.bgm_bar;
    if (bar.marks_valid && bar.marks_revision == cache.scene.revision &&
        bar.marks_total_frames == waveform.total_frames && bar.marks_range == range) return;
    bar.marks.clear();
    bar.marks_valid = true;
    bar.marks_revision = cache.scene.revision;
    bar.marks_total_frames = waveform.total_frames;
    bar.marks_range = range;
    if (!cache.scene.cached_measure_index || !cache.timing.marker_timing_index) return;
    for (const auto& mark : bgm_bar_measure_marks(*cache.scene.cached_measure_index,
             *cache.timing.marker_timing_index, waveform.sample_rate, waveform.total_frames, range,
             WORM_UI_COMPOSE_BGM_BAR_MARK_CAPACITY))
        bar.marks.push_back({mark.audio_fraction, mark.measure});
}
// Beat bookmarks sit at a chart row, audio bookmarks at a song time; both land on the bar at
// their fraction of the song. Bookmarks outside the window are left out.
void populate_bgm_bar_bookmarks(flattened_workspace_t& flattened, const compose_scene_cache_t& cache,
                                const audio::waveform_t& waveform, bgm_bar_range_t range) {
    auto& view = flattened.view.compose;
    auto& out = flattened.compose.waveform.bar_bookmark;
    const auto add = [&](double fraction, const char* label, bool audio) {
        if (std::isfinite(fraction) && fraction >= range.begin && fraction <= range.end)
            (void)out.push_back({fraction, label, audio});
    };
    const auto frames_to_fraction = [&](long double seconds) {
        return static_cast<double>(seconds * waveform.sample_rate / waveform.total_frames);
    };
    if (cache.timing.marker_timing_index) {
        for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
            const auto& bookmark = view.beat_bookmarks[index];
            const auto time = timing_at(*cache.timing.marker_timing_index, bookmark.chart_row,
                                        timing_boundary_side_t::before_delays);
            if (!time.seconds || time.seconds->denominator == 0) continue;
            add(frames_to_fraction(static_cast<long double>(time.seconds->numerator) /
                                   time.seconds->denominator),
                bookmark.label, false);
        }
    }
    for (std::size_t index = 0; index < view.audio_bookmark_count; ++index) {
        const auto& bookmark = view.audio_bookmarks[index];
        add(frames_to_fraction(static_cast<long double>(bookmark.audio_milliseconds) / 1000.0L),
            bookmark.label, true);
    }
    view.bgm_bar_bookmarks = out.data();
    view.bgm_bar_bookmark_count = out.size();
}
// Publishes the BGM bar rows and measure marks for the requested song window.
void populate_bgm_bar(flattened_workspace_t& flattened, compose_scene_cache_t& cache,
                      const audio::waveform_t& waveform, bgm_bar_range_t requested,
                      std::optional<bgm_bar_pan_override_t>* pan_override,
                      std::optional<bgm_bar_window_capture_t>* window_capture) {
    const auto minimum_span = bgm_bar_minimum_span(waveform.total_frames);
    const auto& projection_view = flattened.view.compose;
    const bool keep_capture = window_capture && *window_capture &&
        bgm_bar_window_capture_matches(**window_capture, song_id_t{projection_view.project_id},
            chart_id_t{cache.scene.chart.value}, cache.scene.revision,
            asset_id_t{projection_view.selected_audio_asset_id}, waveform.total_frames,
            waveform.sample_rate);
    if (window_capture && *window_capture && !keep_capture) window_capture->reset();
    const bool keep_pan = pan_override && *pan_override &&
        bgm_bar_pan_override_matches(**pan_override,
            projection_view.waveform_overview_visible_begin_fraction,
            projection_view.waveform_overview_visible_end_fraction,
            song_id_t{projection_view.project_id}, chart_id_t{cache.scene.chart.value}, cache.scene.revision,
            asset_id_t{projection_view.selected_audio_asset_id});
    if (pan_override && *pan_override && !keep_pan) pan_override->reset();
    const auto range = keep_capture ? bgm_bar_fit((*window_capture)->range, minimum_span)
        : keep_pan ? bgm_bar_fit((*pan_override)->range, minimum_span)
        : bgm_bar_center_on_visible_span(requested,
            projection_view.waveform_overview_visible_begin_fraction,
            projection_view.waveform_overview_visible_end_fraction, minimum_span);
    if (!refresh_bgm_bar_rows(cache.waveform.bgm_bar, waveform, range)) return;
    refresh_bgm_bar_marks(cache, waveform, range);
    auto& waveform_views = flattened.compose.waveform;
    for (const auto& row : cache.waveform.bgm_bar.rows) {
        if (!waveform_views.bar_row.push_back(row)) {
            waveform_views.bar_row.clear();
            return;
        }
    }
    for (const auto& mark : cache.waveform.bgm_bar.marks) {
        if (!waveform_views.bar_mark.push_back(mark)) {
            waveform_views.bar_mark.clear();
            waveform_views.bar_row.clear();
            return;
        }
    }
    auto& view = flattened.view.compose;
    view.bgm_bar_rows = waveform_views.bar_row.data();
    view.bgm_bar_row_count = waveform_views.bar_row.size();
    view.bgm_bar_begin_fraction = range.begin;
    view.bgm_bar_end_fraction = range.end;
    view.bgm_bar_marks = waveform_views.bar_mark.data();
    view.bgm_bar_mark_count = waveform_views.bar_mark.size();
    populate_bgm_bar_bookmarks(flattened, cache, waveform, range);
}
} // namespace


void populate_audio_waveform_views(
    flattened_workspace_t& flattened,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    int window_width, const WormUiAudioWaveformViewportRequest* viewport_request) {
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
    auto viewport = wormedit::audio::fit_waveform_viewport(
        waveform.total_frames, static_cast<std::uint32_t>(pixel_width));
    if (audio.visible && viewport_request && viewport_request->valid &&
        viewport_request->asset_id == audio.selected_asset_id &&
        viewport_request->total_frames == waveform.total_frames &&
        viewport_request->pixel_width > 0 &&
        viewport_request->pixel_width <= WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY &&
        std::isfinite(viewport_request->frames_per_pixel) &&
        viewport_request->frames_per_pixel > 0) {
        viewport = wormedit::audio::clamp_waveform_viewport({
            .total_frames = waveform.total_frames,
            .begin_frame = viewport_request->begin_frame,
            .frames_per_pixel = viewport_request->frames_per_pixel,
            .pixel_width = viewport_request->pixel_width});
    }
    const auto projection = wormedit::audio::project_waveform_viewport(waveform, viewport);
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
    audio.waveform_begin_frame = viewport.begin_frame;
    audio.waveform_end_frame = wormedit::audio::waveform_viewport_end_frame(viewport);
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
    const audio::aligned_waveform_style_t* stable_style, bgm_bar_range_t bgm_bar_range,
    std::optional<bgm_bar_pan_override_t>* bgm_bar_pan_override,
    std::optional<bgm_bar_window_capture_t>* bgm_bar_window_capture) -> bool {
    cache.waveform.waveform_overview_total_frames = 0;
    cache.waveform.waveform_overview_sample_rate = 0;
    auto& compose = flattened.view.compose;
    clear_frame_waveform_publication(flattened);
    if (!compose.visible || !has_selected_audio_asset)
        return false;

    compose.waveform_analysis_pending = analysis_pending;
    if (!compose.scene_available || !interaction_state || !cache.timing.marker_timing_index ||
        !analysis || !analysis->waveform)
        return false;

    if (!prepare_frame_waveform_storage(flattened)) {
        compose.truncated = true;
        return false;
    }

    const auto& waveform = *analysis->waveform;
    const audio::aligned_waveform_request_t request{
        .viewport_scroll_pixels = interaction_state->viewport_scroll_pixels,
        .viewport_height_pixels = compose.viewport_height_pixels,
        .pixels_per_standard_measure = compose.pixels_per_standard_measure,
        .maximum_rows = WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY};
    (void)prepare_compose_display_row_table(cache,
        request, {compose.project_id, cache.scene.chart.value, cache.scene.revision});
    const auto window = audio::aligned_waveform_window(waveform, *cache.timing.marker_timing_index, request);
    const auto* selected_waveform = &waveform;
    if (window && detail_analysis && detail_analysis->waveform &&
        detail_analysis->waveform->key.source == waveform.key.source &&
        detail_analysis->waveform->key.begin_frame <= window->begin_frame &&
        detail_analysis->waveform->key.end_frame >= window->end_frame)
        selected_waveform = &*detail_analysis->waveform;
    bool cached_envelope = false;
    const auto style = envelope_style(stable_style, window);
    if (!style.sample_points) {
        const envelope_projection_context_t projection{cache.waveform.waveform_envelope_ring, waveform,
            detail_analysis && detail_analysis->waveform ? &*detail_analysis->waveform : nullptr,
            *cache.timing.marker_timing_index, request, decoded_blocks, style, cache.scene.revision,
            compose.project_id, cache.scene.chart.value, cache.waveform.waveform_row_table};
        cached_envelope = populate_cpu_envelope(projection, flattened.compose.waveform.row);
    } else {
        cache.waveform.waveform_envelope_ring.valid = false;
    }
    if (!cached_envelope)
        populate_direct_envelope(flattened.compose.waveform.row, waveform, selected_waveform,
                                 *cache.timing.marker_timing_index, request, decoded_blocks,
                                 detail_analysis, stable_style);
    orient_compose_waveform_rows(flattened.compose.waveform.row, compose);
    compose.waveform_rows = flattened.compose.waveform.row.data();
    compose.waveform_row_count = flattened.compose.waveform.row.size();

    if (!populate_cached_overview(flattened, cache, waveform, request)) return false;
    compose.waveform_overview_sample_rate = waveform.sample_rate;
    compose.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform.waveform_overview_total_frames = waveform.total_frames;
    cache.waveform.waveform_overview_sample_rate = waveform.sample_rate;
    populate_bgm_bar(flattened, cache, waveform, bgm_bar_range, bgm_bar_pan_override,
                     bgm_bar_window_capture);
    return true;
}

} // namespace wormedit::sys
#endif
