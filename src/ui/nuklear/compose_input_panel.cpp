#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/theme.h"
#include "sys/compose_scroll_geometry.h"
#include "ui/nuklear/bms_branch_panel.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_canvas.h"
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
#include "ui/nuklear/compose_area_menu.h"

static void reset_bmson_marker_move_capture() {
    state.bmson_marker_move_candidate_active = false;
    state.bmson_marker_move_active = false;
    state.bmson_marker_move_event_id = 0;
}

static void reset_timing_marker_drag_capture() {
    state.timing_marker_candidate_active = false;
    state.timing_marker_drag_active = false;
    state.timing_marker_object_id = 0;
}

static void set_waveform_offset_drag_action(WormUiWorkspaceAction* action,
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

static void reset_waveform_offset_drag() {
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
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
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

static constexpr std::array<wormedit::signed_rational_t, 6> beat_fit_snap_steps{{
    {1, 1}, {3, 4}, {1, 2}, {1, 3}, {1, 4}, {1, 10},
}};

static void append_beat_fit_input(WormUiWorkspaceAction* action,
                                  WormUiComposeInputKind kind, double y,
                                  bool ctrl_held) {
    append_compose_input(action, kind, 0.0, y);
    if (!action || action->compose_input_count == 0) return;
    auto& event = action->compose_inputs[action->compose_input_count - 1];
    event.beat_fit_ctrl_held = ctrl_held;
    const auto index = std::min<std::size_t>(state.beat_fit_snap_step_index,
                                             beat_fit_snap_steps.size() - 1);
    event.beat_fit_snap_step_numerator = beat_fit_snap_steps[index].numerator;
    event.beat_fit_snap_step_denominator = beat_fit_snap_steps[index].denominator;
}

static void cancel_beat_fit_drag(WormUiWorkspaceAction* action) {
    if (state.beat_fit_drag_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
        state.beat_fit_drag_active = false;
        state.beat_fit_anchor_screen_y = 0.0f;
    }
}

static void cancel_offset_fit_drag(WormUiWorkspaceAction* action) {
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

static void cancel_compose_navigation(WormUiWorkspaceAction* action) {
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

static bool compose_navigation_action_for_key(uint32_t keycode, [[maybe_unused]] uint16_t modifiers,
                                              bool downscroll,
                                              WormUiComposeNavigationAction* action) {
    if (!action) return false;
    const bool visual_up = keycode == static_cast<uint32_t>(SDLK_UP) ||
                           keycode == static_cast<uint32_t>(SDLK_PAGEUP);
    const bool visual_down = keycode == static_cast<uint32_t>(SDLK_DOWN) ||
                             keycode == static_cast<uint32_t>(SDLK_PAGEDOWN);
    if (!visual_up && !visual_down) return false;
    bool page = keycode == static_cast<uint32_t>(SDLK_PAGEUP) ||
                keycode == static_cast<uint32_t>(SDLK_PAGEDOWN);
#if defined(__APPLE__)
    page = page || (modifiers & SDL_KMOD_ALT) != 0;
#endif
    const auto direction = wormedit::sys::compose_visual_navigation_direction(downscroll,
                                                                               visual_up);
    if (page) {
        *action = direction == wormedit::interaction_direction_t::backward
                      ? WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD
                      : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    } else {
        *action = direction == wormedit::interaction_direction_t::backward
                      ? WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD
                      : WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD;
    }
    return true;
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

static void append_compose_cancel_once(WormUiWorkspaceAction* action) {
    if (action && !compose_action_has_cancel(action)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
    }
}

void queue_compose_key_input(std::uint32_t keycode, std::uint32_t scancode,
                                       std::uint16_t modifiers, bool down, bool repeat) {
    if (state.cancel_pending) {
        return;
    }
    if (state.key_event_count == state.key_events.size()) {
        state.key_event_count = 0;
        state.cancel_pending = true;
        state.tab_navigation.cancel();
        state.raw_tab_down = false;
        state.pinch = {};
        state.pointer_hold_active = false;
        state.note_paint_pointer_active = false;
        state.bmson_razor_pointer_active = false;
        reset_bmson_marker_move_capture();
        state.semantic_layer_paint_pointer_active = false;
        state.lane_hold_active = false;
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
        state.timing_stage_drag_active = false;
        state.kiai_drag_active = false;
        state.recipe_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.keyboard_focus = false;
        state.lane_hold_count = 0;
        return;
    }
    if (keycode == static_cast<std::uint32_t>(SDLK_TAB) && !repeat)
        state.raw_tab_down = down;
    state.key_events[state.key_event_count++] = {
        .keycode = keycode,
        .scancode = scancode,
        .modifiers = modifiers,
        .down = down,
        .repeat = repeat,
    };
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
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
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
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
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


static bool compose_point_hits_note(const WormUiComposeView* view, double x, double y) {
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

static uint64_t compose_bgm_event_at(const WormUiComposeView* view, double x, double y) {
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

static bool compose_point_hits_selected_note(const WormUiComposeView* view, double x, double y) {
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

static bool compose_shortcut_modifier(uint16_t modifiers) {
    return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

static WormUiComposeSelectionMode compose_selection_mode(uint16_t modifiers) {
    if (compose_shortcut_modifier(modifiers)) {
        return WORM_UI_COMPOSE_SELECTION_TOGGLE;
    }
    if ((modifiers & SDL_KMOD_SHIFT) != 0) {
        return WORM_UI_COMPOSE_SELECTION_ADD;
    }
    return WORM_UI_COMPOSE_SELECTION_REPLACE;
}

static auto compose_shift_key(uint32_t keycode) -> std::uint8_t {
    if (keycode == static_cast<uint32_t>(SDLK_LSHIFT)) return 1;
    if (keycode == static_cast<uint32_t>(SDLK_RSHIFT)) return 2;
    return 0;
}

static auto compose_shift_keys(uint16_t modifiers) -> std::uint8_t {
    return ((modifiers & SDL_KMOD_LSHIFT) != 0 ? 1 : 0) |
           ((modifiers & SDL_KMOD_RSHIFT) != 0 ? 2 : 0);
}

static bool release_compose_key_hold(const auto& key_event, WormUiWorkspaceAction* action) {
    if (!key_event.down) {
        if (key_event.keycode == static_cast<std::uint32_t>(SDLK_TAB)) {
            state.tab_navigation.cancel();
            return true;
        }
        state.tab_navigation.release_direction(key_event.scancode);
    }
    if (!key_event.down && state.navigation_key_active &&
        key_event.scancode == state.navigation_keycode) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                             false, WORM_UI_COMPOSE_SELECTION_REPLACE, state.navigation_action);
        state.navigation_key_active = false;
        state.navigation_keycode = 0;
        return true;
    }
    if (!key_event.down) {
        const auto held = std::find_if(state.lane_holds.begin(),
                                       state.lane_holds.begin() + state.lane_hold_count,
                                       [&](const auto& hold) {
                                           return hold.scancode == key_event.scancode;
                                       });
        if (held != state.lane_holds.begin() + state.lane_hold_count) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 0.0, 0.0,
                                 held->lane_id);
            std::move(held + 1, state.lane_holds.begin() + state.lane_hold_count, held);
            --state.lane_hold_count;
            state.lane_hold_active = state.lane_hold_count != 0;
            return true;
        }
    }
    if (!key_event.down && worm_ui_nuklear_release_shift_range(
            state, compose_shift_key(key_event.keycode))) {
        if (state.space_range_hold_moved)
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
        else
            append_compose_cancel_once(action);
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
        return true;
    }
    return false;
}

static void append_tab_navigation_step(WormUiWorkspaceAction* action,
                                       WormUiComposeNavigationAction navigation,
                                       std::uint16_t modifiers, const WormUiComposeView& view,
                                       bool pointer_paint_mode_active) {
    if (!view.paste_active && !pointer_paint_mode_active && !view.space_range_preview_visible &&
        worm_ui_nuklear_begin_shift_range(state, compose_shift_keys(modifiers)))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
    if (state.navigation_key_active || state.pointer_navigation_active)
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
    state.navigation_key_active = false;
    state.navigation_keycode = 0;
    state.pointer_navigation_active = false;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP, 0.0, 0.0, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE, navigation);
    if (!state.action_overflowed && state.space_range_hold_active)
        state.space_range_hold_moved = true;
}

static auto compose_page_navigation(WormUiComposeNavigationAction action)
    -> WormUiComposeNavigationAction {
    return action == WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD ||
                   action == WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD
               ? WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD
               : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
}

static bool process_compose_tab_key(const auto& event, const WormUiComposeView& view,
                                    WormUiWorkspaceAction* action, bool pointer_paint_mode_active) {
    if (event.keycode != static_cast<std::uint32_t>(SDLK_TAB)) return false;
    state.raw_tab_down = true;
    const auto step = state.tab_navigation.press_tab(SDL_GetTicks());
    if (step) append_tab_navigation_step(action, *step, event.modifiers, view,
                                         pointer_paint_mode_active);
    return true;
}

static bool process_compose_horizontal_tab_key(const auto& event,
                                               const WormUiComposeView& view,
                                               WormUiWorkspaceAction* action,
                                               bool pointer_paint_mode_active) {
    if (event.keycode != static_cast<std::uint32_t>(SDLK_LEFT) &&
        event.keycode != static_cast<std::uint32_t>(SDLK_RIGHT)) return false;
    const auto navigation = event.keycode == static_cast<std::uint32_t>(SDLK_LEFT)
                                ? WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD
                                : WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
    const auto step = state.tab_navigation.press_direction(navigation, event.scancode,
                                                           SDL_GetTicks());
    if (!state.tab_navigation.tab_down) return false;
    if (step) append_tab_navigation_step(action, *step, event.modifiers, view,
                                         pointer_paint_mode_active);
    return true;
}

static bool process_compose_navigation_key(const auto& key_event,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action,
                                           bool pointer_paint_mode_active) {
    const auto key = key_event.keycode;
    if (key != static_cast<uint32_t>(SDLK_UP) && key != static_cast<uint32_t>(SDLK_DOWN) &&
        key != static_cast<uint32_t>(SDLK_PAGEUP) &&
        key != static_cast<uint32_t>(SDLK_PAGEDOWN))
        return false;
    WormUiComposeNavigationAction navigation_action{};
    if (key == static_cast<uint32_t>(SDLK_UP) || key == static_cast<uint32_t>(SDLK_DOWN)) {
        if (!compose_navigation_action_for_key(key, key_event.modifiers, view->downscroll,
                                               &navigation_action))
            return true;
    } else {
        compose_navigation_action_for_key(key, key_event.modifiers, view->downscroll,
                                          &navigation_action);
    }
    const auto step = state.tab_navigation.press_direction(
        compose_page_navigation(navigation_action), key_event.scancode, SDL_GetTicks());
    if (state.tab_navigation.tab_down) {
        if (step) append_tab_navigation_step(action, *step, key_event.modifiers, *view,
                                             pointer_paint_mode_active);
        return true;
    }
    if (!view->paste_active && !pointer_paint_mode_active && !view->space_range_preview_visible &&
        worm_ui_nuklear_begin_shift_range(
            state, compose_shift_keys(key_event.modifiers))) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
    }
    update_compose_pointer_navigation(action, std::nullopt);
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE, navigation_action);
    state.navigation_key_active = !state.action_overflowed;
    state.navigation_keycode = key_event.scancode;
    state.navigation_action = navigation_action;
    if (!state.action_overflowed && state.space_range_hold_active)
        state.space_range_hold_moved = true;
    return true;
}

static void cancel_compose_pointer_selection(WormUiWorkspaceAction* action) {
    if (state.note_move_active || state.area_selection_active)
        append_compose_cancel_once(action);
    state.note_move_candidate_active = false;
    state.note_move_active = false;
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
}

static bool process_compose_escape(const auto& key_event,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action) {
    if (key_event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
        cancel_compose_navigation(action);
        cancel_compose_pointer_selection(action);
        cancel_beat_fit_drag(action);
        cancel_offset_fit_drag(action);
        if (state.waveform_beat_zero_drag_active) {
            set_waveform_offset_drag_action(action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                                            state.waveform_beat_zero_drag_delta_frames);
            reset_waveform_offset_drag();
        }
        state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        if (state.bmson_marker_move_active)
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL);
        reset_bmson_marker_move_capture();
        if (state.space_range_hold_active) {
            append_compose_cancel_once(action);
            state.space_range_hold_active = false;
            state.space_range_hold_moved = false;
        }
        if (view->area_range_available || compose_area_capture_active())
            cancel_compose_area_capture(*action);
        return true;
    }
    return false;
}

static void process_compose_lane_key(const auto& key_event, const WormUiComposeView* view,
                                    WormUiWorkspaceAction* action) {
    if (!wormedit::ui::compose_notes_editable(compose_mode_of(*view))) return;
    for (size_t binding_index = 0; binding_index < view->lane_key_binding_count;
         ++binding_index) {
        const auto& binding = view->lane_key_bindings[binding_index];
        if (binding.scancode != key_event.scancode || binding.lane_id == 0) continue;
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 0.0, 0.0,
                             binding.lane_id, (key_event.modifiers & SDL_KMOD_SHIFT) != 0);
        if ((key_event.modifiers & SDL_KMOD_SHIFT) == 0 && !state.action_overflowed &&
            state.lane_hold_count < state.lane_holds.size()) {
            state.lane_hold_active = true;
            state.lane_holds[state.lane_hold_count++] = {key_event.scancode, binding.lane_id};
        }
        break;
    }
}

static auto compose_pointer_paint_mode_active(const WormUiComposeView* view,
                                              const WormUiWorkspaceAction* action) -> bool {
    return view->note_paint_mode_active ||
           action->compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE ||
           view->semantic_layer_paint_mode_active ||
           action->compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
}

static void cancel_holds_for_pointer_paint(bool pointer_paint_mode_active,
                                          WormUiWorkspaceAction* action) {
    if (!pointer_paint_mode_active) return;
    if (state.lane_hold_active) {
        append_compose_cancel_once(action);
        state.lane_hold_active = false;
        state.lane_hold_count = 0;
    }
    if (state.space_range_hold_active) {
        append_compose_cancel_once(action);
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
    }
    if (state.note_move_candidate_active || state.note_move_active) {
        if (state.note_move_active) append_compose_cancel_once(action);
        state.note_move_candidate_active = false;
        state.note_move_active = false;
    }
}

static void process_compose_key_event(const auto& key_event, const WormUiComposeView* view,
                                      WormUiWorkspaceAction* action, bool keyboard_ready,
                                      bool pointer_paint_mode_active) {
    if (release_compose_key_hold(key_event, action)) return;
    if (key_event.down && state.space_range_hold_active)
        state.shift_range_keys |= compose_shift_key(key_event.keycode);
    if (!keyboard_ready || !key_event.down || key_event.repeat) return;
    if (view->paste_active && key_event.keycode == static_cast<uint32_t>(SDLK_RETURN)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_PASTE_COMMIT);
        return;
    }
    if (view->paste_active && key_event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_PASTE_CANCEL);
        return;
    }
    if (process_compose_escape(key_event, view, action)) return;
    if (compose_shortcut_modifier(key_event.modifiers)) return;
    if (process_compose_tab_key(key_event, *view, action, pointer_paint_mode_active)) return;
    if (process_compose_horizontal_tab_key(key_event, *view, action, pointer_paint_mode_active)) return;
    if (process_compose_navigation_key(key_event, view, action, pointer_paint_mode_active)) return;
    if (pointer_paint_mode_active || view->paste_active) return;
    if ((key_event.modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_MODE |
                                SDL_KMOD_LEVEL5)) != 0)
        return;
    process_compose_lane_key(key_event, view, action);
}

