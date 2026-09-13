#pragma once

#include <wormedit/core/command.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

inline constexpr double minimum_pixels_per_standard_measure = 24.0;
inline constexpr double maximum_pixels_per_standard_measure = 4096.0;
inline constexpr double default_pixels_per_standard_measure = 192.0;

enum class interaction_direction_t {
    backward,
    forward,
};

struct move_edit_cursor_action_t {
    interaction_direction_t direction{interaction_direction_t::forward};
};

struct cycle_edit_snap_action_t {
    interaction_direction_t direction{interaction_direction_t::forward};
};

// The position is the view's unsnapped hit-test result. The controller maps it
// onto the current exact snap before placing or removing a tap note.
struct place_tap_action_t {
    local_position_t position;
};

// Hold authoring is a two-step gesture. Beginning stores an exact snapped
// preview anchor without changing the document; committing publishes one
// validated command. A rejected commit leaves the preview available to retry.
struct begin_hold_action_t {
    local_position_t position;
};

struct update_hold_preview_action_t {
    local_position_t position;
};

struct commit_hold_action_t {
    local_position_t position;
};

struct cancel_hold_action_t {};

enum class hold_gesture_source_t {
    keyboard_lane,
    alt_pointer,
};

struct begin_lane_key_hold_gesture_action_t {
    lane_id_t lane;
};

struct release_lane_key_hold_gesture_action_t {};

struct begin_alt_pointer_hold_gesture_action_t {
    lane_id_t lane;
    local_position_t position;
    bool alt_or_option_down{true};
};

struct update_alt_pointer_hold_gesture_action_t {
    local_position_t position;
    bool alt_or_option_down{true};
    bool pointer_captured{true};
};

struct release_alt_pointer_hold_gesture_action_t {
    local_position_t position;
    bool alt_or_option_down{true};
    bool pointer_captured{true};
};

struct begin_note_duration_gesture_action_t {
    object_id_t reference_note;
};

struct update_note_duration_gesture_action_t {
    local_position_t reference_end;
};

struct release_note_duration_gesture_action_t {
    local_position_t reference_end;
};

enum class editor_gesture_cancel_reason_t {
    escape,
    focus_lost,
    modifier_released,
    pointer_capture_lost,
};

struct cancel_editor_gesture_action_t {
    editor_gesture_cancel_reason_t reason{editor_gesture_cancel_reason_t::escape};
};

struct select_editor_notes_action_t {
    std::vector<object_id_t> notes;
};

struct delete_editor_notes_action_t {};

// The reference end is an unsnapped pointer/cursor position. Its snapped
// duration delta is applied atomically to the selected notes when the
// reference belongs to the selection, otherwise only the reference changes.
struct resize_note_durations_action_t {
    object_id_t reference_note;
    local_position_t reference_end;
};

struct select_editor_lane_action_t {
    lane_id_t lane;
};

// A zoom factor above one increases the visible distance between chart rows.
// The cursor remains at the same screen-space y coordinate while zooming.
struct adjust_editor_zoom_action_t {
    double factor{1.0};
    // Omit for keyboard zoom anchored at the edit cursor. Pointer zoom supplies
    // the y coordinate inside the viewport.
    std::optional<double> anchor_screen_y_pixels;
};

struct scroll_editor_view_action_t {
    double vertical_pixels{};
};

using editor_ui_action_t = std::variant<
    move_edit_cursor_action_t, cycle_edit_snap_action_t, place_tap_action_t, begin_hold_action_t,
    update_hold_preview_action_t, commit_hold_action_t, cancel_hold_action_t,
    begin_lane_key_hold_gesture_action_t, release_lane_key_hold_gesture_action_t,
    begin_alt_pointer_hold_gesture_action_t, update_alt_pointer_hold_gesture_action_t,
    release_alt_pointer_hold_gesture_action_t, begin_note_duration_gesture_action_t,
    update_note_duration_gesture_action_t, release_note_duration_gesture_action_t,
    cancel_editor_gesture_action_t, select_editor_notes_action_t, delete_editor_notes_action_t,
    resize_note_durations_action_t, select_editor_lane_action_t, adjust_editor_zoom_action_t,
    scroll_editor_view_action_t>;

