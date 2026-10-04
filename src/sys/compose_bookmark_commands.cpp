#include "sys/compose_bookmark_commands.h"

#include <wormedit/audio/decoder.h>
#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <variant>

namespace wormedit::sys {
namespace {

auto fail(std::string status) -> bookmark_command_outcome_t {
    return {.status = std::move(status)};
}

auto find_chart(const document_history_t& history, chart_id_t id) -> const chart_document_t* {
    const auto found = std::ranges::find(history.document.charts, id, &chart_document_t::id);
    return found == history.document.charts.end() ? nullptr : &*found;
}

auto is_playing(const application_command_state_t& application) -> bool {
    return application.transport_preview_playing && application.transport_preview_playing();
}

auto beat_position_for_audio_milliseconds(const chart_document_t& chart,
                                          std::int64_t milliseconds)
    -> std::optional<local_position_t> {
    const auto measures = build_measure_index(chart);
    const auto timing = build_timing_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures) || !timing.index)
        return std::nullopt;
    const auto query = musical_position_at(*timing.index,
                                           signed_rational_t{milliseconds, 1'000});
    if (!query.absolute_row || query.absolute_row->numerator < 0 ||
        query.absolute_row->denominator == 0)
        return std::nullopt;
    const auto row = static_cast<std::uint64_t>(query.absolute_row->numerator) /
                     query.absolute_row->denominator;
    return local_position_for_row(std::get<measure_index_t>(measures), row);
}

auto seek_playhead_to(const chart_document_t& chart, application_command_state_t& application,
                      local_position_t position, std::string& status) -> bool {
    const auto built = build_measure_index(chart);
    const auto timing = build_timing_index(chart);
    if (!std::holds_alternative<measure_index_t>(built) || !timing.index) {
        status = "Bookmark time could not be resolved.";
        return false;
    }
    const auto end = application.transport_preview_end_frame
                         ? application.transport_preview_end_frame() : std::nullopt;
    const auto frame = bookmark_preview_frame(*timing.index, std::get<measure_index_t>(built),
                                              position, end);
    if (!frame || !application.transport_seek) {
        status = "Chart preview seek is unavailable.";
        return false;
    }
    status = application.transport_seek(*frame);
    return true;
}

} // namespace

auto local_position_for_row(const measure_index_t& index, std::uint64_t row)
    -> std::optional<local_position_t> {
    if (row >= index.total_rows) return std::nullopt;
    const auto after = std::ranges::upper_bound(index.entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin()) return std::nullopt;
    const auto& measure = *std::prev(after);
    return local_position_t{measure.id, row - measure.absolute_begin_row};
}

auto nearest_snap_row(double absolute_row, std::uint64_t divisions_per_measure,
                      std::uint64_t snap_denominator, std::uint64_t total_rows)
    -> std::optional<std::uint64_t> {
    if (total_rows == 0 || !std::isfinite(absolute_row) || snap_denominator == 0 ||
        divisions_per_measure % snap_denominator != 0)
        return std::nullopt;
    const auto step = divisions_per_measure / snap_denominator;
    if (step == 0) return std::nullopt;
    const auto clamped = std::clamp(absolute_row, 0.0, static_cast<double>(total_rows - 1));
    const auto lower = static_cast<std::uint64_t>(std::floor(clamped / static_cast<double>(step)));
    const auto below = lower * step;
    const auto above = below + step;
    const auto distance_below = clamped - static_cast<double>(below);
    const auto distance_above = static_cast<double>(above) - clamped;
    // Ties go to the earlier row; a snap row past the last row is never chosen.
    return distance_below <= distance_above || above >= total_rows ? below : above;
}

auto bookmark_preview_frame(const timing_index_t& timing, const measure_index_t& measures,
                            local_position_t position, std::optional<std::uint64_t> end_frame)
    -> std::optional<std::uint64_t> {
    const auto at = timing_at(timing, measures, position);
    if (!at.seconds || at.seconds->denominator == 0) return std::nullopt;
    const auto frames = std::llround(static_cast<long double>(at.seconds->numerator) *
                                     audio::output_sample_rate / at.seconds->denominator);
    auto frame = static_cast<std::uint64_t>(std::max<long long>(frames, 0));
    if (end_frame) frame = std::min(frame, *end_frame);
    return frame;
}

namespace {

auto playing_bookmark_origin(const chart_document_t& chart,
                              const application_command_state_t& application,
                              marker_navigation_direction_t direction)
    -> std::optional<local_position_t> {
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built) ||
        !application.preview_playhead_absolute_row ||
        !std::isfinite(*application.preview_playhead_absolute_row))
        return std::nullopt;
    const auto& index = std::get<measure_index_t>(built);
    if (index.total_rows == 0) return std::nullopt;
    const auto row = std::clamp(*application.preview_playhead_absolute_row, 0.0,
                                static_cast<double>(index.total_rows - 1));
    // Marker lookup is strict: a fractional playhead must include the row below
    // it when moving backward, and exclude that row when moving forward.
    const auto origin = direction == marker_navigation_direction_t::previous
                            ? std::ceil(row) : std::floor(row);
    return local_position_for_row(index, static_cast<std::uint64_t>(origin));
}

} // namespace

