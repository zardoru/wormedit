#pragma once

#include <wormedit/core/command.h>

#include <optional>
#include <span>

namespace wormedit {

// Moves playable notes to the BGM area in one undo step. A BMSON tap that stands for a stem
// point returns to its stem's BGM lane and keeps its slice point (the retained event just stops
// being playable). Any other note becomes a BGM event at the same row, in `preferred_lane` or,
// when that lane already has an event at the row, the next free lane to the right, then to the
// left, and a new lane when all are taken. Hold tails are dropped. Unknown notes, a stale
// revision, or a stem that cannot be reassigned reject the whole edit.
[[nodiscard]] auto move_notes_to_bgm(document_history_t& history, std::uint64_t expected_revision,
                                     chart_id_t chart, std::span<const object_id_t> notes,
                                     std::optional<object_id_t> preferred_lane)
    -> document_command_result_t;

// The reverse move, in one undo step: BGM events become playable taps in `lane`, keeping their row.
// A BMSON marker that stands for a stem point becomes the stem's playable tap, keeping its slice
// point; any other event becomes a plain tap with its sound, layers and branch. A lane that does
// not exist at the row, or already holds a note there, rejects the whole edit.
[[nodiscard]] auto move_bgm_events_to_lane(document_history_t& history, std::uint64_t expected_revision,
                                           chart_id_t chart, std::span<const object_id_t> events,
                                           lane_id_t lane) -> document_command_result_t;

} // namespace wormedit
