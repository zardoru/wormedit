#include "ui/nuklear/compose_panel.h"
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

void reset_bmson_marker_move_capture() {
    state.bmson_marker_move_candidate_active = false;
    state.bmson_marker_move_active = false;
    state.bmson_marker_move_event_id = 0;
}

static void reset_timing_marker_drag_capture() {
    state.timing_marker_candidate_active = false;
    state.timing_marker_drag_active = false;
    state.timing_marker_object_id = 0;
}

static void reset_bga_event_move_capture() {
    state.bga_event_move_candidate_active = false;
    state.bga_event_move_active = false;
    state.bga_event_move_object_id = 0;
}

void set_waveform_offset_drag_action(WormUiWorkspaceAction* action,
                                            WormUiAudioOffsetDragAction kind,
                                            std::int64_t delta_frames) {
    if (!action || action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE)
        return;
    action->audio_offset_drag_action = kind;
    action->audio_offset_drag_chart_id = state.waveform_beat_zero_drag_chart_id;
    action->audio_offset_drag_document_revision = state.waveform_beat_zero_drag_revision;
    action->audio_offset_drag_sample_rate = state.waveform_beat_zero_drag_sample_rate;
    action->audio_offset_drag_delta_frames = delta_frames;
}

void reset_waveform_offset_drag() {
    state.waveform_beat_zero_drag_active = false;
    state.waveform_beat_zero_drag_delta_frames = 0;
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
        state.cancel_pending = false;
        state.pointer_hold_active = false;
        state.note_paint_pointer_active = false;
        state.bmson_razor_pointer_active = false;
        reset_bmson_marker_move_capture();
        state.semantic_layer_paint_pointer_active = false;
        state.lane_hold_active = false;
        state.space_range_hold_active = false;
        state.space_range_latched = false;
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
        reset_bga_event_move_capture();
        state.timing_stage_drag_active = false;
        state.kiai_drag_active = false;
        state.recipe_drag_active = false;
        state.beat_fit_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.navigation_key_active = false;
        state.navigation_keycode = 0;
        state.pointer_navigation_active = false;
        state.keyboard_focus = false;
        state.lane_hold_count = 0;
        state.tab_navigation.cancel();
        state.raw_tab_down = false;
        state.pinch = {};
        state.action_overflowed = true;
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
    const auto index = std::min<std::size_t>(state.beat_fit_snap_step_index,
                                             beat_fit_snap_steps.size() - 1);
    event.beat_fit_snap_step_numerator = beat_fit_snap_steps[index].numerator;
    event.beat_fit_snap_step_denominator = beat_fit_snap_steps[index].denominator;
}

void cancel_beat_fit_drag(WormUiWorkspaceAction* action) {
    if (state.beat_fit_drag_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
        state.beat_fit_drag_active = false;
        state.beat_fit_anchor_screen_y = 0.0f;
    }
}

void cancel_offset_fit_drag(WormUiWorkspaceAction* action) {
    if (!state.offset_fit_drag_active) return;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL);
    state.offset_fit_drag_active = false;
    state.beat_fit_anchor_screen_y = 0.0f;
}

void
update_compose_pointer_navigation(WormUiWorkspaceAction* action,
                                  std::optional<WormUiComposeNavigationAction> desired_action) {
    if (!action) {
        return;
    }
    if (state.pointer_navigation_active &&
        (!desired_action || *desired_action != state.pointer_navigation_action)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE, state.pointer_navigation_action);
        state.pointer_navigation_active = false;
    }
    if (desired_action && !state.pointer_navigation_active) {
        if (state.navigation_key_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE, state.navigation_action);
            state.navigation_key_active = false;
            state.navigation_keycode = 0;
        }
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE, *desired_action);
        state.pointer_navigation_active = !state.action_overflowed;
        state.pointer_navigation_action = *desired_action;
    }
}

