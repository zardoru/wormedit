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
#include "ui/nuklear/compose_timing_input.h"
#include "ui/compose_timing_geometry.h"

static constexpr double span_edge_tolerance_pixels = compose_drag_capture_t::edge_tolerance_pixels;

static void update_active_timing_stage_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto& timing = *input.timing_view;
    auto& action = *input.action;
    const auto& pointer = input.context->input.mouse.pos;
    const auto continuation = state.timing_drag.timing_stage_capture.advance(
        {.down = input.left_down, .released = input.left_released, .x = pointer.x, .y = pointer.y},
        input.window_focused && state.timing_drag.timing_stage_capture.matches(view, view.waveform_timing_locked) &&
            timing.stage_active && bool(timing.stage_existing_object_id),
        compose_drag_release_policy_t::clear_on_release);
    // Overflow still cancels through the outer registry; release clears only the
    // UI capture, leaving the staged edit available for explicit confirmation.
    if (continuation.phase != compose_drag_phase_t::update) {
        state.timing_drag.timing_stage_drag_active = false;
        state.timing_drag.timing_stage_capture.cancel();
    } else if (continuation.moved && nk_input_is_mouse_moved(&input.context->input) &&
               action.timing_stage_action == wormedit::ui::timing_stage_action_t::none) {
        action.timing_stage_action = wormedit::ui::timing_stage_action_t::move;
        action.timing_chart_id.value = state.timing_drag.timing_stage_capture.chart;
        action.timing_document_revision = state.timing_drag.timing_stage_capture.revision;
        action.timing_object_id.value = timing.stage_existing_object_id.value;
        action.timing_lane = timing.stage_lane;
        action.timing_has_target_y = true;
        action.timing_target = compose_timing_target(view, input.pointer_y);
    }
}

static void start_timing_stage_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto& timing = *input.timing_view;
    if (!input.window_focused || !input.pointer_started_in_view || !input.left_down ||
        !view.timing_stage_active || view.y_lock ||
        !timing.stage_active || !timing.stage_existing_object_id ||
        input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0)
        return;
    const auto extent = wormedit::ui::compose_timing_lane_extent(view, view.timing_stage_lane);
    if (!extent) return;
    const auto y_tolerance = 8.0 * view.viewport_height_pixels / input.viewport.h;
    if (input.pointer_started_x >= extent->first && input.pointer_started_x < extent->second &&
        std::abs(input.pointer_started_y - view.timing_stage_y_pixels) <= y_tolerance)
        state.timing_drag.timing_stage_drag_active = state.timing_drag.timing_stage_capture.begin(
            view, input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x,
            input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y, view.waveform_timing_locked);
}

void update_timing_stage_drag(const ComposePointerEventContext& input) {
    if (compose_area_pointer_consumed()) return;
    if (state.timing_drag.timing_stage_drag_active)
        update_active_timing_stage_drag(input);
    else
        start_timing_stage_drag(input);
}

