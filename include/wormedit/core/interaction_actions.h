#pragma once

// Typed frontend actions the editor controller dispatches (dispatch_editor_action).

#include <wormedit/core/interaction_types.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/bmson_sound_channel_editor.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace wormedit {

enum class interaction_direction_t {
    backward,
    forward,
};

struct move_edit_cursor_action_t {
    interaction_direction_t direction{interaction_direction_t::forward};
};

// Pages by whole measure-index entries, retaining the cursor's fractional
// source-measure row. Differing target capacities use nearest-row rounding,
// with a halfway value resolving toward the target measure's beginning.
struct page_edit_cursor_action_t {
    interaction_direction_t direction{interaction_direction_t::forward};
    std::uint64_t measure_count{1};
    // Optional absolute row-distance floor, clamped to canvas ends.
    std::uint64_t minimum_rows{};
};

// Moves directly to an exact chart position without applying the active snap.
// The source revision lets callers safely consume positions returned by a
// revision-bound query such as marker navigation.
struct jump_edit_cursor_action_t {
    local_position_t position;
    std::uint64_t expected_document_revision{};
};

struct select_bms_canvas_measure_action_t {
    std::size_t number{};
};

struct cycle_edit_snap_action_t {
    interaction_direction_t direction{interaction_direction_t::forward};
};

// Sets the editing snap to one specific grid. A denominator the chart cannot snap to leaves the
// snap unchanged and is not an error (the Quantize grid chips share this value).
struct set_edit_snap_action_t {
    std::uint64_t denominator{};
};

// The position is the view's unsnapped hit-test result. The controller maps it
// onto the current exact snap before placing or removing a tap note.
struct place_tap_action_t {
    local_position_t position;
    // The Sounds-dock sample captured with the click. For an armed Hold/Roll it travels with
    // the pending gesture and is written when the end click commits.
    std::optional<resource_id_t> sound;
    std::optional<bmson_source_geometry_t> source_geometry;
};

// Absolute row in the sparse BMS canvas. A successful first edit extends the
// required default geometry and places one tap in the same undoable command.
struct place_virtual_bms_tap_action_t {
    std::uint64_t canvas_row{};
    lane_id_t lane;
    std::optional<resource_id_t> sample;
    std::optional<bmson_source_geometry_t> source_geometry;
};

struct place_virtual_bms_hold_action_t {
    std::uint64_t anchor_canvas_row{};
    std::uint64_t end_canvas_row{};
    lane_id_t lane;
    std::optional<resource_id_t> sound;
    std::optional<bmson_source_geometry_t> source_geometry;
};

// Hold authoring is a two-step gesture. Beginning stores an exact snapped
// preview anchor without changing the document; committing publishes one
// validated command. A rejected commit leaves the preview available to retry.
struct begin_hold_action_t {
    local_position_t position;
};

struct update_hold_preview_action_t {
    static constexpr auto missing_gesture_message = "hold preview requires a pending hold start";
    local_position_t position;
};

struct commit_hold_action_t {
    static constexpr auto missing_gesture_message = "hold completion requires a pending hold start";
    local_position_t position;
    std::optional<bmson_source_geometry_t> source_geometry;
};

enum class hold_gesture_source_t {
    keyboard_lane,
    alt_pointer,
};

struct begin_lane_key_hold_gesture_action_t {
    lane_id_t lane;
    std::optional<resource_id_t> sound;
    bool displace_to_bgm{};
    std::optional<bmson_source_geometry_t> source_geometry;
};

struct release_lane_key_hold_gesture_action_t {
    lane_id_t lane{};
};

struct begin_alt_pointer_hold_gesture_action_t {
    lane_id_t lane;
    local_position_t position;
    bool alt_or_option_down{true};
    // Captured at press, like a lane key: later changes to the armed sound do not retarget it.
    std::optional<resource_id_t> sound;
    std::optional<bmson_source_geometry_t> source_geometry;
};

struct update_alt_pointer_hold_gesture_action_t {
    static constexpr auto missing_gesture_message = "hold gesture update does not match the active input";
    local_position_t position;
    bool alt_or_option_down{true};
    bool pointer_captured{true};
};

struct release_alt_pointer_hold_gesture_action_t {
    static constexpr auto missing_gesture_message = "hold gesture release does not match the active input";
    local_position_t position;
    bool alt_or_option_down{true};
    bool pointer_captured{true};
    std::optional<bmson_source_geometry_t> source_geometry;
};

struct begin_note_duration_gesture_action_t {
    object_id_t reference_note;
};

struct update_note_duration_gesture_action_t {
    local_position_t reference_end;
    proposal_operation_t operation{proposal_operation_t::revise};
};

// Translate the selected notes and BGM events by the snapped delta between the
// reference head and this position. A nonzero reference lane supplies a
// display-column delta that is applied atomically to every selected note;
// zero retains its original lane for backwards-compatible vertical moves.
struct begin_note_move_gesture_action_t {
    object_id_t reference_note;
    bool displace_to_bgm{};
};

struct update_note_move_gesture_action_t {
    local_position_t reference_position;
    lane_id_t reference_lane{};
    bool to_bgm{};
    // For a BGM reference, the ordinary event's preferred source lane; linked stems keep their lane.
    std::optional<object_id_t> preferred_bgm_lane;
    // Absolute destination in the unmaterialized BMS tail (BGM reference only).
    std::optional<std::uint64_t> virtual_bgm_row;
    proposal_operation_t operation{proposal_operation_t::revise};
};

