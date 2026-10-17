#include "ui/nuklear/compose_gesture_registry.h"
#include "ui/nuklear/compose_measure_resize_input.h"
#include "ui/nuklear/compose_panel.h"
#include "ui/compose_timing_geometry.h"
#include "ui/nuklear/theme.h"
#include "sys/compose_scroll_geometry.h"
#include "ui/nuklear/bms_branch_panel.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_bookmark_rename.h"
#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/compose_recipe_menu.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/timing_stage_editor.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#include <string>

#include "ui/nuklear/compose_input_panel.h"
#include "ui/nuklear/compose_input_internal.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_note_menu.h"

static void clear_note_paint_capture() {
    state.pointer_capture.draw_left_cancelled_until_release |= state.pointer_capture.note_paint_pointer_active;
    state.pointer_capture.note_paint_pointer_active = false;
}

void reset_bmson_marker_move_capture() {
    state.bmson_input.bmson_marker_move_candidate_active = false;
    state.bmson_input.bmson_marker_move_active = false;
    state.bmson_input.bmson_marker_move_event_id = 0;
}

static void reset_timing_marker_drag_capture() {
    state.timing_marker.timing_marker_candidate_active = false;
    state.timing_marker.timing_marker_drag_active = false;
    state.timing_marker.timing_marker_object_id = 0;
    state.timing_drag.timing_marker_capture.cancel();
}

static void reset_bookmark_move_capture() {
    state.bookmark_ui.bookmark_move_candidate_active = false;
    state.bookmark_ui.bookmark_move_active = false;
    state.bookmark_ui.bookmark_move_id = 0;
}

static void reset_bga_event_move_capture() {
    // Every pointer-capture cancel site resets the bookmark drag with the BGA one.
    reset_bookmark_move_capture();
    state.bga_interaction.bga_event_move_candidate_active = false;
    state.bga_interaction.bga_event_move_active = false;
    state.bga_interaction.bga_event_move_object_id = 0;
    state.timing_drag.bga_capture.cancel();
}

static void reset_overview_capture() {
    state.waveform_drag.waveform_overview_gesture = {};
    state.waveform_drag.waveform_overview_capture = {};
}

static void reset_hold_capture() {
    state.pointer_capture.pointer_hold_without_alt = false;
    state.keyboard_navigation.lane_hold_count = 0;
    state.keyboard_navigation.lane_holds = {};
}

static void reset_range_capture() {
    state.range_selection.space_range_latched = false;
    state.range_selection.space_range_hold_moved = false;
    state.range_selection.shift_range_keys = 0;
}

static void reset_draw_right_capture() { worm_ui_nuklear_reset_draw_right_capture(state); }

static void reset_draw_capture() {
    state.pointer_capture.draw_left_cancelled_until_release |= state.pointer_capture.note_paint_pointer_active ||
                                              state.bmson_input.bmson_point_capture.has_value();
    state.bmson_input.bmson_point_capture.reset();
    reset_draw_right_capture();
    state.pointer_capture.draw_stroke_bgm_lane = 0;
}

static void reset_measure_capture() { cancel_compose_measure_resize(); }
static void reset_stem_capture() { worm_ui_nuklear_cancel_stem_lane_resize(state); }
static void reset_tool_chip_capture() {
    state.pointer_capture.tool_chip_pointer_canceled |= state.pointer_capture.tool_chip_pointer_active;
}

static void reset_stage_capture() { state.timing_drag.timing_stage_capture.cancel(); }
static void reset_span_capture() { state.timing_drag.span_capture.cancel(); state.timing_drag.kiai_drag_moved = false; }
static void reset_recipe_capture() { state.timing_drag.recipe_capture.cancel(); state.timing_drag.recipe_drag_moved = false; }
static void reset_fit_capture() { state.timing_drag.fit_capture.cancel(); }
static void reset_beat_zero_capture() {
    if (!state.input_queue.waveform_offset_cancel_pending) state.timing_drag.beat_zero_capture.cancel();
}

struct compose_gesture_entry_t {
    bool* active;
    void (*reset)(){};
    bool cancel_core{true};
};

void cancel_compose_gestures(WormUiWorkspaceAction* action) {
    // Candidate flags are captures too: a late release must not resurrect a cancelled press.
    static const auto compose_gesture_registry = std::to_array<compose_gesture_entry_t>({
        {&state.pointer_capture.pointer_hold_begin_pending, reset_hold_capture},
        {&state.pointer_capture.pointer_hold_active, reset_hold_capture},
        {&state.range_selection.lane_hold_active, reset_hold_capture},
        {&state.pointer_capture.note_paint_pointer_active, reset_draw_capture},
        {&state.pointer_capture.draw_erase_pointer_active, reset_draw_right_capture},
        {&state.pointer_capture.draw_right_candidate, reset_draw_right_capture},
        {&state.bmson_input.bmson_razor_pointer_active},
        {&state.bmson_input.bmson_marker_move_active, reset_bmson_marker_move_capture},
        {&state.bmson_input.bmson_marker_move_candidate_active},
        {&state.pointer_capture.semantic_layer_paint_pointer_active},
        {&state.range_selection.space_range_hold_active, reset_range_capture},
        {&state.note_move.note_move_active},
        {&state.note_move.note_move_candidate_active},
        {&state.area_selection.area_selection_active},
        {&state.area_selection.area_selection_candidate_active},
        {&state.bga_interaction.bga_event_move_active, reset_bga_event_move_capture},
        {&state.bga_interaction.bga_event_move_candidate_active},
        {&state.bookmark_ui.bookmark_move_active, reset_bookmark_move_capture},
        {&state.bookmark_ui.bookmark_move_candidate_active},
        {&state.timing_drag.timing_stage_drag_active, reset_stage_capture},
        {&state.timing_drag.kiai_drag_active, reset_span_capture},
        {&state.timing_drag.recipe_drag_active, reset_recipe_capture},
        {&state.timing_marker.timing_marker_drag_active, reset_timing_marker_drag_capture},
        {&state.timing_marker.timing_marker_candidate_active},
        {&state.waveform_drag.beat_fit_drag_active, reset_fit_capture},
        {&state.waveform_drag.offset_fit_drag_active, reset_fit_capture},
        {&state.waveform_drag.waveform_beat_zero_drag_active, reset_beat_zero_capture},
        {&state.waveform_drag.waveform_overview_drag_active, reset_overview_capture},
        {&state.measure_resize.measure_resize_active, reset_measure_capture},
        {&state.stem_lanes.stem_lane_resize_cursor_requested, reset_stem_capture},
        {&state.pointer_capture.tool_chip_pointer_active, reset_tool_chip_capture, false},
        {&state.stem_lanes.bgm_mix_pointer_active},
    });
    bool pending = state.input_queue.cancel_pending || state.input_queue.core_gesture_active;
    for (const auto& entry : compose_gesture_registry)
        pending |= entry.cancel_core && *entry.active;
    pending |= state.stem_lanes.stem_lane_resize.has_value() || state.bmson_input.bmson_point_capture.has_value();
    // Audio offset is a separate transaction, so preserve its stamp until the deferred cancel.
    state.input_queue.waveform_offset_cancel_pending |= state.waveform_drag.waveform_beat_zero_drag_active;
    if (action && state.input_queue.waveform_offset_cancel_pending) {
        set_waveform_offset_drag_action(action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                                        state.waveform_drag.waveform_beat_zero_drag_delta_frames);
        state.input_queue.waveform_offset_cancel_pending = false;
    }
    for (const auto& entry : compose_gesture_registry) {
        state.pointer_capture.draw_left_cancelled_until_release |= *entry.active;
        if (entry.reset) entry.reset();
        *entry.active = false;
    }
    state.input_queue.core_gesture_active = false;
    state.input_queue.pointer_press_modifiers.reset();
    state.range_selection.alt_wheel_last_event_ms.reset();
    cancel_compose_navigation(action);
    discard_compose_frame_input();
    if (action) {
        action->minimap_window_capture_active = false;
        if (pending) append_compose_cancel_once(action);
        state.input_queue.cancel_pending = false;
    } else state.input_queue.cancel_pending = true;
}

