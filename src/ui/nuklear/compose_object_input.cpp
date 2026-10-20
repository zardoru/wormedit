#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/tooltip.h"
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

#include <wormedit/core/vocabulary.h>

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

static auto pressed_bgm_mix_button(const ComposePointerEventContext& input)
    -> std::optional<std::pair<std::uint64_t, bool>> {
    for (const auto& lane : std::span(input.view->bgm_lanes, input.view->bgm_lane_count)) {
        for (const auto solo : {false, true}) {
            const auto rect = compose_bgm_mix_button_rect(lane, *input.view, input.viewport, solo);
            if (rect.w > 0 && input.left_clicked_screen_x >= rect.x &&
                input.left_clicked_screen_x < rect.x + rect.w && input.left_clicked_screen_y >= rect.y &&
                input.left_clicked_screen_y < rect.y + rect.h)
                return std::pair{lane.source_lane_id, solo};
        }
    }
    return std::nullopt;
}

auto process_bgm_mix_controls(const ComposePointerEventContext& input,
                              bool other_pointer_captured) -> bool {
    if (state.stem_lanes.bgm_mix_pointer_active) {
        if (!input.left_down || !input.window_focused || input.view->input_occluded)
            state.stem_lanes.bgm_mix_pointer_active = false;
        return true;
    }
    if (other_pointer_captured || !input.window_focused || input.view->input_occluded ||
        !input.view->scene_available || !input.pointer_started_in_view || !input.left_down) return false;
    const auto button = pressed_bgm_mix_button(input);
    if (!button) return false;
    state.stem_lanes.bgm_mix_pointer_active = true;
    if (input.view->bgm_mix_available)
        append_compose_input(input.action,
            button->second ? WORM_UI_COMPOSE_INPUT_BGM_SOLO : WORM_UI_COMPOSE_INPUT_BGM_MUTE,
            input.pointer_started_x, input.pointer_started_y, button->first);
    return true;
}

static auto stem_lane_resize_edge(const ComposePointerEventContext& input, double x, double y)
    -> const WormUiComposeBgmLaneView* {
    const auto& view = *input.view;
    const auto tolerance = 4.0 * view.viewport_width_pixels / input.viewport.w;
    if (y < 0 || y >= view.viewport_height_pixels) return nullptr;
    for (const auto& lane : std::span(view.bgm_lanes, view.bgm_lane_count)) {
        if (!lane.stem || lane.resize_edge_pixels > lane.bounds.right + 1e-6 ||
            lane.resize_edge_pixels < lane.bounds.left || x < lane.bounds.left ||
            x > lane.bounds.right + tolerance) continue;
        if (std::abs(x - lane.resize_edge_pixels) <= tolerance) return &lane;
    }
    return nullptr;
}
static auto update_stem_lane_resize(const ComposePointerEventContext& input) -> bool {
    if (!state.stem_lanes.stem_lane_resize) return false;
    const auto resize = *state.stem_lanes.stem_lane_resize;
    const auto [project, chart, lane] = resize.key;
    const auto& view = *input.view;
    if (!input.window_focused || view.input_occluded || view.project_id != project ||
        view.chart_id != chart || compose_action_has_cancel(input.action) ||
        (!input.left_down && !input.left_released)) {
        worm_ui_nuklear_cancel_stem_lane_resize(state);
        return true;
    }
    state.stem_lanes.stem_lane_resize_cursor_requested = true;
    state.stem_lanes.stem_lane_resize_hover_lane = lane;
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
            state.stem_lanes.stem_lane_resize_cursor_requested = true;
            state.stem_lanes.stem_lane_resize_hover_lane = lane->source_lane_id;
            return true;
        }
    }
    return false;
}
auto process_stem_lane_resize(const ComposePointerEventContext& input,
    const worm_ui_nuklear_pointer_click_t* double_clicks, std::size_t double_click_count,
    bool other_pointer_captured) -> bool {
    state.stem_lanes.stem_lane_resize_hover_lane = 0;
    if (update_stem_lane_resize(input)) return true;
    const auto& view = *input.view;
    if (other_pointer_captured || !input.window_focused || view.input_occluded ||
        !view.scene_available || !view.bmson_tools_available) return false;
    if (reset_stem_lane_from_double_click(input, double_clicks, double_click_count)) return true;
    const auto* hovered = stem_lane_resize_edge(input, input.pointer_x, input.pointer_y);
    if (hovered && input.pointer_inside) {
        state.stem_lanes.stem_lane_resize_cursor_requested = true;
        state.stem_lanes.stem_lane_resize_hover_lane = hovered->source_lane_id;
    }
    if (!input.pointer_started_in_view || !input.left_down) return false;
    const auto* lane = stem_lane_resize_edge(input, input.pointer_started_x, input.pointer_started_y);
    if (!lane) return false;
    state.stem_lanes.stem_lane_resize = worm_ui_nuklear_compose_state_t::stem_lane_resize_t{
        {view.project_id, view.chart_id, lane->source_lane_id}, input.pointer_started_x, lane->width_pixels};
    return update_stem_lane_resize(input);
}
static auto resource_pointer_can_start(const ComposePointerEventContext& input,
                                       bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed &&
           wormedit::ui::compose_notes_editable(compose_mode_of(view)) &&
           state.resource_picker.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
           view.scene_available && input.pointer_started_in_view && input.window_focused &&
           input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           !input.compose_note_double_click_in_view && !input.compose_pointer_captured &&
           !state.note_move.note_move_candidate_active && !state.area_selection.area_selection_candidate_active &&
           !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active;
}

