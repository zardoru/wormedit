#include "sys/compose_navigation.h"
#include "sys/bookmark_commands.h"
#include <wormedit/core/snap.h>

#include <wormedit/audio/decoder.h>
#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>
#include <variant>

namespace wormedit::sys {
namespace {

auto fail(std::string status) -> bookmark_command_outcome_t {
    return {.status = std::move(status)};
}

auto is_playing(const application_command_state_t& application) -> bool {
    return application.services.transport_preview_playing && application.services.transport_preview_playing();
}

auto beat_position_for_audio_milliseconds(const chart_document_t& chart,
                                          std::int64_t milliseconds)
    -> std::optional<local_position_t> {
    const auto measures = build_measure_index(chart);
    const auto timing = build_timing_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures) || !timing.index)
        return std::nullopt;
    const auto query = timing.index->row_at(signed_rational_t{milliseconds, 1'000});
    if (!query.absolute_row || query.absolute_row->numerator < 0 ||
        query.absolute_row->denominator == 0)
        return std::nullopt;
    const auto row = static_cast<std::uint64_t>(query.absolute_row->numerator) /
                     query.absolute_row->denominator;
    return (std::get<measure_index_t>(measures)).position_at(row);
}

} // namespace

auto nearest_snap_row(const chart_document_t& chart, const measure_index_t& index,
                      double absolute_row, std::uint64_t snap_denominator)
    -> std::optional<std::uint64_t> {
    if (index.total_rows == 0 || !std::isfinite(absolute_row)) return std::nullopt;
    const auto row = std::clamp(static_cast<long double>(absolute_row), 0.0L,
                                static_cast<long double>(index.total_rows - 1));
    return snap_canvas_row(chart, index, row, snap_denominator, index.total_rows);
}

auto bookmark_preview_frame(const timing_index_t& timing, const measure_index_t& measures,
                            local_position_t position, std::optional<std::uint64_t> end_frame)
    -> std::optional<std::uint64_t> {
    const auto at = timing.seconds_at(measures, position);
    if (!at.seconds || at.seconds->denominator == 0) return std::nullopt;
    const auto frames = std::llround(static_cast<long double>(at.seconds->numerator) *
                                     audio::output_sample_rate / at.seconds->denominator);
    auto frame = static_cast<std::uint64_t>(std::max<long long>(frames, 0));
    if (end_frame) frame = std::min(frame, *end_frame);
    return frame;
}

namespace {

auto absolute_row_of(const measure_index_t& index, local_position_t position)
    -> std::optional<std::uint64_t> {
    const auto entry = std::ranges::find(index.entries, position.measure,
                                         &measure_index_entry_t::id);
    if (entry == index.entries.end()) return std::nullopt;
    return entry->absolute_begin_row + position.row;
}

auto playing_bookmark_origin(const chart_document_t& chart, chart_id_t chart_id,
                              const application_command_state_t& application,
                              marker_navigation_direction_t direction)
    -> std::optional<local_position_t> {
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built) ||
        !application.project.transport.preview_playhead_absolute_row ||
        !std::isfinite(*application.project.transport.preview_playhead_absolute_row))
        return std::nullopt;
    const auto& index = std::get<measure_index_t>(built);
    if (index.total_rows == 0) return std::nullopt;
    if (direction == marker_navigation_direction_t::previous)
        if (const auto recent = application.bookmarks.recent_seek_row(chart_id))
            return (index).position_at(*recent);
    const auto row = std::clamp(*application.project.transport.preview_playhead_absolute_row, 0.0,
                                static_cast<double>(index.total_rows - 1));
    // Marker lookup is strict: a fractional playhead must include the row below
    // it when moving backward, and exclude that row when moving forward.
    const auto origin = direction == marker_navigation_direction_t::previous
                            ? std::ceil(row) : std::floor(row);
    return (index).position_at(static_cast<std::uint64_t>(origin));
}

void record_playing_seek(const chart_document_t& chart, chart_id_t chart_id,
                         application_command_state_t& application, local_position_t target,
                         bool sought) {
    application.bookmarks.clear_seek_record();
    if (!sought) return;
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built)) return;
    if (const auto row = absolute_row_of(std::get<measure_index_t>(built), target))
        application.bookmarks.record_seek(chart_id, *row);
}

} // namespace