void set_waveform_offset_drag_action(WormUiWorkspaceAction* action,
                                            WormUiAudioOffsetDragAction kind,
                                            std::int64_t delta_frames) {
    if (!action || action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE)
        return;
    action->audio_offset_drag_action = kind;
    action->audio_offset_drag_chart_id = state.timing_drag.beat_zero_capture.chart;
    action->audio_offset_drag_document_revision = state.timing_drag.beat_zero_capture.revision;
    action->audio_offset_drag_sample_rate = state.timing_drag.beat_zero_capture.sample_rate;
    action->audio_offset_drag_delta_frames = delta_frames;
}

void reset_waveform_offset_drag() {
    state.waveform_drag.waveform_beat_zero_drag_active = false;
    state.waveform_drag.waveform_beat_zero_drag_delta_frames = 0;
    state.timing_drag.beat_zero_capture.cancel();
}

void append_compose_input(
    WormUiWorkspaceAction* action, WormUiComposeInputKind kind, double x_pixels,
    double y_pixels, uint64_t lane_id, bool successive,
    WormUiComposeSelectionMode selection_mode,
    WormUiComposeNavigationAction navigation_action,
    uint64_t object_id, uint8_t resource_role,
    bool razor_stroke_start,
    WormUiTimingLaneKind timing_lane,
    uint64_t timing_chart_id, uint64_t timing_document_revision,
    uint64_t timing_anchor_chart_row) {
    if (!action) {
        return;
    }
    if (action->compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        action->compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY - 1] = {
            .kind = WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
            .lane_id = 0,
            .object_id = 0,
            .timing_chart_id = 0,
            .timing_document_revision = 0,
            .timing_lane = WORM_UI_TIMING_LANE_BPM,
            .timing_anchor_chart_row = 0,
            .resource_role = 0,
            .successive = false,
            .razor_stroke_start = false,
            .selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
            .navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
            .x_pixels = 0.0,
            .y_pixels = 0.0,
            .audio_fraction = 0.0,
            .beat_fit_ctrl_held = false,
            .beat_fit_snap_step_numerator = 1,
            .beat_fit_snap_step_denominator = 1,
        };
        action->compose_input_count = WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY;
        cancel_compose_gestures(nullptr);
        state.input_queue.action_overflowed = true;
        return;
    }
    action->compose_inputs[action->compose_input_count++] = {
        .kind = kind,
        .lane_id = lane_id,
        .object_id = object_id,
        .timing_chart_id = timing_chart_id,
        .timing_document_revision = timing_document_revision,
        .timing_lane = timing_lane,
        .timing_anchor_chart_row = timing_anchor_chart_row,
        .resource_role = resource_role,
        .successive = successive,
        .razor_stroke_start = razor_stroke_start,
        .selection_mode = selection_mode,
        .navigation_action = navigation_action,
        .x_pixels = x_pixels,
        .y_pixels = y_pixels,
        .audio_fraction = 0.0,
        .beat_fit_ctrl_held = false,
        .beat_fit_snap_step_numerator = 1,
        .beat_fit_snap_step_denominator = 1,
    };
}

void append_beat_fit_input(WormUiWorkspaceAction* action,
                                  WormUiComposeInputKind kind, double y,
                                  bool ctrl_held, bool onset_snap_held) {
    append_compose_input(action, kind, 0.0, y);
    if (!action || action->compose_input_count == 0) return;
    auto& event = action->compose_inputs[action->compose_input_count - 1];
    event.beat_fit_ctrl_held = ctrl_held;
    event.beat_fit_onset_snap_held = onset_snap_held;
    const auto index = std::min<std::size_t>(state.waveform_drag.beat_fit_snap_step_index,
                                             beat_fit_snap_steps.size() - 1);
    event.beat_fit_snap_step_numerator = beat_fit_snap_steps[index].numerator;
    event.beat_fit_snap_step_denominator = beat_fit_snap_steps[index].denominator;
}

void cancel_beat_fit_drag(WormUiWorkspaceAction* action) {
    if (state.waveform_drag.beat_fit_drag_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
        state.waveform_drag.beat_fit_drag_active = false;
        state.timing_drag.fit_capture.cancel();
    }
}

void cancel_offset_fit_drag(WormUiWorkspaceAction* action) {
    if (!state.waveform_drag.offset_fit_drag_active) return;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL);
    state.waveform_drag.offset_fit_drag_active = false;
    state.timing_drag.fit_capture.cancel();
}

void
update_compose_pointer_navigation(WormUiWorkspaceAction* action,
                                  std::optional<WormUiComposeNavigationAction> desired_action) {
    if (!action) {
        return;
    }
    if (state.keyboard_navigation.pointer_navigation_active &&
        (!desired_action || *desired_action != state.keyboard_navigation.pointer_navigation_action)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE, state.keyboard_navigation.pointer_navigation_action);
        state.keyboard_navigation.pointer_navigation_active = false;
    }
    if (desired_action && !state.keyboard_navigation.pointer_navigation_active) {
        if (state.keyboard_navigation.navigation_key_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE, state.keyboard_navigation.navigation_action);
            state.keyboard_navigation.navigation_key_active = false;
            state.keyboard_navigation.navigation_keycode = 0;
        }
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE, *desired_action);
        state.keyboard_navigation.pointer_navigation_active = !state.input_queue.action_overflowed;
        state.keyboard_navigation.pointer_navigation_action = *desired_action;
    }
}

void cancel_compose_navigation(WormUiWorkspaceAction* action) {
    const bool pending = state.keyboard_navigation.tab_navigation_needs_cancel || state.keyboard_navigation.tab_navigation.tab_down ||
                         state.keyboard_navigation.navigation_key_active || state.keyboard_navigation.pointer_navigation_active;
    if (action && pending) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
    }
    state.keyboard_navigation.tab_navigation.cancel();
    state.keyboard_navigation.raw_tab_down = false;
    state.keyboard_navigation.tab_navigation_needs_cancel = pending && !action;
    state.pinch.pinch = {};
    state.keyboard_navigation.navigation_key_active = false;
    state.keyboard_navigation.navigation_keycode = 0;
    state.keyboard_navigation.pointer_navigation_active = false;
}