// A press in the Kiai lane either grabs an edge (head or tail), selects an area by its body, or
// starts a new area on empty lane; releasing after a drag commits one edit. Timing markers keep
// priority wherever they sit, and the gesture stays out of any other active pointer capture.
static auto kiai_gesture_blocked(const ComposePointerEventContext& input) -> bool {
    // Y-lock: Kiai and speed spans only move vertically.
    return !input.window_focused || !input.view->scene_available || input.shortcut_event_count != 0 ||
           input.view->y_lock || input.view->waveform_timing_locked || input.view->input_occluded ||
           (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
           input.timing_view->stage_active || state.timing_drag.timing_stage_drag_active ||
           state.inline_timing.inline_timing_open || state.timing_marker.timing_marker_candidate_active ||
           state.timing_marker.timing_marker_drag_active || compose_area_pointer_consumed();
}

static void begin_speed_drag(const ComposePointerEventContext& input);
static void begin_speed_stretch(const ComposePointerEventContext& input);

static auto begin_span_capture(const ComposePointerEventContext& input, compose_drag_capture_t& capture)
    -> bool {
    const auto& press = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos;
    return capture.begin(*input.view, press.x, press.y, input.view->waveform_timing_locked);
}

// Shared continuation policy; each verb only supplies its commit payload.
static auto continue_span_capture(const ComposePointerEventContext& input, compose_drag_capture_t& capture,
                                  bool& active, bool& moved, double& current_y) -> bool {
    const auto& pointer = input.context->input.mouse.pos;
    const auto continuation = capture.advance(
        {.down = input.left_down, .released = input.left_released, .x = pointer.x, .y = pointer.y},
        input.window_focused && !state.input_queue.action_overflowed &&
            capture.matches(*input.view, input.view->waveform_timing_locked),
        compose_drag_release_policy_t::latched_vertical, moved);
    if (continuation.phase == compose_drag_phase_t::cancel) {
        active = moved = false;
        capture.cancel();
        return false;
    }
    current_y = std::clamp(input.pointer_y, 0.0, input.view->viewport_height_pixels);
    moved = continuation.moved;
    if (continuation.phase == compose_drag_phase_t::update) return false;
    active = false;
    return continuation.phase == compose_drag_phase_t::commit;
}

static void begin_kiai_drag(const ComposePointerEventContext& input) {
    state.timing_drag.kiai_drag_speed = false;
    const auto& view = *input.view;
    const auto extent = compose_timing_lane_inner_extent(view, wormedit::timing_lane_kind_t::kiai);
    const auto x = input.pointer_started_x;
    const auto y = input.pointer_started_y;
    if (!extent || x < extent->first || x >= extent->second ||
        compose_timing_marker_at(view, x, y) != nullptr) return;
    const auto tolerance = span_edge_tolerance_pixels * view.viewport_height_pixels / input.viewport.h;
    state.timing_drag.kiai_drag_edge = WORM_UI_SPAN_HANDLE_CREATE;
    state.timing_drag.kiai_drag_head_id = 0;
    std::uint64_t body_head = 0;
    for (std::size_t index = 0; index < view.kiai_spans.size(); ++index) {
        const auto& span = view.kiai_spans[index];
        const auto head_y = view.downscroll ? span.bounds.bottom : span.bounds.top;
        const auto tail_y = view.downscroll ? span.bounds.top : span.bounds.bottom;
        if (span.head_visible && std::abs(y - head_y) <= tolerance) {
            state.timing_drag.kiai_drag_edge = WORM_UI_SPAN_HANDLE_BEGIN;
            state.timing_drag.kiai_drag_head_id = span.head_object_id.value;
        } else if (span.tail_visible && std::abs(y - tail_y) <= tolerance) {
            state.timing_drag.kiai_drag_edge = WORM_UI_SPAN_HANDLE_END;
            state.timing_drag.kiai_drag_head_id = span.head_object_id.value;
        } else if (y > span.bounds.top && y < span.bounds.bottom) {
            body_head = span.head_object_id.value;
        }
        if (state.timing_drag.kiai_drag_head_id != 0) break;
    }
    if (state.timing_drag.kiai_drag_head_id == 0 && body_head != 0) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT, x, y, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, body_head);
        // The body is selected on press and moves the whole area if it is then dragged.
        state.timing_drag.kiai_drag_edge = WORM_UI_SPAN_HANDLE_BODY;
        state.timing_drag.kiai_drag_head_id = body_head;
    }
    state.timing_drag.kiai_drag_active = begin_span_capture(input, state.timing_drag.span_capture);
    state.timing_drag.kiai_drag_moved = false;
    state.timing_drag.kiai_drag_anchor_y = y;
    state.timing_drag.kiai_drag_current_y = y;
}

static void commit_kiai_drag(const ComposePointerEventContext& input) {
    append_compose_input(input.action,
                         state.timing_drag.kiai_drag_speed ? WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT
                                               : WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT,
                         input.pointer_started_x, state.timing_drag.kiai_drag_current_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.timing_drag.kiai_drag_head_id, 0,
                         false,
                         state.timing_drag.kiai_drag_speed ? wormedit::timing_lane_kind_t::speed : wormedit::timing_lane_kind_t::kiai,
                         state.timing_drag.span_capture.chart, state.timing_drag.span_capture.revision);
    if (input.action->compose_inputs.size() == 0) return;
    auto& queued = input.action->compose_inputs[input.action->compose_inputs.size() - 1];
    queued.span_edge = state.timing_drag.kiai_drag_edge;
    queued.span_anchor_y_pixels = state.timing_drag.kiai_drag_anchor_y;
}

static void continue_kiai_drag(const ComposePointerEventContext& input) {
    if (continue_span_capture(input, state.timing_drag.span_capture, state.timing_drag.kiai_drag_active,
                              state.timing_drag.kiai_drag_moved, state.timing_drag.kiai_drag_current_y))
        commit_kiai_drag(input);
    if (!state.timing_drag.kiai_drag_active) state.timing_drag.span_capture.cancel();
}