static bool open_bga_stack_for_event(const WormUiComposeView& view, std::uint64_t event_id) {
    if (event_id == 0) return false;
    const auto events = std::span(view.bga_events, view.bga_event_count);
    const auto target = std::ranges::find(events, event_id,
        &WormUiComposeBgaEventView::event_id);
    if (target == events.end() || target->overlap_count <=
            WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) return false;
    state.bga_interaction.bga_stack_open = true;
    state.bga_interaction.bga_stack_release_pending = true;
    state.bga_interaction.bga_stack_project_id = view.project_id;
    state.bga_interaction.bga_stack_chart_id = view.chart_id;
    state.bga_interaction.bga_stack_revision = view.document_revision;
    state.bga_interaction.bga_stack_row = target->canvas_row;
    state.bga_interaction.bga_stack_kind = target->kind;
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
    const auto role = state.resource_picker.resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample
                          ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                      : state.resource_picker.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bgm
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
        (existing_bgm && state.resource_picker.resource_pointer_mode ==
                             worm_ui_nuklear_compose_resource_pointer_mode_t::place))
        return false;
    const auto replacing = state.resource_picker.resource_pointer_mode ==
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
                         state.resource_picker.armed_resource_id, role);
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
    return state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::restart
               ? WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART
               : WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE;
}

static auto bmson_razor_can_start(const ComposePointerEventContext& input,
                                  bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed && !input.compose_pointer_captured && view.scene_available &&
           input.window_focused && input.shortcut_event_count == 0 && input.left_down &&
           input.pointer_started_in_view &&
           state.bmson_input.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off &&
           bmson_razor_eligible(view) &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           selected_bmson_lane_at(input, input.pointer_started_x, input.pointer_started_y);
}

static void update_active_bmson_razor(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    auto& action = *input.action;
    const auto input_kind = bmson_razor_input_kind();
    if (!input.window_focused || !view.scene_available ||
        state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off ||
        input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        (!input.left_down && !input.left_released)) {
        state.bmson_input.bmson_razor_pointer_active = false;
    } else if (input.left_released) {
        if (input.pointer_inside && selected_bmson_lane_at(input, input.pointer_x, input.pointer_y) &&
            std::abs(input.pointer_y - state.bmson_input.bmson_razor_last_y_pixels) > 0.1)
            append_compose_input(&action, input_kind, input.pointer_x, input.pointer_y, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 view.selected_bmson_bgm_event_id);
        state.bmson_input.bmson_razor_pointer_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input) &&
               input.pointer_inside && selected_bmson_lane_at(input, input.pointer_x, input.pointer_y) &&
               std::abs(input.pointer_y - state.bmson_input.bmson_razor_last_y_pixels) > 0.1) {
        append_compose_input(&action, input_kind, input.pointer_x, input.pointer_y, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                             view.selected_bmson_bgm_event_id);
        state.bmson_input.bmson_razor_last_y_pixels = input.pointer_y;
    }
}

auto process_bmson_razor_pointer(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    if (state.bmson_input.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off &&
        !bmson_razor_eligible(*input.view))
        state.bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
    if (state.bmson_input.bmson_razor_pointer_active) {
        update_active_bmson_razor(input);
        return true;
    }
    if (!bmson_razor_can_start(input, paint_pointer_consumed)) return paint_pointer_consumed;
    append_compose_input(input.action, bmson_razor_input_kind(), input.pointer_started_x,
                         input.pointer_started_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                         input.view->selected_bmson_bgm_event_id, 0, true);
    state.bmson_input.bmson_razor_pointer_active = !state.input_queue.action_overflowed;
    state.bmson_input.bmson_razor_last_y_pixels = input.pointer_started_y;
    return true;
}

static void append_bmson_move_input(const ComposePointerEventContext& input,
                                    WormUiComposeInputKind kind, double x, double y,
                                    uint64_t lane_id) {
    append_compose_input(input.action, kind, x, y, lane_id, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                         state.bmson_input.bmson_marker_move_event_id);
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
                                 state.bmson_input.bmson_marker_move_event_id);
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
    const auto dx = input.context->input.mouse.pos.x - state.bmson_input.bmson_marker_move_anchor_screen_x;
    const auto dy = input.context->input.mouse.pos.y - state.bmson_input.bmson_marker_move_anchor_screen_y;
    const auto moved_far_enough = std::hypot(dx, dy) >= 4.0f;
    const auto lane_id = bmson_move_lane_at(input, input.pointer_x, input.pointer_y);
    if (input.left_released && moved_far_enough && lane_id == 0 && field_lane_under_pointer(input) != 0) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE, input.pointer_x, input.pointer_y,
                             field_lane_under_pointer(input), false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, state.bmson_input.bmson_marker_move_event_id);
        reset_bmson_marker_move_capture();
        return;
    }
    if (input.left_released) {
        if (moved_far_enough && input.pointer_inside && lane_id != 0) {
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN,
                                    state.bmson_input.bmson_marker_move_anchor_x,
                                    state.bmson_input.bmson_marker_move_anchor_y, 0);
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE,
                                    input.pointer_x, input.pointer_y, lane_id);
        } else {
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
                                    state.bmson_input.bmson_marker_move_anchor_x,
                                    state.bmson_input.bmson_marker_move_anchor_y, 0);
        }
        reset_bmson_marker_move_capture();
    } else if (input.left_down && moved_far_enough) {
        append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN,
                                state.bmson_input.bmson_marker_move_anchor_x,
                                state.bmson_input.bmson_marker_move_anchor_y, 0);
        state.bmson_input.bmson_marker_move_candidate_active = false;
        state.bmson_input.bmson_marker_move_active = !state.input_queue.action_overflowed;
        if (state.bmson_input.bmson_marker_move_active && input.pointer_inside && lane_id != 0)
            append_bmson_move_input(input, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE,
                                    input.pointer_x, input.pointer_y, lane_id);
    }
}

