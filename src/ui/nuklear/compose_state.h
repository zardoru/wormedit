#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/keysound_list_state.h"
#include "ui/nuklear/compose_bookmark_chooser.h"
#include "ui/nuklear/compose_bookmark_rename.h"
#include "ui/nuklear/sound_slot_batch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Historical panel focus does not decide canvas ownership. Actual entry/occlusion does.
auto worm_ui_nuklear_compose_canvas_input_available(const WormUiComposeView* view,
    bool window_focused, bool text_owned, bool ime_owned) -> bool;

enum class worm_ui_nuklear_overview_event_t { none, scroll, click };
struct worm_ui_nuklear_overview_gesture_t {
    bool active{};
    bool dragged{};
    double anchor{};
};
auto worm_ui_nuklear_overview_gesture(worm_ui_nuklear_overview_gesture_t& gesture,
                                     double x, bool pressed, bool down, bool valid)
    -> worm_ui_nuklear_overview_event_t;

enum class worm_ui_nuklear_pinch_phase_t { begin, update, end, cancel };
struct worm_ui_nuklear_pinch_update_t {
    double factor{};
    double mouse_x{};
    double mouse_y{};
};
struct worm_ui_nuklear_pinch_gesture_t {
    bool active{};
    double mouse_x{};
    double mouse_y{};
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::array<worm_ui_nuklear_pinch_update_t,
        WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY> pending{};
    std::size_t count{};
};
void worm_ui_nuklear_pinch_input(worm_ui_nuklear_pinch_gesture_t& gesture,
    worm_ui_nuklear_pinch_phase_t phase, double factor, double mouse_x,
    double mouse_y, std::uint64_t project_id, std::uint64_t chart_id);

struct worm_ui_nuklear_overview_capture_t {
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    std::uint64_t asset_id{};
    std::uint64_t total_frames{};
    std::uint32_t sample_rate{};
    double left{};
    double width{};
    double window_begin{};
    double window_end{1.0};
    bool window_capture{};
    std::uint64_t project_id{};
};

struct worm_ui_nuklear_overview_window_t {
    double begin{};
    double end{1.0};
};

auto worm_ui_nuklear_overview_window_for_capture(
    const worm_ui_nuklear_overview_capture_t& capture, bool active,
    double projected_begin, double projected_end) -> worm_ui_nuklear_overview_window_t;

auto worm_ui_nuklear_overview_fraction(double x, double left, double width)
    -> std::optional<double>;
auto worm_ui_nuklear_overview_frame_delta(double x, double anchor, double width,
                                         std::uint64_t total_frames) -> std::int64_t;
auto worm_ui_nuklear_overview_capture_matches(
    const worm_ui_nuklear_overview_capture_t& capture, const WormUiComposeView& view,
    double left, double width, bool available, bool window_focused) -> bool;

// A canvas-owned Tab chord has its own cadence; ordinary navigation is unchanged.
struct worm_ui_nuklear_tab_navigation_t {
    bool tab_down{};
    std::uint32_t direction_scancode{};
    std::optional<WormUiComposeNavigationAction> direction{};
    std::uint64_t next_repeat_ms{};

    void schedule(std::uint64_t now) noexcept {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        next_repeat_ms = now > maximum - 200 ? maximum : now + 200;
    }
    auto press_tab(std::uint64_t now) noexcept
        -> std::optional<WormUiComposeNavigationAction> {
        if (tab_down) return std::nullopt;
        tab_down = true;
        if (direction) schedule(now);
        return direction;
    }
    auto press_direction(WormUiComposeNavigationAction action,
                         std::uint32_t scancode, std::uint64_t now) noexcept
        -> std::optional<WormUiComposeNavigationAction> {
        if (direction && direction_scancode == scancode) return std::nullopt;
        direction = action;
        direction_scancode = scancode;
        if (!tab_down) return std::nullopt;
        schedule(now);
        return direction;
    }
    void release_direction(std::uint32_t scancode) noexcept {
        if (scancode != direction_scancode) return;
        direction.reset();
        direction_scancode = 0;
        next_repeat_ms = 0;
    }
    void cancel() noexcept { *this = {}; }
    auto advance(std::uint64_t now, bool canvas_available) noexcept
        -> std::optional<WormUiComposeNavigationAction> {
        if (!canvas_available) { cancel(); return std::nullopt; }
        if (!tab_down || !direction || now < next_repeat_ms ||
            next_repeat_ms == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        // Delayed frames emit once and discard debt, never replay a burst of moves.
        schedule(now);
        return direction;
    }
};

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
    bool tab_speed_modifier{};
};

