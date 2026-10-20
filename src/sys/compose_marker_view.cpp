#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/core/rational.h>
#include "sys/compose_view_internal.h"
namespace wormedit::sys::compose_projection_detail {
auto populate_compose_audio_bookmark(
    compose_marker_storage_t<WormUiComposeAudioBookmarkView>& storage,
    const wormedit::bookmark_t& bookmark, WormUiComposeView&) -> bool {
    if (!bookmark.audio_milliseconds || bookmark.position) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::audio_bookmark,
                                  wormedit::notification_severity_t::error,
                                  "An audio-time bookmark has inconsistent position data.",
                                  bookmark.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage))
        return true;
    append_compose_marker(storage, bookmark.label,
                          WormUiComposeAudioBookmarkView{
                              .bookmark_id = bookmark.id.value,
                              .label = nullptr,
                              .audio_milliseconds = *bookmark.audio_milliseconds,
                              .projection_visible = false,
                              .projected_y_pixels = 0.0,
                          });
    return true;
}

auto populate_compose_beat_bookmark(compose_marker_storage_t<WormUiComposeBookmarkView>& storage,
                                    const wormedit::bookmark_t& bookmark,
                                    const wormedit::measure_index_t& measures,
                                    WormUiComposeView&) -> bool {
    if (!bookmark.position) {
        report_compose_projection(
            storage.flattened, projection_feedback_kind_t::beat_bookmark_position,
            wormedit::notification_severity_t::error,
            "A beat-anchored bookmark has no valid chart position.", bookmark.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage))
        return true;
    const auto* measure = measures.entry_for(bookmark.position->measure);
    if (!measure || bookmark.position->row >= measure->row_capacity) {
        report_compose_projection(
            storage.flattened, projection_feedback_kind_t::beat_bookmark_geometry,
            wormedit::notification_severity_t::error,
            "A beat-anchored bookmark is outside the chart measure geometry.", bookmark.id.value);
        return false;
    }
    append_compose_marker(storage, bookmark.label,
                          WormUiComposeBookmarkView{
                              .bookmark_id = bookmark.id.value,
                              .label = nullptr,
                              .measure_number = static_cast<std::uint64_t>(
                                  measure - measures.entries.data() + 1),
                              .row = bookmark.position->row,
                              .chart_row = measure->absolute_begin_row + bookmark.position->row,
                              .projection_visible = false,
                              .projected_y_pixels = 0.0,
                              .proposed_projection_visible = false,
                              .proposed_y_pixels = 0.0,
                          });
    return true;
}

bool populate_compose_bookmark_storage(frontend_workspace_view_t& flattened,
                                       const wormedit::chart_document_t& chart,
                                       const wormedit::measure_index_t& measures,
                                       WormUiComposeView& view) {
    compose_marker_storage_t beat{flattened, flattened.compose.bookmark, view.beat_bookmark_count,
                                  view.beat_bookmarks_truncated, WORM_UI_COMPOSE_BOOKMARK_CAPACITY};
    compose_marker_storage_t audio{flattened, flattened.compose.audio_marker.bookmark,
                                   view.audio_bookmark_count, view.audio_bookmarks_truncated,
                                   WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY};
    prepare_compose_marker_storage(beat, static_cast<std::size_t>(std::ranges::count(
                                             chart.bookmarks, wormedit::marker_anchor_t::beat,
                                             &wormedit::bookmark_t::anchor)));
    prepare_compose_marker_storage(
        audio, static_cast<std::size_t>(std::ranges::count(chart.bookmarks,
                                                           wormedit::marker_anchor_t::audio_time,
                                                           &wormedit::bookmark_t::anchor)));
    view.beat_bookmarks = flattened.compose.bookmark.data();
    view.audio_bookmarks = flattened.compose.audio_marker.bookmark.data();
    for (const auto& bookmark : chart.bookmarks) {
        if (bookmark.anchor == wormedit::marker_anchor_t::audio_time) {
            if (!populate_compose_audio_bookmark(audio, bookmark, view))
                return false;
        } else if (bookmark.anchor == wormedit::marker_anchor_t::beat) {
            if (!populate_compose_beat_bookmark(beat, bookmark, measures, view))
                return false;
        }
    }
    return true;
}