void update_kiai_span_drag(const ComposePointerEventContext& input) {
    if (state.timing_drag.kiai_drag_active) {
        continue_kiai_drag(input);
        return;
    }
    if (input.pointer_started_in_view && input.left_down && !input.left_released &&
        !kiai_gesture_blocked(input)) {
        begin_kiai_drag(input);
        if (!state.timing_drag.kiai_drag_active) begin_speed_drag(input);
    } else if (input.pointer_started_in_view && input.left_down && !input.left_released) {
        begin_speed_stretch(input);
    }
}

// SV recipe areas share the Kiai gesture shape: a press on a begin or end edge resizes, a press
// on the body selects and drags the whole span, a click on empty lane clears the selection.
// Releasing after a drag commits one edit; markers keep priority wherever they sit.
static auto recipe_gesture_blocked(const ComposePointerEventContext& input) -> bool {
    return kiai_gesture_blocked(input) || state.timing_drag.kiai_drag_active;
}

static auto recipe_area_extent(const wormedit::ui::compose_view_t& view, const wormedit::ui::compose_recipe_area_view_t& area) {
    return compose_timing_lane_inner_extent(view, static_cast<wormedit::timing_lane_kind_t>(area.lane));
}

static auto point_in_recipe_lane(const wormedit::ui::compose_view_t& view, double x) -> bool {
    for (const auto lane : {wormedit::timing_lane_kind_t::scroll, wormedit::timing_lane_kind_t::speed}) {
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

static auto recipe_press_at(const wormedit::ui::compose_view_t& view, double x, double y, double tolerance)
    -> std::optional<recipe_press_t> {
    std::optional<recipe_press_t> body;
    for (std::size_t index = view.recipe_areas.size(); index-- > 0;) {
        const auto& area = view.recipe_areas[index];
        const auto extent = recipe_area_extent(view, area);
        if (!extent || x < extent->first || x >= extent->second) continue;
        const auto begin_y = view.downscroll ? area.bounds.bottom : area.bounds.top;
        const auto end_y = view.downscroll ? area.bounds.top : area.bounds.bottom;
        if (area.begin_visible && std::abs(y - begin_y) <= tolerance) return recipe_press_t{area.recipe_id.value, 0};
        if (area.end_visible && std::abs(y - end_y) <= tolerance) return recipe_press_t{area.recipe_id.value, 1};
        if (!body && y > area.bounds.top && y < area.bounds.bottom)
            body = recipe_press_t{area.recipe_id.value, 2};
    }
    return body;
}

// A press on the begin or end edge of a Speed span grabs it: the begin edge moves the event, the
// end edge sets how long it interpolates. Recipe areas sharing the lane keep priority, and the
// drag reuses the Kiai drag state (kiai_drag_speed marks it).
static void begin_speed_drag(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto extent = compose_timing_lane_inner_extent(view, wormedit::timing_lane_kind_t::speed);
    const auto x = input.pointer_started_x;
    const auto y = input.pointer_started_y;
    if (!extent || x < extent->first || x >= extent->second || state.timing_drag.recipe_drag_active ||
        compose_timing_marker_at(view, x, y) != nullptr) return;
    const auto tolerance = span_edge_tolerance_pixels * view.viewport_height_pixels / input.viewport.h;
    if (recipe_press_at(view, x, y, tolerance)) return;
    for (std::size_t index = 0; index < view.speed_spans.size(); ++index) {
        const auto& span = view.speed_spans[index];
        const auto begin_y = view.downscroll ? span.bounds.bottom : span.bounds.top;
        const auto end_y = view.downscroll ? span.bounds.top : span.bounds.bottom;
        // Of two coincident edges (an instant Speed) the end wins: it is the one that can grow.
        const bool on_end = std::abs(y - end_y) <= tolerance;
        const bool on_body = y > span.bounds.top && y < span.bounds.bottom;
        if (!on_end && std::abs(y - begin_y) > tolerance && !on_body) continue;
        state.timing_drag.kiai_drag_edge = on_end ? WORM_UI_SPAN_HANDLE_END
            : on_body && std::abs(y - begin_y) > tolerance ? WORM_UI_SPAN_HANDLE_BODY
                                                            : WORM_UI_SPAN_HANDLE_BEGIN;
        state.timing_drag.kiai_drag_head_id = span.object_id.value;
        state.timing_drag.kiai_drag_speed = true;
        state.timing_drag.kiai_drag_active = begin_span_capture(input, state.timing_drag.span_capture);
        state.timing_drag.kiai_drag_moved = false;
        state.timing_drag.kiai_drag_anchor_y = y;
        state.timing_drag.kiai_drag_current_y = y;
        return;
    }
}

// Shift+drag on a Speed marker stretches it into a span: the marker stays where it is and the
// pointer sets where the transition ends. The marker covers its own edge, so this is the way to
// grow an instant Speed; it shares the Kiai drag state like the edge grab above.
static void begin_speed_stretch(const ComposePointerEventContext& input) {
    constexpr auto other_modifiers = SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI;
    if (state.timing_drag.kiai_drag_active || !input.window_focused || !input.view->scene_available ||
        input.view->y_lock || input.view->waveform_timing_locked || input.view->input_occluded ||
        input.shortcut_event_count != 0 || (input.pointer_modifiers & SDL_KMOD_SHIFT) == 0 ||
        (input.pointer_modifiers & other_modifiers) != 0 || input.timing_view->stage_active ||
        state.timing_drag.timing_stage_drag_active || state.inline_timing.inline_timing_open || compose_area_pointer_consumed())
        return;
    const auto* marker = compose_timing_marker_at(*input.view, input.pointer_started_x,
                                                  input.pointer_started_y);
    if (!marker || marker->lane != wormedit::timing_lane_kind_t::speed || marker->object_id.value == 0 ||
        marker->fixed_initial)
        return;
    state.timing_drag.kiai_drag_edge = WORM_UI_SPAN_HANDLE_END;
    state.timing_drag.kiai_drag_head_id = marker->object_id.value;
    state.timing_drag.kiai_drag_speed = true;
    state.timing_drag.kiai_drag_active = begin_span_capture(input, state.timing_drag.span_capture);
    state.timing_drag.kiai_drag_moved = false;
    state.timing_drag.kiai_drag_anchor_y = input.pointer_started_y;
    state.timing_drag.kiai_drag_current_y = input.pointer_started_y;
}

static void begin_recipe_drag(const ComposePointerEventContext& input) {
    if (input.view->y_lock) return;
    const auto& view = *input.view;
    const auto x = input.pointer_started_x;
    const auto y = input.pointer_started_y;
    if (!point_in_recipe_lane(view, x) || compose_timing_marker_at(view, x, y) != nullptr) return;
    const auto tolerance = span_edge_tolerance_pixels * view.viewport_height_pixels / input.viewport.h;
    const auto press = recipe_press_at(view, x, y, tolerance);
    if (!press) {
        const bool any_selected = std::ranges::any_of(
            std::span(view.recipe_areas),
            [](const auto& area) { return area.selected; });
        if (any_selected)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, x, y);
        return;
    }
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, x, y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, press->id);
    state.timing_drag.recipe_drag_active = begin_span_capture(input, state.timing_drag.recipe_capture);
    state.timing_drag.recipe_drag_moved = false;
    state.timing_drag.recipe_drag_edge = press->edge;
    state.timing_drag.recipe_drag_id = press->id;
    state.timing_drag.recipe_drag_anchor_y = y;
    state.timing_drag.recipe_drag_current_y = y;
}