bool compose_action_has_cancel(const WormUiWorkspaceAction* action) {
    if (!action) {
        return false;
    }
    for (size_t index = 0; index < action->compose_input_count; ++index) {
        if (action->compose_inputs[index].kind == WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE) {
            return true;
        }
    }
    return false;
}

void append_compose_cancel_once(WormUiWorkspaceAction* action) {
    if (action && !compose_action_has_cancel(action)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
    }
}

void queue_compose_wheel_input(double delta_x, double delta_y, double mouse_x,
                                         double mouse_y, std::uint16_t modifiers,
                                         bool coarse_steps) {
    if (state.input_queue.wheel_event_count == state.input_queue.wheel_events.size()) {
        cancel_compose_gestures(nullptr);
        state.input_queue.key_event_count = 0;
        state.input_queue.keyboard_focus = false;
        return;
    }
    state.input_queue.wheel_events[state.input_queue.wheel_event_count++] = {
        .delta_x = delta_x,
        .delta_y = delta_y,
        .mouse_x = mouse_x,
        .mouse_y = mouse_y,
        .modifiers = modifiers,
        .coarse_steps = coarse_steps,
        .tab_speed_modifier = state.keyboard_navigation.raw_tab_down,
    };
}

void queue_compose_pinch_input(worm_ui_nuklear_pinch_phase_t phase,
                               double factor, double mouse_x, double mouse_y) {
    if (state.input_queue.cancel_pending) { state.pinch.pinch = {}; return; }
    worm_ui_nuklear_pinch_input(state.pinch.pinch, phase, factor, mouse_x, mouse_y,
                               state.document_context.last_project_id, state.document_context.last_chart_id);
}

void queue_compose_middle_click_input(float x, float y) {
    state.input_queue.middle_click_pending = true;
    state.input_queue.middle_click_x = x;
    state.input_queue.middle_click_y = y;
}

void compose_handle_focus_lost_input() {
    cancel_compose_gestures(nullptr);
    state.input_queue.key_event_count = 0;
    state.input_queue.keyboard_focus = false;
}

void discard_compose_frame_input() {
    state.pinch.pinch = {};
    state.input_queue.wheel_event_count = 0;
    state.input_queue.middle_click_pending = false;
}


bool compose_point_hits_note(const WormUiComposeView* view, double x, double y) {
    if (!view || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        if (view->note_endpoints[index].proposal_ghost) {
            continue;
        }
        const auto& rect = view->note_endpoints[index].bounds;
        const auto center_y = (rect.top + rect.bottom) / 2.0;
        if (x >= rect.left && x <= rect.right && std::abs(y - center_y) <= 8.0) {
            return true;
        }
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        const auto& rect = view->hold_bodies[index].bounds;
        if (view->hold_bodies[index].proposal_ghost) {
            continue;
        }
        if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) {
            return true;
        }
    }
    return false;
}

uint64_t compose_bgm_event_at(const WormUiComposeView* view, double x, double y) {
    if (!view || !std::isfinite(x) || !std::isfinite(y)) {
        return 0;
    }
    for (size_t index = view->bgm_event_count; index-- > 0;) {
        const auto& event = view->bgm_events[index];
        if (event.proposal_ghost) continue;
        if (x >= event.bounds.left && x <= event.bounds.right &&
            y >= (event.has_tail ? event.head_top_pixels : event.bounds.top) &&
            y <= (event.has_tail ? event.head_bottom_pixels : event.bounds.bottom)) {
            return event.event_id;
        }
    }
    return 0;
}

uint64_t compose_sound_object_at(const WormUiComposeView& view, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return 0;
    for (size_t i = view.note_endpoint_count; i-- > 0;) {
        const auto& note = view.note_endpoints[i];
        if (note.proposal_ghost || note.paint_preview) continue;
        const auto& r = note.bounds;
        if (x >= r.left && x <= r.right && std::abs(y - (r.top + r.bottom) / 2) <= 8)
            return note.note_id;
    }
    for (size_t i = view.hold_body_count; i-- > 0;) {
        const auto& note = view.hold_bodies[i];
        const auto& r = note.bounds;
        if (!note.proposal_ghost && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom)
            return note.note_id;
    }
    if (const auto bgm = compose_bgm_event_at(&view, x, y)) return bgm;
    for (size_t i = 0; i < view.stem_cut_count; ++i) {
        const auto& cut = view.stem_cuts[i];
        if (std::abs(y - cut.y_pixels) > 6) continue;
        for (size_t j = 0; j < view.bgm_lane_count; ++j) {
            const auto& lane = view.bgm_lanes[j];
            if (lane.source_lane_id == cut.bgm_lane_id && x >= lane.bounds.left && x <= lane.bounds.right)
                return cut.object_id;
        }
    }
    return 0;
}

uint64_t compose_bga_event_at(const WormUiComposeView* view, double x, double y) {
    if (!view || !view->bga_lane_visible || !std::isfinite(x) || !std::isfinite(y)) return 0;
    for (size_t index = 0; index < view->bga_event_count; ++index) {
        const auto& event = view->bga_events[index];
        if (event.selected && x >= event.bounds.left && x <= event.bounds.right &&
            y >= event.bounds.top && y <= event.bounds.bottom) return event.event_id;
    }
    for (size_t index = view->bga_event_count; index-- > 0;) {
        const auto& event = view->bga_events[index];
        if (x >= event.bounds.left && x <= event.bounds.right && y >= event.bounds.top &&
            y <= event.bounds.bottom)
            return event.event_id;
    }
    return 0;
}

auto compose_bga_lane_kind_at(const WormUiComposeView& view, double x, double y)
    -> std::optional<std::uint8_t> {
    if (!view.bga_lane_visible || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    for (size_t index = 0; index < std::min<size_t>(view.bga_lane_count, 4); ++index) {
        const auto& rect = view.bga_lane_rects[index];
        if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom)
            return view.bga_lane_kinds[index];
    }
    if (view.bga_lane_count == 0 && x >= view.bga_lane_bounds.left && x < view.bga_lane_bounds.right &&
        y >= view.bga_lane_bounds.top && y < view.bga_lane_bounds.bottom) return 0;
    return std::nullopt;
}

bool compose_point_hits_selected_note(const WormUiComposeView* view, double x, double y) {
    if (!view || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        const auto& endpoint = view->note_endpoints[index];
        if (!endpoint.selected || endpoint.proposal_ghost) {
            continue;
        }
        const auto center_y = (endpoint.bounds.top + endpoint.bounds.bottom) / 2.0;
        if (x >= endpoint.bounds.left && x <= endpoint.bounds.right &&
            std::abs(y - center_y) <= 8.0) {
            return true;
        }
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        const auto& body = view->hold_bodies[index];
        if (body.selected && !body.proposal_ghost && x >= body.bounds.left &&
            x <= body.bounds.right && y >= body.bounds.top && y <= body.bounds.bottom) {
            return true;
        }
    }
    return false;
}

