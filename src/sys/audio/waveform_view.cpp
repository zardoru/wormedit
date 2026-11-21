#include "sys/project/workspace_view.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/rational.h>
#include "sys/audio/waveform_view.h"
#include <wormedit/audio/waveform_viewport.h>
#include "sys/audio/bmson_waveform_view.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/aligned_waveform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>

namespace wormedit::sys {
namespace {
// Measure-mark sampling work is independent of owned output storage.
constexpr std::size_t bgm_bar_measure_mark_budget = 1024;
struct minimap_clock_t {
    std::uint64_t total_frames{};
    std::uint32_t sample_rate{};
    std::shared_ptr<const export_cut_frame_timeline_t> cuts;

    auto fraction_at(std::uint64_t frame) const -> double {
        return cuts ? cuts->collapsed_fraction(frame, total_frames).value_or(0.0)
                    : static_cast<double>(frame) / total_frames;
    }
    auto source_fraction_at(double fraction) const -> double {
        if (!cuts) return fraction;
        return static_cast<double>(cuts->source_frame_at_fraction(fraction, total_frames).value_or(0)) /
            total_frames;
    }
    auto duration_seconds() const -> double {
        const auto kept = cuts ? cuts->kept_frame_count(0, total_frames) : total_frames;
        return static_cast<double>(kept) / sample_rate;
    }
    auto key(const compose_scene_cache_t& cache) const -> compose_scene_cache_t::bgm_bar_cache_t::timeline_key_t {
        return {cache.scene.index.key().value_or(compose_scene_source_key_t{}),
                total_frames, sample_rate, cuts};
    }
};

auto overview_fraction(const minimap_clock_t& clock, const timing_index_t& timing,
                       long double row, timing_boundary_side_t side, bool round_up)
    -> std::optional<double> {
    row = std::clamp(row, 0.0L, static_cast<long double>(timing.total_rows));
    const auto time = timing.query_seconds(static_cast<std::uint64_t>(row), side);
    if (!time || time->value.denominator == 0) return std::nullopt;
    const auto frames = static_cast<long double>(time->value.numerator) * clock.sample_rate /
                        time->value.denominator;
    const auto bounded = std::clamp(round_up ? std::ceil(frames) : std::floor(frames),
                                   0.0L, static_cast<long double>(clock.total_frames));
    return clock.fraction_at(static_cast<std::uint64_t>(bounded));
}
auto build_waveform_overview(const audio::waveform_t& waveform, const timing_index_t& timing,
                             const audio::aligned_waveform_request_t& request, bool& succeeded,
                             compose_scene_cache_t::waveform_state_t::overview_data_t& data) -> bool {
    data.cut_frames.reset();
    if (request.cut_axis) {
        auto frames = export_cut_frame_timeline_t::from(request.cut_axis->timeline(), waveform.sample_rate);
        if (!frames.ok()) { succeeded = false; return false; }
        data.cut_frames = std::make_shared<const export_cut_frame_timeline_t>(std::move(frames.value));
    }
    const auto overview = data.cut_frames
        ? audio::waveform_overview_view_result_t::from(waveform, *data.cut_frames, 0, 1)
        : audio::waveform_overview_view_result_t::from(waveform, timing, request);
    succeeded = !overview.status.has_failure();
    if (!succeeded) return false;
    data.rows.clear();
    for (const auto& row : overview.rows)
        data.rows.push_back({row.audio_fraction, std::clamp(row.absolute_peak, 0.0f, 1.0f)});
    static std::atomic<std::uint64_t> next_generation{1};
    data.generation = next_generation.fetch_add(1, std::memory_order_relaxed);
    return true;
}
auto publish_overview_geometry(wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache,
                               const minimap_clock_t& clock,
                               const audio::aligned_waveform_request_t& request) -> bool {
    if (clock.total_frames == 0 || clock.sample_rate == 0 ||
        !std::isfinite(request.viewport_scroll_pixels) ||
        !std::isfinite(request.viewport_height_pixels) ||
        !std::isfinite(request.pixels_per_standard_measure) ||
        request.viewport_height_pixels <= 0 || request.pixels_per_standard_measure <= 0)
        return false;
    const auto& timing = *cache.timing.marker_timing_index;
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                               request.pixels_per_standard_measure;
    const auto source = [&](long double row) { return request.cut_axis ? request.cut_axis->source_row(row) : row; };
    const auto begin = overview_fraction(clock, timing,
        source(std::floor(request.viewport_scroll_pixels * rows_per_pixel)),
        timing_boundary_side_t::before_delays, false);
    const auto end = overview_fraction(clock, timing,
        source(std::ceil((request.viewport_scroll_pixels + request.viewport_height_pixels) * rows_per_pixel)),
        timing_boundary_side_t::after_stops, true);
    if (!begin || !end) return false;
    view.waveform_overview_visible_begin_fraction = *begin;
    view.waveform_overview_visible_end_fraction = *end;
    view.waveform_cut_frames = cache.waveform.waveform_cut_frames = clock.cuts;
    view.waveform_overview_sample_rate = cache.waveform.waveform_overview_sample_rate = clock.sample_rate;
    view.waveform_overview_total_frames = cache.waveform.waveform_overview_total_frames = clock.total_frames;
    return true;
}

auto populate_cached_overview(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                              const audio::waveform_t& waveform,
                              const audio::aligned_waveform_request_t& request) -> bool {
    auto& view = flattened.view.compose;
    const auto& timing = *cache.timing.marker_timing_index;
    bool succeeded = true;
    const auto& overview = cache.waveform.overview.get_or_build(
        std::tuple<const audio::waveform_key_t&, const void*>{waveform.key, &waveform},
        std::bind_front(build_waveform_overview, std::cref(waveform), std::cref(timing),
                        std::cref(request), std::ref(succeeded)));
    const minimap_clock_t clock{waveform.total_frames, waveform.sample_rate, overview.cut_frames};
    if (!succeeded || !publish_overview_geometry(view, cache, clock, request)) return false;
    view.waveform_overview_rows.assign(overview.rows.begin(), overview.rows.end());
    view.waveform_overview_generation = overview.generation;
    const auto zero = timing.query_seconds(0, timing_boundary_side_t::before_delays);
    const auto zero_frame = zero ? to_long_double(zero->value) * clock.sample_rate : -1.0L;
    view.waveform_beat_zero_visible = std::isfinite(zero_frame) && zero_frame >= 0 && zero_frame <= clock.total_frames;
    view.waveform_beat_zero_fraction = view.waveform_beat_zero_visible
        ? (clock.cuts ? clock.fraction_at(static_cast<std::uint64_t>(zero_frame))
                      : static_cast<double>(zero_frame / clock.total_frames)) : 0.0;
    return true;
}
void clear_frame_waveform_publication(frontend_workspace_view_t& flattened) {
    auto& view = flattened.view.compose;
    view.waveform_cut_frames.reset();
    view.waveform_rows.clear();
    view.waveform_overview_rows.clear();
    view.bgm_bar_rows.clear();
    view.bgm_bar_nps.clear();
    view.bgm_bar_peak_nps = 0;
    view.bgm_bar_chart_timeline = false;
    view.bgm_bar_begin_fraction = 0;
    view.bgm_bar_end_fraction = 1;
    view.bgm_bar_marks.clear();
    view.bgm_bar_bookmarks.clear();
    view.waveform_overview_generation = 0;
    view.waveform_overview_total_frames = 0;
    view.waveform_overview_sample_rate = 0;
    view.waveform_overview_visible_begin_fraction = 0;
    view.waveform_overview_visible_end_fraction = 0;
    view.waveform_beat_zero_visible = false;
    view.waveform_beat_zero_fraction = 0;
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
    const auto builds = ring.build_count();
    const auto geometry = envelope_projection_geometry_t::from(
        {.document = {.project = wormedit::song_id_t{context.project}, .revision = context.revision},
         .chart = wormedit::chart_id_t{context.chart}},
        0, 0, context.request.pixels_per_standard_measure, context.style.detail_level, count);
    static_cast<void>(ring.get_or_build(envelope_projection_lookup_t{context.overview.key, &context.overview, geometry},
        [count](auto& data) { data.rows.assign(count, {}); },
        [count](const auto& data) { return data.rows.size() == count; }));
    const bool reset = ring.build_count() != builds;
    const auto stats = context.blocks ? context.blocks->stats() : audio::block_cache_stats_t{};
    const bool changed = reset || ring.value().detail_identity != context.detail ||
        (context.detail && ring.value().detail_key != context.detail->key) ||
        ring.value().availability_entries != stats.entries || ring.value().availability_frames != stats.cached_frames ||
        ring.value().availability_evictions != stats.evictions;
    ring.value().detail_identity = context.detail;
    if (context.detail) ring.value().detail_key = context.detail->key;
    else ring.value().detail_key.reset();
    ring.value().availability_entries = stats.entries;
    ring.value().availability_frames = stats.cached_frames;
    ring.value().availability_evictions = stats.evictions;
    ring.value().projected_rows = ring.value().reused_rows = ring.value().projection_calls = 0;
    return changed;
}
void build_cpu_envelope_range_view(const envelope_projection_context_t& context,
                                std::int64_t first, std::size_t count) {
    auto& ring = context.ring;
    auto request = context.request;
    request.viewport_scroll_pixels = static_cast<double>(first);
    request.viewport_height_pixels = static_cast<double>(count);
    request.maximum_rows = audio::maximum_aligned_waveform_rows;
    const auto projected_data = audio::aligned_waveform_view_result_t::from(context.overview, context.detail,
        context.row_table, request, context.blocks, context.style);
    ++ring.value().projection_calls;
    ring.value().projected_rows += count;
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = first + static_cast<std::int64_t>(index);
        ring.value().rows[cpu_row_slot(world, ring.value().rows.size())] = {world, {}, false, false};
    }
    const bool incomplete = context.style.detail_level && projected_data.overview_spans != 0;
    for (const auto& row : projected_data.rows) {
        const auto world = first + static_cast<std::int64_t>(std::floor(row.y_pixels));
        if (world < first || world >= first + static_cast<std::int64_t>(count)) continue;
        auto& stored = ring.value().rows[cpu_row_slot(world, ring.value().rows.size())];
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
    const auto& row = ring.value().rows[cpu_row_slot(world, ring.value().rows.size())];
    return row.world_row != world || (row.incomplete && changed);
}
auto populate_cpu_envelope(const envelope_projection_context_t& context,
                            std::vector<wormedit::ui::compose_waveform_row_view_t>& output) -> bool {
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
        if (!cpu_row_needs_projection(ring, world, changed)) { ++ring.value().reused_rows; ++index; continue; }
        const auto begin = index++;
        while (index < count && cpu_row_needs_projection(ring, first + static_cast<std::int64_t>(index), changed))
            ++index;
        build_cpu_envelope_range_view(context, first + static_cast<std::int64_t>(begin), index - begin);
    }
    for (std::size_t row = 0; row < count; ++row) {
        const auto world = first + static_cast<std::int64_t>(row);
        const auto& stored = ring.value().rows[cpu_row_slot(world, count)];
        if (!stored.present) continue;
        auto value = stored.value;
        value.y_pixels -= std::floor(request.viewport_scroll_pixels);
        if (value.y_pixels < 0 || value.y_pixels > request.viewport_height_pixels) continue;
        output.push_back(value);
    }
    return true;
}

struct direct_envelope_context_t {
    const audio::waveform_t& waveform;
    const audio::waveform_t* selected_waveform;
    const timing_index_t& timing;
    audio::aligned_waveform_request_t request;
    audio::decoded_block_cache_t* decoded_blocks;
    const audio::waveform_analysis_result_t* detail_analysis;
    const audio::aligned_waveform_style_t* stable_style;
};
void populate_direct_envelope(std::vector<wormedit::ui::compose_waveform_row_view_t>& output,
                              const direct_envelope_context_t& context) {
    const auto& [waveform, selected_waveform, timing, request, decoded_blocks, detail_analysis,
                 stable_style] = context;
    audio::aligned_waveform_view_result_t aligned;
    aligned = stable_style
        ? audio::aligned_waveform_view_result_t::from(waveform,
            detail_analysis && detail_analysis->waveform ? &*detail_analysis->waveform : nullptr,
            timing, request, decoded_blocks, *stable_style)
        : (decoded_blocks ? audio::aligned_waveform_view_result_t::from(waveform,
            timing, request, *decoded_blocks) : audio::aligned_waveform_view_result_t{});
    if (!stable_style && (aligned.status.has_failure() || aligned.rows.empty()))
        aligned = audio::aligned_waveform_view_result_t::from(*selected_waveform, timing, request);
    if (!aligned.status.has_failure()) {
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
void populate_cached_direct_envelope(frontend_workspace_view_t& flattened,
                                      compose_scene_cache_t& cache,
                                      const direct_envelope_context_t& context) {
    const auto* detail = context.detail_analysis && context.detail_analysis->waveform
        ? &*context.detail_analysis->waveform : nullptr;
    const auto mutation_revision = context.decoded_blocks ? context.decoded_blocks->mutation_revision() : 0;
    const compose_scene_cache_t::waveform_state_t::direct_projection_lookup_t key{
        cache.stamp(), flattened.query_session_generation, context.timing.source_fingerprint,
        {&context.waveform, detail, context.selected_waveform, &context.timing, context.decoded_blocks},
        {context.request.viewport_scroll_pixels, context.request.viewport_height_pixels,
         context.request.pixels_per_standard_measure},
        mutation_revision,
        context.stable_style != nullptr, context.stable_style ? *context.stable_style : audio::aligned_waveform_style_t{},
        context.waveform.key, detail ? detail->key : context.waveform.key,
        context.selected_waveform->key};
    const auto& rows = cache.waveform.direct_projection.get_or_build(key,
        [&context](std::vector<ui::compose_waveform_row_view_t>& rows) {
            rows.clear();
            populate_direct_envelope(rows, context);
        });
    flattened.view.compose.waveform_rows.assign(rows.begin(), rows.end());
}
auto envelope_style(const audio::aligned_waveform_style_t* playback,
                     std::optional<audio::aligned_waveform_window_t> window)
    -> audio::aligned_waveform_style_t {
    if (playback) return *playback;
    if (!window) return {};
    return {.sample_points = window->frames_per_pixel < 1,
            .detail_level = window->frames_per_pixel < 256};
}
void orient_compose_waveform_rows(std::vector<wormedit::ui::compose_waveform_row_view_t>& rows,
                                  const wormedit::ui::compose_view_t& view) {
    for (auto& row : rows)
        row.y_pixels = compose_source_y(row.y_pixels, view.viewport_height_pixels, view.downscroll);
}
// Rows are cached per (source, window): projecting the whole song is the costly case.
auto build_bgm_bar_rows(const audio::waveform_t& waveform, bgm_bar_range_t range,
                        const export_cut_frame_timeline_t* frames, bool& succeeded,
                        std::vector<wormedit::ui::compose_waveform_overview_row_view_t>& rows) -> bool {
    const auto projected = frames
        ? audio::waveform_overview_view_result_t::from(waveform, *frames, range.begin, range.end, audio::maximum_waveform_range_rows)
        : audio::waveform_overview_view_result_t::from(waveform, range.begin, range.end, audio::maximum_waveform_range_rows);
    succeeded = !projected.status.has_failure();
    if (!succeeded) return false;
    rows.clear();
    for (const auto& row : projected.rows)
        rows.push_back({row.audio_fraction, std::clamp(row.absolute_peak, 0.0f, 1.0f)});
    return true;
}
auto refresh_bgm_bar_rows(compose_scene_cache_t::bgm_bar_cache_t& bar,
                          const audio::waveform_t& waveform, bgm_bar_range_t range, const export_cut_frame_timeline_t* frames) -> bool {
    bool succeeded = true;
    static_cast<void>(bar.rows.get_or_build(
        std::tuple<const audio::waveform_key_t&, const void*, bgm_bar_range_t>{waveform.key, &waveform, range},
        std::bind_front(build_bgm_bar_rows, std::cref(waveform), range, frames, std::ref(succeeded))));
    return succeeded;
}
void build_bgm_bar_marks(const compose_scene_cache_t& cache, const minimap_clock_t& clock,
                         bgm_bar_range_t range, std::vector<wormedit::ui::compose_bgm_bar_mark_view_t>& marks) {
    marks.clear();
    if (!cache.scene.cached_measure_index || !cache.timing.marker_timing_index) return;
    const bgm_bar_range_t source_range{
        clock.source_fraction_at(range.begin), clock.source_fraction_at(range.end)};
    for (const auto& mark : bgm_bar_measure_marks(*cache.scene.cached_measure_index,
             *cache.timing.marker_timing_index, clock.sample_rate, clock.total_frames, source_range,
             bgm_bar_measure_mark_budget)) {
        const auto frame = static_cast<std::uint64_t>(std::round(static_cast<long double>(mark.audio_fraction) * clock.total_frames));
        if (clock.cuts && clock.cuts->contains(frame)) continue;
        const auto fraction = clock.cuts ? clock.fraction_at(frame) : mark.audio_fraction;
        marks.push_back({fraction, mark.measure});
    }
}
void refresh_bgm_bar_marks(compose_scene_cache_t& cache, const minimap_clock_t& clock,
                           bgm_bar_range_t range) {
    static_cast<void>(cache.waveform.bgm_bar.marks.get_or_build(
        std::tuple{clock.key(cache), range},
        std::bind_front(build_bgm_bar_marks, std::cref(cache), std::cref(clock), range)));
}
// Beat bookmarks sit at a chart row, audio bookmarks at a song time; both land on the bar at
// their fraction of the song. Bookmarks outside the window are left out.
void populate_bgm_bar_bookmarks(frontend_workspace_view_t& flattened, const compose_scene_cache_t& cache,
                                const minimap_clock_t& clock, bgm_bar_range_t range) {
    auto& view = flattened.view.compose;
    auto& out = view.bgm_bar_bookmarks;
    out.clear();
    const auto add = [&](double fraction, std::string_view label, bool audio) {
        if (std::isfinite(fraction) && fraction >= range.begin && fraction <= range.end)
            (void)out.push_back({fraction, label, audio});
    };
    // A bookmark inside a cut, its begin included and its end excluded, is hidden here and in
    // Compose; one on the cut's end stays and sits on the seam. Beat bookmarks decide by chart
    // row, which is exact; audio bookmarks by song frame.
    const auto frames_to_fraction = [&](long double seconds, bool hide_inside_cut) -> std::optional<double> {
        if (!clock.cuts) return static_cast<double>(seconds * clock.sample_rate / clock.total_frames);
        const auto frame = static_cast<std::uint64_t>(std::clamp(std::round(seconds * clock.sample_rate), 0.0L,
                                                                  static_cast<long double>(clock.total_frames)));
        if (hide_inside_cut && clock.cuts->contains(frame)) return std::nullopt;
        return clock.fraction_at(frame);
    };
    if (cache.timing.marker_timing_index) {
        for (std::size_t index = 0; index < view.beat_bookmarks.size(); ++index) {
            const auto& bookmark = view.beat_bookmarks[index];
            const auto time = cache.timing.marker_timing_index->query_seconds(bookmark.chart_row,
                                        timing_boundary_side_t::before_delays);
            if (!time || time->value.denominator == 0) continue;
            if (view.cut_axis && view.cut_axis->contains_row(bookmark.chart_row)) continue;
            if (const auto fraction = frames_to_fraction(wormedit::to_long_double(time->value), false))
                add(*fraction, bookmark.label, false);
        }
    }
    for (std::size_t index = 0; index < view.audio_bookmarks.size(); ++index) {
        const auto& bookmark = view.audio_bookmarks[index];
        if (const auto fraction = frames_to_fraction(static_cast<long double>(bookmark.audio_milliseconds) / 1000.0L, true))
            add(*fraction, bookmark.label, true);
    }
}
auto resolve_minimap_window(const wormedit::ui::compose_view_t& view, const compose_scene_cache_t& cache,
                      const minimap_clock_t& clock, bgm_bar_range_t requested,
                      std::optional<bgm_bar_pan_override_t>* pan_override,
                      std::optional<bgm_bar_window_capture_t>* window_capture) -> bgm_bar_range_t {
    const auto minimum_span = bgm_bar_minimum_span(clock.total_frames);
    const bool keep_capture = window_capture && *window_capture &&
        bgm_bar_window_capture_matches(**window_capture, song_id_t{view.project_id.value},
            chart_id_t{cache.stamp().chart.value}, cache.stamp().document.revision,
            asset_id_t{view.selected_audio_asset_id.value}, clock.total_frames,
            clock.sample_rate);
    if (window_capture && *window_capture && !keep_capture) window_capture->reset();
    const bool keep_pan = pan_override && *pan_override &&
        bgm_bar_pan_override_matches(**pan_override,
            view.waveform_overview_visible_begin_fraction,
            view.waveform_overview_visible_end_fraction,
            song_id_t{view.project_id.value}, chart_id_t{cache.stamp().chart.value}, cache.stamp().document.revision,
            asset_id_t{view.selected_audio_asset_id.value});
    if (pan_override && *pan_override && !keep_pan) pan_override->reset();
    return keep_capture ? bgm_bar_fit((*window_capture)->range, minimum_span)
        : keep_pan ? bgm_bar_fit((*pan_override)->range, minimum_span)
        : bgm_bar_center_on_visible_span(requested,
            view.waveform_overview_visible_begin_fraction,
            view.waveform_overview_visible_end_fraction, minimum_span);
}

void publish_minimap_window(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                            const minimap_clock_t& clock, bgm_bar_range_t range) {
    refresh_bgm_bar_marks(cache, clock, range);
    auto& view = flattened.view.compose;
    const auto& marks = cache.waveform.bgm_bar.marks.value();
    view.bgm_bar_marks.assign(marks.begin(), marks.end());
    view.bgm_bar_begin_fraction = range.begin;
    view.bgm_bar_end_fraction = range.end;
    populate_bgm_bar_bookmarks(flattened, cache, clock, range);
}

auto chart_minimap_clock(compose_scene_cache_t& cache, const audio::aligned_waveform_request_t& request)
    -> std::optional<minimap_clock_t> {
    const auto& timing = *cache.timing.marker_timing_index;
    const auto end = timing.seconds_at(timing.total_rows, timing_boundary_side_t::after_stops);
    if (!end) return std::nullopt;
    constexpr auto rate = audio::output_sample_rate;
    const auto end_frame = sample_frame_at(*end.seconds, rate, sample_rounding_t::ceil);
    if (!end_frame || *end_frame.frame == 0 ||
        *end_frame.frame >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    minimap_clock_t clock{*end_frame.frame + 1, rate};
    if (request.cut_axis) {
        clock.cuts = cache.waveform.bgm_bar.chart_timeline_cuts.get_or_build(request.cut_axis, [&] {
            auto cuts = export_cut_frame_timeline_t::from(request.cut_axis->timeline(), rate);
            return cuts.ok() ? std::make_shared<const export_cut_frame_timeline_t>(std::move(cuts.value))
                             : nullptr;
        });
        if (!clock.cuts || clock.duration_seconds() == 0.0) return std::nullopt;
    }
    return clock;
}

auto minimap_row_seconds(std::uint64_t row, const timing_index_t& timing, const minimap_clock_t& clock)
    -> std::optional<double> {
    const auto time = timing.seconds_at(row);
    if (!time || time.inside_warp) return std::nullopt;
    const auto seconds = to_double(*time.seconds);
    const auto frame = to_long_double(*time.seconds) * clock.sample_rate;
    if (frame < 0 || frame >= clock.total_frames) return std::nullopt;
    if (!clock.cuts) return seconds;
    const auto source_frame = sample_frame_at(*time.seconds, clock.sample_rate).frame;
    if (!source_frame || clock.cuts->contains(*source_frame)) return std::nullopt;
    return static_cast<double>(clock.cuts->collapsed_frame(*source_frame)) / clock.sample_rate;
}

auto build_minimap_note_density(std::span<const editor_field_scene_note_source_t> notes,
                                const timing_index_t& timing, const minimap_clock_t& clock)
    -> compose_scene_cache_t::bgm_bar_cache_t::note_density_t {
    compose_scene_cache_t::bgm_bar_cache_t::note_density_t density;
    auto& heads = density.head_seconds;
    heads.reserve(notes.size());
    std::optional<std::uint64_t> row;
    std::optional<double> seconds;
    std::vector<lane_id_t> counted_lanes;
    for (const auto& note : notes) {
        if (!is_note_kind_playable(note.kind)) continue;
        if (row != note.begin_row) {
            row = note.begin_row;
            seconds = minimap_row_seconds(*row, timing, clock);
            counted_lanes.clear();
        }
        // Fused BMSON events on one lane contribute one playable head.
        if (!seconds || std::ranges::find(counted_lanes, note.lane) != counted_lanes.end()) continue;
        counted_lanes.push_back(note.lane);
        heads.push_back(*seconds);
    }
    // Scene heads are ordered by row. The timing and cut clocks preserve that order.
    std::size_t begin = 0;
    for (std::size_t end = 0; end < heads.size(); ++end) {
        while (heads[end] - heads[begin] >= 1.0) ++begin;
        density.peak = std::max(density.peak, static_cast<std::uint32_t>(end - begin + 1));
    }
    return density;
}

void sample_minimap_note_density(std::span<const double> heads, double duration, bgm_bar_range_t range,
                                 std::vector<std::uint32_t>& samples) {
    samples.resize(wormedit::ui::compose_bgm_bar_nps_sample_count);
    // Use a centered, half-open second on the song clock even at the edge of a zoomed window.
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto center_seconds = std::lerp(range.begin, range.end,
            static_cast<double>(index) / (samples.size() - 1)) * duration;
        const auto begin = std::ranges::lower_bound(heads, center_seconds - 0.5);
        const auto end = std::lower_bound(begin, heads.end(), center_seconds + 0.5);
        samples[index] = static_cast<std::uint32_t>(end - begin);
    }
}

void publish_minimap_note_density(wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache,
                                  const minimap_clock_t& clock) {
    const auto duration = clock.duration_seconds();
    if (!view.bgm_bar_nps_visible || !cache.scene.index.value() || duration <= 0) return;
    auto& density = cache.waveform.bgm_bar.note_density.get_or_build(clock.key(cache),
        std::bind_front(build_minimap_note_density,
            std::span<const editor_field_scene_note_source_t>(cache.scene.index.value()->notes),
            std::cref(*cache.timing.marker_timing_index), std::cref(clock)));
    const bgm_bar_range_t range{view.bgm_bar_begin_fraction, view.bgm_bar_end_fraction};
    const auto& samples = density.samples.get_or_build(range,
        std::bind_front(sample_minimap_note_density, std::span<const double>(density.head_seconds), duration, range));
    view.bgm_bar_peak_nps = density.peak;
    view.bgm_bar_nps.assign(samples.begin(), samples.end());
}
} // namespace


void populate_audio_waveform_views(
    frontend_workspace_view_t& flattened,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    int window_width, const wormedit::ui::audio_waveform_viewport_request_t* viewport_request) {
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
                 static_cast<std::size_t>(wormedit::ui::audio_waveform_column_limit));
    auto viewport = wormedit::audio::fit_waveform_viewport(
        waveform.total_frames, static_cast<std::uint32_t>(pixel_width));
    if (audio.visible && viewport_request && viewport_request->valid &&
        viewport_request->asset_id == audio.selected_asset_id &&
        viewport_request->total_frames == waveform.total_frames &&
        viewport_request->pixel_width > 0 &&
        viewport_request->pixel_width <= wormedit::ui::audio_waveform_column_limit &&
        std::isfinite(viewport_request->frames_per_pixel) &&
        viewport_request->frames_per_pixel > 0) {
        viewport = wormedit::audio::clamp_waveform_viewport({
            .total_frames = waveform.total_frames,
            .begin_frame = viewport_request->begin_frame,
            .frames_per_pixel = viewport_request->frames_per_pixel,
            .pixel_width = viewport_request->pixel_width});
    }
    auto& projection = flattened.audio_waveform_projection;
    if (!projection.refill(waveform, viewport))
        return;

    audio.waveform_columns.resize(projection.columns.size());
    for (std::size_t i = 0; i < projection.columns.size(); ++i) {
        const auto& column = projection.columns[i];
        audio.waveform_columns[i] = {
            .minimum = {column.minimum[0], column.minimum[1]},
            .maximum = {column.maximum[0], column.maximum[1]},
            .absolute_peak = {column.absolute_peak[0], column.absolute_peak[1]},
            .rms = {column.rms[0], column.rms[1]},
        };
    }
    audio.waveform_available = true;
    audio.total_frames = waveform.total_frames;
    audio.waveform_begin_frame = viewport.begin_frame;
    audio.waveform_end_frame = wormedit::audio::waveform_viewport_end_frame(viewport);
    audio.sample_rate = waveform.sample_rate;

    auto& metadata = flattened.view.metadata;
    metadata.audio_waveform_available = true;
    metadata.audio_waveform_total_frames = waveform.total_frames;
    metadata.audio_waveform_sample_rate = waveform.sample_rate;
    metadata.audio_waveform_columns.assign(audio.waveform_columns.begin(), audio.waveform_columns.end());
}

auto populate_compose_waveform_projection(
    frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    bool has_selected_audio_asset,
    const wormedit::editor_interaction_state_t* interaction_state,
    const audio::waveform_analysis_result_t* detail_analysis,
    audio::decoded_block_cache_t* decoded_blocks,
    const audio::aligned_waveform_style_t* stable_style, bgm_bar_range_t bgm_bar_range,
    std::optional<bgm_bar_pan_override_t>* bgm_bar_pan_override,
    std::optional<bgm_bar_window_capture_t>* bgm_bar_window_capture) -> bool {
    cache.waveform.waveform_cut_frames.reset();
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

    const auto& waveform = *analysis->waveform;
    const audio::aligned_waveform_request_t request{
        .viewport_scroll_pixels = interaction_state->viewport_scroll_pixels,
        .viewport_height_pixels = compose.viewport_height_pixels,
        .pixels_per_standard_measure = compose.pixels_per_standard_measure,
        .maximum_rows = audio::maximum_aligned_waveform_rows,
        .cut_axis = interaction_state->cut_axis};
    (void)prepare_compose_display_row_table(cache,
        request, {compose.project_id.value, cache.stamp().chart.value, cache.stamp().document.revision});
    if (!populate_cached_overview(flattened, cache, waveform, request)) return false;
    cache.waveform.waveform_row_table.cut_frames = compose.waveform_cut_frames;
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
            *cache.timing.marker_timing_index, request, decoded_blocks, style, cache.stamp().document.revision,
            compose.project_id.value, cache.stamp().chart.value, cache.waveform.waveform_row_table};
        cached_envelope = populate_cpu_envelope(projection, compose.waveform_rows);
    } else {
        cache.waveform.waveform_envelope_ring.invalidate();
    }
    if (!cached_envelope)
        populate_cached_direct_envelope(flattened, cache,
            {waveform, selected_waveform, *cache.timing.marker_timing_index, request, decoded_blocks,
             detail_analysis, stable_style});
    orient_compose_waveform_rows(compose.waveform_rows, compose);

    const minimap_clock_t clock{waveform.total_frames, waveform.sample_rate, compose.waveform_cut_frames};
    const auto range = resolve_minimap_window(compose, cache, clock, bgm_bar_range, bgm_bar_pan_override,
                                              bgm_bar_window_capture);
    if (refresh_bgm_bar_rows(cache.waveform.bgm_bar, waveform, range, clock.cuts.get())) {
        const auto& rows = cache.waveform.bgm_bar.rows.value();
        compose.bgm_bar_rows.assign(rows.begin(), rows.end());
        publish_minimap_window(flattened, cache, clock, range);
    }
    return true;
}