static bool go_to_measure_can_open(nk_context* context, const WormUiComposeView& view,
                                   bool window_focused, bool ime_composition_active) {
    return context && context->current && window_focused && !ime_composition_active &&
        view.visible && view.scene_available && !state.inline_timing_open &&
        !context->current->edit.active && (context->current->flags & NK_WINDOW_ROM) == 0;
}

static bool go_to_measure_shortcut(const worm_ui_nuklear_compose_key_event_t& event) {
    return event.down && !event.repeat && event.keycode == static_cast<uint32_t>(SDLK_G) &&
        (event.modifiers & SDL_KMOD_CTRL) != 0 &&
        (event.modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT | SDL_KMOD_MODE |
                            SDL_KMOD_LEVEL5)) == 0;
}

static bool process_go_to_measure_keys(nk_context* context, const WormUiComposeView& view,
                                       WormUiWorkspaceAction* action, bool window_focused,
                                       bool ime_composition_active) {
    if (state.go_to_measure_open) {
        for (size_t index = 0; index < state.key_event_count; ++index) {
            const auto& event = state.key_events[index];
            if (event.down && event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
                state.go_to_measure_cancel_pending = true;
            }
        }
        return true;
    }
    if (!go_to_measure_can_open(context, view, window_focused, ime_composition_active))
        return false;
    for (size_t index = 0; index < state.key_event_count; ++index) {
        const auto& event = state.key_events[index];
        if (!go_to_measure_shortcut(event)) continue;
        cancel_compose_navigation(action);
        cancel_compose_pointer_selection(action);
        append_compose_cancel_once(action);
        state.go_to_measure_open = true;
        state.go_to_measure_cancel_pending = false;
        state.go_to_measure_focus_pending = true;
        state.go_to_measure_chart_id = view.chart_id;
        state.go_to_measure_revision = view.document_revision;
        state.go_to_measure_text.fill('\0');
        state.go_to_measure_error.fill('\0');
        return true;
    }
    return false;
}

static bool compose_text_owned(const nk_context* context) {
    if (!context || !context->active) return false;
    const auto* active = context->active;
    return active->popup.win ? active->popup.win->edit.active != 0 : active->edit.active != 0;
}

static void release_discarded_compose_keys(WormUiWorkspaceAction* action) {
    worm_ui_nuklear_dispatch_compose_keys(state, false, [&](const auto& event) {
        (void)release_compose_key_hold(event, action);
    });
}

static void cancel_compose_shift_range(WormUiWorkspaceAction* action) {
    if (!state.space_range_hold_active) return;
    append_compose_cancel_once(action);
    state.space_range_hold_active = false;
    state.space_range_hold_moved = false;
    state.shift_range_keys = 0;
}

void process_compose_key_events(struct nk_context* context, const WormUiComposeView* view,
                                       WormUiWorkspaceAction* action, bool window_focused,
                                       bool ime_composition_active) {
    if (state.inline_timing_open || state.go_to_measure_open || ime_composition_active ||
        compose_text_owned(context))
        cancel_compose_shift_range(action);
    if (process_go_to_measure_keys(context, *view, action, window_focused,
                                   ime_composition_active)) {
        cancel_compose_shift_range(action);
        cancel_compose_navigation(action);
        release_discarded_compose_keys(action);
        state.key_event_count = 0;
        return;
    }
    if (state.inline_timing_open) {
        cancel_compose_navigation(action);
        state.inline_timing_input_consumed = true;
        for (size_t index = 0; index < state.key_event_count; ++index) {
            const auto& event = state.key_events[index];
            if (event.down && event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
                worm_ui_nuklear_inline_timing_cancel(state, *action);
                break;
            }
        }
        release_discarded_compose_keys(action);
        return;
    }
    const bool keyboard_ready = context && worm_ui_nuklear_compose_canvas_input_available(
        view, window_focused, compose_text_owned(context), ime_composition_active);
    if (!keyboard_ready) {
        cancel_compose_navigation(action);
        cancel_compose_shift_range(action);
    }
    const bool pointer_paint_mode_active = compose_pointer_paint_mode_active(view, action);
    if (ime_composition_active) {
        release_discarded_compose_keys(action);
        cancel_compose_navigation(action);
        state.key_event_count = 0;
        return;
    }
    cancel_holds_for_pointer_paint(pointer_paint_mode_active, action);
    worm_ui_nuklear_dispatch_compose_keys(state, keyboard_ready, [&](const auto& key_event) {
        if (key_event.down && compose_area_capture_active() &&
            key_event.keycode != static_cast<uint32_t>(SDLK_ESCAPE)) return;
        process_compose_key_event(key_event, view, action, keyboard_ready,
                                  pointer_paint_mode_active);
    });
    const auto step = state.tab_navigation.advance(SDL_GetTicks(), keyboard_ready &&
                                                   !state.action_overflowed &&
                                                   !compose_area_capture_active());
    if (step) append_tab_navigation_step(action, *step, SDL_GetModState(), *view,
                                         pointer_paint_mode_active);
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
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
    state.timing_stage_drag_active = false;
    state.kiai_drag_active = false;
    state.recipe_drag_active = false;
    reset_timing_marker_drag_capture();
    state.inline_timing_open = false;
}

