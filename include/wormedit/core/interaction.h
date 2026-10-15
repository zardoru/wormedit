#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/lane_presentation.h>

#include <compare>
#include <concepts>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace wormedit {

struct editor_interaction_setup_t;
struct editor_edit_context_t;
struct editor_interaction_result_t;

enum class editor_gesture_kind_t {
    hold, keyboard_holds, duration, move, paint, semantic_paint, area, range
};

// Four editor lanes: background (including video), layer, layer 2, poor.
inline constexpr std::uint8_t editor_bga_lane_mask_all = 15;
[[nodiscard]] auto editor_bga_lane_index(visual_event_kind_t kind)
    -> std::optional<std::size_t>;
[[nodiscard]] auto editor_bga_visible_lane_mask(const chart_document_t& chart,
                                               std::uint8_t revealed) -> std::uint8_t;

inline constexpr double minimum_pixels_per_standard_measure = 24.0;
inline constexpr double maximum_pixels_per_standard_measure = 4096.0;
inline constexpr double default_pixels_per_standard_measure = 192.0;
inline constexpr double editor_cursor_edge_margin_fraction = 0.2;

// Scrolling may run past either end of the chart by the edge margin, so the playback
// anchor (edge margin from the chart-start side) reaches both the first and last rows.
template <typename T>
[[nodiscard]] constexpr auto editor_maximum_viewport_scroll(T content_height, T viewport_height,
    T end_margin_fraction = static_cast<T>(editor_cursor_edge_margin_fraction))
    -> T {
    const auto maximum =
        content_height - viewport_height * end_margin_fraction;
    return maximum > T{} ? maximum : T{};
}

// Following may extend the chart-start margin to its validated anchor. The chart-end
// margin may shrink to that anchor; ordinary callers retain the original 20 percent.
[[nodiscard]] inline auto is_editor_viewport_margins_valid(double minimum_fraction,
                                                       double end_fraction) noexcept -> bool {
    return std::isfinite(minimum_fraction) && minimum_fraction >= editor_cursor_edge_margin_fraction &&
           minimum_fraction <= 1.0 && std::isfinite(end_fraction) && end_fraction >= 0.0 &&
           end_fraction <= editor_cursor_edge_margin_fraction;
}


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
    local_position_t position;
};

struct commit_hold_action_t {
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
    local_position_t position;
    bool alt_or_option_down{true};
    bool pointer_captured{true};
};

struct release_alt_pointer_hold_gesture_action_t {
    local_position_t position;
    bool alt_or_option_down{true};
    bool pointer_captured{true};
    std::optional<bmson_source_geometry_t> source_geometry;
};

struct begin_note_duration_gesture_action_t {
    object_id_t reference_note;
};

enum class editor_gesture_phase_t { update, commit };

struct update_note_duration_gesture_action_t {
    local_position_t reference_end;
    editor_gesture_phase_t phase{editor_gesture_phase_t::update};
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
    editor_gesture_phase_t phase{editor_gesture_phase_t::update};
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

// Frontend-projected, canonical-source-Y rectangles for non-note canvas objects.
struct editor_area_object_geometry_t {
    editor_selection_object_t object;
    double left_pixels{};
    double top_pixels{};
    double right_pixels{};
    double bottom_pixels{};
};

struct editor_area_geometry_projection_t {
    double viewport_scroll_pixels{};
    double pixels_per_standard_measure{};
    double viewport_height_pixels{};
};

struct editor_area_geometry_snapshot_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    std::vector<editor_area_object_geometry_t> objects;
    std::optional<editor_area_geometry_projection_t> projection;
};

// A validated, revision-bound intent to audition one semantic sound object.
// Runtime audio resolves resources, BMSON slices, and fused layers from this
// identity rather than relying on a lossy resource-only hint.
struct editor_object_audition_request_t {
    std::uint64_t document_revision{};
    chart_id_t chart;
    editor_selection_object_t object;

