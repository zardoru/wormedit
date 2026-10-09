#pragma once

#include <wormedit/core/note_to_bgm.h>

#include <optional>

namespace wormedit::sys {

using note_to_bgm_outcome_t = selected_note_to_bgm_result_t;

// Moves the selected BGM events (plus `also`, the one being dragged) into a playfield lane as one
// undo step (see `move_bgm_events_to_lane`). They become the selected notes afterwards.
[[nodiscard]] auto move_selected_bgm_to_lane(document_history_t& history, editor_interaction_state_t& state,
                                             lane_id_t lane, std::optional<object_id_t> also = std::nullopt,
                                             bool successive = false)
    -> note_to_bgm_outcome_t;

} // namespace wormedit::sys