struct begin_bar_line_move_gesture_action_t {
    measure_id_t reference_bar_line;
};

struct update_bar_line_move_gesture_action_t {
    local_position_t reference_position;
    proposal_operation_t operation{proposal_operation_t::revise};
};

// The mode is captured on the first Space press and applied atomically when
// Space is pressed again.
struct space_range_selection_action_t {
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    editor_selection_scope_t scope{};
    bool minimum_snap_width{};
};

struct begin_editor_area_selection_action_t {
    double x_pixels{};
    double y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    bool pointer_captured{true};
    editor_selection_scope_t scope{};
    bool canvas_pointer_capture{};
    std::shared_ptr<const editor_area_geometry_snapshot_t> object_geometry;
};

struct update_editor_area_selection_action_t {
    double x_pixels{};
    double y_pixels{};
    bool pointer_captured{true};
    proposal_operation_t operation{proposal_operation_t::revise};
};

struct cancel_editor_area_selection_action_t {};

enum class note_paint_intent_t {
    toggle,
    draw,
    erase,
};

struct begin_note_paint_gesture_action_t {
    lane_id_t lane;
    local_position_t position;
    bool pointer_captured{true};
    // Sample-role resource written into every new note of the stroke (with
    // its sound_layer_defaults). Empty paints silent notes.
    std::optional<resource_id_t> sound;
    note_paint_intent_t intent{note_paint_intent_t::toggle};
    std::optional<bmson_source_geometry_t> source_geometry;
};

struct update_note_paint_gesture_action_t {
    lane_id_t lane;
    local_position_t position;
    bool pointer_captured{true};
};

struct end_note_paint_gesture_action_t {
    lane_id_t lane;
    local_position_t position;
    bool pointer_captured{true};
    std::optional<bmson_source_geometry_t> source_geometry;
};

// Semantic-layer painting visits existing notes under a captured pointer and
// adds or removes one explicitly armed layer across the whole visited set as a single
// undoable edit. It never creates, deletes, or moves gameplay notes.
enum class semantic_layer_paint_mode_t { add, remove };

struct begin_semantic_layer_paint_gesture_action_t {
    layer_id_t layer;
    object_id_t note;
    bool pointer_captured{true};
    bool replace_memberships{};
    bool clear_memberships{};
    std::optional<object_id_t> bgm_event;
    semantic_layer_paint_mode_t mode{semantic_layer_paint_mode_t::add};
};

// A zero note commits an existing stroke only in the commit phase.
struct update_semantic_layer_paint_gesture_action_t {
    object_id_t note;
    bool pointer_captured{true};
    std::optional<object_id_t> bgm_event;
    proposal_operation_t operation{proposal_operation_t::revise};
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

// Replaces the typed editor selection. Note objects are mirrored into
// selected_notes so note-only editing commands keep their existing contract.
struct select_editor_objects_action_t {
    std::vector<editor_selection_object_t> objects;
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    bool request_audition{};
};

struct delete_editor_selection_action_t {};

struct select_editor_lane_action_t {
    lane_id_t lane;
};

// A bare lane key assigns every selected note/BGM event and keeps the selection. The
// successive (Shift + lane) form assigns only the first object in deterministic
// chart order, removes it from the selection, and returns an audition request.
struct assign_selected_objects_to_lane_action_t {
    lane_id_t lane;
    bool successive{};
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
    // When present, keep the edit cursor at the snap nearest this stationary
    // screen line while the entire field moves beneath it.
    std::optional<double> fixed_cursor_screen_y_pixels;
    editor_field_scroll_direction_t direction{editor_field_scroll_direction_t::up};
};

using editor_interaction_action_t = std::variant<
    move_edit_cursor_action_t, page_edit_cursor_action_t, jump_edit_cursor_action_t,
    select_bms_canvas_measure_action_t, cycle_edit_snap_action_t, place_tap_action_t,
    place_virtual_bms_tap_action_t, place_virtual_bms_hold_action_t, begin_hold_action_t,
    update_hold_preview_action_t, commit_hold_action_t, begin_lane_key_hold_gesture_action_t,
    release_lane_key_hold_gesture_action_t, begin_alt_pointer_hold_gesture_action_t,
    update_alt_pointer_hold_gesture_action_t, release_alt_pointer_hold_gesture_action_t,
    begin_note_duration_gesture_action_t, update_note_duration_gesture_action_t,
    begin_note_move_gesture_action_t, update_note_move_gesture_action_t,
    begin_bar_line_move_gesture_action_t, update_bar_line_move_gesture_action_t,
    begin_note_paint_gesture_action_t, update_note_paint_gesture_action_t,
    end_note_paint_gesture_action_t, begin_semantic_layer_paint_gesture_action_t,
    update_semantic_layer_paint_gesture_action_t, begin_editor_area_selection_action_t,
    update_editor_area_selection_action_t, cancel_editor_area_selection_action_t,
    space_range_selection_action_t, cancel_editor_gesture_action_t, select_editor_notes_action_t,
    select_editor_objects_action_t, delete_editor_selection_action_t, select_editor_lane_action_t,
    assign_selected_objects_to_lane_action_t, adjust_editor_zoom_action_t,
    scroll_editor_view_action_t, set_edit_snap_action_t>;

} // namespace wormedit