static auto selected_bgm_drag_reference(const ComposePointerEventContext& input) -> uint64_t {
    const auto id = compose_bgm_event_at(input.view, input.pointer_started_x, input.pointer_started_y);
    for (const auto& event : std::span(input.view->bgm_events, input.view->bgm_event_count))
        if (event.event_id == id && event.selected) return id;
    return 0;
}

static auto bmson_marker_move_can_start(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed && !input.compose_pointer_captured && view.scene_available &&
           input.pointer_started_in_view && input.left_down && input.window_focused &&
           input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off &&
           state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
           !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active &&
           selected_bgm_drag_reference(input) != 0;
}

static void begin_bmson_marker_move_candidate(const ComposePointerEventContext& input) {
    state.bmson_input.bmson_marker_move_candidate_active = true;
    state.bmson_input.bmson_marker_move_event_id = selected_bgm_drag_reference(input);
    state.bmson_input.bmson_marker_move_anchor_x = input.pointer_started_x;
    state.bmson_input.bmson_marker_move_anchor_y = input.pointer_started_y;
    state.bmson_input.bmson_marker_move_anchor_screen_x = input.left_clicked_screen_x;
    state.bmson_input.bmson_marker_move_anchor_screen_y = input.left_clicked_screen_y;
}

auto process_bmson_marker_move_pointer(const ComposePointerEventContext& input,
                                              bool paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    if (state.bmson_input.bmson_marker_move_active) {
        update_active_bmson_marker_move(input);
        return true;
    }
    if (state.bmson_input.bmson_marker_move_candidate_active) {
        update_bmson_marker_move_candidate(input);
        return true;
    }
    if (!bmson_marker_move_can_start(input, paint_pointer_consumed)) return paint_pointer_consumed;
    begin_bmson_marker_move_candidate(input);
    return true;
}

void append_layer_brush_begin(const ComposePointerEventContext& input) {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
                         input.paint_anchor_x, input.paint_anchor_y);
    if (!state.input_queue.action_overflowed)
        input.action->compose_inputs[input.action->compose_input_count - 1].paint_intent =
            (input.pointer_modifiers & SDL_KMOD_ALT) != 0 ? 2 : 1;
}

auto process_semantic_layer_paint_pointer(const ComposePointerEventContext& input,
                                                 bool& paint_pointer_consumed) -> bool {
    if (compose_area_pointer_consumed()) return true;
    const auto& view = *input.view;
    auto* action = input.action;
    if (!state.input_queue.action_overflowed && state.pointer_capture.semantic_layer_paint_pointer_active) {
        if (!input.window_focused || !view.scene_available ||
            !view.semantic_layer_paint_mode_active || input.shortcut_event_count != 0 ||
            (input.pointer_modifiers & input.any_pointer_modifier & ~SDL_KMOD_ALT) != 0 ||
            (!input.left_down && !input.left_released)) {
            append_compose_cancel_once(action);
            state.pointer_capture.semantic_layer_paint_pointer_active = false;
        } else if (input.left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                 input.paint_pointer_x, input.paint_pointer_y);
            state.pointer_capture.semantic_layer_paint_pointer_active = false;
        } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
                                 input.paint_pointer_x, input.paint_pointer_y);
        }
        return true;
    }
    if (!state.input_queue.action_overflowed && view.scene_available &&
        view.semantic_layer_paint_mode_active && input.pointer_started_in_view &&
        input.window_focused && input.shortcut_event_count == 0 &&
        (input.pointer_modifiers & input.any_pointer_modifier & ~SDL_KMOD_ALT) == 0 &&
        !state.pointer_capture.pointer_hold_active && !state.range_selection.lane_hold_active && !state.area_selection.area_selection_active &&
        !state.area_selection.area_selection_candidate_active) {
        paint_pointer_consumed = true;
        if (compose_point_hits_note(&view, input.paint_anchor_x, input.paint_anchor_y) ||
            compose_bgm_event_at(&view, input.paint_anchor_x, input.paint_anchor_y) != 0) {
            append_layer_brush_begin(input);
            state.pointer_capture.semantic_layer_paint_pointer_active = !state.input_queue.action_overflowed && input.left_down;
            if (!state.input_queue.action_overflowed && !input.left_down)
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                     input.paint_pointer_x, input.paint_pointer_y);
        }
        return true;
    }
    return false;
}

// The BGM source lane under a Draw press. On a BMSON chart only packed (non-stem) columns
// resolve here; stem lanes keep their own point capture and are never drawn as strokes.
static auto bgm_draw_lane_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiComposeBgmLaneView* {
    for (const auto& lane : std::span(view.bgm_lanes, view.bgm_lane_count))
        if (!(view.bmson_tools_available && lane.stem) && x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom) return &lane;
    return nullptr;
}

