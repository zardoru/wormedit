#pragma once

#include <wormedit/core/notifications.h>

#include <wormedit/core/document.h>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/lane_transform.h>

#include <string>
#include <vector>

namespace wormedit::sys {

enum class selection_transform_t { mirror, rotate_left, rotate_right, shift_earlier, shift_later };

// Lane permutation that moves every playable lane of `layout` one place along its stage group in
// display order, wrapping around. `left` moves toward the first lane.
[[nodiscard]] auto rotate_lane_permutation(const lane_layout_t& layout, bool left)
    -> std::vector<lane_permutation_entry_t>;

// Applies one Transform menu entry to the selected notes and returns the status line. Shifts move
// by one active-snap step; failures keep the core message and leave the document untouched.
[[nodiscard]] auto apply_selection_transform(document_history_t& history,
                                             const editor_interaction_state_t& state,
                                             selection_transform_t transform,
                                             notification_severity_t* severity = nullptr) -> std::string;

} // namespace wormedit::sys