auto bookmark_navigation_t::navigate_beat_bookmark(document_history_t& history, editor_interaction_state_t& state,
                            application_command_state_t& application,
                            marker_navigation_direction_t direction) -> bookmark_command_outcome_t {
    const bool previous = direction == marker_navigation_direction_t::previous;
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart) return fail("Bookmark navigation requires the active Compose chart.");
    const bool playing = is_playing(application);
    if (!playing) application.bookmarks.clear_seek_record();
    const auto origin = playing ? playing_bookmark_origin(*chart, state.chart, application, direction)
                                : std::optional<local_position_t>{state.cursor};
    if (!origin) return fail("The playhead row is not available yet.");
    const auto marker = navigate_markers(history.document, history.document.revision, state.chart,
        *origin, direction, {.include_bookmarks = true, .include_section_begins = false,
                                  .include_section_ends = false});
    if (marker.status == marker_navigation_status_t::no_result)
        return {.accepted = true,
                .status = previous ? "No previous bookmark." : "No next bookmark."};
    if (!marker || !marker.item->beat_position)
        return {.status = marker.diagnostics.empty()
                              ? "Bookmark navigation could not resolve an exact beat row."
                              : marker.diagnostics.front().message,
                .diagnostics = marker.diagnostics};
    const auto target = *marker.item->beat_position;
    auto outcome = navigate_compose_position(history, state, application,
        {.position = target}, marker.document_revision);
    if (playing) record_playing_seek(*chart, state.chart, application, target, outcome.accepted);
    else if (outcome.accepted) outcome.status = previous
        ? "Moved to the previous bookmark." : "Moved to the next bookmark.";
    return outcome;
}

auto bookmark_navigation_t::navigate_to_bookmark(document_history_t& history, editor_interaction_state_t& state,
                          application_command_state_t& application,
                          std::uint64_t expected_revision, chart_id_t chart_id,
                          object_id_t bookmark_id) -> bookmark_command_outcome_t {
    if (history.document.revision != expected_revision || state.chart != chart_id)
        return fail("Bookmark chooser entry is stale.");
    // A chooser jump is a seek of its own: it ends any repeat sequence in progress.
    application.invalidate_playing_seek_records();
    const auto* chart = history.document.find_chart(chart_id);
    if (!chart) return fail("Bookmark chooser chart is unavailable.");
    const auto bookmark = std::ranges::find(chart->bookmarks, bookmark_id, &bookmark_t::id);
    if (bookmark == chart->bookmarks.end())
        return fail("Bookmark is no longer available.");
    compose_navigation_target_t target;
    if (bookmark->anchor == marker_anchor_t::beat && bookmark->position) {
        target.position = *bookmark->position;
    } else if (bookmark->anchor == marker_anchor_t::audio_time && bookmark->audio_milliseconds) {
        target.audio_milliseconds = bookmark->audio_milliseconds;
        if (!is_playing(application)) {
            const auto position = beat_position_for_audio_milliseconds(*chart, *bookmark->audio_milliseconds);
            if (!position) return fail("Audio-time bookmark has no musical position in this chart.");
            target.position = *position;
        }
    } else return fail("Bookmark has no navigable position.");
    auto outcome = navigate_compose_position(history, state, application, target, expected_revision);
    if (outcome.accepted && !is_playing(application)) outcome.status = "Moved to the selected bookmark.";
    return outcome;
}

auto bookmark_navigation_t::add_beat_bookmark_here(document_history_t& history, editor_interaction_state_t& state,
                            application_command_state_t& application)
    -> bookmark_command_outcome_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart) return fail("Choose a chart before editing bookmarks.");
    auto position = state.cursor;
    if (is_playing(application)) {
        const auto built = build_measure_index(*chart);
        if (!application.project.transport.preview_playhead_absolute_row ||
            !std::holds_alternative<measure_index_t>(built))
            return fail("The playhead row is not available yet.");
        const auto& index = std::get<measure_index_t>(built);
        const auto row = nearest_snap_row(*chart, index, *application.project.transport.preview_playhead_absolute_row,
                                          state.active_snap_denominator);
        const auto snapped = row ? (index).position_at(*row) : std::nullopt;
        if (!snapped) return fail("The playhead is not on an editable row.");
        position = *snapped;
    } else if (state.virtual_bms_cursor_row) {
        return fail("Bookmarks can only go inside existing measures. Add measures up to the cursor first.");
    }
    const auto exists = std::ranges::any_of(chart->bookmarks, [&](const bookmark_t& bookmark) {
        return bookmark.anchor == marker_anchor_t::beat && bookmark.position == position;
    });
    if (exists) return fail("A bookmark already exists on this row.");
    const auto applied = apply_marker_edit(history, history.document.revision,
        marker_edit_action_t{insert_bookmark_action_t{state.chart, {}, marker_anchor_t::beat,
                                                      position, std::nullopt}});
    if (!applied.applied)
        return {.status = applied.diagnostics.empty() ? "Bookmark edit made no changes."
                                                      : applied.diagnostics.front().message,
                .diagnostics = applied.diagnostics};
    return {.accepted = true, .document_changed = true,
            .status = is_playing(application) ? "Bookmark added at the playhead."
                                              : "Bookmark added at the Compose cursor."};
}

} // namespace wormedit::sys