bool compose_shortcut_modifier(uint16_t modifiers) {
    return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

WormUiComposeSelectionMode compose_selection_mode(uint16_t modifiers) {
    return compose_pointer_selection_mode(modifiers);
}

static void cancel_pending_compose_panel_actions(WormUiWorkspaceAction* action) {
    if (state.inline_timing.inline_timing_open) worm_ui_nuklear_inline_timing_cancel(state, *action);
    cancel_compose_gestures(action);
    state.resource_picker.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    state.bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
    state.inline_timing.inline_timing_open = false;
}

static void reset_compose_revision_state(bool, WormUiWorkspaceAction* action) {
    cancel_compose_gestures(action);
    state.inline_offset.bgm_column_count_edit = {};
    state.inline_timing.inline_timing_open = false;
    state.timing_marker.timing_missing_initial_selected = false;
    state.layer_editor.semantic_layer_color_editor_visible = false;
}

static void clear_inactive_compose_panel(WormUiWorkspaceAction* action) {
    cancel_compose_gestures(action);
    state.inline_offset.bgm_column_count_edit = {};
    state.input_queue.key_event_count = 0;
    state.input_queue.keyboard_focus = false;
    state.inline_timing.inline_timing_open = false;
    state.timing_marker.timing_missing_initial_selected = false;
    state.layer_editor.semantic_layer_color_editor_visible = false;
    state.document_context.last_chart_id = 0;
    state.document_context.last_document_revision = 0;
}

static void cancel_disabled_compose_paint_modes(const WormUiComposeView* view,
                                                bool window_focused,
                                                WormUiWorkspaceAction* action) {
    if ((state.pointer_capture.note_paint_pointer_active || state.pointer_capture.draw_erase_pointer_active) &&
        (!window_focused || !view->scene_available || !view->note_paint_mode_active ||
         view->input_occluded || view->timing_focus)) {
        append_compose_cancel_once(action);
        clear_note_paint_capture();
        reset_draw_right_capture();
    }
    if (state.pointer_capture.semantic_layer_paint_pointer_active &&
        (!window_focused || !view->scene_available || !view->semantic_layer_paint_mode_active)) {
        append_compose_cancel_once(action);
        state.pointer_capture.semantic_layer_paint_pointer_active = false;
    }
}

static void retain_compose_core_gesture_presence(const WormUiComposeView* view, bool active) {
    if (view && active && view->visible) state.input_queue.core_gesture_active = view->editor_gesture_active;
}

static void sync_armed_resource_context(const WormUiComposeView& view, WormUiWorkspaceAction& action,
                                        bool project_changed, bool chart_changed) {
    if (project_changed) {
        reset_armed_resource(state, compose_resource_reset_reason_t::project_changed);
        state.resource_picker.auto_increment_on_place = false;
        state.resource_picker.resource_pointer_mode =
            worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        state.bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        state.document_context.last_project_id = view.project_id;
    } else if (chart_changed && state.bga_catalog.armed_from_bga_dock && state.bga_catalog.bga_chart_id != view.chart_id) {
        reset_armed_resource(state, compose_resource_reset_reason_t::chart_changed);
        worm_ui_nuklear_pick_draw_source(state, action, 0, 0);
    }
}

static void sync_pointer_hold_acceptance(const WormUiComposeView& view) {
    if (!state.pointer_capture.pointer_hold_begin_pending && !state.pointer_capture.pointer_hold_active) return;
    state.pointer_capture.pointer_hold_begin_pending = false;
    state.pointer_capture.pointer_hold_active = view.editor_pointer_hold_active;
    if (!state.pointer_capture.pointer_hold_active) {
        state.pointer_capture.pointer_hold_without_alt = false;
        state.pointer_capture.draw_left_cancelled_until_release = true;
    }
}

void prepare_compose_panel_input(const WormUiComposeView* view, bool compose_tab_active,
                                           bool window_focused, WormUiWorkspaceAction* action) {
    state.input_queue.pointer_modifiers = static_cast<std::uint16_t>(SDL_GetModState());
    retain_compose_core_gesture_presence(view, compose_tab_active);
    if (!action) {
        return;
    }
    prepare_compose_note_menu(view, compose_tab_active, window_focused, action);
    if (!compose_tab_active || !view || !view->visible) reset_compose_area_menu();
    if (view && compose_tab_active && view->visible) {
        state.input_queue.action_overflowed = false;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
        action->compose_input_viewport_width_pixels = view->viewport_width_pixels;
        if (!view->scene_available || view->input_occluded || !window_focused) {
            cancel_compose_gestures(action);
        }
        if (state.input_queue.cancel_pending) cancel_pending_compose_panel_actions(action);
        const auto project_changed = state.document_context.last_project_id != view->project_id;
        const auto chart_changed = project_changed || state.document_context.last_chart_id != view->chart_id;
        const auto revision_changed = state.document_context.last_document_revision != view->document_revision;
        sync_armed_resource_context(*view, *action, project_changed, chart_changed);
        // A successful chord release refreshes surviving core holds to the new revision.
        // External edits leave their stamps stale and cancel the complete capture instead.
        if (chart_changed || (revision_changed && !view->editor_gesture_current))
            reset_compose_revision_state(chart_changed, action);
        if (chart_changed) {
            state.resource_picker.resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            state.bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        }
        if (chart_changed) {
            // Events received before the first frame for another chart still
            // target the previously visible geometry. Events after an ordinary
            // document edit already belong to the current frame/revision.
            state.input_queue.key_event_count = 0;
            state.input_queue.wheel_event_count = 0;
            state.input_queue.middle_click_pending = false;
            state.input_queue.keyboard_focus = false;
        }
        if (chart_changed || revision_changed) {
            state.document_context.last_chart_id = view->chart_id;
            state.document_context.last_document_revision = view->document_revision;
        }
        cancel_disabled_compose_paint_modes(view, window_focused, action);
        sync_pointer_hold_acceptance(*view);
        return;
    }

    clear_inactive_compose_panel(action);
}

auto compose_timing_target(const WormUiComposeView& view, double screen_y)
    -> WormUiTimingTarget {
    return WormUiTimingTarget{screen_y, view.viewport_height_pixels,
                              view.timing_document_begin_fraction,
                              view.timing_document_end_fraction, view.downscroll};
}

using wormedit::ui::compose_timing_right;

static bool begin_timing_stage_from_rail_click(const ComposeDoubleClickContext& input,
                                               double click_x, double click_y) {
    const auto& view = *input.view;
    const auto& timing_view = *input.timing_view;
    auto& action = *input.action;
    const auto timing_right = compose_timing_right(view);
    if (timing_view.stage_active || click_x < compose_measure_rail_width ||
        click_x >= timing_right ||
        action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE)
        return false;
    const auto clicked_lane = wormedit::ui::compose_timing_lane_at(view, click_x);
    if (!clicked_lane) return false;
    const auto clicked_timing_lane_ordinal = static_cast<std::size_t>(*clicked_lane);
    const auto* clicked_marker = compose_timing_marker_at(view, click_x, click_y);
    const auto existing_object_id = clicked_marker ? clicked_marker->object_id : 0;
    action.timing_stage_action = WORM_UI_TIMING_STAGE_BEGIN;
    action.timing_chart_id = timing_view.chart_id;
    action.timing_document_revision = timing_view.document_revision;
    action.timing_lane = static_cast<WormUiTimingLaneKind>(clicked_timing_lane_ordinal);
    action.timing_object_id = existing_object_id;
    if (existing_object_id == 0) {
        action.timing_has_target_y = true;
        action.timing_target = clicked_marker && clicked_marker->fixed_initial
            ? WormUiTimingTarget{0.0, 1.0, 0.0, 1.0, false}
            : compose_timing_target(view, click_y);
    }
    if (clicked_marker && existing_object_id != 0) {
        append_compose_input(&action, WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT, click_x,
                             click_y, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, existing_object_id,
                             0, false, clicked_marker->lane, view.chart_id,
                             view.document_revision);
    }
    state.timing_marker.timing_missing_initial_selected = clicked_marker && clicked_marker->fixed_initial &&
                                            existing_object_id == 0;
    if (timing_view.initial_bpm_editable &&
        clicked_timing_lane_ordinal != WORM_UI_TIMING_LANE_KIAI) {
        state.inline_timing.inline_timing_open = true;
        state.inline_timing.inline_timing_focus_pending = true;
        state.inline_timing.inline_timing_chart_id = timing_view.chart_id;
        state.inline_timing.inline_timing_revision = timing_view.document_revision;
        state.inline_timing.inline_timing_object_id = existing_object_id;
        state.inline_timing.inline_timing_lane = static_cast<WormUiTimingLaneKind>(clicked_timing_lane_ordinal);
        state.inline_timing.inline_timing_anchor_screen_x = static_cast<float>(
            input.viewport.x + click_x * input.viewport.w / view.viewport_width_pixels);
        state.inline_timing.inline_timing_anchor_screen_y = static_cast<float>(
            input.viewport.y + click_y * input.viewport.h / view.viewport_height_pixels);
        state.inline_timing.inline_timing_text.fill('\0');
        state.inline_timing.inline_timing_error.fill('\0');
    }
    return true;
}

// A double-click on a bookmark line or label renames it, unless a note sits under the pointer
// (that opens the note label editor).
static bool begin_bookmark_rename_from_double_click(const ComposeDoubleClickContext& input,
                                                    double click_x, double click_y) {
    const auto& view = *input.view;
    if (!view.visible || view.input_occluded || state.input_queue.action_overflowed ||
        compose_point_hits_note(input.view, click_x, click_y)) return false;
    return begin_compose_bookmark_rename_at_double_click(state.bookmark_ui.bookmark_rename, view, click_x,
                                                         click_y);
}

static void arm_compose_stem_source(WormUiWorkspaceAction& action, std::uint64_t resource) {
    set_armed_resource(state, resource, compose_resource_origin_t::sounds);
    worm_ui_nuklear_pick_draw_source(state, action, 1, resource);
}

static auto arm_double_clicked_stem(const ComposeDoubleClickContext& input, double x, double y) -> bool {
    for (const auto& note : std::span(input.view->note_endpoints, input.view->note_endpoint_count)) {
        const auto& bounds = note.bounds;
        if (!note.sound_is_stem || !note.sound_resource_id || note.sound_missing || note.tail || note.proposal_ghost ||
            x < bounds.left || x > bounds.right || y < bounds.top || y > bounds.bottom) continue;
        arm_compose_stem_source(*input.action, note.sound_resource_id);
        return true;
    }
    const auto event_id = compose_bgm_event_at(input.view, x, y);
    const auto events = std::span(input.view->bgm_events, input.view->bgm_event_count);
    const auto event = std::ranges::find(events, event_id, &WormUiComposeBgmEventView::event_id);
    if (!event_id || event == events.end() || !event->armable_stem || !event->sound_id) return false;
    arm_compose_stem_source(*input.action, event->sound_id);
    return true;
}

bool process_compose_double_clicks(const ComposeDoubleClickContext& input,
                                          bool compose_pointer_captured,
                                          bool window_focused) {
    if (input.view->paste_active || compose_area_pointer_consumed() || compose_note_menu_consumed()) return true;
    if (compose_pointer_captured || !window_focused || !input.view->scene_available) return false;
    bool note_double_click_in_view = false;
    for (size_t click_index = 0; click_index < input.click_count; ++click_index) {
        const auto& pointer = input.clicks[click_index];
        const auto& viewport = input.viewport;
        if (pointer.x < viewport.x || pointer.x >= input.editor_input_right ||
            pointer.y < viewport.y || pointer.y >= viewport.y + viewport.h)
            continue;
        const auto click_x = (static_cast<double>(pointer.x) - viewport.x) *
                             input.view->viewport_width_pixels / viewport.w;
        const auto click_y = (static_cast<double>(pointer.y) - viewport.y) *
                             input.view->viewport_height_pixels / viewport.h;
        if (begin_timing_stage_from_rail_click(input, click_x, click_y)) {
            note_double_click_in_view = true;
            continue;
        }
        if (arm_double_clicked_stem(input, click_x, click_y)) {
            note_double_click_in_view = true;
            continue;
        }
        if (begin_bookmark_rename_from_double_click(input, click_x, click_y)) {
            note_double_click_in_view = true;
            continue;
        }
        if (click_x < input.note_field_left || click_x >= input.note_field_right) continue;
        state.note_label.note_label_anchor_x = pointer.x;
        state.note_label.note_label_anchor_y = pointer.y;
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK, click_x,
                             (static_cast<double>(pointer.y) - viewport.y) *
                                 input.view->viewport_height_pixels / viewport.h);
        note_double_click_in_view = true;
    }
    return note_double_click_in_view;
}