// A sound is the current Draw source and, on BMSON, it is not owned by a stem lane.
static auto bgm_draw_source_ready(const WormUiComposeView& view) -> bool {
    return state.pointer_capture.draw_source_kind == 1 && state.pointer_capture.draw_source_id != 0 &&
           !view.draw_source_stem_lane_id && !view.draw_source_is_stem;
}

static void emit_draw_stroke(const ComposePointerEventContext& input,
    WormUiComposeInputKind kind, double x, double y, bool erase = false) {
    auto* action = input.action;
    append_compose_input(action, kind, x, y);
    if (!action->compose_input_count || state.input_queue.action_overflowed) return;
    auto& output = action->compose_inputs[action->compose_input_count - 1];
    output.paint_intent = erase ? 2 : 1;
    output.resource_id = erase ? 0 : state.pointer_capture.draw_source_id;
    if (kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
        const auto* pressed = erase
            ? bgm_draw_lane_at(*input.view, state.pointer_capture.draw_right_anchor_x, state.pointer_capture.draw_right_anchor_y)
            : bgm_draw_lane_at(*input.view, input.pointer_started_x, input.pointer_started_y);
        state.pointer_capture.draw_stroke_bgm_lane = pressed ? pressed->source_lane_id : 0;
    }
    if (state.pointer_capture.draw_stroke_bga) {
        output.resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
        output.bga_lane_kind = compose_bga_lane_kind_at(*input.view, x, y).value_or(255);
        output.bga_lane_kind_valid = true;
    } else if (state.pointer_capture.draw_stroke_bgm_lane != 0) {
        output.resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
        output.lane_id = state.pointer_capture.draw_stroke_bgm_lane;
    } else if (!erase && state.pointer_capture.draw_source_kind != 1) {
        output.resource_id = 0;
    }
    if (kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) state.pointer_capture.draw_stroke_bgm_lane = 0;
}

void append_note_paint_begin(const ComposePointerEventContext& input) {
    state.pointer_capture.draw_stroke_bga = state.pointer_capture.draw_source_kind == 2;
    emit_draw_stroke(input, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
        state.pointer_capture.draw_stroke_bga ? input.pointer_started_x : input.paint_anchor_x,
        input.paint_anchor_y);
}

bool process_draw_right_pointer(const ComposePointerEventContext& input) {
    auto& mouse = input.context->input.mouse;
    const auto& button = mouse.buttons[NK_BUTTON_RIGHT];
    if (!input.view->note_paint_mode_active || !input.window_focused ||
        input.view->input_occluded || input.view->timing_focus) {
        if (state.pointer_capture.draw_erase_pointer_active) append_compose_cancel_once(input.action);
        worm_ui_nuklear_reset_draw_right_capture(state);
        return false;
    }
    const auto started_inside = state.pointer_capture.draw_right_screen_x >= input.viewport.x &&
        state.pointer_capture.draw_right_screen_x < input.editor_input_right && state.pointer_capture.draw_right_screen_y >= input.viewport.y &&
        state.pointer_capture.draw_right_screen_y <= input.viewport.y + input.viewport.h;
    if (state.pointer_capture.draw_right_press_pending && !state.pointer_capture.draw_right_candidate && started_inside) {
        state.pointer_capture.draw_right_candidate = true;
        state.pointer_capture.draw_right_anchor_x = (state.pointer_capture.draw_right_screen_x - input.viewport.x) *
            input.view->viewport_width_pixels / input.viewport.w;
        state.pointer_capture.draw_right_anchor_y = (state.pointer_capture.draw_right_screen_y - input.viewport.y) *
            input.view->viewport_height_pixels / input.viewport.h;
    }
    state.pointer_capture.draw_right_press_pending = false;
    if (!state.pointer_capture.draw_right_candidate) { state.pointer_capture.draw_right_release_pending = false; return false; }
    const auto moved = state.pointer_capture.draw_right_dragged ||
        std::hypot(state.pointer_capture.draw_right_current_screen_x - state.pointer_capture.draw_right_screen_x,
            state.pointer_capture.draw_right_current_screen_y - state.pointer_capture.draw_right_screen_y) >= 3.0;
    if (moved && !state.pointer_capture.draw_erase_pointer_active) {
        state.pointer_capture.draw_stroke_bga = compose_bga_lane_kind_at(*input.view,
            state.pointer_capture.draw_right_anchor_x, state.pointer_capture.draw_right_anchor_y).has_value();
        emit_draw_stroke(input, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
            state.pointer_capture.draw_right_anchor_x, state.pointer_capture.draw_right_anchor_y, true);
        state.pointer_capture.draw_erase_pointer_active = state.pointer_capture.draw_right_dragged = true;
    }
    if (state.pointer_capture.draw_erase_pointer_active) {
        emit_draw_stroke(input, button.down ? WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE :
            WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, input.pointer_x, input.pointer_y, true);
    }
    const auto consumed = state.pointer_capture.draw_erase_pointer_active;
    if (!button.down) {
        worm_ui_nuklear_reset_draw_right_capture(state);
    }
    return consumed;
}

// Hold and Roll need an explicit end, so their press goes to the click-click hold gesture and
// never starts a paint stroke (the core rejects the stroke and the UI would keep updating it).
static auto armed_kind_uses_hold_gesture(const WormUiComposeView& view) -> bool {
    return view.armed_note_kind == static_cast<uint32_t>(wormedit::note_kind_t::hold) ||
           view.armed_note_kind == static_cast<uint32_t>(wormedit::note_kind_t::roll);
}

