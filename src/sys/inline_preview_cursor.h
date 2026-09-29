#pragma once

#include <wormedit/core/interaction.h>

#include <optional>
#include <utility>

namespace wormedit::sys {

// The edit cursor follows playback; keep its starting position for Escape.
struct inline_preview_cursor_t {
    struct anchor_t {
        chart_id_t chart;
        local_position_t position;
    };

    std::optional<anchor_t> anchor;

    void capture(const editor_interaction_state_t* state) {
        anchor = state ? std::optional{anchor_t{state->chart, state->cursor}} : std::nullopt;
    }

    [[nodiscard]] auto restore(document_history_t& history,
                               editor_interaction_state_t* state) -> bool {
        const auto previous = std::exchange(anchor, std::nullopt);
        if (!previous || !state || state->chart != previous->chart) return false;
        (void)dispatch_editor_action(history, *state, cancel_editor_gesture_action_t{});
        return static_cast<bool>(dispatch_editor_action(
            history, *state,
            jump_edit_cursor_action_t{previous->position, history.document.revision}));
    }
};

} // namespace wormedit::sys
