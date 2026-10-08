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

#include <wormedit/core/document.h>

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

static auto stem_lane_resize_edge(const ComposePointerEventContext& input, double x, double y)
    -> const WormUiComposeBgmLaneView* {
    const auto& view = *input.view;
    const auto tolerance = 4.0 * view.viewport_width_pixels / input.viewport.w;
    const auto header_height = 18.0 * view.viewport_height_pixels / input.viewport.h;
    if (y < 0 || y >= header_height) return nullptr;
    for (const auto& lane : std::span(view.bgm_lanes, view.bgm_lane_count)) {
        if (!lane.stem || lane.resize_edge_pixels > lane.bounds.right + 1e-6 ||
            lane.resize_edge_pixels < lane.bounds.left || x < lane.bounds.left ||
            x > lane.bounds.right + tolerance) continue;
        if (std::abs(x - lane.resize_edge_pixels) <= tolerance) return &lane;
    }
    return nullptr;
}
static auto update_stem_lane_resize(const ComposePointerEventContext& input) -> bool {
    if (!state.stem_lane_resize) return false;
    const auto resize = *state.stem_lane_resize;
    const auto [project, chart, lane] = resize.key;
    const auto& view = *input.view;
    if (!input.window_focused || view.input_occluded || view.project_id != project ||
        view.chart_id != chart || compose_action_has_cancel(input.action) ||
        (!input.left_down && !input.left_released)) {
        worm_ui_nuklear_cancel_stem_lane_resize(state);
        return true;
    }
    state.stem_lane_resize_cursor_requested = true;
    worm_ui_nuklear_set_stem_lane_width(state, project, chart, lane,
        resize.anchor_width + input.pointer_x - resize.anchor_x);
    if (input.left_released) worm_ui_nuklear_cancel_stem_lane_resize(state, false);
    return true;
}
static auto reset_stem_lane_from_double_click(const ComposePointerEventContext& input,
    const worm_ui_nuklear_pointer_click_t* clicks, std::size_t count) -> bool {
    for (const auto& click : std::span(clicks, count)) {
        if (click.x < input.viewport.x || click.x >= input.viewport.x + input.viewport.w ||
            click.y < input.viewport.y || click.y >= input.viewport.y + input.viewport.h) continue;
        const auto x = (click.x - input.viewport.x) * input.view->viewport_width_pixels / input.viewport.w;
        const auto y = (click.y - input.viewport.y) * input.view->viewport_height_pixels / input.viewport.h;
        if (const auto* lane = stem_lane_resize_edge(input, x, y)) {
            worm_ui_nuklear_cancel_stem_lane_resize(state, false);
            worm_ui_nuklear_set_stem_lane_width(state, input.view->project_id,
                input.view->chart_id, lane->source_lane_id, 36);
            state.stem_lane_resize_cursor_requested = true;
            return true;
        }
    }
    return false;
}
auto process_stem_lane_resize(const ComposePointerEventContext& input,
    const worm_ui_nuklear_pointer_click_t* double_clicks, std::size_t double_click_count,
    bool other_pointer_captured) -> bool {
    if (update_stem_lane_resize(input)) return true;
    const auto& view = *input.view;
    if (other_pointer_captured || !input.window_focused || view.input_occluded ||
        !view.scene_available || !view.bmson_tools_available) return false;
    if (reset_stem_lane_from_double_click(input, double_clicks, double_click_count)) return true;
    const auto* hovered = stem_lane_resize_edge(input, input.pointer_x, input.pointer_y);
    if (hovered && input.pointer_inside) state.stem_lane_resize_cursor_requested = true;
    if (!input.pointer_started_in_view || !input.left_down) return false;
    const auto* lane = stem_lane_resize_edge(input, input.pointer_started_x, input.pointer_started_y);
    if (!lane) return false;
    state.stem_lane_resize = worm_ui_nuklear_compose_state_t::stem_lane_resize_t{
        {view.project_id, view.chart_id, lane->source_lane_id}, input.pointer_started_x, lane->width_pixels};
    return update_stem_lane_resize(input);
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

static bool open_bga_stack_for_event(const WormUiComposeView& view, std::uint64_t event_id) {
    if (event_id == 0) return false;
    const auto events = std::span(view.bga_events, view.bga_event_count);
    const auto target = std::ranges::find(events, event_id,
        &WormUiComposeBgaEventView::event_id);
    if (target == events.end() || target->overlap_count <=
            WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) return false;
    state.bga_stack_open = true;
    state.bga_stack_release_pending = true;
    state.bga_stack_project_id = view.project_id;
    state.bga_stack_chart_id = view.chart_id;
    state.bga_stack_revision = view.document_revision;
    state.bga_stack_row = target->canvas_row;
    state.bga_stack_kind = target->kind;
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

static void set_bga_pointer_kind(WormUiWorkspaceAction* action, std::uint8_t role,
                                 const WormUiComposeView& view, double x, double y) {
    if (role != WORM_UI_COMPOSE_RESOURCE_ROLE_BGA || action->compose_input_count == 0) return;
    auto& output = action->compose_inputs[action->compose_input_count - 1];
    output.bga_lane_kind = compose_bga_lane_kind_at(view, x, y).value_or(255);
    output.bga_lane_kind_valid = true;
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
    set_bga_pointer_kind(action, role, view, input.pointer_started_x, input.pointer_started_y);
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
           bmson_razor_eligible(view) &&
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
    if (state.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off &&
        !bmson_razor_eligible(*input.view))
        state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
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

// The playfield lane whose column holds the pointer, or 0 outside the playable field.
static auto field_lane_under_pointer(const ComposePointerEventContext& input) -> uint64_t {
    const auto& view = *input.view;
    if (!input.pointer_inside || input.pointer_x < input.note_field_left || input.pointer_x >= input.note_field_right)
        return 0;
    for (size_t index = 0; index < view.lane_count; ++index) {
        const auto& lane = view.lanes[index];
        if (input.pointer_x >= lane.bounds.left && input.pointer_x < lane.bounds.right) return lane.lane_id;
    }
    return 0;
}

static void update_active_bmson_marker_move(const ComposePointerEventContext& input) {
    const auto lane_id = bmson_move_lane_at(input, input.pointer_x, input.pointer_y);
    if (lane_id == 0 && field_lane_under_pointer(input) != 0 && input.window_focused &&
        input.view->scene_available && input.shortcut_event_count == 0 &&
        (input.pointer_modifiers & input.any_pointer_modifier) == 0 && (input.left_down || input.left_released)) {
        // Over the playfield the marker waits; dropping it there turns it into a tap in that lane.
        if (input.left_released) {
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL,
                                    input.pointer_x, input.pointer_y, 0);
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE, input.pointer_x,
                                 input.pointer_y, field_lane_under_pointer(input), false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE, WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 state.bmson_marker_move_event_id);
            reset_bmson_marker_move_capture();
        }
        return;
    }
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
    if (input.left_released && moved_far_enough && lane_id == 0 && field_lane_under_pointer(input) != 0) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE, input.pointer_x, input.pointer_y,
                             field_lane_under_pointer(input), false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.bmson_marker_move_event_id);
        reset_bmson_marker_move_capture();
        return;
    }
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