static void reset_compose_revision_state(bool chart_changed, WormUiWorkspaceAction* action) {
    cancel_beat_fit_drag(action);
    if (state.waveform_beat_zero_drag_active) {
        set_waveform_offset_drag_action(action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                                        state.waveform_beat_zero_drag_delta_frames);
        reset_waveform_offset_drag();
    }
    state.waveform_overview_drag_active = false;
    if (state.note_paint_pointer_active || state.semantic_layer_paint_pointer_active ||
        state.space_range_hold_active || state.note_move_active || state.bmson_marker_move_active)
        append_compose_cancel_once(action);
    state.pointer_hold_active = false;
    state.note_paint_pointer_active = false;
    state.bmson_razor_pointer_active = false;
    reset_bmson_marker_move_capture();
    state.semantic_layer_paint_pointer_active = false;
    if (chart_changed) state.lane_hold_active = false;
    state.space_range_hold_active = false;
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
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
    state.space_range_hold_moved = false;
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
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

ComposePointerFocusState update_compose_pointer_focus(
    const ComposePointerFocusContext& focus) {
    const auto& context = *focus.context;
    const auto& view = *focus.view;
    const auto pointer_inside = nk_input_is_mouse_hovering_rect(&context.input, focus.viewport) &&
                                context.input.mouse.pos.x < focus.editor_input_right;
    const auto pointer_clicked =
        nk_input_is_mouse_click_in_rect(&context.input, NK_BUTTON_LEFT, focus.viewport) &&
        context.input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x < focus.editor_input_right;
    const auto overview_pressed =
        focus.overview_available && focus.overview.w > 1.0f && focus.overview.h > 1.0f &&
        nk_input_is_mouse_click_in_rect(&context.input, NK_BUTTON_LEFT, focus.overview) &&
        context.input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x < focus.editor_input_right;
    const auto& left_button = context.input.mouse.buttons[NK_BUTTON_LEFT];
    const auto pointer_pressed_in_view = left_button.down && left_button.clicked != 0 &&
        left_button.clicked_pos.x >= focus.viewport.x &&
        left_button.clicked_pos.x < focus.editor_input_right &&
        left_button.clicked_pos.y >= focus.viewport.y &&
        left_button.clicked_pos.y <= focus.viewport.y + focus.viewport.h;
    if ((overview_pressed && focus.window_focused && view.scene_available) ||
        pointer_clicked || pointer_pressed_in_view) {
        state.keyboard_focus = true;
    } else if (nk_input_any_mouse_click_in_rect(
                   &context.input, nk_rect(0.0f, 0.0f, 1000000.0f, 1000000.0f)) &&
               !pointer_inside && !nk_input_is_mouse_hovering_rect(&context.input, focus.overview)) {
        state.keyboard_focus = false;
        if (!state.pointer_navigation_active) {
            append_compose_input(focus.action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
        }
        if (state.space_range_hold_active) {
            append_compose_cancel_once(focus.action);
            state.space_range_hold_active = false;
            state.space_range_hold_moved = false;
        }
    }
    return {pointer_inside, pointer_clicked, overview_pressed};
}

static ComposeViewportCursor update_compose_viewport_pointer(
    const ComposeViewportInputContext& input) {
    const auto& view = *input.view;
    const auto& context = *input.context;
    const auto x = (static_cast<double>(context.input.mouse.pos.x) - input.viewport.x) *
                   view.viewport_width_pixels / input.viewport.w;
    const auto y = (static_cast<double>(context.input.mouse.pos.y) - input.viewport.y) *
                   view.viewport_height_pixels / input.viewport.h;
    const auto note_left = view.playable_field_left_pixels;
    const auto note_right = view.playable_field_right_pixels;
    if (!state.action_overflowed && input.window_focused && view.scene_available) {
        input.action->compose_viewport_pointer_valid = true;
        input.action->compose_viewport_pointer_x_pixels = x;
        input.action->compose_viewport_pointer_y_pixels = y;
        if (!compose_area_capture_active() && state.middle_click_pending && state.middle_click_x >= input.viewport.x &&
            state.middle_click_x < input.input_right && state.middle_click_y >= input.viewport.y &&
            state.middle_click_y <= input.viewport.y + input.viewport.h) {
            input.action->compose_viewport_middle_click = true;
            input.action->compose_viewport_middle_click_x_pixels =
                (static_cast<double>(state.middle_click_x) - input.viewport.x) *
                view.viewport_width_pixels / input.viewport.w;
            input.action->compose_viewport_middle_click_y_pixels =
                (static_cast<double>(state.middle_click_y) - input.viewport.y) *
                view.viewport_height_pixels / input.viewport.h;
        }
    }
    state.middle_click_pending = false;
    return {x, y, note_left, note_right,
            input.pointer_inside && x >= note_left && x < note_right};
}

static bool append_beat_fit_snap_wheel(const ComposeViewportInputContext& input,
                                       const worm_ui_nuklear_compose_wheel_event_t& wheel) {
    if (!state.beat_fit_drag_active || (wheel.modifiers & SDL_KMOD_CTRL) == 0) return false;
    // Compose wheel deltas are positive toward the song end: wheel down picks a finer step.
    const auto step = wheel.delta_y > 0.0 ? 1 : wheel.delta_y < 0.0 ? -1 : 0;
    const auto next = static_cast<int>(state.beat_fit_snap_step_index) + step;
    const auto updated = static_cast<std::uint8_t>(
        std::clamp(next, 0, static_cast<int>(beat_fit_snap_steps.size() - 1)));
    if (updated != state.beat_fit_snap_step_index) {
        state.beat_fit_snap_step_index = updated;
        const auto y = (wheel.mouse_y - input.viewport.y) * input.view->viewport_height_pixels /
                       input.viewport.h;
        append_beat_fit_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, y, true);
    }
    return true;
}

static void append_compose_wheel_inputs(const ComposeViewportInputContext& input) {
    const auto& view = *input.view;
    if (!state.action_overflowed && view.scene_available && !view.input_occluded &&
        input.window_focused) {
        for (size_t index = 0; index < state.wheel_event_count; ++index) {
            const auto& wheel = state.wheel_events[index];
            if (wheel.mouse_x < input.viewport.x || wheel.mouse_x >= input.input_right ||
                wheel.mouse_y < input.viewport.y || wheel.mouse_y > input.viewport.y + input.viewport.h) {
                continue;
            }
            if (append_beat_fit_snap_wheel(input, wheel)) continue;
            const auto zoom_modifier = (wheel.modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            if (input.action->compose_viewport_input_count >= WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY)
                break;
            const auto visual_y = (wheel.mouse_y - input.viewport.y) * view.viewport_height_pixels /
                                  input.viewport.h;
            input.action->compose_viewport_inputs[input.action->compose_viewport_input_count++] = {
                .kind = wheel.coarse_steps ? WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                                           : WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
                .zoom_modifier = zoom_modifier,
                .delta_x = worm_ui_nuklear_compose_wheel_speed(wheel.delta_x, wheel.tab_speed_modifier && !zoom_modifier),
                .delta_y = worm_ui_nuklear_compose_wheel_speed(wheel.delta_y, wheel.tab_speed_modifier && !zoom_modifier),
                .cursor_x_pixels =
                    (wheel.mouse_x - input.viewport.x) * view.viewport_width_pixels / input.viewport.w,
                .cursor_y_pixels = zoom_modifier
                    ? worm_ui_nuklear_compose_zoom_anchor(visual_y, view.viewport_height_pixels, view.downscroll)
                    : visual_y,
            };
        }
    }
    state.wheel_event_count = 0;
}

static void append_compose_pinch_inputs(const ComposeViewportInputContext& input,
                                        bool competing_capture) {
    const auto& view = *input.view;
    if (competing_capture || !input.context || !view.scene_available || view.input_occluded ||
        !input.window_focused || compose_text_owned(input.context) || state.inline_timing_open ||
        state.go_to_measure_open || state.pinch.project_id != view.project_id ||
        state.pinch.chart_id != view.chart_id || !(input.viewport.w > 0) || !(input.viewport.h > 0)) {
        state.pinch = {};
        return;
    }
    for (std::size_t index = 0; index < state.pinch.count; ++index) {
        const auto& update = state.pinch.pending[index];
        if (update.mouse_x < input.viewport.x || update.mouse_x >= input.input_right ||
            update.mouse_y < input.viewport.y || update.mouse_y > input.viewport.y + input.viewport.h) {
            state.pinch = {};
            return;
        }
        if (input.action->compose_viewport_input_count >= WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY) {
            state.pinch = {};
            return;
        }
        const auto visual_y = (update.mouse_y - input.viewport.y) * view.viewport_height_pixels /
                              input.viewport.h;
        input.action->compose_viewport_inputs[input.action->compose_viewport_input_count++] = {
            .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM,
            .cursor_x_pixels = (update.mouse_x - input.viewport.x) * view.viewport_width_pixels /
                               input.viewport.w,
            .cursor_y_pixels = worm_ui_nuklear_compose_zoom_anchor(visual_y,
                                   view.viewport_height_pixels, view.downscroll),
            .zoom_factor = update.factor,
        };
    }
    state.pinch.count = 0;
}

ComposeViewportCursor collect_compose_viewport_inputs(
    const ComposeViewportInputContext& input) {
    const bool competing_capture = state.space_range_hold_active || input.view->space_range_preview_visible || state.pointer_hold_active || state.lane_hold_active ||
        state.note_paint_pointer_active || state.note_move_active || state.note_move_candidate_active ||
        state.area_selection_active || state.area_selection_candidate_active ||
        state.bmson_razor_pointer_active || state.bmson_marker_move_active ||
        state.bmson_marker_move_candidate_active || state.semantic_layer_paint_pointer_active ||
        state.inline_timing_open || state.timing_stage_drag_active || state.kiai_drag_active ||
        state.recipe_drag_active || compose_recipe_menu_open() ||
        state.timing_marker_candidate_active || state.timing_marker_drag_active ||
        state.beat_fit_drag_active || state.offset_fit_drag_active ||
        state.waveform_beat_zero_drag_active || state.waveform_overview_drag_active;
    if (!competing_capture || compose_area_capture_active())
        process_compose_area_menu(input.context, *input.view, *input.action, input.viewport,
                                  input.input_right, input.window_focused);
    process_compose_recipe_menu(input.context, *input.view, *input.action, input.viewport,
                                input.input_right, input.window_focused);
    const auto cursor = update_compose_viewport_pointer(input);
    append_compose_wheel_inputs(input);
    append_compose_pinch_inputs(input, competing_capture);
    return cursor;
}

static bool compose_beat_fit_start_allowed(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    return view.scene_available && input.window_focused && input.pointer_started_in_view &&
           input.left_down && input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.blocked_modifiers) == 0 &&
           input.pointer_started_x >= 22.0 && input.pointer_started_x <= 42.0 &&
           view.waveform_row_count != 0 && view.waveform_overview_sample_rate != 0 &&
           !view.waveform_timing_locked && !view.waveform_offset_drag_active &&
           !state.beat_fit_drag_active && !state.waveform_beat_zero_drag_active;
}

static bool compose_offset_fit_start_allowed(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    // SDL_KMOD_SHIFT covers both Shift keys; either one alone starts the offset fit.
    constexpr auto other_modifiers = SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI;
    return view.scene_available && input.window_focused && input.pointer_started_in_view &&
           input.left_down && input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & SDL_KMOD_SHIFT) != 0 &&
           (input.pointer_modifiers & other_modifiers) == 0 &&
           input.pointer_started_x >= 22.0 && input.pointer_started_x <= 42.0 &&
           view.waveform_row_count != 0 && view.waveform_overview_sample_rate != 0 &&
           !view.waveform_timing_locked && !view.audio_offset_locked && !view.waveform_offset_drag_active &&
           !state.offset_fit_drag_active && !state.waveform_beat_zero_drag_active;
}

