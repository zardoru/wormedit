#pragma once

// Pending gesture captures and their render-only previews.

#include <wormedit/core/interaction_actions.h>
#include <wormedit/core/command.h>
#include <wormedit/core/time_shift.h>

#include <array>
#include <concepts>
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
    std::optional<osu_hitsample_t> hitsample;

    [[nodiscard]] static auto from(editor_edit_context_t&, const hold_start_t&)
        -> diagnosed_t<std::optional<pending_hold_t>>;
    [[nodiscard]] auto validate_start(const editor_edit_context_t&) const -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, local_position_t) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&, local_position_t,
                             std::optional<bmson_source_geometry_t>) const -> editor_interaction_result_t;
    auto operator==(const pending_hold_t&) const -> bool = default;
};

struct keyboard_entry_plan_t {
    lane_id_t lane;
    local_position_t position;
    std::optional<object_id_t> occupant;
    std::optional<object_id_t> stem;
    std::optional<resource_id_t> sound;
    bool is_displacing{};
    std::optional<bmson_source_geometry_t> geometry;
    [[nodiscard]] static auto from(editor_edit_context_t&, const hold_start_t&)
        -> diagnosed_t<std::optional<keyboard_entry_plan_t>>;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const keyboard_entry_plan_t&) const -> bool = default;
};

class hold_gesture_t {
public:
    static constexpr auto kind = editor_gesture_kind_t::hold;
    static constexpr auto revision_message = update_hold_preview_action_t::missing_gesture_message;
    explicit hold_gesture_t(pending_hold_t primary) : primary_(std::move(primary)) {}
    hold_gesture_t(std::optional<pending_hold_t> primary, std::vector<pending_hold_t> additional)
        : primary_(std::move(primary)), additional_(std::move(additional)) {}
    explicit hold_gesture_t(keyboard_entry_plan_t entry) : immediate_(std::move(entry)) {}
    [[nodiscard]] auto primary() -> pending_hold_t* { return primary_ ? &*primary_ : nullptr; }
    [[nodiscard]] auto primary() const -> const pending_hold_t* { return primary_ ? &*primary_ : nullptr; }
    [[nodiscard]] auto additional() const -> std::span<const pending_hold_t> { return additional_; }
    [[nodiscard]] auto has_pending() const -> bool { return primary_ || !additional_.empty(); }
    [[nodiscard]] auto has_immediate_edit() const -> bool { return immediate_.has_value(); }
    [[nodiscard]] auto is_keyboard() const -> bool {
        return primary_ ? primary_->gesture_source == hold_gesture_source_t::keyboard_lane
                        : !additional_.empty();
    }
    void discard_primary() { primary_.reset(); }
    [[nodiscard]] static auto can_restore_begin(const hold_start_t&) -> bool;
    template<class Action> [[nodiscard]] static constexpr auto can_restore_revision(const Action&) -> bool {
        return std::same_as<Action, begin_lane_key_hold_gesture_action_t> || std::same_as<Action, std::uint64_t>;
    }
    [[nodiscard]] static auto begin(editor_edit_context_t&, const hold_start_t&)
        -> diagnosed_t<std::optional<hold_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, const begin_lane_key_hold_gesture_action_t&) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, const update_hold_preview_action_t&) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, const update_alt_pointer_hold_gesture_action_t&) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, local_position_t) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, std::uint64_t canvas_row) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, const commit_hold_action_t&) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, const release_lane_key_hold_gesture_action_t&) -> diagnostic_list_t;
    [[nodiscard]] auto revise(editor_edit_context_t&, const release_alt_pointer_hold_gesture_action_t&) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) -> editor_interaction_result_t;
    auto operator==(const hold_gesture_t&) const -> bool = default;
private:
    struct release_t {
        lane_id_t lane;
        local_position_t position;
        std::optional<std::array<std::uint64_t, 2>> canvas_rows;
        std::optional<bmson_source_geometry_t> geometry;
        auto operator==(const release_t&) const -> bool = default;
    };
    [[nodiscard]] auto revise_primary(editor_edit_context_t&, local_position_t, bool keep_visible) -> diagnostic_list_t;
    [[nodiscard]] auto selected_hold(lane_id_t) const -> const pending_hold_t*;
    void remove_hold(lane_id_t);
    std::optional<pending_hold_t> primary_;
    std::vector<pending_hold_t> additional_;
    std::optional<keyboard_entry_plan_t> immediate_;
    std::optional<release_t> release_;
};

