#pragma once

#include <wormedit/core/editor_scene.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

enum class player_preview_skin_t : std::uint8_t {
    neutral,
    mania,
    arrows,
};

enum class player_preview_scroll_direction_t : std::uint8_t {
    toward_top,
    toward_bottom,
};

struct player_preview_index_options_t {
    std::size_t maximum_notes{100'000};
    // The first implementation reuses the editor-field source compiler. Keep
    // its otherwise-unused BGM traversal bounded until the indexes diverge.
    std::size_t maximum_bgm_events{100'000};
    std::size_t maximum_layout_segments{4'096};
    std::size_t maximum_lane_definitions{100'000};
};

struct player_preview_note_source_t {
    object_id_t note;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    double begin_displacement_beats{};
    double end_displacement_beats{};
    double minimum_displacement_beats{};
    double maximum_displacement_beats{};
    std::optional<color_rgba_t> semantic_color;
};

// Immutable chart/revision-bound source data. The interval tree is keyed by
// each note's minimum visual displacement and stores subtree maximum ends, so
// a frame only visits notes intersecting its visible displacement window.
struct player_preview_index_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    timing_index_t timing;
    scroll_speed_index_t scroll_speed;
    std::vector<editor_field_scene_layout_source_t> layouts;
    std::vector<player_preview_note_source_t> notes;
    std::size_t note_tree_leaf_base{1};
    std::vector<double> note_tree_max_end;
};

struct player_preview_index_result_t {
    std::optional<player_preview_index_t> index;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return index.has_value() && diagnostics.empty();
    }
};

struct player_preview_rect_t {
    double left{};
    double top{};
    double right{};
    double bottom{};

    auto operator==(const player_preview_rect_t&) const -> bool = default;
};

struct player_preview_lane_geometry_t {
    lane_id_t lane;
    lane_role_t role{lane_role_t::playable};
    stage_group_t stage{stage_group_t::none};
    lane_glyph_t glyph{lane_glyph_t::bar};
    std::uint32_t display_order{};
    player_preview_rect_t bounds;

    auto operator==(const player_preview_lane_geometry_t&) const -> bool = default;
};

struct player_preview_receptor_geometry_t {
    lane_id_t lane;
    lane_glyph_t glyph{lane_glyph_t::bar};
    player_preview_rect_t bounds;

    auto operator==(const player_preview_receptor_geometry_t&) const -> bool = default;
};

struct player_preview_note_geometry_t {
    object_id_t note;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    lane_glyph_t glyph{lane_glyph_t::bar};
    bool tail{};
    std::optional<color_rgba_t> semantic_color;
    player_preview_rect_t bounds;

    auto operator==(const player_preview_note_geometry_t&) const -> bool = default;
};

struct player_preview_hold_geometry_t {
    object_id_t note;
    lane_id_t lane;
    std::optional<color_rgba_t> semantic_color;
    player_preview_rect_t bounds;

    auto operator==(const player_preview_hold_geometry_t&) const -> bool = default;
};

struct player_preview_scene_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    std::uint64_t sample_frame{};
    signed_rational_t absolute_row;
    player_preview_skin_t skin{player_preview_skin_t::neutral};
    player_preview_scroll_direction_t direction{player_preview_scroll_direction_t::toward_top};
    bool pre_zero{};
    bool split_stage{};
    std::size_t composition_operations{};
    std::size_t visible_note_count{};
    std::vector<player_preview_lane_geometry_t> lanes;
    std::vector<player_preview_receptor_geometry_t> receptors;
    std::vector<player_preview_hold_geometry_t> hold_bodies;
    std::vector<player_preview_note_geometry_t> note_endpoints;

    auto operator==(const player_preview_scene_t&) const -> bool = default;
};

struct player_preview_scene_request_t {
    std::uint64_t sample_frame{};
    std::uint32_t sample_rate{44'100};
    double viewport_width_pixels{480.0};
    double viewport_height_pixels{720.0};
    double receptor_y_pixels{64.0};
    double pixels_per_beat{96.0};
    double split_stage_gutter_pixels{12.0};
    double note_head_height_pixels{14.0};
    double receptor_height_pixels{18.0};
    double hold_body_width_ratio{0.38};
    player_preview_skin_t skin{player_preview_skin_t::neutral};
    player_preview_scroll_direction_t direction{player_preview_scroll_direction_t::toward_top};
    std::size_t maximum_visible_notes{4'096};
    std::size_t maximum_composition_operations{100'000};
};

struct player_preview_scene_result_t {
    std::optional<player_preview_scene_t> scene;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return scene.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] auto build_player_preview_index(const chart_document_t& chart,
                                              std::uint64_t document_revision,
                                              player_preview_index_options_t options = {})
    -> player_preview_index_result_t;

// Composes one deterministic visual frame from the shared sample-frame clock.
// This contract deliberately contains no gameplay input, judgement, score,
// combo, life, replay, or failure state.
[[nodiscard]] auto make_player_preview_scene(const player_preview_index_t& index,
                                             const player_preview_scene_request_t& request = {})
    -> player_preview_scene_result_t;

} // namespace wormedit
