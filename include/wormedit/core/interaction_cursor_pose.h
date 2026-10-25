#pragma once

// Edit cursor and viewport placement over editor_interaction_state_t.

#include <wormedit/core/interaction_state.h>
#include <wormedit/core/editor_axis.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// Update the non-authoring playback cursor and viewport using retained geometry.
[[nodiscard]] auto follow_editor_playhead(document_history_t& history,
    editor_interaction_state_t& state, editor_measure_geometry_t geometry,
    local_position_t position, double target_scroll,
    double following_anchor_fraction = editor_cursor_edge_margin_fraction,
    double cursor_subrow = 0.0) -> editor_interaction_result_t;

// Restore a valid authored cursor after undo or another document edit removes
// the cursor's measure, retaining the viewport when its extent still exists.
[[nodiscard]] auto recover_editor_interaction(const document_history_t& history,
                                              const editor_interaction_state_t& previous)
    -> editor_interaction_setup_t;

// The edit cursor's position and where it is drawn. Production code must not assign the
// underlying state fields; it captures, restores or places the cursor through these helpers
// (or dispatches an editor action), so a cursor move cannot leave one field stale. The
// `cursor_write_guard` test enforces this.
struct cursor_pose_t {
    local_position_t cursor;
    std::optional<std::uint64_t> virtual_bms_cursor_row;
    double cursor_subrow{};
    double viewport_scroll_pixels{};
    std::optional<double> fixed_cursor_screen_y_pixels;
    editor_field_scroll_direction_t scroll_direction{editor_field_scroll_direction_t::up};

    friend bool operator==(const cursor_pose_t&, const cursor_pose_t&) = default;
};

[[nodiscard]] inline auto capture_cursor_pose(const editor_interaction_state_t& state)
    -> cursor_pose_t {
    return {state.cursor, state.virtual_bms_cursor_row, state.cursor_subrow,
            state.viewport_scroll_pixels, state.fixed_cursor_screen_y_pixels, state.scroll_direction};
}

inline void restore_cursor_pose(editor_interaction_state_t& state, const cursor_pose_t& pose) {
    state.cursor = pose.cursor;
    state.virtual_bms_cursor_row = pose.virtual_bms_cursor_row;
    state.cursor_subrow = pose.cursor_subrow;
    state.viewport_scroll_pixels = pose.viewport_scroll_pixels;
    state.fixed_cursor_screen_y_pixels = pose.fixed_cursor_screen_y_pixels;
    state.scroll_direction = pose.scroll_direction;
}

// Change the logical authored position, clearing a stale fractional offset and any
// virtual row. The viewport and screen anchor remain unchanged.
inline void place_cursor_at(editor_interaction_state_t& state, const local_position_t& position) {
    if (state.cursor != position || state.virtual_bms_cursor_row) state.cursor_subrow = 0.0;
    state.cursor = position;
    state.virtual_bms_cursor_row.reset();
}

// Pick an exact row, clearing the fractional offset even when the logical row is unchanged.
inline void place_cursor_on_picked_row(editor_interaction_state_t& state,
                                       const local_position_t& position) {
    place_cursor_at(state, position);
    state.cursor_subrow = 0.0;
}

// Clamp the viewport scroll without moving the cursor.
inline void set_viewport_scroll_pixels(editor_interaction_state_t& state, double pixels) {
    state.viewport_scroll_pixels = pixels;
}

inline void set_cursor_subrow(editor_interaction_state_t& state, double subrow) {
    state.cursor_subrow = subrow;
}

inline void set_cursor_scale(editor_interaction_state_t& state, double pixels_per_measure) {
    state.pixels_per_standard_measure = pixels_per_measure;
}

inline void place_cursor_on_canvas_row(editor_interaction_state_t& state,
    std::optional<local_position_t> position, std::uint64_t row, double subrow = 0.0) {
    if (position) place_cursor_at(state, *position);
    state.virtual_bms_cursor_row = position ? std::nullopt : std::optional{row};
    state.cursor_subrow = subrow;
}

inline void set_cursor_screen_anchor(editor_interaction_state_t& state,
    std::optional<double> anchor, editor_field_scroll_direction_t direction) {
    state.fixed_cursor_screen_y_pixels = anchor;
    state.scroll_direction = direction;
}

inline auto editor_cursor_screen_anchor(double height, editor_field_scroll_direction_t direction) -> double {
    return height * (direction == editor_field_scroll_direction_t::down
        ? 1.0 - editor_cursor_edge_margin_fraction : editor_cursor_edge_margin_fraction);
}

inline void configure_cursor_viewport(editor_interaction_state_t& state, double height,
    editor_field_scroll_direction_t direction) {
    if (state.fixed_cursor_screen_y_pixels && state.viewport_height_pixels == height &&
        state.scroll_direction == direction) return;
    const auto old_source = state.fixed_cursor_screen_y_pixels
        ? (state.scroll_direction == editor_field_scroll_direction_t::down
            ? state.viewport_height_pixels - *state.fixed_cursor_screen_y_pixels
            : *state.fixed_cursor_screen_y_pixels) : 0.0;
    const auto fraction = state.fixed_cursor_screen_y_pixels && state.viewport_height_pixels > 0.0
        ? old_source / state.viewport_height_pixels : editor_cursor_edge_margin_fraction;
    const auto source = height * fraction;
    if (state.fixed_cursor_screen_y_pixels)
        set_viewport_scroll_pixels(state, state.viewport_scroll_pixels + old_source - source);
    state.viewport_height_pixels = height;
    set_cursor_screen_anchor(state,
        direction == editor_field_scroll_direction_t::down ? height - source : source, direction);
}

// Cursor drawing has one policy: an explicit screen anchor wins over the logical
// chart row. Logical moves, including temporary edit positions, cannot move the line.
inline auto editor_cursor_screen_y_pixels(const editor_interaction_view_model_t& view,
    long double row, std::uint64_t divisions, editor_field_scroll_direction_t direction,
    std::optional<double> anchor = std::nullopt) -> double {
    if (anchor) return *anchor;
    if (view.fixed_cursor_screen_y_pixels) return *view.fixed_cursor_screen_y_pixels;
    const auto source = editor_axis_projection_t::from(view, divisions).y_for_row(row + view.cursor_subrow);
    return direction == editor_field_scroll_direction_t::down ? view.viewport_height_pixels - source : source;
}

// Resolve the snapped edit row and fractional offset beneath the stationary line
// after any viewport change. BMS virtual rows remain unmaterialized.
[[nodiscard]] auto synchronize_editor_cursor_to_viewport(const chart_document_t& chart,
    const measure_index_t& index, editor_interaction_state_t& state) -> bool;

} // namespace wormedit