static std::uint64_t nearest_beat_fit_row(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    std::uint64_t nearest_row = 0;
    auto nearest_distance = 6.0 * view.viewport_height_pixels / input.viewport.h;
    for (std::size_t index = 0; index < view.grid_line_count; ++index) {
        const auto& line = view.grid_lines[index];
        if (line.chart_row == 0 || (line.kind != 1 && line.kind != 3)) {
            continue;
        }
        const auto distance = std::abs(line.y_pixels - input.pointer_started_y);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_row = line.chart_row;
        }
    }
    return nearest_row;
}

static bool start_compose_beat_fit_drag(const ComposeBeatFitInput& input) {
    if (!compose_beat_fit_start_allowed(input)) {
        return false;
    }
    const auto nearest_row = nearest_beat_fit_row(input);
    if (nearest_row == 0) {
        return false;
    }
    const auto& view = *input.view;
    state.beat_fit_drag_active = true;
    state.beat_fit_ctrl_held = false;
    state.beat_fit_chart_row = nearest_row;
    state.beat_fit_chart_id = view.chart_id;
    state.beat_fit_revision = view.document_revision;
    state.beat_fit_asset_id = view.selected_audio_asset_id;
    state.beat_fit_sample_rate = view.waveform_overview_sample_rate;
    state.beat_fit_anchor_screen_y = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 0.0,
                         input.pointer_started_y, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, nearest_row);
    return true;
}

static bool start_compose_offset_fit_drag(const ComposeBeatFitInput& input) {
    if (!compose_offset_fit_start_allowed(input)) return false;
    const auto nearest_row = nearest_beat_fit_row(input);
    if (nearest_row == 0) return false;
    const auto& view = *input.view;
    state.offset_fit_drag_active = true;
    state.beat_fit_chart_row = nearest_row;
    state.beat_fit_chart_id = view.chart_id;
    state.beat_fit_revision = view.document_revision;
    state.beat_fit_asset_id = view.selected_audio_asset_id;
    state.beat_fit_sample_rate = view.waveform_overview_sample_rate;
    state.beat_fit_anchor_screen_y = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN, 0.0,
                         input.pointer_started_y, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, nearest_row);
    return true;
}

static void cancel_compose_offset_fit_drag(WormUiWorkspaceAction* action) {
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL);
    state.offset_fit_drag_active = false;
}

static void update_active_offset_fit_drag(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    const bool matches = input.window_focused && view.scene_available &&
                         view.offset_fit_drag_active && !view.waveform_timing_locked && !view.audio_offset_locked &&
                         view.chart_id == state.beat_fit_chart_id &&
                         view.document_revision == state.beat_fit_revision &&
                         view.selected_audio_asset_id == state.beat_fit_asset_id &&
                         view.waveform_overview_sample_rate == state.beat_fit_sample_rate;
    if (!matches || (!input.left_down && !input.left_released)) {
        cancel_compose_offset_fit_drag(input.action);
    } else if (input.left_released) {
        if (std::abs(input.context->input.mouse.pos.y - state.beat_fit_anchor_screen_y) < 4.0f) {
            cancel_compose_offset_fit_drag(input.action);
        } else {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE, 0.0,
                                 std::clamp(input.pointer_y, 0.0, view.viewport_height_pixels));
            state.offset_fit_drag_active = false;
        }
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 0.0,
                             std::clamp(input.pointer_y, 0.0, view.viewport_height_pixels));
    }
}

static void cancel_unmatched_view_beat_fit(const ComposeBeatFitInput& input) {
    const auto cancel_pending = std::ranges::any_of(
        input.action->compose_inputs, input.action->compose_inputs + input.action->compose_input_count,
        [](const auto& event) { return event.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL; });
    if (!cancel_pending) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
    }
}

static void update_active_beat_fit_drag(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    const bool matches = input.window_focused && view.scene_available && view.beat_fit_drag_active &&
                         !view.waveform_timing_locked && view.chart_id == state.beat_fit_chart_id &&
                         view.document_revision == state.beat_fit_revision &&
                         view.selected_audio_asset_id == state.beat_fit_asset_id &&
                         view.waveform_overview_sample_rate == state.beat_fit_sample_rate;
    if (!matches || (!input.left_down && !input.left_released)) {
        cancel_beat_fit_drag(input.action);
    } else if (input.left_released) {
        if (std::abs(input.context->input.mouse.pos.y - state.beat_fit_anchor_screen_y) < 4.0f) {
            cancel_beat_fit_drag(input.action);
        } else {
            append_beat_fit_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE,
                                  std::clamp(input.pointer_y, 0.0,
                                             view.viewport_height_pixels),
                                  (input.pointer_modifiers & SDL_KMOD_CTRL) != 0);
            state.beat_fit_drag_active = false;
        }
    } else if (const bool ctrl_held = (input.pointer_modifiers & SDL_KMOD_CTRL) != 0;
               input.left_down && (nk_input_is_mouse_moved(&input.context->input) ||
                                   ctrl_held != state.beat_fit_ctrl_held)) {
        // Pressing or releasing Ctrl re-previews in place so the snap toggles without motion.
        state.beat_fit_ctrl_held = ctrl_held;
        append_beat_fit_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE,
                              std::clamp(input.pointer_y, 0.0, view.viewport_height_pixels),
                              ctrl_held);
    }
}

bool process_compose_beat_fit_pointer(const ComposeBeatFitInput& input) {
    if (input.view->paste_active || compose_area_pointer_consumed()) return true;
    if (state.offset_fit_drag_active) {
        update_active_offset_fit_drag(input);
        return true;
    }
    if (state.beat_fit_drag_active) {
        update_active_beat_fit_drag(input);
        return true;
    }
    if (input.view->beat_fit_drag_active) {
        cancel_unmatched_view_beat_fit(input);
        return false;
    }
    if (compose_offset_fit_start_allowed(input)) return start_compose_offset_fit_drag(input);
    return start_compose_beat_fit_drag(input);
}

// A zoomed overview spans only part of the song, so a pixel is fewer frames.
static std::uint64_t overview_window_frames(std::uint64_t total_frames, double span) {
    if (!std::isfinite(span) || span <= 0.0 || span >= 1.0) return total_frames;
    return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(
        std::llround(static_cast<long double>(total_frames) * span)));
}

// Position of an audio fraction inside the overview rectangle, if the window shows it.
static std::optional<double> overview_window_x(const ComposeOverviewInput& input, double fraction) {
    const auto span = input.window_end - input.window_begin;
    if (!(span > 0.0) || fraction < input.window_begin || fraction > input.window_end)
        return std::nullopt;
    return input.overview.x + (fraction - input.window_begin) / span * input.overview.w;
}

static std::int64_t waveform_offset_drag_delta(const ComposeOverviewInput& input) {
    return worm_ui_nuklear_overview_frame_delta(
        input.context->input.mouse.pos.x, state.waveform_beat_zero_drag_anchor_x,
        state.waveform_beat_zero_drag_width,
        overview_window_frames(state.waveform_beat_zero_drag_total_frames,
                               state.waveform_beat_zero_drag_span));
}