static auto timing_marker_capture_matches(const ComposePointerEventContext& input) -> bool {
    return input.window_focused &&
           state.timing_drag.timing_marker_capture.matches(*input.view, input.view->waveform_timing_locked) &&
           !state.input_queue.action_overflowed;
}

static auto update_compose_timing_marker_drag(const ComposePointerEventContext& input) -> bool {
    if (!state.timing_marker.timing_marker_candidate_active) return false;
    if (!timing_marker_capture_matches(input) || (!input.left_down && !input.left_released)) {
        reset_timing_marker_drag_capture();
        return true;
    }
    state.timing_marker.timing_marker_current_y_pixels = input.pointer_y;
    state.timing_marker.timing_marker_drag_active |= state.timing_drag.timing_marker_capture.moved_past_threshold(
        input.context->input.mouse.pos.x, input.context->input.mouse.pos.y);
    if (input.left_released) {
        if (state.timing_marker.timing_marker_drag_active) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE,
                                 input.pointer_x, input.pointer_y, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 state.timing_marker.timing_marker_object_id, 0, false,
                                 state.timing_marker.timing_marker_lane, state.timing_drag.timing_marker_capture.chart,
                                 state.timing_drag.timing_marker_capture.revision,
                                 state.timing_marker.timing_marker_chart_row);
        }
        reset_timing_marker_drag_capture();
        return true;
    }
    return true;
}