void cancel_compose_navigation(WormUiWorkspaceAction* action) {
    state.tab_navigation.cancel();
    state.raw_tab_down = false;
    state.pinch = {};
    if (action && (state.navigation_key_active || state.pointer_navigation_active)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
    }
    state.navigation_key_active = false;
    state.navigation_keycode = 0;
    state.pointer_navigation_active = false;
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
    if (state.wheel_event_count == state.wheel_events.size()) {
        state.wheel_event_count = 0;
        state.cancel_pending = true;
        state.tab_navigation.cancel();
        state.raw_tab_down = false;
        state.pinch = {};
        state.key_event_count = 0;
        state.pointer_hold_active = false;
        state.note_paint_pointer_active = false;
        state.bmson_razor_pointer_active = false;
        reset_bmson_marker_move_capture();
        state.semantic_layer_paint_pointer_active = false;
        state.lane_hold_active = false;
        state.space_range_hold_active = false;
        state.space_range_latched = false;
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
        reset_bga_event_move_capture();
        state.timing_stage_drag_active = false;
        state.kiai_drag_active = false;
        state.recipe_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.keyboard_focus = false;
        state.lane_hold_count = 0;
        return;
    }
    state.wheel_events[state.wheel_event_count++] = {
        .delta_x = delta_x,
        .delta_y = delta_y,
        .mouse_x = mouse_x,
        .mouse_y = mouse_y,
        .modifiers = modifiers,
        .coarse_steps = coarse_steps,
        .tab_speed_modifier = state.raw_tab_down,
    };
}

void queue_compose_pinch_input(worm_ui_nuklear_pinch_phase_t phase,
                               double factor, double mouse_x, double mouse_y) {
    if (state.cancel_pending) { state.pinch = {}; return; }
    worm_ui_nuklear_pinch_input(state.pinch, phase, factor, mouse_x, mouse_y,
                               state.last_project_id, state.last_chart_id);
}

void queue_compose_middle_click_input(float x, float y) {
    state.middle_click_pending = true;
    state.middle_click_x = x;
    state.middle_click_y = y;
}

void compose_handle_focus_lost_input() {
    state.alt_wheel_last_event_ms.reset();
    state.tab_navigation.cancel();
    state.raw_tab_down = false;
    state.pinch = {};
    state.cancel_pending = true;
    state.key_event_count = 0;
    state.wheel_event_count = 0;
    state.middle_click_pending = false;
    state.keyboard_focus = false;
    state.pointer_hold_active = false;
    state.bmson_razor_pointer_active = false;
    reset_bmson_marker_move_capture();
    // Keep paint capture until Compose can deliver the pending cancel.
    state.lane_hold_active = false;
    state.lane_hold_count = 0;
    state.space_range_hold_active = false;
    state.space_range_latched = false;
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
    reset_bga_event_move_capture();
    state.timing_stage_drag_active = false;
    state.kiai_drag_active = false;
    state.recipe_drag_active = false;
    reset_timing_marker_drag_capture();
    // Deliver the timing cancellation through prepare_compose_panel_input.
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
    state.navigation_key_active = false;
    state.navigation_keycode = 0;
    state.pointer_navigation_active = false;
}

