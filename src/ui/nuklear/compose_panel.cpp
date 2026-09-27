#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/bms_branch_panel.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/timing_panel.h"

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

static auto& state = worm_ui_nuklear_compose_state();

void worm_ui_nuklear_reset_compose_panel() {
    worm_ui_nuklear_reset_compose_markers();
    worm_ui_nuklear_reset_compose_state();
}

static void append_compose_input(
    WormUiWorkspaceAction* action, WormUiComposeInputKind kind, double x_pixels = 0.0,
    double y_pixels = 0.0, uint64_t lane_id = 0, bool successive = false,
    WormUiComposeSelectionMode selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
    WormUiComposeNavigationAction navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
    uint64_t object_id = 0, uint8_t resource_role = 0) {
    if (!action) {
        return;
    }
    if (action->compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        action->compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY - 1] = {
            .kind = WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
            .lane_id = 0,
            .object_id = 0,
            .resource_role = 0,
            .successive = false,
            .selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
            .navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
            .x_pixels = 0.0,
            .y_pixels = 0.0,
        };
        action->compose_input_count = WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY;
        state.cancel_pending = false;
        state.pointer_hold_active = false;
        state.note_paint_pointer_active = false;
        state.semantic_layer_paint_pointer_active = false;
        state.lane_hold_active = false;
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
        state.timing_stage_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.navigation_key_active = false;
        state.navigation_keycode = 0;
        state.pointer_navigation_active = false;
        state.keyboard_focus = false;
        state.lane_hold_keycode = 0;
        state.lane_hold_lane_id = 0;
        state.action_overflowed = true;
        return;
    }
    action->compose_inputs[action->compose_input_count++] = {
        .kind = kind,
        .lane_id = lane_id,
        .object_id = object_id,
        .resource_role = resource_role,
        .successive = successive,
        .selection_mode = selection_mode,
        .navigation_action = navigation_action,
        .x_pixels = x_pixels,
        .y_pixels = y_pixels,
    };
}

static void
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
    if (action && (state.navigation_key_active || state.pointer_navigation_active)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
    }
    state.navigation_key_active = false;
    state.navigation_keycode = 0;
    state.pointer_navigation_active = false;
}

static bool compose_navigation_action_for_key(uint32_t keycode, [[maybe_unused]] uint16_t modifiers,
                                              WormUiComposeNavigationAction* action) {
    if (!action) {
        return false;
    }
    if (keycode == static_cast<uint32_t>(SDLK_UP)) {
#if defined(__APPLE__)
        if ((modifiers & SDL_KMOD_ALT) != 0) {
            *action = WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD;
            return true;
        }
#endif
        *action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD;
        return true;
    }
    if (keycode == static_cast<uint32_t>(SDLK_DOWN)) {
#if defined(__APPLE__)
        if ((modifiers & SDL_KMOD_ALT) != 0) {
            *action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
            return true;
        }
#endif
        *action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD;
        return true;
    }
    if (keycode == static_cast<uint32_t>(SDLK_PAGEUP)) {
        *action = WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD;
        return true;
    }
    if (keycode == static_cast<uint32_t>(SDLK_PAGEDOWN)) {
        *action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
        return true;
    }
    return false;
}

