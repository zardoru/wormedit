#include "sys/compose_waveform_overlays.h"

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

auto rational_as_long_double(signed_rational_t value) -> long double {
    if (value.denominator == 0)
        return 0.0L;
    return static_cast<long double>(value.numerator) /
           static_cast<long double>(value.denominator);
}

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
        wormedit::timing_at(original, static_cast<std::uint64_t>(std::floor(first_visible)),
                            wormedit::timing_boundary_side_t::before_delays);
    const auto last_time =
        wormedit::timing_at(original, static_cast<std::uint64_t>(std::ceil(last_visible)),
                            wormedit::timing_boundary_side_t::after_stops);
    if (!first_time || !last_time)
        return std::nullopt;
    const auto source_row = [&](wormedit::signed_rational_t seconds) -> std::optional<long double> {
        const auto position = wormedit::musical_position_at(proposed, seconds);
        if (position)
            return rational_as_long_double(*position.absolute_row);
        const auto time = rational_as_long_double(seconds);
        if (time < rational_as_long_double(proposed.rows.front().before_delays_seconds))
            return 0.0L;
        if (time > rational_as_long_double(proposed.rows.back().after_stops_seconds))
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

void project_compose_proposed_grid(flattened_workspace_t& flattened,
                                   const compose_scene_cache_t& cache,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::timing_index_t& original,
                                   const wormedit::timing_index_t& proposed,
                                   const wormedit::editor_interaction_state_t& interaction) {
    auto& view = flattened.view.compose;
    auto& output = flattened.compose_proposed_grid_storage;
    output.clear();
    if (!cache.index || !cache.interaction_view)
        return;
    const auto downscroll = compose_field_downscroll(chart);
    const auto rows =
        proposed_visible_source_rows(original, proposed, interaction.viewport_scroll_pixels,
                                     view.viewport_height_pixels, view.pixels_per_standard_measure);
    if (!rows)
        return;
    auto source_view = *cache.interaction_view;
    const auto pixels_per_row =
        view.pixels_per_standard_measure / original.divisions_per_standard_measure;
    source_view.viewport_scroll_pixels =
        std::max(0.0, static_cast<double>(rows->first) * pixels_per_row - 16.0);
    const auto remaining_height = static_cast<double>(proposed.total_rows) * pixels_per_row -
                                  source_view.viewport_scroll_pixels;
    source_view.viewport_height_pixels = std::max(
        1.0, std::min(remaining_height,
                      static_cast<double>(rows->second - rows->first) * pixels_per_row + 32.0));
    const auto composed = wormedit::make_editor_field_scene(chart, *cache.index, source_view,
                                                            cache.preview_scene_request);
    if (!composed) {
        view.truncated = true;
        return;
    }
    const auto& source_scene = *composed.scene;
    output.reserve(source_scene.grid_lines.size());
    for (const auto& line : source_scene.grid_lines) {
        const auto projected = wormedit::proposed_timing_y_on_fixed_waveform(
            original, proposed, line.chart_row, interaction.viewport_scroll_pixels,
            view.pixels_per_standard_measure);
        if (!projected)
            continue;
        const auto y = compose_source_y(*projected, view.viewport_height_pixels, downscroll);
        if (y < 0.0 || y > view.viewport_height_pixels)
            continue;
        std::uint32_t measure_number{};
        if (line.kind == wormedit::editor_grid_line_kind_t::measure) {
            const auto& measures = cache.index->measures.entries;
            const auto found =
                std::ranges::lower_bound(measures, line.chart_row.whole_rows, {},
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
    view.proposed_grid_lines = output.data();
    view.proposed_grid_line_count = output.size();
    const auto proposed_y = [&](std::uint64_t row) {
        const auto projected = wormedit::proposed_timing_y_on_fixed_waveform(
            original, proposed, {row, {}}, interaction.viewport_scroll_pixels,
            view.pixels_per_standard_measure);
        return projected ? std::optional{compose_source_y(
                               *projected, view.viewport_height_pixels, downscroll)}
                         : std::nullopt;
    };
    auto& preview_rects = flattened.compose_timing_preview_rect_storage;
    preview_rects.clear();
    preview_rects.reserve(source_scene.note_endpoints.size() + source_scene.bgm_events.size());
    const auto append_rect = [&](WormUiComposeRect bounds, std::uint64_t row, std::uint8_t kind) {
        if (preview_rects.size() >= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY) {
            view.truncated = true;
            return;
        }
        const auto y = proposed_y(row);
        if (!y)
            return;
        const auto old_y = compose_project_y(row, pixels_per_row,
                                             interaction.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
        const auto shift = *y - old_y;
        if (std::abs(shift) < 0.5 || bounds.bottom + shift < 0.0 ||
            bounds.top + shift > view.viewport_height_pixels)
            return;
        bounds.top += shift;
        bounds.bottom += shift;
        if (std::abs(bounds.top) > 10000000.0 || std::abs(bounds.bottom) > 10000000.0)
            return;
        preview_rects.push_back({.bounds = bounds, .kind = kind});
    };
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    for (const auto& note : source_scene.note_endpoints) {
        const auto source = cache.index->note_indices_by_id.find(note.note.value);
        if (source == cache.index->note_indices_by_id.end() || note.proposal_ghost)
            continue;
        const auto& indexed = cache.index->notes[source->second];
        append_rect(convert_rect(note.bounds), note.tail ? indexed.end_row : indexed.begin_row, 0);
    }
    for (const auto& event : source_scene.bgm_events) {
        const auto row = cache.bgm_rows_by_id.find(event.event.value);
        if (row != cache.bgm_rows_by_id.end())
            append_rect(convert_rect(event.bounds), row->second, 1);
    }
    const auto projected_geometry_y = [&](double old_y) -> std::optional<double> {
        const auto source_y = compose_source_y(old_y, source_view.viewport_height_pixels,
                                               downscroll);
        const auto row = (static_cast<long double>(source_y) +
                          source_view.viewport_scroll_pixels) *
                         original.divisions_per_standard_measure / view.pixels_per_standard_measure;
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
            interaction.viewport_scroll_pixels, view.pixels_per_standard_measure);
        return projected ? std::optional{compose_source_y(
                               *projected, view.viewport_height_pixels, downscroll)}
                         : std::nullopt;
    };
    for (const auto& hold : source_scene.hold_bodies) {
        if (preview_rects.size() >= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY) {
            view.truncated = true;
            break;
        }
        const auto top = projected_geometry_y(hold.bounds.top);
        const auto bottom = projected_geometry_y(hold.bounds.bottom);
        const auto old_top_source = compose_source_y(hold.bounds.top,
                                                     source_view.viewport_height_pixels, downscroll);
        const auto old_bottom_source = compose_source_y(hold.bounds.bottom,
                                                        source_view.viewport_height_pixels, downscroll);
        const auto old_top = compose_source_y(old_top_source + source_view.viewport_scroll_pixels -
                                                  interaction.viewport_scroll_pixels,
                                              view.viewport_height_pixels, downscroll);
        const auto old_bottom = compose_source_y(old_bottom_source + source_view.viewport_scroll_pixels -
                                                     interaction.viewport_scroll_pixels,
                                                 view.viewport_height_pixels, downscroll);
        if (!top || !bottom || std::abs(*bottom - *top) < 0.5 ||
            (std::abs(*top - old_top) < 0.5 && std::abs(*bottom - old_bottom) < 0.5) ||
            std::max(*top, *bottom) < 0.0 ||
            std::min(*top, *bottom) > view.viewport_height_pixels)
            continue;
        auto bounds = convert_rect(hold.bounds);
        bounds.top = std::min(*top, *bottom);
        bounds.bottom = std::max(*top, *bottom);
        if (std::abs(bounds.top) > 10000000.0 || std::abs(bounds.bottom) > 10000000.0)
            continue;
        preview_rects.push_back({.bounds = bounds, .kind = 2});
    }
    view.timing_preview_rects = preview_rects.data();
    view.timing_preview_rect_count = preview_rects.size();

    auto& preview_markers = flattened.compose_timing_preview_marker_storage;
    preview_markers.clear();
    const auto append_marker = [&](wormedit::local_position_t position, std::uint64_t id,
                                   std::size_t source_order, std::size_t storage_order,
                                   WormUiTimingLaneKind lane) {
        if (preview_markers.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
            view.truncated = true;
            return;
        }
        const auto row = wormedit::absolute_row(cache.index->measures, position);
        if (!row || *row < rows->first || *row > rows->second)
            return;
        const auto y = proposed_y(*row);
        const auto old_y = compose_project_y(*row, pixels_per_row,
                                             interaction.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
        if (!y || std::abs(*y - old_y) < 0.5 || *y < 0.0 || *y > view.viewport_height_pixels)
            return;
        preview_markers.push_back({.object_id = id,
                                   .source_order = static_cast<std::uint64_t>(source_order),
                                   .storage_order = static_cast<std::uint64_t>(storage_order),
                                   .marker_y_pixels = *y,
                                   .marker_left_fraction = 0.0,
                                   .marker_right_fraction = 1.0,
                                   .chart_row = *row,
                                   .lane = lane});
    };
    for (std::size_t index = 0; index < chart.bpms.size(); ++index)
        append_marker(chart.bpms[index].position, chart.bpms[index].id.value,
                      chart.bpms[index].provenance.source_order, index, WORM_UI_TIMING_LANE_BPM);
    for (std::size_t index = 0; index < chart.scrolls.size(); ++index)
        append_marker(chart.scrolls[index].position, chart.scrolls[index].id.value,
                      chart.scrolls[index].provenance.source_order, index,
                      WORM_UI_TIMING_LANE_SCROLL);
    for (std::size_t index = 0; index < chart.speeds.size(); ++index)
        append_marker(chart.speeds[index].position, chart.speeds[index].id.value,
                      chart.speeds[index].provenance.source_order, index,
                      WORM_UI_TIMING_LANE_SPEED);
    for (std::size_t index = 0; index < chart.pauses.size(); ++index)
        append_marker(chart.pauses[index].position, chart.pauses[index].id.value,
                      chart.pauses[index].provenance.source_order, index,
                      chart.pauses[index].kind == wormedit::pause_kind_t::stop
                          ? WORM_UI_TIMING_LANE_STOP
                          : WORM_UI_TIMING_LANE_DELAY);
    for (std::size_t index = 0; index < chart.warps.size(); ++index)
        append_marker(chart.warps[index].span.begin, chart.warps[index].id.value,
                      chart.warps[index].provenance.source_order, index, WORM_UI_TIMING_LANE_WARP);
    for (std::size_t index = 0; index < chart.kiai.size(); ++index)
        append_marker(chart.kiai[index].position, chart.kiai[index].id.value,
                      chart.kiai[index].provenance.source_order, index, WORM_UI_TIMING_LANE_KIAI);
    for (std::size_t index = 0; index < chart.timing_anchors.size(); ++index)
        append_marker(chart.timing_anchors[index].position, chart.timing_anchors[index].id.value,
                      index, index, WORM_UI_TIMING_LANE_ANCHOR);
    std::ranges::sort(preview_markers, [](const auto& left, const auto& right) {
        return std::tie(left.lane, left.marker_y_pixels, left.source_order, left.storage_order,
                        left.object_id) < std::tie(right.lane, right.marker_y_pixels,
                                                   right.source_order, right.storage_order,
                                                   right.object_id);
    });
    for (std::size_t first = 0; first < preview_markers.size();) {
        auto after = first + 1;
        while (after < preview_markers.size() &&
               preview_markers[after].lane == preview_markers[first].lane &&
               preview_markers[after].marker_y_pixels == preview_markers[first].marker_y_pixels)
            ++after;
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            preview_markers[index].marker_left_fraction =
                static_cast<double>(index - first) / count;
            preview_markers[index].marker_right_fraction =
                static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
    view.timing_preview_markers = preview_markers.data();
    view.timing_preview_marker_count = preview_markers.size();
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        auto& bookmark = flattened.compose_bookmark_storage[index];
        const auto y = proposed_y(bookmark.chart_row);
        const auto old_y = compose_project_y(bookmark.chart_row, pixels_per_row,
                                             interaction.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
        bookmark.proposed_projection_visible =
            y && std::abs(*y - old_y) >= 0.5 && *y >= 0.0 && *y <= view.viewport_height_pixels;
        bookmark.proposed_y_pixels = bookmark.proposed_projection_visible ? *y : 0.0;
    }
    for (std::size_t index = 0; index < view.beat_section_count; ++index) {
        auto& section = flattened.compose_section_storage[index];
        const auto begin = proposed_y(section.begin_chart_row);
        const auto end = proposed_y(section.end_chart_row);
        const auto old_begin = compose_project_y(section.begin_chart_row, pixels_per_row,
                                                 interaction.viewport_scroll_pixels,
                                                 view.viewport_height_pixels, downscroll);
        const auto old_end = compose_project_y(section.end_chart_row, pixels_per_row,
                                               interaction.viewport_scroll_pixels,
                                               view.viewport_height_pixels, downscroll);
        section.proposed_begin_projection_visible = begin && std::abs(*begin - old_begin) >= 0.5 &&
                                                    *begin >= 0.0 &&
                                                    *begin <= view.viewport_height_pixels;
        section.proposed_end_projection_visible = end && std::abs(*end - old_end) >= 0.5 &&
                                                  *end >= 0.0 &&
                                                  *end <= view.viewport_height_pixels;
        section.proposed_begin_y_pixels = section.proposed_begin_projection_visible ? *begin : 0.0;
        section.proposed_end_y_pixels = section.proposed_end_projection_visible ? *end : 0.0;
        section.proposed_span_projection_visible =
            begin && end && std::max(*begin, *end) >= 0.0 &&
            std::min(*begin, *end) <= view.viewport_height_pixels &&
            (std::abs(*begin - old_begin) >= 0.5 || std::abs(*end - old_end) >= 0.5);
        if (section.proposed_span_projection_visible) {
            section.proposed_span_top_y_pixels =
                std::clamp(std::min(*begin, *end), 0.0, view.viewport_height_pixels);
            section.proposed_span_bottom_y_pixels =
                std::clamp(std::max(*begin, *end), 0.0, view.viewport_height_pixels);
        }
    }
}

} // namespace

void populate_compose_waveform_overlays(
    bool overview_available, flattened_workspace_t& flattened,
    document_history_t& history, const workspace_controller_t& workspace,
    compose_scene_cache_t& cache, editor_interaction_state_t* interaction,
    const std::optional<audio_offset_drag_t>& audio_offset_drag,
    const std::optional<workspace_tab_t>& audio_offset_drag_tab,
    const audio::waveform_analysis_result_t* waveform_analysis) {
    if (!overview_available)
        return;
    auto& compose = flattened.view.compose;

    if (audio_offset_drag && audio_offset_drag_tab && workspace.selected_chart &&
        *audio_offset_drag_tab == workspace_tab_t::compose &&
        audio_offset_drag->chart == *workspace.selected_chart &&
        audio_offset_drag->document_revision == history.document.revision) {
        compose.waveform_offset_drag_active = true;
        if (cache.proposed_offset_seconds != audio_offset_drag->proposed_offset_seconds) {
            cache.proposed_offset_timing_index.reset();
            cache.proposed_offset_seconds = audio_offset_drag->proposed_offset_seconds;
            const auto chart = std::ranges::find(history.document.charts,
                                                 *workspace.selected_chart,
                                                 &chart_document_t::id);
            if (chart != history.document.charts.end()) {
                auto candidate = *chart;
                candidate.beat_zero_audio_offset_seconds =
                    audio_offset_drag->proposed_offset_seconds;
                auto built = build_timing_index(candidate);
                if (built)
                    cache.proposed_offset_timing_index = std::move(*built.index);
            }
        }
        if (cache.proposed_offset_timing_index && workspace.selected_chart && interaction &&
            cache.marker_timing_index) {
            const auto chart = std::ranges::find(history.document.charts,
                                                 *workspace.selected_chart,
                                                 &chart_document_t::id);
            if (chart != history.document.charts.end())
                project_compose_proposed_grid(flattened, cache, *chart,
                                              *cache.marker_timing_index,
                                              *cache.proposed_offset_timing_index, *interaction);
        }
        compose.waveform_proposed_audio_offset_numerator =
            audio_offset_drag->proposed_offset_seconds.numerator;
        compose.waveform_proposed_audio_offset_denominator =
            audio_offset_drag->proposed_offset_seconds.denominator;
        if (waveform_analysis && waveform_analysis->waveform &&
            waveform_analysis->waveform->total_frames != 0 &&
            audio_offset_drag->proposed_offset_seconds.denominator != 0) {
            const auto proposed_fraction =
                static_cast<long double>(audio_offset_drag->proposed_offset_seconds.numerator) *
                waveform_analysis->waveform->sample_rate /
                audio_offset_drag->proposed_offset_seconds.denominator /
                waveform_analysis->waveform->total_frames;
            compose.waveform_proposed_beat_zero_visible =
                std::isfinite(proposed_fraction) && proposed_fraction >= 0.0L &&
                proposed_fraction <= 1.0L;
            if (compose.waveform_proposed_beat_zero_visible)
                compose.waveform_proposed_beat_zero_fraction =
                    static_cast<double>(proposed_fraction);
        }
    }

    if (!waveform_analysis || !waveform_analysis->waveform)
        return;
    cache.waveform_overview_total_frames = waveform_analysis->waveform->total_frames;
    cache.waveform_overview_sample_rate = waveform_analysis->waveform->sample_rate;
    if (!workspace.transport.audio_asset ||
        !cache.beat_fit_drag || cache.beat_fit_asset != workspace.transport.audio_asset ||
        cache.beat_fit_drag->sample_rate != waveform_analysis->waveform->sample_rate ||
        cache.beat_fit_drag->document_revision != history.document.revision)
        return;

    const auto& drag = *cache.beat_fit_drag;
    compose.beat_fit_drag_active = true;
    compose.beat_fit_target_y_pixels = -1.0;
    compose.beat_fit_target_sample_frame = drag.proposed_sample_frame;
    compose.beat_fit_original_bpm_numerator = drag.original_bpm.numerator;
    compose.beat_fit_original_bpm_denominator = drag.original_bpm.denominator;
    compose.beat_fit_proposed_bpm_numerator = drag.proposed_bpm.numerator;
    compose.beat_fit_proposed_bpm_denominator = drag.proposed_bpm.denominator;

    const auto chart = std::ranges::find(history.document.charts, drag.chart,
                                         &chart_document_t::id);
    if (chart != history.document.charts.end() && cache.index && interaction &&
        cache.marker_timing_index) {
        compose.beat_fit_chart_row =
            absolute_row(cache.index->measures, drag.beat).value_or(0);
        const auto anchor_row = absolute_row(cache.index->measures, drag.original_bpm_position);
        const auto pixels_per_row = compose.pixels_per_standard_measure /
                                    cache.marker_timing_index->divisions_per_standard_measure;
        const auto downscroll = compose_field_downscroll(*chart);
        if (anchor_row)
            compose.beat_fit_anchor_y_pixels =
                compose_project_y(*anchor_row, pixels_per_row,
                                  interaction->viewport_scroll_pixels,
                                  compose.viewport_height_pixels, downscroll);
        compose.beat_fit_beat_y_pixels =
            compose_project_y(compose.beat_fit_chart_row, pixels_per_row,
                              interaction->viewport_scroll_pixels,
                              compose.viewport_height_pixels, downscroll);

        if (cache.proposed_beat_bpm != drag.proposed_bpm ||
            cache.proposed_beat_event != drag.bpm_event) {
            cache.proposed_beat_timing_index.reset();
            cache.proposed_beat_bpm = drag.proposed_bpm;
            cache.proposed_beat_event = drag.bpm_event;
            auto candidate = *chart;
            const auto event = std::ranges::find(candidate.bpms, drag.bpm_event,
                                                 &bpm_event_t::id);
            if (event != candidate.bpms.end()) {
                event->beats_per_minute = drag.proposed_bpm;
                auto built = build_timing_index(candidate);
                if (built)
                    cache.proposed_beat_timing_index = std::move(*built.index);
            }
        }
        if (cache.proposed_beat_timing_index)
            project_compose_proposed_grid(flattened, cache, *chart, *cache.marker_timing_index,
                                          *cache.proposed_beat_timing_index, *interaction);

        const auto target = musical_position_at(
            *cache.marker_timing_index,
            {static_cast<std::int64_t>(drag.proposed_sample_frame), drag.sample_rate});
        if (target) {
            const auto& row = *target.absolute_row;
            const auto source_y = static_cast<double>(
                static_cast<long double>(row.numerator) / row.denominator *
                    compose.pixels_per_standard_measure /
                    cache.marker_timing_index->divisions_per_standard_measure -
                interaction->viewport_scroll_pixels);
            compose.beat_fit_target_y_pixels = compose_source_y(
                source_y, compose.viewport_height_pixels, compose_field_downscroll(*chart));
        }
    }
}

} // namespace wormedit::sys
#endif