void discard_compose_frame_input() {
    state.pinch = {};
    state.wheel_event_count = 0;
    state.middle_click_pending = false;
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
        if (x >= event.bounds.left && x <= event.bounds.right && y >= event.bounds.top &&
            y <= event.bounds.bottom) {
            return event.event_id;
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
    if (compose_shortcut_modifier(modifiers)) {
        return WORM_UI_COMPOSE_SELECTION_TOGGLE;
    }
    if ((modifiers & SDL_KMOD_SHIFT) != 0) {
        return WORM_UI_COMPOSE_SELECTION_ADD;
    }
    return WORM_UI_COMPOSE_SELECTION_REPLACE;
}

static void cancel_pending_compose_panel_actions(WormUiWorkspaceAction* action) {
    if (state.inline_timing_open) worm_ui_nuklear_inline_timing_cancel(state, *action);
    cancel_beat_fit_drag(action);
    cancel_offset_fit_drag(action);
    if (state.waveform_beat_zero_drag_active) {
        set_waveform_offset_drag_action(action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                                        state.waveform_beat_zero_drag_delta_frames);
        reset_waveform_offset_drag();
    }
    state.waveform_overview_drag_active = false;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
    state.cancel_pending = false;
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
    state.note_paint_pointer_active = false;
    state.bmson_razor_pointer_active = false;
    reset_bmson_marker_move_capture();
    state.semantic_layer_paint_pointer_active = false;
    state.space_range_hold_active = false;
    state.space_range_latched = false;
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
    reset_bga_event_move_capture();
    state.timing_stage_drag_active = false;
    state.kiai_drag_active = false;
    state.recipe_drag_active = false;
    reset_timing_marker_drag_capture();
    state.inline_timing_open = false;
}

static void reset_compose_revision_state(bool chart_changed, WormUiWorkspaceAction* action) {
    state.alt_wheel_last_event_ms.reset();
    state.bgm_column_count_edit = {};
    cancel_beat_fit_drag(action);
    if (state.waveform_beat_zero_drag_active) {
        set_waveform_offset_drag_action(action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                                        state.waveform_beat_zero_drag_delta_frames);
        reset_waveform_offset_drag();
    }
    state.waveform_overview_drag_active = false;
    if (state.note_paint_pointer_active || state.semantic_layer_paint_pointer_active ||
        state.space_range_hold_active || state.note_move_active || state.bmson_marker_move_active ||
        state.bga_event_move_active)
        append_compose_cancel_once(action);
    state.pointer_hold_active = false;
    state.note_paint_pointer_active = false;
    state.bmson_razor_pointer_active = false;
    reset_bmson_marker_move_capture();
    state.semantic_layer_paint_pointer_active = false;
    if (chart_changed) state.lane_hold_active = false;
    state.space_range_hold_active = false;
    state.space_range_latched = false;
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
    reset_bga_event_move_capture();
    state.timing_stage_drag_active = false;
    state.kiai_drag_active = false;
    state.recipe_drag_active = false;
    reset_timing_marker_drag_capture();
    state.inline_timing_open = false;
    state.timing_missing_initial_selected = false;
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
    state.semantic_layer_color_editor_visible = false;
    if (chart_changed) state.lane_hold_count = 0;
    cancel_compose_navigation(action);
}

static void clear_inactive_compose_panel(WormUiWorkspaceAction* action) {
    state.alt_wheel_last_event_ms.reset();
    state.bgm_column_count_edit = {};
    state.cancel_pending = false;
    cancel_beat_fit_drag(action);
    state.key_event_count = 0;
    state.wheel_event_count = 0;
    state.middle_click_pending = false;
    state.keyboard_focus = false;
    if (state.pointer_hold_active || state.lane_hold_active || state.note_paint_pointer_active ||
        state.semantic_layer_paint_pointer_active || state.space_range_hold_active ||
        state.area_selection_active)
        append_compose_cancel_once(action);
    state.pointer_hold_active = false;
    state.note_paint_pointer_active = false;
    state.semantic_layer_paint_pointer_active = false;
    state.lane_hold_active = false;
    state.lane_hold_count = 0;
    state.space_range_hold_active = false;
    state.space_range_latched = false;
    state.space_range_hold_moved = false;
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
    reset_bga_event_move_capture();
    state.timing_stage_drag_active = false;
    state.kiai_drag_active = false;
    state.recipe_drag_active = false;
    reset_timing_marker_drag_capture();
    state.inline_timing_open = false;
    state.timing_missing_initial_selected = false;
    state.semantic_layer_color_editor_visible = false;
    cancel_compose_navigation(action);
    state.last_chart_id = 0;
    state.last_document_revision = 0;
}

static void cancel_disabled_compose_paint_modes(const WormUiComposeView* view,
                                                bool window_focused,
                                                WormUiWorkspaceAction* action) {
    if (state.note_paint_pointer_active &&
        (!window_focused || !view->scene_available || !view->note_paint_mode_active)) {
        append_compose_cancel_once(action);
        state.note_paint_pointer_active = false;
    }
    if (state.semantic_layer_paint_pointer_active &&
        (!window_focused || !view->scene_available || !view->semantic_layer_paint_mode_active)) {
        append_compose_cancel_once(action);
        state.semantic_layer_paint_pointer_active = false;
    }
}

void prepare_compose_panel_input(const WormUiComposeView* view, bool compose_tab_active,
                                           bool window_focused, WormUiWorkspaceAction* action) {
    if (!action) {
        return;
    }
    prepare_compose_note_menu(view, compose_tab_active, window_focused, action);
    if (!compose_tab_active || !view || !view->visible) reset_compose_area_menu();
    if (view && compose_tab_active && view->visible) {
        state.action_overflowed = false;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
        action->compose_input_viewport_width_pixels = view->viewport_width_pixels;
        if (!view->scene_available || view->input_occluded || !window_focused) {
            cancel_compose_navigation(action);
        }
        if (state.cancel_pending) cancel_pending_compose_panel_actions(action);
        const auto project_changed = state.last_project_id != view->project_id;
        const auto chart_changed = project_changed || state.last_chart_id != view->chart_id;
        const auto revision_changed = state.last_document_revision != view->document_revision;
        if (project_changed) {
            state.armed_resource_id = 0;
            state.auto_increment_on_place = false;
            state.resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
            state.last_project_id = view->project_id;
        }
        if (chart_changed || revision_changed) reset_compose_revision_state(chart_changed, action);
        if (chart_changed) {
            state.resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        }
        if (chart_changed) {
            // Events received before the first frame for another chart still
            // target the previously visible geometry. Events after an ordinary
            // document edit already belong to the current frame/revision.
            state.key_event_count = 0;
            state.wheel_event_count = 0;
            state.middle_click_pending = false;
            state.keyboard_focus = false;
        }
        if (chart_changed || revision_changed) {
            state.last_chart_id = view->chart_id;
            state.last_document_revision = view->document_revision;
        }
        cancel_disabled_compose_paint_modes(view, window_focused, action);
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

// Timing lane under a horizontal pixel position, honoring the lane mask in force.
static auto compose_timing_lane_at_x(const WormUiComposeView& view, double x,
                                     double field_left) -> std::optional<WormUiTimingLaneKind> {
    const auto visible_lane_count =
        static_cast<std::size_t>(std::popcount(view.timing_lane_visibility_mask));
    if (visible_lane_count == 0) return std::nullopt;
    const auto lane_width = (field_left - compose_measure_rail_width) /
                            static_cast<double>(visible_lane_count);
    if (lane_width <= 0.0) return std::nullopt;
    const auto visible_slot = std::min(
        static_cast<std::size_t>((x - compose_measure_rail_width) / lane_width),
        visible_lane_count - 1);
    std::size_t slot = 0;
    for (std::size_t candidate = 0; candidate < WORM_UI_TIMING_LANE_COUNT; ++candidate) {
        if ((view.timing_lane_visibility_mask & (1U << candidate)) == 0) continue;
        if (slot++ == visible_slot) return static_cast<WormUiTimingLaneKind>(candidate);
    }
    return std::nullopt;
}

static auto compose_timing_right(const WormUiComposeView& view) -> double {
    return view.bga_lane_visible ? view.bga_lane_bounds.left
                                 : view.playable_field_left_pixels;
}

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
    const auto clicked_lane = compose_timing_lane_at_x(view, click_x, timing_right);
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
    state.timing_missing_initial_selected = clicked_marker && clicked_marker->fixed_initial &&
                                            existing_object_id == 0;
    if (timing_view.initial_bpm_editable &&
        clicked_timing_lane_ordinal != WORM_UI_TIMING_LANE_KIAI) {
        state.inline_timing_open = true;
        state.inline_timing_focus_pending = true;
        state.inline_timing_chart_id = timing_view.chart_id;
        state.inline_timing_revision = timing_view.document_revision;
        state.inline_timing_object_id = existing_object_id;
        state.inline_timing_lane = static_cast<WormUiTimingLaneKind>(clicked_timing_lane_ordinal);
        state.inline_timing_anchor_screen_x = static_cast<float>(
            input.viewport.x + click_x * input.viewport.w / view.viewport_width_pixels);
        state.inline_timing_anchor_screen_y = static_cast<float>(
            input.viewport.y + click_y * input.viewport.h / view.viewport_height_pixels);
        state.inline_timing_text.fill('\0');
        state.inline_timing_error.fill('\0');
    }
    return true;
}

// A double-click on a bookmark line or label renames it, unless a note sits under the pointer
// (that opens the note label editor).
static bool begin_bookmark_rename_from_double_click(const ComposeDoubleClickContext& input,
                                                    double click_x, double click_y) {
    const auto& view = *input.view;
    if (!view.visible || view.input_occluded || state.action_overflowed ||
        compose_point_hits_note(input.view, click_x, click_y)) return false;
    return begin_compose_bookmark_rename_at_double_click(state.bookmark_rename, view, click_x,
                                                         click_y);
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
        if (begin_bookmark_rename_from_double_click(input, click_x, click_y)) {
            note_double_click_in_view = true;
            continue;
        }
        if (click_x < input.note_field_left || click_x >= input.note_field_right) continue;
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK, click_x,
                             (static_cast<double>(pointer.y) - viewport.y) *
                                 input.view->viewport_height_pixels / viewport.h);
        note_double_click_in_view = true;
    }
    return note_double_click_in_view;
}