struct hold_gesture_preview_t {
    static constexpr auto kind = editor_gesture_kind_t::hold;
    std::optional<pending_hold_t> primary_hold;
    std::vector<pending_hold_t> additional_holds;
    [[nodiscard]] static auto from(const hold_gesture_t& holds) -> hold_gesture_preview_t {
        const auto* primary = holds.primary();
        const auto additional = holds.additional();
        return {.primary_hold = primary ? std::optional{*primary} : std::nullopt,
                .additional_holds = std::vector<pending_hold_t>{additional.begin(), additional.end()}};
    }
    [[nodiscard]] auto primary() -> pending_hold_t* { return primary_hold ? &*primary_hold : nullptr; }
    [[nodiscard]] auto primary() const -> const pending_hold_t* { return primary_hold ? &*primary_hold : nullptr; }
    [[nodiscard]] auto additional() const -> std::span<const pending_hold_t> { return additional_holds; }
    auto operator==(const hold_gesture_preview_t&) const -> bool = default;
};

struct note_duration_preview_t {
    object_id_t note;
    note_kind_t kind{note_kind_t::tap};
    std::optional<local_position_t> end;

    auto operator==(const note_duration_preview_t&) const -> bool = default;
};

struct pending_note_duration_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::duration;
    static constexpr std::string_view revision_message = "duration gesture update requires an active gesture";
    object_id_t reference_note;
    std::vector<object_id_t> notes;
    local_position_t preview_end;
    std::vector<note_duration_preview_t> preview;

    [[nodiscard]] static auto begin(editor_edit_context_t&, object_id_t)
        -> diagnosed_t<std::optional<pending_note_duration_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, local_position_t) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_note_duration_gesture_t&) const -> bool = default;
};



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
    static constexpr auto revision_message = "note move update requires an active gesture";
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
    // Bar lines selected with the moved objects follow the drag's row delta through the row shift.
    std::vector<measure_id_t> bar_lines;
    bar_line_time_t bar_line_time{bar_line_time_t::keep_song_time};

    [[nodiscard]] static auto begin(editor_edit_context_t&, object_id_t, bool displace_to_bgm = false)
        -> diagnosed_t<std::optional<pending_note_move_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, local_position_t, lane_id_t, bool,
                              std::optional<object_id_t>, std::optional<std::uint64_t>) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_note_move_gesture_t&) const -> bool = default;
};

// A vertical drag anchored by a selected bar line. Unlike the note-move gesture, this has no
// playable-lane component: it moves the current note/bar-line selection by one snapped row delta.
struct pending_bar_line_move_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::move;
    static constexpr auto revision_message = "bar-line move update requires an active gesture";
    measure_id_t reference_bar_line;
    std::uint64_t reference_row{};
    std::int64_t preview_rows{};
    local_position_t preview_position;
    shift_selection_action_t selection;

    [[nodiscard]] static auto begin(editor_edit_context_t&, measure_id_t)
        -> diagnosed_t<std::optional<pending_bar_line_move_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, local_position_t) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_bar_line_move_gesture_t&) const -> bool = default;
};

struct pending_note_paint_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::paint;
    static constexpr auto revision_message = "note paint update requires an active gesture";
    std::shared_ptr<const std::vector<note_paint_target_t>> targets;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> visited;
    lane_id_t last_lane;
    local_position_t last_position;
    std::optional<std::uint64_t> last_canvas_row;
    std::optional<resource_id_t> sound;
    note_paint_intent_t intent{note_paint_intent_t::toggle};
    note_kind_t note_kind{note_kind_t::tap};
    std::optional<std::vector<layer_id_t>> layers;
    std::optional<bmson_source_geometry_t> source_geometry;
    std::optional<osu_hitsample_t> hitsample;

    [[nodiscard]] static auto begin(editor_edit_context_t&, const begin_note_paint_gesture_action_t&)
        -> diagnosed_t<std::optional<pending_note_paint_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, lane_id_t, local_position_t,
                              std::optional<bmson_source_geometry_t>,
                              std::optional<std::uint64_t> canvas_row = std::nullopt) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_note_paint_gesture_t&) const -> bool = default;
};

