#pragma once

// Read-only queries: inspection, position mapping, hit testing, hover and area selection.

#include <wormedit/core/interaction_state.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

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
    bar_line,
    volume_event,
    sample_set_event,
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

} // namespace wormedit