static void start_waveform_beat_zero_drag(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto zero_x = overview_window_x(input, view.waveform_beat_zero_fraction);
    if (state.action_overflowed || !input.available || !input.pressed ||
        !input.window_focused || !view.scene_available ||
        !view.waveform_beat_zero_visible || view.waveform_timing_locked || view.audio_offset_locked ||
        view.waveform_offset_drag_active ||
        input.action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE ||
        !zero_x || std::abs(static_cast<double>(left_button.clicked_pos.x) - *zero_x) > 7.0) {
        return;
    }
    state.waveform_overview_drag_active = false;
    state.waveform_beat_zero_drag_active = true;
    state.waveform_beat_zero_drag_anchor_x = left_button.clicked_pos.x;
    state.waveform_beat_zero_drag_width = input.overview.w;
    state.waveform_beat_zero_drag_left = input.overview.x;
    state.waveform_beat_zero_drag_span = input.window_end - input.window_begin;
    state.waveform_beat_zero_drag_total_frames = view.waveform_overview_total_frames;
    state.waveform_beat_zero_drag_chart_id = view.chart_id;
    state.waveform_beat_zero_drag_revision = view.document_revision;
    state.waveform_beat_zero_drag_asset_id = view.selected_audio_asset_id;
    state.waveform_beat_zero_drag_sample_rate = view.waveform_overview_sample_rate;
    state.waveform_beat_zero_drag_delta_frames = 0;
    set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_BEGIN, 0);
}

static void update_waveform_beat_zero_drag(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto released = nk_input_is_mouse_released(&input.context->input, NK_BUTTON_LEFT);
    if (!state.waveform_beat_zero_drag_active) {
        start_waveform_beat_zero_drag(input);
        return;
    }
    const bool matches =
        input.available && input.window_focused && view.scene_available &&
        !view.waveform_timing_locked && !view.audio_offset_locked && view.chart_id == state.waveform_beat_zero_drag_chart_id &&
        view.document_revision == state.waveform_beat_zero_drag_revision &&
        view.selected_audio_asset_id == state.waveform_beat_zero_drag_asset_id &&
        view.waveform_overview_total_frames == state.waveform_beat_zero_drag_total_frames &&
        view.waveform_overview_sample_rate == state.waveform_beat_zero_drag_sample_rate &&
        input.overview.w == state.waveform_beat_zero_drag_width &&
        input.overview.x == state.waveform_beat_zero_drag_left &&
        (view.waveform_offset_drag_active || input.pressed);
    if (!matches || (!left_button.down && !released)) {
        set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                                        state.waveform_beat_zero_drag_delta_frames);
        reset_waveform_offset_drag();
    } else if (released) {
        set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_COMMIT,
                                        waveform_offset_drag_delta(input));
        reset_waveform_offset_drag();
    } else if (left_button.down && nk_input_is_mouse_moved(&input.context->input)) {
        const auto delta = waveform_offset_drag_delta(input);
        state.waveform_beat_zero_drag_delta_frames = delta;
        set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_UPDATE, delta);
    }
}

static void update_waveform_overview_seek(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    if (input.available && !state.action_overflowed && !input.beat_zero_was_active &&
        !state.waveform_beat_zero_drag_active && input.pressed &&
        input.window_focused &&
        view.scene_available && !view.waveform_offset_drag_active &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        state.waveform_overview_drag_active = true;
        state.waveform_overview_capture = {
            view.chart_id, view.document_revision, view.selected_audio_asset_id,
            view.waveform_overview_total_frames, view.waveform_overview_sample_rate,
            input.overview.x, input.overview.w};
    }
    const bool valid = worm_ui_nuklear_overview_capture_matches(
            state.waveform_overview_capture, view, input.overview.x, input.overview.w,
            input.available, input.window_focused) &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE &&
        !view.waveform_offset_drag_active;
    const auto event = worm_ui_nuklear_overview_gesture(state.waveform_overview_gesture,
        input.context->input.mouse.pos.x, input.pressed && state.waveform_overview_drag_active,
        left_button.down, valid);
    state.waveform_overview_drag_active = state.waveform_overview_gesture.active;
    if ((event == worm_ui_nuklear_overview_event_t::scroll &&
         !nk_input_is_mouse_moved(&input.context->input)) ||
        event == worm_ui_nuklear_overview_event_t::none ||
        input.action->compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) return;
    append_compose_input(input.action, event == worm_ui_nuklear_overview_event_t::click
        ? WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_CLICK : WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK);
    auto& emitted = input.action->compose_inputs[input.action->compose_input_count - 1];
    emitted.audio_fraction = input.window_begin +
        worm_ui_nuklear_overview_fraction(input.context->input.mouse.pos.x,
            input.overview.x, input.overview.w).value_or(0.0) *
        (input.window_end - input.window_begin);
    const auto modifiers = SDL_GetModState();
    emitted.overview_shift = (modifiers & SDL_KMOD_SHIFT) != 0;
#ifdef __APPLE__
    emitted.overview_primary = (modifiers & SDL_KMOD_GUI) != 0;
#else
    emitted.overview_primary = (modifiers & SDL_KMOD_CTRL) != 0;
#endif
}

void process_compose_overview_input(const ComposeOverviewInput& input) {
    if (input.available && input.window_focused && input.pressed &&
        input.view->scene_available && !state.action_overflowed)
        state.keyboard_focus = true;
    // An orphaned offset preview is cancelled unless the rail offset fit owns it.
    if (input.view->waveform_offset_drag_active && !state.waveform_beat_zero_drag_active &&
        !state.offset_fit_drag_active &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        input.action->audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
    }
    update_waveform_beat_zero_drag(input);
    update_waveform_overview_seek(input);
}

// Document fraction under a screen-space y. The document fractions run in source order, so a
// downscroll field (source row 0 at the bottom) reads the viewport from the other end.
static auto compose_timing_target_fraction(const WormUiComposeView& view, double screen_y)
    -> double {
    const auto viewport_fraction = wormedit::sys::compose_source_fraction(
        screen_y, view.viewport_height_pixels, view.downscroll);
    return view.timing_document_begin_fraction +
           viewport_fraction * (view.timing_document_end_fraction -
                                view.timing_document_begin_fraction);
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

static bool begin_timing_stage_from_rail_click(const ComposeDoubleClickContext& input,
                                               double click_x, double click_y) {
    const auto& view = *input.view;
    const auto& timing_view = *input.timing_view;
    auto& action = *input.action;
    if (timing_view.stage_active || click_x < compose_measure_rail_width ||
        click_x >= input.note_field_left ||
        action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE)
        return false;
    const auto clicked_lane = compose_timing_lane_at_x(view, click_x, input.note_field_left);
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
        action.timing_target_y_fraction = clicked_marker && clicked_marker->fixed_initial
            ? 0.0
            : compose_timing_target_fraction(view, click_y);
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

bool process_compose_double_clicks(const ComposeDoubleClickContext& input,
                                          bool compose_pointer_captured,
                                          bool window_focused) {
    if (input.view->paste_active || compose_area_pointer_consumed()) return true;
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
        if (click_x < input.note_field_left || click_x >= input.note_field_right) continue;
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK, click_x,
                             (static_cast<double>(pointer.y) - viewport.y) *
                                 input.view->viewport_height_pixels / viewport.h);
        note_double_click_in_view = true;
    }
    return note_double_click_in_view;
}

static void update_active_timing_stage_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto& timing = *input.timing_view;
    auto& action = *input.action;
    if (!input.window_focused || !view.scene_available || !timing.stage_active ||
        timing.stage_existing_object_id == 0 || (!input.left_down && !input.left_released)) {
        state.timing_stage_drag_active = false;
    } else if (input.left_released) {
        state.timing_stage_drag_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input) &&
               action.timing_stage_action == WORM_UI_TIMING_STAGE_NONE) {
        action.timing_stage_action = WORM_UI_TIMING_STAGE_MOVE;
        action.timing_chart_id = timing.chart_id;
        action.timing_document_revision = timing.document_revision;
        action.timing_object_id = timing.stage_existing_object_id;
        action.timing_lane = timing.stage_lane;
        action.timing_has_target_y = true;
        action.timing_target_y_fraction = compose_timing_target_fraction(view, input.pointer_y);
    }
}

static void start_timing_stage_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto& timing = *input.timing_view;
    if (!input.pointer_started_in_view || !input.left_down || !view.timing_stage_active ||
        !timing.stage_active || timing.stage_existing_object_id == 0 ||
        input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0)
        return;
    const auto staged_timing_lane_ordinal = static_cast<std::size_t>(view.timing_stage_lane);
    const auto lane_bit = static_cast<std::uint8_t>(1U << staged_timing_lane_ordinal);
    const auto visible_lane_count =
        static_cast<std::size_t>(std::popcount(view.timing_lane_visibility_mask));
    if ((view.timing_lane_visibility_mask & lane_bit) == 0 || visible_lane_count == 0) return;
    const auto lane_width = (input.note_field_left - 42.0) /
                            static_cast<double>(visible_lane_count);
    const auto preceding_mask =
        static_cast<std::uint8_t>(view.timing_lane_visibility_mask & (lane_bit - 1U));
    const auto visible_slot = static_cast<std::size_t>(std::popcount(preceding_mask));
    const auto lane_left = 42.0 + lane_width * visible_slot;
    const auto y_tolerance = 8.0 * view.viewport_height_pixels / input.viewport.h;
    if (input.pointer_started_x >= lane_left && input.pointer_started_x < lane_left + lane_width &&
        std::abs(input.pointer_started_y - view.timing_stage_y_pixels) <= y_tolerance)
        state.timing_stage_drag_active = true;
}

void update_timing_stage_drag(const ComposePointerEventContext& input) {
    if (compose_area_pointer_consumed()) return;
    if (state.timing_stage_drag_active)
        update_active_timing_stage_drag(input);
    else
        start_timing_stage_drag(input);
}

// A press in the Kiai lane either grabs an edge (head or tail), selects an area by its body, or
// starts a new area on empty lane; releasing after a drag commits one edit. Timing markers keep
// priority wherever they sit, and the gesture stays out of any other active pointer capture.
static constexpr double span_edge_tolerance_pixels = 6.0;
static constexpr double kiai_drag_threshold_pixels = 4.0;

