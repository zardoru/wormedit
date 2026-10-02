#pragma once

#include <wormedit/core/interaction.h>
#include <wormedit/core/measure_editor.h>

#include <string>

namespace wormedit::sys {

enum class cursor_measure_command_t { insert_before, insert_after, remove_empty };

// Inserts a measure (a copy of the cursor measure's definition) before or after the measure that
// contains the edit cursor, or removes that measure when it is empty. One undoable command each;
// after a removal the cursor moves to the start of the neighboring measure. Returns the status
// line, with the core message on rejection.
[[nodiscard]] auto apply_cursor_measure_command(document_history_t& history,
                                                editor_interaction_state_t& state,
                                                cursor_measure_command_t command) -> std::string;

} // namespace wormedit::sys