void append_note_paint_begin(const ComposePointerEventContext& input) {
    auto* action = input.action;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
                         input.paint_anchor_x, input.paint_anchor_y);
    worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *action);
}

// Hold and Roll need an explicit end, so their press goes to the click-click hold gesture and
// never starts a paint stroke (the core rejects the stroke and the UI would keep updating it).
static auto armed_kind_uses_hold_gesture(const WormUiComposeView& view) -> bool {
    return view.armed_note_kind == static_cast<uint32_t>(wormedit::note_kind_t::hold) ||
           view.armed_note_kind == static_cast<uint32_t>(wormedit::note_kind_t::roll);
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
        !armed_kind_uses_hold_gesture(view) && input.pointer_started_in_note_field &&
        input.window_focused &&
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

// The pointer is over the BGM area, to the right of the playable field.
static auto note_move_over_bgm_pane(const ComposePointerEventContext& input) -> bool {
    return input.view->bgm_lane_count != 0 && input.pointer_inside && input.pointer_x >= input.note_field_right;
}

// The BGM source lane whose column holds the pointer, or 0 between or beyond the columns.
static auto bgm_lane_under_pointer(const ComposePointerEventContext& input) -> uint64_t {
    const auto& view = *input.view;
    for (size_t index = 0; index < view.bgm_lane_count; ++index) {
        const auto& lane = view.bgm_lanes[index];
        if (input.pointer_x >= lane.bounds.left && input.pointer_x < lane.bounds.right)
            return lane.source_lane_id;
    }
    return 0;
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
            if (state.note_move_active && !note_move_over_bgm_pane(input))
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
    } else if (input.left_released && note_move_over_bgm_pane(input)) {
        // Dropped on the BGM area: the notes become BGM events instead of moving in the field.
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_TO_BGM,
                             input.pointer_x, input.pointer_y, bgm_lane_under_pointer(input));
        state.note_move_active = false;
    } else if (input.left_released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.note_move_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input) &&
               !note_move_over_bgm_pane(input)) {
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

// With Hold or Roll armed, press-drag draws the hold (press = head, release = tail) instead of
// starting an area selection; a press without a drag stays the click-click gesture.
static void begin_pointer_hold_drag(const ComposePointerEventContext& input, bool released) {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN,
                         state.area_selection_anchor_x, state.area_selection_anchor_y);
    worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *input.action);
    state.area_selection_candidate_active = false;
    state.pointer_hold_active = !state.action_overflowed;
    state.pointer_hold_without_alt = state.pointer_hold_active;
    if (!state.pointer_hold_active) return;
    append_compose_input(input.action,
                         released ? WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE
                                  : WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
                         input.pointer_x, input.pointer_y);
    if (released) {
        state.pointer_hold_active = false;
        state.pointer_hold_without_alt = false;
    }
}

