#pragma once
#include "compose.h"
#include <wormedit/core/geometry.h>
#include <wormedit/core/visual_layout.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
enum class timing_stage_action_t { none, begin, update, move, confirm, cancel, remove, quick_insert };

// Real composition work bound: one entry for each supported visual layer kind.
inline constexpr std::size_t player_preview_visual_layer_limit = 6;
// Bounds on actual scene evaluation and rendering work, independent of view storage.
inline constexpr std::size_t maximum_preview_notes = 4096;
inline constexpr std::size_t maximum_preview_storyboard_instances = 27;
enum class player_preview_visual_diagnostic_t { none, video_frame_late, video_decode_failed };
enum class external_player_action_kind_t { none, select, save, add, remove, close, open, play_start, play_here, stop };

struct timing_speed_view_t {
    wormedit::object_id_t object_id{};
    uint64_t source_order{};
    uint64_t storage_order{};
    double marker_y_pixels{};
    double marker_left_fraction{};
    double marker_right_fraction{};
    double affected_begin_y_pixels{};
    double affected_end_y_pixels{};
    double multiplier{};
    double transition_duration{};
    bool transition_is_seconds{};
    bool transition_clipped{};
    auto operator==(const timing_speed_view_t&) const -> bool = default;
};

struct timing_grid_line_view_t {
    double y_pixels{};
    wormedit::editor_grid_line_kind_t kind{};
    wormedit::editor_grid_emphasis_t emphasis{};
    auto operator==(const timing_grid_line_view_t&) const -> bool = default;
};

struct timing_view_t {
    bool input_occluded{};
    bool visible{};
    bool scene_available{};
    double viewport_width_pixels{};
    double viewport_height_pixels{};
    wormedit::chart_id_t chart_id{};
    uint64_t document_revision{};
    uint64_t total_rows{};
    uint64_t active_snap_denominator{};
    std::vector<uint64_t> supported_snap_denominators{};
    std::string status_text{};
    size_t measure_count{};
    size_t bpm_count{};
    bool initial_bpm_editable{};
    bool initial_bpm_present{};
    wormedit::object_id_t initial_bpm_event_id{};
    double initial_bpm_value{};
    size_t pause_count{};
    size_t warp_count{};
    size_t bookmark_count{};
    size_t section_count{};
    size_t anchor_count{};
    size_t speed_total_count{};
    std::vector<timing_speed_view_t> speeds{};
    std::vector<wormedit::ui::compose_timing_marker_view_t> markers{};
    std::vector<timing_grid_line_view_t> grid_lines{};
    bool stage_active{};
    bool stage_valid{};
    wormedit::object_id_t stage_existing_object_id{};
    wormedit::timing_lane_kind_t stage_lane{};
    double stage_marker_y_pixels{};
    double stage_value{};
    double stage_secondary_value{};
    bool stage_transition_is_seconds{};
    bool stage_toggle_primary{};
    wormedit::pause_unit_t stage_pause_unit{};
    std::string stage_primary_decimal_text{};
    bool stage_primary_decimal_exact{};
    std::string stage_primary_numerator_text{};
    std::string stage_primary_denominator_text{};
    std::string stage_secondary_numerator_text{};
    std::string stage_secondary_denominator_text{};
    std::string stage_status_text{};
    void reset() {
        input_occluded = {};
        visible = {};
        scene_available = {};
        viewport_width_pixels = {};
        viewport_height_pixels = {};
        chart_id = {};
        document_revision = {};
        total_rows = {};
        active_snap_denominator = {};
        supported_snap_denominators.clear();
        status_text.clear();
        measure_count = {};
        bpm_count = {};
        initial_bpm_editable = {};
        initial_bpm_present = {};
        initial_bpm_event_id = {};
        initial_bpm_value = {};
        pause_count = {};
        warp_count = {};
        bookmark_count = {};
        section_count = {};
        anchor_count = {};
        speed_total_count = {};
        speeds.clear();
        markers.clear();
        grid_lines.clear();
        stage_active = {};
        stage_valid = {};
        stage_existing_object_id = {};
        stage_lane = {};
        stage_marker_y_pixels = {};
        stage_value = {};
        stage_secondary_value = {};
        stage_transition_is_seconds = {};
        stage_toggle_primary = {};
        stage_pause_unit = {};
        stage_primary_decimal_text.clear();
        stage_primary_decimal_exact = {};
        stage_primary_numerator_text.clear();
        stage_primary_denominator_text.clear();
        stage_secondary_numerator_text.clear();
        stage_secondary_denominator_text.clear();
        stage_status_text.clear();
    }
};

struct player_preview_lane_view_t {
    wormedit::lane_id_t lane_id{};
    wormedit::lane_role_t role{};
    wormedit::lane_glyph_t glyph{};
    uint32_t display_order{};
    wormedit::rect_t bounds{};
    auto operator==(const player_preview_lane_view_t&) const -> bool = default;
};

struct player_preview_receptor_view_t {
    wormedit::lane_id_t lane_id{};
    wormedit::lane_glyph_t glyph{};
    wormedit::rect_t bounds{};
    auto operator==(const player_preview_receptor_view_t&) const -> bool = default;
};

struct player_preview_note_view_t {
    wormedit::object_id_t note_id{};
    wormedit::lane_id_t lane_id{};
    wormedit::note_kind_t kind{};
    wormedit::lane_glyph_t glyph{};
    bool tail{};
    bool semantic_color_visible{};
    uint8_t semantic_red{};
    uint8_t semantic_green{};
    uint8_t semantic_blue{};
    uint8_t semantic_alpha{};
    wormedit::rect_t bounds{};
    auto operator==(const player_preview_note_view_t&) const -> bool = default;
};