auto populate_compose_audio_section(
    compose_marker_storage_t<WormUiComposeAudioSectionView>& storage,
    const wormedit::section_t& section, WormUiComposeView&) -> bool {
    if (!section.audio_millisecond_span || section.beat_span ||
        section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::audio_section,
                                  wormedit::notification_severity_t::error,
                                  "An audio-time section has inconsistent span data.",
                                  section.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage))
        return true;
    append_compose_marker(storage, section.label,
                          WormUiComposeAudioSectionView{
                              .section_id = section.id.value,
                              .label = nullptr,
                              .begin_audio_milliseconds = section.audio_millisecond_span->first,
                              .end_audio_milliseconds = section.audio_millisecond_span->second,
                              .begin_projection_visible = false,
                              .end_projection_visible = false,
                              .span_projection_visible = false,
                              .projected_begin_y_pixels = 0.0,
                              .projected_end_y_pixels = 0.0,
                              .projected_span_top_y_pixels = 0.0,
                              .projected_span_bottom_y_pixels = 0.0,
                          });
    return true;
}

auto populate_compose_beat_section(compose_marker_storage_t<WormUiComposeSectionView>& storage,
                                   const wormedit::section_t& section,
                                   const wormedit::measure_index_t& measures,
                                   WormUiComposeView&) -> bool {
    if (!section.beat_span) {
        report_compose_projection(
            storage.flattened, projection_feedback_kind_t::beat_section_position,
            wormedit::notification_severity_t::error,
            "A beat-anchored section has no valid chart span.", section.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage))
        return true;
    const auto* begin_measure = measures.entry_for(section.beat_span->begin.measure);
    const auto* end_measure = measures.entry_for(section.beat_span->end.measure);
    if (!begin_measure || !end_measure ||
        section.beat_span->begin.row >= begin_measure->row_capacity ||
        section.beat_span->end.row >= end_measure->row_capacity) {
        report_compose_projection(
            storage.flattened, projection_feedback_kind_t::beat_section_geometry,
            wormedit::notification_severity_t::error,
            "A beat-anchored section is outside the chart measure geometry.", section.id.value);
        return false;
    }
    append_compose_marker(
        storage, section.label,
        WormUiComposeSectionView{
            .section_id = section.id.value,
            .label = nullptr,
            .begin_measure_number = static_cast<std::uint64_t>(
                begin_measure - measures.entries.data() + 1),
            .begin_row = section.beat_span->begin.row,
            .end_measure_number = static_cast<std::uint64_t>(
                end_measure - measures.entries.data() + 1),
            .end_row = section.beat_span->end.row,
            .begin_chart_row = begin_measure->absolute_begin_row + section.beat_span->begin.row,
            .end_chart_row = end_measure->absolute_begin_row + section.beat_span->end.row,
            .begin_projection_visible = false,
            .end_projection_visible = false,
            .span_projection_visible = false,
            .projected_begin_y_pixels = 0.0,
            .projected_end_y_pixels = 0.0,
            .projected_span_top_y_pixels = 0.0,
            .projected_span_bottom_y_pixels = 0.0,
            .proposed_begin_projection_visible = false,
            .proposed_end_projection_visible = false,
            .proposed_span_projection_visible = false,
            .proposed_begin_y_pixels = 0.0,
            .proposed_end_y_pixels = 0.0,
            .proposed_span_top_y_pixels = 0.0,
            .proposed_span_bottom_y_pixels = 0.0,
        });
    return true;
}

bool populate_compose_section_storage(frontend_workspace_view_t& flattened,
                                      const wormedit::chart_document_t& chart,
                                      const wormedit::measure_index_t& measures,
                                      WormUiComposeView& view) {
    compose_marker_storage_t beat{flattened, flattened.compose.section, view.beat_section_count,
                                  view.beat_sections_truncated, WORM_UI_COMPOSE_SECTION_CAPACITY};
    compose_marker_storage_t audio{flattened, flattened.compose.audio_marker.section,
                                   view.audio_section_count, view.audio_sections_truncated,
                                   WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY};
    prepare_compose_marker_storage(
        beat, static_cast<std::size_t>(std::ranges::count(
                  chart.sections, wormedit::marker_anchor_t::beat, &wormedit::section_t::anchor)));
    prepare_compose_marker_storage(audio, static_cast<std::size_t>(std::ranges::count(
                                              chart.sections, wormedit::marker_anchor_t::audio_time,
                                              &wormedit::section_t::anchor)));
    view.beat_sections = flattened.compose.section.data();
    view.audio_sections = flattened.compose.audio_marker.section.data();
    for (const auto& section : chart.sections) {
        if (section.anchor == wormedit::marker_anchor_t::audio_time) {
            if (!populate_compose_audio_section(audio, section, view))
                return false;
        } else if (section.anchor == wormedit::marker_anchor_t::beat) {
            if (!populate_compose_beat_section(beat, section, measures, view))
                return false;
        }
    }
    return true;
}