    auto operator==(const editor_object_audition_request_t&) const -> bool = default;
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
    editor_gesture_phase_t phase{editor_gesture_phase_t::update};
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
    editor_gesture_phase_t phase{editor_gesture_phase_t::update};
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

enum class editor_field_scroll_direction_t : std::uint8_t {
    up,
    down,
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
    begin_note_paint_gesture_action_t, update_note_paint_gesture_action_t,
    end_note_paint_gesture_action_t, begin_semantic_layer_paint_gesture_action_t,
    update_semantic_layer_paint_gesture_action_t, begin_editor_area_selection_action_t,
    update_editor_area_selection_action_t, cancel_editor_area_selection_action_t,
    space_range_selection_action_t, cancel_editor_gesture_action_t, select_editor_notes_action_t,
    select_editor_objects_action_t, delete_editor_selection_action_t, select_editor_lane_action_t,
    assign_selected_objects_to_lane_action_t, adjust_editor_zoom_action_t,
    scroll_editor_view_action_t, set_edit_snap_action_t>;

// Exact, revision-bound result that a pending hold would publish. The source
// anchor/moving endpoint stay on pending_hold_t so controllers can update the
// gesture, while renderers use this normalized result for destructive merge
// previews without reimplementing collision policy.
using hold_preview_removals_t = std::shared_ptr<const std::vector<object_id_t>>;
inline auto equal_hold_removals(const std::vector<object_id_t>& left,
                                const std::vector<object_id_t>& right) -> bool {
    return left == right;
}
inline auto equal_hold_removals(const hold_preview_removals_t& left,
                                const hold_preview_removals_t& right) -> bool {
    return (!left && !right) || (left && right && *left == *right);
}
template <class Removals> struct hold_placement_t {
    local_position_t head;
    std::optional<local_position_t> tail;
    Removals removals;
    bool adds_note{};

    auto operator==(const hold_placement_t& other) const -> bool {
        return head == other.head && tail == other.tail && adds_note == other.adds_note &&
               equal_hold_removals(removals, other.removals);
    }
};
using pending_hold_placement_preview_t = hold_placement_t<hold_preview_removals_t>;

struct pending_hold_t {
    local_position_t position;
    lane_id_t lane;
    document_stamp_t stamp;
    std::optional<std::uint64_t> anchor_canvas_row;
    std::optional<std::uint64_t> preview_canvas_row;
    std::optional<local_position_t> preview_end;
    std::optional<pending_hold_placement_preview_t> placement_preview;
    std::optional<hold_gesture_source_t> gesture_source;
    // Captured on key-down; changing the armed sound must not retarget a held key.
    std::optional<resource_id_t> sound;

    note_kind_t kind{note_kind_t::tap};
    std::optional<std::vector<layer_id_t>> layers;
    std::shared_ptr<const bmson_source_geometry_t> source_geometry;

    auto operator==(const pending_hold_t&) const -> bool = default;
};

struct note_duration_preview_t {
    object_id_t note;
    note_kind_t kind{note_kind_t::tap};
    std::optional<local_position_t> end;

    auto operator==(const note_duration_preview_t&) const -> bool = default;
};

struct pending_note_duration_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::duration;
    static constexpr std::string_view update_message = "duration gesture update requires an active gesture";
    object_id_t reference_note;
    std::vector<object_id_t> notes;
    local_position_t preview_end;
    std::vector<note_duration_preview_t> preview;

    [[nodiscard]] static auto begin(editor_edit_context_t&, object_id_t)
        -> diagnosed_t<std::optional<pending_note_duration_gesture_t>>;
    [[nodiscard]] auto update(editor_edit_context_t&, local_position_t) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_note_duration_gesture_t&) const -> bool = default;
};


enum class note_move_conflict_t { none, occupied, out_of_lane, out_of_chart, other };

struct note_move_preview_t {
    object_id_t note;
    lane_id_t lane;
    local_position_t position;
    std::optional<local_position_t> end;
    note_move_conflict_t conflict{note_move_conflict_t::none};
    std::optional<object_id_t> occupant;
    std::optional<std::int64_t> display_column;
    std::size_t display_columns{};
    // Display-only rows preserve a rejected member's translation past chart boundaries.
    std::optional<long double> display_begin_row;
    std::optional<long double> display_end_row;

    auto operator==(const note_move_preview_t&) const -> bool = default;
};

struct note_move_bgm_preview_t {
    object_id_t note;
    object_id_t lane;
    local_position_t position;
    std::optional<local_position_t> end;
    std::optional<resource_id_t> sound;
    std::optional<std::uint64_t> canvas_begin_row;
    std::optional<std::uint64_t> canvas_end_row;
    auto operator==(const note_move_bgm_preview_t&) const -> bool = default;
};

struct note_move_bgm_lane_preview_t {
    object_id_t id;
    std::size_t source_order{};
    auto operator==(const note_move_bgm_lane_preview_t&) const -> bool = default;
};