// Timing focus: Ctrl+click on empty BPM lane space inserts a BPM equal to the one
// in force; Alt+click on empty Anchor lane space inserts an anchor. Both skip the
// inline editor and leave modifier-clicks on existing markers to selection.
static auto begin_timing_quick_insert(const ComposePointerEventContext& input) -> bool {
    const auto& view = *input.view;
    const auto& timing = *input.timing_view;
    const auto mode = compose_mode_of(view);
    constexpr auto other_modifiers = SDL_KMOD_SHIFT | SDL_KMOD_GUI;
    const auto ctrl = (input.pointer_modifiers & SDL_KMOD_CTRL) != 0;
    const auto alt = (input.pointer_modifiers & SDL_KMOD_ALT) != 0;
    if ((input.pointer_modifiers & other_modifiers) != 0 || ctrl == alt) return false;
    const auto wanted = ctrl ? WORM_UI_TIMING_LANE_BPM : WORM_UI_TIMING_LANE_ANCHOR;
    const auto allowed = ctrl ? wormedit::ui::compose_ctrl_click_inserts_matching_bpm(mode)
                              : wormedit::ui::compose_alt_click_inserts_anchor(mode);
    const auto lane = wormedit::ui::compose_timing_lane_at(view, input.pointer_started_x);
    if (!allowed || !lane || *lane != wanted || !timing.initial_bpm_editable ||
        timing.stage_active || input.action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE)
        return false;
    auto& action = *input.action;
    action.timing_stage_action = WORM_UI_TIMING_STAGE_QUICK_INSERT;
    action.timing_chart_id = timing.chart_id;
    action.timing_document_revision = timing.document_revision;
    action.timing_lane = wanted;
    action.timing_object_id = 0;
    action.timing_has_target_y = true;
    action.timing_target = compose_timing_target(view, input.pointer_started_y);
    return true;
}

static auto begin_compose_timing_marker_drag(const ComposePointerEventContext& input,
                                             bool paint_pointer_consumed) -> bool {
    if (paint_pointer_consumed || !input.view->scene_available || !input.window_focused ||
        !input.pointer_started_in_view || !input.left_down ||
        input.pointer_started_x < compose_measure_rail_width ||
        input.pointer_started_x >= compose_timing_right(*input.view) ||
        input.compose_note_double_click_in_view || state.input_queue.action_overflowed)
        return false;
    const auto* marker = compose_timing_marker_at(*input.view, input.pointer_started_x,
                                                   input.pointer_started_y);
    if (!marker) return begin_timing_quick_insert(input);
    const auto has_modifier = (input.pointer_modifiers & input.any_pointer_modifier) != 0;
    if (marker->object_id == 0) {
        state.timing_marker.timing_missing_initial_selected = true;
    } else {
        state.timing_marker.timing_missing_initial_selected = false;
        if (!marker->selected || has_modifier) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT,
                                 input.pointer_started_x, input.pointer_started_y, 0, false,
                                 compose_selection_mode(input.pointer_modifiers),
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 marker->object_id, 0, false, marker->lane,
                                 input.view->chart_id, input.view->document_revision);
        }
    }
    // Y-lock: these objects only move vertically, so a drag would have nowhere to go.
    if (marker->fixed_initial || marker->object_id == 0 ||
        input.view->waveform_timing_locked || has_modifier || input.view->y_lock) return true;
    const auto& press = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos;
    if (!state.timing_drag.timing_marker_capture.begin(*input.view, press.x, press.y,
                                           input.view->waveform_timing_locked)) return true;
    state.timing_marker.timing_marker_candidate_active = true;
    state.timing_marker.timing_marker_drag_active = false;
    state.timing_marker.timing_marker_object_id = marker->object_id;
    state.timing_marker.timing_marker_chart_row = marker->chart_row;
    state.timing_marker.timing_marker_lane = marker->lane;
    state.timing_marker.timing_marker_anchor_y_pixels = input.pointer_started_y;
    state.timing_marker.timing_marker_current_y_pixels = input.pointer_started_y;
    return true;
}

static bool compose_pointer_start_hits_element(const ComposePointerEventContext& input);

static auto bookmark_move_capture_matches(const ComposePointerEventContext& input) -> bool {
    return input.window_focused && input.view->scene_available && !input.view->input_occluded &&
           input.view->chart_id == state.bookmark_ui.bookmark_move_chart_id &&
           input.view->document_revision == state.bookmark_ui.bookmark_move_revision &&
           !state.input_queue.action_overflowed;
}


// The press was taken for a possible drag, which would have started a paint stroke. A plain
// click on a bookmark row then paints the note as it would anywhere else.
static void replay_bookmark_press_as_paint(const ComposePointerEventContext& input) {
    if (!input.view->note_paint_mode_active || !input.pointer_started_in_note_field ||
        state.input_queue.action_overflowed) return;
    append_note_paint_begin(input);
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
                         input.paint_pointer_x, input.paint_pointer_y);
}

// Returns true while the drag owns the pointer. A press-release without movement is handed back
// (false) so it still works as an ordinary field click.
static auto update_bookmark_move_capture(const ComposePointerEventContext& input) -> bool {
    if (!state.bookmark_ui.bookmark_move_candidate_active) return false;
    if (!bookmark_move_capture_matches(input) || (!input.left_down && !input.left_released)) {
        reset_bookmark_move_capture();
        return true;
    }
    state.bookmark_ui.bookmark_move_current_y_pixels = input.pointer_y;
    if (input.left_released) {
        const auto moved = state.bookmark_ui.bookmark_move_active;
        if (moved)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE,
                input.pointer_x, input.pointer_y, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.bookmark_ui.bookmark_move_id, 0, false,
                WORM_UI_TIMING_LANE_BPM, state.bookmark_ui.bookmark_move_chart_id,
                state.bookmark_ui.bookmark_move_revision);
        reset_bookmark_move_capture();
        if (!moved) replay_bookmark_press_as_paint(input);
        return moved || input.view->note_paint_mode_active;
    }
    if (!state.bookmark_ui.bookmark_move_active && std::abs(input.context->input.mouse.pos.y -
                                                 state.bookmark_ui.bookmark_move_anchor_screen_y) >= 4.0f)
        state.bookmark_ui.bookmark_move_active = true;
    return true;
}

static auto is_beat_bookmark(const WormUiComposeView& view, std::uint64_t id) -> bool {
    return std::ranges::any_of(std::span(view.beat_bookmarks, view.beat_bookmark_count),
                               [id](const auto& item) { return item.bookmark_id == id; });
}

static auto begin_bookmark_move_capture(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !view.visible || view.input_occluded || !input.pointer_started_in_view ||
        !input.left_down || !input.window_focused || view.y_lock ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        input.compose_note_double_click_in_view || state.bookmark_ui.bookmark_move_candidate_active ||
        state.bookmark_ui.bookmark_rename.active || state.bookmark_ui.bookmark_rename.release_pending ||
        state.note_move.note_move_active || state.note_move.note_move_candidate_active || state.area_selection.area_selection_active ||
        state.area_selection.area_selection_candidate_active || state.bga_interaction.bga_event_move_candidate_active ||
        state.timing_marker.timing_marker_candidate_active || compose_pointer_start_hits_element(input))
        return false;
    const auto id = compose_bookmark_at(view, input.pointer_started_x, input.pointer_started_y);
    if (!id || !is_beat_bookmark(view, *id)) return false;
    state.bookmark_ui.bookmark_move_candidate_active = true;
    state.bookmark_ui.bookmark_move_active = false;
    state.bookmark_ui.bookmark_move_id = *id;
    state.bookmark_ui.bookmark_move_chart_id = view.chart_id;
    state.bookmark_ui.bookmark_move_revision = view.document_revision;
    state.bookmark_ui.bookmark_move_current_y_pixels = input.pointer_started_y;
    state.bookmark_ui.bookmark_move_anchor_screen_y = input.left_clicked_screen_y;
    return true;
}

