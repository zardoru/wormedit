#pragma once

#include <wormedit/core/command.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
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

struct note_paint_target_t {
    lane_id_t lane;
    local_position_t position;

    auto operator==(const note_paint_target_t&) const -> bool = default;
};

enum class editor_area_selection_mode_t {
    replace,
    add,
    toggle,
};

// Track scope is deliberately explicit: the traditional lane-field gesture
// remains note-only unless the frontend opts into chart-event tracks.
struct editor_selection_scope_t {
    bool notes{true};
    bool bgm{};
    bool timing{};
    bool bga{};

    auto operator==(const editor_selection_scope_t&) const -> bool = default;
};

enum class editor_selection_object_kind_t {
    note,
    bgm_event,
    bpm_event,
    stop_event,
    delay_event,
    warp_event,
    scroll_event,
    speed_event,
    kiai_event,
    timing_anchor,
    bga_event,
    bookmark,
    section,
};

// IDs are only unique within a chart object family; retaining the family in
// selection state prevents collisions between (for example) note 17 and BGA 17.
struct editor_selection_object_t {
    editor_selection_object_kind_t kind{editor_selection_object_kind_t::note};
    object_id_t id;

    auto operator==(const editor_selection_object_t&) const -> bool = default;
    auto operator<=>(const editor_selection_object_t&) const = default;
};

// The mode is captured on the first Space press and applied atomically when
// Space is pressed again.
struct space_range_selection_action_t {
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    editor_selection_scope_t scope{};
};

struct begin_editor_area_selection_action_t {
    double x_pixels{};
    double y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    bool pointer_captured{true};
    editor_selection_scope_t scope{};
};

struct update_editor_area_selection_action_t {
    double x_pixels{};
    double y_pixels{};
    bool pointer_captured{true};
};

struct commit_editor_area_selection_action_t {
    double x_pixels{};
    double y_pixels{};
    bool pointer_captured{true};
};

struct cancel_editor_area_selection_action_t {};

struct begin_note_paint_gesture_action_t {
    lane_id_t lane;
    local_position_t position;
    bool pointer_captured{true};
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
};

struct paint_cell_key_t {
    lane_id_t lane;
    std::uint64_t absolute_row{};

    auto operator==(const paint_cell_key_t&) const -> bool = default;
};

struct paint_cell_hash_t {
    [[nodiscard]] auto operator()(paint_cell_key_t cell) const noexcept -> std::size_t;
};

inline constexpr std::size_t maximum_note_paint_targets = 50'000;

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

// A bare lane key assigns every selected note and keeps the selection. The
// successive (Shift + lane) form assigns only the first note in deterministic
// chart order, removes it from the selection, and returns an audition request.
struct assign_selected_notes_to_lane_action_t {
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
};

using editor_ui_action_t =
    std::variant<move_edit_cursor_action_t, cycle_edit_snap_action_t, place_tap_action_t,
                 begin_hold_action_t, update_hold_preview_action_t, commit_hold_action_t,
                 cancel_hold_action_t, begin_lane_key_hold_gesture_action_t,
                 release_lane_key_hold_gesture_action_t, begin_alt_pointer_hold_gesture_action_t,
                 update_alt_pointer_hold_gesture_action_t,
                 release_alt_pointer_hold_gesture_action_t, begin_note_duration_gesture_action_t,
                 update_note_duration_gesture_action_t, release_note_duration_gesture_action_t,
                 begin_note_paint_gesture_action_t, update_note_paint_gesture_action_t,
                 end_note_paint_gesture_action_t, begin_editor_area_selection_action_t,
                 update_editor_area_selection_action_t, commit_editor_area_selection_action_t,
                 cancel_editor_area_selection_action_t, space_range_selection_action_t,
                 cancel_editor_gesture_action_t, select_editor_notes_action_t,
                 delete_editor_notes_action_t, resize_note_durations_action_t,
                 select_editor_lane_action_t, assign_selected_notes_to_lane_action_t,
                 adjust_editor_zoom_action_t, scroll_editor_view_action_t>;

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

struct pending_note_paint_gesture_t {
    std::uint64_t document_revision{};
    std::shared_ptr<const std::vector<note_paint_target_t>> targets;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> visited;
    lane_id_t last_lane;
    local_position_t last_position;
};

struct note_paint_gesture_preview_t {
    std::uint64_t document_revision{};
    std::shared_ptr<const std::vector<note_paint_target_t>> targets;
};

struct editor_hit_test_index_t;

struct pending_space_range_selection_t {
    std::uint64_t document_revision{};
    std::shared_ptr<const editor_hit_test_index_t> hit_index;
    local_position_t anchor;
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    editor_selection_scope_t scope{};
};