struct pending_note_move_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::move;
    static constexpr auto update_message = "note move update requires an active gesture";
    object_id_t reference_note;
    std::vector<object_id_t> notes;
    local_position_t preview_position;
    lane_id_t preview_lane;
    std::vector<note_move_preview_t> preview;
    std::vector<object_id_t> occupants;
    std::size_t conflicting_notes{};
    bool to_bgm{};
    bool displace_to_bgm{};
    std::optional<object_id_t> preferred_bgm_lane;
    std::vector<note_move_bgm_preview_t> bgm_preview;
    std::vector<note_move_bgm_lane_preview_t> new_bgm_lanes;
    std::vector<std::string> bgm_diagnostics;
    std::optional<std::uint64_t> virtual_bgm_row;

    [[nodiscard]] static auto begin(editor_edit_context_t&, object_id_t, bool displace_to_bgm = false)
        -> diagnosed_t<std::optional<pending_note_move_gesture_t>>;
    [[nodiscard]] auto update(editor_edit_context_t&, local_position_t, lane_id_t, bool,
                              std::optional<object_id_t>, std::optional<std::uint64_t>) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_note_move_gesture_t&) const -> bool = default;
};

struct pending_note_paint_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::paint;
    static constexpr auto update_message = "note paint update requires an active gesture";
    std::shared_ptr<const std::vector<note_paint_target_t>> targets;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> visited;
    lane_id_t last_lane;
    local_position_t last_position;
    std::optional<resource_id_t> sound;
    note_paint_intent_t intent{note_paint_intent_t::toggle};
    note_kind_t note_kind{note_kind_t::tap};
    std::optional<std::vector<layer_id_t>> layers;
    std::optional<bmson_source_geometry_t> source_geometry;

    [[nodiscard]] static auto begin(editor_edit_context_t&, const begin_note_paint_gesture_action_t&)
        -> diagnosed_t<std::optional<pending_note_paint_gesture_t>>;
    [[nodiscard]] auto update(editor_edit_context_t&, lane_id_t, local_position_t,
                              std::optional<bmson_source_geometry_t>) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_note_paint_gesture_t&) const -> bool = default;
};

struct note_paint_gesture_preview_t {
    document_stamp_t stamp;
    std::shared_ptr<const std::vector<note_paint_target_t>> targets;
    auto operator==(const note_paint_gesture_preview_t&) const -> bool = default;
};

struct pending_semantic_layer_paint_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::semantic_paint;
    static constexpr auto update_message = "semantic-layer paint update requires an active gesture";
    layer_id_t layer;
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<object_id_t>> bgm_events;
    std::unordered_set<std::uint64_t> visited;
    bool replace_memberships{};
    bool clear_memberships{};
    semantic_layer_paint_mode_t mode{semantic_layer_paint_mode_t::add};

    [[nodiscard]] static auto begin(editor_edit_context_t&, const begin_semantic_layer_paint_gesture_action_t&)
        -> diagnosed_t<std::optional<pending_semantic_layer_paint_gesture_t>>;
    [[nodiscard]] auto update(editor_edit_context_t&, std::optional<editor_selection_object_t>) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_semantic_layer_paint_gesture_t&) const -> bool = default;
};

// Temporary: remaining alternatives migrate here and legacy fields are removed by T-634.
using active_gesture_t = std::variant<proposal_t<pending_note_duration_gesture_t, document_stamp_t>,
                                      proposal_t<pending_note_move_gesture_t, document_stamp_t>,
                                      proposal_t<pending_note_paint_gesture_t, document_stamp_t>,
                                      proposal_t<pending_semantic_layer_paint_gesture_t, document_stamp_t>>;

// A projection contains only render data; paint's visited cells stay in the capture.
using active_gesture_preview_t = std::variant<
    proposal_t<pending_note_duration_gesture_t, document_stamp_t>,
    proposal_t<pending_note_move_gesture_t, document_stamp_t>, note_paint_gesture_preview_t>;

[[nodiscard]] inline auto build_editor_gesture_preview(const std::optional<active_gesture_t>& active)
    -> std::optional<active_gesture_preview_t> {
    if (!active) return std::nullopt;
    return std::visit([](const auto& proposal) -> std::optional<active_gesture_preview_t> {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_note_paint_gesture_t>)
            return note_paint_gesture_preview_t{.stamp = proposal.stamp, .targets = proposal.payload.targets};
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_semantic_layer_paint_gesture_t>)
            return std::nullopt;
        else
            return proposal;
    }, *active);
}

