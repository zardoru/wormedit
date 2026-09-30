#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

struct worm_ui_nuklear_compose_key_event_t {
    std::uint32_t keycode{};
    std::uint32_t scancode{};
    std::uint16_t modifiers{};
    bool down{};
    bool repeat{};
};

struct worm_ui_nuklear_compose_wheel_event_t {
    double delta_x{};
    double delta_y{};
    double mouse_x{};
    double mouse_y{};
    std::uint16_t modifiers{};
    bool coarse_steps{};
};

struct worm_ui_nuklear_compose_color_t {
    float r{};
    float g{};
    float b{};
    float a{};
};

struct worm_ui_nuklear_lane_hold_t {
    std::uint32_t scancode{};
    std::uint64_t lane_id{};
};

enum class worm_ui_nuklear_compose_resource_filter_t : std::uint8_t {
    sample,
    bgm,
    bga,
};

enum class worm_ui_nuklear_compose_resource_pointer_mode_t : std::uint8_t {
    off,
    place,
    replace,
};

enum class worm_ui_nuklear_bmson_razor_mode_t : std::uint8_t {
    off,
    continuation,
    restart,
};

struct worm_ui_nuklear_compose_state_t {
    std::array<worm_ui_nuklear_compose_key_event_t, 64> key_events{};
    std::size_t key_event_count{};
    std::array<worm_ui_nuklear_compose_wheel_event_t, WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY>
        wheel_events{};
    std::size_t wheel_event_count{};
    bool middle_click_pending{};
    float middle_click_x{};
    float middle_click_y{};
    bool cancel_pending{};
    bool keyboard_focus{};
    bool action_overflowed{};
    bool pointer_hold_active{};
    bool note_paint_pointer_active{};
    bool bmson_razor_pointer_active{};
    double bmson_razor_last_y_pixels{};
    bool bmson_marker_move_candidate_active{};
    bool bmson_marker_move_active{};
    std::uint64_t bmson_marker_move_event_id{};
    double bmson_marker_move_anchor_x{};
    double bmson_marker_move_anchor_y{};
    float bmson_marker_move_anchor_screen_x{};
    float bmson_marker_move_anchor_screen_y{};
    bool semantic_layer_paint_pointer_active{};
    bool lane_hold_active{};
    bool space_range_hold_active{};
    bool space_range_hold_moved{};
    bool note_move_candidate_active{};
    bool note_move_active{};
    bool timing_stage_drag_active{};
    bool timing_marker_candidate_active{};
    bool timing_marker_drag_active{};
    bool timing_missing_initial_selected{};
    std::uint64_t timing_marker_object_id{};
    std::uint64_t timing_marker_chart_row{};
    WormUiTimingLaneKind timing_marker_lane{WORM_UI_TIMING_LANE_BPM};
    double timing_marker_anchor_y_pixels{};
    float timing_marker_anchor_screen_y{};
    double timing_marker_current_y_pixels{};
    std::uint64_t timing_marker_chart_id{};
    std::uint64_t timing_marker_revision{};
    bool inline_timing_open{};
    bool inline_timing_focus_pending{};
    std::uint64_t inline_timing_chart_id{};
    std::uint64_t inline_timing_revision{};
    std::uint64_t inline_timing_object_id{};
    WormUiTimingLaneKind inline_timing_lane{WORM_UI_TIMING_LANE_BPM};
    float inline_timing_anchor_screen_x{};
    float inline_timing_anchor_screen_y{};
    std::array<char, 64> inline_timing_text{};
    std::array<char, 128> inline_timing_error{};
    bool waveform_overview_drag_active{};
    bool waveform_beat_zero_drag_active{};
    bool beat_fit_drag_active{};
    bool offset_fit_drag_active{};
    std::uint64_t beat_fit_chart_row{};
    std::uint64_t beat_fit_chart_id{};
    std::uint64_t beat_fit_revision{};
    std::uint64_t beat_fit_asset_id{};
    std::uint32_t beat_fit_sample_rate{};
    float beat_fit_anchor_screen_y{};
    std::uint8_t beat_fit_snap_step_index{};
    bool beat_fit_ctrl_held{};
    float waveform_beat_zero_drag_anchor_y{};
    float waveform_beat_zero_drag_height{};
    std::uint64_t waveform_beat_zero_drag_total_frames{};
    std::uint64_t waveform_beat_zero_drag_chart_id{};
    std::uint64_t waveform_beat_zero_drag_revision{};
    std::uint64_t waveform_beat_zero_drag_asset_id{};
    std::uint32_t waveform_beat_zero_drag_sample_rate{};
    std::int64_t waveform_beat_zero_drag_delta_frames{};
    double note_move_anchor_x{};
    double note_move_anchor_y{};
    float note_move_anchor_screen_x{};
    float note_move_anchor_screen_y{};
    bool area_selection_candidate_active{};
    bool area_selection_active{};
    double area_selection_anchor_x{};
    double area_selection_anchor_y{};
    float area_selection_anchor_screen_x{};
    float area_selection_anchor_screen_y{};
    WormUiComposeSelectionMode area_selection_mode{WORM_UI_COMPOSE_SELECTION_REPLACE};
    std::array<worm_ui_nuklear_lane_hold_t, 64> lane_holds{};
    std::size_t lane_hold_count{};
    bool navigation_key_active{};
    std::uint32_t navigation_keycode{};
    WormUiComposeNavigationAction navigation_action{WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
    bool pointer_navigation_active{};
    WormUiComposeNavigationAction pointer_navigation_action{
        WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
    std::uint64_t last_chart_id{};
    std::uint64_t last_project_id{};
    std::uint64_t last_document_revision{};
    std::uint64_t label_project_id{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t label_chart_id{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t label_document_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t label_note_id{std::numeric_limits<std::uint64_t>::max()};
    std::array<char, WORM_UI_COMPOSE_NOTE_LABEL_BYTES + 1> note_label{};
    std::array<char, WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY> semantic_layer_name{};
    std::uint64_t semantic_layer_name_id{};
    std::uint64_t semantic_layer_name_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t semantic_layer_color_id{};
    worm_ui_nuklear_compose_color_t semantic_layer_color{};
    bool semantic_layer_color_editor_visible{};
    bool inspector_visible{true};
    int bmson_audition_index{1};
    worm_ui_nuklear_compose_resource_filter_t resource_filter{
        worm_ui_nuklear_compose_resource_filter_t::sample};
    std::array<char, 96> resource_search{};
    std::size_t export_group_document_index{};
    bool export_overwrite{};
    std::array<char, WORM_UI_COMPOSE_IMPORT_PATH_BYTES + 1> resource_import_path{};
    std::array<char, 256> resource_import_error{};
    std::uint64_t armed_resource_id{};
    std::uint64_t resource_selection_chart_id{};
    std::uint64_t resource_selection_object_id{};
    std::uint64_t resource_selection_assigned_id{};
    bool auto_increment_on_place{};
    worm_ui_nuklear_compose_resource_pointer_mode_t resource_pointer_mode{
        worm_ui_nuklear_compose_resource_pointer_mode_t::off};
    worm_ui_nuklear_bmson_razor_mode_t bmson_razor_mode{
        worm_ui_nuklear_bmson_razor_mode_t::off};
};

[[nodiscard]] auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t&;
void worm_ui_nuklear_advance_resource_picker(std::uint64_t placed_resource_id,
                                              std::uint64_t next_resource_id);

void worm_ui_nuklear_reset_compose_state();

void worm_ui_nuklear_inline_timing_initial_text(const WormUiTimingView& view, char* destination,
                                                std::size_t capacity);
[[nodiscard]] auto worm_ui_nuklear_inline_timing_confirm(const WormUiTimingView& view,
                                                         const char* text,
                                                         WormUiWorkspaceAction& action) -> bool;

struct worm_ui_nuklear_compose_file_dialog_result_t {
    std::string path;
    std::string error;
    bool canceled{};
};

[[nodiscard]] auto worm_ui_nuklear_compose_file_dialog_begin()
    -> std::optional<std::uint64_t>;
void worm_ui_nuklear_compose_file_dialog_complete(
    std::uint64_t generation, std::string path, std::string error, bool canceled);
[[nodiscard]] auto worm_ui_nuklear_compose_file_dialog_take()
    -> std::optional<worm_ui_nuklear_compose_file_dialog_result_t>;
[[nodiscard]] auto worm_ui_nuklear_compose_file_dialog_open() -> bool;
