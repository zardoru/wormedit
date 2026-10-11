#pragma once

#include "sys/compose_view.h"

#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/timing.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace wormedit::sys {

// A repeated playing seek (bookmark step or wheel notch) this soon after the last one starts from
// that seek's target, because the audio clock has not caught up to it yet.
inline constexpr auto playing_seek_repeat_window = std::chrono::milliseconds{300};

// The current time for repeat-window checks; tests inject `bookmark_clock`.
[[nodiscard]] auto playing_seek_now(const application_command_state_t& application)
    -> std::chrono::steady_clock::time_point;

struct bookmark_command_outcome_t {
    bool accepted{};
    bool document_changed{};
    std::string status;
    diagnostic_list_t diagnostics;
    bool feedback_posted{};
};

// The local position of an absolute row, or nothing past the last row.
[[nodiscard]] auto local_position_for_row(const measure_index_t& index, std::uint64_t row)
    -> std::optional<local_position_t>;

// The active-snap row nearest `absolute_row` among rows 0 .. total_rows-1; ties go to the
// earlier row. Returns nothing when the snap does not divide the measure or there are no rows.
[[nodiscard]] auto nearest_snap_row(const chart_document_t& chart, const measure_index_t& index,
                                    double absolute_row, std::uint64_t snap_denominator)
    -> std::optional<std::uint64_t>;

// Preview frame of a beat position, clamped to [0, end_frame] when an end is known.
[[nodiscard]] auto bookmark_preview_frame(const timing_index_t& timing,
                                          const measure_index_t& measures, local_position_t position,
                                          std::optional<std::uint64_t> end_frame)
    -> std::optional<std::uint64_t>;

// Jumps the edit cursor (stopped or paused) or the playhead (playing) to the previous or next
// beat bookmark. Section boundaries and audio-time bookmarks are skipped.
[[nodiscard]] auto navigate_beat_bookmark(document_history_t& history,
                                          editor_interaction_state_t& state,
                                          application_command_state_t& application,
                                          marker_navigation_direction_t direction)
    -> bookmark_command_outcome_t;

// Navigates to an exact active-chart bookmark ID. The expected revision and
// chart stamp reject stale chooser rows; stopped audio anchors resolve through
// timing, while playback seeks the original audio millisecond position.
[[nodiscard]] auto navigate_to_bookmark(document_history_t& history,
                                        editor_interaction_state_t& state,
                                        application_command_state_t& application,
                                        std::uint64_t expected_revision, chart_id_t chart,
                                        object_id_t bookmark)
    -> bookmark_command_outcome_t;

// Adds an unnamed beat bookmark at the edit cursor, or at the active-snap row nearest the
// playhead while playing. A row that already holds a bookmark adds nothing. The caller rejects
// the command while a Compose gesture is pending.
[[nodiscard]] auto add_beat_bookmark_here(document_history_t& history,
                                          editor_interaction_state_t& state,
                                          application_command_state_t& application)
    -> bookmark_command_outcome_t;

} // namespace wormedit::sys
