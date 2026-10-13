#include <wormedit/core/rational.h>
#include "sys/compose_waveform_overlays.h"
#include "sys/frontend_geometry.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <tuple>
#include <utility>

namespace wormedit::sys {
namespace {


auto proposed_visible_source_rows(const wormedit::timing_index_t& original,
                                  const wormedit::timing_index_t& proposed, double scroll,
                                  double height, double pixels_per_measure)
    -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
    if (original.total_rows == 0 || proposed.total_rows == 0 ||
        original.divisions_per_standard_measure == 0 || pixels_per_measure <= 0.0)
        return std::nullopt;
    const auto pixels_per_row = pixels_per_measure / original.divisions_per_standard_measure;
    const auto first_visible = std::clamp(static_cast<long double>(scroll / pixels_per_row), 0.0L,
                                          static_cast<long double>(original.total_rows));
    const auto last_visible =
        std::clamp(static_cast<long double>((scroll + height) / pixels_per_row), 0.0L,
                   static_cast<long double>(original.total_rows));
    const auto first_time =
        original.seconds_at(static_cast<std::uint64_t>(std::floor(first_visible)),
                            wormedit::timing_boundary_side_t::before_delays);
    const auto last_time =
        original.seconds_at(static_cast<std::uint64_t>(std::ceil(last_visible)),
                            wormedit::timing_boundary_side_t::after_stops);
    if (!first_time || !last_time)
        return std::nullopt;
    const auto source_row = [&](wormedit::signed_rational_t seconds) -> std::optional<long double> {
        const auto position = proposed.row_at(seconds);
        if (position)
            return to_long_double(*position.absolute_row);
        const auto time = to_long_double(seconds);
        if (time < to_long_double(proposed.rows.front().before_delays_seconds))
            return 0.0L;
        if (time > to_long_double(proposed.rows.back().after_stops_seconds))
            return static_cast<long double>(proposed.total_rows);
        return std::nullopt;
    };
    const auto first = source_row(*first_time.seconds);
    const auto last = source_row(*last_time.seconds);
    if (!first || !last)
        return std::nullopt;
    const auto padding = std::ceil(16.0 / pixels_per_row) + 2.0;
    const auto begin = static_cast<std::uint64_t>(std::max(0.0L, std::floor(*first - padding)));
    const auto end = static_cast<std::uint64_t>(
        std::min(static_cast<long double>(proposed.total_rows), std::ceil(*last + padding)));
    return std::pair{
        std::min(begin, proposed.total_rows - 1),
        std::max(std::min(end, proposed.total_rows), std::min(begin, proposed.total_rows - 1) + 1)};
}

struct proposed_grid_context_t {
    const compose_chart_scope_t<>& scope;
    flattened_workspace_t& flattened;
    const compose_scene_cache_t& cache;
    const wormedit::timing_index_t& original;
    const wormedit::timing_index_t& proposed;
    const wormedit::editor_interaction_state_t& interaction;
    const wormedit::editor_field_scene_t& source_scene;
    std::pair<std::uint64_t, std::uint64_t> rows;