static auto timing_marker_capture_matches(const ComposePointerEventContext& input) -> bool {
    return input.window_focused && input.view->scene_available &&
           input.view->chart_id == state.timing_marker_chart_id &&
           input.view->document_revision == state.timing_marker_revision &&
           !input.view->waveform_timing_locked && !state.action_overflowed;
}

static auto update_compose_timing_marker_drag(const ComposePointerEventContext& input) -> bool {
    if (!state.timing_marker_candidate_active) return false;
    if (!timing_marker_capture_matches(input) || (!input.left_down && !input.left_released)) {
        reset_timing_marker_drag_capture();
        return true;
    }
    state.timing_marker_current_y_pixels = input.pointer_y;
    if (input.left_released) {
        if (state.timing_marker_drag_active) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE,
                                 input.pointer_x, input.pointer_y, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 state.timing_marker_object_id, 0, false,
                                 state.timing_marker_lane, state.timing_marker_chart_id,
                                 state.timing_marker_revision,
                                 state.timing_marker_chart_row);
        }
        reset_timing_marker_drag_capture();
        return true;
    }
    if (!state.timing_marker_drag_active &&
        std::abs(input.context->input.mouse.pos.y - state.timing_marker_anchor_screen_y) >= 4.0f)
        state.timing_marker_drag_active = true;
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
    const auto lane = compose_timing_lane_at_x(view, input.pointer_started_x,
                                               compose_timing_right(view));
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
        input.compose_note_double_click_in_view || state.action_overflowed)
        return false;
    const auto* marker = compose_timing_marker_at(*input.view, input.pointer_started_x,
                                                   input.pointer_started_y);
    if (!marker) return begin_timing_quick_insert(input);
    const auto has_modifier = (input.pointer_modifiers & input.any_pointer_modifier) != 0;
    if (marker->object_id == 0) {
        state.timing_missing_initial_selected = true;
    } else {
        state.timing_missing_initial_selected = false;
        if (!marker->selected || has_modifier) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT,
                                 input.pointer_started_x, input.pointer_started_y, 0, false,
                                 compose_selection_mode(input.pointer_modifiers),
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 marker->object_id, 0, false, marker->lane,
                                 input.view->chart_id, input.view->document_revision);
        }
    }
    if (marker->fixed_initial || marker->object_id == 0 ||
        input.view->waveform_timing_locked || has_modifier) return true;
    state.timing_marker_candidate_active = true;
    state.timing_marker_drag_active = false;
    state.timing_marker_object_id = marker->object_id;
    state.timing_marker_chart_row = marker->chart_row;
    state.timing_marker_lane = marker->lane;
    state.timing_marker_anchor_y_pixels = input.pointer_started_y;
    state.timing_marker_current_y_pixels = input.pointer_started_y;
    state.timing_marker_anchor_screen_y = input.context->input.mouse.pos.y;
    state.timing_marker_chart_id = input.view->chart_id;
    state.timing_marker_revision = input.view->document_revision;
    return true;
}