static void update_draw_left_pointer(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    auto* action = input.action;
    if (!input.window_focused || !view.scene_available || !view.note_paint_mode_active ||
        input.shortcut_event_count != 0 ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        (!input.left_down && !input.left_released)) {
        append_compose_cancel_once(action);
        state.pointer_capture.note_paint_pointer_active = false;
    } else if (input.left_released) {
        emit_draw_stroke(input, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
            state.pointer_capture.draw_stroke_bga ? input.pointer_x : input.paint_pointer_x, input.paint_pointer_y);
        state.pointer_capture.note_paint_pointer_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        emit_draw_stroke(input, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE,
            state.pointer_capture.draw_stroke_bga ? input.pointer_x : input.paint_pointer_x, input.paint_pointer_y);
    }
}

static bool draw_left_can_start(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto bga = state.pointer_capture.draw_source_kind == 2;
    const auto bgm = bgm_draw_source_ready(view) &&
        bgm_draw_lane_at(view, input.pointer_started_x, input.pointer_started_y) != nullptr;
    const auto target = bga ? compose_bga_lane_kind_at(view, input.pointer_started_x,
        input.pointer_started_y).has_value() : (input.pointer_started_in_note_field || bgm);
    // A sample or BGA stroke starts only from a press or a release this frame. The pointer
    // started position is stale between clicks, so hovering must not emit empty strokes.
    if ((bga || bgm) && !input.left_down && !input.left_released) return false;
    return !state.input_queue.action_overflowed && view.scene_available && view.note_paint_mode_active &&
        (bga || bgm || !armed_kind_uses_hold_gesture(view)) && target && input.window_focused &&
        input.shortcut_event_count == 0 &&
        (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
        !state.pointer_capture.pointer_hold_active && !state.range_selection.lane_hold_active && !state.area_selection.area_selection_active &&
        !state.area_selection.area_selection_candidate_active;
}

static auto bmson_lane_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiComposeBgmLaneView* {
    for (const auto& lane : std::span(view.bgm_lanes, view.bgm_lane_count))
        if (x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom) return &lane;
    return nullptr;
}

static void finish_bmson_point_capture(const ComposePointerEventContext& input) {
    const auto capture = *state.bmson_input.bmson_point_capture;
    state.bmson_input.bmson_point_capture.reset();
    const auto* lane = bmson_lane_at(*input.view, input.pointer_x, input.pointer_y);
    if (capture.dock && (!lane || !lane->stem || lane->source_lane_id != capture.lane)) return;
    const auto kind = capture.packed ? WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW :
        capture.restart ? WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART :
                          WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE;
    append_compose_input(input.action, kind, capture.dock ? input.pointer_x : capture.x,
        capture.dock ? input.pointer_y : capture.y, capture.lane);
    if (input.action->compose_input_count) {
        auto& output = input.action->compose_inputs[input.action->compose_input_count - 1];
        output.resource_id = capture.resource;
        output.resource_role = capture.dock ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : 0;
    }
}

static bool bmson_point_capture_current(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto& capture = *state.bmson_input.bmson_point_capture;
    return input.window_focused && view.scene_available && !view.input_occluded && !view.timing_focus &&
        view.note_paint_mode_active && capture.project == view.project_id &&
        capture.chart == view.chart_id && capture.revision == view.document_revision;
}

static bool bmson_draw_can_start(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    return view.bmson_tools_available && view.note_paint_mode_active && !view.timing_focus &&
        input.window_focused && state.pointer_capture.draw_source_kind != 2 && input.pointer_inside &&
        input.pointer_started_in_view && !input.shortcut_event_count && !input.compose_pointer_captured &&
        state.bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::off &&
        !compose_bgm_event_at(input.view, input.pointer_started_x, input.pointer_started_y) &&
        !(input.pointer_modifiers & input.any_pointer_modifier & ~SDL_KMOD_SHIFT);
}

static bool packed_draw_source_available(const WormUiComposeView& view) {
    return bgm_draw_source_ready(view);
}

static void show_packed_draw_source_tip(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    const auto* lane = bmson_lane_at(view, input.pointer_x, input.pointer_y);
    if (!view.note_paint_mode_active || view.timing_focus || !view.bmson_tools_available ||
        !input.pointer_inside || !lane || lane->stem || packed_draw_source_available(view)) return;
    const auto name = view.draw_source_stem_lane_name ? view.draw_source_stem_lane_name : "";
    wormedit::ui::nuklear::tooltip::text(input.context, (view.draw_source_stem_lane_id || view.draw_source_is_stem) ?
        std::string("This source belongs to stem lane ") + name + "." :
        std::string("Pick a keysound source to draw in this packed column."));
}

static bool process_bmson_draw_pointer(const ComposePointerEventContext& input) {
    const auto& view = *input.view;
    show_packed_draw_source_tip(input);
    if (state.bmson_input.bmson_point_capture) {
        if (!bmson_point_capture_current(input)) state.bmson_input.bmson_point_capture.reset();
        else if (input.left_released) finish_bmson_point_capture(input);
        else if (!input.left_down) state.bmson_input.bmson_point_capture.reset();
        return true;
    }
    if (!bmson_draw_can_start(input)) return false;
    const auto* lane = bmson_lane_at(view, input.pointer_started_x, input.pointer_started_y);
    if (!lane) return false;
    // Packed columns draw as BGM-style strokes; only the missing-source case stops here.
    if (!lane->stem) return !packed_draw_source_available(view);
    state.bmson_input.bmson_point_capture = worm_ui_nuklear_compose_state_t::bmson_point_capture_t{
        view.project_id, view.chart_id, view.document_revision, lane->source_lane_id,
        state.pointer_capture.draw_source_id, input.pointer_started_x, input.pointer_started_y,
        (input.pointer_modifiers & SDL_KMOD_SHIFT) != 0, !lane->stem, false};
    if (!input.left_down) finish_bmson_point_capture(input);
    return true;
}

static auto bgm_draw_press_without_source(const ComposePointerEventContext& input) -> bool {
    const auto& view = *input.view;
    return view.note_paint_mode_active && !bgm_draw_source_ready(view) && input.pointer_started_in_view &&
        (input.left_down || input.left_released) &&
        bgm_draw_lane_at(view, input.pointer_started_x, input.pointer_started_y) != nullptr;
}

void process_note_paint_pointer(const ComposePointerEventContext& input,
                                       bool& paint_pointer_consumed) {
    if (input.view->note_paint_mode_active && input.view->draw_source_missing) {
        if (state.pointer_capture.note_paint_pointer_active || state.bmson_input.bmson_point_capture)
            append_compose_cancel_once(input.action);
        state.pointer_capture.note_paint_pointer_active = false;
        state.bmson_input.bmson_point_capture.reset();
        paint_pointer_consumed = true;
        return;
    }
    if (state.pointer_capture.draw_left_cancelled_until_release) {
        if (!input.left_down) state.pointer_capture.draw_left_cancelled_until_release = false;
        paint_pointer_consumed = true;
        return;
    }
    if (process_bmson_draw_pointer(input)) { paint_pointer_consumed = true; return; }
    if (input.view->paste_active) { paint_pointer_consumed = true; return; }
    if (compose_area_pointer_consumed()) return;
    const auto& view = *input.view;
    if (state.pointer_capture.draw_source_kind == 2 && view.note_paint_mode_active &&
        input.pointer_started_in_note_field && !state.pointer_capture.note_paint_pointer_active) {
        paint_pointer_consumed = true;
        return;
    }

    if (!state.input_queue.action_overflowed && state.pointer_capture.note_paint_pointer_active) {
        update_draw_left_pointer(input);
        return;
    }
    // Without a sound there is nothing to draw in a BGM lane; the press must not fall through
    // to a marquee or a selection.
    if (bgm_draw_press_without_source(input)) {
        paint_pointer_consumed = true;
        return;
    }
    if (draw_left_can_start(input)) {
        append_note_paint_begin(input);
        paint_pointer_consumed = true;
        state.pointer_capture.note_paint_pointer_active = !state.input_queue.action_overflowed && input.left_down;
        if (!state.input_queue.action_overflowed && !input.left_down)
            emit_draw_stroke(input, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
                state.pointer_capture.draw_stroke_bga ? input.pointer_x : input.paint_pointer_x, input.paint_pointer_y);
    }
}

// The pointer is over the BGM area, to the right of the playable field.
static auto note_move_over_bgm_pane(const ComposePointerEventContext& input) -> bool {
    return (input.view->bgm_lane_count != 0 || input.view->bms_measure_labels ||
            input.view->bmson_tools_available) && input.pointer_inside &&
           input.pointer_x >= input.note_field_right;
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

static void append_note_move_input(const ComposePointerEventContext& input,
                                    WormUiComposeInputKind kind) {
    const auto count = input.action->compose_input_count;
    append_compose_input(input.action, kind, input.pointer_x, input.pointer_y,
                         bgm_lane_under_pointer(input));
    if (input.action->compose_input_count > count)
        input.action->compose_inputs[count].note_move_to_bgm = note_move_over_bgm_pane(input);
}

static auto update_note_move_candidate(const ComposePointerEventContext& input,
                                      bool paint_pointer_consumed) -> bool {
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !state.note_move.note_move_candidate_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        state.note_move.note_move_candidate_active = false;
    } else if (input.left_released) {
        if (state.note_move.note_move_select_on_release)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 state.note_move.note_move_anchor_x, state.note_move.note_move_anchor_y);
        state.note_move.note_move_candidate_active = false;
    } else if (input.left_down) {
        constexpr float drag_threshold_pixels = 4.0f;
        const auto dx = input.context->input.mouse.pos.x - state.note_move.note_move_anchor_screen_x;
        const auto dy = input.context->input.mouse.pos.y - state.note_move.note_move_anchor_screen_y;
        if (std::hypot(dx, dy) >= drag_threshold_pixels) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
                                 state.note_move.note_move_anchor_x, state.note_move.note_move_anchor_y);
            state.note_move.note_move_candidate_active = false;
            state.note_move.note_move_active = !state.input_queue.action_overflowed;
            if (state.note_move.note_move_active)
                append_note_move_input(input, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE);
        }
    }
    return true;
}