    auto view() const -> WormUiComposeView& { return flattened.view.compose; }
    auto preview_rects() -> std::vector<WormUiComposeTimingPreviewRectView>& {
        return flattened.compose.timing_preview_rect;
    }
    auto preview_markers() -> std::vector<WormUiTimingMarkerView>& {
        return flattened.compose.timing_preview_marker;
    }
    auto proposed_y(std::uint64_t row) const -> std::optional<double> {
        const auto projected = wormedit::proposed_timing_y_on_fixed_waveform(
            original, proposed, {row, {}}, interaction.viewport_scroll_pixels,
            view().pixels_per_standard_measure);
        if (!projected) return std::nullopt;
        return compose_source_y(*projected, view().viewport_height_pixels, scope.downscroll);
    }
    auto old_y(std::uint64_t row) const -> double {
        return compose_project_y(row, scope.pixels_per_row, interaction.viewport_scroll_pixels,
                                 view().viewport_height_pixels, scope.downscroll);
    }
    auto projected_geometry_y(double old_screen_y) const -> std::optional<double> {
        const auto source_y = compose_source_y(
            old_screen_y, scope.interaction_view.viewport_height_pixels, scope.downscroll);
        const auto row =
            (static_cast<long double>(source_y) + scope.interaction_view.viewport_scroll_pixels) *
            original.divisions_per_standard_measure / view().pixels_per_standard_measure;
        if (!std::isfinite(row) || row < 0.0L || row > original.total_rows)
            return std::nullopt;
        constexpr std::uint64_t fractional_scale = 1'000'000;
        auto whole = static_cast<std::uint64_t>(std::floor(row));
        auto fractional = static_cast<std::uint64_t>(
            std::llround((row - static_cast<long double>(whole)) * fractional_scale));
        if (fractional == fractional_scale) {
            ++whole;
            fractional = 0;
        }
        const auto projected = wormedit::proposed_timing_y_on_fixed_waveform(
            original, proposed, {whole, {fractional, fractional_scale}},
            interaction.viewport_scroll_pixels, view().pixels_per_standard_measure);
        if (!projected) return std::nullopt;
        return compose_source_y(*projected, view().viewport_height_pixels, scope.downscroll);
    }
};

void append_proposed_grid_lines(proposed_grid_context_t& projection) {
    auto& output = projection.flattened.compose.grid.proposed;
    output.reserve(projection.source_scene.grid_lines.size());
    for (const auto& line : projection.source_scene.grid_lines) {
        const auto projected = wormedit::proposed_timing_y_on_fixed_waveform(
            projection.original, projection.proposed, line.chart_row,
            projection.interaction.viewport_scroll_pixels,
            projection.view().pixels_per_standard_measure);
        if (!projected) continue;
        const auto y = compose_source_y(*projected, projection.view().viewport_height_pixels,
                                        projection.scope.downscroll);
        if (y < 0.0 || y > projection.view().viewport_height_pixels) continue;
        std::uint32_t measure_number{};
        if (line.kind == wormedit::editor_grid_line_kind_t::measure) {
            const auto& measures = projection.cache.scene.index->measures.entries;
            const auto found = std::ranges::lower_bound(
                measures, line.chart_row.whole_rows, {},
                &wormedit::measure_index_entry_t::absolute_begin_row);
            if (found != measures.end() && found->absolute_begin_row == line.chart_row.whole_rows)
                measure_number = static_cast<std::uint32_t>(found - measures.begin() + 1);
        }
        output.push_back({.y_pixels = y,
                          .kind = static_cast<std::uint8_t>(line.kind),
                          .emphasis = static_cast<std::uint8_t>(line.emphasis),
                          .measure_number = measure_number,
                          .chart_row = line.chart_row.fractional_rows.numerator == 0
                                           ? line.chart_row.whole_rows
                                           : 0});
    }
    projection.view().proposed_grid_lines = output.data();
    projection.view().proposed_grid_line_count = output.size();
}

void append_preview_rect(proposed_grid_context_t& projection, WormUiComposeRect bounds,
                         std::uint64_t row, std::uint8_t kind) {
    auto& rects = projection.preview_rects();
    if (rects.size() >= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY) {
        projection.view().truncated = true;
        return;
    }
    const auto y = projection.proposed_y(row);
    if (!y) return;
    const auto shift = *y - projection.old_y(row);
    if (std::abs(shift) < 0.5 || bounds.bottom + shift < 0.0 ||
        bounds.top + shift > projection.view().viewport_height_pixels) return;
    bounds.top += shift;
    bounds.bottom += shift;
    if (std::abs(bounds.top) > 10000000.0 || std::abs(bounds.bottom) > 10000000.0) return;
    rects.push_back({.bounds = bounds, .kind = kind});
}

void append_note_and_bgm_preview_rects(proposed_grid_context_t& projection) {
    auto& rects = projection.preview_rects();
    rects.reserve(projection.source_scene.note_endpoints.size() +
                  projection.source_scene.bgm_events.size());
    for (const auto& note : projection.source_scene.note_endpoints) {
        const auto source = projection.cache.scene.index->note_indices_by_id.find(note.note.value);
        if (source == projection.cache.scene.index->note_indices_by_id.end() || note.proposal_ghost)
            continue;
        const auto& indexed = projection.cache.scene.index->notes[source->second];
        append_preview_rect(projection, to_frontend_rect(note.bounds),
                            note.tail ? indexed.end_row : indexed.begin_row, 0);
    }
    for (const auto& event : projection.source_scene.bgm_events) {
        const auto row = projection.cache.scene.bgm_rows_by_id.find(event.event.value);
        if (row != projection.cache.scene.bgm_rows_by_id.end())
            append_preview_rect(projection, to_frontend_rect(event.bounds), row->second, 1);
    }
}

void append_hold_preview_rect(proposed_grid_context_t& projection,
                              const wormedit::editor_field_hold_body_geometry_t& hold) {
    auto& rects = projection.preview_rects();
    if (rects.size() >= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY) {
        projection.view().truncated = true;
        return;
    }
    const auto top = projection.projected_geometry_y(hold.bounds.top);
    const auto bottom = projection.projected_geometry_y(hold.bounds.bottom);
    const auto old_top_source =
        compose_source_y(hold.bounds.top, projection.scope.interaction_view.viewport_height_pixels,
                         projection.scope.downscroll);
    const auto old_bottom_source = compose_source_y(
        hold.bounds.bottom, projection.scope.interaction_view.viewport_height_pixels,
        projection.scope.downscroll);
    const auto old_top =
        compose_source_y(old_top_source + projection.scope.interaction_view.viewport_scroll_pixels -
                             projection.interaction.viewport_scroll_pixels,
                         projection.view().viewport_height_pixels, projection.scope.downscroll);
    const auto old_bottom = compose_source_y(
        old_bottom_source + projection.scope.interaction_view.viewport_scroll_pixels -
            projection.interaction.viewport_scroll_pixels,
        projection.view().viewport_height_pixels, projection.scope.downscroll);
    if (!top || !bottom || std::abs(*bottom - *top) < 0.5 ||
        (std::abs(*top - old_top) < 0.5 && std::abs(*bottom - old_bottom) < 0.5) ||
        std::max(*top, *bottom) < 0.0 ||
        std::min(*top, *bottom) > projection.view().viewport_height_pixels) return;
    auto bounds = to_frontend_rect(hold.bounds);
    bounds.top = std::min(*top, *bottom);
    bounds.bottom = std::max(*top, *bottom);
    if (std::abs(bounds.top) > 10000000.0 || std::abs(bounds.bottom) > 10000000.0) return;
    rects.push_back({.bounds = bounds, .kind = 2});
}

void append_hold_preview_rects(proposed_grid_context_t& projection) {
    for (const auto& hold : projection.source_scene.hold_bodies) {
        if (projection.preview_rects().size() >= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY) {
            projection.view().truncated = true;
            break;
        }
        append_hold_preview_rect(projection, hold);
    }
    projection.view().timing_preview_rects = projection.preview_rects().data();
    projection.view().timing_preview_rect_count = projection.preview_rects().size();
}

void append_proposed_marker(proposed_grid_context_t& projection,
                            wormedit::local_position_t position, std::uint64_t id,
                            std::size_t source_order, std::size_t storage_order,
                            WormUiTimingLaneKind lane) {
    auto& markers = projection.preview_markers();
    if (markers.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
        projection.view().truncated = true;
        return;
    }
    const auto row = (projection.cache.scene.index->measures).row_of(position);
    if (!row || *row < projection.rows.first || *row > projection.rows.second) return;
    const auto y = projection.proposed_y(*row);
    if (!y || std::abs(*y - projection.old_y(*row)) < 0.5 || *y < 0.0 ||
        *y > projection.view().viewport_height_pixels) return;
    markers.push_back({.object_id = id,
                       .source_order = static_cast<std::uint64_t>(source_order),
                       .storage_order = static_cast<std::uint64_t>(storage_order),
                       .marker_y_pixels = *y,
                       .marker_left_fraction = 0.0,
                       .marker_right_fraction = 1.0,
                       .chart_row = *row,
                       .lane = lane});
}

template <typename Events>
void append_standard_event_markers(proposed_grid_context_t& projection,
                                   const Events& events, WormUiTimingLaneKind lane) {
    for (std::size_t index = 0; index < events.size(); ++index)
        append_proposed_marker(projection, events[index].position, events[index].id.value,
                               events[index].provenance.source_order, index, lane);
}

void append_pause_markers(proposed_grid_context_t& projection) {
    const auto& pauses = projection.scope.chart_scope.chart.pauses;
    for (std::size_t index = 0; index < pauses.size(); ++index) {
        const auto lane = pauses[index].kind == wormedit::pause_kind_t::stop
                              ? WORM_UI_TIMING_LANE_STOP : WORM_UI_TIMING_LANE_DELAY;
        append_proposed_marker(projection, pauses[index].position, pauses[index].id.value,
                               pauses[index].provenance.source_order, index, lane);
    }
}

void append_warp_markers(proposed_grid_context_t& projection) {
    const auto& warps = projection.scope.chart_scope.chart.warps;
    for (std::size_t index = 0; index < warps.size(); ++index)
        append_proposed_marker(projection, warps[index].span.begin, warps[index].id.value,
                               warps[index].provenance.source_order, index,
                               WORM_UI_TIMING_LANE_WARP);
}

void append_anchor_markers(proposed_grid_context_t& projection) {
    const auto& anchors = projection.scope.chart_scope.chart.timing_anchors;
    for (std::size_t index = 0; index < anchors.size(); ++index)
        append_proposed_marker(projection, anchors[index].position, anchors[index].id.value,
                               index, index, WORM_UI_TIMING_LANE_ANCHOR);
}

void layout_proposed_marker_overlaps(proposed_grid_context_t& projection) {
    auto& markers = projection.preview_markers();
    std::ranges::sort(markers, [](const auto& left, const auto& right) {
        return std::tie(left.lane, left.marker_y_pixels, left.source_order, left.storage_order,
                        left.object_id) < std::tie(right.lane, right.marker_y_pixels,
                                                   right.source_order, right.storage_order,
                                                   right.object_id);
    });
    for (std::size_t first = 0; first < markers.size();) {
        auto after = first + 1;
        while (after < markers.size() && markers[after].lane == markers[first].lane &&
               markers[after].marker_y_pixels == markers[first].marker_y_pixels) ++after;
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            markers[index].marker_left_fraction = static_cast<double>(index - first) / count;
            markers[index].marker_right_fraction = static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
    projection.view().timing_preview_markers = markers.data();
    projection.view().timing_preview_marker_count = markers.size();
}

void append_proposed_markers(proposed_grid_context_t& projection) {
    auto& markers = projection.preview_markers();
    markers.clear();
    append_standard_event_markers(projection, projection.scope.chart_scope.chart.bpms,
                                  WORM_UI_TIMING_LANE_BPM);
    append_standard_event_markers(projection, projection.scope.chart_scope.chart.scrolls,
                                  WORM_UI_TIMING_LANE_SCROLL);
    append_standard_event_markers(projection, projection.scope.chart_scope.chart.speeds,
                                  WORM_UI_TIMING_LANE_SPEED);
    append_pause_markers(projection);
    append_warp_markers(projection);
    append_standard_event_markers(projection, projection.scope.chart_scope.chart.kiai,
                                  WORM_UI_TIMING_LANE_KIAI);
    append_anchor_markers(projection);
    layout_proposed_marker_overlaps(projection);
}

void project_proposed_bookmarks(proposed_grid_context_t& projection) {
    for (std::size_t index = 0; index < projection.view().beat_bookmark_count; ++index) {
        auto& bookmark = projection.flattened.compose.bookmark[index];
        const auto y = projection.proposed_y(bookmark.chart_row);
        const auto old = projection.old_y(bookmark.chart_row);
        bookmark.proposed_projection_visible = y && std::abs(*y - old) >= 0.5 &&
            *y >= 0.0 && *y <= projection.view().viewport_height_pixels;
        bookmark.proposed_y_pixels = bookmark.proposed_projection_visible ? *y : 0.0;
    }
}

void project_proposed_section(proposed_grid_context_t& projection, std::size_t index) {
    auto& section = projection.flattened.compose.section[index];
    const auto begin = projection.proposed_y(section.begin_chart_row);
    const auto end = projection.proposed_y(section.end_chart_row);
    const auto old_begin = projection.old_y(section.begin_chart_row);
    const auto old_end = projection.old_y(section.end_chart_row);
    const auto height = projection.view().viewport_height_pixels;
    section.proposed_begin_projection_visible = begin && std::abs(*begin - old_begin) >= 0.5 &&
        *begin >= 0.0 && *begin <= height;
    section.proposed_end_projection_visible = end && std::abs(*end - old_end) >= 0.5 &&
        *end >= 0.0 && *end <= height;
    section.proposed_begin_y_pixels = section.proposed_begin_projection_visible ? *begin : 0.0;
    section.proposed_end_y_pixels = section.proposed_end_projection_visible ? *end : 0.0;
    section.proposed_span_projection_visible = begin && end && std::max(*begin, *end) >= 0.0 &&
        std::min(*begin, *end) <= height &&
        (std::abs(*begin - old_begin) >= 0.5 || std::abs(*end - old_end) >= 0.5);
    if (!section.proposed_span_projection_visible) return;
    section.proposed_span_top_y_pixels = std::clamp(std::min(*begin, *end), 0.0, height);
    section.proposed_span_bottom_y_pixels = std::clamp(std::max(*begin, *end), 0.0, height);
}

void project_proposed_sections(proposed_grid_context_t& projection) {
    for (std::size_t index = 0; index < projection.view().beat_section_count; ++index)
        project_proposed_section(projection, index);
}

void project_compose_proposed_grid(flattened_workspace_t& flattened,
                                   const compose_scene_cache_t& cache,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::timing_index_t& original,
                                   const wormedit::timing_index_t& proposed,
                                   const wormedit::editor_interaction_state_t& interaction) {
    auto& view = flattened.view.compose;
    flattened.compose.grid.proposed.clear();
    flattened.compose.timing_preview_rect.clear();
    flattened.compose.timing_preview_marker.clear();
    if (!cache.scene.index || !cache.frame.interaction_view) return;
    const auto downscroll = compose_field_downscroll(chart);
    const auto rows = proposed_visible_source_rows(
        original, proposed, interaction.viewport_scroll_pixels,
        view.viewport_height_pixels, view.pixels_per_standard_measure);
    if (!rows) return;
    auto source_view = *cache.frame.interaction_view;
    const auto pixels_per_row = view.pixels_per_standard_measure /
                                original.divisions_per_standard_measure;
    source_view.viewport_scroll_pixels =
        std::max(0.0, static_cast<double>(rows->first) * pixels_per_row - 16.0);
    const auto remaining_height = static_cast<double>(proposed.total_rows) * pixels_per_row -
                                  source_view.viewport_scroll_pixels;
    source_view.viewport_height_pixels = std::max(
        1.0, std::min(remaining_height,
                      static_cast<double>(rows->second - rows->first) * pixels_per_row + 32.0));
    const auto composed = wormedit::make_editor_field_scene(
        chart, *cache.scene.index, source_view, cache.frame.preview_scene_request);
    if (!composed) {
        view.truncated = true;
        return;
    }
    const auto scope = compose_chart_scope_t<>::from(
        {chart, cache.scene.index->measures}, source_view, downscroll, pixels_per_row);
    proposed_grid_context_t projection{scope,    flattened,   cache,           original,
                                       proposed, interaction, *composed.scene, *rows};
    append_proposed_grid_lines(projection);
    append_note_and_bgm_preview_rects(projection);
    append_hold_preview_rects(projection);
    append_proposed_markers(projection);
    project_proposed_bookmarks(projection);
    project_proposed_sections(projection);
}

} // namespace

namespace {

auto audio_offset_drag_matches(const std::optional<audio_offset_drag_t>& drag,
                               const std::optional<workspace_tab_t>& drag_tab,
                               const workspace_controller_t& workspace,
                               const document_history_t& history) -> bool {
    return drag && drag_tab && workspace.selected_chart &&
           *drag_tab == workspace_tab_t::compose && drag->chart == *workspace.selected_chart &&
           drag->document_revision == history.document.revision;
}

void rebuild_offset_timing_index(compose_scene_cache_t& cache,
                                 const document_history_t& history,
                                 wormedit::chart_id_t chart_id,
                                 signed_rational_t proposed_offset) {
    cache.timing.proposed_offset_timing_index.reset();
    cache.timing.proposed_offset_seconds = proposed_offset;
    const auto chart = history.document.find_chart(chart_id);
    if (chart == nullptr) return;
    auto candidate = *chart;
    candidate.beat_zero_audio_offset_seconds = proposed_offset;
    auto built = build_timing_index(candidate);
    if (built) cache.timing.proposed_offset_timing_index = std::move(*built.index);
}

void project_offset_grid(flattened_workspace_t& flattened, compose_scene_cache_t& cache,
                         const document_history_t& history,
                         const workspace_controller_t& workspace,
                         const editor_interaction_state_t* interaction) {
    if (!cache.timing.proposed_offset_timing_index || !workspace.selected_chart || !interaction ||
        !cache.timing.marker_timing_index) return;
    const auto chart = history.document.find_chart(*workspace.selected_chart);
    if (chart != nullptr)
        project_compose_proposed_grid(flattened, cache, *chart, *cache.timing.marker_timing_index,
                                      *cache.timing.proposed_offset_timing_index, *interaction);
}

void update_proposed_beat_zero(flattened_workspace_t& flattened,
                               const audio_offset_drag_t& drag,
                               const audio::waveform_analysis_result_t* waveform_analysis) {
    auto& compose = flattened.view.compose;
    compose.waveform_proposed_audio_offset_numerator = drag.proposed_offset_seconds.numerator;
    compose.waveform_proposed_audio_offset_denominator = drag.proposed_offset_seconds.denominator;
    if (!waveform_analysis || !waveform_analysis->waveform ||
        waveform_analysis->waveform->total_frames == 0 ||
        drag.proposed_offset_seconds.denominator == 0) return;
    const auto fraction = static_cast<long double>(drag.proposed_offset_seconds.numerator) *
                          waveform_analysis->waveform->sample_rate /
                          drag.proposed_offset_seconds.denominator /
                          waveform_analysis->waveform->total_frames;
    compose.waveform_proposed_beat_zero_visible = std::isfinite(fraction) && fraction >= 0.0L &&
                                                   fraction <= 1.0L;
    if (compose.waveform_proposed_beat_zero_visible)
        compose.waveform_proposed_beat_zero_fraction = static_cast<double>(fraction);
}

void populate_audio_offset_overlay(flattened_workspace_t& flattened,
                                   document_history_t& history,
                                   const workspace_controller_t& workspace,
                                   compose_scene_cache_t& cache,
                                   const editor_interaction_state_t* interaction,
                                   const std::optional<audio_offset_drag_t>& drag,
                                   const std::optional<workspace_tab_t>& drag_tab,
                                   const audio::waveform_analysis_result_t* waveform_analysis) {
    if (!audio_offset_drag_matches(drag, drag_tab, workspace, history)) return;
    flattened.view.compose.waveform_offset_drag_active = true;
    if (cache.timing.proposed_offset_seconds != drag->proposed_offset_seconds)
        rebuild_offset_timing_index(cache, history, *workspace.selected_chart,
                                    drag->proposed_offset_seconds);
    project_offset_grid(flattened, cache, history, workspace, interaction);
    update_proposed_beat_zero(flattened, *drag, waveform_analysis);
}

auto beat_fit_drag_matches(const compose_scene_cache_t& cache,
                           const workspace_controller_t& workspace,
                           const document_history_t& history,
                           const audio::waveform_analysis_result_t& waveform) -> bool {
    return workspace.transport.audio_asset && cache.timing.beat_fit_drag &&
           cache.timing.beat_fit_asset == workspace.transport.audio_asset &&
           cache.timing.beat_fit_drag->sample_rate == waveform.waveform->sample_rate &&
           cache.timing.beat_fit_drag->document_revision == history.document.revision;
}

void populate_beat_fit_fields(WormUiComposeView& compose, const beat_interval_drag_t& drag) {
    compose.beat_fit_drag_active = true;
    compose.beat_fit_target_y_pixels = -1.0;
    compose.beat_fit_target_sample_frame = drag.proposed_sample_frame;
    compose.beat_fit_original_bpm_numerator = drag.original_bpm.numerator;
    compose.beat_fit_original_bpm_denominator = drag.original_bpm.denominator;
    compose.beat_fit_proposed_bpm_numerator = drag.proposed_bpm.numerator;
    compose.beat_fit_proposed_bpm_denominator = drag.proposed_bpm.denominator;
    compose.beat_fit_snap_active = drag.snap_active;
    compose.beat_fit_snap_step_numerator = drag.snap_step.numerator;
    compose.beat_fit_snap_step_denominator = drag.snap_step.denominator;
}

void rebuild_beat_fit_timing_index(compose_scene_cache_t& cache,
                                  const wormedit::chart_document_t& chart,
                                  const beat_interval_drag_t& drag) {
    if (cache.timing.proposed_beat_bpm == drag.proposed_bpm &&
        cache.timing.proposed_beat_event == drag.bpm_event) return;
    cache.timing.proposed_beat_timing_index.reset();
    cache.timing.proposed_beat_bpm = drag.proposed_bpm;
    cache.timing.proposed_beat_event = drag.bpm_event;
    auto candidate = chart;
    const auto event = std::ranges::find(candidate.bpms, drag.bpm_event, &bpm_event_t::id);
    if (event == candidate.bpms.end()) return;
    event->beats_per_minute = drag.proposed_bpm;
    auto built = build_timing_index(candidate);
    if (built) cache.timing.proposed_beat_timing_index = std::move(*built.index);
}

void project_beat_fit_positions(WormUiComposeView& compose,
                                const compose_scene_cache_t& cache,
                                const wormedit::chart_document_t& chart,
                                const editor_interaction_state_t& interaction,
                                const beat_interval_drag_t& drag) {
    const auto& timing = *cache.timing.marker_timing_index;
    compose.beat_fit_chart_row = (cache.scene.index->measures).row_of(drag.beat).value_or(0);
    const auto anchor_row = (cache.scene.index->measures).row_of(drag.original_bpm_position);
    const auto pixels_per_row = compose.pixels_per_standard_measure /
                                timing.divisions_per_standard_measure;
    const auto downscroll = compose_field_downscroll(chart);
    if (anchor_row)
        compose.beat_fit_anchor_y_pixels = compose_project_y(
            *anchor_row, pixels_per_row, interaction.viewport_scroll_pixels,
            compose.viewport_height_pixels, downscroll);
    compose.beat_fit_beat_y_pixels = compose_project_y(
        compose.beat_fit_chart_row, pixels_per_row, interaction.viewport_scroll_pixels,
        compose.viewport_height_pixels, downscroll);
    const auto target = timing.row_at({static_cast<std::int64_t>(drag.proposed_sample_frame), drag.sample_rate});
    if (!target) return;
    const auto& row = *target.absolute_row;
    const auto source_y = static_cast<double>(
        wormedit::to_long_double(row) *
        compose.pixels_per_standard_measure / timing.divisions_per_standard_measure -
        interaction.viewport_scroll_pixels);
    compose.beat_fit_target_y_pixels = compose_source_y(
        source_y, compose.viewport_height_pixels, downscroll);
}

void populate_beat_fit_overlay(flattened_workspace_t& flattened,
                               document_history_t& history,
                               const workspace_controller_t& workspace,
                               compose_scene_cache_t& cache,
                               const editor_interaction_state_t* interaction,
                               const audio::waveform_analysis_result_t& waveform) {
    if (!beat_fit_drag_matches(cache, workspace, history, waveform)) return;
    const auto& drag = *cache.timing.beat_fit_drag;
    auto& compose = flattened.view.compose;
    populate_beat_fit_fields(compose, drag);
    compose.beat_fit_onset_snap_active = cache.timing.beat_fit_onset_snapped;
    const auto chart = history.document.find_chart(drag.chart);
    if (chart == nullptr || !cache.scene.index || !interaction ||
        !cache.timing.marker_timing_index) return;
    project_beat_fit_positions(compose, cache, *chart, *interaction, drag);
    rebuild_beat_fit_timing_index(cache, *chart, drag);
    if (cache.timing.proposed_beat_timing_index)
        project_compose_proposed_grid(flattened, cache, *chart, *cache.timing.marker_timing_index,
                                      *cache.timing.proposed_beat_timing_index, *interaction);
}

} // namespace

namespace {
void populate_tap_apply_overlay(flattened_workspace_t& flattened,
    document_history_t& history, const workspace_controller_t& workspace,
    compose_scene_cache_t& cache, editor_interaction_state_t* interaction,
    const audio::waveform_analysis_result_t* analysis) {
    if (!cache.timing.tap_apply_proposal) {
        cache.timing.tap_preview_binding.reset();
        cache.timing.tap_preview_timing.reset();
        return;
    }
    if (!workspace.selected_chart || !interaction ||
        !analysis || !analysis->waveform || !cache.timing.marker_timing_index) return;
    const auto& proposal = *cache.timing.tap_apply_proposal;
    if (proposal.chart != *workspace.selected_chart ||
        proposal.document_revision != history.document.revision) return;
    const auto& bound = cache.timing.tap_preview_binding;
    if (!bound || std::tie(bound->chart, bound->document_revision, bound->beats_per_minute,
                           bound->beat_zero_audio_offset_seconds) !=
                  std::tie(proposal.chart, proposal.document_revision, proposal.beats_per_minute,
                           proposal.beat_zero_audio_offset_seconds)) {
        cache.timing.tap_preview_binding = proposal;
        cache.timing.tap_preview_timing.reset();
        const auto candidate = make_tap_timing_preview(history.document, proposal);
        if (!candidate) return;
        auto timing = build_timing_index(*candidate);
        if (timing) cache.timing.tap_preview_timing = std::move(timing.index);
    }
    if (!cache.timing.tap_preview_timing) return;
    const auto chart = history.document.find_chart(proposal.chart);
    if (chart == nullptr) return;
    project_compose_proposed_grid(flattened, cache, *chart, *cache.timing.marker_timing_index,
                                  *cache.timing.tap_preview_timing, *interaction);
    const audio_offset_drag_t offset{.chart = proposal.chart,
        .document_revision = proposal.document_revision, .sample_rate = proposal.sample_rate,
        .proposed_offset_seconds = proposal.beat_zero_audio_offset_seconds};
    update_proposed_beat_zero(flattened, offset, analysis);
    flattened.view.compose.waveform_offset_drag_active = true;
}
} // namespace

void populate_compose_waveform_overlays(
    bool overview_available, flattened_workspace_t& flattened,
    document_history_t& history, const workspace_controller_t& workspace,
    compose_scene_cache_t& cache, editor_interaction_state_t* interaction,
    const std::optional<audio_offset_drag_t>& audio_offset_drag,
    const std::optional<workspace_tab_t>& audio_offset_drag_tab,
    const audio::waveform_analysis_result_t* waveform_analysis) {
    if (!overview_available) return;
    populate_audio_offset_overlay(flattened, history, workspace, cache, interaction,
                                  audio_offset_drag, audio_offset_drag_tab, waveform_analysis);
    if (!waveform_analysis || !waveform_analysis->waveform) return;
    cache.waveform.waveform_overview_total_frames = waveform_analysis->waveform->total_frames;
    cache.waveform.waveform_overview_sample_rate = waveform_analysis->waveform->sample_rate;
    populate_beat_fit_overlay(flattened, history, workspace, cache, interaction,
                              *waveform_analysis);
    if (!audio_offset_drag && !cache.timing.beat_fit_drag && !cache.timing.offset_fit_drag)
        populate_tap_apply_overlay(flattened, history, workspace, cache, interaction,
                                    waveform_analysis);
    if (cache.timing.offset_fit_drag && workspace.transport.audio_asset &&
        cache.timing.offset_fit_drag->asset == workspace.transport.audio_asset &&
        cache.timing.offset_fit_drag->beat.document_revision == history.document.revision) {
        flattened.view.compose.offset_fit_drag_active = true;
        flattened.view.compose.offset_fit_target_y_pixels =
            cache.timing.offset_fit_drag->target_y_pixels;
    }
}

} // namespace wormedit::sys
#endif