// SDL wheel x/y are fractional wheel units, not native pixel coordinates.
// Preserve their magnitude and normalize the flipped-device convention once.
auto worm_ui_nuklear_normalize_compose_wheel(double x, double y, double mouse_x,
    double mouse_y, std::uint16_t modifiers, bool flipped)
    -> std::optional<worm_ui_nuklear_compose_wheel_event_t>;

inline auto worm_ui_nuklear_compose_wheel_speed(double delta, bool tab_modifier) noexcept
    -> double { return delta * (tab_modifier ? 2.0 : 1.0); }

inline auto worm_ui_nuklear_compose_zoom_anchor(double visual_y, double height,
                                               bool downscroll) noexcept -> double {
    return downscroll ? height - visual_y : visual_y;
}

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

struct nk_color;
enum class worm_ui_nuklear_compose_color_view_t { normal, snap, layer };

// Transient arrows retain their snap artwork without a semantic layer ring.
auto compose_note_is_transient(const WormUiComposeView& view,
                               const WormUiComposeNoteView& note) -> bool;
auto compose_arrow_layer_outline_visible(worm_ui_nuklear_compose_color_view_t color_view,
    const WormUiComposeView& view, const WormUiComposeNoteView& note) -> bool;

// Pure base fills; kind, hover and preview overrides are applied by the canvas.
auto compose_note_base_fill(worm_ui_nuklear_compose_color_view_t color_view,
                            const WormUiComposeNoteView& note) -> nk_color;
auto compose_note_base_fill(worm_ui_nuklear_compose_color_view_t color_view,
                            const WormUiComposeHoldView& hold) -> nk_color;

