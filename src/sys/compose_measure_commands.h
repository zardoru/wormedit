#pragma once

#include <wormedit/core/notifications.h>

#include <wormedit/core/interaction.h>
#include <wormedit/core/measure_editor.h>

#include <string>
#include <string_view>

namespace wormedit::sys {

enum class cursor_measure_command_t { insert_before, insert_after, remove_empty };

// Inserts a measure (a copy of the cursor measure's definition) before or after the measure that
// contains the edit cursor, or removes that measure when it is empty. One undoable command each;
// after a removal the cursor moves to the start of the neighboring measure. Returns the status
// line, with the core message on rejection.
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

} // namespace wormedit::sys