static auto kiai_gesture_blocked(const ComposePointerEventContext& input) -> bool {
    return !input.window_focused || !input.view->scene_available || input.shortcut_event_count != 0 ||
           (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
           input.timing_view->stage_active || state.timing_stage_drag_active ||
           state.inline_timing_open || state.timing_marker_candidate_active ||
           state.timing_marker_drag_active || compose_area_pointer_consumed();
}

static void begin_speed_drag(const ComposePointerEventContext& input);

static void begin_kiai_drag(const ComposePointerEventContext& input) {
    state.kiai_drag_speed = false;
    const auto& view = *input.view;
    const auto extent = compose_timing_lane_inner_extent(view, WORM_UI_TIMING_LANE_KIAI);
    const auto x = input.pointer_started_x;
    const auto y = input.pointer_started_y;
    if (!extent || x < extent->first || x >= extent->second ||
        compose_timing_marker_at(view, x, y) != nullptr) return;
    const auto tolerance = span_edge_tolerance_pixels * view.viewport_height_pixels / input.viewport.h;
    state.kiai_drag_edge = 2;
    state.kiai_drag_head_id = 0;
    std::uint64_t body_head = 0;
    for (std::size_t index = 0; index < view.kiai_span_count; ++index) {
        const auto& span = view.kiai_spans[index];
        const auto head_y = view.downscroll ? span.bounds.bottom : span.bounds.top;
        const auto tail_y = view.downscroll ? span.bounds.top : span.bounds.bottom;
        if (span.head_visible && std::abs(y - head_y) <= tolerance) {
            state.kiai_drag_edge = 0;
            state.kiai_drag_head_id = span.head_object_id;
        } else if (span.tail_visible && std::abs(y - tail_y) <= tolerance) {
            state.kiai_drag_edge = 1;
            state.kiai_drag_head_id = span.head_object_id;
        } else if (y > span.bounds.top && y < span.bounds.bottom) {
            body_head = span.head_object_id;
        }
        if (state.kiai_drag_head_id != 0) break;
    }
    if (state.kiai_drag_head_id == 0 && body_head != 0) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT, x, y, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, body_head);
        return;
    }
    state.kiai_drag_active = true;
    state.kiai_drag_moved = false;
    state.kiai_drag_anchor_y = y;
    state.kiai_drag_current_y = y;
}

static void commit_kiai_drag(const ComposePointerEventContext& input) {
    append_compose_input(input.action,
                         state.kiai_drag_speed ? WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT
                                               : WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT,
                         input.pointer_started_x, state.kiai_drag_current_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.kiai_drag_head_id, 0,
                         false,
                         state.kiai_drag_speed ? WORM_UI_TIMING_LANE_SPEED : WORM_UI_TIMING_LANE_KIAI,
                         input.timing_view->chart_id, input.timing_view->document_revision);
    if (input.action->compose_input_count == 0) return;
    auto& queued = input.action->compose_inputs[input.action->compose_input_count - 1];
    queued.span_edge = state.kiai_drag_edge;
    queued.span_anchor_y_pixels = state.kiai_drag_anchor_y;
}

static void continue_kiai_drag(const ComposePointerEventContext& input) {
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        state.kiai_drag_active = false;
        state.recipe_drag_active = false;
        return;
    }
    state.kiai_drag_current_y = std::clamp(input.pointer_y, 0.0, input.view->viewport_height_pixels);
    const auto threshold = kiai_drag_threshold_pixels * input.view->viewport_height_pixels / input.viewport.h;
    if (std::abs(state.kiai_drag_current_y - state.kiai_drag_anchor_y) > threshold)
        state.kiai_drag_moved = true;
    if (!input.left_released) return;
    state.kiai_drag_active = false;
    state.recipe_drag_active = false;
    if (state.kiai_drag_moved) commit_kiai_drag(input);
}

void update_kiai_span_drag(const ComposePointerEventContext& input) {
    if (state.kiai_drag_active) {
        continue_kiai_drag(input);
        return;
    }
    if (input.pointer_started_in_view && input.left_down && !input.left_released &&
        !kiai_gesture_blocked(input)) {
        begin_kiai_drag(input);
        if (!state.kiai_drag_active) begin_speed_drag(input);
    }
}

// SV recipe areas share the Kiai gesture shape: a press on a begin or end edge resizes, a press
// on the body selects and drags the whole span, a click on empty lane clears the selection.
// Releasing after a drag commits one edit; markers keep priority wherever they sit.
static auto recipe_gesture_blocked(const ComposePointerEventContext& input) -> bool {
    return kiai_gesture_blocked(input) || state.kiai_drag_active;
}

static auto recipe_area_extent(const WormUiComposeView& view, const WormUiComposeRecipeAreaView& area) {
    return compose_timing_lane_inner_extent(view, static_cast<WormUiTimingLaneKind>(area.lane));
}

static auto point_in_recipe_lane(const WormUiComposeView& view, double x) -> bool {
    for (const auto lane : {WORM_UI_TIMING_LANE_SCROLL, WORM_UI_TIMING_LANE_SPEED}) {
        const auto extent = compose_timing_lane_inner_extent(view, lane);
        if (extent && x >= extent->first && x < extent->second) return true;
    }
    return false;
}

// Which recipe edge or body a press at (x, y) lands on; the last drawn (topmost) area wins.
struct recipe_press_t {
    std::uint64_t id{};
    std::uint8_t edge{};
};

static auto recipe_press_at(const WormUiComposeView& view, double x, double y, double tolerance)
    -> std::optional<recipe_press_t> {
    std::optional<recipe_press_t> body;
    for (std::size_t index = view.recipe_area_count; index-- > 0;) {
        const auto& area = view.recipe_areas[index];
        const auto extent = recipe_area_extent(view, area);
        if (!extent || x < extent->first || x >= extent->second) continue;
        const auto begin_y = view.downscroll ? area.bounds.bottom : area.bounds.top;
        const auto end_y = view.downscroll ? area.bounds.top : area.bounds.bottom;
        if (area.begin_visible && std::abs(y - begin_y) <= tolerance) return recipe_press_t{area.recipe_id, 0};
        if (area.end_visible && std::abs(y - end_y) <= tolerance) return recipe_press_t{area.recipe_id, 1};
        if (!body && y > area.bounds.top && y < area.bounds.bottom)
            body = recipe_press_t{area.recipe_id, 2};
    }
    return body;
}

// A press on the begin or end edge of a Speed span grabs it: the begin edge moves the event, the
// end edge sets how long it interpolates. Recipe areas sharing the lane keep priority, and the
// drag reuses the Kiai drag state (kiai_drag_speed marks it).
static void begin_speed_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto extent = compose_timing_lane_inner_extent(view, WORM_UI_TIMING_LANE_SPEED);
    const auto x = input.pointer_started_x;
    const auto y = input.pointer_started_y;
    if (!extent || x < extent->first || x >= extent->second || state.recipe_drag_active ||
        compose_timing_marker_at(view, x, y) != nullptr) return;
    const auto tolerance = span_edge_tolerance_pixels * view.viewport_height_pixels / input.viewport.h;
    if (recipe_press_at(view, x, y, tolerance)) return;
    for (std::size_t index = 0; index < view.speed_span_count; ++index) {
        const auto& span = view.speed_spans[index];
        const auto begin_y = view.downscroll ? span.bounds.bottom : span.bounds.top;
        const auto end_y = view.downscroll ? span.bounds.top : span.bounds.bottom;
        // Of two coincident edges (an instant Speed) the end wins: it is the one that can grow.
        const bool on_end = std::abs(y - end_y) <= tolerance;
        if (!on_end && std::abs(y - begin_y) > tolerance) continue;
        state.kiai_drag_edge = on_end ? 1 : 0;
        state.kiai_drag_head_id = span.object_id;
        state.kiai_drag_speed = true;
        state.kiai_drag_active = true;
        state.kiai_drag_moved = false;
        state.kiai_drag_anchor_y = y;
        state.kiai_drag_current_y = y;
        return;
    }
}

static void begin_recipe_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto x = input.pointer_started_x;
    const auto y = input.pointer_started_y;
    if (!point_in_recipe_lane(view, x) || compose_timing_marker_at(view, x, y) != nullptr) return;
    const auto tolerance = span_edge_tolerance_pixels * view.viewport_height_pixels / input.viewport.h;
    const auto press = recipe_press_at(view, x, y, tolerance);
    if (!press) {
        const bool any_selected = std::ranges::any_of(
            std::span(view.recipe_areas, view.recipe_area_count),
            [](const auto& area) { return area.selected; });
        if (any_selected)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, x, y);
        return;
    }
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, x, y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, press->id);
    state.recipe_drag_active = true;
    state.recipe_drag_moved = false;
    state.recipe_drag_edge = press->edge;
    state.recipe_drag_id = press->id;
    state.recipe_drag_anchor_y = y;
    state.recipe_drag_current_y = y;
}

static void commit_recipe_drag(const ComposePointerEventContext& input) {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT,
                         input.pointer_started_x, state.recipe_drag_current_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.recipe_drag_id, 0, false,
                         WORM_UI_TIMING_LANE_SCROLL, input.timing_view->chart_id,
                         input.timing_view->document_revision);
    if (input.action->compose_input_count == 0) return;
    auto& queued = input.action->compose_inputs[input.action->compose_input_count - 1];
    queued.span_edge = state.recipe_drag_edge;
    queued.span_anchor_y_pixels = state.recipe_drag_anchor_y;
}

static void continue_recipe_drag(const ComposePointerEventContext& input) {
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        state.recipe_drag_active = false;
        return;
    }
    state.recipe_drag_current_y = std::clamp(input.pointer_y, 0.0, input.view->viewport_height_pixels);
    const auto threshold = kiai_drag_threshold_pixels * input.view->viewport_height_pixels / input.viewport.h;
    if (std::abs(state.recipe_drag_current_y - state.recipe_drag_anchor_y) > threshold)
        state.recipe_drag_moved = true;
    if (!input.left_released) return;
    state.recipe_drag_active = false;
    if (state.recipe_drag_moved) commit_recipe_drag(input);
}

void update_recipe_drag(const ComposePointerEventContext& input) {
    if (state.recipe_drag_active) {
        continue_recipe_drag(input);
        return;
    }
    if (input.pointer_started_in_view && input.left_down && !input.left_released &&
        !recipe_gesture_blocked(input))
        begin_recipe_drag(input);
}