auto process_compose_timing_marker_pointer(const ComposePointerEventContext& input,
                                                  bool paint_pointer_consumed) -> bool {
    if (state.bookmark_rename.active || state.bookmark_rename.release_pending) return true;
    if (compose_area_pointer_consumed() || compose_note_menu_consumed()) return true;
    const auto rename_press = !paint_pointer_consumed &&
        nk_input_is_mouse_pressed(&input.context->input, NK_BUTTON_LEFT) && input.left_down &&
        input.pointer_started_in_view && input.window_focused && input.view->visible &&
        input.view->scene_available && !input.view->input_occluded &&
        input.pointer_modifiers == 0 && !state.action_overflowed;
    if (rename_press && begin_compose_bookmark_rename_at_pointer(
            state.bookmark_rename, *input.view, input.pointer_started_x,
            input.pointer_started_y, true, true))
        return true;
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
        compose_timing_marker_at(*input.view, input.pointer_started_x, input.pointer_started_y);
}

static void start_compose_note_move_candidate(const ComposePointerEventContext& input) {
    const auto can_move = wormedit::ui::compose_notes_editable(compose_mode_of(*input.view));
    state.note_move_select_on_release = can_move && compose_point_hits_selected_note(
        input.view, input.pointer_started_x, input.pointer_started_y);
    if (!state.note_move_select_on_release)
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             input.pointer_started_x, input.pointer_started_y);
    state.note_move_candidate_active = can_move && !state.action_overflowed;
    state.note_move_anchor_x = input.pointer_started_x;
    state.note_move_anchor_y = input.pointer_started_y;
    state.note_move_anchor_screen_x = input.left_clicked_screen_x;
    state.note_move_anchor_screen_y = input.left_clicked_screen_y;
}

