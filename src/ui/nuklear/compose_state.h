#pragma once
#include "ui/nuklear/file_dialog_slot.h"

#include "ui/frontend.h"
#include "ui/nuklear/compose_drag_capture.h"
#include "ui/nuklear/keysound_list_state.h"
#include "ui/nuklear/compose_bookmark_chooser.h"
#include "ui/nuklear/compose_sound_picker.h"
#include "ui/nuklear/compose_bookmark_rename.h"
#include "ui/nuklear/context_menu.h"
#include "ui/nuklear/sound_slot_batch.h"
#include "ui/nuklear/dialog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
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

inline auto worm_ui_nuklear_beat_fit_line(const WormUiComposeGridLineView& line) noexcept -> bool {
    return line.chart_row != 0 && line.kind >= 1 && line.kind <= 3;
}

// Page keys and Tab chords share one canvas-owned cadence and discard repeat debt.
struct worm_ui_nuklear_tab_navigation_t {
    bool tab_down{};
    bool page_key{};
    std::uint32_t direction_scancode{};
    std::optional<WormUiComposeNavigationAction> direction{};
    std::uint64_t next_repeat_ms{};

    auto current_action() const noexcept -> std::optional<WormUiComposeNavigationAction> {
        if (!page_key || tab_down || !direction) return direction;
        return *direction == WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_BACKWARD
            ? WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    }
    void release_tab(std::uint64_t now) noexcept {
        if (!page_key) { cancel(); return; }
        tab_down = false;
        schedule(now);
    }
    void schedule(std::uint64_t now, std::uint64_t delay = 100) noexcept {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        next_repeat_ms = now > maximum - delay ? maximum : now + delay;
    }
    auto press_tab(std::uint64_t now) noexcept
        -> std::optional<WormUiComposeNavigationAction> {
        if (tab_down) return std::nullopt;
        tab_down = true;
        if (direction) schedule(now, 350);
        return direction;
    }
    auto press_direction(WormUiComposeNavigationAction action,
                         std::uint32_t scancode, std::uint64_t now, bool page = false) noexcept
        -> std::optional<WormUiComposeNavigationAction> {
        if (direction && direction_scancode == scancode) return std::nullopt;
        direction = action;
        direction_scancode = scancode;
        page_key = page;
        if (!tab_down && !page_key) return std::nullopt;
        schedule(now, 350);
        return current_action();
    }
    void release_direction(std::uint32_t scancode) noexcept {
        if (scancode != direction_scancode) return;
        direction.reset();
        page_key = false;
        direction_scancode = 0;
        next_repeat_ms = 0;
    }
    void cancel() noexcept { *this = {}; }
    auto advance(std::uint64_t now, bool canvas_available) noexcept
        -> std::optional<WormUiComposeNavigationAction> {
        if (!canvas_available) { cancel(); return std::nullopt; }
        if ((!tab_down && !page_key) || !direction || now < next_repeat_ms ||
            next_repeat_ms == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        // Delayed frames emit once and discard debt, never replay a burst of moves.
        schedule(now);
        return current_action();
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
    struct tool_chip_stamp_t {
        std::uint64_t project{}, chart{}, revision{}, source{};
        std::uint8_t kind{};
    };
    struct bmson_point_capture_t {
        uint64_t project{}, chart{}, revision{}, lane{}, resource{};
        double x{}, y{};
        bool restart{}, packed{}, dock{};
    };
    struct bgm_column_count_edit_t {
        std::uint64_t chart_id{};
        std::uint64_t revision{};
        std::array<char, 4> text{};
        bool initialized{};
        bool commit_pending{};
        bool input_owned{};
        bool release_pending{};
    };
    using stem_lane_key_t = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
    struct stem_lane_resize_t {
        stem_lane_key_t key;
        double anchor_x{}, anchor_width{36};
    };
    struct layer_creation_request_t {
        std::uint64_t project{}, chart{}, revision{};
    };

    struct input_queue_t {

        worm_ui_nuklear_compose_color_view_t color_view{
            worm_ui_nuklear_compose_color_view_t::normal};
        std::array<worm_ui_nuklear_compose_key_event_t, 64> key_events{};
        std::size_t key_event_count{};
        std::array<worm_ui_nuklear_compose_wheel_event_t, WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY>
            wheel_events{};
        std::size_t wheel_event_count{};
        bool middle_click_pending{};
        float middle_click_x{};
        float middle_click_y{};
        bool cancel_pending{};
        bool waveform_offset_cancel_pending{};
        bool core_gesture_active{};
        bool keyboard_focus{};
        bool action_overflowed{};
        std::uint16_t pointer_modifiers{};
        std::optional<std::uint16_t> pointer_press_modifiers;
    } input_queue;

    struct pointer_capture_t {
        bool pointer_hold_begin_pending{};
        bool pointer_hold_active{};
        // The active pointer hold is a plain press-drag with Hold/Roll armed, so it needs no Alt.
        bool pointer_hold_without_alt{};
        bool note_paint_pointer_active{};
        bool draw_stroke_bga{};
        // BGM source lane the current Draw stroke was pressed on; 0 for key-lane and BGA strokes.
        uint64_t draw_stroke_bgm_lane{};
        bool draw_erase_pointer_active{};
        bool draw_left_cancelled_until_release{};
        bool draw_right_candidate{};
        bool draw_right_native_down{};
        bool draw_right_press_pending{};
        bool draw_right_release_pending{};
        float draw_right_screen_x{};
        float draw_right_screen_y{};
        float draw_right_current_screen_x{};
        float draw_right_current_screen_y{};
        bool draw_right_dragged{};
        double draw_right_anchor_x{};
        double draw_right_anchor_y{};
        uint8_t draw_source_kind{};
        uint64_t draw_source_id{};
        bool tool_chip_pointer_active{};
        bool tool_chip_pointer_canceled{};
        tool_chip_stamp_t tool_chip_stamp;
        std::uint64_t chip_reveal_project{}, chip_reveal_chart{}, source_chip_reveal_id{},
            layer_chip_reveal_id{};
        std::uint8_t source_chip_reveal_kind{};
        bool semantic_layer_paint_pointer_active{};
    } pointer_capture;

    struct bmson_input_t {
        std::optional<bmson_point_capture_t> bmson_point_capture;
        bool bmson_razor_pointer_active{};
        double bmson_razor_last_y_pixels{};
        bool bmson_marker_move_candidate_active{};
        bool bmson_marker_move_active{};
        std::uint64_t bmson_marker_move_event_id{};
        double bmson_marker_move_anchor_x{};
        double bmson_marker_move_anchor_y{};
        float bmson_marker_move_anchor_screen_x{};
        float bmson_marker_move_anchor_screen_y{};
        worm_ui_nuklear_bmson_razor_mode_t bmson_razor_mode{
            worm_ui_nuklear_bmson_razor_mode_t::off};
    } bmson_input;

    struct range_selection_t {
        bool lane_hold_active{};
        bool space_range_hold_active{};
        bool space_range_hold_moved{};
        bool space_range_latched{};
        std::uint8_t shift_range_keys{};
        std::optional<std::uint64_t> alt_wheel_last_event_ms;
    } range_selection;

    struct note_move_t {
        bool note_move_candidate_active{};
        bool note_move_select_on_release{};
        bool note_move_active{};
        double note_move_anchor_x{};
        double note_move_anchor_y{};
        float note_move_anchor_screen_x{};
        float note_move_anchor_screen_y{};
    } note_move;

    struct timing_drag_t {
        compose_drag_capture_t timing_stage_capture{}, span_capture{}, recipe_capture{};
        compose_drag_capture_t timing_marker_capture{}, fit_capture{}, beat_zero_capture{},
            bga_capture{};
        bool timing_stage_drag_active{};
        // Kiai area edge or creation drag on the canvas; y values are viewport pixels.
        bool kiai_drag_active{};
        // The drag grabbed a Speed span edge (kiai_drag_head_id is the Speed event) rather than
        // Kiai.
        bool kiai_drag_speed{};
        bool kiai_drag_moved{};
        std::uint8_t kiai_drag_edge{};
        std::uint64_t kiai_drag_head_id{};
        double kiai_drag_anchor_y{};
        double kiai_drag_current_y{};
        // SV recipe edge or whole-span drag; edge 0 is the begin edge, 1 the end edge, 2 the body.
        bool recipe_drag_active{};
        bool recipe_drag_moved{};
        std::uint8_t recipe_drag_edge{};
        std::uint64_t recipe_drag_id{};
        double recipe_drag_anchor_y{};
        double recipe_drag_current_y{};
    } timing_drag;

    struct recipe_editor_t {
        // Edit buffers of the selected SV recipe's form (see compose_recipe_form.cpp).
        WormUiTimingRecipeForm recipe_edit_form{};
        std::uint64_t recipe_edit_id{};
        std::uint64_t recipe_edit_revision{};
        bool recipe_edit_dirty{};
        bool recipe_edit_apply_pending{};
        // Typed time signature for the cursor measure (Measure at cursor section).
        std::array<char, 32> meter_numerator{'4'};
        std::array<char, 32> meter_denominator{'4'};
    } recipe_editor;

    struct timing_marker_t {
        bool timing_marker_candidate_active{};
        bool timing_marker_drag_active{};
        bool timing_missing_initial_selected{};
        std::uint64_t timing_marker_object_id{};
        std::uint64_t timing_marker_chart_row{};
        WormUiTimingLaneKind timing_marker_lane{WORM_UI_TIMING_LANE_BPM};
        double timing_marker_anchor_y_pixels{};
        double timing_marker_current_y_pixels{};
    } timing_marker;

    struct inline_offset_t {
        bool inline_offset_open{};
        bool inline_offset_in_inspector{};
        bool inline_offset_focus_pending{};
        bool inline_offset_cancel_pending{};
        bool inline_offset_input_consumed{};
        std::uint64_t inline_offset_chart_id{};
        std::uint64_t inline_offset_revision{};
        std::array<char, 64> inline_offset_text{};
        std::array<char, 128> inline_offset_error{};
        bgm_column_count_edit_t bgm_column_count_edit;
    } inline_offset;

    struct inline_timing_t {
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
    } inline_timing;

    struct bookmark_ui_t {
        bookmark_chooser_state_t bookmark_chooser;
        inline_label_editor_t bookmark_rename;
        // A beat bookmark grabbed on its line; `active` once the pointer moved past the threshold.
        bool bookmark_move_candidate_active{};
        bool bookmark_move_active{};
        std::uint64_t bookmark_move_id{};
        std::uint64_t bookmark_move_chart_id{};
        std::uint64_t bookmark_move_revision{};
        double bookmark_move_current_y_pixels{};
        float bookmark_move_anchor_screen_y{};
    } bookmark_ui;

    struct sound_picker_ui_t {
        sound_picker_state_t sound_picker;
    } sound_picker_ui;

    struct note_label_t {
        // Window position of the last note double-click, where the inline note-label editor opens.
        float note_label_anchor_x{};
        float note_label_anchor_y{};
        std::uint64_t label_project_id{std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t label_chart_id{std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t label_document_revision{std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t label_note_id{std::numeric_limits<std::uint64_t>::max()};
        std::array<char, WORM_UI_COMPOSE_NOTE_LABEL_BYTES + 1> note_label{};
        std::array<char, 129> label_search{};
    } note_label;

    struct dialogs_t {
        bool go_to_measure_open{};
        std::uint64_t export_dialog_project_id{};
        std::uint64_t export_dialog_chart_id{};
        std::uint64_t export_dialog_revision{};
        bool export_dialog_open{};
        wormedit::ui::chrome::Dialog export_dialog{};
        bool compose_dialog_release_pending{};
        bool go_to_measure_cancel_pending{};
        bool go_to_measure_focus_pending{};
        std::uint64_t go_to_measure_chart_id{};
        std::uint64_t go_to_measure_revision{};
        std::array<char, 32> go_to_measure_text{};
        std::array<char, 96> go_to_measure_error{};
        std::size_t export_group_document_index{};
        bool export_overwrite{};
    } dialogs;

    struct waveform_drag_t {
        bool waveform_overview_drag_active{};
        worm_ui_nuklear_overview_gesture_t waveform_overview_gesture{};
        worm_ui_nuklear_overview_capture_t waveform_overview_capture{};
        bool waveform_beat_zero_drag_active{};
        bool beat_fit_drag_active{};
        bool offset_fit_drag_active{};
        std::uint64_t beat_fit_chart_row{};
        std::uint8_t beat_fit_snap_step_index{};
        bool beat_fit_ctrl_held{};
        bool beat_fit_onset_snap_held{};
        float waveform_beat_zero_drag_width{};
        float waveform_beat_zero_drag_left{};
        double waveform_beat_zero_drag_span{1.0};
        std::uint64_t waveform_beat_zero_drag_total_frames{};
        std::int64_t waveform_beat_zero_drag_delta_frames{};
    } waveform_drag;

    struct measure_resize_t {
        // Alt+drag of a bar line: the measure whose end line is held, and the proposed result.
        bool measure_resize_active{};
        bool measure_resize_blocked_until_release{};
        bool measure_resize_valid{};
        std::uint64_t measure_resize_measure_id{};
        std::uint64_t measure_resize_chart_id{};
        std::uint64_t measure_resize_revision{};
        std::uint64_t measure_resize_old_numerator{1};
        std::uint64_t measure_resize_old_denominator{1};
        double measure_resize_anchor_y_pixels{};
        double measure_resize_line_y_pixels{};
        double measure_resize_direction{1.0};
        double measure_resize_ghost_y_pixels{};
        float measure_resize_anchor_screen_y{};
        // Shift+Alt: only the line moves, so the next measure trades length with this one.
        bool measure_resize_move_line{};
        std::uint64_t measure_resize_next_numerator{1};
        std::uint64_t measure_resize_next_denominator{1};
        std::uint32_t measure_resize_second_numerator{};
        std::uint32_t measure_resize_second_denominator{};
        std::uint32_t measure_resize_numerator{};
        std::uint32_t measure_resize_denominator{};
        std::array<char, 80> measure_resize_label{};
    } measure_resize;

    struct area_selection_t {
        bool area_selection_candidate_active{};
        bool area_selection_active{};
        double area_selection_anchor_x{};
        double area_selection_anchor_y{};
        float area_selection_anchor_screen_x{};
        float area_selection_anchor_screen_y{};
        WormUiComposeSelectionMode area_selection_mode{WORM_UI_COMPOSE_SELECTION_REPLACE};
    } area_selection;

    struct keyboard_navigation_t {
        std::array<worm_ui_nuklear_lane_hold_t, 64> lane_holds{};
        std::size_t lane_hold_count{};
        worm_ui_nuklear_tab_navigation_t tab_navigation{};
        // Keep cancellation pending after key release until ownership changes notify the
        // dispatcher.
        bool tab_navigation_needs_cancel{};
        bool raw_tab_down{};
        bool navigation_key_active{};
        std::uint32_t navigation_keycode{};
        WormUiComposeNavigationAction navigation_action{WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
        bool pointer_navigation_active{};
        WormUiComposeNavigationAction pointer_navigation_action{
            WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
    } keyboard_navigation;

    struct document_context_t {
        std::uint64_t last_chart_id{};
        std::uint64_t last_project_id{};
        std::uint64_t last_document_revision{};
    } document_context;

    struct layer_editor_t {
        std::array<char, WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY> semantic_layer_name{};
        nk_colorf semantic_layer_color{};
        bool semantic_layer_color_editor_visible{};
        bool semantic_layer_name_editor_visible{};
        bool semantic_layer_name_focus_pending{};
        // Stamp object id is the captured layer id.
        wormedit::ui::chrome::ContextMenu layers_menu{.title = "Layer actions"};
        bool layers_menu_input_consumed{};
        WormUiComposeSemanticLayerView layers_menu_layer{};
        std::string layers_menu_layer_name;
        std::size_t layers_menu_row_index{};
        std::optional<layer_creation_request_t> layer_creation_request;
        std::optional<layer_creation_request_t> layer_creation_context;
        wormedit::ui::chrome::Dialog layer_creation_dialog{};
        std::array<char, WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY> layer_creation_name{};
        bool layer_creation_focus_pending{};
        std::size_t layers_panel_total_count{};
        std::uint64_t layers_panel_chart_id{};
        std::uint64_t layers_panel_project_id{};
    } layer_editor;

    struct stem_lanes_t {
        std::map<stem_lane_key_t, double> stem_lane_widths;
        std::optional<stem_lane_resize_t> stem_lane_resize;
        bool stem_lane_resize_cursor_requested{};
        bool bgm_mix_pointer_active{};
        // Stem lane whose right edge is hovered or being dragged this frame; drives the edge
        // highlight.
        std::uint64_t stem_lane_resize_hover_lane{};
    } stem_lanes;

    struct snap_grid_t {
        wormedit::ui::chrome::ContextMenu snap_grid_menu{.title = "Editing grid"};
        int snap_grid_choice{};
    } snap_grid;

    struct dock_layout_t {
        bool inspector_visible{true};
        // Keysound listbox (compose_sounds_panel.cpp).
        bool sounds_panel_visible{true};
        bool layers_panel_visible{};
        bool compact_dock_layout{};
        bool sounds_layers_open{};
        // The Sounds dock's selected tab; non-BMSON charts always show Sounds.
        bool sounds_dock_bmson_tab{};
        bool sounds_preview_on_click{true};
        bool bga_panel_visible{};
        // Measures dock: selection by measure ID, the typed meter, and the IDs staged for one
        // action.
        bool measures_panel_visible{};
        // The BMS branch dock on the left of the field: wanted by the user, and the width it took
        // last frame.
        bool branch_panel_visible{true};
        float branch_dock_width_drawn{};
    } dock_layout;

    struct keysound_filters_t {
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
        auto is_popup_open() const -> bool {
            return keysound_filter_group_popup_open || keysound_layer_filter_popup_open;
        }
        bool keysound_filter_group_popup_open{};
        bool keysound_layer_filter_popup_open{};
        bool keysound_filter_release_pending{};
    } keysound_filters;

    struct keysound_slots_t {
        worm_ui_sound_slot_batch_t keysound_slot_batch;
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
        std::uint64_t keysound_pending_project{}, keysound_pending_chart{},
            keysound_pending_revision{};
        std::uint64_t keysound_pending_occupied_resource{};
        std::uint16_t keysound_pending_slot{};
        bool keysound_renumber_confirmation_open{};
        bool keysound_renumber_release_pending{};
        std::uint64_t keysound_renumber_project{}, keysound_renumber_chart{};
        std::uint64_t keysound_renumber_revision{};
        std::vector<std::uint64_t> keysound_renumber_resources;
        bool sound_layer_confirmation_open{};
        std::uint64_t sound_layer_confirmation_project{}, sound_layer_confirmation_chart{};
        std::uint64_t sound_layer_confirmation_revision{}, sound_layer_confirmation_layer{};
        std::vector<std::uint64_t> sound_layer_confirmation_resources;
    } keysound_slots;

    struct keysound_catalog_t {
        worm_ui_keysound_group_review_t keysound_group_review;
        // Stamp object id is the right-clicked sound. Group review dialogs also arm its
        // release latch so their closing gesture stays away from the list.
        wormedit::ui::chrome::ContextMenu keysound_group_menu{.title = "Sound group actions"};
        std::array<char, 64> keysound_slots_search{};
        std::size_t keysound_slots_first_row{};
        std::size_t keysound_slots_row_limit{64};
        std::uint16_t keysound_selected_slot{1};
        std::uint64_t keysound_selected_slot_resource{};
        bool keysound_selected_slot_resource_known{};
        std::uint64_t keysound_slots_project{}, keysound_slots_chart{}, keysound_slots_revision{};
        std::uint64_t keysound_project_id{};
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
        std::vector<std::string> keysound_staged_paths;
        std::vector<const char*> keysound_staged_path_pointers;
    } keysound_catalog;

    struct bga_interaction_t {
        bool bga_event_move_candidate_active{};
        bool bga_event_move_active{};
        std::uint64_t bga_event_move_object_id{};
        std::uint64_t bga_event_move_anchor_row{};
        double bga_event_move_anchor_y_pixels{};
        double bga_event_move_current_y_pixels{};
        double bga_event_move_current_x_pixels{};
        std::uint8_t bga_stack_kind{};
        bool bga_stack_open{};
        bool bga_stack_release_pending{};
        std::uint64_t bga_stack_project_id{};
        std::uint64_t bga_stack_chart_id{};
        std::uint64_t bga_stack_revision{};
        std::uint64_t bga_stack_row{};
    } bga_interaction;

    struct measures_dock_t {
        worm_ui_keysound_selection_t measure_selection;
        std::array<char, 32> measures_meter_numerator{'4'};
        std::array<char, 32> measures_meter_denominator{'4'};
        std::vector<std::uint64_t> measures_staged_ids;
    } measures_dock;

    struct bga_catalog_t {
        worm_ui_keysound_selection_t bga_selection;
        std::array<char, 64> bga_search{};
        std::uint8_t bga_use_filter{};
        std::size_t bga_first_row{}, bga_row_limit{32};
        bool bga_sort_by_id{};
        bool bga_renumber_confirmation_open{};
        bool bga_renumber_release_pending{};
        std::uint64_t bga_renumber_project{}, bga_renumber_chart{};
        std::uint64_t bga_renumber_revision{};
        std::vector<std::uint64_t> bga_renumber_resources;
        std::uint64_t bga_project_id{}, bga_chart_id{}, bga_revision{};
        bool bga_auto_increment_on_place{};
        bool armed_from_bga_dock{};
        bool bga_reveal_primary{};
        std::vector<std::uint64_t> bga_advance_ids;
        std::vector<std::uint64_t> bga_staged_ids;
        std::vector<std::string> bga_staged_paths;
        std::vector<const char*> bga_staged_path_pointers;
        bool bga_list_valid{};
        float bga_list_x{}, bga_list_y{}, bga_list_width{}, bga_list_height{};
        // The BGA rows shown last frame, kept so Ctrl+A over the list can select them.
        std::vector<std::uint64_t> bga_shown;
    } bga_catalog;

    struct sounds_dock_t {
        // Window rectangle of the list (for dropped files); valid only on frames that draw it.
        bool sound_list_valid{};
        float sound_list_x{};
        float sound_list_y{};
        float sound_list_width{};
        float sound_list_height{};
        int bmson_audition_index{1};
    } sounds_dock;

    struct resource_picker_t {
        worm_ui_nuklear_compose_resource_filter_t resource_filter{
            worm_ui_nuklear_compose_resource_filter_t::sample};
        std::array<char, 96> resource_search{};
        std::uint64_t armed_resource_id{};
        std::uint64_t resource_selection_chart_id{};
        std::uint64_t resource_selection_object_id{};
        std::uint64_t resource_selection_assigned_id{};
        bool auto_increment_on_place{};
        worm_ui_nuklear_compose_resource_pointer_mode_t resource_pointer_mode{
            worm_ui_nuklear_compose_resource_pointer_mode_t::off};
    } resource_picker;

    struct pinch_t {
        worm_ui_nuklear_pinch_gesture_t pinch{};
    } pinch;
};

// Claims Compose keyboard input for the BGM column number field as soon as it
// receives a click, including the frame before Nuklear reports it active.
auto worm_ui_nuklear_update_bgm_column_count_input_owner(
    worm_ui_nuklear_compose_state_t& state, std::uint64_t chart_id,
    std::uint64_t revision, bool editable, bool field_clicked, bool edit_active,
    bool edit_finished) -> bool;
void worm_ui_nuklear_retire_bgm_column_count_input_owner(
    worm_ui_nuklear_compose_state_t& state, const nk_context* context);

void worm_ui_nuklear_update_compose_release_latches(
    worm_ui_nuklear_compose_state_t& state, const nk_context* context);
void worm_ui_nuklear_reset_draw_right_capture(worm_ui_nuklear_compose_state_t& state);

// Keep press/release pairs ordered, including pairs received within one SDL frame.
// When another owner takes input, discard new presses but deliver matching releases.
template <typename Consumer>
void worm_ui_nuklear_dispatch_compose_keys(worm_ui_nuklear_compose_state_t& state,
    bool accept_presses, Consumer&& consume) {
    for (size_t index = 0; index < state.input_queue.key_event_count &&
                            !state.input_queue.action_overflowed; ++index) {
        const auto& event = state.input_queue.key_events[index];
        if (event.down && !accept_presses) continue;
        consume(event);
    }
    state.input_queue.key_event_count = 0;
}

[[nodiscard]] auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t&;
auto worm_ui_nuklear_stem_lane_width(const worm_ui_nuklear_compose_state_t& state,
    std::uint64_t project, std::uint64_t chart, std::uint64_t lane) -> double;
void worm_ui_nuklear_set_stem_lane_width(worm_ui_nuklear_compose_state_t& state,
    std::uint64_t project, std::uint64_t chart, std::uint64_t lane, double width);
void worm_ui_nuklear_cancel_stem_lane_resize(worm_ui_nuklear_compose_state_t& state,
    bool restore = true);
void worm_ui_nuklear_cancel_stale_stem_lane_resize(worm_ui_nuklear_compose_state_t& state,
    const WormUiComposeView& view, bool window_focused);
// True when a window position lies over the keysound listbox drawn this frame.
[[nodiscard]] auto worm_ui_nuklear_sound_list_contains(float x, float y) -> bool;
void worm_ui_nuklear_advance_resource_picker(std::uint64_t placed_resource_id,
                                              std::uint64_t next_resource_id);

// The one answer to "which Sounds-dock sample would a new note carry": the armed dock sample
// when it is still listed and not missing, else 0. Lane keys and note paint both ask this
// instead of reading armed_resource_id themselves.
[[nodiscard]] auto worm_ui_nuklear_armed_dock_sound(
    const worm_ui_nuklear_compose_state_t& state, const WormUiComposeView& view) -> std::uint64_t;

// Stamps the armed Sounds-dock sample on the compose input just appended, so every gesture that
// can create a note (lane key, paint, Alt-hold, click) captures it the same way at press time.
void worm_ui_nuklear_tag_last_compose_input_sound(const worm_ui_nuklear_compose_state_t& state,
                                                  const WormUiComposeView& view,
                                                  WormUiWorkspaceAction& action);

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

using worm_ui_nuklear_compose_file_dialog_result_t = wormedit::ui::file_dialog_result_t;

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

void worm_ui_nuklear_pick_draw_source(worm_ui_nuklear_compose_state_t&, WormUiWorkspaceAction&, uint8_t kind, uint64_t id);

struct nk_context;
bool worm_ui_nuklear_compose_context_click(const nk_context*, const WormUiComposeView&);