struct note_paint_gesture_preview_t {
    document_stamp_t stamp;
    std::shared_ptr<const std::vector<note_paint_target_t>> targets;
    auto operator==(const note_paint_gesture_preview_t&) const -> bool = default;
};

struct pending_resource_paint_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::resource_paint;
    static constexpr auto revision_message = "keysound paint update requires an active gesture";
    std::optional<resource_id_t> resource;
    std::optional<osu_hitsample_t> hitsample;
    std::shared_ptr<const std::vector<editor_selection_object_t>> targets;
    std::unordered_set<std::uint64_t> visited;
    [[nodiscard]] static auto begin(editor_edit_context_t&, const begin_resource_paint_gesture_action_t&)
        -> diagnosed_t<std::optional<pending_resource_paint_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, object_id_t) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_resource_paint_gesture_t&) const -> bool = default;
};

struct pending_semantic_layer_paint_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::semantic_paint;
    static constexpr auto revision_message = "semantic-layer paint update requires an active gesture";
    layer_id_t layer;
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<object_id_t>> bgm_events;
    std::unordered_set<std::uint64_t> visited;
    bool replace_memberships{};
    bool clear_memberships{};
    semantic_layer_paint_mode_t mode{semantic_layer_paint_mode_t::add};

    [[nodiscard]] static auto begin(editor_edit_context_t&, const begin_semantic_layer_paint_gesture_action_t&)
        -> diagnosed_t<std::optional<pending_semantic_layer_paint_gesture_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, std::optional<editor_selection_object_t>) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_semantic_layer_paint_gesture_t&) const -> bool = default;
};

struct editor_hit_test_index_t;

// Field inspection follows scrolling; retained Compose overlays keep captured coordinates.
enum class editor_area_preview_anchor_policy_t { captured_coordinates, viewport_relative };
struct pending_editor_area_selection_t;

struct editor_area_selection_preview_t {
    [[nodiscard]] static auto from(const pending_editor_area_selection_t&, document_stamp_t,
                                    double viewport_scroll_pixels, editor_area_preview_anchor_policy_t)
        -> editor_area_selection_preview_t;
    document_stamp_t stamp;
    double left_pixels{};
    double top_pixels{};
    double right_pixels{};
    double bottom_pixels{};
    std::shared_ptr<const std::vector<object_id_t>> notes;
    std::shared_ptr<const std::vector<editor_selection_object_t>> objects;
    auto operator==(const editor_area_selection_preview_t&) const -> bool = default;
};

struct pending_editor_area_selection_t {
    static constexpr auto kind = editor_gesture_kind_t::area;
    static constexpr auto revision_message = "area selection update requires an active gesture";
    static constexpr bool needs_initial_view = true;
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

    [[nodiscard]] static auto begin(editor_edit_context_t&, const begin_editor_area_selection_action_t&)
        -> diagnosed_t<std::optional<pending_editor_area_selection_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&, double x_pixels, double y_pixels) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_editor_area_selection_t&) const -> bool = default;
};

struct pending_space_range_selection_t {
    static constexpr auto kind = editor_gesture_kind_t::range;
    static constexpr auto revision_message = "Space range selection update requires an active gesture";
    static constexpr bool needs_initial_view = true;
    std::shared_ptr<const editor_hit_test_index_t> hit_index;
    local_position_t anchor;
    std::uint64_t anchor_canvas_row{};
    editor_area_selection_mode_t mode{editor_area_selection_mode_t::replace};
    editor_selection_scope_t scope{};
    std::uint64_t minimum_span_rows{};

    [[nodiscard]] static auto begin(editor_edit_context_t&, const space_range_selection_action_t&)
        -> diagnosed_t<std::optional<pending_space_range_selection_t>>;
    [[nodiscard]] auto revise(editor_edit_context_t&) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_edit_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_space_range_selection_t&) const -> bool = default;
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
    auto operator==(const space_range_selection_preview_t&) const -> bool = default;
};