static auto resource_pointer_can_start(const ComposePointerEventContext& input,
                                       bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed &&
           wormedit::ui::compose_notes_editable(compose_mode_of(view)) &&
           state.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
           view.scene_available && input.pointer_started_in_view && input.window_focused &&
           input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           !input.compose_note_double_click_in_view && !input.compose_pointer_captured &&
           !state.note_move_candidate_active && !state.area_selection_candidate_active &&
           !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active;
}

auto begin_compose_resource_pointer(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    const auto& view = *input.view;
    auto* action = input.action;
    if (!resource_pointer_can_start(input, paint_pointer_consumed)) return false;
    const auto role = state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample
                          ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                      : state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bgm
                          ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                          : WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
    uint64_t target_lane_id = 0;
    bool in_placement_pane = input.pointer_started_in_note_field;
    if (role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) {
        in_placement_pane = false;
        for (size_t index = 0; index < view.bgm_lane_count; ++index) {
            const auto& lane = view.bgm_lanes[index];
            if (input.pointer_started_x >= lane.bounds.left &&
                input.pointer_started_x < lane.bounds.right &&
                input.pointer_started_y >= lane.bounds.top &&
                input.pointer_started_y < lane.bounds.bottom) {
                target_lane_id = lane.source_lane_id;
                in_placement_pane = true;
                break;
            }
        }
    }
    const auto existing_note = role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE &&
                               compose_point_hits_note(&view, input.pointer_started_x,
                                                       input.pointer_started_y);
    const auto existing_bgm = role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM &&
                              compose_bgm_event_at(&view, input.pointer_started_x,
                                                   input.pointer_started_y) != 0;
    if (!in_placement_pane || existing_note ||
        (existing_bgm && state.resource_pointer_mode ==
                             worm_ui_nuklear_compose_resource_pointer_mode_t::place))
        return false;
    const auto replacing = state.resource_pointer_mode ==
                           worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
    if (replacing && role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM)
        target_lane_id = compose_bgm_event_at(&view, input.pointer_started_x,
                                              input.pointer_started_y);
    append_compose_input(action,
                         replacing ? WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE
                                   : WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE,
                         input.pointer_started_x, input.pointer_started_y, target_lane_id, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                         state.armed_resource_id, role);
    return true;
}

static auto selected_bmson_lane_at(const ComposePointerEventContext& input,
                                   double x, double y) -> bool {
    const auto& view = *input.view;
    for (size_t index = 0; index < view.bgm_lane_count; ++index) {
        const auto& lane = view.bgm_lanes[index];
        if (lane.source_lane_id == view.selected_bmson_source_lane_id &&
            x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom)
            return true;
    }
    return false;
}

static auto bmson_move_lane_at(const ComposePointerEventContext& input,
                               double x, double y) -> uint64_t {
    const auto& view = *input.view;
    for (size_t index = 0; index < view.bgm_lane_count; ++index) {
        const auto& lane = view.bgm_lanes[index];
        if (x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom &&
            (view.selected_bmson_bgm_event_id == 0 ||
             lane.source_lane_id == view.selected_bmson_source_lane_id))
            return lane.source_lane_id;
    }
    return 0;
}

static auto bmson_razor_input_kind() -> WormUiComposeInputKind {
    return state.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::restart
               ? WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART
               : WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE;
}

static auto bmson_razor_can_start(const ComposePointerEventContext& input,
                                  bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed && !input.compose_pointer_captured && view.scene_available &&
           input.window_focused && input.shortcut_event_count == 0 && input.left_down &&
           input.pointer_started_in_view &&
           state.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off &&
           view.selected_bmson_bgm_event_id != 0 &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           selected_bmson_lane_at(input, input.pointer_started_x, input.pointer_started_y);
}

static void update_active_bmson_razor(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    auto& action = *input.action;
    const auto input_kind = bmson_razor_input_kind();
    if (!input.window_focused || !view.scene_available ||
        state.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off ||
        input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        (!input.left_down && !input.left_released)) {
        state.bmson_razor_pointer_active = false;
    } else if (input.left_released) {
        if (input.pointer_inside && selected_bmson_lane_at(input, input.pointer_x, input.pointer_y) &&
            std::abs(input.pointer_y - state.bmson_razor_last_y_pixels) > 0.1)
            append_compose_input(&action, input_kind, input.pointer_x, input.pointer_y, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 view.selected_bmson_bgm_event_id);
        state.bmson_razor_pointer_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input) &&
               input.pointer_inside && selected_bmson_lane_at(input, input.pointer_x, input.pointer_y) &&
               std::abs(input.pointer_y - state.bmson_razor_last_y_pixels) > 0.1) {
        append_compose_input(&action, input_kind, input.pointer_x, input.pointer_y, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                             view.selected_bmson_bgm_event_id);
        state.bmson_razor_last_y_pixels = input.pointer_y;
    }
}

auto process_bmson_razor_pointer(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    if (state.bmson_razor_pointer_active) {
        update_active_bmson_razor(input);
        return true;
    }
    if (!bmson_razor_can_start(input, paint_pointer_consumed)) return paint_pointer_consumed;
    append_compose_input(input.action, bmson_razor_input_kind(), input.pointer_started_x,
                         input.pointer_started_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                         input.view->selected_bmson_bgm_event_id, 0, true);
    state.bmson_razor_pointer_active = !state.action_overflowed;
    state.bmson_razor_last_y_pixels = input.pointer_started_y;
    return true;
}

static void append_bmson_move_input(const ComposePointerEventContext& input,
                                    WormUiComposeInputKind kind, double x, double y,
                                    uint64_t lane_id) {
    append_compose_input(input.action, kind, x, y, lane_id, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                         state.bmson_marker_move_event_id);
}

static void update_active_bmson_marker_move(const ComposePointerEventContext& input) {
    const auto lane_id = bmson_move_lane_at(input, input.pointer_x, input.pointer_y);
    if (!input.window_focused || !input.view->scene_available ||
        input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        (!input.left_down && !input.left_released) || !input.pointer_inside || lane_id == 0) {
        append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL,
                                input.pointer_x, input.pointer_y, lane_id);
        reset_bmson_marker_move_capture();
    } else if (input.left_released) {
        append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE,
                                input.pointer_x, input.pointer_y, lane_id);
        reset_bmson_marker_move_capture();
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE,
                                input.pointer_x, input.pointer_y, lane_id);
    }
}

static void update_bmson_marker_move_candidate(const ComposePointerEventContext& input) {
    if (!input.window_focused || !input.view->scene_available || input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        (!input.left_down && !input.left_released)) {
        reset_bmson_marker_move_capture();
        return;
    }
    const auto dx = input.context->input.mouse.pos.x - state.bmson_marker_move_anchor_screen_x;
    const auto dy = input.context->input.mouse.pos.y - state.bmson_marker_move_anchor_screen_y;
    const auto moved_far_enough = std::hypot(dx, dy) >= 4.0f;
    const auto lane_id = bmson_move_lane_at(input, input.pointer_x, input.pointer_y);
    if (input.left_released) {
        if (moved_far_enough && input.pointer_inside && lane_id != 0) {
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN,
                                    state.bmson_marker_move_anchor_x,
                                    state.bmson_marker_move_anchor_y, 0);
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE,
                                    input.pointer_x, input.pointer_y, lane_id);
        } else {
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
                                    state.bmson_marker_move_anchor_x,
                                    state.bmson_marker_move_anchor_y, 0);
        }
        reset_bmson_marker_move_capture();
    } else if (input.left_down && moved_far_enough) {
        append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN,
                                state.bmson_marker_move_anchor_x,
                                state.bmson_marker_move_anchor_y, 0);
        state.bmson_marker_move_candidate_active = false;
        state.bmson_marker_move_active = !state.action_overflowed;
        if (state.bmson_marker_move_active && input.pointer_inside && lane_id != 0)
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE,
                                    input.pointer_x, input.pointer_y, lane_id);
    }
}

static auto bmson_marker_move_can_start(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed && !input.compose_pointer_captured && view.scene_available &&
           input.pointer_started_in_view && input.left_down && input.window_focused &&
           input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           state.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off &&
           state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
           !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active &&
           view.selected_bgm_event_id != 0 &&
           compose_bgm_event_at(&view, input.pointer_started_x, input.pointer_started_y) ==
               view.selected_bgm_event_id;
}

static void begin_bmson_marker_move_candidate(const ComposePointerEventContext& input) {
    state.bmson_marker_move_candidate_active = true;
    state.bmson_marker_move_event_id = input.view->selected_bgm_event_id;
    state.bmson_marker_move_anchor_x = input.pointer_started_x;
    state.bmson_marker_move_anchor_y = input.pointer_started_y;
    state.bmson_marker_move_anchor_screen_x = input.left_clicked_screen_x;
    state.bmson_marker_move_anchor_screen_y = input.left_clicked_screen_y;
}

auto process_bmson_marker_move_pointer(const ComposePointerEventContext& input,
                                              bool paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    if (state.bmson_marker_move_active) {
        update_active_bmson_marker_move(input);
        return true;
    }
    if (state.bmson_marker_move_candidate_active) {
        update_bmson_marker_move_candidate(input);
        return true;
    }
    if (!bmson_marker_move_can_start(input, paint_pointer_consumed)) return paint_pointer_consumed;
    begin_bmson_marker_move_candidate(input);
    return true;
}