struct space_range_selection_preview_t {
    std::uint64_t document_revision{};
    local_position_t anchor;
    local_position_t cursor;
    std::uint64_t first_row{};
    std::uint64_t last_row{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<editor_selection_object_t>> objects;
};

struct editor_area_selection_preview_t {
    std::uint64_t document_revision{};
    double left_pixels{};
    double top_pixels{};
    double right_pixels{};
    double bottom_pixels{};
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<editor_selection_object_t>> objects;
};

struct pending_editor_area_selection_t {
    std::uint64_t document_revision{};
    std::shared_ptr<const editor_hit_test_index_t> hit_index;
    double anchor_x_pixels{};
    double anchor_y_pixels{};
    double current_x_pixels{};
    double current_y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    std::shared_ptr<const std::vector<object_id_t>> notes;
    editor_selection_scope_t scope{};
    std::shared_ptr<const std::vector<editor_selection_object_t>> objects;
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
    std::optional<pending_note_paint_gesture_t> pending_note_paint_gesture;
    std::optional<pending_editor_area_selection_t> pending_editor_area_selection;
    std::optional<pending_space_range_selection_t> pending_space_range_selection;
    std::vector<object_id_t> selected_notes;
    std::vector<editor_selection_object_t> selected_objects;
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
    std::optional<note_paint_gesture_preview_t> pending_note_paint_gesture;
    std::optional<editor_area_selection_preview_t> pending_editor_area_selection;
    std::optional<space_range_selection_preview_t> pending_space_range_selection;
    std::vector<object_id_t> selected_notes;
    std::vector<editor_selection_object_t> selected_objects;
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
    struct note_audition_t {
        object_id_t note;
        std::optional<resource_id_t> sound;

        auto operator==(const note_audition_t&) const -> bool = default;
    };

    bool accepted{};
    bool document_changed{};
    std::optional<editor_interaction_view_model_t> view;
    diagnostic_list_t diagnostics;
    std::optional<note_audition_t> audition;

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

// Maps screen y through the view's exact measure-prefix geometry, zoom, and
// scroll to the nearest editable row. Half-row ties resolve toward the earlier row.
[[nodiscard]] auto editor_position_at_y_pixels(const chart_document_t& chart,
                                               const editor_interaction_view_model_t& view,
                                               double y_pixels) -> std::optional<local_position_t>;

enum class editor_hit_target_kind_t {
    lane,
    note,
    hold_head,
    hold_tail,
};

struct editor_hit_target_t {
    editor_hit_target_kind_t kind{editor_hit_target_kind_t::lane};
    lane_id_t lane;
    std::optional<object_id_t> note;
    std::optional<note_kind_t> note_kind;
    local_position_t position;

    auto operator==(const editor_hit_target_t&) const -> bool = default;
};

struct editor_hit_test_entry_t {
    object_id_t note;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    local_position_t position;
    std::uint64_t absolute_row{};
    bool is_tail{};
};

struct editor_note_span_entry_t {
    object_id_t note;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
};

struct editor_selection_span_entry_t {
    editor_selection_object_t object;
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    std::optional<lane_id_t> lane;
};

enum class editor_inline_editor_kind_t {
    note_properties,
    bgm_event_properties,
    bpm_event,
    stop_event,
    delay_event,
    warp_event,
    scroll_event,
    speed_event,
    kiai_event,
    timing_anchor,
    bga_event,
    bookmark,
    section,
};

struct editor_hover_index_entry_t {
    editor_selection_object_t object;
    editor_inline_editor_kind_t editor{editor_inline_editor_kind_t::note_properties};
    local_position_t position;
    std::uint64_t absolute_row{};
    std::uint64_t end_absolute_row{};
    std::string tooltip;
};

// Build once per document revision and reuse while querying pointer motion.
// Each measure bucket contains note heads and hold tails owned by that measure.
struct editor_hit_test_index_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    measure_index_t measures;
    std::vector<std::vector<editor_hit_test_entry_t>> measure_buckets;
    std::vector<editor_note_span_entry_t> note_spans;
    std::size_t note_span_tree_leaf_base{1};
    std::vector<std::uint64_t> note_span_tree_max_end;
    std::vector<editor_selection_span_entry_t> selection_spans;
    std::size_t selection_span_tree_leaf_base{1};
    std::vector<std::uint64_t> selection_span_tree_max_end;
    std::vector<editor_selection_object_t> available_objects;
    // Sorted by typed object ID for bounded hit-detail and click validation.
    std::vector<editor_hover_index_entry_t> hover_objects_by_id;
};

struct editor_hit_test_request_t {
    double x_pixels{};
    double y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    double note_hit_radius_pixels{8.0};
};

struct editor_object_hover_request_t {
    double x_pixels{};
    double y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    double hit_radius_pixels{8.0};
    editor_selection_scope_t scope{.notes = false, .bgm = true, .timing = true, .bga = true};
};

struct editor_object_hover_state_t {
    editor_selection_object_t object;
    editor_inline_editor_kind_t editor{editor_inline_editor_kind_t::note_properties};
    local_position_t position;
    std::uint64_t document_revision{};
    std::string tooltip;