static auto update_note_move_active(const ComposePointerEventContext& input,
                                    bool paint_pointer_consumed) -> bool {
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !state.note_move.note_move_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        state.note_move.note_move_active = false;
    } else if (input.left_released) {
        append_note_move_input(input, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE);
        state.note_move.note_move_active = false;
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_note_move_input(input, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE);
    }
    return true;
}

static void reset_bar_line_move_capture() {
    state.bar_line_move = {};
}

static void append_bar_line_select(const ComposePointerEventContext& input,
                                   std::uint64_t measure_id) {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT,
                         input.pointer_x, input.pointer_y, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, measure_id);
}

static auto update_bar_line_move_capture(const ComposePointerEventContext& input) -> bool {
    auto& move = state.bar_line_move;
    if (!move.capture.active) return false;
    const auto valid = input.window_focused && input.view->measure_lane.visible &&
        !state.input_queue.action_overflowed && move.capture.matches(*input.view);
    const auto continuation = move.capture.advance(
        {.down = input.left_down,
         .released = input.left_released,
         .x = input.context->input.mouse.pos.x,
         .y = input.context->input.mouse.pos.y},
        valid, compose_drag_release_policy_t::latched_planar, move.active);
    if (continuation.phase == compose_drag_phase_t::cancel) {
        if (state.bar_line_move.active)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        reset_bar_line_move_capture();
        return true;
    }
    state.bar_line_move.current_y = input.pointer_y;
    if (input.left_released) {
        if (state.bar_line_move.active)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE,
                                 input.pointer_x, input.pointer_y, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                                 state.bar_line_move.measure_id);
        else if (state.bar_line_move.select_on_release)
            append_bar_line_select(input, state.bar_line_move.measure_id);
        reset_bar_line_move_capture();
        return true;
    }
    if (!state.bar_line_move.active && continuation.moved) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_BEGIN,
                             state.bar_line_move.anchor_x, state.bar_line_move.anchor_y, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                             state.bar_line_move.measure_id);
        state.bar_line_move.active = !state.input_queue.action_overflowed;
    }
    if (state.bar_line_move.active && nk_input_is_mouse_moved(&input.context->input))
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_UPDATE,
                             input.pointer_x, input.pointer_y, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                             state.bar_line_move.measure_id);
    return true;
}