template<class Gesture, class Variant>
[[nodiscard]] auto gesture_payload(Variant* active)
    -> std::conditional_t<std::is_const_v<Variant>, const Gesture*, Gesture*> {
    if constexpr (std::same_as<Gesture, note_paint_gesture_preview_t>)
        return active ? std::get_if<Gesture>(active) : nullptr;
    else {
        const auto proposal = active ? std::get_if<proposal_t<Gesture, document_stamp_t>>(active) : nullptr;
        return proposal ? &proposal->payload : nullptr;
    }
}

struct editor_hit_test_index_t;

struct pending_space_range_selection_t {
    document_stamp_t stamp;
    std::shared_ptr<const editor_hit_test_index_t> hit_index;
    local_position_t anchor;
    std::uint64_t anchor_canvas_row{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    editor_selection_scope_t scope{};
    std::uint64_t minimum_span_rows{};
};

struct space_range_selection_preview_t {
    document_stamp_t stamp;
    local_position_t anchor;
    local_position_t cursor;
    std::uint64_t anchor_canvas_row{};
    std::uint64_t cursor_canvas_row{};
    std::uint64_t first_row{};
    std::uint64_t last_row{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<editor_selection_object_t>> objects;
};

struct canvas_row_span_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};

    auto operator==(const canvas_row_span_t&) const -> bool = default;
};

struct editor_area_selection_preview_t {
    document_stamp_t stamp;
    double left_pixels{};
    double top_pixels{};
    double right_pixels{};
    double bottom_pixels{};
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<editor_selection_object_t>> objects;
};

struct pending_editor_area_selection_t {
    document_stamp_t stamp;
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
    std::shared_ptr<const editor_area_geometry_snapshot_t> object_geometry;
};

struct editor_interaction_state_t {
    // Creates transient, frontend-independent editing state from a validated
    // history snapshot. Snap denominators are ordered by editor preference.
    [[nodiscard]] static auto
    from(const document_history_t& history, chart_id_t chart, lane_id_t active_lane,
         local_position_t cursor, std::span<const std::uint64_t> ordered_snap_denominators,
         std::uint64_t requested_snap_denominator, double viewport_height_pixels = 1.0)
        -> editor_interaction_setup_t;

    chart_id_t chart;
    lane_id_t active_lane;
    editor_scratch_presentation_t scratch_on_right;
    note_kind_t armed_note_kind{note_kind_t::tap};
    // An explicit palette target overrides sound defaults; an empty vector means No layer.
    std::optional<std::vector<layer_id_t>> armed_note_layers;
    local_position_t cursor;
    std::optional<std::uint64_t> virtual_bms_cursor_row;
    // Non-authoring signed offset, in chart rows, from `cursor` to where it is drawn: set while
    // following audio or scrolling with a fixed cursor anchor; other cursor moves clear it.
    double cursor_subrow{};
    std::vector<std::uint64_t> supported_snap_denominators;
    std::uint64_t active_snap_denominator{};
    bool active_snap_was_fallback{};
    double pixels_per_standard_measure{default_pixels_per_standard_measure};
    double viewport_scroll_pixels{};
    // Non-authoring bound retained after a configured playback anchor.
    double viewport_minimum_margin_fraction{editor_cursor_edge_margin_fraction};
    double viewport_end_margin_fraction{editor_cursor_edge_margin_fraction};
    double viewport_height_pixels{1.0};
    std::optional<double> fixed_cursor_screen_y_pixels;
    editor_field_scroll_direction_t scroll_direction{editor_field_scroll_direction_t::up};
    std::optional<pending_hold_t> pending_hold;
    std::vector<pending_hold_t> additional_keyboard_holds;
    std::optional<active_gesture_t> active_gesture;