static auto handle_compose_note_field_pointer_start(const ComposePointerEventContext& input,
                                                    bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    if (state.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !input.pointer_started_in_view || !input.window_focused || input.alt_down ||
        state.note_move_active || state.note_move_candidate_active || state.area_selection_active ||
        state.area_selection_candidate_active || state.kiai_drag_active || state.recipe_drag_active ||
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
    state.area_selection_candidate_active = true;
    state.area_selection_anchor_x = input.pointer_started_x;
    state.area_selection_anchor_y = input.pointer_started_y;
    state.area_selection_anchor_screen_x = input.left_clicked_screen_x;
    state.area_selection_anchor_screen_y = input.left_clicked_screen_y;
    state.area_selection_mode = selection_mode;
    return true;
}

static auto handle_compose_note_field_click(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || paint_pointer_consumed || !input.view->scene_available ||
        !input.pointer_clicked || !input.pointer_in_note_field || !input.window_focused ||
        state.pointer_hold_active || state.note_move_active || state.note_move_candidate_active ||
        state.area_selection_active || state.area_selection_candidate_active ||
        input.compose_note_double_click_in_view)
        return false;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                         input.pointer_x, input.pointer_y, 0, false,
                         compose_selection_mode(input.pointer_modifiers));
    return true;
}