static auto begin_bar_line_move_capture(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !view.measure_lane.visible || !input.window_focused || !input.left_down ||
        !input.pointer_started_in_view || state.bar_line_move.capture.active ||
        (input.pointer_modifiers & input.any_pointer_modifier) != 0 ||
        compose_selection_mode(input.pointer_modifiers) != WORM_UI_COMPOSE_SELECTION_REPLACE)
        return false;
    const auto* line = wormedit::ui::compose_bar_line_at(
        view, input.pointer_started_x, input.pointer_started_y);
    if (!line) return false;
    if (!state.bar_line_move.capture.begin(
            view, input.left_clicked_screen_x, input.left_clicked_screen_y, false))
        return false;
    state.bar_line_move.select_on_release = view.measure_lane.is_selected(line->measure_id);
    state.bar_line_move.measure_id = line->measure_id.value;
    state.bar_line_move.anchor_x = input.pointer_started_x;
    state.bar_line_move.anchor_y = input.pointer_started_y;
    state.bar_line_move.current_y = input.pointer_started_y;
    if (!state.bar_line_move.select_on_release)
        append_bar_line_select(input, state.bar_line_move.measure_id);
    return true;
}

static bool area_selection_drag_threshold_reached(const ComposePointerEventContext& input) {
    const auto dx = input.context->input.mouse.pos.x - state.area_selection.area_selection_anchor_screen_x;
    const auto dy = input.context->input.mouse.pos.y - state.area_selection.area_selection_anchor_screen_y;
    return std::hypot(dx, dy) >= 4.0f;
}

static auto request_pointer_hold(const ComposePointerEventContext& input,
                                 double x, double y, bool without_alt) -> bool {
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN,
                         x, y);
    worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *input.action);
    state.pointer_capture.pointer_hold_begin_pending = !state.input_queue.action_overflowed;
    state.pointer_capture.pointer_hold_without_alt = without_alt && state.pointer_capture.pointer_hold_begin_pending;
    return state.pointer_capture.pointer_hold_begin_pending;
}

static void emit_pointer_hold_motion(const ComposePointerEventContext& input, bool released) {
    append_compose_input(input.action,
                         released ? WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE
                                  : WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
                         input.pointer_x, input.pointer_y);
    if (released) {
        state.pointer_capture.pointer_hold_begin_pending = false;
        state.pointer_capture.pointer_hold_active = false;
        state.pointer_capture.pointer_hold_without_alt = false;
    }
}

// With Hold/Roll armed, a drag starts the same pointer lifecycle as Alt/Option.
static void begin_pointer_hold_drag(const ComposePointerEventContext& input, bool released) {
    state.area_selection.area_selection_candidate_active = false;
    if (request_pointer_hold(input, state.area_selection.area_selection_anchor_x,
                             state.area_selection.area_selection_anchor_y, true))
        emit_pointer_hold_motion(input, released);
}

static void begin_area_selection_drag(const ComposePointerEventContext& input, bool released) {
    if (input.view->note_paint_mode_active && armed_kind_uses_hold_gesture(*input.view)) {
        begin_pointer_hold_drag(input, released);
        return;
    }
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                         state.area_selection.area_selection_anchor_x, state.area_selection.area_selection_anchor_y,
                         0, false, state.area_selection.area_selection_mode);
    state.area_selection.area_selection_candidate_active = false;
    state.area_selection.area_selection_active = !state.input_queue.action_overflowed;
    if (!state.area_selection.area_selection_active) return;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                         input.pointer_x, input.pointer_y);
    if (released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.area_selection.area_selection_active = false;
    }
}

