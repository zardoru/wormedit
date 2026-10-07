#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>

#include <optional>

namespace wormedit::sys {

struct note_to_bgm_outcome_t {
    // False when no selected note qualified, so the caller carries on with its ordinary path.
    bool handled{};
    document_command_result_t command;
    // Handled and nothing else is selected: the caller has nothing left to do.
    bool selection_exhausted{};
};

// Moves the selected playable notes to the BGM area as one undo step (see `move_notes_to_bgm`).
// With `only_stem_taps` (the Delete command on a BMSON chart) only taps that stand for a stem
// point qualify, because deleting one would cut its slice point out of the stem. Moved notes leave
// the selection, and become the selected BGM events when `keep_selected`.
[[nodiscard]] auto move_selected_notes_to_bgm(document_history_t& history, editor_interaction_state_t& state,
                                              std::optional<object_id_t> lane, bool only_stem_taps,
                                              bool keep_selected) -> note_to_bgm_outcome_t;

// Moves the selected BGM events (plus `also`, the one being dragged) into a playfield lane as one
// undo step (see `move_bgm_events_to_lane`). They become the selected notes afterwards.
[[nodiscard]] auto move_selected_bgm_to_lane(document_history_t& history, editor_interaction_state_t& state,
                                             lane_id_t lane, std::optional<object_id_t> also = std::nullopt)
    -> note_to_bgm_outcome_t;

} // namespace wormedit::sys
