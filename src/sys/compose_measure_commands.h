#pragma once

#include <wormedit/core/notifications.h>
#include "sys/compose_view.h"

#include <wormedit/core/interaction.h>
#include <wormedit/core/measure_editor.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace wormedit::sys {

inline auto compose_measure_edit_rejection(const editor_interaction_state_t& state,
                                           bool paste_active)
    -> std::optional<std::string> {
    if (editor_interaction_has_pending_gesture(state) || paste_active)
        return "Finish or cancel the active Compose gesture before editing measures.";
    return std::nullopt;
}

[[nodiscard]] auto apply_cursor_measure_split(document_history_t& history,
    editor_interaction_state_t& state, notification_severity_t* severity = nullptr) -> std::string;

enum class cursor_measure_command_t { insert_before, insert_after, remove_empty, remove_with_contents };

// Inserts a measure (a copy of the cursor measure's definition) before or after the measure that
// contains the edit cursor, or removes that measure when it is empty (`remove_empty`). A nonempty
// measure is removed with `remove_with_contents` after the UI confirmation; that moves the cursor
// to the start of the following measure, or of the previous one when the last measure goes. One
// undoable command each; after a removal the cursor moves to the start of the neighboring measure.
// Returns the status line, with the core message on rejection.
[[nodiscard]] auto apply_cursor_measure_command(document_history_t& history,
                                                editor_interaction_state_t& state,
                                                cursor_measure_command_t command,
                                                notification_severity_t* severity = nullptr) -> std::string;

// Sets the time signature of the measure that contains the edit cursor to `numerator/denominator`
// (the measure's length changes with it, as in the signature's own whole-note fraction). Both parts
// are digits only and fit in 32 bits, non-zero. One undoable command; returns the status line,
// with the core message on rejection.
[[nodiscard]] auto apply_cursor_measure_meter(document_history_t& history,
                                              editor_interaction_state_t& state,
                                              std::string_view numerator,
                                              std::string_view denominator,
                                              notification_severity_t* severity = nullptr) -> std::string;

// Sets one time signature on every listed measure (chart positions, virtual tail measures included)
// of the selected chart in a single undo step; objects keep their rows from the start of the song.
// Same text rules and messages as apply_cursor_measure_meter; an empty list, an unknown measure
// or a stale revision changes nothing.
[[nodiscard]] auto apply_measures_meter(document_history_t& history,
                                        editor_interaction_state_t& state,
                                        std::span<const std::uint64_t> measure_ids,
                                        std::string_view numerator,
                                        std::string_view denominator,
                                        notification_severity_t* severity = nullptr) -> std::string;

// Moves the bar line that ends the measure at `measure_index` (a chart position, virtual tail
// included): that measure takes the first meter and the next measure the second, and no other bar
// line or object row changes. One undo step.
[[nodiscard]] auto apply_bar_line_move(document_history_t& history,
                                       editor_interaction_state_t& state,
                                       std::uint64_t measure_index,
                                       meter_t first, meter_t second,
                                       notification_severity_t* severity = nullptr) -> std::string;

} // namespace wormedit::sys