static void begin_area_selection_drag(const ComposePointerEventContext& input, bool released) {
    if (input.view->note_paint_mode_active && armed_kind_uses_hold_gesture(*input.view)) {
        begin_pointer_hold_drag(input, released);
        return;
    }
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

// A marker of the stem lane under the pointer; it names the sound channel a new point joins.
static auto stem_lane_reference_marker(const WormUiComposeView& view, double x, double y) -> uint64_t {
    for (size_t index = 0; index < view.bgm_lane_count; ++index) {
        const auto& lane = view.bgm_lanes[index];
        if (x < lane.bounds.left || x >= lane.bounds.right || y < lane.bounds.top || y >= lane.bounds.bottom)
            continue;
        const auto events = std::span(view.bgm_events, view.bgm_event_count);
        const auto marker = std::ranges::find_if(events, [&](const auto& event) {
            return event.bmson_slice && event.source_lane_id == lane.source_lane_id;
        });
        return marker == events.end() ? 0 : marker->event_id;
    }
    return 0;
}

// Click on empty space in a stem lane: add a continue point, or a restart point with Shift.
static auto add_bmson_stem_point(const ComposePointerEventContext& input, double x, double y) -> bool {
    const auto& view = *input.view;
    // Plain or Shift (either key) only; any other modifier keeps its own meaning.
    const auto shift_only = (input.pointer_modifiers & input.any_pointer_modifier & ~SDL_KMOD_SHIFT) == 0;
    if (!shift_only || state.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off ||
        state.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off ||
        view.note_paint_mode_active || view.semantic_layer_paint_mode_active || view.timing_focus)
        return false;
    const auto reference = stem_lane_reference_marker(view, x, y);
    if (reference == 0) return false;
    const auto restart = (input.pointer_modifiers & SDL_KMOD_SHIFT) != 0;
    append_compose_input(input.action, restart ? WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART
                                               : WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE,
                         x, y, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, reference);
    return true;
}

static void release_compose_selection_click(const ComposePointerEventContext& input) {
    if (state.area_selection_anchor_x >= input.note_field_left &&
        state.area_selection_anchor_x < input.note_field_right) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             state.area_selection_anchor_x, state.area_selection_anchor_y,
                             0, false, state.area_selection_mode);
        worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *input.action);
    } else if (input.view->bms_measure_labels &&
               state.area_selection_anchor_x < compose_measure_rail_width) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
                             state.area_selection_anchor_x, state.area_selection_anchor_y);
    } else {
        // A click without a drag on empty space in a stem lane adds a point there.
        static_cast<void>(add_bmson_stem_point(input, state.area_selection_anchor_x,
                                               state.area_selection_anchor_y));
    }
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
        worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *action);
        state.pointer_hold_active = !state.action_overflowed;
        state.pointer_hold_without_alt = false;
        return true;
    }
    if (state.action_overflowed || paint_pointer_consumed || !state.pointer_hold_active)
        return false;
    if (!input.window_focused || (!input.alt_down && !state.pointer_hold_without_alt)) {
        append_compose_cancel_once(action);
        state.pointer_hold_active = false;
        state.pointer_hold_without_alt = false;
    } else if (input.left_released) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.pointer_hold_active = false;
        state.pointer_hold_without_alt = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
                             input.pointer_x, input.pointer_y);
    }
    return true;
}
