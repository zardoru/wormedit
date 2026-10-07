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

} // namespace wormedit