struct worm_ui_nuklear_compose_state_t {
    worm_ui_nuklear_compose_color_view_t color_view{worm_ui_nuklear_compose_color_view_t::normal};
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
    bool space_range_latched{};
    std::uint8_t shift_range_keys{};
    std::optional<std::uint64_t> alt_wheel_last_event_ms;
    bool note_move_candidate_active{};
    bool note_move_select_on_release{};
    bool note_move_active{};
    bool timing_stage_drag_active{};
    // Kiai area edge or creation drag on the canvas; y values are viewport pixels.
    bool kiai_drag_active{};
    // The drag grabbed a Speed span edge (kiai_drag_head_id is the Speed event) rather than Kiai.
    bool kiai_drag_speed{};
    bool kiai_drag_moved{};
    std::uint8_t kiai_drag_edge{};
    std::uint64_t kiai_drag_head_id{};
    double kiai_drag_anchor_y{};
    double kiai_drag_current_y{};
    // Edit buffers of the selected SV recipe's form (see compose_recipe_form.cpp).
    WormUiTimingRecipeForm recipe_edit_form{};
    std::uint64_t recipe_edit_id{};
    std::uint64_t recipe_edit_revision{};
    bool recipe_edit_dirty{};
    bool recipe_edit_apply_pending{};
    // Typed time signature for the cursor measure (Measure at cursor section).
    std::array<char, 32> meter_numerator{'4'};
    std::array<char, 32> meter_denominator{'4'};
    // SV recipe edge or whole-span drag; edge 0 is the begin edge, 1 the end edge, 2 the body.
    bool recipe_drag_active{};
    bool recipe_drag_moved{};
    std::uint8_t recipe_drag_edge{};
    std::uint64_t recipe_drag_id{};
    double recipe_drag_anchor_y{};
    double recipe_drag_current_y{};
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
    bool inline_offset_open{};
    bool inline_offset_focus_pending{};
    bool inline_offset_cancel_pending{};
    bool inline_offset_input_consumed{};
    std::uint64_t inline_offset_chart_id{};
    std::uint64_t inline_offset_revision{};
    std::array<char, 64> inline_offset_text{};
    std::array<char, 128> inline_offset_error{};
    struct bgm_column_count_edit_t {
        std::uint64_t chart_id{};
        std::uint64_t revision{};
        std::array<char, 4> text{};
        bool initialized{};
        bool commit_pending{};
        bool input_owned{};
        bool release_pending{};
    } bgm_column_count_edit;
    bool inline_timing_open{};
    bool inline_timing_stage_seen{};
    std::uint64_t inline_timing_seen_chart_id{};
    std::uint64_t inline_timing_seen_revision{};
    std::uint64_t inline_timing_seen_object_id{};
    WormUiTimingLaneKind inline_timing_seen_lane{WORM_UI_TIMING_LANE_BPM};
    double inline_timing_seen_stage_y{};
    bool inline_timing_focus_pending{};
    bool inline_timing_popup_close_pending{};
    bool inline_timing_input_consumed{};
    std::uint64_t inline_timing_chart_id{};
    std::uint64_t inline_timing_revision{};
    std::uint64_t inline_timing_object_id{};
    WormUiTimingLaneKind inline_timing_lane{WORM_UI_TIMING_LANE_BPM};
    float inline_timing_anchor_screen_x{};
    float inline_timing_anchor_screen_y{};
    std::array<char, 64> inline_timing_text{};
    std::array<char, 128> inline_timing_error{};
    bookmark_chooser_state_t bookmark_chooser;
    bookmark_rename_editor_t bookmark_rename;
    bool go_to_measure_open{};
    std::uint64_t resource_export_group_project_id{};
    std::uint64_t resource_export_group_chart_id{};
    std::uint64_t resource_export_group_revision{};
    bool resource_export_group_popup_open{};
    bool resource_export_group_release_pending{};
    bool go_to_measure_cancel_pending{};
    bool go_to_measure_focus_pending{};
    std::uint64_t go_to_measure_chart_id{};
    std::uint64_t go_to_measure_revision{};
    std::array<char, 32> go_to_measure_text{};
    std::array<char, 96> go_to_measure_error{};
    bool waveform_overview_drag_active{};
    worm_ui_nuklear_overview_gesture_t waveform_overview_gesture{};
    worm_ui_nuklear_overview_capture_t waveform_overview_capture{};
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
    float waveform_beat_zero_drag_anchor_x{};
    float waveform_beat_zero_drag_width{};
    float waveform_beat_zero_drag_left{};
    double waveform_beat_zero_drag_span{1.0};
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
    worm_ui_nuklear_tab_navigation_t tab_navigation{};
    bool raw_tab_down{};
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
    std::array<char, 129> label_search{};
    std::array<char, WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY> semantic_layer_name{};
    std::uint64_t semantic_layer_name_id{};
    std::uint64_t semantic_layer_name_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t semantic_layer_color_id{};
    worm_ui_nuklear_compose_color_t semantic_layer_color{};
    bool semantic_layer_color_editor_visible{};
    bool semantic_layer_name_editor_visible{};
    bool semantic_layer_name_focus_pending{};
    bool layers_menu_open{};
    bool layers_menu_input_consumed{};
    bool layers_menu_close_pending{};
    bool layers_menu_release_pending{};
    std::uint64_t layers_menu_project_id{};
    std::uint64_t layers_menu_chart_id{};
    std::uint64_t layers_menu_revision{};
    std::uint64_t layers_menu_layer_id{};
    WormUiComposeSemanticLayerView layers_menu_layer{};
    std::string layers_menu_layer_name;
    std::size_t layers_menu_row_index{};
    float layers_menu_anchor_x{}, layers_menu_anchor_y{};
    float layers_menu_x{}, layers_menu_y{};
    bool inspector_visible{true};
    // Keysound listbox (compose_sounds_panel.cpp).
    bool sounds_panel_visible{true};
    bool layers_panel_visible{};
    std::uint64_t layers_panel_chart_id{};
    std::uint64_t layers_panel_project_id{};
    bool sounds_layers_open{};
    bool sounds_preview_on_click{true};
    bool keysound_sort_by_id{};
    bool keysound_slots_mode{};
    std::uint64_t keysound_filter_project_id{};
    std::uint64_t keysound_filter_chart_id{};
    worm_ui_keysound_filter_state_t keysound_filters;
    std::unordered_map<std::uint64_t, worm_ui_keysound_filter_state_t>
        keysound_filter_states_by_project;
    std::size_t keysound_auto_first_row{};
    std::size_t keysound_auto_row_limit{64};
    std::size_t keysound_filter_layer_first{};
    bool keysound_filter_popup_open{};
    bool keysound_filter_group_popup_open{};
    bool keysound_layer_filter_popup_open{};
    bool keysound_filter_release_pending{};
    worm_ui_sound_slot_batch_t keysound_slot_batch;
    worm_ui_keysound_group_review_t keysound_group_review;
    bool keysound_group_menu_open{};
    bool keysound_group_menu_release_pending{};
    double keysound_group_menu_anchor_x{}, keysound_group_menu_anchor_y{};
    std::uint64_t keysound_group_menu_resource_id{};
    std::uint64_t keysound_group_menu_project_id{}, keysound_group_menu_chart_id{};
    std::uint64_t keysound_group_menu_revision{};
    bool bga_event_move_candidate_active{};
    bool bga_event_move_active{};
    std::uint64_t bga_event_move_object_id{};
    std::uint64_t bga_event_move_chart_id{};
    std::uint64_t bga_event_move_revision{};
    std::uint64_t bga_event_move_anchor_row{};
    double bga_event_move_anchor_y_pixels{};
    double bga_event_move_current_y_pixels{};
    float bga_event_move_anchor_screen_y{};
    bool bga_stack_open{};
    bool bga_stack_release_pending{};
    std::uint64_t bga_stack_project_id{};
    std::uint64_t bga_stack_chart_id{};
    std::uint64_t bga_stack_revision{};
    std::uint64_t bga_stack_row{};
    std::array<char, 64> keysound_slots_search{};
    std::size_t keysound_slots_first_row{};
    std::size_t keysound_slots_row_limit{64};
    std::uint16_t keysound_selected_slot{1};
    std::uint64_t keysound_selected_slot_resource{};
    bool keysound_selected_slot_resource_known{};
    std::uint64_t keysound_slots_project{}, keysound_slots_chart{}, keysound_slots_revision{};
    std::string keysound_slot_feedback;
    bool keysound_slot_confirmation_open{};
    std::uint64_t keysound_slot_confirmation_project{}, keysound_slot_confirmation_chart{};
    std::uint64_t keysound_slot_confirmation_revision{};
    std::uint64_t keysound_slot_confirmation_resource{};
    std::uint16_t keysound_slot_confirmation_number{};
    std::string keysound_slot_confirmation_path;
    bool keysound_slot_release_pending{};
    bool keysound_pending_file{};
    std::string keysound_pending_path;
    std::uint64_t keysound_pending_project{}, keysound_pending_chart{}, keysound_pending_revision{};
    std::uint64_t keysound_pending_occupied_resource{};
    std::uint16_t keysound_pending_slot{};
    std::uint64_t keysound_project_id{};
    bool keysound_renumber_confirmation_open{};
    bool keysound_renumber_release_pending{};
    std::uint64_t keysound_renumber_project{}, keysound_renumber_chart{};
    std::uint64_t keysound_renumber_revision{};
    std::vector<std::uint64_t> keysound_renumber_resources;
    bool bga_panel_visible{};
    worm_ui_keysound_selection_t bga_selection;
    std::array<char, 64> bga_search{};
    std::size_t bga_first_row{}, bga_row_limit{32};
    bool bga_sort_by_id{};
    bool bga_renumber_confirmation_open{};
    bool bga_renumber_release_pending{};
    std::uint64_t bga_renumber_project{}, bga_renumber_chart{};
    std::uint64_t bga_renumber_revision{};
    std::vector<std::uint64_t> bga_renumber_resources;
    std::uint64_t bga_project_id{}, bga_chart_id{};
    bool bga_auto_increment_on_place{};
    bool armed_from_bga_dock{};
    bool bga_reveal_primary{};
    std::vector<std::uint64_t> bga_advance_ids;
    std::vector<std::uint64_t> bga_staged_ids;
    std::vector<std::string> bga_staged_paths;
    std::vector<const char*> bga_staged_path_pointers;
    bool bga_list_valid{};
    float bga_list_x{}, bga_list_y{}, bga_list_width{}, bga_list_height{};
    worm_ui_keysound_selection_t keysound_selection;
    std::array<char, 64> keysound_search{};
    std::uint64_t keysound_chart_id{};
    std::uint64_t keysound_revision{std::numeric_limits<std::uint64_t>::max()};
    // Scratch reused across frames so a frame with thousands of rows does not allocate.
    std::vector<std::uint64_t> keysound_shown;
    std::vector<worm_ui_keysound_advance_row_t> keysound_advance_rows;
    bool armed_from_sounds_dock{};
    bool keysound_reveal_primary{};
    std::vector<std::uint64_t> keysound_all_ids;
    std::vector<worm_ui_keysound_mask_t> keysound_masks;
    // Frontend-owned staging that actions point at until their immediate dispatch.
    std::vector<std::uint64_t> keysound_staged_ids;
    bool sound_layer_confirmation_open{};
    std::uint64_t sound_layer_confirmation_project{}, sound_layer_confirmation_chart{};
    std::uint64_t sound_layer_confirmation_revision{}, sound_layer_confirmation_layer{};
    std::vector<std::uint64_t> sound_layer_confirmation_resources;
    std::vector<std::string> keysound_staged_paths;
    std::vector<const char*> keysound_staged_path_pointers;
    // Window rectangle of the list (for dropped files); valid only on frames that draw it.
    bool sound_list_valid{};
    float sound_list_x{};
    float sound_list_y{};
    float sound_list_width{};
    float sound_list_height{};
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
    worm_ui_nuklear_pinch_gesture_t pinch{};
};