bool populate_compose_bookmark_and_section_storage(frontend_workspace_view_t& flattened,
                                                   const wormedit::chart_document_t& chart,
                                                   const wormedit::measure_index_t& measures,
                                                   WormUiComposeView& view) {
    return populate_compose_bookmark_storage(flattened, chart, measures, view) &&
           populate_compose_section_storage(flattened, chart, measures, view);
}

auto compose_timing_short_number(wormedit::signed_rational_t value) -> std::string {
    if (value.denominator == 0)
        return "?";
    std::array<char, 96> buffer{};
    const auto number =
        wormedit::to_double(value);
    const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number,
                                         std::chars_format::general, 5);
    if (formatted.ec != std::errc{})
        return "?";
    return std::string(buffer.data(), formatted.ptr);
}

auto compose_timing_exact_number(wormedit::signed_rational_t value) -> std::string {
    if (value.denominator == 0)
        return "invalid fraction";
    std::string fraction = std::to_string(value.numerator);
    if (value.denominator != 1) {
        fraction += "/" + std::to_string(value.denominator);
    }
    return fraction;
}

auto compose_timing_decimal_detail(wormedit::signed_rational_t value) -> std::string {
    if (value.denominator == 0)
        return "?";
    const auto exact = compose_timing_exact_number(value);
    if (value.denominator == 1)
        return exact;
    std::array<char, 96> buffer{};
    const auto number =
        wormedit::to_double(value);
    const auto formatted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), number,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (formatted.ec != std::errc{})
        return exact;
    const std::string decimal(buffer.data(), formatted.ptr);
    return decimal + " (exact " + exact + ")";
}

auto compose_speed_transition_detail(const wormedit::speed_event_t& event) -> std::string {
    if (event.transition_beats.numerator == 0 || event.transition_beats.denominator == 0)
        return {};
    std::string text = " · transition " + std::to_string(event.transition_beats.numerator);
    if (event.transition_beats.denominator != 1)
        text += "/" + std::to_string(event.transition_beats.denominator);
    return text + (event.transition_is_seconds ? " s" : " beats");
}

auto compose_pause_unit_label(wormedit::pause_unit_t unit) -> std::string_view {
    switch (unit) {
    case wormedit::pause_unit_t::seconds:
        return "s";
    case wormedit::pause_unit_t::milliseconds:
        return "ms";
    case wormedit::pause_unit_t::bms_stop_ticks:
        return "ticks";
    case wormedit::pause_unit_t::beats:
        return "beats";
    }
    return "";
}

auto compose_initial_bpm(const wormedit::chart_document_t& chart,
                         const wormedit::measure_index_t& measures)
    -> const wormedit::bpm_event_t* {
    const wormedit::bpm_event_t* initial = nullptr;
    for (const auto& bpm : chart.bpms) {
        if ((measures).row_of(bpm.position) == 0 &&
            bpm.beats_per_minute.numerator > 0 && bpm.beats_per_minute.denominator != 0 &&
            (!initial || bpm.provenance.source_order >= initial->provenance.source_order)) {
            initial = &bpm;
        }
    }
    return initial;
}

