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

auto is_playing(const application_command_state_t& application) -> bool {
    return application.services.transport_preview_playing && application.services.transport_preview_playing();
}

auto beat_position_for_audio_milliseconds(const chart_indices_t& indices,
                                          std::int64_t milliseconds)
    -> std::optional<local_position_t> {
    if (!indices.measures || !indices.timing.value)
        return std::nullopt;
    const auto query = indices.timing.value->query_position(signed_rational_t{milliseconds, 1'000});
    if (!query || query->value.numerator < 0 ||
        query->value.denominator == 0)
        return std::nullopt;
    const auto row = static_cast<std::uint64_t>(query->value.numerator) /
                     query->value.denominator;
    return indices.measures->position_at(row);
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
    const auto at = timing.query_seconds(measures, position);
    if (!at || at->value.denominator == 0) return std::nullopt;
    const auto frames = std::llround(static_cast<long double>(at->value.numerator) *
                                     audio::output_sample_rate / at->value.denominator);
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

auto playing_bookmark_origin(const chart_indices_t& indices, chart_id_t chart_id,
                              const application_command_state_t& application,
                              marker_navigation_direction_t direction)
    -> std::optional<local_position_t> {
    if (!indices.measures ||
        !application.project.transport.preview_playhead_absolute_row ||
        !std::isfinite(*application.project.transport.preview_playhead_absolute_row))
        return std::nullopt;
    const auto& index = *indices.measures;
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

void record_playing_seek(const chart_indices_t& indices, chart_id_t chart_id,
                         application_command_state_t& application, local_position_t target,
                         bool sought) {
    application.bookmarks.clear_seek_record();
    if (!sought) return;
    if (!indices.measures) return;
    if (const auto row = absolute_row_of(*indices.measures, target))
        application.bookmarks.record_seek(chart_id, *row);
}

} // namespace

auto bookmark_navigation_t::navigate_beat_bookmark(document_history_t& history,
                                                   editor_interaction_state_t& state,
                                                   application_command_state_t& application,
                                                   marker_navigation_direction_t direction)
    -> compose_action_outcome_t {
    const bool previous = direction == marker_navigation_direction_t::previous;
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return compose_action_outcome_t::blocked(
            "Bookmark navigation requires the active Compose chart.", notification_source_t::input);
    const auto& indices = *history.indices(state.chart);
    const bool playing = is_playing(application);
    if (!playing) application.bookmarks.clear_seek_record();
    const auto origin = playing ? playing_bookmark_origin(indices, state.chart, application, direction)
                                : std::optional<local_position_t>{state.cursor};
    if (!origin)
        return compose_action_outcome_t::blocked("The playhead row is not available yet.",
                                                 notification_source_t::input);
    auto marker = marker_navigation_match_t::from(history.document, history.document.revision,
                                                  state.chart, *origin, direction,
                                                  {.include_bookmarks = true,
                                                   .include_section_begins = false,
                                                   .include_section_ends = false});
    if (marker.value.status == marker_navigation_status_t::no_result)
        return compose_action_outcome_t::from(
            previous ? "No previous bookmark." : "No next bookmark.", notification_severity_t::hint,
            notification_source_t::input, false, true);
    if (!marker.value.is_found() || !marker.diagnostics.empty() ||
        !marker.value.item->beat_position) {
        auto outcome = compose_action_outcome_t::from(
            marker.diagnostics.empty() ? "Bookmark navigation could not resolve an exact beat row."
                                       : marker.diagnostics.front().message,
            to_notification_severity(marker.diagnostics, notification_severity_t::warning),
            notification_source_t::input, false, false);
        outcome.diagnostics = std::move(marker.diagnostics);
        return outcome;
    }
    const auto target = *marker.value.item->beat_position;
    auto outcome = navigate_compose_position(history, state, application,
        {.position = target}, marker.value.document_revision);
    if (playing) record_playing_seek(indices, state.chart, application, target, outcome.accepted);
    else if (outcome.accepted)
        outcome.message =
            previous ? "Moved to the previous bookmark." : "Moved to the next bookmark.";
    return outcome;
}

auto bookmark_navigation_t::navigate_to_bookmark(document_history_t& history,
                                                 editor_interaction_state_t& state,
                                                 application_command_state_t& application,
                                                 std::uint64_t expected_revision,
                                                 chart_id_t chart_id, object_id_t bookmark_id)
    -> compose_action_outcome_t {
    if (!chart_stamp_t{.document = {.project = history.document.id, .revision = expected_revision},
                       .chart = chart_id}.is_current(history.document, state.chart))
        return compose_action_outcome_t::blocked(stale_action_message("Bookmark chooser"),
                                                 notification_source_t::input);
    // A chooser jump is a seek of its own: it ends any repeat sequence in progress.
    application.invalidate_playing_seek_records();
    const auto* chart = history.document.find_chart(chart_id);
    if (!chart)
        return compose_action_outcome_t::blocked("Bookmark chooser chart is unavailable.",
                                                 notification_source_t::input);
    const auto bookmark = std::ranges::find(chart->bookmarks, bookmark_id, &bookmark_t::id);
    if (bookmark == chart->bookmarks.end())
        return compose_action_outcome_t::blocked("Bookmark is no longer available.",
                                                 notification_source_t::input);
    compose_navigation_target_t target;
    if (bookmark->anchor == marker_anchor_t::beat && bookmark->position) {
        target.position = *bookmark->position;
    } else if (bookmark->anchor == marker_anchor_t::audio_time && bookmark->audio_milliseconds) {
        target.audio_milliseconds = bookmark->audio_milliseconds;
        if (!is_playing(application)) {
            const auto position = beat_position_for_audio_milliseconds(*history.indices(chart_id), *bookmark->audio_milliseconds);
            if (!position)
                return compose_action_outcome_t::blocked(
                    "Audio-time bookmark has no musical position in this chart.",
                    notification_source_t::input);
            target.position = *position;
        }
    } else
        return compose_action_outcome_t::blocked("Bookmark has no navigable position.",
                                                 notification_source_t::input);
    auto outcome = navigate_compose_position(history, state, application, target, expected_revision);
    if (outcome.accepted && !is_playing(application))
        outcome.message = "Moved to the selected bookmark.";
    return outcome;
}

auto bookmark_navigation_t::add_beat_bookmark_here(document_history_t& history,
                                                   editor_interaction_state_t& state,
                                                   application_command_state_t& application)
    -> compose_action_outcome_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return compose_action_outcome_t::blocked("Choose a chart before editing bookmarks.",
                                                 notification_source_t::edit);
    auto position = state.cursor;
    if (is_playing(application)) {
        const auto& indices = *history.indices(state.chart);
        if (!application.project.transport.preview_playhead_absolute_row ||
            !indices.measures)
            return compose_action_outcome_t::blocked("The playhead row is not available yet.",
                                                     notification_source_t::edit);
        const auto& index = *indices.measures;
        const auto row = nearest_snap_row(*chart, index, *application.project.transport.preview_playhead_absolute_row,
                                          state.active_snap_denominator);
        const auto snapped = row ? (index).position_at(*row) : std::nullopt;
        if (!snapped)
            return compose_action_outcome_t::blocked("The playhead is not on an editable row.",
                                                     notification_source_t::edit);
        position = *snapped;
    } else if (state.virtual_bms_cursor_row) {
        return compose_action_outcome_t::blocked(
            "Bookmarks can only go inside existing measures. Add measures up to the cursor first.",
            notification_source_t::edit);
    }
    const auto exists = std::ranges::any_of(chart->bookmarks, [&](const bookmark_t& bookmark) {
        return bookmark.anchor == marker_anchor_t::beat && bookmark.position == position;
    });
    if (exists)
        return compose_action_outcome_t::blocked("A bookmark already exists on this row.",
                                                 notification_source_t::edit);
    auto applied = apply_marker_edit(
        history, marker_edit_action_t{wormedit::insert_edit_t<wormedit::bookmark_edit_value_t>{
                     .chart = state.chart,
                     .value = {.label = {},
                               .anchor = marker_anchor_t::beat,
                               .position = position,
                               .audio_milliseconds = std::nullopt}}});
    if (!applied.applied) {
        auto outcome = compose_action_outcome_t::from(
            applied.diagnostics.empty() ? "Bookmark edit made no changes."
                                        : applied.diagnostics.front().message,
            to_notification_severity(applied.diagnostics, notification_severity_t::warning),
            notification_source_t::edit, false, false);
        outcome.diagnostics = std::move(applied.diagnostics);
        return outcome;
    }
    auto outcome = compose_action_outcome_t::from(
        is_playing(application) ? "Bookmark added at the playhead."
                                : "Bookmark added at the Compose cursor.",
        notification_severity_t::success, notification_source_t::edit, true, true);
    outcome.rebind_failure =
        "Command applied, but environment.document.workspace rebinding failed.";
    outcome.is_rebind_severity_diagnostic = false;
    return outcome;
}

} // namespace wormedit::sys