auto process_semantic_layer_paint_pointer(const ComposePointerEventContext& input,
                                                 bool& paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    const auto& view = *input.view;
    auto* action = input.action;
    if (!state.action_overflowed && state.semantic_layer_paint_pointer_active) {
        if (!input.window_focused || !view.scene_available ||
            !view.semantic_layer_paint_mode_active || input.shortcut_event_count != 0 ||
            (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
            (!input.left_down && !input.left_released)) {
            append_compose_cancel_once(action);
            state.semantic_layer_paint_pointer_active = false;
        } else if (input.left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                 input.paint_pointer_x, input.paint_pointer_y);
            state.semantic_layer_paint_pointer_active = false;
        } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
                                 input.paint_pointer_x, input.paint_pointer_y);
        }
        return true;
    }
    if (!state.action_overflowed && view.scene_available &&
        view.semantic_layer_paint_mode_active && input.pointer_started_in_note_field &&
        input.window_focused && input.shortcut_event_count == 0 &&
        (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
        !state.pointer_hold_active && !state.lane_hold_active && !state.area_selection_active &&
        !state.area_selection_candidate_active) {
        paint_pointer_consumed = true;
        if (compose_point_hits_note(&view, input.paint_anchor_x, input.paint_anchor_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
                                 input.paint_anchor_x, input.paint_anchor_y);
            state.semantic_layer_paint_pointer_active = !state.action_overflowed && input.left_down;
            if (!state.action_overflowed && !input.left_down)
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                     input.paint_pointer_x, input.paint_pointer_y);
        }
        return true;
    }
    return false;
}

void process_note_paint_pointer(const ComposePointerEventContext& input,
                                       bool& paint_pointer_consumed) {
    if (input.view->paste_active) { paint_pointer_consumed = true; return; }
    if (compose_area_pointer_consumed()) return;
    const auto& view = *input.view;
    auto* action = input.action;
    if (!state.action_overflowed && state.note_paint_pointer_active) {
        if (!input.window_focused || !view.scene_available || !view.note_paint_mode_active ||
            input.shortcut_event_count != 0 ||
            (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
            (!input.left_down && !input.left_released)) {
            append_compose_cancel_once(action);
            state.note_paint_pointer_active = false;
        } else if (input.left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
                                 input.paint_pointer_x, input.paint_pointer_y);
            state.note_paint_pointer_active = false;
        } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE,
                                 input.paint_pointer_x, input.paint_pointer_y);
        }
        return;
    }
    if (!state.action_overflowed && view.scene_available && view.note_paint_mode_active &&
        input.pointer_started_in_note_field && input.window_focused &&
        input.shortcut_event_count == 0 &&
        (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
        !state.pointer_hold_active && !state.lane_hold_active && !state.area_selection_active &&
        !state.area_selection_candidate_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
                             input.paint_anchor_x, input.paint_anchor_y);
        paint_pointer_consumed = true;
        state.note_paint_pointer_active = !state.action_overflowed && input.left_down;
        if (!state.action_overflowed && !input.left_down)
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
                                 input.paint_pointer_x, input.paint_pointer_y);
    }
}

static auto update_note_move_candidate(const ComposePointerEventContext& input,
                                      bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || paint_pointer_consumed || !state.note_move_candidate_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        state.note_move_candidate_active = false;
    } else if (input.left_released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             state.note_move_anchor_x, state.note_move_anchor_y);
        state.note_move_candidate_active = false;
    } else if (input.left_down) {
        constexpr float drag_threshold_pixels = 4.0f;
        const auto dx = input.context->input.mouse.pos.x - state.note_move_anchor_screen_x;
        const auto dy = input.context->input.mouse.pos.y - state.note_move_anchor_screen_y;
        if (std::hypot(dx, dy) >= drag_threshold_pixels) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
                                 state.note_move_anchor_x, state.note_move_anchor_y);
            state.note_move_candidate_active = false;
            state.note_move_active = !state.action_overflowed;
            if (state.note_move_active)
                append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE,
                                     input.pointer_x, input.pointer_y);
        }
    }
    return true;
}

static auto update_note_move_active(const ComposePointerEventContext& input,
                                    bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || paint_pointer_consumed || !state.note_move_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        state.note_move_active = false;
    } else if (input.left_released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.note_move_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE,
                             input.pointer_x, input.pointer_y);
    }
    return true;
}

static bool area_selection_drag_threshold_reached(const ComposePointerEventContext& input) {
    const auto dx = input.context->input.mouse.pos.x - state.area_selection_anchor_screen_x;
    const auto dy = input.context->input.mouse.pos.y - state.area_selection_anchor_screen_y;
    return std::hypot(dx, dy) >= 4.0f;
}

static void begin_area_selection_drag(const ComposePointerEventContext& input, bool released) {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                         state.area_selection_anchor_x, state.area_selection_anchor_y,
                         0, false, state.area_selection_mode);
    state.area_selection_candidate_active = false;
    state.area_selection_active = !state.action_overflowed;
    if (!state.area_selection_active) return;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                         input.pointer_x, input.pointer_y);
    if (released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.area_selection_active = false;
    }
}

static void release_compose_selection_click(const ComposePointerEventContext& input) {
    if (state.area_selection_anchor_x >= input.note_field_left &&
        state.area_selection_anchor_x < input.note_field_right)
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             state.area_selection_anchor_x, state.area_selection_anchor_y,
                             0, false, state.area_selection_mode);
    else if (input.view->bms_measure_labels &&
             state.area_selection_anchor_x < compose_measure_rail_width)
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
                             state.area_selection_anchor_x, state.area_selection_anchor_y);
}

static auto update_area_selection_candidate(const ComposePointerEventContext& input,
                                            bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || paint_pointer_consumed ||
        !state.area_selection_candidate_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        if (input.window_focused)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        state.area_selection_candidate_active = false;
    } else if (input.left_released) {
        if (area_selection_drag_threshold_reached(input)) {
            begin_area_selection_drag(input, true);
        } else {
            release_compose_selection_click(input);
            state.area_selection_candidate_active = false;
        }
    } else if (input.left_down && area_selection_drag_threshold_reached(input)) {
        begin_area_selection_drag(input, false);
    }
    return true;
}

static auto update_area_selection_active(const ComposePointerEventContext& input,
                                         bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || paint_pointer_consumed || !state.area_selection_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        state.area_selection_active = false;
    } else if (input.left_released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.area_selection_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                             input.pointer_x, input.pointer_y);
    }
    return true;
}

void process_compose_drag_gestures(const ComposePointerEventContext& input,
                                          bool paint_pointer_consumed) {
    if (input.view->paste_active) return;
    if (compose_area_pointer_consumed()) return;
    if (update_note_move_candidate(input, paint_pointer_consumed)) return;
    if (update_note_move_active(input, paint_pointer_consumed)) return;
    if (update_area_selection_candidate(input, paint_pointer_consumed)) return;
    update_area_selection_active(input, paint_pointer_consumed);
}

static auto handle_compose_measure_click(const ComposePointerEventContext& input,
                                         bool paint_pointer_consumed) -> bool {
    if (state.action_overflowed || paint_pointer_consumed || !input.view->scene_available ||
        !input.view->bms_measure_labels || !input.pointer_clicked || !input.pointer_inside ||
        input.pointer_x >= 42.0 || !input.window_focused || state.pointer_hold_active ||
        state.lane_hold_active || state.note_move_active || state.area_selection_active ||
        state.area_selection_candidate_active ||
        state.timing_stage_drag_active)
        return false;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
                         input.pointer_x, input.pointer_y);
    return true;
}

static auto handle_compose_alt_pointer(const ComposePointerEventContext& input,
                                       bool paint_pointer_consumed) -> bool {
    auto* action = input.action;
    if (!state.action_overflowed && !paint_pointer_consumed && input.view->scene_available &&
        input.pointer_started_in_note_field && input.alt_down && !state.pointer_hold_active &&
        input.window_focused && !input.compose_note_double_click_in_view &&
        wormedit::ui::compose_notes_editable(compose_mode_of(*input.view))) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN,
                             input.pointer_x, input.pointer_y);
        state.pointer_hold_active = !state.action_overflowed;
        return true;
    }
    if (state.action_overflowed || paint_pointer_consumed || !state.pointer_hold_active)
        return false;
    if (!input.window_focused || !input.alt_down) {
        append_compose_cancel_once(action);
        state.pointer_hold_active = false;
    } else if (input.left_released) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.pointer_hold_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
                             input.pointer_x, input.pointer_y);
    }
    return true;
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
                                               input.note_field_left);
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
    action.timing_target_y_fraction =
        compose_timing_target_fraction(view, input.pointer_started_y);
    return true;
}

static auto begin_compose_timing_marker_drag(const ComposePointerEventContext& input,
                                             bool paint_pointer_consumed) -> bool {
    if (paint_pointer_consumed || !input.view->scene_available || !input.window_focused ||
        !input.pointer_started_in_view || !input.left_down ||
        input.pointer_started_x < compose_measure_rail_width ||
        input.pointer_started_x >= input.note_field_left ||
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
    if (compose_area_pointer_consumed()) return true;
    if (update_compose_timing_marker_drag(input)) return true;
    return begin_compose_timing_marker_drag(input, paint_pointer_consumed);
}

static bool compose_pointer_start_hits_element(const ComposePointerEventContext& input) {
    return compose_point_hits_note(input.view, input.pointer_started_x, input.pointer_started_y) ||
        compose_bgm_event_at(input.view, input.pointer_started_x, input.pointer_started_y) != 0 ||
        compose_timing_marker_at(*input.view, input.pointer_started_x, input.pointer_started_y);
}

static void start_compose_note_move_candidate(const ComposePointerEventContext& input) {
    const auto can_move = wormedit::ui::compose_notes_editable(compose_mode_of(*input.view));
    if (!can_move || !compose_point_hits_selected_note(input.view, input.pointer_started_x,
                                                       input.pointer_started_y))
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
    if (state.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !input.pointer_clicked || !input.pointer_inside || input.pointer_x < input.note_field_right ||
        !input.window_focused || state.pointer_hold_active || state.note_move_active ||
        state.note_move_candidate_active || state.area_selection_active ||
        state.area_selection_candidate_active)
        return false;
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
    if (compose_area_pointer_consumed()) return;
    if (handle_compose_measure_click(input, paint_pointer_consumed)) return;
    if (handle_compose_alt_pointer(input, paint_pointer_consumed)) return;
    if (handle_compose_note_field_pointer_start(input, paint_pointer_consumed)) return;
    if (handle_compose_note_field_click(input, paint_pointer_consumed)) return;
    handle_compose_bgm_click(input, paint_pointer_consumed);
}