static auto handle_compose_bgm_click(const ComposePointerEventContext& input,
                                     bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    auto* action = input.action;
    const auto bga_event_id = compose_bga_event_at(&view, input.pointer_x, input.pointer_y);
    if (state.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !input.pointer_clicked || !input.pointer_inside ||
        (input.pointer_x < input.note_field_right && bga_event_id == 0) ||
        !input.window_focused || state.pointer_hold_active || state.note_move_active ||
        state.note_move_candidate_active || state.area_selection_active ||
        state.area_selection_candidate_active)
        return false;
    if (bga_event_id != 0) {
        const auto stacked = std::ranges::find(
            std::span(view.bga_events, view.bga_event_count), bga_event_id,
            &WormUiComposeBgaEventView::event_id);
        if (stacked != std::span(view.bga_events, view.bga_event_count).end() &&
            stacked->overlap_count > WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) {
            state.bga_stack_release_pending = true;
            state.bga_stack_open = true;
            state.bga_stack_project_id = view.project_id;
            state.bga_stack_chart_id = view.chart_id;
            state.bga_stack_revision = view.document_revision;
            state.bga_stack_row = stacked->canvas_row;
            return true;
        }
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK,
            input.pointer_x, input.pointer_y, 0, false,
            compose_selection_mode(input.pointer_modifiers),
            WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, bga_event_id, 0, false,
            WORM_UI_TIMING_LANE_BPM, view.chart_id, view.document_revision);
        return true;
    }
    if (state.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off &&
        view.selected_bmson_bgm_event_id != 0 &&
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
                               (input.pointer_modifiers & SDL_KMOD_SHIFT) != 0 &&
                               (input.pointer_modifiers &
                                (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;
    append_compose_input(action, shift_restart ? WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE
                                               : WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
                         input.pointer_x, input.pointer_y, 0, false,
                         compose_selection_mode(input.pointer_modifiers),
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, event_id);
    return true;
}

static auto update_bga_event_move_capture(const ComposePointerEventContext& input) -> bool {
    if (!state.bga_event_move_candidate_active) return false;
    const bool valid = input.window_focused && input.view->scene_available &&
        input.view->chart_id == state.bga_event_move_chart_id &&
        input.view->document_revision == state.bga_event_move_revision &&
        state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
        !state.action_overflowed;
    if (!valid || (!input.left_down && !input.left_released)) {
        reset_bga_event_move_capture();
        return true;
    }
    state.bga_event_move_current_y_pixels = input.pointer_y;
    if (input.left_released) {
        if (!state.bga_event_move_active) {
            const auto events = std::span(input.view->bga_events, input.view->bga_event_count);
            const auto selected = std::ranges::find(events, state.bga_event_move_object_id,
                &WormUiComposeBgaEventView::event_id);
            if (selected != events.end() && selected->overlap_count >
                    WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) {
                state.bga_stack_release_pending = true;
                state.bga_stack_open = true;
                state.bga_stack_project_id = input.view->project_id;
                state.bga_stack_chart_id = input.view->chart_id;
                state.bga_stack_revision = input.view->document_revision;
                state.bga_stack_row = selected->canvas_row;
            }
        }
        if (state.bga_event_move_active)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE,
                input.pointer_x, input.pointer_y, 0, false,
                WORM_UI_COMPOSE_SELECTION_REPLACE,
                WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                state.bga_event_move_object_id, 0, false, WORM_UI_TIMING_LANE_BPM,
                state.bga_event_move_chart_id, state.bga_event_move_revision,
                state.bga_event_move_anchor_row);
        reset_bga_event_move_capture();
        return true;
    }
    if (!state.bga_event_move_active && std::abs(input.context->input.mouse.pos.y -
                                                  state.bga_event_move_anchor_screen_y) >= 4.0f)
        state.bga_event_move_active = true;
    return true;
}

static auto begin_bga_event_move_capture(const ComposePointerEventContext& input,
                                         bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || !input.view->scene_available || !input.pointer_started_in_view ||
        paint_pointer_consumed ||
        state.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        !input.left_down || !input.window_focused || state.bga_event_move_candidate_active ||
        state.note_move_active || state.note_move_candidate_active || state.area_selection_active ||
        state.area_selection_candidate_active)
        return false;
    const auto id = compose_bga_event_at(input.view, input.pointer_started_x,
                                         input.pointer_started_y);
    if (id == 0) return false;
    const auto events = std::span(input.view->bga_events, input.view->bga_event_count);
    const auto event = std::ranges::find(events, id, &WormUiComposeBgaEventView::event_id);
    if (event == events.end() || !event->selected)
        return false;
    state.bga_event_move_candidate_active = true;
    state.bga_event_move_object_id = id;
    state.bga_event_move_chart_id = input.view->chart_id;
    state.bga_event_move_revision = input.view->document_revision;
    state.bga_event_move_anchor_row = event->canvas_row;
    state.bga_event_move_anchor_y_pixels = input.pointer_started_y;
    state.bga_event_move_current_y_pixels = input.pointer_started_y;
    state.bga_event_move_anchor_screen_y = input.left_clicked_screen_y;
    return true;
}

static auto process_compose_bga_event_pointer(const ComposePointerEventContext& input,
                                              bool paint_pointer_consumed) -> bool {
    if (update_bga_event_move_capture(input)) return true;
    return begin_bga_event_move_capture(input, paint_pointer_consumed);
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