static auto process_compose_bookmark_pointer(const ComposePointerEventContext& input,
                                             bool paint_pointer_consumed) -> bool {
    if (update_bookmark_move_capture(input)) return true;
    return begin_bookmark_move_capture(input, paint_pointer_consumed);
}

auto process_compose_timing_marker_pointer(const ComposePointerEventContext& input,
                                                  bool paint_pointer_consumed) -> bool {
    if (state.bookmark_ui.bookmark_rename.active || state.bookmark_ui.bookmark_rename.release_pending) return true;
    if (compose_area_pointer_consumed() || compose_note_menu_consumed()) return true;
    // Before note painting, so a bookmark can be grabbed while a note tool is armed.
    if (process_compose_bookmark_pointer(input, paint_pointer_consumed)) return true;
    if (update_compose_timing_marker_drag(input)) return true;
    return begin_compose_timing_marker_drag(input, paint_pointer_consumed);
}

static bool compose_pointer_start_hits_element(const ComposePointerEventContext& input) {
    const auto& lane = input.view->bga_lane_bounds;
    const bool bga_lane = input.view->bga_lane_visible &&
        input.pointer_started_x >= lane.left && input.pointer_started_x < lane.right &&
        input.pointer_started_y >= lane.top && input.pointer_started_y < lane.bottom;
    return compose_point_hits_note(input.view, input.pointer_started_x, input.pointer_started_y) ||
        compose_bgm_event_at(input.view, input.pointer_started_x, input.pointer_started_y) != 0 ||
        compose_bga_event_at(input.view, input.pointer_started_x, input.pointer_started_y) != 0 ||
        bga_lane ||
        compose_timing_marker_at(*input.view, input.pointer_started_x, input.pointer_started_y) ||
        wormedit::ui::is_compose_measure_lane_press(*input.view, input.pointer_started_x,
                                                    input.pointer_started_y);
}

static void start_compose_note_move_candidate(const ComposePointerEventContext& input) {
    const auto can_move = wormedit::ui::compose_notes_editable(compose_mode_of(*input.view));
    state.note_move.note_move_select_on_release = can_move && compose_point_hits_selected_note(
        input.view, input.pointer_started_x, input.pointer_started_y);
    if (!state.note_move.note_move_select_on_release)
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             input.pointer_started_x, input.pointer_started_y);
    state.note_move.note_move_candidate_active = can_move && !state.input_queue.action_overflowed;
    state.note_move.note_move_anchor_x = input.pointer_started_x;
    state.note_move.note_move_anchor_y = input.pointer_started_y;
    state.note_move.note_move_anchor_screen_x = input.left_clicked_screen_x;
    state.note_move.note_move_anchor_screen_y = input.left_clicked_screen_y;
}

static auto handle_compose_note_field_pointer_start(const ComposePointerEventContext& input,
                                                    bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !input.pointer_started_in_view || !input.window_focused || input.alt_down ||
        state.note_move.note_move_active || state.note_move.note_move_candidate_active || state.area_selection.area_selection_active ||
        state.area_selection.area_selection_candidate_active || state.timing_drag.kiai_drag_active || state.timing_drag.recipe_drag_active ||
        input.compose_note_double_click_in_view)
        return false;
    const auto selection_mode = compose_selection_mode(input.pointer_modifiers);
    const bool note_hit = input.pointer_started_in_note_field &&
        compose_point_hits_note(&view, input.pointer_started_x, input.pointer_started_y);
    if (note_hit) {
        if (selection_mode != WORM_UI_COMPOSE_SELECTION_REPLACE || !input.left_down)
            return false;
        start_compose_note_move_candidate(input);
        return true;
    }
    if (!input.left_down || compose_pointer_start_hits_element(input)) return false;
    state.area_selection.area_selection_candidate_active = true;
    state.area_selection.area_selection_anchor_x = input.pointer_started_x;
    state.area_selection.area_selection_anchor_y = input.pointer_started_y;
    state.area_selection.area_selection_anchor_screen_x = input.left_clicked_screen_x;
    state.area_selection.area_selection_anchor_screen_y = input.left_clicked_screen_y;
    state.area_selection.area_selection_mode = selection_mode;
    return true;
}

static auto handle_compose_note_field_click(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) -> bool {
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !input.view->scene_available ||
        !input.pointer_clicked || !input.pointer_in_note_field || !input.window_focused ||
        state.pointer_capture.pointer_hold_active || state.note_move.note_move_active || state.note_move.note_move_candidate_active ||
        state.area_selection.area_selection_active || state.area_selection.area_selection_candidate_active ||
        input.compose_note_double_click_in_view)
        return false;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                         input.pointer_x, input.pointer_y, 0, false,
                         compose_selection_mode(input.pointer_modifiers));
    worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *input.action);
    return true;
}

static auto handle_compose_bgm_click(const ComposePointerEventContext& input,
                                     bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    auto* action = input.action;
    const auto bga_event_id = compose_bga_event_at(&view, input.pointer_x, input.pointer_y);
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !input.pointer_clicked || !input.pointer_inside ||
        (input.pointer_x < input.note_field_right && bga_event_id == 0) ||
        !input.window_focused || state.pointer_capture.pointer_hold_active || state.note_move.note_move_active ||
        state.note_move.note_move_candidate_active || state.area_selection.area_selection_active ||
        state.area_selection.area_selection_candidate_active)
        return false;
    if (bga_event_id != 0) {
        const auto stacked = std::ranges::find(
            std::span(view.bga_events, view.bga_event_count), bga_event_id,
            &WormUiComposeBgaEventView::event_id);
        if (stacked != std::span(view.bga_events, view.bga_event_count).end() &&
            stacked->overlap_count > WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) {
            state.bga_interaction.bga_stack_release_pending = true;
            state.bga_interaction.bga_stack_open = true;
            state.bga_interaction.bga_stack_project_id = view.project_id;
            state.bga_interaction.bga_stack_chart_id = view.chart_id;
            state.bga_interaction.bga_stack_revision = view.document_revision;
            state.bga_interaction.bga_stack_row = stacked->canvas_row;
            state.bga_interaction.bga_stack_kind = stacked->kind;
            return true;
        }
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK,
            input.pointer_x, input.pointer_y, 0, false,
            compose_selection_mode(input.pointer_modifiers),
            WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, bga_event_id, 0, false,
            WORM_UI_TIMING_LANE_BPM, view.chart_id, view.document_revision);
        return true;
    }
    if (state.bmson_input.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off &&
        bmson_razor_eligible(view) &&
        selected_bmson_lane_at(input, input.pointer_x, input.pointer_y) &&
        (input.pointer_modifiers & input.any_pointer_modifier) == 0) {
        append_compose_input(action, bmson_razor_input_kind(), input.pointer_x, input.pointer_y,
                             0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                             view.selected_bmson_bgm_event_id, 0, true);
        return true;
    }
    const auto event_id = compose_bgm_event_at(&view, input.pointer_x, input.pointer_y);
    if (event_id == 0) return true;
    const auto event = std::find_if(view.bgm_events, view.bgm_events + view.bgm_event_count,
                                    [event_id](const auto& item) {
                                        return item.event_id == event_id;
                                    });
    const auto shift_restart = event != view.bgm_events + view.bgm_event_count &&
                               event->bmson_slice &&
                               compose_pointer_chord(input.pointer_modifiers) == compose_pointer_chord_t::add;
    append_compose_input(action, shift_restart ? WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE
                                               : WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
                         input.pointer_x, input.pointer_y, 0, false,
                         compose_selection_mode(input.pointer_modifiers),
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, event_id);
    return true;
}