struct player_preview_hold_view_t {
    wormedit::object_id_t note_id{};
    wormedit::lane_id_t lane_id{};
    bool semantic_color_visible{};
    uint8_t semantic_red{};
    uint8_t semantic_green{};
    uint8_t semantic_blue{};
    uint8_t semantic_alpha{};
    wormedit::rect_t bounds{};
    auto operator==(const player_preview_hold_view_t&) const -> bool = default;
};

struct player_preview_visual_layer_view_t {
    wormedit::object_id_t object_id{};
    wormedit::resource_id_t resource_id{};
    uint64_t event_sample_frame{};
    wormedit::visual_event_kind_t kind{};
    size_t draw_order{};
    size_t source_order{};
    bool video_clock_ready{};
    uint64_t video_elapsed_sample_frames{};
    uint64_t video_time_numerator{};
    uint32_t video_time_denominator{};
    bool static_image{};
    bool video_frame{};
    bool image_ready{};
    bool image_pending{};
    wormedit::ui::player_preview_visual_diagnostic_t diagnostic{};
    std::string_view diagnostic_text{};
    uint64_t image_content_id{};
    uint32_t image_width{};
    uint32_t image_height{};
    std::span<const uint8_t> rgba_pixels{};
};

struct player_preview_storyboard_sprite_view_t {
    uint64_t stable_id{};
    wormedit::resource_id_t resource_id{};
    size_t source_order{};
    uint8_t layer_order{};
    double x{};
    double y{};
    double origin_x{};
    double origin_y{};
    double scale_x{};
    double scale_y{};
    double rotation_radians{};
    double opacity{};
    double red{};
    double green{};
    double blue{};
    bool image_ready{};
    bool image_pending{};
    uint64_t image_content_id{};
    uint32_t image_width{};
    uint32_t image_height{};
    std::span<const uint8_t> rgba_pixels{};
};

struct player_preview_view_t {
    bool visible{};
    bool scene_available{};
    bool pre_zero{};
    bool split_stage{};
    wormedit::player_preview_skin_t skin{};
    wormedit::visual_fit_policy_t visual_fit{};
    wormedit::song_id_t project_id{};
    wormedit::chart_id_t chart_id{};
    uint64_t document_revision{};
    uint64_t sample_frame{};
    double viewport_width_pixels{};
    double viewport_height_pixels{};
    std::string status_text{};
    std::vector<player_preview_lane_view_t> lanes{};
    std::vector<player_preview_receptor_view_t> receptors{};
    std::vector<player_preview_hold_view_t> hold_bodies{};
    std::vector<player_preview_note_view_t> note_endpoints{};
    std::vector<player_preview_visual_layer_view_t> visual_layers{};
    std::vector<player_preview_storyboard_sprite_view_t> storyboard_sprites{};
    void reset() {
        visible = {};
        scene_available = {};
        pre_zero = {};
        split_stage = {};
        skin = {};
        visual_fit = {};
        project_id = {};
        chart_id = {};
        document_revision = {};
        sample_frame = {};
        viewport_width_pixels = {};
        viewport_height_pixels = {};
        status_text.clear();
        lanes.clear();
        receptors.clear();
        hold_bodies.clear();
        note_endpoints.clear();
        visual_layers.clear();
        storyboard_sprites.clear();
    }
};

struct external_player_definition_view_t {
    std::string_view name{};
    std::string_view path{};
    std::string_view start_arguments{};
    std::string_view here_arguments{};
    std::string_view stop_arguments{};
    auto operator==(const external_player_definition_view_t&) const -> bool = default;
};

struct external_player_view_t {
    bool visible{};
    uint64_t revision{};
    std::vector<external_player_definition_view_t> players{};
    size_t selected{};
    void reset() {
        visible = {};
        revision = {};
        players.clear();
        selected = {};
    }
};

struct external_player_action_t {
    wormedit::ui::external_player_action_kind_t kind{};
    size_t selected{};
    std::string name{};
    std::string path{};
    std::string start_arguments{};
    std::string here_arguments{};
    std::string stop_arguments{};
    auto operator==(const external_player_action_t&) const -> bool = default;
};

struct tap_timing_view_t {
    bool visible{};
    bool playing{};
    bool source_available{};
    bool estimate_available{};
    bool stable{};
    bool downbeat_flash{};
    uint16_t beats_per_measure{};
    uint16_t active_segment{};
    uint64_t tap_count{};
    uint64_t tap_capacity{};
    double bpm{};
    double ms_per_beat{};
    double offset_ms{};
    double deviation_ms{};
    double confidence{};
    uint8_t bpm_choice{};
    bool apply_available{};
    uint64_t apply_revision{};
    double proposed_bpm{};
    double proposed_offset_ms{};
    std::string_view status{};
    bool tap_flash{};
    uint16_t tap_segment{};
    bool timing_locked{};
    bool music_available{};
    bool input_blocked{};
    bool close_key_pressed{};
    auto operator==(const tap_timing_view_t&) const -> bool = default;
};

struct tap_timing_action_t {
    bool toggle{};
    bool close{};
    bool reset{};
    bool choose_bpm{};
    uint8_t bpm_choice{};
    bool apply{};
    uint64_t apply_revision{};
    auto operator==(const tap_timing_action_t&) const -> bool = default;
};

struct timing_settings_action_t {
    bool toggle{};
    bool close{};
    bool change_enable{};
    bool enabled{};
    bool change_volume{};
    uint8_t volume{};
    bool change_count_in{};
    uint8_t count_in{};
    auto operator==(const timing_settings_action_t&) const -> bool = default;
};

} // namespace wormedit::ui
