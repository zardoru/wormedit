#pragma once

#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wormedit {

struct editor_field_scene_index_options_t {
    std::size_t maximum_notes{100'000};
    std::size_t maximum_bgm_events{100'000};
    std::size_t maximum_layout_segments{4'096};
    std::size_t maximum_lane_definitions{100'000};
};

struct editor_field_scene_bgm_source_t {
    object_id_t event;
    object_id_t source_lane;
    std::uint64_t row{};
    std::optional<resource_id_t> sound;
    std::optional<bool> bmson_restart;
};

struct editor_field_scene_note_source_t {
    object_id_t note;
    lane_id_t lane;
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
    chart_id_t chart;
    std::uint64_t document_revision{};
    measure_index_t measures;
    std::vector<editor_field_scene_note_source_t> notes;
    // BGM events use a separate source-lane stack, but share this exact
    // revision-bound row index with the playable field.
    std::vector<editor_field_scene_bgm_source_t> bgm_events;
    std::unordered_map<std::uint64_t, std::size_t> note_indices_by_id;
    std::size_t note_tree_leaf_base{1};
    std::vector<std::uint64_t> note_tree_max_end;
    std::vector<editor_field_scene_layout_source_t> layouts;
};

struct editor_field_rect_t {
    double left{};
    double top{};
    double right{};
    double bottom{};

    auto operator==(const editor_field_rect_t&) const -> bool = default;
};

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
};

struct editor_field_hold_body_geometry_t {
    object_id_t note;
    lane_id_t lane;
    bool selected{};
    bool proposal_origin{};
    bool proposal_ghost{};
    std::optional<color_rgba_t> semantic_color;
    editor_field_rect_t bounds;
};

struct editor_field_cursor_geometry_t {
    lane_id_t lane;
    editor_field_rect_t bounds;
};

struct editor_field_bgm_lane_geometry_t {
    object_id_t source_lane;
    std::size_t source_order{};
    editor_field_rect_t bounds;
};

struct editor_field_bgm_event_geometry_t {
    object_id_t event;
    object_id_t source_lane;
    std::optional<resource_id_t> sound;
    std::optional<bool> bmson_restart;
    bool selected{};
    editor_field_rect_t bounds;
};

struct editor_field_scene_t {
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
    std::optional<editor_field_cursor_geometry_t> cursor;
};

struct editor_field_scene_request_t {
    double field_left_pixels{};
    double field_width_pixels{};
    double split_stage_gutter_pixels{12.0};
    double note_head_height_pixels{10.0};
    double comparison_difference_height_pixels{10.0};
    double hold_body_width_ratio{0.35};
    // Optional adjacent BGM pane. A zero width leaves BGM geometry omitted.
    double bgm_pane_left_pixels{};
    double bgm_pane_width_pixels{};
    double bgm_lane_width_pixels{36.0};
    double bgm_horizontal_scroll_pixels{};
    editor_grid_density_t grid_density;
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
};

struct editor_field_scene_result_t {
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

} // namespace wormedit