static auto update_bga_event_move_capture(const ComposePointerEventContext& input) -> bool {
    if (!state.bga_interaction.bga_event_move_candidate_active) return false;
    const bool valid = input.window_focused && state.timing_drag.bga_capture.matches(*input.view) &&
        state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
        !state.input_queue.action_overflowed;
    if (!valid || (!input.left_down && !input.left_released)) {
        reset_bga_event_move_capture();
        return true;
    }
    state.bga_interaction.bga_event_move_current_y_pixels = input.pointer_y;
    state.bga_interaction.bga_event_move_active |= state.timing_drag.bga_capture.moved_past_threshold(
        input.context->input.mouse.pos.x, input.context->input.mouse.pos.y, true);
    state.bga_interaction.bga_event_move_current_x_pixels = input.pointer_x;
    if (input.left_released) {
        if (!state.bga_interaction.bga_event_move_active) {
            const auto events = std::span(input.view->bga_events, input.view->bga_event_count);
            const auto selected = std::ranges::find(events, state.bga_interaction.bga_event_move_object_id,
                &WormUiComposeBgaEventView::event_id);
            if (selected != events.end() && selected->overlap_count >
                    WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) {
                state.bga_interaction.bga_stack_release_pending = true;
                state.bga_interaction.bga_stack_open = true;
                state.bga_interaction.bga_stack_project_id = input.view->project_id;
                state.bga_interaction.bga_stack_chart_id = input.view->chart_id;
                state.bga_interaction.bga_stack_revision = input.view->document_revision;
                state.bga_interaction.bga_stack_row = selected->canvas_row;
                state.bga_interaction.bga_stack_kind = selected->kind;
            }
        }
        if (state.bga_interaction.bga_event_move_active) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE,
                input.pointer_x, input.pointer_y, 0, false,
                WORM_UI_COMPOSE_SELECTION_REPLACE,
                WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                state.bga_interaction.bga_event_move_object_id, 0, false, WORM_UI_TIMING_LANE_BPM,
                state.timing_drag.bga_capture.chart, state.timing_drag.bga_capture.revision,
                state.bga_interaction.bga_event_move_anchor_row);
            if (input.action->compose_input_count != 0) {
                auto& output = input.action->compose_inputs[input.action->compose_input_count - 1];
                output.bga_lane_kind = compose_bga_lane_kind_at(*input.view,
                    input.pointer_x, input.pointer_y).value_or(255);
                output.bga_lane_kind_valid = true;
            }
        }
        reset_bga_event_move_capture();
        return true;
    }
    return true;
}

static auto begin_bga_event_move_capture(const ComposePointerEventContext& input,
                                         bool paint_pointer_consumed) -> bool {
    if (state.input_queue.action_overflowed || !input.view->scene_available || !input.pointer_started_in_view ||
        paint_pointer_consumed || input.view->y_lock ||
        state.resource_picker.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        !input.left_down || !input.window_focused || state.bga_interaction.bga_event_move_candidate_active ||
        state.note_move.note_move_active || state.note_move.note_move_candidate_active || state.area_selection.area_selection_active ||
        state.area_selection.area_selection_candidate_active)
        return false;
    const auto id = compose_bga_event_at(input.view, input.pointer_started_x,
                                         input.pointer_started_y);
    if (id == 0) return false;
    const auto events = std::span(input.view->bga_events, input.view->bga_event_count);
    const auto event = std::ranges::find(events, id, &WormUiComposeBgaEventView::event_id);
    if (event == events.end() || !event->selected)
        return false;
    const auto& press = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos;
    if (!state.timing_drag.bga_capture.begin(*input.view, press.x, press.y, false)) return false;
    state.bga_interaction.bga_event_move_candidate_active = true;
    state.bga_interaction.bga_event_move_object_id = id;
    state.bga_interaction.bga_event_move_anchor_row = event->canvas_row;
    state.bga_interaction.bga_event_move_anchor_y_pixels = input.pointer_started_y;
    state.bga_interaction.bga_event_move_current_y_pixels = input.pointer_started_y;
    state.bga_interaction.bga_event_move_current_x_pixels = input.pointer_started_x;
    return true;
}


static auto process_compose_bga_event_pointer(const ComposePointerEventContext& input,
                                              bool paint_pointer_consumed) -> bool {
    if (update_bga_event_move_capture(input)) return true;
    return begin_bga_event_move_capture(input, paint_pointer_consumed);
}

void update_compose_sound_hover(const ComposePointerEventContext& input) {
    input.action->compose_sound_hover_object_id = !state.input_queue.action_overflowed && input.window_focused &&
        input.view->scene_available && input.pointer_inside && !input.compose_pointer_captured
        ? compose_sound_object_at(*input.view, input.pointer_x, input.pointer_y) : 0;
}

auto handle_same_sound_pointer(const ComposePointerEventContext& input) -> bool {
    if (!compose_pointer_same_sound(input.pointer_modifiers) ||
        !input.window_focused || input.compose_pointer_captured ||
        !input.view->scene_available || state.input_queue.action_overflowed || !input.pointer_inside) return false;
    const auto object = compose_sound_object_at(*input.view, input.pointer_started_x, input.pointer_started_y);
    if (!object) return false;
    if (input.pointer_clicked)
        append_compose_input(input.action,
            compose_pointer_chord(input.pointer_modifiers) == compose_pointer_chord_t::same_visible
                ? WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE :
            WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART, input.pointer_x, input.pointer_y, 0, false,
            WORM_UI_COMPOSE_SELECTION_TOGGLE, WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, object);
    return input.left_down || input.left_released || input.pointer_clicked;
}

void process_compose_pointer_actions(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) {
    if (input.view->paste_active) {
        if (input.window_focused && input.pointer_inside && input.pointer_in_note_field) {
            const auto delta = input.context->input.mouse.delta;
            if (delta.x != 0.0f || delta.y != 0.0f || input.left_released)
                append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
                                     input.pointer_x, input.pointer_y);
            if (input.left_released && input.pointer_started_in_view)
                append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_PASTE_COMMIT);
        }
        return;
    }
    if (compose_area_pointer_consumed() || compose_note_menu_consumed()) return;
    if (process_compose_bga_event_pointer(input, paint_pointer_consumed)) return;
    if (handle_compose_measure_click(input, paint_pointer_consumed)) return;
    if (handle_compose_alt_pointer(input, paint_pointer_consumed)) return;
    if (handle_compose_note_field_pointer_start(input, paint_pointer_consumed)) return;
    if (handle_compose_note_field_click(input, paint_pointer_consumed)) return;
    handle_compose_bgm_click(input, paint_pointer_consumed);
}