static void commit_recipe_drag(const ComposePointerEventContext& input) {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT,
                         input.pointer_started_x, state.timing_drag.recipe_drag_current_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.timing_drag.recipe_drag_id, 0, false,
                         wormedit::timing_lane_kind_t::scroll, state.timing_drag.recipe_capture.chart,
                         state.timing_drag.recipe_capture.revision);
    if (input.action->compose_inputs.size() == 0) return;
    auto& queued = input.action->compose_inputs[input.action->compose_inputs.size() - 1];
    queued.span_edge = state.timing_drag.recipe_drag_edge;
    queued.span_anchor_y_pixels = state.timing_drag.recipe_drag_anchor_y;
}

static void continue_recipe_drag(const ComposePointerEventContext& input) {
    if (continue_span_capture(input, state.timing_drag.recipe_capture, state.timing_drag.recipe_drag_active,
                              state.timing_drag.recipe_drag_moved, state.timing_drag.recipe_drag_current_y))
        commit_recipe_drag(input);
    if (!state.timing_drag.recipe_drag_active) state.timing_drag.recipe_capture.cancel();
}

void update_recipe_drag(const ComposePointerEventContext& input) {
    if (state.timing_drag.recipe_drag_active) {
        continue_recipe_drag(input);
        return;
    }
    if (input.pointer_started_in_view && input.left_down && !input.left_released &&
        !recipe_gesture_blocked(input))
        begin_recipe_drag(input);
}
