#pragma once
#include <wormedit/core/geometry.h>

#include <wormedit/core/grid.h>
#include <wormedit/core/result.h>
#include <wormedit/core/interaction.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wormedit {

inline constexpr std::size_t bga_lane_overlap_stack_threshold{2};

struct editor_field_scene_index_options_t {
    std::size_t maximum_notes{100'000};
    std::size_t maximum_bgm_events{100'000};
    std::size_t maximum_bga_events{100'000};
    std::size_t maximum_layout_segments{4'096};
    std::size_t maximum_lane_definitions{100'000};
};

struct editor_field_scene_bgm_source_t {
    [[nodiscard]] auto sort_key() const { return std::tie(row, source_lane.value, event.value); }
    object_id_t event;
    object_id_t source_lane;
    std::uint64_t row{};
    std::uint64_t end_row{};
    std::optional<resource_id_t> sound;
    std::optional<bool> bmson_restart;
    std::optional<color_rgba_t> semantic_color;
    std::vector<layer_id_t> semantic_layers;
};

struct editor_field_scene_bga_source_t {
    object_id_t event;
    resource_id_t resource;
    visual_event_kind_t kind{visual_event_kind_t::background};
    std::uint64_t row{};
};

struct editor_field_scene_note_source_t {
    [[nodiscard]] auto sort_key() const { return std::tie(begin_row, end_row, note.value); }
    object_id_t note;
    lane_id_t lane;
    std::optional<resource_id_t> sound;
    note_kind_t kind{note_kind_t::tap};
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    // The first visible matching layer in chart palette order. Keeping the
    // resolved color in the revision-bound scene index makes the renderer
    // independent of document traversal while preserving a deterministic
    // precedence rule until multi-layer blend authoring is specified.
    std::optional<color_rgba_t> semantic_color;
    // Stable memberships are retained in the index so view-only layer filters
    // do not need document traversal on each composition frame.
    std::vector<layer_id_t> semantic_layers;
};

struct editor_field_scene_layout_source_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    lane_layout_t layout;
};

// Immutable, revision-bound data built away from the render loop. Notes are
// sorted by their head row, while the max-end tree narrows holds that begin
// before the visible interval without scanning every earlier note.
struct editor_field_scene_index_t {
    [[nodiscard]] static auto from(const chart_document_t& chart, std::uint64_t document_revision,
        editor_field_scene_index_options_t options = {})
        -> result_t<editor_field_scene_index_t, diagnostic_list_t>;
    chart_id_t chart;
    std::uint64_t document_revision{};
    measure_index_t measures;
    std::vector<editor_field_scene_note_source_t> notes;
    // BGM events use a separate source-lane stack, but share this exact
    // revision-bound row index with the playable field.
    std::vector<editor_field_scene_bgm_source_t> bgm_events;
    std::size_t bgm_tree_leaf_base{1};
    std::vector<std::uint64_t> bgm_tree_max_end;
    std::vector<editor_field_scene_bga_source_t> bga_events;
    std::unordered_map<std::uint64_t, std::size_t> note_indices_by_id;
    std::size_t note_tree_leaf_base{1};
    std::vector<std::uint64_t> note_tree_max_end;
    std::vector<editor_field_scene_layout_source_t> layouts;
};

using editor_field_rect_t = rect_t;

struct editor_field_layout_band_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    double top_pixels{};
    double bottom_pixels{};
    std::string profile_id;
    bool split_stage{};
};

struct editor_field_lane_geometry_t {
    std::size_t layout_band{};
    lane_id_t lane;
    lane_role_t role{lane_role_t::playable};
    stage_group_t stage{stage_group_t::none};
    lane_glyph_t glyph{lane_glyph_t::bar};
    std::uint32_t display_order{};
    lane_key_color_t key_color{lane_key_color_t::generic};
    editor_field_rect_t bounds;
};

struct editor_field_grid_geometry_t {
    double y_pixels{};
    editor_grid_line_kind_t kind{editor_grid_line_kind_t::active_snap};
    editor_grid_emphasis_t emphasis{editor_grid_emphasis_t::subtle};
    exact_chart_row_t chart_row;
};

struct editor_field_comparison_difference_geometry_t {
    local_position_t position;
    editor_field_rect_t bounds;
};

struct editor_field_note_endpoint_geometry_t {
    object_id_t note;
    lane_id_t lane;
    std::optional<resource_id_t> sound;
    lane_glyph_t glyph{lane_glyph_t::bar};
    note_kind_t kind{note_kind_t::tap};
    bool tail{};
    bool selected{};
    bool proposal_origin{};
    bool proposal_ghost{};
    std::optional<color_rgba_t> semantic_color;
    editor_field_rect_t bounds;
    bool paint_preview{};
    bool paint_erase_preview{};
    bool paint_invalid_preview{};
    note_move_conflict_t move_conflict{note_move_conflict_t::none};
    bool move_conflict_occupant{};
};

struct editor_field_hold_body_geometry_t {
    object_id_t note;
    lane_id_t lane;
    bool selected{};
    bool proposal_origin{};
    bool proposal_ghost{};
    std::optional<color_rgba_t> semantic_color;
    editor_field_rect_t bounds;
    note_move_conflict_t move_conflict{note_move_conflict_t::none};
    bool move_conflict_occupant{};
};

struct editor_field_cursor_geometry_t {
    lane_id_t lane;
    editor_field_rect_t bounds;
};

struct editor_field_bgm_lane_geometry_t {
    object_id_t source_lane;
    std::size_t source_order{};
    editor_field_rect_t bounds;
    double unclipped_left_pixels{}, unclipped_right_pixels{};
};

struct editor_field_bgm_event_geometry_t {
    object_id_t event;
    object_id_t source_lane;
    std::optional<resource_id_t> sound;
    std::optional<bool> bmson_restart;
    bool selected{};
    editor_field_rect_t bounds;
    editor_field_rect_t head_bounds;
    bool has_tail{};
    bool proposal_ghost{};
    bool move_conflict{};
    std::optional<color_rgba_t> semantic_color;
};

struct editor_field_bga_lane_geometry_t {
    editor_field_rect_t bounds;
    visual_event_kind_t kind{visual_event_kind_t::background};
};

struct editor_field_bga_event_geometry_t {
    object_id_t event;
    resource_id_t resource;
    visual_event_kind_t kind{visual_event_kind_t::background};
    std::uint64_t row{};
    std::size_t overlap_count{1};
    bool selected{};
    editor_field_rect_t bounds;
};

struct editor_field_scene_t {
    [[nodiscard]] auto primitive_count() const noexcept -> std::size_t;
    std::uint64_t visible_begin_row{};
    std::uint64_t visible_end_row{};
    std::vector<editor_field_layout_band_t> layout_bands;
    std::vector<editor_field_lane_geometry_t> lanes;
    // Frontends render these behind the field with the comparison-difference
    // color (yellow in the default skin).
    std::vector<editor_field_comparison_difference_geometry_t> comparison_differences;
    std::vector<editor_field_grid_geometry_t> grid_lines;
    std::vector<editor_field_hold_body_geometry_t> hold_bodies;
    std::vector<editor_field_note_endpoint_geometry_t> note_endpoints;
    std::vector<editor_field_bgm_lane_geometry_t> bgm_lanes;
    std::vector<editor_field_bgm_event_geometry_t> bgm_events;
    std::optional<editor_field_bga_lane_geometry_t> bga_lane;
    std::vector<editor_field_bga_lane_geometry_t> bga_lanes;
    std::vector<editor_field_bga_event_geometry_t> bga_events;
    std::optional<editor_field_cursor_geometry_t> cursor;
};

struct editor_field_scene_request_t {
    double field_left_pixels{};
    double field_width_pixels{};
    editor_scratch_presentation_t scratch_on_right;
    double split_stage_gutter_pixels{12.0};
    double note_head_height_pixels{10.0};
    // Up scroll projects later rows toward the top of the viewport. Rectangular
    // note heads begin at the snap line in up scroll and end there in down scroll.
    editor_field_scroll_direction_t scroll_direction{editor_field_scroll_direction_t::up};
    // Draw the edit cursor at a stationary screen line while the snapped
    // position under it changes with viewport scroll.
    std::optional<double> fixed_cursor_screen_y_pixels;
    double comparison_difference_height_pixels{10.0};
    double hold_body_width_ratio{0.35};
    // Optional adjacent BGM pane. A zero width leaves BGM geometry omitted.
    double bgm_pane_left_pixels{};
    double bgm_pane_width_pixels{};
    double bgm_lane_width_pixels{36.0};
    std::span<const double> bgm_lane_widths_pixels;
    double bgm_horizontal_scroll_pixels{};
    // Optional dedicated single BGA event lane adjacent to the field/BGM pane.
    double bga_pane_left_pixels{};
    double bga_pane_width_pixels{};
    editor_grid_density_t grid_density;
    bool include_virtual_bms_tail{};
    std::vector<editor_grid_grouping_t> authored_groupings;
    std::vector<local_position_t> comparison_difference_rows;
    // When set, compose only notes carrying this stable layer ID. This is a
    // view filter: it never changes document visibility or memberships.
    std::optional<layer_id_t> solo_semantic_layer;
    // General view-only semantic-layer filter. Required layers are conjunctive;
    // excluded layers remove matching notes. IDs must be distinct chart layers.
    std::vector<layer_id_t> required_semantic_layers;
    std::vector<layer_id_t> excluded_semantic_layers;
    std::size_t maximum_grid_lines{100'000};
    std::size_t maximum_authored_groupings{4'096};
    std::size_t maximum_comparison_difference_rows{100'000};
    std::size_t maximum_scene_primitives{250'000};
    std::size_t maximum_composition_operations{500'000};
    // Extra retained-source margin only; visible geometry is unchanged.
    // Zero preserves the original three-viewport cache policy.
    double retained_window_minimum_margin_pixels{};
};

struct editor_field_scene_window_t;

using editor_field_scene_outcome_t = diagnosed_t<std::optional<editor_field_scene_t>>;

struct editor_field_scene_result_t {
    [[nodiscard]] static auto from(const chart_document_t& chart,
        const editor_field_scene_index_t& index, const editor_interaction_view_model_t& view,
        const editor_field_scene_request_t& request = {}) -> editor_field_scene_outcome_t;
    [[nodiscard]] static auto from(editor_field_scene_outcome_t&& outcome) -> editor_field_scene_result_t;
    [[nodiscard]] auto into_outcome() && -> editor_field_scene_outcome_t;
    [[nodiscard]] static auto from(const chart_document_t& chart,
        const editor_field_scene_index_t& index, const editor_interaction_view_model_t& view,
        const editor_field_scene_request_t& request, editor_field_scene_window_t& window)
        -> editor_field_scene_outcome_t;
    std::optional<editor_field_scene_t> scene;
    diagnostic_list_t diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
    [[nodiscard]] explicit operator bool() const noexcept {
        return scene.has_value() && !has_errors();
    }
};

[[nodiscard]] auto build_editor_field_scene_index(const chart_document_t& chart,
                                                  std::uint64_t document_revision,
                                                  editor_field_scene_index_options_t options = {})
    -> std::variant<editor_field_scene_index_t, diagnostic_list_t>;

// Composes backend-neutral screen geometry from one immutable index. The
// result is safe to pass to Nuklear, ImGui, Clay, SDL GPU, or a test renderer;
// it contains no backend-owned types or pointers into the document.
[[nodiscard]] auto make_editor_field_scene(const chart_document_t& chart,
                                           const editor_field_scene_index_t& index,
                                           const editor_interaction_view_model_t& view,
                                           const editor_field_scene_request_t& request = {})
    -> editor_field_scene_result_t;

// A revision-bound screen window. Transient edit previews use fresh composition.
// Counters distinguish actual window rebuilds from scroll-only projections.
struct editor_field_scene_window_t {
    std::optional<editor_field_scene_t> scene;
    editor_interaction_view_model_t view;
    editor_field_scene_request_t request;
    std::uint64_t canvas_rows{};
    std::size_t rebuild_count{};
    std::size_t reuse_count{};
    std::vector<double> bgm_lane_widths_snapshot;
};

[[nodiscard]] auto editor_bgm_lane_width_pixels(const chart_document_t& chart,
                                                const editor_field_scene_request_t& request,
                                                std::size_t lane_index) -> double;
[[nodiscard]] auto editor_bgm_content_width_pixels(const chart_document_t& chart,
                                                    const editor_field_scene_request_t& request)
    -> double;

// Read-only eligibility shares the exact retained-window policy with composition.
[[nodiscard]] auto can_reuse_editor_field_scene_window(const chart_document_t& chart,
                                                       const editor_field_scene_index_t& index,
                                                       const editor_interaction_view_model_t& view,
                                                       const editor_field_scene_request_t& request,
                                                       const editor_field_scene_window_t& window)
    -> bool;

struct editor_field_scene_window_metadata_t {
    editor_field_scene_result_t projection;
    double hold_clip_top{}, hold_clip_bottom{};
};
// Projects current layout/cursor and row/hold clipping bounds without copying
// retained notes/holds/BGM objects. Unusable windows return an empty projection;
// callers retain the existing full-composition fallback. This validates only
// metadata primitives: consumers MUST enforce maximum_scene_primitives again
// after extracting retained objects, using full composition on overflow.
[[nodiscard]] auto build_editor_field_scene_window_metadata_view(
    const chart_document_t& chart, const editor_field_scene_index_t& index,
    const editor_interaction_view_model_t& view, const editor_field_scene_request_t& request,
    const editor_field_scene_window_t& window) -> editor_field_scene_window_metadata_t;

[[nodiscard]] auto make_editor_field_scene_reusing_window(
    const chart_document_t& chart, const editor_field_scene_index_t& index,
    const editor_interaction_view_model_t& view, const editor_field_scene_request_t& request,
    editor_field_scene_window_t& window) -> editor_field_scene_result_t;

} // namespace wormedit
