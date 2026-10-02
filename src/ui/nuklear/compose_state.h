#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

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
};

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
    std::uint8_t shift_range_keys{};
    bool note_move_candidate_active{};
    bool note_move_active{};
    bool timing_stage_drag_active{};
    // Kiai area edge or creation drag on the canvas; y values are viewport pixels.
    bool kiai_drag_active{};
    bool kiai_drag_moved{};
    std::uint8_t kiai_drag_edge{};
    std::uint64_t kiai_drag_head_id{};
    double kiai_drag_anchor_y{};
    double kiai_drag_current_y{};
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
    bool inline_timing_open{};
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
    bool go_to_measure_open{};
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
    worm_ui_nuklear_pinch_gesture_t pinch{};
};

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
auto worm_ui_nuklear_begin_shift_range(worm_ui_nuklear_compose_state_t& state,
    std::uint8_t shift_keys) -> bool;
auto worm_ui_nuklear_release_shift_range(worm_ui_nuklear_compose_state_t& state,
    std::uint8_t released_key) -> bool;