struct pending_hold_t {
    local_position_t position;
    lane_id_t lane;
    std::uint64_t document_revision{};
    std::optional<local_position_t> preview_end;
    std::optional<hold_gesture_source_t> gesture_source;

    auto operator==(const pending_hold_t&) const -> bool = default;
};

struct note_duration_preview_t {
    object_id_t note;
    note_kind_t kind{note_kind_t::tap};
    std::optional<local_position_t> end;

    auto operator==(const note_duration_preview_t&) const -> bool = default;
};

struct pending_note_duration_gesture_t {
    object_id_t reference_note;
    std::uint64_t document_revision{};
    std::vector<object_id_t> notes;
    local_position_t preview_end;
    std::vector<note_duration_preview_t> preview;

    auto operator==(const pending_note_duration_gesture_t&) const -> bool = default;
};

struct editor_interaction_state_t {
    chart_id_t chart;
    lane_id_t active_lane;
    local_position_t cursor;
    std::vector<std::uint64_t> supported_snap_denominators;
    std::uint64_t active_snap_denominator{};
    bool active_snap_was_fallback{};
    double pixels_per_standard_measure{default_pixels_per_standard_measure};
    double viewport_scroll_pixels{};
    double viewport_height_pixels{1.0};
    std::optional<pending_hold_t> pending_hold;
    std::optional<pending_note_duration_gesture_t> pending_note_duration_gesture;
    std::vector<object_id_t> selected_notes;
};

struct editor_interaction_view_model_t {
    chart_id_t chart;
    lane_id_t active_lane;
    std::string active_lane_name;
    local_position_t cursor;
    std::uint64_t active_snap_denominator{};
    std::vector<std::uint64_t> supported_snap_denominators;
    std::uint64_t document_revision{};
    double pixels_per_standard_measure{default_pixels_per_standard_measure};
    double viewport_scroll_pixels{};
    double viewport_height_pixels{1.0};
    std::optional<pending_hold_t> pending_hold;
    std::optional<pending_note_duration_gesture_t> pending_note_duration_gesture;
    std::vector<object_id_t> selected_notes;
    bool can_move_backward{};
    bool can_move_forward{};
};

struct editor_interaction_setup_t {
    std::optional<editor_interaction_state_t> state;
    std::optional<editor_interaction_view_model_t> view;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state.has_value() && view.has_value();
    }
};

struct editor_interaction_result_t {
    bool accepted{};
    bool document_changed{};
    std::optional<editor_interaction_view_model_t> view;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return accepted;
    }
};

// Creates transient, frontend-independent editing state from a validated
// history snapshot. Snap denominators are ordered by editor preference.
[[nodiscard]] auto make_editor_interaction(const document_history_t& history, chart_id_t chart,
                                           lane_id_t active_lane, local_position_t cursor,
                                           std::span<const std::uint64_t> ordered_snap_denominators,
                                           std::uint64_t requested_snap_denominator,
                                           double viewport_height_pixels = 1.0)
    -> editor_interaction_setup_t;

// Maps an exact chart position into backend-neutral vertical pixels for a view.
[[nodiscard]] auto editor_position_y_pixels(const chart_document_t& chart,
                                            const editor_interaction_view_model_t& view,
                                            local_position_t position) -> std::optional<double>;

// Dispatches one typed frontend action. Document edits are published through
// commit_document_command so validation, undo, and redo share one boundary.
[[nodiscard]] auto dispatch_editor_action(document_history_t& history,
                                          editor_interaction_state_t& state,
                                          const editor_ui_action_t& action)
    -> editor_interaction_result_t;

} // namespace wormedit