static bool compose_action_has_cancel(const WormUiWorkspaceAction* action) {
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

void worm_ui_nuklear_queue_compose_key(std::uint32_t keycode, std::uint32_t scancode,
                                       std::uint16_t modifiers, bool down, bool repeat) {
    if (state.cancel_pending) {
        return;
    }
    if (state.key_event_count == state.key_events.size()) {
        state.key_event_count = 0;
        state.cancel_pending = true;
        state.pointer_hold_active = false;
        state.note_paint_pointer_active = false;
        state.semantic_layer_paint_pointer_active = false;
        state.lane_hold_active = false;
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
        state.timing_stage_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.keyboard_focus = false;
        state.lane_hold_keycode = 0;
        state.lane_hold_lane_id = 0;
        return;
    }
    state.key_events[state.key_event_count++] = {
        .keycode = keycode,
        .scancode = scancode,
        .modifiers = modifiers,
        .down = down,
        .repeat = repeat,
    };
}

void worm_ui_nuklear_queue_compose_wheel(double delta_x, double delta_y, double mouse_x,
                                         double mouse_y, std::uint16_t modifiers,
                                         bool coarse_steps) {
    if (state.wheel_event_count == state.wheel_events.size()) {
        state.wheel_event_count = 0;
        state.cancel_pending = true;
        state.key_event_count = 0;
        state.pointer_hold_active = false;
        state.note_paint_pointer_active = false;
        state.semantic_layer_paint_pointer_active = false;
        state.lane_hold_active = false;
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
        state.note_move_candidate_active = false;
        state.note_move_active = false;
        state.timing_stage_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.keyboard_focus = false;
        state.lane_hold_keycode = 0;
        state.lane_hold_lane_id = 0;
        return;
    }
    state.wheel_events[state.wheel_event_count++] = {
        .delta_x = delta_x,
        .delta_y = delta_y,
        .mouse_x = mouse_x,
        .mouse_y = mouse_y,
        .modifiers = modifiers,
        .coarse_steps = coarse_steps,
    };
}

void worm_ui_nuklear_queue_compose_middle_click(float x, float y) {
    state.middle_click_pending = true;
    state.middle_click_x = x;
    state.middle_click_y = y;
}

void worm_ui_nuklear_compose_handle_focus_lost() {
    state.cancel_pending = true;
    state.key_event_count = 0;
    state.wheel_event_count = 0;
    state.middle_click_pending = false;
    state.keyboard_focus = false;
    state.pointer_hold_active = false;
    // Keep paint capture until Compose can deliver the pending cancel.
    state.lane_hold_active = false;
    state.space_range_hold_active = false;
    state.space_range_hold_moved = false;
    state.note_move_candidate_active = false;
    state.note_move_active = false;
    state.timing_stage_drag_active = false;
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
    state.navigation_key_active = false;
    state.navigation_keycode = 0;
    state.pointer_navigation_active = false;
}

void worm_ui_nuklear_compose_discard_frame_input() {
    state.wheel_event_count = 0;
    state.middle_click_pending = false;
}

static struct nk_rect compose_screen_rect(const WormUiComposeRect* source,
                                          const WormUiComposeView* view, struct nk_rect viewport) {
    const auto x_scale = viewport.w / static_cast<float>(view->viewport_width_pixels);
    const auto y_scale = viewport.h / static_cast<float>(view->viewport_height_pixels);
    const auto left = viewport.x + static_cast<float>(source->left) * x_scale;
    const auto top = viewport.y + static_cast<float>(source->top) * y_scale;
    const auto right = viewport.x + static_cast<float>(source->right) * x_scale;
    const auto bottom = viewport.y + static_cast<float>(source->bottom) * y_scale;
    const auto clipped_left = std::max(viewport.x, left);
    const auto clipped_top = std::max(viewport.y, top);
    const auto clipped_right = std::min(viewport.x + viewport.w, right);
    const auto clipped_bottom = std::min(viewport.y + viewport.h, bottom);
    return nk_rect(clipped_left, clipped_top, std::max(0.0f, clipped_right - clipped_left),
                   std::max(0.0f, clipped_bottom - clipped_top));
}

static void compose_fill_rect(struct nk_command_buffer* canvas, const WormUiComposeRect* source,
                              const WormUiComposeView* view, struct nk_rect viewport,
                              struct nk_color color) {
    const auto rect = compose_screen_rect(source, view, viewport);
    if (rect.w > 0.0f && rect.h > 0.0f) {
        nk_fill_rect(canvas, rect, 0.0f, color);
    }
}

static struct nk_color compose_lane_color(uint8_t role, size_t display_order) {
    const auto alternate = (display_order & 1U) != 0;
    switch (role) {
    case 1: // scratch
        return alternate ? nk_rgb(48, 44, 68) : nk_rgb(42, 39, 61);
    case 5: // free zone
        return alternate ? nk_rgb(47, 54, 63) : nk_rgb(42, 49, 58);
    case 6: // foot pedal
        return alternate ? nk_rgb(40, 58, 59) : nk_rgb(36, 52, 54);
    default:
        return alternate ? nk_rgb(37, 45, 59) : nk_rgb(33, 40, 53);
    }
}

static int compose_rect_valid(const WormUiComposeRect* rect) {
    if (!rect || !std::isfinite(rect->left) || !std::isfinite(rect->top) ||
        !std::isfinite(rect->right) || !std::isfinite(rect->bottom) || rect->right < rect->left ||
        rect->bottom < rect->top) {
        return 0;
    }
    constexpr double coordinate_limit = 10000000.0;
    return std::abs(rect->left) <= coordinate_limit && std::abs(rect->top) <= coordinate_limit &&
           std::abs(rect->right) <= coordinate_limit && std::abs(rect->bottom) <= coordinate_limit;
}

static bool compose_selection_mode_valid(WormUiComposeSelectionMode mode) {
    return mode == WORM_UI_COMPOSE_SELECTION_REPLACE || mode == WORM_UI_COMPOSE_SELECTION_ADD ||
           mode == WORM_UI_COMPOSE_SELECTION_TOGGLE;
}

static int compose_geometry_valid(const WormUiComposeView* view) {
    if (!view || !std::isfinite(view->pixels_per_standard_measure) ||
        view->pixels_per_standard_measure < WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE ||
        view->pixels_per_standard_measure > WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE ||
        !std::isfinite(view->viewport_width_pixels) || view->viewport_width_pixels <= 0.0 ||
        view->viewport_width_pixels > 10000000.0 || !std::isfinite(view->viewport_height_pixels) ||
        view->viewport_height_pixels <= 0.0 || view->viewport_height_pixels > 10000000.0 ||
        !std::isfinite(view->playable_field_right_pixels) ||
        !std::isfinite(view->playable_field_left_pixels) ||
        view->playable_field_left_pixels < 0.0 ||
        view->playable_field_left_pixels > view->playable_field_right_pixels ||
        view->playable_field_right_pixels < 0.0 ||
        view->playable_field_right_pixels > view->viewport_width_pixels ||
        !std::isfinite(view->timing_document_begin_fraction) ||
        !std::isfinite(view->timing_document_end_fraction) ||
        view->timing_document_begin_fraction < 0.0 ||
        view->timing_document_end_fraction < view->timing_document_begin_fraction ||
        view->timing_document_end_fraction > 1.0 ||
        (view->timing_stage_active &&
         (!std::isfinite(view->timing_stage_y_pixels) || view->timing_stage_y_pixels < 0.0 ||
          view->timing_stage_y_pixels > view->viewport_height_pixels ||
          view->timing_stage_lane < WORM_UI_TIMING_LANE_BPM ||
          view->timing_stage_lane > WORM_UI_TIMING_LANE_ANCHOR)) ||
        (view->playhead_visible &&
         (!std::isfinite(view->playhead_y_pixels) || view->playhead_y_pixels < 0.0 ||
          view->playhead_y_pixels > view->viewport_height_pixels)) ||
        view->branch_row_count > WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY ||
        view->branch_choice_count > WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY ||
        view->branch_diagnostic_count > WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY ||
        view->semantic_layer_count > WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY ||
        view->beat_bookmark_count > WORM_UI_COMPOSE_BOOKMARK_CAPACITY ||
        view->beat_section_count > WORM_UI_COMPOSE_SECTION_CAPACITY ||
        view->audio_bookmark_count > WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY ||
        view->audio_section_count > WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY ||
        view->timing_marker_count > WORM_UI_TIMING_MARKER_CAPACITY ||
        view->resource_count > WORM_UI_COMPOSE_RESOURCE_CAPACITY ||
        view->resource_total_count < view->resource_count ||
        (view->semantic_layer_page_count == 0
             ? view->semantic_layer_page != 0 || view->semantic_layer_total_count != 0 ||
                   view->semantic_layer_count != 0
             : view->semantic_layer_total_count == 0 ||
                   view->semantic_layer_page >= view->semantic_layer_page_count ||
                   view->semantic_layer_page_count !=
                       1 + (view->semantic_layer_total_count - 1) /
                               WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY ||
                   view->semantic_layer_count !=
                       std::min(static_cast<size_t>(WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY),
                                view->semantic_layer_total_count -
                                    view->semantic_layer_page *
                                        WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY)) ||
        (view->branch_row_count != 0 && !view->branch_rows) ||
        (view->branch_choice_count != 0 && !view->branch_choices) ||
        (view->beat_bookmark_count != 0 && !view->beat_bookmarks) ||
        (view->beat_section_count != 0 && !view->beat_sections) ||
        (view->audio_bookmark_count != 0 && !view->audio_bookmarks) ||
        (view->audio_section_count != 0 && !view->audio_sections) ||
        (view->timing_marker_count != 0 && !view->timing_markers) ||
        (view->resource_count != 0 && !view->resources))
        return 0;
    if ((view->area_selection_preview_visible &&
         (!compose_rect_valid(&view->area_selection_preview_bounds) ||
          !compose_selection_mode_valid(view->area_selection_preview_mode))) ||
        (view->space_range_preview_visible &&
         (!compose_rect_valid(&view->space_range_preview_bounds) ||
          !compose_selection_mode_valid(view->space_range_preview_mode) ||
          !std::isfinite(view->space_range_anchor_y_pixels) ||
          !std::isfinite(view->space_range_tail_y_pixels) ||
          view->space_range_anchor_y_pixels < 0.0 ||
          view->space_range_anchor_y_pixels > view->viewport_height_pixels ||
          view->space_range_tail_y_pixels < 0.0 ||
          view->space_range_tail_y_pixels > view->viewport_height_pixels)))
        return 0;
    for (size_t index = 0; index < view->band_count; ++index) {
        if (!compose_rect_valid(&view->bands[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->lane_count; ++index) {
        if (!compose_rect_valid(&view->lanes[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        if (!std::isfinite(view->grid_lines[index].y_pixels) ||
            std::abs(view->grid_lines[index].y_pixels) > 10000000.0)
            return 0;
    }
    for (size_t index = 0; index < view->timing_marker_count; ++index) {
        const auto& marker = view->timing_markers[index];
        if (marker.object_id == 0 || !std::isfinite(marker.marker_y_pixels) ||
            marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            !std::isfinite(marker.marker_left_fraction) ||
            !std::isfinite(marker.marker_right_fraction) ||
            marker.marker_left_fraction < 0.0 ||
            marker.marker_right_fraction <= marker.marker_left_fraction ||
            marker.marker_right_fraction > 1.0 || marker.lane < WORM_UI_TIMING_LANE_BPM ||
            marker.lane > WORM_UI_TIMING_LANE_ANCHOR) {
            return 0;
        }
    }
    constexpr std::uint8_t compose_resource_roles = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE |
                                                    WORM_UI_COMPOSE_RESOURCE_ROLE_BGM |
                                                    WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
    for (size_t index = 0; index < view->resource_count; ++index) {
        const auto& resource = view->resources[index];
        if (resource.resource_id == 0 || resource.asset_id == 0 || resource.roles == 0 ||
            (resource.roles & ~compose_resource_roles) != 0 || !resource.target_index ||
            !resource.name || !resource.source_name) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->difference_count; ++index) {
        if (!compose_rect_valid(&view->differences[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        if (!compose_rect_valid(&view->hold_bodies[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        if (!compose_rect_valid(&view->note_endpoints[index].bounds) ||
            view->note_endpoints[index].glyph > 5)
            return 0;
    }
    for (size_t index = 0; index < view->bgm_lane_count; ++index) {
        if (!compose_rect_valid(&view->bgm_lanes[index].bounds) ||
            view->bgm_lanes[index].source_lane_id == 0)
            return 0;
    }
    for (size_t index = 0; index < view->bgm_event_count; ++index) {
        if (!compose_rect_valid(&view->bgm_events[index].bounds) ||
            view->bgm_events[index].event_id == 0 || view->bgm_events[index].source_lane_id == 0)
            return 0;
    }
    for (size_t index = 0; index < view->semantic_layer_count; ++index) {
        if (!view->semantic_layers || view->semantic_layers[index].layer_id == 0 ||
            !view->semantic_layers[index].name) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->beat_bookmark_count; ++index) {
        if (view->beat_bookmarks[index].bookmark_id == 0 || !view->beat_bookmarks[index].label ||
            view->beat_bookmarks[index].measure_number == 0) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->beat_section_count; ++index) {
        if (view->beat_sections[index].section_id == 0 || !view->beat_sections[index].label ||
            view->beat_sections[index].begin_measure_number == 0 ||
            view->beat_sections[index].end_measure_number == 0 ||
            view->beat_sections[index].begin_measure_number >
                view->beat_sections[index].end_measure_number ||
            (view->beat_sections[index].begin_measure_number ==
                 view->beat_sections[index].end_measure_number &&
             view->beat_sections[index].begin_row >= view->beat_sections[index].end_row)) {
            return 0;
        }
    }
    if (view->beat_section_selection_span_available &&
        (view->beat_section_selection_begin_measure_number == 0 ||
         view->beat_section_selection_end_measure_number == 0 ||
         view->beat_section_selection_begin_measure_number >
             view->beat_section_selection_end_measure_number ||
         (view->beat_section_selection_begin_measure_number ==
              view->beat_section_selection_end_measure_number &&
          view->beat_section_selection_begin_row >= view->beat_section_selection_end_row))) {
        return 0;
    }
    for (size_t index = 0; index < view->audio_bookmark_count; ++index) {
        const auto& bookmark = view->audio_bookmarks[index];
        if (bookmark.bookmark_id == 0 || !bookmark.label ||
            (bookmark.projection_visible &&
             (!std::isfinite(bookmark.projected_y_pixels) || bookmark.projected_y_pixels < 0.0 ||
              bookmark.projected_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->audio_section_count; ++index) {
        const auto& section = view->audio_sections[index];
        if (section.section_id == 0 || !section.label ||
            section.end_audio_milliseconds <= section.begin_audio_milliseconds ||
            (section.begin_projection_visible &&
             (!std::isfinite(section.projected_begin_y_pixels) ||
              section.projected_begin_y_pixels < 0.0 ||
              section.projected_begin_y_pixels > view->viewport_height_pixels)) ||
            (section.end_projection_visible &&
             (!std::isfinite(section.projected_end_y_pixels) ||
              section.projected_end_y_pixels < 0.0 ||
              section.projected_end_y_pixels > view->viewport_height_pixels)) ||
            (section.span_projection_visible &&
             (!std::isfinite(section.projected_span_top_y_pixels) ||
              !std::isfinite(section.projected_span_bottom_y_pixels) ||
              section.projected_span_top_y_pixels < 0.0 ||
              section.projected_span_bottom_y_pixels < section.projected_span_top_y_pixels ||
              section.projected_span_bottom_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }
    if (view->hover_note_visible && (!view->hover_tooltip || view->hover_note_id == 0)) {
        return 0;
    }
    for (size_t index = 0; index < view->branch_row_count; ++index) {
        if (!view->branch_rows[index].display_text || view->branch_rows[index].depth > 512)
            return 0;
    }
    for (size_t index = 0; index < view->branch_choice_count; ++index) {
        const auto& choice = view->branch_choices[index];
        if (choice.branch_id == 0 || choice.kind > WORM_UI_COMPOSE_BRANCH_SWITCH ||
            (choice.editable && !choice.active) ||
            (choice.can_clear && (!choice.active || !choice.has_selected_value)) ||
            (choice.has_previous_value && choice.has_selected_value &&
             choice.previous_value >= choice.selected_value) ||
            (choice.has_next_value && choice.has_selected_value &&
             choice.next_value <= choice.selected_value)) {
            return 0;
        }
    }
    return !view->cursor || compose_rect_valid(&view->cursor->bounds);
}

int worm_ui_nuklear_compose_view_valid(const WormUiComposeView* view) {
    return view && view->band_count <= WORM_UI_COMPOSE_BAND_CAPACITY &&
           view->lane_count <= WORM_UI_COMPOSE_LANE_CAPACITY &&
           view->grid_line_count <= WORM_UI_COMPOSE_GRID_CAPACITY &&
           view->timing_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           view->difference_count <= WORM_UI_COMPOSE_DIFFERENCE_CAPACITY &&
           view->hold_body_count <= WORM_UI_COMPOSE_HOLD_CAPACITY &&
           view->note_endpoint_count <= WORM_UI_COMPOSE_NOTE_CAPACITY &&
           view->bgm_lane_count <= WORM_UI_COMPOSE_BGM_LANE_CAPACITY &&
           view->bgm_event_count <= WORM_UI_COMPOSE_BGM_EVENT_CAPACITY &&
           view->semantic_layer_count <= WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
           view->lane_key_binding_count <= WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY &&
           view->branch_row_count <= WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY &&
           view->branch_choice_count <= WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY &&
           view->branch_diagnostic_count <= WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY &&
           (view->band_count == 0 || view->bands) && (view->lane_count == 0 || view->lanes) &&
           (view->grid_line_count == 0 || view->grid_lines) &&
           (view->timing_marker_count == 0 || view->timing_markers) &&
           (view->difference_count == 0 || view->differences) &&
           (view->hold_body_count == 0 || view->hold_bodies) &&
           (view->note_endpoint_count == 0 || view->note_endpoints) &&
           (view->bgm_lane_count == 0 || view->bgm_lanes) &&
           (view->bgm_event_count == 0 || view->bgm_events) &&
           (view->semantic_layer_count == 0 || view->semantic_layers) &&
           (view->lane_key_binding_count == 0 || view->lane_key_bindings) &&
           (view->branch_choice_count == 0 || view->branch_choices) &&
           (view->branch_row_count == 0 || view->branch_rows) && compose_geometry_valid(view);
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

static void process_compose_key_events(struct nk_context* context, const WormUiComposeView* view,
                                       WormUiWorkspaceAction* action, bool window_focused,
                                       bool ime_composition_active) {
    const bool keyboard_ready =
        window_focused && state.keyboard_focus && view->scene_available && context;
    const bool note_paint_mode_active =
        view->note_paint_mode_active ||
        action->compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    const bool semantic_layer_paint_mode_active =
        view->semantic_layer_paint_mode_active ||
        action->compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
    const bool pointer_paint_mode_active =
        note_paint_mode_active || semantic_layer_paint_mode_active;
    if (ime_composition_active) {
        cancel_compose_navigation(action);
        state.key_event_count = 0;
        return;
    }
    if (pointer_paint_mode_active && state.lane_hold_active) {
        append_compose_cancel_once(action);
        state.lane_hold_active = false;
        state.lane_hold_keycode = 0;
        state.lane_hold_lane_id = 0;
    }
    if (pointer_paint_mode_active && state.space_range_hold_active) {
        append_compose_cancel_once(action);
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
    }
    if (pointer_paint_mode_active && (state.note_move_candidate_active || state.note_move_active)) {
        if (state.note_move_active) {
            append_compose_cancel_once(action);
        }
        state.note_move_candidate_active = false;
        state.note_move_active = false;
    }
    for (size_t event_index = 0; event_index < state.key_event_count && !state.action_overflowed;
         ++event_index) {
        const auto& key_event = state.key_events[event_index];
        WormUiComposeNavigationAction navigation_action{};
        if (!key_event.down && state.navigation_key_active &&
            key_event.scancode == state.navigation_keycode) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE, state.navigation_action);
            state.navigation_key_active = false;
            state.navigation_keycode = 0;
            continue;
        }
        if (state.lane_hold_active && !key_event.down &&
            key_event.scancode == state.lane_hold_keycode) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 0.0, 0.0,
                                 state.lane_hold_lane_id);
            state.lane_hold_active = false;
            state.lane_hold_keycode = 0;
            state.lane_hold_lane_id = 0;
            continue;
        }
        if (state.space_range_hold_active && !key_event.down &&
            key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
            if (state.space_range_hold_moved) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
            }
            state.space_range_hold_active = false;
            state.space_range_hold_moved = false;
            continue;
        }
        if (!keyboard_ready || !state.keyboard_focus || !key_event.down || key_event.repeat) {
            continue;
        }
        const bool shortcut = compose_shortcut_modifier(key_event.modifiers);
        if (key_event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
            if (state.space_range_hold_active) {
                append_compose_cancel_once(action);
                state.space_range_hold_active = false;
                state.space_range_hold_moved = false;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
            constexpr auto any_space_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                                SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
            if (!pointer_paint_mode_active && !state.space_range_hold_active &&
                (key_event.modifiers & any_space_modifier) == 0) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION, 0.0, 0.0,
                                     0, false, WORM_UI_COMPOSE_SELECTION_REPLACE);
                state.space_range_hold_active =
                    !view->space_range_preview_visible && !state.action_overflowed;
                state.space_range_hold_moved = false;
            }
            continue;
        }
        if (shortcut) {
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_UP)) {
            if (!compose_navigation_action_for_key(key_event.keycode, key_event.modifiers,
                                                   &navigation_action)) {
                continue;
            }
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE, navigation_action);
            state.navigation_key_active = !state.action_overflowed;
            state.navigation_keycode = key_event.scancode;
            state.navigation_action = navigation_action;
            if (!state.action_overflowed && state.space_range_hold_active) {
                state.space_range_hold_moved = true;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_DOWN)) {
            if (!compose_navigation_action_for_key(key_event.keycode, key_event.modifiers,
                                                   &navigation_action)) {
                continue;
            }
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE, navigation_action);
            state.navigation_key_active = !state.action_overflowed;
            state.navigation_keycode = key_event.scancode;
            state.navigation_action = navigation_action;
            if (!state.action_overflowed && state.space_range_hold_active) {
                state.space_range_hold_moved = true;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_PAGEUP)) {
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD);
            state.navigation_key_active = !state.action_overflowed;
            state.navigation_keycode = key_event.scancode;
            state.navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD;
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_PAGEDOWN)) {
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD);
            state.navigation_key_active = !state.action_overflowed;
            state.navigation_keycode = key_event.scancode;
            state.navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
            continue;
        }
        if (pointer_paint_mode_active) {
            continue;
        }
        if (state.lane_hold_active ||
            (key_event.modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_MODE |
                                    SDL_KMOD_LEVEL5)) != 0) {
            continue;
        }
        for (size_t binding_index = 0; binding_index < view->lane_key_binding_count;
             ++binding_index) {
            const auto& binding = view->lane_key_bindings[binding_index];
            if (binding.scancode != key_event.scancode || binding.lane_id == 0) {
                continue;
            }
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 0.0, 0.0,
                                 binding.lane_id, (key_event.modifiers & SDL_KMOD_SHIFT) != 0);
            if ((key_event.modifiers & SDL_KMOD_SHIFT) == 0) {
                state.lane_hold_active = true;
                state.lane_hold_keycode = key_event.scancode;
                state.lane_hold_lane_id = binding.lane_id;
            }
            break;
        }
    }
    state.key_event_count = 0;
}

void worm_ui_nuklear_prepare_compose_panel(const WormUiComposeView* view, bool compose_tab_active,
                                           bool window_focused, WormUiWorkspaceAction* action) {
    if (!action) {
        return;
    }
    if (view && compose_tab_active && view->visible) {
        state.action_overflowed = false;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
        action->compose_input_viewport_width_pixels = view->viewport_width_pixels;
        if (!view->scene_available) {
            cancel_compose_navigation(action);
        }
        if (state.cancel_pending) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            state.cancel_pending = false;
            state.resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            state.note_paint_pointer_active = false;
            state.semantic_layer_paint_pointer_active = false;
            state.space_range_hold_active = false;
            state.space_range_hold_moved = false;
            state.note_move_candidate_active = false;
            state.note_move_active = false;
            state.timing_stage_drag_active = false;
        }
        const auto project_changed = state.last_project_id != view->project_id;
        const auto chart_changed = project_changed || state.last_chart_id != view->chart_id;
        const auto revision_changed = state.last_document_revision != view->document_revision;
        if (project_changed) {
            state.armed_resource_id = 0;
            state.resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            state.last_project_id = view->project_id;
        }
        if (chart_changed || revision_changed) {
            if (state.note_paint_pointer_active || state.semantic_layer_paint_pointer_active ||
                state.space_range_hold_active || state.note_move_active) {
                append_compose_cancel_once(action);
            }
            state.pointer_hold_active = false;
            state.note_paint_pointer_active = false;
            state.semantic_layer_paint_pointer_active = false;
            state.lane_hold_active = false;
            state.space_range_hold_active = false;
            state.space_range_hold_moved = false;
            state.note_move_candidate_active = false;
            state.note_move_active = false;
            state.timing_stage_drag_active = false;
            state.area_selection_candidate_active = false;
            state.area_selection_active = false;
            state.semantic_layer_color_editor_visible = false;
            state.lane_hold_keycode = 0;
            state.lane_hold_lane_id = 0;
            cancel_compose_navigation(action);
        }
        if (chart_changed) {
            state.resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
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
        if (state.note_paint_pointer_active &&
            (!window_focused || !view->scene_available || !view->note_paint_mode_active)) {
            append_compose_cancel_once(action);
            state.note_paint_pointer_active = false;
        }
        if (state.semantic_layer_paint_pointer_active &&
            (!window_focused || !view->scene_available ||
             !view->semantic_layer_paint_mode_active)) {
            append_compose_cancel_once(action);
            state.semantic_layer_paint_pointer_active = false;
        }
        return;
    }

    state.cancel_pending = false;
    state.key_event_count = 0;
    state.wheel_event_count = 0;
    state.middle_click_pending = false;
    state.keyboard_focus = false;
    if (state.pointer_hold_active || state.lane_hold_active || state.note_paint_pointer_active ||
        state.semantic_layer_paint_pointer_active || state.space_range_hold_active ||
        state.area_selection_active) {
        append_compose_cancel_once(action);
    }
    state.pointer_hold_active = false;
    state.note_paint_pointer_active = false;
    state.semantic_layer_paint_pointer_active = false;
    state.lane_hold_active = false;
    state.space_range_hold_active = false;
    state.space_range_hold_moved = false;
    state.area_selection_candidate_active = false;
    state.area_selection_active = false;
    state.timing_stage_drag_active = false;
    state.semantic_layer_color_editor_visible = false;
    cancel_compose_navigation(action);
    state.last_chart_id = 0;
    state.last_document_revision = 0;
}

void worm_ui_nuklear_render_compose_panel(struct nk_context* context, const WormUiComposeView* view,
                                          const WormUiTimingView* timing_view,
                                          WormUiWorkspaceAction* action, bool window_focused,
                                          bool ime_composition_active,
                                          const worm_ui_nuklear_pointer_click_t* double_clicks,
                                          std::size_t double_click_count,
                                          std::size_t shortcut_event_count) {
    if (!context || !view || !timing_view || !action || !view->visible ||
        (double_click_count != 0 && !double_clicks)) {
        return;
    }
    if (state.label_project_id != view->project_id || state.label_chart_id != view->chart_id ||
        state.label_document_revision != view->document_revision ||
        state.label_note_id != view->selected_note_label_note_id) {
        state.note_label.fill('\0');
        if (view->selected_note_label && view->selected_note_label_note_id != 0) {
            const auto length =
                std::min(strlen(view->selected_note_label), state.note_label.size() - 1);
            memcpy(state.note_label.data(), view->selected_note_label, length);
            state.note_label[length] = '\0';
        }
        state.label_project_id = view->project_id;
        state.label_chart_id = view->chart_id;
        state.label_document_revision = view->document_revision;
        state.label_note_id = view->selected_note_label_note_id;
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 11);
    nk_layout_row_push(context, 0.06f);
    if (nk_button_label(context, "Zoom -") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_OUT;
    nk_layout_row_push(context, 0.06f);
    if (nk_button_label(context, "Zoom +") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_IN;
    std::optional<WormUiComposeNavigationAction> pointer_navigation;
    const auto& navigation_left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto pointer_down =
        window_focused && view->scene_available && navigation_left_button.down != 0;
    const auto pointer_started = pointer_down && navigation_left_button.clicked != 0;
    if (state.pointer_navigation_active && pointer_down) {
        pointer_navigation = state.pointer_navigation_action;
    }
    const auto navigation_button = [&](const char* label, WormUiComposeNavigationAction action) {
        const auto bounds = nk_widget_bounds(context);
        if (!view->scene_available) {
            nk_widget_disable_begin(context);
        }
        (void)nk_button_label(context, label);
        if (!view->scene_available) {
            nk_widget_disable_end(context);
        }
        if (!state.pointer_navigation_active && pointer_started &&
            navigation_left_button.clicked_pos.x >= bounds.x &&
            navigation_left_button.clicked_pos.x <= bounds.x + bounds.w &&
            navigation_left_button.clicked_pos.y >= bounds.y &&
            navigation_left_button.clicked_pos.y <= bounds.y + bounds.h) {
            pointer_navigation = action;
        }
    };
    nk_layout_row_push(context, 0.07f);
    navigation_button("Step up", WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD);
    nk_layout_row_push(context, 0.07f);
    navigation_button("Step down", WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD);
    nk_layout_row_push(context, 0.08f);
    navigation_button("Page up", WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD);
    nk_layout_row_push(context, 0.08f);
    navigation_button("Page down", WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD);
    update_compose_pointer_navigation(action, pointer_navigation);
    nk_layout_row_push(context, 0.07f);
    if (nk_button_label(context, "Reset") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW;
    nk_layout_row_push(context, 0.14f);
    nk_labelf(context, NK_TEXT_LEFT, "Zoom %.0f px/measure", view->pixels_per_standard_measure);
    nk_layout_row_push(context, 0.12f);
    if (!view->scene_available) {
        nk_widget_disable_begin(context);
    }
    const auto paint_label = view->note_paint_mode_active ? "Paint: Armed" : "Paint: Off";
    if (nk_button_label(context, paint_label) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    }
    if (!view->scene_available) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.07f);
    if (nk_button_label(context, state.inspector_visible ? "Hide side" : "Show side")) {
        state.inspector_visible = !state.inspector_visible;
    }
    nk_layout_row_push(context, 0.18f);
    if (view->truncated) {
        nk_label(context, "Geometry capped", NK_TEXT_RIGHT);
    } else {
        nk_labelf(context, NK_TEXT_RIGHT, "%zu notes · %zu lanes · %zu grid",
                  view->note_endpoint_count, view->lane_count, view->grid_line_count);
    }
    nk_layout_row_end(context);

    const auto compose_body_bounds = nk_window_get_content_region(context);
    const auto compose_body_height = std::max(64.0f, compose_body_bounds.h);
    constexpr float inspector_gap = 5.0f;
    const auto inspector_drawn = state.inspector_visible && compose_body_bounds.w >= 540.0f;
    const auto inspector_width = inspector_drawn
                                     ? std::clamp(compose_body_bounds.w * 0.24f, 280.0f, 380.0f)
                                     : 0.0f;
    const auto editor_width = std::max(120.0f, compose_body_bounds.w - inspector_width -
                                                   (inspector_drawn ? inspector_gap : 0.0f));
    nk_layout_space_begin(context, NK_STATIC, compose_body_height, inspector_drawn ? 2 : 1);
    if (inspector_drawn) {
        nk_layout_space_push(context, nk_rect(editor_width + inspector_gap, 0.0f, inspector_width,
                                              compose_body_height));
        if (nk_group_begin(context, "Compose inspector", NK_WINDOW_BORDER)) {

    if (nk_tree_push(context, NK_TREE_TAB, "Timing lanes", NK_MINIMIZED)) {
        constexpr std::array<const char*, 8> labels{"BPM", "SV", "SPD", "STP",
                                                    "DLY", "WRP", "KIA", "ANC"};
        for (std::size_t row = 0; row < 2; ++row) {
            nk_layout_row_begin(context, NK_DYNAMIC, 24, 4);
            for (std::size_t column = 0; column < 4; ++column) {
                const auto lane = row * 4 + column;
                const auto bit = static_cast<std::uint8_t>(1U << lane);
                const auto visible = (view->timing_lane_visibility_mask & bit) != 0;
                const auto editing_lane = timing_view->stage_active &&
                                          static_cast<std::size_t>(timing_view->stage_lane) == lane;
                char label[16]{};
                (void)snprintf(label, sizeof(label), "%s: %s", labels[lane],
                               visible ? "On" : "Off");
                nk_layout_row_push(context, 0.25f);
                const auto disabled = !view->scene_available || (editing_lane && visible);
                if (disabled) {
                    nk_widget_disable_begin(context);
                }
                const auto clicked = nk_button_label(context, label);
                if (disabled) {
                    nk_widget_disable_end(context);
                }
                if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_timing_lane = static_cast<std::uint8_t>(lane);
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY;
                }
            }
            nk_layout_row_end(context);
        }
        nk_tree_pop(context);
    }

    if (timing_view->stage_active &&
        nk_tree_push(context, NK_TREE_TAB, "Timing event", NK_MAXIMIZED)) {
        worm_ui_nuklear_render_timing_stage_editor(context, timing_view, action);
        nk_tree_pop(context);
    }

    worm_ui_nuklear_render_compose_resources(context, view, action, &state);

    if (nk_tree_push(context, NK_TREE_TAB, "Markers / sections", NK_MINIMIZED)) {
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    if (!view->scene_available || !view->beat_marker_navigation_available) {
        nk_widget_disable_begin(context);
    }
    nk_layout_row_push(context, 0.5f);
    const auto previous_marker_clicked = nk_button_label(context, "Previous marker");
    nk_layout_row_push(context, 0.5f);
    const auto next_marker_clicked = nk_button_label(context, "Next marker");
    if (!view->scene_available || !view->beat_marker_navigation_available) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context,
             view->beat_marker_navigation_available
                 ? "Navigate beat-anchored bookmarks and section boundaries"
                 : "No beat-anchored bookmarks or sections",
             NK_TEXT_LEFT);
    if (action && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        if (previous_marker_clicked) {
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER;
        } else if (next_marker_clicked) {
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER;
        }
    }

    worm_ui_nuklear_render_compose_markers(context, view, action);
    nk_tree_pop(context);
    }

    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Label (%zu)", view->selected_note_count);
    if (!view->scene_available || view->selected_note_count == 0) {
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 25, 1);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.note_label.data(),
                                         state.note_label.size(), nk_filter_default);
    if (view->focus_note_label_editor) {
        nk_edit_focus(context, NK_EDIT_GOTO_END_ON_ACTIVATE);
        if (action) {
            action->compose_note_label_focus_consumed = true;
        }
    }
    nk_layout_row_dynamic(context, 25, 2);
    if (nk_button_label(context, "Apply") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_note_label, state.note_label.data(),
               sizeof(action->compose_note_label));
        action->compose_action = WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL;
    }
    if (nk_button_label(context, "Clear") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL;
    }
    if (!view->scene_available || view->selected_note_count == 0) {
        nk_widget_disable_end(context);
    }

    const auto visible_layer_count = view->semantic_layer_count;
    bool semantic_target_active = false;
    for (size_t index = 0; index < visible_layer_count; ++index) {
        semantic_target_active = semantic_target_active || view->semantic_layers[index].selected;
    }
    if (nk_tree_push(context, NK_TREE_TAB, "Semantic layers", NK_MINIMIZED)) {
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_button_label(context, "Add semantic layer") && action &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 3);
    nk_layout_row_push(context, 0.28f);
    if (view->semantic_layer_page == 0) {
        nk_widget_disable_begin(context);
    }
    const auto previous_layer_page_clicked = nk_button_label(context, "Previous");
    if (view->semantic_layer_page == 0) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.44f);
    if (view->semantic_layer_page_count == 0) {
        nk_label(context, "Palette empty", NK_TEXT_CENTERED);
    } else {
        nk_labelf(context, NK_TEXT_CENTERED, "Page %zu/%zu · %zu layers",
                  view->semantic_layer_page + 1, view->semantic_layer_page_count,
                  view->semantic_layer_total_count);
    }
    nk_layout_row_push(context, 0.28f);
    const auto last_layer_page = view->semantic_layer_page_count == 0 ||
                                 view->semantic_layer_page + 1 >= view->semantic_layer_page_count;
    if (last_layer_page) {
        nk_widget_disable_begin(context);
    }
    const auto next_layer_page_clicked = nk_button_label(context, "Next");
    if (last_layer_page) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
    if (action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (previous_layer_page_clicked) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE;
        } else if (next_layer_page_clicked) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE;
        }
    }
    if (visible_layer_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Semantic layers: no visible palette entries", NK_TEXT_LEFT);
    } else {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, view->semantic_layers_truncated ? "Layers+" : "Layers", NK_TEXT_LEFT);
        for (size_t index = 0; index < visible_layer_count; ++index) {
            const auto& layer = view->semantic_layers[index];
            std::array<char, 96> label{};
            const auto name_length = std::min<size_t>(strlen(layer.name), 24);
            snprintf(label.data(), label.size(), "%zu:%.*s #%llu%s%s", index + 1,
                     static_cast<int>(name_length), layer.name,
                     static_cast<unsigned long long>(layer.layer_id),
                     layer.visible ? "" : " [hidden]", layer.selected ? " *" : "");
            nk_layout_row_dynamic(context, 25, 1);
            const auto palette_color = nk_rgba(layer.red, layer.green, layer.blue, layer.alpha);
            nk_style_push_style_item(context, &context->style.button.normal,
                                     nk_style_item_color(palette_color));
            nk_style_push_style_item(context, &context->style.button.hover,
                                     nk_style_item_color(palette_color));
            nk_style_push_style_item(context, &context->style.button.active,
                                     nk_style_item_color(palette_color));
            if (!view->scene_available) {
                nk_widget_disable_begin(context);
            }
            const auto clicked = nk_button_label(context, label.data());
            if (!view->scene_available) {
                nk_widget_disable_end(context);
            }
            nk_style_pop_style_item(context);
            nk_style_pop_style_item(context);
            nk_style_pop_style_item(context);
            if (clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER;
                action->compose_semantic_layer_id = layer.layer_id;
            }
        }
        const auto selected_layer =
            std::ranges::find_if(view->semantic_layers, view->semantic_layers + visible_layer_count,
                                 [](const auto& layer) { return layer.selected; });
        if (selected_layer != view->semantic_layers + visible_layer_count) {
            if (state.semantic_layer_name_id != selected_layer->layer_id ||
                state.semantic_layer_name_revision != view->document_revision) {
                state.semantic_layer_name.fill('\0');
                const auto length =
                    std::min(strlen(selected_layer->name), state.semantic_layer_name.size() - 1);
                memcpy(state.semantic_layer_name.data(), selected_layer->name, length);
                state.semantic_layer_name_id = selected_layer->layer_id;
                state.semantic_layer_name_revision = view->document_revision;
            }
            if (state.semantic_layer_color_id != selected_layer->layer_id) {
                state.semantic_layer_color_id = selected_layer->layer_id;
                state.semantic_layer_color_editor_visible = false;
            }
            if (!state.semantic_layer_color_editor_visible) {
                const auto color =
                    nk_color_cf(nk_rgba(selected_layer->red, selected_layer->green,
                                        selected_layer->blue, selected_layer->alpha));
                state.semantic_layer_color = {color.r, color.g, color.b, color.a};
            }
            nk_layout_row_dynamic(context, 24, 1);
            (void)nk_edit_string_zero_terminated(
                context, NK_EDIT_FIELD, state.semantic_layer_name.data(),
                state.semantic_layer_name.size(), nk_filter_default);
            nk_layout_row_dynamic(context, 24, 2);
            if (nk_button_label(context, "Rename") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                memcpy(action->compose_semantic_layer_name, state.semantic_layer_name.data(),
                       sizeof(action->compose_semantic_layer_name));
                action->compose_action = WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER;
            }
            if (nk_button_label(context, "Color...")) {
                state.semantic_layer_color_editor_visible =
                    !state.semantic_layer_color_editor_visible;
            }
            nk_layout_row_dynamic(context, 24, 4);
            if (selected_layer->locked) {
                nk_widget_disable_begin(context);
            }
            const auto delete_layer_clicked = nk_button_label(context, "Delete");
            if (selected_layer->locked) {
                nk_widget_disable_end(context);
            }
            if (delete_layer_clicked && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER;
            }
            if (nk_button_label(context, "Up") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
            }
            if (nk_button_label(context, "Down") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN;
            }
            if (nk_button_label(context, view->semantic_layer_target_visible ? "Hide" : "Show") &&
                action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY;
            }
            if (state.semantic_layer_color_editor_visible) {
                nk_layout_row_dynamic(context, 110, 1);
                auto color = nk_colorf{state.semantic_layer_color.r, state.semantic_layer_color.g,
                                       state.semantic_layer_color.b, state.semantic_layer_color.a};
                color = nk_color_picker(context, color, NK_RGBA);
                state.semantic_layer_color = {color.r, color.g, color.b, color.a};
                const auto byte_component = [](float component) {
                    return static_cast<uint8_t>(
                        std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
                };
                const auto red = byte_component(state.semantic_layer_color.r);
                const auto green = byte_component(state.semantic_layer_color.g);
                const auto blue = byte_component(state.semantic_layer_color.b);
                const auto alpha = byte_component(state.semantic_layer_color.a);
                nk_layout_row_begin(context, NK_DYNAMIC, 24, 3);
                nk_layout_row_push(context, 0.46f);
                nk_labelf(context, NK_TEXT_LEFT, "RGBA %u %u %u %u", static_cast<unsigned>(red),
                          static_cast<unsigned>(green), static_cast<unsigned>(blue),
                          static_cast<unsigned>(alpha));
                nk_layout_row_push(context, 0.27f);
                const auto apply_color_clicked = nk_button_label(context, "Apply color");
                nk_layout_row_push(context, 0.27f);
                const auto cancel_color_clicked = nk_button_label(context, "Cancel color");
                nk_layout_row_end(context);
                if (apply_color_clicked && action &&
                    action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_semantic_layer_red = red;
                    action->compose_semantic_layer_green = green;
                    action->compose_semantic_layer_blue = blue;
                    action->compose_semantic_layer_alpha = alpha;
                    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
                    state.semantic_layer_color_editor_visible = false;
                } else if (cancel_color_clicked) {
                    state.semantic_layer_color_editor_visible = false;
                }
            }
            if (selected_layer->assigned_note_count != 0) {
                nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
                nk_layout_row_push(context, 0.65f);
                nk_labelf(context, NK_TEXT_LEFT, "Assigned to %zu note%s",
                          selected_layer->assigned_note_count,
                          selected_layer->assigned_note_count == 1 ? "" : "s");
                nk_layout_row_push(context, 0.35f);
                if (selected_layer->locked) {
                    nk_widget_disable_begin(context);
                }
                const auto delete_and_unlink_clicked = nk_button_label(context, "Delete + unlink");
                if (selected_layer->locked) {
                    nk_widget_disable_end(context);
                }
                if (delete_and_unlink_clicked && action &&
                    action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS;
                }
                nk_layout_row_end(context);
            }
            if (selected_layer->visible) {
                nk_layout_row_begin(context, NK_DYNAMIC, 22, 2);
                nk_layout_row_push(context, 0.5f);
                if (nk_button_label(context, view->semantic_layer_target_required_filter
                                                 ? "Require: On"
                                                 : "Require filter") &&
                    action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
                }
                nk_layout_row_push(context, 0.5f);
                if (nk_button_label(context, view->semantic_layer_target_excluded_filter
                                                 ? "Exclude: On"
                                                 : "Exclude filter") &&
                    action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER;
                }
                nk_layout_row_end(context);
            }
        }

        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Layer (%zu)", view->selected_note_count);
        const auto membership_enabled = view->scene_available && semantic_target_active &&
                                        !view->semantic_layer_target_locked &&
                                        view->selected_note_count != 0;
        const auto membership_button = [&](const char* label, WormUiComposeActionKind kind) {
            if (!membership_enabled) {
                nk_widget_disable_begin(context);
            }
            const auto clicked = nk_button_label(context, label);
            if (!membership_enabled) {
                nk_widget_disable_end(context);
            }
            if (clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = kind;
            }
        };
        nk_layout_row_dynamic(context, 24, 3);
        if (!view->scene_available || !semantic_target_active) {
            nk_widget_disable_begin(context);
        }
        const auto select_clicked = nk_button_label(context, "Select");
        if (!view->scene_available || !semantic_target_active) {
            nk_widget_disable_end(context);
        }
        if (select_clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES;
        }
        membership_button("Add", WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER);
        membership_button("Toggle", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER);
        nk_layout_row_dynamic(context, 24, 3);
        membership_button("Remove", WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER);
        const auto layer_paint_enabled =
            view->scene_available && semantic_target_active && !view->semantic_layer_target_locked;
        if (!layer_paint_enabled) {
            nk_widget_disable_begin(context);
        }
        const auto layer_paint_clicked = nk_button_label(
            context, view->semantic_layer_paint_mode_active ? "Paint: Armed" : "Paint: Off");
        if (!layer_paint_enabled) {
            nk_widget_disable_end(context);
        }
        if (layer_paint_clicked && action &&
            action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
        }
        if (!semantic_target_active) {
            nk_widget_disable_begin(context);
        }
        const auto solo_clicked =
            nk_button_label(context, view->semantic_layer_solo_active ? "Solo: On" : "Solo: Off");
        if (!semantic_target_active) {
            nk_widget_disable_end(context);
        }
        if (solo_clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO;
        }
        nk_layout_row_dynamic(context, 24, 2);
        if (!semantic_target_active) {
            nk_widget_disable_begin(context);
        }
        const auto lock_clicked =
            nk_button_label(context, view->semantic_layer_target_locked ? "Unlock" : "Lock");
        if (!semantic_target_active) {
            nk_widget_disable_end(context);
        }
        if (lock_clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK;
        }
        if (!semantic_target_active) {
            nk_widget_disable_begin(context);
        }
        const auto clear_target = nk_button_label(context, "Clear target");
        if (!semantic_target_active) {
            nk_widget_disable_end(context);
        }
        if (clear_target && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER;
        }
    }
    nk_tree_pop(context);
    }

    worm_ui_nuklear_render_bms_branch_panel(context, view, action);
    nk_group_end(context);
        }
    }
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, editor_width, compose_body_height));
    if (nk_group_begin(context, "Compose editor", NK_WINDOW_NO_SCROLLBAR)) {
    const auto render_compose_editor = [&]() {

    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context,
             state.timing_stage_drag_active
                 ? "Move timing event · release to keep staged position · Apply to commit"
             : state.resource_pointer_mode ==
                       worm_ui_nuklear_compose_resource_pointer_mode_t::place
                 ? "Resource placement armed · click a chart lane · Esc disarms"
             : state.resource_pointer_mode ==
                       worm_ui_nuklear_compose_resource_pointer_mode_t::replace
                 ? "Resource replacement armed · click an existing object · Esc disarms"
             : timing_view->stage_active && timing_view->stage_existing_object_id != 0
                 ? "Drag outlined timing marker to resnap · edit/apply in side inspector"
             : view->semantic_layer_paint_mode_active
                 ? "Layer paint armed · click/drag toggles layer · Esc cancels"
             : view->semantic_layer_solo_active
                 ? "Layer solo active · only selected-layer notes shown"
             : view->note_paint_mode_active
                 ? "Paint armed · click/drag draws or erases · Esc cancels"
                 : "↑/↓ move · PgUp/PgDn page · drag selects/moves · Alt-drag hold · Del",
             NK_TEXT_LEFT);

    const auto group_bounds = nk_window_get_content_region(context);
    const auto surface_height = std::max(32.0f, group_bounds.h - 24.0f);
    nk_layout_space_begin(context, NK_DYNAMIC, surface_height, 1);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, context);
    nk_layout_space_end(context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f)
        return;

    auto* canvas = nk_window_get_canvas(context);
    if (!canvas)
        return;

    const bool pointer_inside = nk_input_is_mouse_hovering_rect(&context->input, viewport);
    const bool pointer_clicked =
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, viewport);
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool pointer_pressed_in_view = left_button.down && left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x <= viewport.x + viewport.w &&
                                         left_button.clicked_pos.y >= viewport.y &&
                                         left_button.clicked_pos.y <= viewport.y + viewport.h;
    if (pointer_clicked || pointer_pressed_in_view) {
        state.keyboard_focus = true;
    } else if (nk_input_any_mouse_click_in_rect(&context->input,
                                                nk_rect(0.0f, 0.0f, 1000000.0f, 1000000.0f)) &&
               !pointer_inside) {
        state.keyboard_focus = false;
        if (!state.pointer_navigation_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
        }
        if (state.space_range_hold_active) {
            append_compose_cancel_once(action);
            state.space_range_hold_active = false;
            state.space_range_hold_moved = false;
        }
    }
    process_compose_key_events(context, view, action, window_focused, ime_composition_active);

    const auto pointer_x = (static_cast<double>(context->input.mouse.pos.x) - viewport.x) *
                           view->viewport_width_pixels / viewport.w;
    const auto pointer_y = (static_cast<double>(context->input.mouse.pos.y) - viewport.y) *
                           view->viewport_height_pixels / viewport.h;
    const auto note_field_left = view->playable_field_left_pixels;
    const auto note_field_right = view->playable_field_right_pixels;
    const auto pointer_in_note_field =
        pointer_inside && pointer_x >= note_field_left && pointer_x < note_field_right;
    if (!state.action_overflowed && window_focused && view->scene_available) {
        action->compose_viewport_pointer_valid = true;
        action->compose_viewport_pointer_x_pixels = pointer_x;
        action->compose_viewport_pointer_y_pixels = pointer_y;
        if (state.middle_click_pending && state.middle_click_x >= viewport.x &&
            state.middle_click_x <= viewport.x + viewport.w && state.middle_click_y >= viewport.y &&
            state.middle_click_y <= viewport.y + viewport.h) {
            action->compose_viewport_middle_click = true;
            action->compose_viewport_middle_click_x_pixels =
                (static_cast<double>(state.middle_click_x) - viewport.x) *
                view->viewport_width_pixels / viewport.w;
            action->compose_viewport_middle_click_y_pixels =
                (static_cast<double>(state.middle_click_y) - viewport.y) *
                view->viewport_height_pixels / viewport.h;
        }
    }
    state.middle_click_pending = false;
    if (!state.action_overflowed && view->scene_available && window_focused) {
        for (size_t index = 0; index < state.wheel_event_count; ++index) {
            const auto& wheel = state.wheel_events[index];
            if (wheel.mouse_x < viewport.x || wheel.mouse_x > viewport.x + viewport.w ||
                wheel.mouse_y < viewport.y || wheel.mouse_y > viewport.y + viewport.h) {
                continue;
            }
            const auto zoom_modifier = (wheel.modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            action->compose_viewport_inputs[action->compose_viewport_input_count++] = {
                .kind = wheel.coarse_steps ? WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                                           : WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
                .zoom_modifier = zoom_modifier,
                .delta_x = wheel.delta_x,
                .delta_y = wheel.delta_y,
                .cursor_x_pixels =
                    (wheel.mouse_x - viewport.x) * view->viewport_width_pixels / viewport.w,
                .cursor_y_pixels =
                    (wheel.mouse_y - viewport.y) * view->viewport_height_pixels / viewport.h,
            };
        }
    }
    state.wheel_event_count = 0;
    const bool alt_down = nk_input_is_key_down(&context->input, NK_KEY_ALT) != 0;
    const bool left_down = context->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    const auto pointer_modifiers = static_cast<uint16_t>(SDL_GetModState());
    constexpr uint16_t any_pointer_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                              SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
    const auto pointer_started_in_view = left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x <= viewport.x + viewport.w &&
                                         left_button.clicked_pos.y >= viewport.y &&
                                         left_button.clicked_pos.y <= viewport.y + viewport.h;
    const auto pointer_started_x = (static_cast<double>(left_button.clicked_pos.x) - viewport.x) *
                                   view->viewport_width_pixels / viewport.w;
    const auto pointer_started_y = (static_cast<double>(left_button.clicked_pos.y) - viewport.y) *
                                   view->viewport_height_pixels / viewport.h;
    const auto pointer_started_in_note_field =
        pointer_started_in_view && pointer_started_x >= note_field_left &&
        pointer_started_x < note_field_right;
    const auto clamp_paint_x = [&](double value) {
        return std::clamp(value, note_field_left, note_field_right);
    };
    const auto clamp_paint_y = [&](double value) {
        return std::clamp(value, 0.0, view->viewport_height_pixels);
    };
    const auto paint_anchor_x =
        clamp_paint_x((static_cast<double>(left_button.clicked_pos.x) - viewport.x) *
                      view->viewport_width_pixels / viewport.w);
    const auto paint_anchor_y =
        clamp_paint_y((static_cast<double>(left_button.clicked_pos.y) - viewport.y) *
                      view->viewport_height_pixels / viewport.h);
    const auto paint_pointer_x = clamp_paint_x(pointer_x);
    const auto paint_pointer_y = clamp_paint_y(pointer_y);
    if (state.timing_stage_drag_active) {
        if (!window_focused || !view->scene_available || !timing_view->stage_active ||
            timing_view->stage_existing_object_id == 0 || (!left_down && !left_released)) {
            state.timing_stage_drag_active = false;
        } else if (left_released) {
            state.timing_stage_drag_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&context->input) &&
                   action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE) {
            const auto viewport_fraction =
                std::clamp(pointer_y / view->viewport_height_pixels, 0.0, 1.0);
            action->timing_stage_action = WORM_UI_TIMING_STAGE_MOVE;
            action->timing_chart_id = timing_view->chart_id;
            action->timing_document_revision = timing_view->document_revision;
            action->timing_object_id = timing_view->stage_existing_object_id;
            action->timing_lane = timing_view->stage_lane;
            action->timing_has_target_y = true;
            action->timing_target_y_fraction =
                view->timing_document_begin_fraction +
                viewport_fraction * (view->timing_document_end_fraction -
                                     view->timing_document_begin_fraction);
        }
    } else if (pointer_started_in_view && left_down && view->timing_stage_active &&
               timing_view->stage_active && timing_view->stage_existing_object_id != 0 &&
               shortcut_event_count == 0 && (pointer_modifiers & any_pointer_modifier) == 0) {
        const auto lane_index = static_cast<std::size_t>(view->timing_stage_lane);
        const auto lane_bit = static_cast<std::uint8_t>(1U << lane_index);
        const auto visible_lane_count =
            static_cast<std::size_t>(std::popcount(view->timing_lane_visibility_mask));
        if ((view->timing_lane_visibility_mask & lane_bit) != 0 && visible_lane_count != 0) {
            const auto lane_width =
                (note_field_left - 42.0) / static_cast<double>(visible_lane_count);
            const auto preceding_mask =
                static_cast<std::uint8_t>(view->timing_lane_visibility_mask & (lane_bit - 1U));
            const auto visible_slot = static_cast<std::size_t>(std::popcount(preceding_mask));
            const auto lane_left = 42.0 + lane_width * visible_slot;
            const auto y_tolerance = 8.0 * view->viewport_height_pixels / viewport.h;
            if (pointer_started_x >= lane_left && pointer_started_x < lane_left + lane_width &&
                std::abs(pointer_started_y - view->timing_stage_y_pixels) <= y_tolerance) {
                state.timing_stage_drag_active = true;
            }
        }
    }
    const auto compose_pointer_captured = state.note_paint_pointer_active ||
                                          state.semantic_layer_paint_pointer_active ||
                                          state.pointer_hold_active || state.lane_hold_active ||
                                          state.note_move_active || state.area_selection_active ||
                                          state.timing_stage_drag_active;
    action->compose_has_pointer_hover = !state.action_overflowed && window_focused &&
                                        view->scene_available && pointer_in_note_field &&
                                        !compose_pointer_captured;
    if (action->compose_has_pointer_hover) {
        action->compose_pointer_hover_x_pixels = pointer_x;
        action->compose_pointer_hover_y_pixels = pointer_y;
    }
    bool compose_note_double_click_in_view = false;
    if (!compose_pointer_captured && window_focused && view->scene_available) {
        for (size_t click = 0; click < double_click_count; ++click) {
            const auto& pointer = double_clicks[click];
            if (pointer.x < viewport.x || pointer.x >= viewport.x + viewport.w ||
                pointer.y < viewport.y || pointer.y >= viewport.y + viewport.h) {
                continue;
            }
            const auto click_x = (static_cast<double>(pointer.x) - viewport.x) *
                                 view->viewport_width_pixels / viewport.w;
            const auto click_y = (static_cast<double>(pointer.y) - viewport.y) *
                                 view->viewport_height_pixels / viewport.h;
            constexpr double measure_rail_width = 42.0;
            if (!timing_view->stage_active && click_x >= measure_rail_width &&
                click_x < note_field_left &&
                action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE) {
                const auto visible_lane_count = static_cast<std::size_t>(
                    std::popcount(view->timing_lane_visibility_mask));
                const auto lane_width = visible_lane_count == 0
                                            ? 0.0
                                            : (note_field_left - measure_rail_width) /
                                                  visible_lane_count;
                if (lane_width > 0.0 && visible_lane_count != 0) {
                    const auto visible_slot = std::min(
                        static_cast<std::size_t>((click_x - measure_rail_width) / lane_width),
                        visible_lane_count - 1);
                    std::size_t lane_index = 0;
                    for (std::size_t candidate = 0, slot = 0; candidate < 8; ++candidate) {
                        if ((view->timing_lane_visibility_mask & (1U << candidate)) == 0) {
                            continue;
                        }
                        if (slot++ == visible_slot) {
                            lane_index = candidate;
                            break;
                        }
                    }
                    uint64_t existing_object_id = 0;
                    auto nearest_distance = std::numeric_limits<double>::max();
                    const auto lane_left = measure_rail_width + lane_width * visible_slot;
                    const auto marker_inner_left = lane_left + 2.0;
                    const auto marker_inner_width = std::max(0.0, lane_width - 4.0);
                    const auto hit_tolerance = 7.0 * view->viewport_height_pixels / viewport.h;
                    for (size_t marker_index = 0; marker_index < view->timing_marker_count;
                         ++marker_index) {
                        const auto& marker = view->timing_markers[marker_index];
                        if (static_cast<std::size_t>(marker.lane) != lane_index) {
                            continue;
                        }
                        const auto marker_left =
                            marker_inner_left + marker_inner_width * marker.marker_left_fraction;
                        const auto marker_right =
                            marker_inner_left + marker_inner_width * marker.marker_right_fraction;
                        const auto distance = std::abs(click_y - marker.marker_y_pixels);
                        if (click_x >= marker_left && click_x <= marker_right &&
                            distance <= hit_tolerance && distance <= nearest_distance) {
                            existing_object_id = marker.object_id;
                            nearest_distance = distance;
                        }
                    }
                    action->timing_stage_action = WORM_UI_TIMING_STAGE_BEGIN;
                    action->timing_chart_id = timing_view->chart_id;
                    action->timing_document_revision = timing_view->document_revision;
                    action->timing_lane = static_cast<WormUiTimingLaneKind>(lane_index);
                    action->timing_object_id = existing_object_id;
                    if (existing_object_id == 0) {
                        action->timing_has_target_y = true;
                        const auto viewport_fraction =
                            std::clamp(click_y / view->viewport_height_pixels, 0.0, 1.0);
                        action->timing_target_y_fraction =
                            view->timing_document_begin_fraction +
                            viewport_fraction * (view->timing_document_end_fraction -
                                                 view->timing_document_begin_fraction);
                    }
                    continue;
                }
            }
            if (click_x < note_field_left || click_x >= note_field_right)
                continue;
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK, click_x,
                                 (static_cast<double>(pointer.y) - viewport.y) *
                                     view->viewport_height_pixels / viewport.h);
            compose_note_double_click_in_view = true;
        }
    }
    bool paint_pointer_consumed = state.note_paint_pointer_active ||
                                  state.semantic_layer_paint_pointer_active ||
                                  compose_action_has_cancel(action);
    if (!paint_pointer_consumed &&
        state.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
        view->scene_available && pointer_started_in_view && window_focused &&
        shortcut_event_count == 0 && (pointer_modifiers & any_pointer_modifier) == 0 &&
        !compose_note_double_click_in_view && !compose_pointer_captured &&
        !state.note_move_candidate_active && !state.area_selection_candidate_active &&
        !view->note_paint_mode_active && !view->semantic_layer_paint_mode_active) {
        const auto role =
            state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample
                ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                : state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bgm
                      ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                      : WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
        uint64_t target_lane_id = 0;
        bool in_placement_pane = false;
        if (role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) {
            for (size_t index = 0; index < view->bgm_lane_count; ++index) {
                const auto& lane = view->bgm_lanes[index];
                if (pointer_started_x >= lane.bounds.left &&
                    pointer_started_x < lane.bounds.right &&
                    pointer_started_y >= lane.bounds.top &&
                    pointer_started_y < lane.bounds.bottom) {
                    target_lane_id = lane.source_lane_id;
                    in_placement_pane = true;
                    break;
                }
            }
        } else {
            in_placement_pane = pointer_started_in_note_field;
        }
        if (in_placement_pane) {
            const auto replacing = state.resource_pointer_mode ==
                worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
            if (replacing && role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) {
                target_lane_id =
                    compose_bgm_event_at(view, pointer_started_x, pointer_started_y);
            }
            append_compose_input(
                action, replacing ? WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE
                                  : WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE,
                pointer_started_x, pointer_started_y, target_lane_id, false,
                WORM_UI_COMPOSE_SELECTION_REPLACE,
                WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
                state.armed_resource_id, role);
            paint_pointer_consumed = true;
        }
    }
    if (!state.action_overflowed && state.semantic_layer_paint_pointer_active) {
        if (!window_focused || !view->scene_available || !view->semantic_layer_paint_mode_active ||
            shortcut_event_count != 0 || (pointer_modifiers & any_pointer_modifier) != 0 ||
            (!left_down && !left_released)) {
            append_compose_cancel_once(action);
            state.semantic_layer_paint_pointer_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                 paint_pointer_x, paint_pointer_y);
            state.semantic_layer_paint_pointer_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
                                 paint_pointer_x, paint_pointer_y);
        }
    } else if (!state.action_overflowed && view->scene_available &&
               view->semantic_layer_paint_mode_active && pointer_started_in_note_field &&
               window_focused && shortcut_event_count == 0 &&
               (pointer_modifiers & any_pointer_modifier) == 0 && !state.pointer_hold_active &&
               !state.lane_hold_active && !state.area_selection_active &&
               !state.area_selection_candidate_active) {
        paint_pointer_consumed = true;
        if (compose_point_hits_note(view, paint_anchor_x, paint_anchor_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
                                 paint_anchor_x, paint_anchor_y);
            state.semantic_layer_paint_pointer_active = !state.action_overflowed && left_down;
            if (!state.action_overflowed && !left_down) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                     paint_pointer_x, paint_pointer_y);
            }
        }
    } else if (!state.action_overflowed && state.note_paint_pointer_active) {
        if (!window_focused || !view->scene_available || !view->note_paint_mode_active ||
            shortcut_event_count != 0 || (pointer_modifiers & any_pointer_modifier) != 0 ||
            (!left_down && !left_released)) {
            append_compose_cancel_once(action);
            state.note_paint_pointer_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, paint_pointer_x,
                                 paint_pointer_y);
            state.note_paint_pointer_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE, paint_pointer_x,
                                 paint_pointer_y);
        }
    } else if (!state.action_overflowed && view->scene_available && view->note_paint_mode_active &&
               pointer_started_in_note_field && window_focused && shortcut_event_count == 0 &&
               (pointer_modifiers & any_pointer_modifier) == 0 && !state.pointer_hold_active &&
               !state.lane_hold_active && !state.area_selection_active &&
               !state.area_selection_candidate_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, paint_anchor_x,
                             paint_anchor_y);
        paint_pointer_consumed = true;
        state.note_paint_pointer_active = !state.action_overflowed && left_down;
        if (!state.action_overflowed && !left_down) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, paint_pointer_x,
                                 paint_pointer_y);
        }
    }

    if (!state.action_overflowed && !paint_pointer_consumed && view->scene_available &&
        pointer_started_in_note_field && alt_down && !state.pointer_hold_active && window_focused &&
        !compose_note_double_click_in_view) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN, pointer_x, pointer_y);
        state.pointer_hold_active = !state.action_overflowed;
    } else if (!state.action_overflowed && !paint_pointer_consumed && state.pointer_hold_active) {
        if (!window_focused || !alt_down) {
            append_compose_cancel_once(action);
            state.pointer_hold_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE, pointer_x,
                                 pointer_y);
            state.pointer_hold_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE, pointer_x,
                                 pointer_y);
        }
    } else if (!state.action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_started_in_note_field && window_focused && !alt_down &&
               !state.note_move_active && !state.note_move_candidate_active &&
               !state.area_selection_active && !state.area_selection_candidate_active &&
               !compose_note_double_click_in_view) {
        if (compose_point_hits_selected_note(view, pointer_x, pointer_y) && left_down) {
            state.note_move_candidate_active = true;
            state.note_move_anchor_x = pointer_x;
            state.note_move_anchor_y = pointer_y;
            state.note_move_anchor_screen_x = context->input.mouse.pos.x;
            state.note_move_anchor_screen_y = context->input.mouse.pos.y;
        } else if (compose_point_hits_note(view, pointer_x, pointer_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
        } else if (left_down) {
            state.area_selection_candidate_active = true;
            state.area_selection_anchor_x = pointer_x;
            state.area_selection_anchor_y = pointer_y;
            state.area_selection_anchor_screen_x = context->input.mouse.pos.x;
            state.area_selection_anchor_screen_y = context->input.mouse.pos.y;
            state.area_selection_mode =
                compose_selection_mode(static_cast<uint16_t>(SDL_GetModState()));
        } else {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
        }
    } else if (!state.action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_clicked && pointer_in_note_field && window_focused &&
               !state.pointer_hold_active && !state.note_move_active &&
               !state.note_move_candidate_active && !state.area_selection_active &&
               !state.area_selection_candidate_active && !compose_note_double_click_in_view) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
    } else if (!state.action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_clicked && pointer_inside && pointer_x >= note_field_right &&
               window_focused && !state.pointer_hold_active && !state.note_move_active &&
               !state.note_move_candidate_active && !state.area_selection_active &&
               !state.area_selection_candidate_active) {
        const auto event_id = compose_bgm_event_at(view, pointer_x, pointer_y);
        if (event_id != 0) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK, pointer_x,
                                 pointer_y, 0, false, compose_selection_mode(pointer_modifiers),
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, event_id);
        }
    }

    if (!state.action_overflowed && !paint_pointer_consumed && state.note_move_candidate_active) {
        if (!window_focused || (!left_down && !left_released)) {
            state.note_move_candidate_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 state.note_move_anchor_x, state.note_move_anchor_y);
            state.note_move_candidate_active = false;
        } else if (left_down) {
            constexpr float drag_threshold_pixels = 4.0f;
            const auto dx = context->input.mouse.pos.x - state.note_move_anchor_screen_x;
            const auto dy = context->input.mouse.pos.y - state.note_move_anchor_screen_y;
            if (std::hypot(dx, dy) >= drag_threshold_pixels) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
                                     state.note_move_anchor_x, state.note_move_anchor_y);
                state.note_move_candidate_active = false;
                state.note_move_active = !state.action_overflowed;
                if (state.note_move_active) {
                    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, pointer_x,
                                         pointer_y);
                }
            }
        }
    } else if (!state.action_overflowed && !paint_pointer_consumed && state.note_move_active) {
        if (!window_focused || (!left_down && !left_released)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            state.note_move_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, pointer_x,
                                 pointer_y);
            state.note_move_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, pointer_x,
                                 pointer_y);
        }
    } else if (!state.action_overflowed && !paint_pointer_consumed &&
               state.area_selection_candidate_active) {
        if (!window_focused || (!left_down && !left_released)) {
            if (window_focused) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            }
            state.area_selection_candidate_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 state.area_selection_anchor_x, state.area_selection_anchor_y);
            state.area_selection_candidate_active = false;
        } else if (left_down) {
            constexpr float drag_threshold_pixels = 4.0f;
            const auto dx = context->input.mouse.pos.x - state.area_selection_anchor_screen_x;
            const auto dy = context->input.mouse.pos.y - state.area_selection_anchor_screen_y;
            if (std::hypot(dx, dy) >= drag_threshold_pixels) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                                     state.area_selection_anchor_x, state.area_selection_anchor_y,
                                     0, false, state.area_selection_mode);
                state.area_selection_candidate_active = false;
                state.area_selection_active = !state.action_overflowed;
                if (state.area_selection_active) {
                    append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                                         pointer_x, pointer_y);
                }
            }
        }
    } else if (!state.action_overflowed && !paint_pointer_consumed && state.area_selection_active) {
        if (!window_focused || (!left_down && !left_released)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            state.area_selection_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE, pointer_x,
                                 pointer_y);
            state.area_selection_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE, pointer_x,
                                 pointer_y);
        }
    }

    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    nk_fill_rect(canvas, viewport, 0.0f, nk_rgb(22, 25, 33));
    if (!view->scene_available) {
        const auto* status = view->status_text ? view->status_text : "Chart field is unavailable.";
        nk_draw_text(canvas, viewport, status, static_cast<int>(strlen(status)),
                     context->style.font, nk_rgb(22, 25, 33), nk_rgb(190, 196, 207));
        nk_push_scissor(canvas, parent_clip);
    } else {
        const auto rail_width = static_cast<float>(42.0 * viewport.w / view->viewport_width_pixels);
        const auto rail = nk_rect(viewport.x, viewport.y, rail_width, viewport.h);
        nk_fill_rect(canvas, rail, 0.0f, nk_rgb(18, 21, 29));
        nk_stroke_line(canvas, rail.x + rail.w, rail.y, rail.x + rail.w, rail.y + rail.h, 1.0f,
                       nk_rgb(80, 91, 112));
        constexpr std::array<const char*, 8> timing_labels{"BPM", "SV", "SPD", "STP",
                                                           "DLY", "WRP", "KIA", "ANC"};
        const std::array<struct nk_color, 8> timing_colors{
            nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218),
            nk_rgb(224, 132, 119), nk_rgb(207, 153, 104), nk_rgb(176, 142, 218),
            nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};
        const auto timing_left = rail.x + rail.w;
        const auto field_left =
            viewport.x + static_cast<float>(view->playable_field_left_pixels) * viewport.w /
                             static_cast<float>(view->viewport_width_pixels);
        const auto timing_width = std::max(0.0f, field_left - timing_left);
        const auto visible_timing_lane_count =
            static_cast<std::size_t>(std::popcount(view->timing_lane_visibility_mask));
        const auto timing_lane_width = visible_timing_lane_count == 0
                                           ? 0.0f
                                           : timing_width /
                                                 static_cast<float>(visible_timing_lane_count);
        std::array<struct nk_rect, 8> timing_lanes{};
        std::size_t visible_timing_lane = 0;
        for (size_t index = 0; index < timing_lanes.size(); ++index) {
            if ((view->timing_lane_visibility_mask & (1U << index)) == 0) {
                continue;
            }
            timing_lanes[index] =
                nk_rect(timing_left +
                            timing_lane_width * static_cast<float>(visible_timing_lane++),
                        viewport.y, timing_lane_width, viewport.h);
            nk_fill_rect(canvas, timing_lanes[index], 0.0f,
                         (index & 1U) != 0 ? nk_rgb(25, 29, 38) : nk_rgb(28, 32, 42));
            nk_stroke_line(canvas, timing_lanes[index].x, viewport.y, timing_lanes[index].x,
                           viewport.y + viewport.h, 1.0f, nk_rgb(58, 67, 83));
            if (timing_lane_width >= 17.0f) {
                const auto title = nk_rect(timing_lanes[index].x + 2.0f, viewport.y + 2.0f,
                                           std::max(1.0f, timing_lane_width - 4.0f), 14.0f);
                nk_draw_text(canvas, title, timing_labels[index],
                             static_cast<int>(strlen(timing_labels[index])), context->style.font,
                             nk_rgba(18, 21, 29, 215), timing_colors[index]);
            }
        }
        for (size_t band_index = 0; band_index < view->band_count; ++band_index) {
            const auto& band = view->bands[band_index];
            const auto color = band.split_stage ? nk_rgb(27, 31, 43) : nk_rgb(25, 29, 39);
            compose_fill_rect(canvas, &band.bounds, view, viewport, color);
            const auto top = viewport.y + static_cast<float>(band.bounds.top) * viewport.h /
                                              static_cast<float>(view->viewport_height_pixels);
            const auto bottom = viewport.y + static_cast<float>(band.bounds.bottom) * viewport.h /
                                                 static_cast<float>(view->viewport_height_pixels);
            nk_stroke_line(canvas, viewport.x, top, viewport.x + viewport.w, top, 1.0f,
                           nk_rgb(80, 91, 112));
            nk_stroke_line(canvas, viewport.x, bottom, viewport.x + viewport.w, bottom, 1.0f,
                           nk_rgb(61, 70, 87));
        }
        for (size_t lane_index = 0; lane_index < view->lane_count; ++lane_index) {
            const auto& lane = view->lanes[lane_index];
            const auto rect = compose_screen_rect(&lane.bounds, view, viewport);
            if (rect.w <= 0.0f || rect.h <= 0.0f)
                continue;
            nk_fill_rect(canvas, rect, 0.0f, compose_lane_color(lane.role, lane.display_order));
            nk_stroke_line(canvas, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                           nk_rgb(62, 70, 86));
            if (rect.w >= 28.0f && rect.h >= 18.0f) {
                char label[16]{};
                const auto label_length =
                    snprintf(label, sizeof(label), "%u", lane.display_order + 1U);
                if (label_length > 0) {
                    const auto label_bounds = nk_rect(rect.x + 2.0f, rect.y + 2.0f,
                                                      std::min(28.0f, rect.w - 4.0f), 15.0f);
                    nk_draw_text(canvas, label_bounds, label, static_cast<int>(strlen(label)),
                                 context->style.font, nk_rgb(37, 43, 55), nk_rgb(122, 133, 153));
                }
            }
        }
        for (size_t lane_index = 0; lane_index < view->bgm_lane_count; ++lane_index) {
            const auto& lane = view->bgm_lanes[lane_index];
            const auto rect = compose_screen_rect(&lane.bounds, view, viewport);
            if (rect.w <= 0.0f || rect.h <= 0.0f)
                continue;
            const auto alternate = (lane.source_order & 1U) != 0;
            nk_fill_rect(canvas, rect, 0.0f, alternate ? nk_rgb(43, 38, 52) : nk_rgb(38, 34, 47));
            nk_stroke_line(canvas, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                           nk_rgb(79, 67, 91));
        }
        for (size_t index = 0; index < view->difference_count; ++index) {
            compose_fill_rect(canvas, &view->differences[index].bounds, view, viewport,
                              nk_rgba(229, 190, 58, 74));
        }
        for (size_t index = 0; index < view->grid_line_count; ++index) {
            const auto& line = view->grid_lines[index];
            if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
                line.y_pixels > view->viewport_height_pixels)
                continue;
            const auto y = viewport.y + static_cast<float>(line.y_pixels) * viewport.h /
                                            static_cast<float>(view->viewport_height_pixels);
            struct nk_color color = nk_rgb(42, 49, 63);
            float thickness = 0.7f;
            if (line.kind == 3) {
                color = nk_rgb(95, 107, 128);
                thickness = 1.2f;
            } else if (line.kind == 1 || line.kind == 2) {
                color = nk_rgb(60, 70, 88);
                thickness = line.emphasis >= 2 ? 1.1f : 0.9f;
            } else if (line.emphasis >= 2) {
                color = nk_rgb(52, 61, 78);
            }
            nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, thickness, color);
            if (line.measure_number != 0 && y + 15.0f <= viewport.y + viewport.h) {
                char label[20]{};
                const auto length = snprintf(label, sizeof(label), "M%u", line.measure_number);
                if (length > 0) {
                    const auto label_bounds =
                        nk_rect(rail.x + 3.0f, y + 2.0f, std::max(1.0f, rail.w - 6.0f), 14.0f);
                    nk_draw_text(canvas, label_bounds, label, static_cast<int>(strlen(label)),
                                 context->style.font, nk_rgb(18, 21, 29), nk_rgb(174, 185, 203));
                }
            }
        }
        for (size_t index = 0; index < view->timing_marker_count; ++index) {
            const auto& marker = view->timing_markers[index];
            const auto lane_index = static_cast<size_t>(marker.lane);
            if (lane_index >= timing_lanes.size() || timing_lanes[lane_index].w <= 0.0f) {
                continue;
            }
            const auto& lane = timing_lanes[lane_index];
            const auto inner_left = lane.x + 2.0f;
            const auto inner_width = std::max(0.0f, lane.w - 4.0f);
            const auto left =
                inner_left + inner_width * static_cast<float>(marker.marker_left_fraction);
            const auto right =
                inner_left + inner_width * static_cast<float>(marker.marker_right_fraction);
            const auto y = viewport.y + static_cast<float>(marker.marker_y_pixels) * viewport.h /
                                            static_cast<float>(view->viewport_height_pixels);
            nk_fill_rect(canvas, nk_rect(left, y - 2.5f, std::max(1.0f, right - left), 5.0f),
                         0.0f, timing_colors[lane_index]);
        }
        if (view->timing_stage_active) {
            const auto lane_index = static_cast<size_t>(view->timing_stage_lane);
            if (lane_index < timing_lanes.size()) {
                const auto& lane = timing_lanes[lane_index];
                const auto y = viewport.y +
                               static_cast<float>(view->timing_stage_y_pixels) * viewport.h /
                                   static_cast<float>(view->viewport_height_pixels);
                const auto bounds = nk_rect(lane.x + 2.0f, y - 5.0f,
                                            std::max(1.0f, lane.w - 4.0f), 10.0f);
                const auto color = view->timing_stage_valid ? nk_rgb(255, 221, 108)
                                                            : nk_rgb(238, 96, 88);
                nk_fill_rect(canvas, bounds, 1.0f, nk_rgba(color.r, color.g, color.b, 105));
                nk_stroke_rect(canvas, bounds, 1.0f, 2.0f, color);
            }
        }
        const auto projected_screen_y = [&](double y_pixels) {
            return viewport.y + static_cast<float>(y_pixels) * viewport.h /
                                    static_cast<float>(view->viewport_height_pixels);
        };
        for (size_t index = 0; index < view->audio_section_count; ++index) {
            const auto& section = view->audio_sections[index];
            if (section.span_projection_visible) {
                const auto top = projected_screen_y(section.projected_span_top_y_pixels);
                const auto bottom = projected_screen_y(section.projected_span_bottom_y_pixels);
                nk_fill_rect(canvas,
                             nk_rect(viewport.x, top, viewport.w, std::max(1.0f, bottom - top)),
                             0.0f, nk_rgba(188, 111, 235, 24));
            }
            if (section.begin_projection_visible) {
                const auto y = projected_screen_y(section.projected_begin_y_pixels);
                nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, 1.1f,
                               nk_rgb(205, 139, 242));
            }
            if (section.end_projection_visible) {
                const auto y = projected_screen_y(section.projected_end_y_pixels);
                nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, 1.1f,
                               nk_rgb(205, 139, 242));
            }
        }
        for (size_t index = 0; index < view->audio_bookmark_count; ++index) {
            const auto& bookmark = view->audio_bookmarks[index];
            if (!bookmark.projection_visible) {
                continue;
            }
            const auto y = projected_screen_y(bookmark.projected_y_pixels);
            nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, 1.4f,
                           nk_rgb(232, 166, 255));
        }
        for (size_t index = 0; index < view->hold_body_count; ++index) {
            const auto& hold = view->hold_bodies[index];
            const auto rect = compose_screen_rect(&hold.bounds, view, viewport);
            if (rect.w <= 0.0f || rect.h <= 0.0f) {
                continue;
            }
            if (hold.proposal_ghost) {
                nk_fill_rect(canvas, rect, 0.0f, nk_rgba(240, 171, 71, 74));
                nk_stroke_rect(canvas, rect, 0.0f, 1.4f, nk_rgb(255, 224, 156));
            } else {
                const auto color =
                    hold.proposal_origin
                        ? nk_rgb(51, 82, 100)
                        : (hold.semantic_color_visible
                               ? nk_rgba(hold.semantic_red, hold.semantic_green, hold.semantic_blue,
                                         hold.semantic_alpha)
                               : (hold.selected ? nk_rgb(62, 170, 204) : nk_rgb(48, 111, 151)));
                nk_fill_rect(canvas, rect, 0.0f, color);
                if (hold.selected) {
                    nk_stroke_rect(canvas, rect, 0.0f, 1.6f, nk_rgb(220, 247, 255));
                }
                if (view->hover_note_visible && hold.note_id == view->hover_note_id) {
                    nk_stroke_rect(canvas, rect, 0.0f, 2.0f, nk_rgb(255, 235, 148));
                }
            }
        }
        for (size_t index = 0; index < view->bgm_event_count; ++index) {
            const auto& event = view->bgm_events[index];
            const auto rect = compose_screen_rect(&event.bounds, view, viewport);
            if (rect.w <= 0.0f || rect.h <= 0.0f)
                continue;
            const auto fill = event.has_sound ? nk_rgb(177, 116, 207) : nk_rgb(102, 88, 113);
            nk_fill_rect(canvas, rect, 1.5f, fill);
            nk_stroke_rect(canvas, rect, 1.5f, event.selected ? 2.0f : 1.0f,
                           event.selected ? nk_rgb(248, 224, 255) : nk_rgb(50, 35, 58));
        }
        for (size_t index = 0; index < view->bgm_lane_count; ++index) {
            const auto rect = compose_screen_rect(&view->bgm_lanes[index].bounds, view, viewport);
            if (rect.w < 18.0f || rect.h <= 0.0f) {
                continue;
            }
            char label[20]{};
            const auto length = snprintf(label, sizeof(label), "BGM %zu", index + 1);
            if (length > 0) {
                const auto title = nk_rect(rect.x + 2.0f, viewport.y + 2.0f,
                                           std::max(1.0f, rect.w - 4.0f), 14.0f);
                nk_draw_text(canvas, title, label, static_cast<int>(strlen(label)),
                             context->style.font, nk_rgba(31, 25, 38, 215),
                             nk_rgb(211, 175, 226));
            }
        }
        for (size_t index = 0; index < view->note_endpoint_count; ++index) {
            const auto& note = view->note_endpoints[index];
            auto color =
                note.proposal_origin
                    ? nk_rgb(73, 101, 120)
                    : (note.semantic_color_visible
                           ? nk_rgba(note.semantic_red, note.semantic_green, note.semantic_blue,
                                     note.semantic_alpha)
                           : (note.selected ? nk_rgb(111, 221, 236) : nk_rgb(133, 182, 222)));
            auto border = nk_rgb(17, 24, 34);
            auto border_width = 1.0f;
            if (note.proposal_ghost) {
                color = nk_rgba(240, 171, 71, 132);
                border = nk_rgb(255, 224, 156);
                border_width = 2.0f;
            } else if (view->hover_note_visible && note.note_id == view->hover_note_id) {
                color = nk_rgb(255, 226, 127);
                border = nk_rgb(255, 247, 190);
                border_width = 2.0f;
            } else if (note.selected && note.semantic_color_visible) {
                border = nk_rgb(220, 247, 255);
                border_width = 2.0f;
            } else if (note.paint_invalid_preview) {
                color = nk_rgba(245, 178, 65, 210);
                border = nk_rgb(255, 229, 166);
                border_width = 2.0f;
            } else if (note.paint_erase_preview) {
                color = nk_rgba(244, 94, 111, 210);
                border = nk_rgb(255, 184, 190);
                border_width = 2.0f;
            } else if (note.paint_preview) {
                color = nk_rgba(81, 218, 145, 210);
                border = nk_rgb(185, 255, 216);
                border_width = 2.0f;
            } else if (note.kind == 4) {
                color = nk_rgb(214, 91, 104);
            } else if (note.kind == 5) {
                color = nk_rgb(236, 194, 112);
            } else if (note.tail) {
                color = nk_rgb(113, 201, 216);
            }
            const auto rect = compose_screen_rect(&note.bounds, view, viewport);
            if (rect.w > 0.0f && rect.h > 0.0f) {
                const auto tile = nk_rect(rect.x + 1.0f, rect.y, std::max(1.0f, rect.w - 2.0f),
                                          std::max(2.0f, rect.h));
                nk_fill_rect(canvas, tile, 0.0f, color);
                nk_stroke_rect(canvas, tile, 0.0f, border_width, border);
                nk_stroke_line(canvas, tile.x + 1.0f, tile.y + 1.0f,
                               tile.x + tile.w - 1.0f, tile.y + 1.0f, 1.0f,
                               nk_rgba(255, 255, 255, 105));
                if (note.glyph >= 2 && note.glyph <= 5 && tile.w >= 10.0f && tile.h >= 6.0f) {
                    const auto center_x = tile.x + tile.w * 0.5f;
                    const auto center_y = tile.y + tile.h * 0.5f;
                    const auto extent = std::max(2.0f, std::min(tile.w, tile.h) * 0.3f);
                    auto first_x = center_x + extent;
                    auto first_y = center_y - extent;
                    auto tip_x = center_x - extent;
                    auto tip_y = center_y;
                    auto last_x = center_x + extent;
                    auto last_y = center_y + extent;
                    if (note.glyph == 3) {
                        first_x = center_x - extent;
                        first_y = center_y - extent;
                        tip_x = center_x;
                        tip_y = center_y + extent;
                        last_x = center_x + extent;
                        last_y = center_y - extent;
                    } else if (note.glyph == 4) {
                        first_x = center_x - extent;
                        first_y = center_y + extent;
                        tip_x = center_x;
                        tip_y = center_y - extent;
                        last_x = center_x + extent;
                        last_y = center_y + extent;
                    } else if (note.glyph == 5) {
                        first_x = center_x - extent;
                        first_y = center_y - extent;
                        tip_x = center_x + extent;
                        tip_y = center_y;
                        last_x = center_x - extent;
                        last_y = center_y + extent;
                    }
                    const auto arrow_color = nk_rgb(245, 248, 252);
                    nk_stroke_line(canvas, first_x, first_y, tip_x, tip_y, 2.0f, arrow_color);
                    nk_stroke_line(canvas, tip_x, tip_y, last_x, last_y, 2.0f, arrow_color);
                }
            }
        }
        if (view->playhead_visible) {
            const auto y = viewport.y + static_cast<float>(view->playhead_y_pixels) * viewport.h /
                                            static_cast<float>(view->viewport_height_pixels);
            nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, 2.0f,
                           nk_rgb(93, 231, 190));
        }
        if (view->cursor) {
            const auto rect = compose_screen_rect(&view->cursor->bounds, view, viewport);
            if (rect.w > 0.0f && rect.h > 0.0f) {
                const auto head_y = rect.y + rect.h * 0.5f;
                nk_stroke_line(canvas, viewport.x, head_y, viewport.x + viewport.w, head_y, 1.4f,
                               nk_rgb(255, 223, 108));
                nk_fill_rect(canvas, rect, 0.0f, nk_rgb(248, 226, 115));
                nk_stroke_line(canvas, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                               nk_rgb(255, 243, 174));
                nk_stroke_line(canvas, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h,
                               1.0f, nk_rgb(255, 243, 174));
            }
        }
        const auto draw_selection_preview = [&](const WormUiComposeRect& bounds,
                                                WormUiComposeSelectionMode mode) {
            const auto rect = compose_screen_rect(&bounds, view, viewport);
            if (rect.w <= 0.0f || rect.h <= 0.0f) {
                return;
            }
            struct nk_color fill{};
            struct nk_color border{};
            if (mode == WORM_UI_COMPOSE_SELECTION_ADD) {
                fill = nk_rgba(113, 214, 164, 34);
                border = nk_rgb(113, 214, 164);
            } else if (mode == WORM_UI_COMPOSE_SELECTION_TOGGLE) {
                fill = nk_rgba(197, 151, 245, 34);
                border = nk_rgb(197, 151, 245);
            } else {
                fill = nk_rgba(101, 183, 242, 34);
                border = nk_rgb(101, 183, 242);
            }
            nk_fill_rect(canvas, rect, 0.0f, fill);
            nk_stroke_rect(canvas, rect, 0.0f, 1.2f, border);
        };
        if (view->space_range_preview_visible) {
            draw_selection_preview(view->space_range_preview_bounds,
                                   view->space_range_preview_mode);
            const auto anchor_bright = (SDL_GetTicks() / 350U) % 2U == 0U;
            const auto draw_space_range_row = [&](double y_pixels, struct nk_color color) {
                const auto y = viewport.y + static_cast<float>(y_pixels) * viewport.h /
                                                static_cast<float>(view->viewport_height_pixels);
                nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, 2.4f, color);
            };
            if (std::abs(view->space_range_anchor_y_pixels - view->space_range_tail_y_pixels) <
                0.01) {
                draw_space_range_row(view->space_range_anchor_y_pixels,
                                     anchor_bright ? nk_rgb(255, 224, 122) : nk_rgb(185, 142, 72));
            } else {
                draw_space_range_row(view->space_range_anchor_y_pixels,
                                     anchor_bright ? nk_rgb(255, 224, 122) : nk_rgb(185, 142, 72));
                draw_space_range_row(view->space_range_tail_y_pixels,
                                     anchor_bright ? nk_rgb(97, 201, 255) : nk_rgb(63, 125, 176));
            }
        }
        if (view->area_selection_preview_visible) {
            draw_selection_preview(view->area_selection_preview_bounds,
                                   view->area_selection_preview_mode);
        }
        if (view->viewport_autoscroll_active &&
            std::isfinite(view->viewport_autoscroll_anchor_x_pixels) &&
            std::isfinite(view->viewport_autoscroll_anchor_y_pixels)) {
            const auto anchor_x =
                viewport.x +
                static_cast<float>(std::clamp(view->viewport_autoscroll_anchor_x_pixels, 0.0,
                                              view->viewport_width_pixels) *
                                   viewport.w / view->viewport_width_pixels);
            const auto anchor_y =
                viewport.y +
                static_cast<float>(std::clamp(view->viewport_autoscroll_anchor_y_pixels, 0.0,
                                              view->viewport_height_pixels) *
                                   viewport.h / view->viewport_height_pixels);
            nk_stroke_line(canvas, anchor_x - 9.0f, anchor_y, anchor_x + 9.0f, anchor_y, 2.0f,
                           nk_rgb(255, 211, 103));
            nk_stroke_line(canvas, anchor_x, anchor_y - 9.0f, anchor_x, anchor_y + 9.0f, 2.0f,
                           nk_rgb(255, 211, 103));
            const auto indicator =
                nk_rect(viewport.x + 8.0f, viewport.y + viewport.h - 26.0f,
                        std::min(250.0f, std::max(150.0f, viewport.w - 16.0f)), 18.0f);
            nk_fill_rect(canvas, indicator, 3.0f, nk_rgba(12, 16, 24, 235));
            constexpr const char* indicator_text = "Auto-scroll latched - middle-click to cancel";
            nk_draw_text(canvas, indicator, indicator_text,
                         static_cast<int>(strlen(indicator_text)), context->style.font,
                         nk_rgb(12, 16, 24), nk_rgb(255, 231, 157));
        }
        if (view->hover_note_visible && view->hover_tooltip && view->hover_tooltip[0] != '\0') {
            const auto tooltip_width = std::min(420.0f, std::max(120.0f, viewport.w - 16.0f));
            const auto tooltip_bounds =
                nk_rect(viewport.x + 8.0f, viewport.y + 8.0f, tooltip_width, 24.0f);
            nk_fill_rect(canvas, tooltip_bounds, 3.0f, nk_rgba(12, 16, 24, 235));
            nk_stroke_rect(canvas, tooltip_bounds, 3.0f, 1.0f, nk_rgb(255, 235, 148));
            nk_draw_text(canvas, tooltip_bounds, view->hover_tooltip,
                         static_cast<int>(strlen(view->hover_tooltip)), context->style.font,
                         nk_rgb(12, 16, 24), nk_rgb(255, 244, 192));
        }
    }
    nk_push_scissor(canvas, parent_clip);
    };
    render_compose_editor();
    nk_group_end(context);
    }
    nk_layout_space_end(context);
}

bool worm_ui_nuklear_compose_lane_focused(const WormUiComposeView* view, bool compose_tab_active,
                                          bool window_focused) {
    return view && window_focused && compose_tab_active && state.keyboard_focus && view->visible &&
           view->scene_available && view->cursor && view->cursor->lane_id != 0;
}