void assign_compose_timing_marker_fractions(WormUiComposeView& view) {
    std::ranges::sort(view.timing_markers, [](const auto& left, const auto& right) {
        return std::tie(left.lane, left.marker_y_pixels, left.source_order, left.storage_order,
                        left.object_id) < std::tie(right.lane, right.marker_y_pixels,
                                                   right.source_order, right.storage_order,
                                                   right.object_id);
    });
    for (std::size_t first = 0; first < view.timing_markers.size();) {
        auto after = first + 1;
        while (after < view.timing_markers.size() &&
               view.timing_markers[after].lane ==
                   view.timing_markers[first].lane &&
               view.timing_markers[after].marker_y_pixels ==
                   view.timing_markers[first].marker_y_pixels) {
            ++after;
        }
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            view.timing_markers[index].marker_left_fraction =
                static_cast<double>(index - first) / count;
            view.timing_markers[index].marker_right_fraction =
                static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
}

auto compose_timing_marker_selected(const wormedit::editor_interaction_view_model_t& interaction,
                                    wormedit::timing_lane_kind_t lane, std::uint64_t id) -> bool {
    if (id == 0)
        return false;
    const auto* descriptor = wormedit::find_catalog_entry(wormedit::timing_lanes, lane);
    const auto kind = descriptor ? descriptor->selection_kind
                                 : wormedit::editor_selection_object_kind_t::bpm_event;
    return std::ranges::any_of(interaction.selected_objects, [kind, id](const auto& object) {
        return object.kind == kind && object.id.value == id;
    });
}

auto compose_bga_event_selected(const wormedit::editor_interaction_view_model_t& interaction,
                                std::uint64_t id) -> bool {
    return id != 0 && std::ranges::any_of(interaction.selected_objects, [id](const auto& object) {
               return object.kind == wormedit::editor_selection_object_kind_t::bga_event &&
                      object.id.value == id;
           });
}

void build_compose_beat_markers_view(const compose_projection_context_t<>& input) {
    auto& flattened = input.frame.flattened;
    const auto& interaction_view = input.scope.interaction_view;
    auto& view = input.frame.view();
    const auto downscroll = input.scope.downscroll;
    const auto beat_pixels_per_row = input.scope.pixels_per_row;
    const auto visible_beat_y = [&](std::uint64_t row) -> std::optional<double> {
        const auto y =
            compose_project_y(row, beat_pixels_per_row, interaction_view.viewport_scroll_pixels,
                              view.viewport_height_pixels, downscroll);
        if (!std::isfinite(y) || y < 0.0 || y > view.viewport_height_pixels)
            return std::nullopt;
        return y;
    };
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        auto& bookmark = flattened.compose.bookmark[index];
        const auto y = visible_beat_y(bookmark.chart_row);
        bookmark.projection_visible = y.has_value();
        bookmark.projected_y_pixels = y.value_or(0.0);
    }
    for (std::size_t index = 0; index < view.beat_section_count; ++index) {
        auto& section = flattened.compose.section[index];
        const auto begin_y = compose_project_y(section.begin_chart_row, beat_pixels_per_row,
                                               interaction_view.viewport_scroll_pixels,
                                               view.viewport_height_pixels, downscroll);
        const auto end_y = compose_project_y(section.end_chart_row, beat_pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
        section.begin_projection_visible =
            std::isfinite(begin_y) && begin_y >= 0.0 && begin_y <= view.viewport_height_pixels;
        section.end_projection_visible =
            std::isfinite(end_y) && end_y >= 0.0 && end_y <= view.viewport_height_pixels;
        section.projected_begin_y_pixels = section.begin_projection_visible ? begin_y : 0.0;
        section.projected_end_y_pixels = section.end_projection_visible ? end_y : 0.0;
        const auto span_top = std::min(begin_y, end_y);
        const auto span_bottom = std::max(begin_y, end_y);
        section.span_projection_visible = std::isfinite(begin_y) && std::isfinite(end_y) &&
                                          span_bottom >= 0.0 &&
                                          span_top <= view.viewport_height_pixels;
        if (section.span_projection_visible) {
            section.projected_span_top_y_pixels =
                std::clamp(span_top, 0.0, view.viewport_height_pixels);
            section.projected_span_bottom_y_pixels =
                std::clamp(span_bottom, 0.0, view.viewport_height_pixels);
        }
    }
}

bool compose_audio_marker_y_visible(double y, const WormUiComposeView& view) {
    return std::isfinite(y) && y >= 0.0 && y <= view.viewport_height_pixels;
}

void build_compose_audio_markers_view(const compose_projection_context_t<>& input) {
    if (!input.cache().timing.marker_timing_index)
        return;
    const auto& timing_index = *input.cache().timing.marker_timing_index;
    build_compose_audio_bookmarks_view(input, timing_index);
    build_compose_audio_sections_view(input, timing_index);
}

void build_compose_markers_view(
    const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& source) {
    const auto scope = source.scope.for_view(std::as_const(source.scope.interaction_view));
    const auto input = compose_projection_context_t<>::from(source.frame, scope);
    build_compose_beat_markers_view(input);
    build_compose_audio_markers_view(input);
}

void build_compose_timing_payloads(const wormedit::chart_document_t& chart,
                                   const wormedit::measure_index_t& measures,
                                   std::vector<compose_timing_marker_payload_t>& markers) {
    markers.clear();
    append_compose_chart_timing_markers(chart, measures,
        [&measures, &markers](wormedit::local_position_t position, std::uint64_t id, std::size_t source_order,
            std::size_t storage_order, wormedit::timing_lane_kind_t lane, std::string short_label,
            std::string detail, bool fixed_initial) {
            markers.push_back({measures.row_of(position), id, source_order, storage_order, lane,
                               std::move(short_label), std::move(detail), fixed_initial});
        });
}

auto cached_compose_timing_payloads(const wormedit::song_document_t& document,
                                    const wormedit::chart_document_t& chart,
                                    const compose_scene_cache_t& cache)
    -> const std::vector<compose_timing_marker_payload_t>& {
    return cache.timing_payload_cache.get_or_build(compose_scene_source_key_t::from(document, chart),
        std::bind_front(build_compose_timing_payloads, std::cref(chart),
                        std::cref(cache.scene.index.value()->measures)));
}

void populate_compose_timing_markers(
    frontend_workspace_view_t& flattened, const wormedit::song_document_t& document,
    const wormedit::chart_document_t& chart, const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view, WormUiComposeView& view,
    bool downscroll, double pixels_per_row, std::uint64_t canvas_rows,
    const wormedit::timing_lane_pseudonote_outcome_t* timing_stage) {
    view.timing_markers.clear();
    const auto marker_total = chart.bpms.size() + chart.scrolls.size() + chart.speeds.size() +
                              chart.pauses.size() + chart.warps.size() + chart.kiai.size() +
                              chart.timing_anchors.size() + 1;
    flattened.compose.timing_marker_labels.resize(marker_total);
    if (canvas_rows != 0 && pixels_per_row > 0.0) {
        const auto total_rows = static_cast<double>(canvas_rows);
        view.timing_document_begin_fraction = std::clamp(
            interaction_view.viewport_scroll_pixels / pixels_per_row / total_rows, 0.0, 1.0);
        view.timing_document_end_fraction =
            std::clamp((interaction_view.viewport_scroll_pixels + view.viewport_height_pixels) /
                           pixels_per_row / total_rows,
                       view.timing_document_begin_fraction, 1.0);
    }
    const auto append_timing_marker = [&](std::optional<std::uint64_t> row, std::uint64_t id,
                                          std::size_t source_order, std::size_t storage_order,
                                          wormedit::timing_lane_kind_t lane, std::string_view short_label,
                                          std::string_view detail, bool fixed_initial) {
        if (!row) {
            return;
        }
        const auto y =
            compose_project_y(*row, pixels_per_row, interaction_view.viewport_scroll_pixels,
                              view.viewport_height_pixels, downscroll);
        if (!std::isfinite(y) || y < 0.0 || y > view.viewport_height_pixels) {
            return;
        }
        auto& labels = flattened.compose.timing_marker_labels[view.timing_markers.size()];
        labels.first.assign(short_label);
        labels.second.assign(detail);
        view.timing_markers.push_back({
            .object_id = {id},
            .source_order = static_cast<std::uint64_t>(source_order),
            .storage_order = static_cast<std::uint64_t>(storage_order),
            .marker_y_pixels = y,
            .marker_left_fraction = 0.0,
            .marker_right_fraction = 1.0,
            .chart_row = *row,
            .lane = lane,
            .short_label = labels.first,
            .detail = labels.second,
            .fixed_initial = fixed_initial,
            .selected = compose_timing_marker_selected(interaction_view, lane, id),
        });
    };
    if (cache.timing_payload_cache_enabled) {
        for (const auto& marker : cached_compose_timing_payloads(document, chart, cache))
            append_timing_marker(marker.row, marker.id, marker.source_order, marker.storage_order,
                                 marker.lane, marker.short_label, marker.detail,
                                 marker.fixed_initial);
    } else {
        append_compose_chart_timing_markers(
            chart, cache.scene.index.value()->measures,
            [&](wormedit::local_position_t position, std::uint64_t id, std::size_t source_order,
                std::size_t storage_order, wormedit::timing_lane_kind_t lane, std::string short_label,
                std::string detail, bool fixed_initial) {
                append_timing_marker((cache.scene.index.value()->measures).row_of(position), id,
                                     source_order, storage_order, lane, std::move(short_label),
                                     std::move(detail), fixed_initial);
            });
    }
    assign_compose_timing_marker_fractions(view);

    const auto* stage_report = timing_stage ? timing_stage->report() : nullptr;
    if (stage_report &&
        stage_report->lifecycle.status() == wormedit::proposal_status_t::active &&
        timing_stage->captured_stamp().is_current(chart.id, view.document_revision)) {
        const auto stage_row =
            (cache.scene.index.value()->measures).row_of(stage_report->position);
        if (stage_row) {
            const auto y = compose_project_y(*stage_row, pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
            if (std::isfinite(y) && y >= 0.0 && y <= view.viewport_height_pixels) {
                view.timing_stage_active = true;
                view.timing_stage_valid = timing_stage->value.has_value();
                view.timing_stage_lane = stage_report->kind;
                view.timing_stage_y_pixels = y;
            }
        }
    }
}

void assign_dead_span_label(std::string& label, const wormedit::sys::dead_span_band_t& band) {
    using wormedit::sys::dead_span_kind_t;
    if (band.kind == dead_span_kind_t::warp) {
        label = "WARP";
        return;
    }
    const auto seconds =
        wormedit::to_double(band.seconds);
    char text[48]{};
    std::snprintf(text, sizeof(text), "%s %.3fs",
                  band.kind == dead_span_kind_t::stop ? "STOP" : "DELAY", seconds);
    label = text;
}

void populate_compose_dead_spans(frontend_workspace_view_t& flattened,
                                 const compose_scene_cache_t& cache,
                                 const wormedit::editor_interaction_view_model_t& interaction_view,
                                 WormUiComposeView& view, bool downscroll, double pixels_per_row) {
    view.dead_spans.clear();

    if (!cache.timing.dead_spans)
        return;
    auto& bands = flattened.compose.timing_geometry.dead_bands;
    wormedit::sys::build_dead_span_bands_view(
        *cache.timing.dead_spans, pixels_per_row, interaction_view.viewport_scroll_pixels,
        view.viewport_height_pixels, downscroll, wormedit::sys::dead_span_pause_band_pixels, bands);
    const auto count = bands.size();
    view.dead_spans.reserve(count);
    flattened.compose.dead_span_labels.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& band = bands[index];
        auto& label = flattened.compose.dead_span_labels[index];
        assign_dead_span_label(label, band);
        view.dead_spans.push_back(
            {.bounds = {0.0, band.top, view.viewport_width_pixels, band.bottom},
             .kind = band.kind == dead_span_kind_t::warp ? timing_lane_kind_t::warp
                     : band.kind == dead_span_kind_t::stop ? timing_lane_kind_t::stop
                                                          : timing_lane_kind_t::delay,
             .object_id = band.object,
             .label = label});
    }
}

void populate_compose_kiai_and_sv(frontend_workspace_view_t& flattened,
                                  const compose_scene_cache_t& cache,
                                  const wormedit::editor_interaction_view_model_t& interaction_view,
                                  WormUiComposeView& view, bool downscroll, double pixels_per_row,
                                  std::uint64_t total_rows, std::uint64_t kiai_selected_head) {
    view.kiai_spans.clear();
    view.sv_graph_points.clear();

    view.sv_graph_one_fraction = -1.0;
    if (!(pixels_per_row > 0.0))
        return;
    const auto scroll = interaction_view.viewport_scroll_pixels;
    const auto height = view.viewport_height_pixels;
    const auto row_to_y = [&](std::uint64_t row) {
        return compose_project_y(static_cast<long double>(row), pixels_per_row, scroll, height,
                                 downscroll);
    };
    if (cache.timing.kiai_spans) {
        for (const auto& span : cache.timing.kiai_spans->spans) {
            const auto begin_y = row_to_y(span.begin_row);
            const auto end_y = row_to_y(span.end_row);
            const auto top = std::min(begin_y, end_y);
            const auto bottom = std::max(begin_y, end_y);
            if (bottom < 0.0 || top > height)
                continue;
            view.kiai_spans.push_back(
                {.bounds = {0.0, top, 0.0, bottom},
                 .head_object_id = span.head,
                 .tail_object_id = span.tail,
                 .begin_row = span.begin_row,
                 .end_row = span.end_row,
                 .open_ended = span.open_ended,
                 .head_visible = begin_y >= 0.0 && begin_y <= height,
                 .tail_visible = !span.open_ended && end_y >= 0.0 && end_y <= height,
                 .selected = span.head.value == kiai_selected_head});
        }
    }
    if (cache.timing.sv_steps && !cache.timing.sv_steps->steps.empty()) {
        const auto first =
            static_cast<std::uint64_t>(std::max(0.0, std::floor(scroll / pixels_per_row)));
        const auto last = std::min<std::uint64_t>(
            total_rows, static_cast<std::uint64_t>(std::ceil((scroll + height) / pixels_per_row)));
        auto& graph = flattened.compose.timing_geometry.sv_graph;
        wormedit::sys::build_sv_graph(*cache.timing.sv_steps, first, last, row_to_y, graph);
        for (const auto& point : graph.points) {
            view.sv_graph_points.push_back({point.x, point.y});
        }

        view.sv_graph_minimum = graph.minimum;
        view.sv_graph_maximum = graph.maximum;
        view.sv_graph_one_fraction = graph.one_x;
    }
}

void populate_compose_recipe_areas(
    frontend_workspace_view_t& flattened, const wormedit::chart_document_t& chart,
    const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view, WormUiComposeView& view,
    bool downscroll, double pixels_per_row, std::uint64_t total_rows, std::uint64_t selected_recipe,
    const recipe_form_outcome_t* preview) {
    view.recipe_areas.clear();
    view.recipe_graph_points.clear();

    if (!cache.timing.recipe_areas || !(pixels_per_row > 0.0))
        return;
    const auto* report = preview ? preview->report() : nullptr;
    const auto scroll = interaction_view.viewport_scroll_pixels;
    const auto height = view.viewport_height_pixels;
    const auto row_to_y = [&](std::uint64_t row) {
        return compose_project_y(static_cast<long double>(row), pixels_per_row, scroll, height,
                                 downscroll);
    };
    const auto first =
        static_cast<std::uint64_t>(std::max(0.0, std::floor(scroll / pixels_per_row)));
    const auto last = std::min<std::uint64_t>(
        total_rows, static_cast<std::uint64_t>(std::ceil((scroll + height) / pixels_per_row)));
    flattened.compose.recipe_area_labels.resize(cache.timing.recipe_areas->areas.size());
    for (const auto& stored_area : cache.timing.recipe_areas->areas) {
        const auto* shown = &stored_area;
        wormedit::sys::recipe_area_t previewed;
        const bool previewing = report && report->candidate.id == stored_area.recipe;
        if (previewing) {
            // The unapplied candidate replaces the stored values in this area's graph.
            previewed = stored_area;
            const auto sampled = wormedit::sv_recipe_samples_t::from(chart, report->candidate);
            previewed.evaluable = sampled.diagnostics.empty() && !sampled.value.points.empty();
            previewed.points.clear();
            for (const auto& point : sampled.value.points)
                previewed.points.push_back({point.absolute_row, point.value});
            shown = &previewed;
        }
        const auto& area = *shown;
        if (area.end_row <= first || area.begin_row >= last)
            continue;
        const auto begin_y = row_to_y(area.begin_row);
        const auto end_y = row_to_y(area.end_row);
        auto& graph = flattened.compose.timing_geometry.recipe_graph;
        wormedit::sys::build_recipe_graph(area, first, last, row_to_y, graph);
        const auto graph_first = view.recipe_graph_points.size();
        for (const auto& point : graph.points) {
            view.recipe_graph_points.push_back({point.x, point.y});
        }
        auto& label = flattened.compose.recipe_area_labels[view.recipe_areas.size()];
        label.assign(area.name);
        if (area.curve == wormedit::sv_recipe_curve_t::lua_expression)
            label += " (Lua)";
        if (!area.enabled)
            label += " (off)";
        view.recipe_areas.push_back(
            {.bounds = {0.0, std::min(begin_y, end_y), 0.0, std::max(begin_y, end_y)},
             .recipe_id = area.recipe,
             .label = label,
             .lane = area.output == wormedit::sv_recipe_output_t::speeds
                         ? wormedit::timing_lane_kind_t::speed
                         : wormedit::timing_lane_kind_t::scroll,
             .curve = area.curve,
             .enabled = area.enabled,
             .selected = area.recipe.value == selected_recipe,
             .evaluable = area.evaluable,
             .begin_visible = begin_y >= 0.0 && begin_y <= height,
             .end_visible = end_y >= 0.0 && end_y <= height,
             .previewing = previewing,
             .graph_first = graph_first,
             .graph_count = view.recipe_graph_points.size() - graph_first});
    }
}

void populate_compose_speed_spans(frontend_workspace_view_t& flattened,
                                  const compose_scene_cache_t& cache,
                                  const wormedit::editor_interaction_view_model_t& interaction_view,
                                  WormUiComposeView& view, bool downscroll, double pixels_per_row) {
    view.speed_spans.clear();
    view.speed_graph_points.clear();

    view.speed_graph_one_fraction = -1.0;
    if (!cache.timing.speed_spans || !(pixels_per_row > 0.0))
        return;
    const auto scroll = interaction_view.viewport_scroll_pixels;
    const auto height = view.viewport_height_pixels;
    if (!cache.timing.speed_spans->spans.empty()) {
        auto& graph = flattened.compose.timing_geometry.speed_graph;
        wormedit::sys::build_speed_graph(
            *cache.timing.speed_spans, std::max(0.0, scroll / pixels_per_row),
            (scroll + height) / pixels_per_row, [&](double row) {
                return compose_project_y(row, pixels_per_row, scroll, height, downscroll);
            }, graph);
        for (const auto& point : graph.points) {
            view.speed_graph_points.push_back({point.x, point.y});
        }

        view.speed_graph_one_fraction = graph.one_x;
    }
    for (const auto& span : cache.timing.speed_spans->spans) {
        const auto begin_y =
            compose_project_y(span.begin_row, pixels_per_row, scroll, height, downscroll);
        const auto end_y =
            compose_project_y(span.end_row, pixels_per_row, scroll, height, downscroll);
        const auto top = std::min(begin_y, end_y);
        const auto bottom = std::max(begin_y, end_y);
        if (bottom < 0.0 || top > height)
            continue;
        view.speed_spans.push_back({.bounds = {0.0, top, 0.0, bottom},
                                    .object_id = span.object,
                                    .multiplier = span.multiplier,
                                    .transition_length = span.transition_length,
                                    .transition_is_seconds = span.transition_is_seconds,
                                    .clipped = span.clipped});
    }
}

void drop_stale_recipe_preview(application_command_state_t& application_state,
                               const wormedit::chart_document_t& chart, std::uint64_t revision) {
    auto& preview = application_state.project.timing_recipe.recipe_preview;
    if (!preview)
        return;
    const auto* report = preview->report();
    const bool current = report && preview->captured_stamp().is_current(chart.id, revision) &&
                         report->candidate.id.value == application_state.project.timing_recipe.recipe_selected &&
                         std::ranges::any_of(chart.sv_recipes, [&](const auto& recipe) {
                             return recipe.id == report->candidate.id;
                         });
    if (!current)
        preview.reset();
}

void populate_compose_recipe_form(frontend_workspace_view_t& flattened,
                                  const wormedit::chart_document_t& chart,
                                  const application_command_state_t& application_state,
                                  WormUiComposeView& view) {
    flattened.compose.recipe_form_text.clear();
    view.recipe_form = {};
    const auto recipe = std::ranges::find(chart.sv_recipes,
                                          wormedit::recipe_id_t{application_state.project.timing_recipe.recipe_selected},
                                          &wormedit::sv_recipe_t::id);
    if (application_state.project.timing_recipe.recipe_selected == 0 || recipe == chart.sv_recipes.end())
        return;
    flattened.compose.recipe_form_text.reserve(3);
    const auto& name = flattened.compose.recipe_form_text.emplace_back(recipe->name);
    const auto& source = flattened.compose.recipe_form_text.emplace_back(recipe->expression_source);
    const auto& status = flattened.compose.recipe_form_text.emplace_back(
        application_state.project.timing_recipe.recipe_preview ? std::string{application_state.project.timing_recipe.recipe_preview->diagnostics.empty()
            ? std::string_view{} : application_state.project.timing_recipe.recipe_preview->diagnostics.front().message} : std::string{});
    view.recipe_form.visible = true;
    view.recipe_form.recipe_id = recipe->id.value;
    view.recipe_form.enabled = recipe->enabled;
    view.recipe_form.name = name.c_str();
    view.recipe_form.expression_source = source.c_str();
    view.recipe_form.status_text = status.empty() ? nullptr : status.c_str();
    view.recipe_form.form = wormedit::sys::recipe_form_from_recipe(*recipe);
}
} // namespace wormedit::sys::compose_projection_detail