// Temporary: remaining alternatives migrate here and legacy fields are removed by T-634.
using active_gesture_t = std::variant<proposal_t<pending_note_duration_gesture_t, document_stamp_t>,
                                      proposal_t<pending_note_move_gesture_t, document_stamp_t>,
                                      proposal_t<pending_bar_line_move_gesture_t, document_stamp_t>,
                                      proposal_t<pending_note_paint_gesture_t, document_stamp_t>,
                                      proposal_t<pending_semantic_layer_paint_gesture_t, document_stamp_t>,
                                      proposal_t<pending_resource_paint_gesture_t, document_stamp_t>,
                                      proposal_t<pending_editor_area_selection_t, document_stamp_t>,
                                      proposal_t<pending_space_range_selection_t, document_stamp_t>,
                                      proposal_t<hold_gesture_t, document_stamp_t>>;

// A projection contains only render data; paint's visited cells stay in the capture.
using active_gesture_preview_t = std::variant<
    proposal_t<pending_note_duration_gesture_t, document_stamp_t>,
    proposal_t<pending_note_move_gesture_t, document_stamp_t>,
    proposal_t<pending_bar_line_move_gesture_t, document_stamp_t>, note_paint_gesture_preview_t,
    editor_area_selection_preview_t, space_range_selection_preview_t,
    proposal_t<hold_gesture_preview_t, document_stamp_t>>;

[[nodiscard]] inline auto build_editor_gesture_preview(const std::optional<active_gesture_t>& active,
    double viewport_scroll_pixels = 0.0,
    editor_area_preview_anchor_policy_t area_anchor_policy = editor_area_preview_anchor_policy_t::viewport_relative,
    std::optional<space_range_selection_preview_t>&& range_preview = std::nullopt)
    -> std::optional<active_gesture_preview_t> {
    if (!active) return std::nullopt;
    return std::visit([viewport_scroll_pixels, area_anchor_policy, &range_preview](const auto& proposal) -> std::optional<active_gesture_preview_t> {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_note_paint_gesture_t>)
            return note_paint_gesture_preview_t{.stamp = proposal.stamp, .targets = proposal.payload.targets};
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_semantic_layer_paint_gesture_t> ||
                           std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_resource_paint_gesture_t>)
            return std::nullopt;
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_editor_area_selection_t>)
            return editor_area_selection_preview_t::from(proposal.payload, proposal.stamp,
                                                        viewport_scroll_pixels, area_anchor_policy);
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, pending_space_range_selection_t>) {
            if (!range_preview) return std::nullopt;
            return std::move(*range_preview);
        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(proposal.payload)>, hold_gesture_t>)
            return proposal_t<hold_gesture_preview_t, document_stamp_t>{.stamp = proposal.stamp, .payload = hold_gesture_preview_t::from(proposal.payload)};
        else return proposal;
    }, *active);
}

// The gesture contract identifies wrapped captures; render-only payloads are stored directly.
template<class Gesture, class Variant>
[[nodiscard]] auto gesture_storage(Variant* active) {
    using storage_type = std::conditional_t<requires { Gesture::kind; },
        proposal_t<Gesture, document_stamp_t>, Gesture>;
    return active ? std::get_if<storage_type>(active) : nullptr;
}

template<class Gesture, class Variant>
[[nodiscard]] auto gesture_payload(Variant* active)
    -> std::conditional_t<std::is_const_v<Variant>, const Gesture*, Gesture*> {
    const auto stored = gesture_storage<Gesture>(active);
    if constexpr (requires { Gesture::kind; }) return stored ? &stored->payload : nullptr;
    else return stored;
}

template<class Gesture, class Variant>
[[nodiscard]] auto gesture_stamp(const Variant* active) -> const document_stamp_t* {
    const auto stored = gesture_storage<Gesture>(active);
    return stored ? &stored->stamp : nullptr;
}

template<class Variant>
using hold_projection_t = std::conditional_t<std::same_as<std::remove_cv_t<Variant>, active_gesture_t>,
    hold_gesture_t, hold_gesture_preview_t>;

template<class Variant>
[[nodiscard]] auto hold_payload(Variant* active) {
    const auto holds = gesture_payload<hold_projection_t<Variant>>(active);
    return holds ? holds->primary() : nullptr;
}

template<class Variant>
[[nodiscard]] auto additional_hold_payload(const Variant* active) -> std::span<const pending_hold_t> {
    const auto holds = gesture_payload<hold_projection_t<Variant>>(active);
    return holds ? holds->additional() : std::span<const pending_hold_t>{};
}

} // namespace wormedit