    template<class Gesture> [[nodiscard]] auto gesture() -> Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    template<class Gesture> [[nodiscard]] auto gesture() const -> const Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    std::optional<pending_editor_area_selection_t> pending_editor_area_selection;
    std::optional<pending_space_range_selection_t> pending_space_range_selection;
    // The most recently committed non-empty area/Space interval. This is
    // controller state rather than renderer state so marker authoring and
    // future frontends can consume the same exact chart span.
    std::optional<position_span_t> retained_selection_range;
    std::optional<canvas_row_span_t> retained_canvas_selection_range;
    std::optional<std::size_t> selected_canvas_measure;
    std::vector<object_id_t> selected_notes;
    std::vector<editor_selection_object_t> selected_objects;
    // Document revision the selection was last checked against (see prune_stale_selection).
    std::optional<std::uint64_t> selection_checked_revision;
    // Edit snapping applies to every pointer-driven object class; while off, edits land on the
    // chart's finest row instead of the active grid (see effective_edit_snap_denominator).
    bool snap_enabled{true};
    // Y-lock (as in iBMSC and BMSE): pointer drags of notes change lanes only and never rows.
    bool y_lock{false};
    // View-only BGA event lane visibility; hidden lanes have no hit geometry.
    bool bga_lane_visible{true};
    std::uint8_t bga_revealed_lane_mask{};
};

// The denominator pointer-driven edits of any object class should snap to.
[[nodiscard]] inline auto edit_snap_denominator(const chart_document_t& chart,
                                                const editor_interaction_state_t& state) noexcept
    -> std::uint64_t {
    return effective_edit_snap_denominator(chart, state.active_snap_denominator, state.snap_enabled);
}

struct editor_interaction_view_model_t {
    chart_id_t chart;
    lane_id_t active_lane;
    editor_scratch_presentation_t scratch_on_right;
    std::string active_lane_name;
    local_position_t cursor;
    std::optional<std::uint64_t> virtual_bms_cursor_row;
    double cursor_subrow{};
    std::uint64_t active_snap_denominator{};
    bool snap_enabled{true};
    bool y_lock{false};
    bool bga_lane_visible{true};
    std::uint8_t bga_revealed_lane_mask{};
    std::vector<std::uint64_t> supported_snap_denominators;
    std::uint64_t document_revision{};
    double pixels_per_standard_measure{default_pixels_per_standard_measure};
    double viewport_scroll_pixels{};
    double viewport_height_pixels{1.0};
    double viewport_minimum_margin_fraction{editor_cursor_edge_margin_fraction};
    double viewport_end_margin_fraction{editor_cursor_edge_margin_fraction};
    std::optional<double> fixed_cursor_screen_y_pixels;
    editor_field_scroll_direction_t scroll_direction{editor_field_scroll_direction_t::up};
    std::optional<pending_hold_t> pending_hold;
    std::vector<pending_hold_t> additional_keyboard_holds;
    struct virtual_hold_preview_t {
        std::uint64_t anchor_row{};
        std::uint64_t end_row{};
        std::uint64_t head_row{};
        std::optional<std::uint64_t> tail_row;
        lane_id_t lane;
        std::uint64_t document_revision{};
        std::shared_ptr<const std::vector<object_id_t>> removals;
        bool adds_note{};
    };
    std::optional<virtual_hold_preview_t> pending_virtual_hold;
    std::vector<virtual_hold_preview_t> pending_canvas_keyboard_holds;
    std::optional<active_gesture_preview_t> active_gesture;

    template<class Gesture> [[nodiscard]] auto gesture() -> Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    template<class Gesture> [[nodiscard]] auto gesture() const -> const Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    std::optional<editor_area_selection_preview_t> pending_editor_area_selection;
    std::optional<space_range_selection_preview_t> pending_space_range_selection;
    std::optional<position_span_t> retained_selection_range;
    std::optional<canvas_row_span_t> retained_canvas_selection_range;
    std::optional<std::size_t> selected_canvas_measure;
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
    [[nodiscard]] static auto from(const document_history_t& history,
        const editor_interaction_state_t& state, bool document_changed = false)
        -> editor_interaction_result_t;
    [[nodiscard]] static auto from(
        std::variant<editor_interaction_view_model_t, diagnostic_list_t> view,
        bool document_changed = false) -> editor_interaction_result_t;

    bool accepted{};
    bool document_changed{};
    std::optional<editor_interaction_view_model_t> view;
    diagnostic_list_t diagnostics;
    std::optional<editor_object_audition_request_t> audition;
    // Distinguishes placement from deletion/merging for advance-after-place.
    std::optional<object_id_t> created_note;

    [[nodiscard]] explicit operator bool() const noexcept {
        return accepted;
    }
};

// A borrowed measure index is valid only for this chart and document revision.
struct editor_measure_geometry_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    const measure_index_t* index{};
};

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
    const auto source = static_cast<double>((row + view.cursor_subrow) *
        view.pixels_per_standard_measure / divisions) - view.viewport_scroll_pixels;
    return direction == editor_field_scroll_direction_t::down ? view.viewport_height_pixels - source : source;
}

