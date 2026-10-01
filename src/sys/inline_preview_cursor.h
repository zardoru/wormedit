#pragma once

#include <wormedit/core/interaction.h>

#include <variant>

#include <cmath>
#include <algorithm>
#include <optional>
#include <utility>

namespace wormedit::sys {

// The edit cursor follows playback; keep its starting position for Escape.
struct inline_preview_cursor_t {
    struct anchor_t {
        chart_id_t chart;
        // Includes the drawn offset and scroll, so a fixed-cursor view returns to the same
        // screen position.
        cursor_pose_t pose;
    };

    std::optional<anchor_t> anchor;
    // Share of the viewport, from the chart-start edge, where the cursor sat when playback
    // began; following keeps it there instead of re-anchoring to the preference.
    std::optional<double> follow_anchor_fraction;

    void capture_follow_anchor(const document_history_t& history,
                               const editor_interaction_state_t* state) {
        follow_anchor_fraction.reset();
        if (!state || state->virtual_bms_cursor_row || state->viewport_height_pixels <= 0.0) return;
        const auto chart = std::ranges::find(history.document.charts, state->chart,
                                             &chart_document_t::id);
        if (chart == history.document.charts.end() || chart->divisions_per_standard_measure == 0) return;
        const auto view = inspect_editor_interaction(history, *state);
        const auto* model = std::get_if<editor_interaction_view_model_t>(&view);
        if (!model) return;
        const auto y = editor_position_y_pixels(*chart, *model, state->cursor);
        if (!y) return;
        const auto drawn = *y + state->cursor_subrow * state->pixels_per_standard_measure /
                                    chart->divisions_per_standard_measure;
        const auto fraction = drawn / state->viewport_height_pixels;
        if (std::isfinite(fraction) && fraction >= 0.0 && fraction <= 1.0)
            follow_anchor_fraction = fraction;
    }

    void capture(const editor_interaction_state_t* state) {
        anchor = state ? std::optional{anchor_t{state->chart, capture_cursor_pose(*state)}}
                       : std::nullopt;
    }

    [[nodiscard]] auto restore(document_history_t& history,
                               editor_interaction_state_t* state) -> bool {
        const auto previous = std::exchange(anchor, std::nullopt);
        if (!previous || !state || state->chart != previous->chart) return false;
        (void)dispatch_editor_action(history, *state, cancel_editor_gesture_action_t{});
        const auto jumped = dispatch_editor_action(
            history, *state,
            jump_edit_cursor_action_t{previous->pose.cursor, history.document.revision});
        if (!jumped) return false;
        if (std::isfinite(previous->pose.cursor_subrow) &&
            std::isfinite(previous->pose.viewport_scroll_pixels))
            restore_cursor_pose(*state, previous->pose);
        return true;
    }
};

} // namespace wormedit::sys