    auto operator==(const editor_object_hover_state_t&) const -> bool = default;
};

struct editor_object_edit_request_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    editor_selection_object_t object;
    editor_inline_editor_kind_t editor{editor_inline_editor_kind_t::note_properties};

    auto operator==(const editor_object_edit_request_t&) const -> bool = default;
};

struct editor_double_click_settings_t {
    std::uint64_t maximum_interval_milliseconds{500};
    double pointer_slop_pixels{4.0};
};

struct editor_click_candidate_t {
    editor_selection_object_t object;
    std::uint64_t document_revision{};
    std::uint64_t time_milliseconds{};
    double x_pixels{};
    double y_pixels{};
};

struct editor_double_click_state_t {
    std::optional<editor_click_candidate_t> previous_click;
};

struct editor_area_selection_request_t {
    double start_x_pixels{};
    double start_y_pixels{};
    double end_x_pixels{};
    double end_y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    editor_selection_scope_t scope{};
};

[[nodiscard]] auto build_editor_hit_test_index(const chart_document_t& chart,
                                               std::uint64_t document_revision)
    -> std::variant<editor_hit_test_index_t, diagnostic_list_t>;
[[nodiscard]] auto hit_test_editor(const chart_document_t& chart,
                                   const editor_hit_test_index_t& index,
                                   const editor_interaction_view_model_t& view,
                                   const editor_hit_test_request_t& request)
    -> std::optional<editor_hit_target_t>;
// Queries indexed typed objects in the requested pane scope. An empty result
// clears the current hover target; the returned tooltip is stable for the
// indexed document revision and contains no renderer-specific data.
[[nodiscard]] auto hover_editor_object(const chart_document_t& chart,
                                       const editor_hit_test_index_t& index,
                                       const editor_interaction_view_model_t& view,
                                       const editor_object_hover_request_t& request)
    -> std::optional<editor_object_hover_state_t>;
// A click is eligible only while its view/index/hover share a revision and
// focus and pointer capture are intact. The first click arms the stable typed
// object; only a second click on that same object within platform thresholds
// returns an edit request. Invalid input clears the pending click.
[[nodiscard]] auto register_editor_object_click(
    editor_double_click_state_t& state, const editor_hit_test_index_t& index,
    const editor_interaction_view_model_t& view,
    const std::optional<editor_object_hover_state_t>& hover, double x_pixels, double y_pixels,
    std::uint64_t time_milliseconds, editor_double_click_settings_t settings = {},
    bool pointer_captured = true, bool focused = true)
    -> std::optional<editor_object_edit_request_t>;
auto clear_editor_double_click_state(editor_double_click_state_t& state) -> void;
// Returns notes whose lane column and head, tail, or hold body intersects the
// screen-space rectangle. The query uses exact zoom/scroll mapping and the
// revision-bound measure/span data in the hit-test index.
[[nodiscard]] auto editor_notes_in_area_selection(const chart_document_t& chart,
                                                  const editor_hit_test_index_t& index,
                                                  const editor_interaction_view_model_t& view,
                                                  const editor_area_selection_request_t& request)
    -> std::variant<std::vector<object_id_t>, diagnostic_list_t>;
// Non-note chart tracks are queried against the exact continuous chart-row
// interval under the screen rectangle and are gated by request.scope. Beat-
// anchored bookmarks/sections participate; audio-time markers are excluded
// until the controller has a timing index to project them into chart rows. The
// screen rectangle supplies only the vertical interval; lane-column geometry
// applies to notes through editor_notes_in_area_selection.
[[nodiscard]] auto editor_objects_in_row_range(const chart_document_t& chart,
                                               const editor_hit_test_index_t& index,
                                               std::uint64_t first_row, std::uint64_t last_row,
                                               editor_selection_scope_t scope)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t>;
[[nodiscard]] auto editor_objects_in_area_selection(const chart_document_t& chart,
                                                    const editor_hit_test_index_t& index,
                                                    const editor_interaction_view_model_t& view,
                                                    const editor_area_selection_request_t& request)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t>;

// Dispatches one typed frontend action. Document edits are published through
// commit_document_command so validation, undo, and redo share one boundary.
[[nodiscard]] auto dispatch_editor_action(document_history_t& history,
                                          editor_interaction_state_t& state,
                                          const editor_ui_action_t& action)
    -> editor_interaction_result_t;

} // namespace wormedit