// Resolve the snapped edit row and fractional offset beneath the stationary line
// after any viewport change. BMS virtual rows remain unmaterialized.
[[nodiscard]] auto synchronize_editor_cursor_to_viewport(const chart_document_t& chart,
    const measure_index_t& index, editor_interaction_state_t& state) -> bool;

// Drops selected notes, objects and retained spans that no longer exist in `chart`. Runs a
// chart scan only when the document revision changed since the last check and something is
// selected, so a steady frame costs nothing. Never mutates the document. Returns true if the
// selection changed.
auto prune_stale_selection(const chart_document_t& chart, std::uint64_t document_revision,
                           editor_interaction_state_t& state) -> bool;

[[nodiscard]] auto inspect_editor_interaction(const document_history_t& history,
                                              const editor_interaction_state_t& state)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t>;

// Borrow revision-bound geometry for this call; the producer must bind its index
// to this document/chart source and keep it alive through inspection. Missing or
// stale chart/revision bindings use the same exact path as the overload above.
[[nodiscard]] auto inspect_editor_interaction(const document_history_t& history,
    const editor_interaction_state_t& state, editor_measure_geometry_t geometry)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t>;

// Maps an exact chart position into backend-neutral vertical pixels for a view.
[[nodiscard]] auto editor_position_y_pixels(const chart_document_t& chart,
                                            const editor_interaction_view_model_t& view,
                                            local_position_t position) -> std::optional<double>;

// Maps screen y through the view's exact measure-prefix geometry, zoom, and
// scroll to the nearest editable row. Half-row ties resolve toward the earlier row.
[[nodiscard]] auto editor_position_at_y_pixels(const chart_document_t& chart,
                                               const editor_interaction_view_model_t& view,
                                               double y_pixels) -> std::optional<local_position_t>;

// The caller binds and retains this validated index to the current chart source.
[[nodiscard]] auto editor_position_at_y_pixels(const chart_document_t& chart,
    const measure_index_t& index, const editor_interaction_view_model_t& view,
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
    // Location of the source object in the chart (measure index for notes and
    // BGM events, otherwise zero; index within that container). The tooltip is
    // formatted from it only for the hovered object instead of for every object.
    std::size_t source_group{};
    std::size_t source_index{};
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
    double note_head_height_pixels{};
    double split_stage_gutter_pixels{};
};

struct editor_object_hover_request_t {
    double x_pixels{};
    double y_pixels{};
    double field_left_pixels{};
    double field_width_pixels{};
    double hit_radius_pixels{8.0};
    double note_head_height_pixels{};
    double split_stage_gutter_pixels{};
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
    std::shared_ptr<const editor_area_geometry_snapshot_t> object_geometry;
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

// The chart source family determines which authoring kinds are available.
[[nodiscard]] auto is_note_kind_allowed(const chart_document_t& chart, note_kind_t kind) -> bool;
[[nodiscard]] auto arm_note_kind(const document_history_t& history,
                                 editor_interaction_state_t& state, note_kind_t kind)
    -> editor_interaction_result_t;

// Dispatches one typed frontend action. Document edits are published through
// commit_document_command so validation, undo, and redo share one boundary.
[[nodiscard]] auto dispatch_editor_action(document_history_t& history,
                                          editor_interaction_state_t& state,
                                          const editor_interaction_action_t& action)
    -> editor_interaction_result_t;

// Typed objects are authoritative; legacy note-only callers fall back to their note list.
[[nodiscard]] auto editor_selection_targets(const editor_interaction_state_t& state)
    -> std::vector<editor_selection_object_t>;
[[nodiscard]] auto is_editor_selection_scope_accepted(editor_selection_scope_t scope,
    editor_selection_object_kind_t kind) -> bool;


// Select/invert only the requested families; selections outside that scope survive.
[[nodiscard]] auto select_editor_scope(document_history_t& history, editor_interaction_state_t& state,
    editor_selection_scope_t scope, bool invert) -> editor_interaction_result_t;

// Canonical typed selection for Quantize preview and Apply. Existing BGM and note IDs
// may coincide; unrelated selected object families do not participate.
[[nodiscard]] auto editor_quantize_selection(const editor_interaction_state_t& state)
    -> quantize_selection_t;

// Shared ownership predicate for commands that require a completed canvas edit.
[[nodiscard]] auto has_pending_editor_gesture(const editor_interaction_state_t& state) -> bool;

} // namespace wormedit