// Claims Compose keyboard input for the BGM column number field as soon as it
// receives a click, including the frame before Nuklear reports it active.
auto worm_ui_nuklear_update_bgm_column_count_input_owner(
    worm_ui_nuklear_compose_state_t& state, std::uint64_t chart_id,
    std::uint64_t revision, bool editable, bool field_clicked, bool edit_active,
    bool edit_finished) -> bool;
void worm_ui_nuklear_retire_bgm_column_count_input_owner(
    worm_ui_nuklear_compose_state_t& state);

// Keep press/release pairs ordered, including pairs received within one SDL frame.
// When another owner takes input, discard new presses but deliver matching releases.
template <typename Consumer>
void worm_ui_nuklear_dispatch_compose_keys(worm_ui_nuklear_compose_state_t& state,
    bool accept_presses, Consumer&& consume) {
    for (size_t index = 0; index < state.key_event_count && !state.action_overflowed; ++index) {
        const auto& event = state.key_events[index];
        if (event.down && !accept_presses) continue;
        consume(event);
    }
    state.key_event_count = 0;
}

[[nodiscard]] auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t&;
// True when a window position lies over the keysound listbox drawn this frame.
[[nodiscard]] auto worm_ui_nuklear_sound_list_contains(float x, float y) -> bool;
void worm_ui_nuklear_advance_resource_picker(std::uint64_t placed_resource_id,
                                              std::uint64_t next_resource_id);

