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

#include "ui/nuklear/compose_input_internal.h"
#include "ui/nuklear/compose_keyboard_input.h"
#include "ui/nuklear/compose_area_menu.h"

#include "ui/nuklear/compose_input_internal.h"
#include "ui/nuklear/compose_object_input.h"

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

static bool open_bga_stack_for_event(const WormUiComposeView& view, std::uint64_t event_id) {
    if (event_id == 0) return false;
    const auto events = std::span(view.bga_events, view.bga_event_count);
    const auto target = std::ranges::find(events, event_id,
        &WormUiComposeBgaEventView::event_id);
    if (target == events.end() || target->overlap_count <=
            WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) return false;
    state.bga_stack_open = true;
    state.bga_stack_project_id = view.project_id;
    state.bga_stack_chart_id = view.chart_id;
    state.bga_stack_revision = view.document_revision;
    state.bga_stack_row = target->canvas_row;
    return true;
}

static bool prepare_bga_pointer_target(const WormUiComposeView& view, double x, double y,
                                       bool& in_placement_pane, std::uint64_t& target_id) {
    in_placement_pane = view.bga_lane_visible && x >= view.bga_lane_bounds.left &&
        x < view.bga_lane_bounds.right && y >= view.bga_lane_bounds.top &&
        y < view.bga_lane_bounds.bottom;
    target_id = compose_bga_event_at(&view, x, y);
    return open_bga_stack_for_event(view, target_id);
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
    } else if (role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA) {
        if (prepare_bga_pointer_target(view, input.pointer_started_x,
                input.pointer_started_y, in_placement_pane, target_lane_id)) return true;
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
    if (replacing && role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA && target_lane_id == 0)
        return false;
    append_compose_input(action,
                         replacing ? WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE
                                   : WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE,
                         input.pointer_started_x, input.pointer_started_y, target_lane_id, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                         state.armed_resource_id, role);
    return true;
}

auto selected_bmson_lane_at(const ComposePointerEventContext& input,
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

auto bmson_razor_input_kind() -> WormUiComposeInputKind {
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
        view.semantic_layer_paint_mode_active && input.pointer_started_in_view &&
        input.window_focused && input.shortcut_event_count == 0 &&
        (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
        !state.pointer_hold_active && !state.lane_hold_active && !state.area_selection_active &&
        !state.area_selection_candidate_active) {
        paint_pointer_consumed = true;
        if (compose_point_hits_note(&view, input.paint_anchor_x, input.paint_anchor_y) ||
            compose_bgm_event_at(&view, input.paint_anchor_x, input.paint_anchor_y) != 0) {
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

// The Sounds dock owns the sound choice; note paint carries it with each stroke.
static auto armed_note_paint_sound(const WormUiComposeView& view) -> uint64_t {
    if (state.armed_resource_id == 0 || !state.armed_from_sounds_dock) return 0;
    for (size_t index = 0; index < view.keysound_count; ++index) {
        const auto& row = view.keysounds[index];
        if (row.resource_id == state.armed_resource_id) return row.missing ? 0 : row.resource_id;
    }
    return 0;
}

static void append_note_paint_begin(const ComposePointerEventContext& input) {
    auto* action = input.action;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
                         input.paint_anchor_x, input.paint_anchor_y);
    if (!state.action_overflowed && action->compose_input_count > 0)
        action->compose_inputs[action->compose_input_count - 1].resource_id =
            armed_note_paint_sound(*input.view);
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
        append_note_paint_begin(input);
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
        if (state.note_move_select_on_release)
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

auto handle_compose_measure_click(const ComposePointerEventContext& input,
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

auto handle_compose_alt_pointer(const ComposePointerEventContext& input,
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