static void release_compose_selection_click(const ComposePointerEventContext& input) {
    if (state.area_selection.area_selection_anchor_x >= input.note_field_left &&
        state.area_selection.area_selection_anchor_x < input.note_field_right) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             state.area_selection.area_selection_anchor_x, state.area_selection.area_selection_anchor_y,
                             0, false, state.area_selection.area_selection_mode);
        worm_ui_nuklear_tag_last_compose_input_sound(state, *input.view, *input.action);
    } else if (input.view->bms_measure_labels &&
               state.area_selection.area_selection_anchor_x < compose_measure_rail_width) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
                             state.area_selection.area_selection_anchor_x, state.area_selection.area_selection_anchor_y);
    } else {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BMSON_EMPTY_SELECT,
                             state.area_selection.area_selection_anchor_x, state.area_selection.area_selection_anchor_y);
    }
}

static auto update_area_selection_candidate(const ComposePointerEventContext& input,
                                            bool paint_pointer_consumed) -> bool {
    if (state.input_queue.action_overflowed || paint_pointer_consumed ||
        !state.area_selection.area_selection_candidate_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        if (input.window_focused)
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        state.area_selection.area_selection_candidate_active = false;
    } else if (input.left_released) {
        if (area_selection_drag_threshold_reached(input)) {
            begin_area_selection_drag(input, true);
        } else {
            release_compose_selection_click(input);
            state.area_selection.area_selection_candidate_active = false;
        }
    } else if (input.left_down && area_selection_drag_threshold_reached(input)) {
        begin_area_selection_drag(input, false);
    }
    return true;
}

static auto update_area_selection_active(const ComposePointerEventContext& input,
                                         bool paint_pointer_consumed) -> bool {
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !state.area_selection.area_selection_active)
        return false;
    if (!input.window_focused || (!input.left_down && !input.left_released)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        state.area_selection.area_selection_active = false;
    } else if (input.left_released) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
                             input.pointer_x, input.pointer_y);
        state.area_selection.area_selection_active = false;
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
    if (update_bar_line_move_capture(input)) return;
    if (begin_bar_line_move_capture(input, paint_pointer_consumed)) return;
    if (update_note_move_candidate(input, paint_pointer_consumed)) return;
    if (update_note_move_active(input, paint_pointer_consumed)) return;
    if (update_area_selection_candidate(input, paint_pointer_consumed)) return;
    update_area_selection_active(input, paint_pointer_consumed);
}

static auto is_compose_rail_click(const ComposePointerEventContext& input, bool paint_pointer_consumed) -> bool {
    return !state.input_queue.action_overflowed && !paint_pointer_consumed && input.view->scene_available &&
        input.pointer_clicked && input.pointer_inside && input.pointer_x < compose_measure_rail_width &&
        input.window_focused && !state.pointer_capture.pointer_hold_active &&
        !state.range_selection.lane_hold_active && !state.note_move.note_move_active &&
        !state.bar_line_move.active && !state.bar_line_move.capture.active &&
        !state.area_selection.area_selection_active && !state.area_selection.area_selection_candidate_active &&
        !state.timing_drag.timing_stage_drag_active;
}

// The measure lane selects the bar line under the pointer, or in Draw mode places one.
static auto handle_compose_measure_lane_click(const ComposePointerEventContext& input) -> bool {
    const auto& view = *input.view;
    if (!wormedit::ui::is_compose_measure_lane_press(view, input.pointer_started_x, input.pointer_started_y))
        return false;
    if (const auto* line = wormedit::ui::compose_bar_line_at(view, input.pointer_started_x, input.pointer_started_y))
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT, input.pointer_x,
                             input.pointer_y, 0, false, compose_selection_mode(input.pointer_modifiers),
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, line->measure_id.value);
    else
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BAR_LINE_PLACE, input.pointer_x,
                             input.pointer_y);
    return true;
}

auto handle_compose_measure_click(const ComposePointerEventContext& input,
                                         bool paint_pointer_consumed) -> bool {
    if (!is_compose_rail_click(input, paint_pointer_consumed)) return false;
    if (handle_compose_measure_lane_click(input)) return true;
    if (!input.view->bms_measure_labels) return false;
    append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
                         input.pointer_x, input.pointer_y);
    return true;
}

auto handle_compose_alt_pointer(const ComposePointerEventContext& input,
                                       bool paint_pointer_consumed) -> bool {
    auto* action = input.action;
    if (!state.input_queue.action_overflowed && !paint_pointer_consumed && input.view->scene_available &&
        input.pointer_started_in_note_field && input.alt_down &&
        !state.pointer_capture.pointer_hold_active && !state.pointer_capture.pointer_hold_begin_pending &&
        input.window_focused && !input.compose_note_double_click_in_view &&
        wormedit::ui::compose_notes_editable(compose_mode_of(*input.view))) {
        if (request_pointer_hold(input, input.pointer_started_x, input.pointer_started_y, false) &&
            input.left_released)
            emit_pointer_hold_motion(input, true);
        return true;
    }
    if (state.input_queue.action_overflowed || paint_pointer_consumed || !state.pointer_capture.pointer_hold_active)
        return false;
    if (!input.window_focused || (!input.alt_down && !state.pointer_capture.pointer_hold_without_alt)) {
        append_compose_cancel_once(action);
        state.pointer_capture.pointer_hold_active = false;
        state.pointer_capture.pointer_hold_without_alt = false;
    } else if (input.left_released) {
        emit_pointer_hold_motion(input, true);
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        emit_pointer_hold_motion(input, false);
    }
    return true;
}