void worm_ui_nuklear_reset_compose_state();

[[nodiscard]] auto worm_ui_nuklear_begin_inline_offset(worm_ui_nuklear_compose_state_t& state,
                                                        const WormUiComposeView& view) -> bool;
void worm_ui_nuklear_cancel_inline_offset(worm_ui_nuklear_compose_state_t& state);
void worm_ui_nuklear_update_inline_offset(worm_ui_nuklear_compose_state_t& state,
                                          const WormUiComposeView& view, WormUiWorkspaceAction& action,
                                          bool committed, bool deactivated);

void worm_ui_nuklear_inline_timing_initial_text(const WormUiTimingView& view, char* destination,
                                                std::size_t capacity);
[[nodiscard]] auto worm_ui_nuklear_inline_timing_review_new_stage(
    worm_ui_nuklear_compose_state_t& state, const WormUiTimingView& view,
    const WormUiWorkspaceAction& action, float anchor_x, float anchor_y) -> bool;
void worm_ui_nuklear_inline_timing_cancel(worm_ui_nuklear_compose_state_t& state,
                                         WormUiWorkspaceAction& action);
void worm_ui_nuklear_inline_timing_update(worm_ui_nuklear_compose_state_t& state,
                                         const WormUiTimingView& view,
                                         WormUiWorkspaceAction& action,
                                         bool committed, bool deactivated);
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

auto worm_ui_nuklear_overview_source_fraction(const WormUiComposeView& view)
    -> std::optional<double>;

auto worm_ui_nuklear_compose_measure_number(const WormUiComposeView& view, const char* text)
    -> std::optional<std::uint64_t>;

// Returns true only for the first navigation edge of a Shift range.
auto worm_ui_nuklear_alt_wheel_step(worm_ui_nuklear_compose_state_t& state,
    double delta, bool alt, std::uint64_t now_ms) -> int;

auto worm_ui_nuklear_begin_shift_range(worm_ui_nuklear_compose_state_t& state,
    std::uint8_t shift_keys) -> bool;
auto worm_ui_nuklear_release_shift_range(worm_ui_nuklear_compose_state_t& state,
    std::uint8_t released_key) -> bool;