void populate_compose_minimap_density(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
    const editor_interaction_state_t& interaction,
    bgm_bar_range_t requested, std::optional<bgm_bar_pan_override_t>* pan_override,
    std::optional<bgm_bar_window_capture_t>* window_capture) {
    auto& view = flattened.view.compose;
    if (!view.bgm_bar_reserved || !view.bgm_bar_nps_available || !view.scene_available ||
        !cache.timing.marker_timing_index) return;
    if (!view.bgm_bar_rows.empty()) {
        publish_minimap_note_density(view, cache,
            {view.waveform_overview_total_frames, view.waveform_overview_sample_rate, view.waveform_cut_frames});
        return;
    }

    const audio::aligned_waveform_request_t request{
        .viewport_scroll_pixels = interaction.viewport_scroll_pixels,
        .viewport_height_pixels = view.viewport_height_pixels,
        .pixels_per_standard_measure = view.pixels_per_standard_measure,
        .cut_axis = interaction.cut_axis};
    const auto clock = chart_minimap_clock(cache, request);
    if (!clock || !publish_overview_geometry(view, cache, *clock, request)) return;
    view.bgm_bar_chart_timeline = true;
    const auto range = resolve_minimap_window(view, cache, *clock, requested, pan_override, window_capture);
    publish_minimap_window(flattened, cache, *clock, range);
    publish_minimap_note_density(view, cache, *clock);
}

} // namespace wormedit::sys
#endif
