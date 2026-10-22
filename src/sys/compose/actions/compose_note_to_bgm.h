#pragma once

#include <wormedit/core/note_to_bgm.h>
#include "sys/compose_view.h"
#include <algorithm>

#include <optional>

namespace wormedit::sys {

inline auto compose_move_to_bgm_available(const chart_document_t& chart,
    const editor_interaction_state_t& state, const application_command_state_t& application) -> bool {
    return (chart.source_family == format_family_t::bms || chart.source_family == format_family_t::bmson) &&
        !state.is_pending() && !application.project.clipboard.paste_session &&
        ui::compose_notes_editable(compose_mode_of(application)) &&
        (!state.selected_notes.empty() || std::ranges::any_of(state.selected_objects,
            [](const auto& object) { return object.kind == editor_selection_object_kind_t::note; }));
}

// Moves the selected BGM events (plus `also`, the one being dragged) into a playfield lane as one
// undo step (see `move_bgm_events_to_lane`). They become the selected notes afterwards.
[[nodiscard]] auto move_selected_bgm_to_lane(document_history_t& history, editor_interaction_state_t& state,
                                             lane_id_t lane, std::optional<object_id_t> also = std::nullopt,
                                             bool successive = false)
    -> selected_note_to_bgm_result_t;

} // namespace wormedit::sys