auto navigate_beat_bookmark(document_history_t& history, editor_interaction_state_t& state,
                            application_command_state_t& application,
                            marker_navigation_direction_t direction) -> bookmark_command_outcome_t {
    const bool previous = direction == marker_navigation_direction_t::previous;
    const auto* chart = find_chart(history, state.chart);
    if (!chart) return fail("Bookmark navigation requires the active Compose chart.");
    const bool playing = is_playing(application);
    const auto origin = playing ? playing_bookmark_origin(*chart, application, direction)
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
    if (playing) {
        std::string status;
        const auto sought = seek_playhead_to(*chart, application, target, status);
        return {.accepted = sought, .status = std::move(status), .feedback_posted = sought && application.transport_seek_posts_feedback};
    }
    const auto jumped = dispatch_editor_action(history, state,
        jump_edit_cursor_action_t{target, marker.document_revision});
    return {.accepted = jumped.accepted,
            .status = jumped.accepted ? (previous ? "Moved to the previous bookmark."
                                                  : "Moved to the next bookmark.")
                                      : "Bookmark navigation was rejected.",
            .diagnostics = jumped.diagnostics};
}

auto navigate_to_bookmark(document_history_t& history, editor_interaction_state_t& state,
                          application_command_state_t& application,
                          std::uint64_t expected_revision, chart_id_t chart_id,
                          object_id_t bookmark_id) -> bookmark_command_outcome_t {
    if (history.document.revision != expected_revision || state.chart != chart_id)
        return fail("Bookmark chooser entry is stale.");
    const auto* chart = find_chart(history, chart_id);
    if (!chart) return fail("Bookmark chooser chart is unavailable.");
    const auto bookmark = std::ranges::find(chart->bookmarks, bookmark_id, &bookmark_t::id);
    if (bookmark == chart->bookmarks.end())
        return fail("Bookmark is no longer available.");
    if (bookmark->anchor == marker_anchor_t::beat && bookmark->position) {
        if (is_playing(application)) {
            std::string status;
            const auto sought = seek_playhead_to(*chart, application, *bookmark->position, status);
            return {.accepted = sought, .status = std::move(status), .feedback_posted = sought && application.transport_seek_posts_feedback};
        }
        const auto jumped = dispatch_editor_action(history, state,
            jump_edit_cursor_action_t{*bookmark->position, expected_revision});
        return {.accepted = jumped.accepted,
                .status = jumped.accepted ? "Moved to the selected bookmark."
                                          : "Bookmark navigation was rejected.",
                .diagnostics = jumped.diagnostics};
    }
    if (bookmark->anchor != marker_anchor_t::audio_time ||
        !bookmark->audio_milliseconds)
        return fail("Bookmark has no navigable position.");
    if (is_playing(application)) {
        if (!application.transport_seek_audio_milliseconds)
            return fail("Audio-time bookmark seek is unavailable.");
        return {.accepted = true,
                .status = application.transport_seek_audio_milliseconds(
                    *bookmark->audio_milliseconds),
                .feedback_posted = application.transport_seek_posts_feedback};
    }
    const auto position = beat_position_for_audio_milliseconds(*chart,
                                                               *bookmark->audio_milliseconds);
    if (!position) return fail("Audio-time bookmark has no musical position in this chart.");
    const auto jumped = dispatch_editor_action(history, state,
        jump_edit_cursor_action_t{*position, expected_revision});
    return {.accepted = jumped.accepted,
            .status = jumped.accepted ? "Moved to the selected bookmark."
                                      : "Bookmark navigation was rejected.",
            .diagnostics = jumped.diagnostics};
}

auto add_beat_bookmark_here(document_history_t& history, editor_interaction_state_t& state,
                            application_command_state_t& application)
    -> bookmark_command_outcome_t {
    const auto* chart = find_chart(history, state.chart);
    if (!chart) return fail("Choose a chart before editing bookmarks.");
    auto position = state.cursor;
    if (is_playing(application)) {
        const auto built = build_measure_index(*chart);
        if (!application.preview_playhead_absolute_row ||
            !std::holds_alternative<measure_index_t>(built))
            return fail("The playhead row is not available yet.");
        const auto& index = std::get<measure_index_t>(built);
        const auto row = nearest_snap_row(*application.preview_playhead_absolute_row,
                                          chart->divisions_per_standard_measure,
                                          state.active_snap_denominator, index.total_rows);
        const auto snapped = row ? local_position_for_row(index, *row) : std::nullopt;
        if (!snapped) return fail("The playhead is not on an editable row.");
        position = *snapped;
    } else if (state.virtual_bms_cursor_row) {
        return fail("Beat bookmarks require a materialized cursor measure.");
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
