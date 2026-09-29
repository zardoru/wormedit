#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"
#include "sys/compose_scroll_geometry.h"
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

using wormedit::ui::lucide::Glyph;

static auto& state = worm_ui_nuklear_compose_state();
static constexpr double compose_measure_rail_width = 42.0;

static auto compose_inspector_icon(bool visible) -> Glyph {
    return visible ? Glyph::panel_right_close : Glyph::panel_right_open;
}

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

void worm_ui_nuklear_reset_compose_panel() {
    worm_ui_nuklear_reset_compose_markers();
    worm_ui_nuklear_reset_compose_state();
}

static void append_compose_input(
    WormUiWorkspaceAction* action, WormUiComposeInputKind kind, double x_pixels = 0.0,
    double y_pixels = 0.0, uint64_t lane_id = 0, bool successive = false,
    WormUiComposeSelectionMode selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
    WormUiComposeNavigationAction navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
    uint64_t object_id = 0, uint8_t resource_role = 0,
    bool razor_stroke_start = false,
    WormUiTimingLaneKind timing_lane = WORM_UI_TIMING_LANE_BPM,
    uint64_t timing_chart_id = 0, uint64_t timing_document_revision = 0,
    uint64_t timing_anchor_chart_row = 0) {
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
        state.beat_fit_drag_active = false;
        state.area_selection_candidate_active = false;
        state.area_selection_active = false;
        state.navigation_key_active = false;
        state.navigation_keycode = 0;
        state.pointer_navigation_active = false;
        state.keyboard_focus = false;
        state.lane_hold_count = 0;
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
    };
}

static void append_bgm_horizontal_scroll(WormUiWorkspaceAction* action,
                                         const WormUiComposeView* view, double delta_pixels) {
    if (!action || !view || !std::isfinite(delta_pixels) || delta_pixels == 0.0 ||
        action->compose_viewport_input_count >= WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY ||
        view->bgm_maximum_horizontal_scroll_pixels <= 0.0) return;
    action->compose_viewport_inputs[action->compose_viewport_input_count++] = {
        .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
        .zoom_modifier = false,
        .delta_x = delta_pixels,
        .delta_y = 0.0,
        .cursor_x_pixels = (view->playable_field_right_pixels + view->viewport_width_pixels) * 0.5,
        .cursor_y_pixels = view->viewport_height_pixels * 0.5,
    };
}

static void cancel_beat_fit_drag(WormUiWorkspaceAction* action) {
    if (state.beat_fit_drag_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
        state.beat_fit_drag_active = false;
        state.beat_fit_anchor_screen_y = 0.0f;
    }
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
        state.bmson_razor_pointer_active = false;
        reset_bmson_marker_move_capture();
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
        state.lane_hold_count = 0;
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
        state.bmson_razor_pointer_active = false;
        reset_bmson_marker_move_capture();
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
    reset_timing_marker_drag_capture();
    state.inline_timing_open = false;
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

struct ComposePaintContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    struct nk_command_buffer* canvas;
    struct nk_rect viewport;
    struct nk_rect rail;
};

static void draw_compose_grid_tick(const ComposePaintContext& paint,
                                   const WormUiComposeGridLineView& line, float y) {
    const auto& view = *paint.view;
    if (view.waveform_row_count != 0 && !view.waveform_timing_locked && line.chart_row != 0 &&
        (line.kind == 1 || line.kind == 3)) {
        nk_fill_rect(paint.canvas,
                     nk_rect(paint.rail.x + paint.rail.w - 13.0f, y - 3.0f, 7.0f, 6.0f), 1.0f,
                     line.chart_row == view.beat_fit_chart_row && view.beat_fit_drag_active
                         ? nk_rgb(255, 207, 104)
                         : nk_rgb(165, 177, 195));
    }
    if (line.measure_number == 0 || y + 15.0f > paint.viewport.y + paint.viewport.h) {
        return;
    }
    char label[20]{};
    const auto length = view.bms_measure_labels
                            ? snprintf(label, sizeof(label), "#%03u", line.measure_number - 1)
                            : snprintf(label, sizeof(label), "M%u", line.measure_number);
    if (length <= 0) {
        return;
    }
    const auto label_bounds = nk_rect(paint.rail.x + 3.0f, y + 2.0f,
                                      std::max(1.0f, paint.rail.w - 6.0f), 14.0f);
    if (view.selected_bms_measure_number_plus_one == line.measure_number) {
        nk_fill_rect(paint.canvas, label_bounds, 2.0f, nk_rgb(67, 93, 135));
    }
    nk_draw_text(paint.canvas, label_bounds, label, static_cast<int>(strlen(label)),
                 paint.context->style.font, nk_rgb(18, 21, 29), nk_rgb(174, 185, 203));
}

static void draw_compose_grid_line(const ComposePaintContext& paint,
                                   const WormUiComposeGridLineView& line) {
    const auto& view = *paint.view;
    if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
        line.y_pixels > view.viewport_height_pixels)
        return;
    const auto y = paint.viewport.y + static_cast<float>(line.y_pixels) * paint.viewport.h /
                                          static_cast<float>(view.viewport_height_pixels);
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
    nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                   thickness, color);
    draw_compose_grid_tick(paint, line, y);
}

static void draw_compose_grid_lines(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->grid_line_count; ++index) {
        draw_compose_grid_line(paint, paint.view->grid_lines[index]);
    }
}

static float compose_projected_y(const ComposePaintContext& paint, double y_pixels) {
    return paint.viewport.y + static_cast<float>(y_pixels) * paint.viewport.h /
                                    static_cast<float>(paint.view->viewport_height_pixels);
}

static void draw_compose_beat_sections(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->beat_section_count; ++index) {
        const auto& section = paint.view->beat_sections[index];
        if (section.proposed_span_projection_visible) {
            const auto top = compose_projected_y(paint, section.proposed_span_top_y_pixels);
            const auto bottom = compose_projected_y(paint, section.proposed_span_bottom_y_pixels);
            nk_fill_rect(paint.canvas,
                         nk_rect(paint.viewport.x, top, paint.viewport.w,
                                 std::max(1.0f, bottom - top)),
                         0.0f, nk_rgba(255, 207, 104, 25));
        }
        if (section.span_projection_visible) {
            const auto top = compose_projected_y(paint, section.projected_span_top_y_pixels);
            const auto bottom = compose_projected_y(paint, section.projected_span_bottom_y_pixels);
            nk_fill_rect(paint.canvas,
                         nk_rect(paint.viewport.x, top, paint.viewport.w,
                                 std::max(1.0f, bottom - top)),
                         0.0f, nk_rgba(87, 198, 177, 20));
        }
        if (section.begin_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_begin_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.0f, nk_rgba(98, 206, 185, 160));
        }
        if (section.end_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_end_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.0f, nk_rgba(98, 206, 185, 160));
        }
        if (section.proposed_begin_projection_visible) {
            const auto y = compose_projected_y(paint, section.proposed_begin_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.4f, nk_rgba(255, 207, 104, 185));
        }
        if (section.proposed_end_projection_visible) {
            const auto y = compose_projected_y(paint, section.proposed_end_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.4f, nk_rgba(255, 207, 104, 185));
        }
    }
}

static void draw_compose_beat_bookmarks(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->beat_bookmark_count; ++index) {
        const auto& bookmark = paint.view->beat_bookmarks[index];
        if (bookmark.projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.3f, nk_rgb(111, 224, 201));
        }
        if (bookmark.proposed_projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.proposed_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.6f, nk_rgb(255, 218, 139));
        }
    }
}

static void draw_compose_audio_sections(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->audio_section_count; ++index) {
        const auto& section = paint.view->audio_sections[index];
        if (section.span_projection_visible) {
            const auto top = compose_projected_y(paint, section.projected_span_top_y_pixels);
            const auto bottom = compose_projected_y(paint, section.projected_span_bottom_y_pixels);
            nk_fill_rect(paint.canvas,
                         nk_rect(paint.viewport.x, top, paint.viewport.w,
                                 std::max(1.0f, bottom - top)),
                         0.0f, nk_rgba(188, 111, 235, 24));
        }
        if (section.begin_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_begin_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.1f, nk_rgb(205, 139, 242));
        }
        if (section.end_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_end_y_pixels);
            nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.1f, nk_rgb(205, 139, 242));
        }
    }
}

static void draw_compose_audio_bookmarks(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->audio_bookmark_count; ++index) {
        const auto& bookmark = paint.view->audio_bookmarks[index];
        if (!bookmark.projection_visible) {
            continue;
        }
        const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
        nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       1.4f, nk_rgb(232, 166, 255));
    }
}

static struct nk_color compose_lane_color(uint8_t role, size_t display_order) {
    const auto alternate = (display_order & 1U) != 0;
    switch (role) {
    case 1: // scratch
        return wormedit::ui::theme::compose_lane(0, alternate);
    case 5: // free zone
        return wormedit::ui::theme::compose_lane(1, alternate);
    case 6: // foot pedal
        return wormedit::ui::theme::compose_lane(2, alternate);
    default:
        return wormedit::ui::theme::compose_lane(3, alternate);
    }
}

struct ComposeTimingLaneLayout {
    std::array<struct nk_rect, WORM_UI_TIMING_LANE_COUNT> lanes{};
    std::array<struct nk_color, WORM_UI_TIMING_LANE_COUNT> colors{};
    float field_left{};
};

static auto compose_timing_marker_document_rect(const WormUiComposeView& view,
                                                const WormUiTimingMarkerView& marker)
    -> WormUiComposeRect {
    const auto marker_lane_ordinal = static_cast<unsigned>(marker.lane);
    if (marker_lane_ordinal >= WORM_UI_TIMING_LANE_COUNT) return {};
    const auto bit = static_cast<std::uint8_t>(1U << marker_lane_ordinal);
    const auto visible_count = std::popcount(view.timing_lane_visibility_mask);
    if (visible_count == 0 || (view.timing_lane_visibility_mask & bit) == 0) return {};
    const auto preceding = static_cast<std::uint8_t>(view.timing_lane_visibility_mask & (bit - 1));
    const auto visible_marker_lane_slot = std::popcount(preceding);
    const auto width = (view.playable_field_left_pixels - compose_measure_rail_width) / visible_count;
    const auto inner_left = compose_measure_rail_width + width * visible_marker_lane_slot + 2.0;
    const auto inner_width = std::max(0.0, width - 4.0);
    const auto left = inner_left + inner_width * marker.marker_left_fraction;
    const auto right = inner_left + inner_width * marker.marker_right_fraction;
    constexpr double height = 16.0;
    const auto top = std::clamp(marker.marker_y_pixels - height * 0.5, 0.0,
                                std::max(0.0, view.viewport_height_pixels - height));
    return {left, top, std::max(left + 1.0, right), top + height};
}

static auto compose_timing_marker_screen_rect(const ComposePaintContext& paint,
                                               const WormUiTimingMarkerView& marker)
    -> struct nk_rect {
    const auto bounds = compose_timing_marker_document_rect(*paint.view, marker);
    return compose_screen_rect(&bounds, paint.view, paint.viewport);
}

static auto compose_hovered_timing_marker(const ComposePaintContext& paint)
    -> const WormUiTimingMarkerView* {
    const WormUiTimingMarkerView* hovered = nullptr;
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index) {
        const auto& marker = paint.view->timing_markers[index];
        const auto bounds = compose_timing_marker_screen_rect(paint, marker);
        if (bounds.w > 0.0f && bounds.h > 0.0f &&
            nk_input_is_mouse_hovering_rect(&paint.context->input, bounds)) hovered = &marker;
    }
    return hovered;
}

static auto compose_timing_marker_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiTimingMarkerView* {
    const WormUiTimingMarkerView* hit = nullptr;
    for (size_t index = 0; index < view.timing_marker_count; ++index) {
        const auto& marker = view.timing_markers[index];
        const auto bounds = compose_timing_marker_document_rect(view, marker);
        if (x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom)
            hit = &marker;
    }
    return hit;
}

static void draw_compose_timing_lanes(const ComposePaintContext& paint,
                                      ComposeTimingLaneLayout& layout) {
    constexpr std::array<const char*, WORM_UI_TIMING_LANE_COUNT> labels{
        "BPM", "SV", "SPD", "STP", "DLY", "WRP", "KIA", "ANC"};
    for (size_t index = 0; index < layout.colors.size(); ++index) {
        layout.colors[index] = wormedit::ui::theme::compose_note_accent(index);
    }
    const auto timing_left = paint.rail.x + paint.rail.w;
    const auto timing_width = std::max(0.0f, layout.field_left - timing_left);
    const auto visible_count = static_cast<std::size_t>(
        std::popcount(paint.view->timing_lane_visibility_mask));
    const auto lane_width = visible_count == 0 ? 0.0f : timing_width / visible_count;
    std::size_t visible_lane = 0;
    for (size_t index = 0; index < layout.lanes.size(); ++index) {
        if ((paint.view->timing_lane_visibility_mask & (1U << index)) == 0) {
            continue;
        }
        auto& lane = layout.lanes[index];
        lane = nk_rect(timing_left + lane_width * static_cast<float>(visible_lane++),
                       paint.viewport.y, lane_width, paint.viewport.h);
        nk_fill_rect(paint.canvas, lane, 0.0f,
                     (index & 1U) != 0 ? nk_rgb(25, 29, 38) : nk_rgb(28, 32, 42));
        nk_stroke_line(paint.canvas, lane.x, paint.viewport.y, lane.x,
                       paint.viewport.y + paint.viewport.h, 1.0f,
                       wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::grid));
        if (lane_width >= 17.0f) {
            const auto title = nk_rect(lane.x + 2.0f, paint.viewport.y + 2.0f,
                                       std::max(1.0f, lane_width - 4.0f), 14.0f);
            nk_draw_text(paint.canvas, title, labels[index], static_cast<int>(strlen(labels[index])),
                         paint.context->style.font, nk_rgba(18, 21, 29, 215), layout.colors[index]);
        }
    }
}

static void draw_compose_bands(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->band_count; ++index) {
        const auto& band = paint.view->bands[index];
        compose_fill_rect(paint.canvas, &band.bounds, paint.view, paint.viewport,
                          band.split_stage ? nk_rgb(27, 31, 43) : nk_rgb(25, 29, 39));
        const auto top = compose_projected_y(paint, band.bounds.top);
        const auto bottom = compose_projected_y(paint, band.bounds.bottom);
        nk_stroke_line(paint.canvas, paint.viewport.x, top, paint.viewport.x + paint.viewport.w,
                       top, 1.0f, nk_rgb(80, 91, 112));
        nk_stroke_line(paint.canvas, paint.viewport.x, bottom, paint.viewport.x + paint.viewport.w,
                       bottom, 1.0f, nk_rgb(61, 70, 87));
    }
}

static void draw_compose_play_lanes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->lane_count; ++index) {
        const auto& lane = paint.view->lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        nk_fill_rect(paint.canvas, rect, 0.0f, compose_lane_color(lane.role, lane.display_order));
        nk_stroke_line(paint.canvas, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       nk_rgb(62, 70, 86));
        if (rect.w >= 28.0f && rect.h >= 18.0f) {
            char label[16]{};
            const auto label_length = snprintf(label, sizeof(label), "%u", lane.display_order + 1U);
            if (label_length > 0) {
                const auto label_bounds = nk_rect(rect.x + 2.0f, rect.y + 2.0f,
                                                  std::min(28.0f, rect.w - 4.0f), 15.0f);
                nk_draw_text(paint.canvas, label_bounds, label, static_cast<int>(strlen(label)),
                             paint.context->style.font, nk_rgb(37, 43, 55), nk_rgb(122, 133, 153));
            }
        }
    }
}

static void draw_compose_bgm_lanes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bgm_lane_count; ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto alternate = (lane.source_order & 1U) != 0;
        nk_fill_rect(paint.canvas, rect, 0.0f,
                     alternate ? nk_rgb(43, 38, 52) : nk_rgb(38, 34, 47));
        nk_stroke_line(paint.canvas, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       nk_rgb(79, 67, 91));
    }
}

static void draw_compose_scene_background(const ComposePaintContext& paint,
                                          ComposeTimingLaneLayout& layout) {
    nk_fill_rect(paint.canvas, paint.rail, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::rail));
    nk_stroke_line(paint.canvas, paint.rail.x + paint.rail.w, paint.rail.y,
                   paint.rail.x + paint.rail.w, paint.rail.y + paint.rail.h, 1.0f,
                   nk_rgb(80, 91, 112));
    draw_compose_timing_lanes(paint, layout);
    draw_compose_bands(paint);
    draw_compose_play_lanes(paint);
    draw_compose_bgm_lanes(paint);
}

static void draw_compose_waveform(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.waveform_row_count == 0) {
        return;
    }
    const auto scale = paint.viewport.w / static_cast<float>(view.viewport_width_pixels);
    const auto left = paint.viewport.x + static_cast<float>(view.playable_field_left_pixels) * scale;
    const auto right = paint.viewport.x + static_cast<float>(view.playable_field_right_pixels) * scale;
    const auto center = (left + right) * 0.5f;
    const auto half_width = std::max(0.0f, (right - left) * 0.47f);
    for (size_t index = 0; index < view.waveform_row_count; ++index) {
        const auto& row = view.waveform_rows[index];
        const auto y = compose_projected_y(paint, row.y_pixels);
        nk_stroke_line(paint.canvas, center - row.rms[0] * half_width, y, center, y, 2.0f,
                       nk_rgba(57, 137, 167, 96));
        nk_stroke_line(paint.canvas, center, y, center + row.rms[1] * half_width, y, 2.0f,
                       nk_rgba(57, 137, 167, 96));
        nk_stroke_line(paint.canvas, center - row.absolute_peak[0] * half_width, y, center, y,
                       1.0f, nk_rgba(114, 222, 239, 142));
        nk_stroke_line(paint.canvas, center, y, center + row.absolute_peak[1] * half_width, y,
                       1.0f, nk_rgba(114, 222, 239, 142));
    }
}

static void draw_compose_channel_waveform(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.channel_waveform_row_count == 0) {
        return;
    }
    const auto selected_lane = std::find_if(
        view.bgm_lanes, view.bgm_lanes + view.bgm_lane_count, [&](const auto& lane) {
            return lane.source_lane_id == view.channel_waveform_source_lane_id;
        });
    if (selected_lane == view.bgm_lanes + view.bgm_lane_count) {
        return;
    }
    const auto rect = compose_screen_rect(&selected_lane->bounds, &view, paint.viewport);
    if (rect.w <= 1.0f || rect.h <= 0.0f) {
        return;
    }
    const auto center = rect.x + rect.w * 0.5f;
    const auto half_width = rect.w * 0.43f;
    for (size_t index = 0; index < view.channel_waveform_row_count; ++index) {
        const auto& row = view.channel_waveform_rows[index];
        const auto y = compose_projected_y(paint, row.y_pixels);
        nk_stroke_line(paint.canvas, center - row.rms[0] * half_width, y,
                       center + row.rms[1] * half_width, y, 2.0f, nk_rgba(87, 157, 196, 126));
        nk_stroke_line(paint.canvas, center - row.absolute_peak[0] * half_width, y,
                       center + row.absolute_peak[1] * half_width, y, 1.0f,
                       nk_rgba(146, 230, 251, 224));
    }
}

static void draw_compose_beat_fit_region(const ComposePaintContext& paint,
                                         const ComposeTimingLaneLayout& layout) {
    const auto& view = *paint.view;
    if (!view.beat_fit_drag_active) {
        return;
    }
    const auto top = std::clamp(compose_projected_y(paint, view.beat_fit_anchor_y_pixels),
                                paint.viewport.y, paint.viewport.y + paint.viewport.h);
    const auto bottom = std::clamp(compose_projected_y(paint, view.beat_fit_beat_y_pixels),
                                   paint.viewport.y, paint.viewport.y + paint.viewport.h);
    if (bottom <= top) {
        return;
    }
    const auto right = paint.viewport.x +
                       static_cast<float>(view.playable_field_right_pixels) * paint.viewport.w /
                           static_cast<float>(view.viewport_width_pixels);
    nk_fill_rect(paint.canvas,
                 nk_rect(layout.field_left, top, right - layout.field_left, bottom - top), 0.0f,
                 nk_rgba(255, 207, 104, 22));
}

static void draw_compose_differences(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->difference_count; ++index) {
        compose_fill_rect(paint.canvas, &paint.view->differences[index].bounds, paint.view,
                          paint.viewport, nk_rgba(229, 190, 58, 74));
    }
}

static void draw_compose_waveform_layer(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    draw_compose_waveform(paint);
    draw_compose_channel_waveform(paint);
    draw_compose_beat_fit_region(paint, layout);
    draw_compose_differences(paint);
}

static void draw_compose_proposed_grid(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->proposed_grid_line_count; ++index) {
        const auto& line = paint.view->proposed_grid_lines[index];
        const auto y = compose_projected_y(paint, line.y_pixels);
        const auto color = line.kind == 3                     ? nk_rgba(255, 207, 104, 230)
                           : line.kind == 1 || line.kind == 2 ? nk_rgba(255, 207, 104, 155)
                                                              : nk_rgba(255, 207, 104, 75);
        nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       line.kind == 3 ? 1.6f : 1.0f, color);
    }
}

static void draw_compose_beat_fit_overlay(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.beat_fit_drag_active) {
        return;
    }
    const auto y = compose_projected_y(paint, view.beat_fit_target_y_pixels);
    if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h) {
        nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       1.3f, nk_rgba(110, 222, 236, 215));
    }
    char label[140]{};
    const auto length =
        snprintf(label, sizeof(label), "Fit %lld/%llu -> %lld/%llu BPM · frame %llu",
                 static_cast<long long>(view.beat_fit_original_bpm_numerator),
                 static_cast<unsigned long long>(view.beat_fit_original_bpm_denominator),
                 static_cast<long long>(view.beat_fit_proposed_bpm_numerator),
                 static_cast<unsigned long long>(view.beat_fit_proposed_bpm_denominator),
                 static_cast<unsigned long long>(view.beat_fit_target_sample_frame));
    if (length > 0) {
        const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f,
                                    paint.viewport.y + 18.0f,
                                    std::min(310.0f, paint.viewport.w - paint.rail.w - 8.0f), 18.0f);
        nk_draw_text(paint.canvas, bounds, label, static_cast<int>(strlen(label)),
                     paint.context->style.font, nk_rgba(18, 21, 29, 235), nk_rgb(255, 219, 148));
    }
}

static void draw_compose_timing_preview_legend(const ComposePaintContext& paint) {
    if (paint.view->timing_preview_rect_count == 0) {
        return;
    }
    constexpr const char* legend = "Outlines: proposed notes/holds in cyan, BGM in purple";
    const auto top = paint.viewport.y + (paint.view->beat_fit_drag_active ? 38.0f : 18.0f);
    const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f, top,
                                std::min(380.0f, paint.viewport.w - paint.rail.w - 8.0f), 17.0f);
    nk_draw_text(paint.canvas, bounds, legend, static_cast<int>(strlen(legend)),
                 paint.context->style.font, nk_rgba(18, 21, 29, 220), nk_rgb(194, 227, 238));
}

static void draw_compose_timing_marker_drag_ghosts(const ComposePaintContext& paint) {
    if (!state.timing_marker_drag_active ||
        state.timing_marker_chart_id != paint.view->chart_id ||
        state.timing_marker_revision != paint.view->document_revision) return;
    bool selected_fixed = false;
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index)
        selected_fixed |= paint.view->timing_markers[index].selected &&
                          paint.view->timing_markers[index].fixed_initial;
    const auto ghost_color = selected_fixed ? nk_rgb(242, 111, 104) : nk_rgb(255, 207, 99);
    const auto delta = static_cast<float>(state.timing_marker_current_y_pixels -
                                           state.timing_marker_anchor_y_pixels) *
                       paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index) {
        const auto& marker = paint.view->timing_markers[index];
        if (!marker.selected) continue;
        auto ghost = compose_timing_marker_screen_rect(paint, marker);
        ghost.y += delta;
        nk_fill_rect(paint.canvas, ghost, 1.0f,
                     selected_fixed ? nk_rgba(242, 111, 104, 45) : nk_rgba(255, 207, 99, 40));
        nk_stroke_rect(paint.canvas, ghost, 1.0f, 2.0f, ghost_color);
    }
}

static void draw_compose_timing_markers(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    const auto* hovered = compose_hovered_timing_marker(paint);
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index) {
        const auto& marker = paint.view->timing_markers[index];
        const auto marker_lane_ordinal = static_cast<size_t>(marker.lane);
        if (marker_lane_ordinal >= layout.lanes.size() ||
            layout.lanes[marker_lane_ordinal].w <= 0.0f) {
            continue;
        }
        const auto bounds = compose_timing_marker_screen_rect(paint, marker);
        nk_fill_rect(paint.canvas, bounds, 1.0f, nk_rgb(18, 22, 31));
        const auto selected = marker.selected ||
                              (marker.fixed_initial && marker.object_id == 0 &&
                               state.timing_missing_initial_selected);
        const auto outline = hovered == &marker ? nk_rgb(255, 220, 112)
                             : selected ? nk_rgb(116, 92, 49)
                                        : nk_rgb(55, 64, 79);
        nk_stroke_rect(paint.canvas, bounds, 1.0f, hovered == &marker ? 2.0f : 1.5f,
                       outline);
        if (!marker.short_label || !marker.short_label[0]) continue;
        const auto* font = paint.context->style.font;
        const auto length = static_cast<int>(strlen(marker.short_label));
        const auto text_width = font->width(font->userdata, font->height, marker.short_label,
                                            length);
        const auto text_bounds = nk_rect(bounds.x + 2.0f, bounds.y + (bounds.h - font->height) * 0.5f,
                                         std::max(bounds.w - 4.0f, text_width + 2.0f), font->height);
        nk_draw_text(paint.canvas, text_bounds, marker.short_label, length, font,
                     nk_rgba(18, 22, 31, 0), layout.colors[marker_lane_ordinal]);
    }
    draw_compose_timing_marker_drag_ghosts(paint);
}

static void draw_compose_timing_preview_markers(const ComposePaintContext& paint,
                                                const ComposeTimingLaneLayout& layout) {
    for (size_t index = 0; index < paint.view->timing_preview_marker_count; ++index) {
        const auto& marker = paint.view->timing_preview_markers[index];
        const auto marker_lane_ordinal = static_cast<size_t>(marker.lane);
        if (marker_lane_ordinal >= layout.lanes.size() ||
            layout.lanes[marker_lane_ordinal].w <= 0.0f) {
            continue;
        }
        const auto& lane = layout.lanes[marker_lane_ordinal];
        const auto y = compose_projected_y(paint, marker.marker_y_pixels);
        nk_stroke_line(paint.canvas, lane.x + 2.0f, y, lane.x + lane.w - 2.0f, y, 2.0f,
                       nk_rgba(255, 207, 104, 215));
    }
}

static void draw_compose_timing_stage_marker(const ComposePaintContext& paint,
                                             const ComposeTimingLaneLayout& layout) {
    if (!paint.view->timing_stage_active) {
        return;
    }
    const auto staged_timing_lane_ordinal = static_cast<size_t>(paint.view->timing_stage_lane);
    if (staged_timing_lane_ordinal >= layout.lanes.size()) {
        return;
    }
    const auto& lane = layout.lanes[staged_timing_lane_ordinal];
    const auto y = compose_projected_y(paint, paint.view->timing_stage_y_pixels);
    const auto bounds = nk_rect(lane.x + 2.0f, y - 5.0f,
                                std::max(1.0f, lane.w - 4.0f), 10.0f);
    const auto color = paint.view->timing_stage_valid ? nk_rgb(255, 221, 108)
                                                      : nk_rgb(238, 96, 88);
    nk_fill_rect(paint.canvas, bounds, 1.0f, nk_rgba(color.r, color.g, color.b, 105));
    nk_stroke_rect(paint.canvas, bounds, 1.0f, 2.0f, color);
}

static void draw_compose_timing_layer(const ComposePaintContext& paint,
                                      const ComposeTimingLaneLayout& layout) {
    draw_compose_proposed_grid(paint);
    draw_compose_beat_fit_overlay(paint);
    draw_compose_timing_preview_legend(paint);
    draw_compose_timing_markers(paint, layout);
    draw_compose_timing_preview_markers(paint, layout);
    draw_compose_timing_stage_marker(paint, layout);
    draw_compose_beat_sections(paint);
    draw_compose_beat_bookmarks(paint);
    draw_compose_audio_sections(paint);
    draw_compose_audio_bookmarks(paint);
}

static void draw_compose_hold_bodies(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->hold_body_count; ++index) {
        const auto& hold = paint.view->hold_bodies[index];
        const auto rect = compose_screen_rect(&hold.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        if (hold.proposal_ghost) {
            nk_fill_rect(paint.canvas, rect, 0.0f, nk_rgba(240, 171, 71, 74));
            nk_stroke_rect(paint.canvas, rect, 0.0f, 1.4f, nk_rgb(255, 224, 156));
            continue;
        }
        const auto color = hold.proposal_origin
                               ? nk_rgb(51, 82, 100)
                               : (hold.semantic_color_visible
                                      ? nk_rgba(hold.semantic_red, hold.semantic_green,
                                                hold.semantic_blue, hold.semantic_alpha)
                                      : (hold.selected ? nk_rgb(62, 170, 204)
                                                       : nk_rgb(48, 111, 151)));
        nk_fill_rect(paint.canvas, rect, 0.0f, color);
        if (hold.selected) {
            nk_stroke_rect(paint.canvas, rect, 0.0f, 1.6f, nk_rgb(220, 247, 255));
        }
        if (paint.view->hover_note_visible && hold.note_id == paint.view->hover_note_id) {
            nk_stroke_rect(paint.canvas, rect, 0.0f, 2.0f, nk_rgb(255, 235, 148));
        }
    }
}

struct ComposeNoteStyle {
    struct nk_color fill;
    struct nk_color border;
    float border_width;
};

static ComposeNoteStyle compose_note_style(const WormUiComposeView& view,
                                           const WormUiComposeNoteView& note) {
    auto fill = note.proposal_origin
                    ? nk_rgb(73, 101, 120)
                    : (note.semantic_color_visible
                           ? nk_rgba(note.semantic_red, note.semantic_green, note.semantic_blue,
                                     note.semantic_alpha)
                           : (note.selected ? nk_rgb(111, 221, 236) : nk_rgb(133, 182, 222)));
    auto border = nk_rgb(17, 24, 34);
    auto border_width = 1.0f;
    if (note.proposal_ghost) {
        fill = nk_rgba(240, 171, 71, 132);
        border = nk_rgb(255, 224, 156);
        border_width = 2.0f;
    } else if (view.hover_note_visible && note.note_id == view.hover_note_id) {
        fill = nk_rgb(255, 226, 127);
        border = nk_rgb(255, 247, 190);
        border_width = 2.0f;
    } else if (note.selected && note.semantic_color_visible) {
        border = nk_rgb(220, 247, 255);
        border_width = 2.0f;
    } else if (note.paint_invalid_preview) {
        fill = nk_rgba(245, 178, 65, 210);
        border = nk_rgb(255, 229, 166);
        border_width = 2.0f;
    } else if (note.paint_erase_preview) {
        fill = nk_rgba(244, 94, 111, 210);
        border = nk_rgb(255, 184, 190);
        border_width = 2.0f;
    } else if (note.paint_preview) {
        fill = nk_rgba(81, 218, 145, 210);
        border = nk_rgb(185, 255, 216);
        border_width = 2.0f;
    } else if (note.kind == 4) {
        fill = nk_rgb(214, 91, 104);
    } else if (note.kind == 5) {
        fill = nk_rgb(236, 194, 112);
    } else if (note.tail) {
        fill = nk_rgb(113, 201, 216);
    }
    return {fill, border, border_width};
}

static void draw_compose_note_glyph(const ComposePaintContext& paint,
                                    const WormUiComposeNoteView& note, struct nk_rect tile) {
    if (note.glyph < 2 || note.glyph > 5 || tile.w < 10.0f || tile.h < 6.0f) {
        return;
    }
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
    nk_stroke_line(paint.canvas, first_x, first_y, tip_x, tip_y, 2.0f, arrow_color);
    nk_stroke_line(paint.canvas, tip_x, tip_y, last_x, last_y, 2.0f, arrow_color);
}

static void draw_compose_note_sound_label(const ComposePaintContext& paint,
                                          const WormUiComposeNoteView& note,
                                          struct nk_rect tile) {
    if (note.glyph > 1 || note.tail || !note.sound_label || !note.sound_label[0]) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(strlen(note.sound_label));
    const auto text_width = font->width(font->userdata, font->height, note.sound_label, length);
    if (tile.h < font->height || tile.w < text_width + 4.0f) return;

    const auto text_bounds = nk_rect(tile.x + (tile.w - text_width) * 0.5f,
                                     tile.y + (tile.h - font->height) * 0.5f,
                                     text_width, font->height);
    nk_draw_text(paint.canvas, text_bounds, note.sound_label, length, font,
                 nk_rgba(13, 21, 31, 220), nk_rgb(248, 251, 255));
}

static void draw_compose_note(const ComposePaintContext& paint,
                              const WormUiComposeNoteView& note) {
    const auto style = compose_note_style(*paint.view, note);
    const auto rect = compose_screen_rect(&note.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto tile = nk_rect(rect.x + 1.0f, rect.y, std::max(1.0f, rect.w - 2.0f),
                              std::max(2.0f, rect.h));
    nk_fill_rect(paint.canvas, tile, 0.0f, style.fill);
    nk_stroke_rect(paint.canvas, tile, 0.0f, style.border_width, style.border);
    nk_stroke_line(paint.canvas, tile.x + 1.0f, tile.y + 1.0f,
                   tile.x + tile.w - 1.0f, tile.y + 1.0f, 1.0f,
                   nk_rgba(255, 255, 255, 105));
    draw_compose_note_glyph(paint, note, tile);
    draw_compose_note_sound_label(paint, note, tile);
}

static void draw_compose_notes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->note_endpoint_count; ++index) {
        draw_compose_note(paint, paint.view->note_endpoints[index]);
    }
}

static void draw_compose_timing_preview_rects(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->timing_preview_rect_count; ++index) {
        const auto& preview = paint.view->timing_preview_rects[index];
        const auto rect = compose_screen_rect(&preview.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto color = preview.kind == 1 ? nk_rgba(220, 164, 244, 200)
                                             : nk_rgba(110, 222, 236, 195);
        const auto fill = preview.kind == 1 ? nk_rgba(220, 164, 244, 38)
                                            : nk_rgba(110, 222, 236, 34);
        nk_fill_rect(paint.canvas, rect, 0.0f, fill);
        nk_stroke_rect(paint.canvas, rect, 0.0f, 1.5f, color);
    }
}

static void draw_compose_playhead(const ComposePaintContext& paint) {
    if (!paint.view->playhead_visible) {
        return;
    }
    const auto y = compose_projected_y(paint, paint.view->playhead_y_pixels);
    nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 2.0f,
                   nk_rgb(93, 231, 190));
}

static void draw_compose_cursor(const ComposePaintContext& paint) {
    if (!paint.view->cursor) {
        return;
    }
    const auto rect = compose_screen_rect(&paint.view->cursor->bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto head_y = rect.y + rect.h * 0.5f;
    nk_stroke_line(paint.canvas, paint.viewport.x, head_y, paint.viewport.x + paint.viewport.w,
                   head_y, 1.4f, nk_rgb(255, 223, 108));
    nk_fill_rect(paint.canvas, rect, 0.0f, nk_rgb(248, 226, 115));
    nk_stroke_line(paint.canvas, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                   nk_rgb(255, 243, 174));
    nk_stroke_line(paint.canvas, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0f,
                   nk_rgb(255, 243, 174));
}

static void draw_compose_foreground_markers(const ComposePaintContext& paint) {
    draw_compose_timing_preview_rects(paint);
    draw_compose_playhead(paint);
    draw_compose_cursor(paint);
}

struct ComposeHoveredBgm {
    const char* detail{};
    bool restart{};
    bool slice{};
};

static void draw_compose_bgm_event(const ComposePaintContext& paint,
                                  const WormUiComposeBgmEventView& event,
                                  ComposeHoveredBgm& hovered) {
    const auto rect = compose_screen_rect(&event.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto fill = event.has_sound ? nk_rgb(177, 116, 207) : nk_rgb(102, 88, 113);
    nk_fill_rect(paint.canvas, rect, 1.5f, fill);
    nk_stroke_rect(paint.canvas, rect, 1.5f, event.selected ? 2.0f : 1.0f,
                   event.selected ? nk_rgb(248, 224, 255) : nk_rgb(50, 35, 58));
    if (event.bmson_slice) {
        const auto color = event.bmson_restart ? nk_rgb(255, 188, 94) : nk_rgb(119, 231, 226);
        nk_stroke_line(paint.canvas, rect.x + 3.0f, rect.y + 2.0f, rect.x + 3.0f,
                       rect.y + rect.h - 2.0f, 2.0f, color);
    }
    if (event.sound_label && event.sound_label[0] && rect.w >= 18.0f && rect.h >= 13.0f) {
        const auto text_rect = nk_rect(rect.x + (event.bmson_slice ? 6.0f : 3.0f), rect.y + 2.0f,
                                       std::max(1.0f, rect.w - (event.bmson_slice ? 8.0f : 5.0f)),
                                       std::min(15.0f, rect.h - 3.0f));
        nk_draw_text(paint.canvas, text_rect, event.sound_label,
                     static_cast<int>(strlen(event.sound_label)), paint.context->style.font, fill,
                     nk_rgb(255, 245, 255));
    }
    if (event.sound_detail && nk_input_is_mouse_hovering_rect(&paint.context->input, rect)) {
        hovered = {event.sound_detail, event.bmson_restart, event.bmson_slice};
    }
}

static void draw_compose_bgm_events(const ComposePaintContext& paint,
                                   ComposeHoveredBgm& hovered) {
    for (size_t index = 0; index < paint.view->bgm_event_count; ++index) {
        draw_compose_bgm_event(paint, paint.view->bgm_events[index], hovered);
    }
}

static void draw_compose_bmson_marker_drafts(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bmson_marker_draft_count; ++index) {
        const auto& marker = paint.view->bmson_marker_drafts[index];
        const auto rect = compose_screen_rect(&marker.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        nk_fill_rect(paint.canvas, rect, 1.5f,
                     marker.playable ? nk_rgba(85, 184, 245, 106) : nk_rgba(240, 171, 71, 94));
        nk_stroke_rect(paint.canvas, rect, 1.5f, 2.0f,
                       marker.restart ? nk_rgb(255, 188, 94) : nk_rgb(119, 231, 226));
    }
}

static void draw_compose_bmson_move_preview(const ComposePaintContext& paint) {
    if (!paint.view->bmson_marker_move_preview) {
        return;
    }
    const auto& marker = *paint.view->bmson_marker_move_preview;
    const auto rect = compose_screen_rect(&marker.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    nk_fill_rect(paint.canvas, rect, 1.5f,
                 marker.valid ? nk_rgba(240, 171, 71, 112) : nk_rgba(239, 82, 96, 112));
    nk_stroke_rect(paint.canvas, rect, 1.5f, 2.0f,
                   marker.valid ? nk_rgb(255, 224, 156) : nk_rgb(255, 132, 144));
}

static void draw_compose_bgm_lane_labels(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bgm_lane_count; ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w < 18.0f || rect.h <= 0.0f) {
            continue;
        }
        char label[20]{};
        const auto length = snprintf(label, sizeof(label), "BGM %zu", lane.source_order + 1);
        if (length > 0) {
            const auto title = nk_rect(rect.x + 2.0f, paint.viewport.y + 2.0f,
                                       std::max(1.0f, rect.w - 4.0f), 14.0f);
            nk_draw_text(paint.canvas, title, label, static_cast<int>(strlen(label)),
                         paint.context->style.font, nk_rgba(31, 25, 38, 215),
                         nk_rgb(211, 175, 226));
        }
    }
}

static ComposeHoveredBgm draw_compose_audio_markers(const ComposePaintContext& paint) {
    ComposeHoveredBgm hovered{};
    draw_compose_bgm_events(paint, hovered);
    draw_compose_bmson_marker_drafts(paint);
    draw_compose_bmson_move_preview(paint);
    draw_compose_bgm_lane_labels(paint);
    return hovered;
}

static void draw_compose_selection_preview(const ComposePaintContext& paint,
                                           const WormUiComposeRect& bounds,
                                           WormUiComposeSelectionMode mode) {
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto fill = mode == WORM_UI_COMPOSE_SELECTION_ADD ? nk_rgba(113, 214, 164, 34)
                     : mode == WORM_UI_COMPOSE_SELECTION_TOGGLE ? nk_rgba(197, 151, 245, 34)
                                                               : nk_rgba(101, 183, 242, 34);
    const auto border = mode == WORM_UI_COMPOSE_SELECTION_ADD ? nk_rgb(113, 214, 164)
                        : mode == WORM_UI_COMPOSE_SELECTION_TOGGLE ? nk_rgb(197, 151, 245)
                                                                  : nk_rgb(101, 183, 242);
    nk_fill_rect(paint.canvas, rect, 0.0f, fill);
    nk_stroke_rect(paint.canvas, rect, 0.0f, 1.2f, border);
}

static void draw_compose_space_range_preview(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.space_range_preview_visible) {
        return;
    }
    draw_compose_selection_preview(paint, view.space_range_preview_bounds,
                                   view.space_range_preview_mode);
    const auto anchor_bright = (SDL_GetTicks() / 350U) % 2U == 0U;
    const auto anchor_color = anchor_bright ? nk_rgb(255, 224, 122) : nk_rgb(185, 142, 72);
    const auto draw_row = [&](double y_pixels, struct nk_color color) {
        const auto y = compose_projected_y(paint, y_pixels);
        nk_stroke_line(paint.canvas, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       2.4f, color);
    };
    draw_row(view.space_range_anchor_y_pixels, anchor_color);
    if (std::abs(view.space_range_anchor_y_pixels - view.space_range_tail_y_pixels) >= 0.01) {
        draw_row(view.space_range_tail_y_pixels,
                 anchor_bright ? nk_rgb(97, 201, 255) : nk_rgb(63, 125, 176));
    }
}

static void draw_compose_autoscroll_preview(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.viewport_autoscroll_active ||
        !std::isfinite(view.viewport_autoscroll_anchor_x_pixels) ||
        !std::isfinite(view.viewport_autoscroll_anchor_y_pixels)) {
        return;
    }
    const auto anchor_x = paint.viewport.x + static_cast<float>(
        std::clamp(view.viewport_autoscroll_anchor_x_pixels, 0.0, view.viewport_width_pixels) *
        paint.viewport.w / view.viewport_width_pixels);
    const auto anchor_y = paint.viewport.y + static_cast<float>(
        std::clamp(view.viewport_autoscroll_anchor_y_pixels, 0.0, view.viewport_height_pixels) *
        paint.viewport.h / view.viewport_height_pixels);
    nk_stroke_line(paint.canvas, anchor_x - 9.0f, anchor_y, anchor_x + 9.0f, anchor_y, 2.0f,
                   nk_rgb(255, 211, 103));
    nk_stroke_line(paint.canvas, anchor_x, anchor_y - 9.0f, anchor_x, anchor_y + 9.0f, 2.0f,
                   nk_rgb(255, 211, 103));
    const auto indicator = nk_rect(paint.viewport.x + 8.0f, paint.viewport.y + paint.viewport.h - 26.0f,
                                   std::min(250.0f, std::max(150.0f, paint.viewport.w - 16.0f)),
                                   18.0f);
    nk_fill_rect(paint.canvas, indicator, 3.0f, nk_rgba(12, 16, 24, 235));
    constexpr const char* text = "Auto-scroll latched - middle-click to cancel";
    nk_draw_text(paint.canvas, indicator, text, static_cast<int>(strlen(text)),
                 paint.context->style.font, nk_rgb(12, 16, 24), nk_rgb(255, 231, 157));
}

static void draw_compose_note_tooltip(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.hover_note_visible || !view.hover_tooltip || view.hover_tooltip[0] == '\0') {
        return;
    }
    const auto width = std::min(420.0f, std::max(120.0f, paint.viewport.w - 16.0f));
    const auto bounds = nk_rect(paint.viewport.x + 8.0f, paint.viewport.y + 8.0f, width, 24.0f);
    nk_fill_rect(paint.canvas, bounds, 3.0f, nk_rgba(12, 16, 24, 235));
    nk_stroke_rect(paint.canvas, bounds, 3.0f, 1.0f, nk_rgb(255, 235, 148));
    nk_draw_text(paint.canvas, bounds, view.hover_tooltip, static_cast<int>(strlen(view.hover_tooltip)),
                 paint.context->style.font, nk_rgb(12, 16, 24), nk_rgb(255, 244, 192));

    for (size_t index = 0; index < view.note_endpoint_count; ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id != view.hover_note_id || !note.sound_detail || !note.sound_detail[0])
            continue;
        const auto resource_bounds = nk_rect(bounds.x, bounds.y + bounds.h + 2.0f,
                                             bounds.w, bounds.h);
        nk_fill_rect(paint.canvas, resource_bounds, 3.0f, nk_rgba(12, 16, 24, 235));
        nk_draw_text(paint.canvas, resource_bounds, note.sound_detail,
                     static_cast<int>(strlen(note.sound_detail)), paint.context->style.font,
                     nk_rgb(12, 16, 24), nk_rgb(245, 227, 255));
        break;
    }
}

static void draw_compose_timing_tooltip(const ComposePaintContext& paint) {
    const auto* marker = compose_hovered_timing_marker(paint);
    if (!marker || !marker->detail || !marker->detail[0]) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(strlen(marker->detail));
    const auto text_width = font->width(font->userdata, font->height, marker->detail, length);
    const auto width = std::max(1.0f, std::min(paint.viewport.w - 16.0f,
                                              text_width + 16.0f));
    const auto mouse = paint.context->input.mouse.pos;
    const auto x = std::clamp(mouse.x + 10.0f, paint.viewport.x + 8.0f,
                              std::max(paint.viewport.x + 8.0f,
                                       paint.viewport.x + paint.viewport.w - width - 8.0f));
    const auto y = std::clamp(mouse.y - 32.0f, paint.viewport.y + 8.0f,
                              std::max(paint.viewport.y + 8.0f,
                                       paint.viewport.y + paint.viewport.h - 28.0f));
    const auto bounds = nk_rect(x, y, width, 24.0f);
    nk_fill_rect(paint.canvas, bounds, 3.0f, nk_rgba(12, 16, 24, 245));
    nk_stroke_rect(paint.canvas, bounds, 3.0f, 1.0f, nk_rgb(255, 220, 112));
    nk_draw_text(paint.canvas, bounds, marker->detail, length, font,
                 nk_rgb(12, 16, 24), nk_rgb(255, 244, 192));
}

static void draw_compose_bgm_tooltip(const ComposePaintContext& paint,
                                     const ComposeHoveredBgm& hovered) {
    if (!hovered.detail) {
        return;
    }
    const auto bounds = nk_rect(paint.viewport.x + 8.0f, paint.viewport.y + 34.0f,
                                std::min(420.0f, std::max(120.0f, paint.viewport.w - 16.0f)),
                                24.0f);
    nk_fill_rect(paint.canvas, bounds, 3.0f, nk_rgba(12, 16, 24, 235));
    nk_stroke_rect(paint.canvas, bounds, 3.0f, 1.0f, nk_rgb(232, 190, 247));
    auto name_bounds = bounds;
    if (hovered.slice) {
        constexpr float mode_width = 76.0f;
        const auto* mode = hovered.restart ? "Restart" : "Continue";
        nk_draw_text(paint.canvas, nk_rect(bounds.x, bounds.y, mode_width, bounds.h), mode,
                     static_cast<int>(strlen(mode)), paint.context->style.font, nk_rgb(12, 16, 24),
                     hovered.restart ? nk_rgb(255, 205, 138) : nk_rgb(155, 243, 237));
        name_bounds.x += mode_width;
        name_bounds.w = std::max(1.0f, name_bounds.w - mode_width);
    }
    nk_draw_text(paint.canvas, name_bounds, hovered.detail,
                 static_cast<int>(strlen(hovered.detail)), paint.context->style.font,
                 nk_rgb(12, 16, 24), nk_rgb(245, 227, 255));
}

static void draw_compose_editor_overlays(const ComposePaintContext& paint,
                                         const ComposeHoveredBgm& hovered) {
    const auto& view = *paint.view;
    draw_compose_space_range_preview(paint);
    if (view.area_selection_preview_visible) {
        draw_compose_selection_preview(paint, view.area_selection_preview_bounds,
                                       view.area_selection_preview_mode);
    }
    draw_compose_autoscroll_preview(paint);
    draw_compose_note_tooltip(paint);
    draw_compose_bgm_tooltip(paint, hovered);
    draw_compose_timing_tooltip(paint);
}

static auto compose_editor_status_hint(const WormUiComposeView* view,
                                       const WormUiTimingView* timing_view) -> const char* {
    if (state.timing_stage_drag_active)
        return "Move timing event · release to keep staged position · Apply to commit";
    if (state.beat_fit_drag_active)
        return "Fit beat to waveform · release to commit one BPM change · Esc cancels";
    if (state.waveform_beat_zero_drag_active)
        return "Move beat zero on waveform overview · release to commit · Esc cancels";
    if (state.bmson_marker_move_active)
        return "Move BMSON BGM marker · release to commit · Esc cancels";
    if (state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place)
        return "Resource placement armed · click a chart lane · Esc disarms";
    if (state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::replace)
        return "Resource replacement armed · click an existing object · Esc disarms";
    if (state.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off)
        return "BMSON razor armed · click/drag in selected BGM lane · Esc disarms";
    if (timing_view->stage_active && timing_view->stage_existing_object_id != 0)
        return "Drag outlined timing marker to resnap · edit/apply in side inspector";
    if (view->semantic_layer_paint_mode_active)
        return "Layer paint armed · click/drag toggles layer · Esc cancels";
    if (view->semantic_layer_solo_active)
        return "Layer solo active · only selected-layer notes shown";
    if (view->note_paint_mode_active)
        return "Paint armed · click/drag draws or erases · Esc cancels";
    if (view->waveform_overview_row_count != 0 && !view->waveform_timing_locked)
        return "Drag beat squares in rail to fit BPM · drag beat zero in song overview";
    return "↑/↓ move · drag selects/moves · Alt-drag hold · drag BMSON BGM to move";
}

static void draw_compose_scene_canvas(nk_context* context, const WormUiComposeView* view,
                                      nk_command_buffer* canvas, struct nk_rect viewport) {
    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    nk_fill_rect(canvas, viewport, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::viewport));
    if (!view->scene_available) {
        const auto* status = view->status_text ? view->status_text : "Chart field is unavailable.";
        nk_draw_text(canvas, viewport, status, static_cast<int>(strlen(status)),
                     context->style.font,
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::viewport),
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::label));
    } else {
        const auto rail_width = static_cast<float>(42.0 * viewport.w / view->viewport_width_pixels);
        const auto rail = nk_rect(viewport.x, viewport.y, rail_width, viewport.h);
        const auto field_left =
            viewport.x + static_cast<float>(view->playable_field_left_pixels) * viewport.w /
                             static_cast<float>(view->viewport_width_pixels);
        const ComposePaintContext paint{context, view, canvas, viewport, rail};
        ComposeTimingLaneLayout timing_layout{};
        timing_layout.field_left = field_left;
        draw_compose_scene_background(paint, timing_layout);
        draw_compose_waveform_layer(paint, timing_layout);
        draw_compose_grid_lines(paint);
        draw_compose_timing_layer(paint, timing_layout);
        draw_compose_hold_bodies(paint);
        const auto hovered_bgm = draw_compose_audio_markers(paint);
        draw_compose_notes(paint);
        draw_compose_foreground_markers(paint);
        draw_compose_editor_overlays(paint, hovered_bgm);
    }
    nk_push_scissor(canvas, parent_clip);
}

struct ComposeToolbarContext {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
};

static void update_compose_note_label_cache(const WormUiComposeView* view) {
    if (state.label_project_id == view->project_id && state.label_chart_id == view->chart_id &&
        state.label_document_revision == view->document_revision &&
        state.label_note_id == view->selected_note_label_note_id)
        return;
    state.note_label.fill('\0');
    if (view->selected_note_label && view->selected_note_label_note_id != 0) {
        const auto length = std::min(strlen(view->selected_note_label), state.note_label.size() - 1);
        memcpy(state.note_label.data(), view->selected_note_label, length);
        state.note_label[length] = '\0';
    }
    state.label_project_id = view->project_id;
    state.label_chart_id = view->chart_id;
    state.label_document_revision = view->document_revision;
    state.label_note_id = view->selected_note_label_note_id;
}

static void render_compose_view_controls(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    if (wormedit::ui::chrome::IconButton{Glyph::zoom_out, "Zoom out"}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_OUT;
    if (wormedit::ui::chrome::IconButton{Glyph::zoom_in, "Zoom in"}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_IN;
    update_compose_pointer_navigation(action, std::nullopt);
    if (wormedit::ui::chrome::IconButton{Glyph::rotate_ccw, "Reset view"}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW;
    nk_label(context, "", NK_TEXT_LEFT);
    if (!view->scene_available) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::paintbrush, "Toggle note paint mode",
                                         view->note_paint_mode_active}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    if (!view->scene_available) nk_widget_disable_end(context);
    if (wormedit::ui::chrome::IconButton{compose_inspector_icon(state.inspector_visible),
                                         state.inspector_visible ? "Hide inspector" : "Show inspector",
                                         state.inspector_visible}.render(context))
        state.inspector_visible = !state.inspector_visible;
}

static void render_compose_bgm_scroll_controls(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    const bool scrollable = view->bgm_maximum_horizontal_scroll_pixels > 0.0;
    if (!scrollable) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left,
                                         "Scroll BGM columns left"}.render(context) && scrollable)
        append_bgm_horizontal_scroll(action, view, -80.0);
    constexpr nk_size scroll_steps = 1000;
    nk_size scroll_position = scrollable
        ? static_cast<nk_size>(view->bgm_horizontal_scroll_pixels /
                               view->bgm_maximum_horizontal_scroll_pixels * scroll_steps)
        : 0;
    if (nk_progress(context, &scroll_position, scroll_steps, NK_MODIFIABLE) && scrollable) {
        const auto target = view->bgm_maximum_horizontal_scroll_pixels *
                            static_cast<double>(scroll_position) / scroll_steps;
        append_bgm_horizontal_scroll(action, view,
                                     target - view->bgm_horizontal_scroll_pixels);
    }
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_right,
                                         "Scroll BGM columns right"}.render(context) && scrollable)
        append_bgm_horizontal_scroll(action, view, 80.0);
    if (!scrollable) nk_widget_disable_end(context);
    nk_labelf(context, NK_TEXT_RIGHT, "%.0f px/measure · %zu lanes",
              view->pixels_per_standard_measure, view->lane_count);
}

static void render_compose_toolbar(const ComposeToolbarContext& toolbar) {
    update_compose_note_label_cache(toolbar.view);
    auto* context = toolbar.context;
    nk_layout_row_template_begin(context, 27);
    wormedit::ui::chrome::IconButton::layout_columns(context, 3);
    nk_layout_row_template_push_static(context, 12);
    wormedit::ui::chrome::IconButton::layout_columns(context, 3);
    nk_layout_row_template_push_dynamic(context);
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_static(context, 215);
    nk_layout_row_template_end(context);
    render_compose_view_controls(toolbar);
    render_compose_bgm_scroll_controls(toolbar);
}

struct ComposeInspectorContext {
    nk_context* context;
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
};

static void render_initial_bpm_control(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto* timing_view = inspector.timing_view;
    auto* action = inspector.action;
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.57f);
    if (timing_view->initial_bpm_present && std::isfinite(timing_view->initial_bpm_value))
        nk_labelf(context, NK_TEXT_LEFT, "Initial BPM: %.6g", timing_view->initial_bpm_value);
    else
        nk_label(context, "Initial BPM: unset", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.43f);
    const auto can_edit_initial_bpm = timing_view->initial_bpm_editable &&
        !timing_view->stage_active && !timing_view->sv_recipe.active &&
        !timing_view->sv_recipe.selection_armed &&
        action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE;
    if (!can_edit_initial_bpm) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, "Set initial BPM");
    if (!can_edit_initial_bpm) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (clicked && can_edit_initial_bpm) {
        action->timing_stage_action = WORM_UI_TIMING_STAGE_BEGIN;
        action->timing_chart_id = timing_view->chart_id;
        action->timing_document_revision = timing_view->document_revision;
        action->timing_lane = WORM_UI_TIMING_LANE_BPM;
        action->timing_object_id = timing_view->initial_bpm_event_id;
        action->timing_has_target_y = timing_view->initial_bpm_event_id == 0;
        action->timing_target_y_fraction = 0.0;
    }
}

static void render_timing_lane_visibility(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto* view = inspector.view;
    const auto* timing_view = inspector.timing_view;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Timing lanes", NK_MINIMIZED)) return;
    constexpr std::array<const char*, WORM_UI_TIMING_LANE_COUNT> labels{
        "BPM", "SV", "SPD", "STP", "DLY", "WRP", "KIA", "ANC"};
    for (std::size_t row = 0; row < 2; ++row) {
        nk_layout_row_begin(context, NK_DYNAMIC, 24, 4);
        for (std::size_t column = 0; column < 4; ++column) {
            const auto lane = row * 4 + column;
            const auto bit = static_cast<std::uint8_t>(1U << lane);
            const auto visible = (view->timing_lane_visibility_mask & bit) != 0;
            const auto editing_lane = timing_view->stage_active &&
                                      static_cast<std::size_t>(timing_view->stage_lane) == lane;
            char label[16]{};
            (void)snprintf(label, sizeof(label), "%s: %s", labels[lane], visible ? "On" : "Off");
            nk_layout_row_push(context, 0.25f);
            const auto disabled = !view->scene_available || (editing_lane && visible);
            if (disabled) nk_widget_disable_begin(context);
            const auto clicked = nk_button_label(context, label);
            if (disabled) nk_widget_disable_end(context);
            if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_timing_lane = static_cast<std::uint8_t>(lane);
                action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY;
            }
        }
        nk_layout_row_end(context);
    }
    nk_tree_pop(context);
}

static void render_staged_bmson_regions(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (view->staged_bmson_marker_count == 0 ||
        !nk_tree_push(context, NK_TREE_TAB, "Staged BMSON regions", NK_MAXIMIZED))
        return;
    state.bmson_audition_index = std::clamp(state.bmson_audition_index, 1,
                                            static_cast<int>(view->staged_bmson_marker_count));
    nk_layout_row_dynamic(context, 24, 1);
    nk_property_int(context, "Region", 1, &state.bmson_audition_index,
                    static_cast<int>(view->staged_bmson_marker_count), 1, 1.0f);
    const auto selected_index = static_cast<std::size_t>(state.bmson_audition_index - 1);
    const auto assigned_lane = view->staged_bmson_marker_playable_lanes[selected_index];
    nk_labelf(context, NK_TEXT_LEFT, "%s · %s",
              view->staged_bmson_marker_restarts[selected_index] ? "Restart" : "Continue",
              assigned_lane == 0 ? "BGM" : "Playable key lane");
    const auto selected = static_cast<std::uint64_t>(state.bmson_audition_index);
    if (nk_button_label(context, "Audition staged region"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Assign active key lane"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Assign BGM"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Toggle restart"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Remove region"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    nk_tree_pop(context);
}

static void render_compose_marker_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Markers / sections", NK_MINIMIZED)) return;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    const auto disabled = !view->scene_available || !view->beat_marker_navigation_available;
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_push(context, 0.5f);
    const auto previous_clicked = nk_button_label(context, "Previous marker");
    nk_layout_row_push(context, 0.5f);
    const auto next_clicked = nk_button_label(context, "Next marker");
    if (disabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, view->beat_marker_navigation_available
                          ? "Navigate beat-anchored bookmarks and section boundaries"
                          : "No beat-anchored bookmarks or sections",
             NK_TEXT_LEFT);
    if (action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        if (previous_clicked)
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER;
        else if (next_clicked)
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER;
    }
    worm_ui_nuklear_render_compose_markers(context, view, action);
    nk_tree_pop(context);
}

static void render_compose_timing_inspector(const ComposeInspectorContext& inspector) {
    render_initial_bpm_control(inspector);
    render_timing_lane_visibility(inspector);
    if (!state.inline_timing_open && inspector.timing_view->stage_active &&
        nk_tree_push(inspector.context, NK_TREE_TAB, "Timing event", NK_MAXIMIZED)) {
        worm_ui_nuklear_render_timing_stage_editor(inspector.context, inspector.timing_view,
                                                   inspector.action);
        nk_tree_pop(inspector.context);
    }
    worm_ui_nuklear_render_compose_resources(inspector.context, inspector.view,
                                             inspector.action, &state);
    if (inspector.view->waveform_analysis_pending) {
        nk_layout_row_dynamic(inspector.context, 18, 1);
        nk_label(inspector.context, "Building aligned waveform...", NK_TEXT_LEFT);
    }
    render_staged_bmson_regions(inspector);
    render_compose_marker_section(inspector);
}

static void render_compose_note_label_editor(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Label (%zu)", view->selected_note_count);
    const auto disabled = !view->scene_available || view->selected_note_count == 0;
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 1);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.note_label.data(),
                                         state.note_label.size(), nk_filter_default);
    if (view->focus_note_label_editor) {
        nk_edit_focus(context, NK_EDIT_GOTO_END_ON_ACTIVATE);
        action->compose_note_label_focus_consumed = true;
    }
    nk_layout_row_dynamic(context, 25, 2);
    if (nk_button_label(context, "Apply") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_note_label, state.note_label.data(),
               sizeof(action->compose_note_label));
        action->compose_action = WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL;
    }
    if (nk_button_label(context, "Clear") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL;
    if (disabled) nk_widget_disable_end(context);
}

static auto semantic_layer_target_active(const WormUiComposeView* view) -> bool {
    for (size_t index = 0; index < view->semantic_layer_count; ++index)
        if (view->semantic_layers[index].selected) return true;
    return false;
}

static void render_semantic_layer_page_controls(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_button_label(context, "Add semantic layer") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 3);
    nk_layout_row_push(context, 0.28f);
    if (view->semantic_layer_page == 0) nk_widget_disable_begin(context);
    const auto previous_clicked = nk_button_label(context, "Previous");
    if (view->semantic_layer_page == 0) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.44f);
    if (view->semantic_layer_page_count == 0) {
        nk_label(context, "Palette empty", NK_TEXT_CENTERED);
    } else {
        nk_labelf(context, NK_TEXT_CENTERED, "Page %zu/%zu · %zu layers",
                  view->semantic_layer_page + 1, view->semantic_layer_page_count,
                  view->semantic_layer_total_count);
    }
    nk_layout_row_push(context, 0.28f);
    const auto last_page = view->semantic_layer_page_count == 0 ||
                           view->semantic_layer_page + 1 >= view->semantic_layer_page_count;
    if (last_page) nk_widget_disable_begin(context);
    const auto next_clicked = nk_button_label(context, "Next");
    if (last_page) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (previous_clicked)
        action->compose_action = WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE;
    else if (next_clicked)
        action->compose_action = WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE;
}

static void render_semantic_layer_button(const ComposeInspectorContext& inspector,
                                         const auto& layer, std::size_t index) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    std::array<char, 96> label{};
    const auto name_length = std::min<std::size_t>(strlen(layer.name), 24);
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
    if (!view->scene_available) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, label.data());
    if (!view->scene_available) nk_widget_disable_end(context);
    nk_style_pop_style_item(context);
    nk_style_pop_style_item(context);
    nk_style_pop_style_item(context);
    if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER;
        action->compose_semantic_layer_id = layer.layer_id;
    }
}

static void render_semantic_layer_palette(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    if (view->semantic_layer_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Semantic layers: no visible palette entries", NK_TEXT_LEFT);
        return;
    }
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, view->semantic_layers_truncated ? "Layers+" : "Layers", NK_TEXT_LEFT);
    for (size_t index = 0; index < view->semantic_layer_count; ++index)
        render_semantic_layer_button(inspector, view->semantic_layers[index], index);
}

static auto render_semantic_layer_palette_panel(const ComposeInspectorContext& inspector) -> bool {
    if (!nk_tree_push(inspector.context, NK_TREE_TAB, "Semantic layers", NK_MINIMIZED)) return false;
    render_semantic_layer_page_controls(inspector);
    render_semantic_layer_palette(inspector);
    return true;
}

static void render_semantic_layer_color_editor(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!state.semantic_layer_color_editor_visible) return;
    nk_layout_row_dynamic(context, 110, 1);
    auto color = nk_colorf{state.semantic_layer_color.r, state.semantic_layer_color.g,
                           state.semantic_layer_color.b, state.semantic_layer_color.a};
    color = nk_color_picker(context, color, NK_RGBA);
    state.semantic_layer_color = {color.r, color.g, color.b, color.a};
    const auto byte_component = [](float component) {
        return static_cast<uint8_t>(std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
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
    const auto apply_clicked = nk_button_label(context, "Apply color");
    nk_layout_row_push(context, 0.27f);
    const auto cancel_clicked = nk_button_label(context, "Cancel color");
    nk_layout_row_end(context);
    if (apply_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_semantic_layer_red = red;
        action->compose_semantic_layer_green = green;
        action->compose_semantic_layer_blue = blue;
        action->compose_semantic_layer_alpha = alpha;
        action->compose_action = WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
        state.semantic_layer_color_editor_visible = false;
    } else if (cancel_clicked) {
        state.semantic_layer_color_editor_visible = false;
    }
}

static void render_semantic_layer_selection_fields(const ComposeInspectorContext& inspector,
                                                   const auto& layer) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    if (state.semantic_layer_name_id != layer.layer_id ||
        state.semantic_layer_name_revision != view->document_revision) {
        state.semantic_layer_name.fill('\0');
        const auto length = std::min(strlen(layer.name), state.semantic_layer_name.size() - 1);
        memcpy(state.semantic_layer_name.data(), layer.name, length);
        state.semantic_layer_name_id = layer.layer_id;
        state.semantic_layer_name_revision = view->document_revision;
    }
    if (state.semantic_layer_color_id != layer.layer_id) {
        state.semantic_layer_color_id = layer.layer_id;
        state.semantic_layer_color_editor_visible = false;
    }
    if (!state.semantic_layer_color_editor_visible) {
        const auto color = nk_color_cf(nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
        state.semantic_layer_color = {color.r, color.g, color.b, color.a};
    }
    nk_layout_row_dynamic(context, 24, 1);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.semantic_layer_name.data(),
                                         state.semantic_layer_name.size(), nk_filter_default);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Rename") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_semantic_layer_name, state.semantic_layer_name.data(),
               sizeof(action->compose_semantic_layer_name));
        action->compose_action = WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER;
    }
    if (nk_button_label(context, "Color..."))
        state.semantic_layer_color_editor_visible = !state.semantic_layer_color_editor_visible;
}

static void render_semantic_layer_membership_details(const ComposeInspectorContext& inspector,
                                                     const auto& layer);

static void render_semantic_layer_selected_actions(const ComposeInspectorContext& inspector,
                                                   const auto& layer) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    nk_layout_row_dynamic(context, 24, 4);
    if (layer.locked) nk_widget_disable_begin(context);
    const auto delete_clicked = nk_button_label(context, "Delete");
    if (layer.locked) nk_widget_disable_end(context);
    if (delete_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER;
    if (nk_button_label(context, "Up") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
    if (nk_button_label(context, "Down") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN;
    if (nk_button_label(context, view->semantic_layer_target_visible ? "Hide" : "Show") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY;
    render_semantic_layer_color_editor(inspector);
    render_semantic_layer_membership_details(inspector, layer);
}

static void render_semantic_layer_membership_details(const ComposeInspectorContext& inspector,
                                                     const auto& layer) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    if (layer.assigned_note_count != 0) {
        nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
        nk_layout_row_push(context, 0.65f);
        nk_labelf(context, NK_TEXT_LEFT, "Assigned to %zu note%s", layer.assigned_note_count,
                  layer.assigned_note_count == 1 ? "" : "s");
        nk_layout_row_push(context, 0.35f);
        if (layer.locked) nk_widget_disable_begin(context);
        const auto unlink_clicked = nk_button_label(context, "Delete + unlink");
        if (layer.locked) nk_widget_disable_end(context);
        if (unlink_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
            action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS;
        nk_layout_row_end(context);
    }
    if (!layer.visible) return;
    nk_layout_row_begin(context, NK_DYNAMIC, 22, 2);
    nk_layout_row_push(context, 0.5f);
    if (nk_button_label(context, view->semantic_layer_target_required_filter ? "Require: On"
                                                                            : "Require filter") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
    nk_layout_row_push(context, 0.5f);
    if (nk_button_label(context, view->semantic_layer_target_excluded_filter ? "Exclude: On"
                                                                            : "Exclude filter") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER;
    nk_layout_row_end(context);
}

static void render_semantic_layer_selected_panel(const ComposeInspectorContext& inspector,
                                                std::size_t layer_count) {
    const auto* layers = inspector.view->semantic_layers;
    const auto selected = std::ranges::find_if(layers, layers + layer_count,
                                               [](const auto& layer) { return layer.selected; });
    if (selected == layers + layer_count) return;
    render_semantic_layer_selection_fields(inspector, *selected);
    render_semantic_layer_selected_actions(inspector, *selected);
}

static void render_semantic_layer_membership_buttons(const ComposeInspectorContext& inspector,
                                                     bool target_active) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Layer (%zu)", view->selected_note_count);
    const auto membership_enabled = view->scene_available && target_active &&
                                    !view->semantic_layer_target_locked &&
                                    view->selected_note_count != 0;
    const auto button = [&](const char* label, WormUiComposeActionKind kind) {
        if (!membership_enabled) nk_widget_disable_begin(context);
        const auto clicked = nk_button_label(context, label);
        if (!membership_enabled) nk_widget_disable_end(context);
        if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
            action->compose_action = kind;
    };
    nk_layout_row_dynamic(context, 24, 3);
    if (!view->scene_available || !target_active) nk_widget_disable_begin(context);
    const auto select_clicked = nk_button_label(context, "Select");
    if (!view->scene_available || !target_active) nk_widget_disable_end(context);
    if (select_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES;
    button("Add", WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER);
    button("Toggle", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER);
    nk_layout_row_dynamic(context, 24, 3);
    button("Remove", WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER);
}

static void render_semantic_layer_target_buttons(const ComposeInspectorContext& inspector,
                                                 bool target_active) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    const auto layer_paint_enabled = view->scene_available && target_active &&
                                     !view->semantic_layer_target_locked;
    nk_layout_row_dynamic(context, 24, 3);
    if (!layer_paint_enabled) nk_widget_disable_begin(context);
    const auto paint_clicked = nk_button_label(
        context, view->semantic_layer_paint_mode_active ? "Paint: Armed" : "Paint: Off");
    if (!layer_paint_enabled) nk_widget_disable_end(context);
    if (paint_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
    if (!target_active) nk_widget_disable_begin(context);
    const auto solo_clicked =
        nk_button_label(context, view->semantic_layer_solo_active ? "Solo: On" : "Solo: Off");
    if (!target_active) nk_widget_disable_end(context);
    if (solo_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO;
    nk_layout_row_dynamic(context, 24, 2);
    if (!target_active) nk_widget_disable_begin(context);
    const auto lock_clicked =
        nk_button_label(context, view->semantic_layer_target_locked ? "Unlock" : "Lock");
    if (!target_active) nk_widget_disable_end(context);
    if (lock_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK;
    if (!target_active) nk_widget_disable_begin(context);
    const auto clear_clicked = nk_button_label(context, "Clear target");
    if (!target_active) nk_widget_disable_end(context);
    if (clear_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER;
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

static bool compose_bgm_scroll_valid(const WormUiComposeView* view) {
    return std::isfinite(view->bgm_horizontal_scroll_pixels) &&
           std::isfinite(view->bgm_maximum_horizontal_scroll_pixels) &&
           view->bgm_horizontal_scroll_pixels >= 0.0 &&
           view->bgm_maximum_horizontal_scroll_pixels >= view->bgm_horizontal_scroll_pixels;
}

static bool compose_geometry_dimensions_valid(const WormUiComposeView* view) {
    const auto valid_viewport = std::isfinite(view->viewport_width_pixels) &&
                                view->viewport_width_pixels > 0.0 &&
                                view->viewport_width_pixels <= 10000000.0 &&
                                std::isfinite(view->viewport_height_pixels) &&
                                view->viewport_height_pixels > 0.0 &&
                                view->viewport_height_pixels <= 10000000.0;
    const auto valid_measure = std::isfinite(view->pixels_per_standard_measure) &&
                               view->pixels_per_standard_measure >=
                                   WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE &&
                               view->pixels_per_standard_measure <=
                                   WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE;
    const auto valid_field = std::isfinite(view->playable_field_left_pixels) &&
                             std::isfinite(view->playable_field_right_pixels) &&
                             view->playable_field_left_pixels >= 0.0 &&
                             view->playable_field_left_pixels <= view->playable_field_right_pixels &&
                             view->playable_field_right_pixels <= view->viewport_width_pixels;
    const auto valid_document = std::isfinite(view->timing_document_begin_fraction) &&
                                std::isfinite(view->timing_document_end_fraction) &&
                                view->timing_document_begin_fraction >= 0.0 &&
                                view->timing_document_end_fraction >=
                                    view->timing_document_begin_fraction &&
                                view->timing_document_end_fraction <= 1.0;
    return valid_viewport && valid_measure && valid_field && valid_document &&
           compose_bgm_scroll_valid(view);
}

static bool compose_geometry_overlay_valid(const WormUiComposeView* view) {
    const auto stage_valid = !view->timing_stage_active ||
        (std::isfinite(view->timing_stage_y_pixels) && view->timing_stage_y_pixels >= 0.0 &&
         view->timing_stage_y_pixels <= view->viewport_height_pixels &&
         view->timing_stage_lane >= WORM_UI_TIMING_LANE_BPM &&
         view->timing_stage_lane <= WORM_UI_TIMING_LANE_ANCHOR);
    const auto playhead_valid = !view->playhead_visible ||
        (std::isfinite(view->playhead_y_pixels) && view->playhead_y_pixels >= 0.0 &&
         view->playhead_y_pixels <= view->viewport_height_pixels);
    const auto selection_valid = !view->area_selection_preview_visible ||
        (compose_rect_valid(&view->area_selection_preview_bounds) &&
         compose_selection_mode_valid(view->area_selection_preview_mode));
    const auto space_valid = !view->space_range_preview_visible ||
        (compose_rect_valid(&view->space_range_preview_bounds) &&
         compose_selection_mode_valid(view->space_range_preview_mode) &&
         std::isfinite(view->space_range_anchor_y_pixels) &&
         std::isfinite(view->space_range_tail_y_pixels) &&
         view->space_range_anchor_y_pixels >= 0.0 &&
         view->space_range_anchor_y_pixels <= view->viewport_height_pixels &&
         view->space_range_tail_y_pixels >= 0.0 &&
         view->space_range_tail_y_pixels <= view->viewport_height_pixels);
    return stage_valid && playhead_valid && selection_valid && space_valid;
}

static bool compose_geometry_counts_valid(const WormUiComposeView* view) {
    return view->branch_row_count <= WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY &&
           view->branch_choice_count <= WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY &&
           view->branch_diagnostic_count <= WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY &&
           view->semantic_layer_count <= WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
           view->beat_bookmark_count <= WORM_UI_COMPOSE_BOOKMARK_CAPACITY &&
           view->beat_section_count <= WORM_UI_COMPOSE_SECTION_CAPACITY &&
           view->audio_bookmark_count <= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY &&
           view->audio_section_count <= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY &&
           view->timing_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           view->resource_count <= WORM_UI_COMPOSE_RESOURCE_CAPACITY &&
           view->output_group_count <= WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY &&
           view->resource_total_count >= view->resource_count &&
           (view->semantic_layer_page_count == 0
                ? view->semantic_layer_page == 0 && view->semantic_layer_total_count == 0 &&
                      view->semantic_layer_count == 0
                : view->semantic_layer_total_count != 0 &&
                      view->semantic_layer_page < view->semantic_layer_page_count &&
                      view->semantic_layer_page_count ==
                          1 + (view->semantic_layer_total_count - 1) /
                                  WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
                      view->semantic_layer_count ==
                          std::min(static_cast<size_t>(WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY),
                                   view->semantic_layer_total_count -
                                       view->semantic_layer_page *
                                           WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY));
}

static bool compose_geometry_arrays_valid(const WormUiComposeView* view) {
    return (view->branch_row_count == 0 || view->branch_rows) &&
           (view->branch_choice_count == 0 || view->branch_choices) &&
           (view->beat_bookmark_count == 0 || view->beat_bookmarks) &&
           (view->beat_section_count == 0 || view->beat_sections) &&
           (view->audio_bookmark_count == 0 || view->audio_bookmarks) &&
           (view->audio_section_count == 0 || view->audio_sections) &&
           (view->timing_marker_count == 0 || view->timing_markers) &&
           (view->resource_count == 0 || view->resources) &&
           (view->output_group_count == 0 || view->output_groups);
}

static bool compose_geometry_scene_elements_valid(const WormUiComposeView* view);

static bool compose_geometry_valid(const WormUiComposeView* view) {
    return view && compose_geometry_dimensions_valid(view) &&
           compose_geometry_overlay_valid(view) && compose_geometry_counts_valid(view) &&
           compose_geometry_arrays_valid(view) && compose_geometry_scene_elements_valid(view);
}

static bool compose_geometry_basic_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->band_count; ++index) {
        if (!compose_rect_valid(&view->bands[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->lane_count; ++index) {
        if (!compose_rect_valid(&view->lanes[index].bounds))
            return 0;
    }

    return true;
}

static bool compose_geometry_grid_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->grid_line_count; ++index) {
        if (!std::isfinite(view->grid_lines[index].y_pixels) ||
            std::abs(view->grid_lines[index].y_pixels) > 10000000.0)
            return 0;
    }
    for (size_t index = 0; index < view->proposed_grid_line_count; ++index) {
        if (!std::isfinite(view->proposed_grid_lines[index].y_pixels) ||
            view->proposed_grid_lines[index].y_pixels < 0.0 ||
            view->proposed_grid_lines[index].y_pixels > view->viewport_height_pixels)
            return 0;
    }

    return true;
}

static bool compose_geometry_timing_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->timing_preview_rect_count; ++index) {
        if (!compose_rect_valid(&view->timing_preview_rects[index].bounds) ||
            view->timing_preview_rects[index].kind > 2)
            return 0;
    }
    for (size_t index = 0; index < view->timing_preview_marker_count; ++index) {
        const auto& marker = view->timing_preview_markers[index];
        if (!std::isfinite(marker.marker_y_pixels) || marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            marker.lane < WORM_UI_TIMING_LANE_BPM || marker.lane > WORM_UI_TIMING_LANE_ANCHOR)
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

    return true;
}

static bool compose_geometry_resource_elements_valid(const WormUiComposeView* view) {    constexpr std::uint8_t compose_resource_roles = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE |
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
    for (size_t index = 0; index < view->output_group_count; ++index) {
        const auto& group = view->output_groups[index];
        if (!group.name || !group.relative_output_path || !group.relative_asset_directory ||
            (index != 0 && group.document_index <= view->output_groups[index - 1].document_index))
            return 0;
    }

    return true;
}

static bool compose_geometry_note_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->difference_count; ++index) {
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

    return true;
}

static bool compose_geometry_waveform_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->waveform_row_count; ++index) {
        const auto& row = view->waveform_rows[index];
        if (!std::isfinite(row.y_pixels) || row.y_pixels < 0.0 ||
            row.y_pixels > view->viewport_height_pixels) {
            return 0;
        }
        for (size_t channel = 0; channel < 2; ++channel) {
            if (!std::isfinite(row.absolute_peak[channel]) || !std::isfinite(row.rms[channel]) ||
                row.absolute_peak[channel] < 0.0f || row.absolute_peak[channel] > 1.0f ||
                row.rms[channel] < 0.0f || row.rms[channel] > 1.0f) {
                return 0;
            }
        }
    }
    for (size_t index = 0; index < view->channel_waveform_row_count; ++index) {
        const auto& row = view->channel_waveform_rows[index];
        if (!std::isfinite(row.y_pixels) || row.y_pixels < 0.0 ||
            row.y_pixels > view->viewport_height_pixels)
            return 0;
        for (size_t channel = 0; channel < 2; ++channel) {
            if (!std::isfinite(row.absolute_peak[channel]) || !std::isfinite(row.rms[channel]) ||
                row.absolute_peak[channel] < 0.0f || row.absolute_peak[channel] > 1.0f ||
                row.rms[channel] < 0.0f || row.rms[channel] > 1.0f)
                return 0;
        }
    }
    for (size_t index = 0; index < view->waveform_overview_row_count; ++index) {
        const auto& row = view->waveform_overview_rows[index];
        if (!std::isfinite(row.audio_fraction) || row.audio_fraction < 0.0 ||
            row.audio_fraction > 1.0 || !std::isfinite(row.absolute_peak) ||
            row.absolute_peak < 0.0f || row.absolute_peak > 1.0f) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_bmson_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->bmson_marker_draft_count; ++index) {
        if (!compose_rect_valid(&view->bmson_marker_drafts[index].bounds)) {
            return 0;
        }
    }
    if (view->bmson_marker_move_preview &&
        !compose_rect_valid(&view->bmson_marker_move_preview->bounds)) {
        return 0;
    }
    for (size_t index = 0; index < view->semantic_layer_count; ++index) {
        if (!view->semantic_layers || view->semantic_layers[index].layer_id == 0 ||
            !view->semantic_layers[index].name) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_bookmark_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->beat_bookmark_count; ++index) {
        const auto& bookmark = view->beat_bookmarks[index];
        if (bookmark.bookmark_id == 0 || !bookmark.label || bookmark.measure_number == 0 ||
            (bookmark.projection_visible &&
             (!std::isfinite(bookmark.projected_y_pixels) || bookmark.projected_y_pixels < 0.0 ||
              bookmark.projected_y_pixels > view->viewport_height_pixels)) ||
            (bookmark.proposed_projection_visible &&
             (!std::isfinite(bookmark.proposed_y_pixels) || bookmark.proposed_y_pixels < 0.0 ||
              bookmark.proposed_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_beat_section_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->beat_section_count; ++index) {
        const auto& section = view->beat_sections[index];
        const auto valid_y = [&](bool visible, double y) {
            return !visible || (std::isfinite(y) && y >= 0.0 && y <= view->viewport_height_pixels);
        };
        if (section.section_id == 0 || !section.label || section.begin_measure_number == 0 ||
            section.end_measure_number == 0 ||
            section.begin_measure_number > section.end_measure_number ||
            (section.begin_measure_number == section.end_measure_number &&
             section.begin_row >= section.end_row) ||
            section.begin_chart_row >= section.end_chart_row ||
            !valid_y(section.begin_projection_visible, section.projected_begin_y_pixels) ||
            !valid_y(section.end_projection_visible, section.projected_end_y_pixels) ||
            !valid_y(section.span_projection_visible, section.projected_span_top_y_pixels) ||
            !valid_y(section.span_projection_visible, section.projected_span_bottom_y_pixels) ||
            (section.span_projection_visible &&
             section.projected_span_top_y_pixels > section.projected_span_bottom_y_pixels) ||
            !valid_y(section.proposed_begin_projection_visible, section.proposed_begin_y_pixels) ||
            !valid_y(section.proposed_end_projection_visible, section.proposed_end_y_pixels) ||
            !valid_y(section.proposed_span_projection_visible,
                     section.proposed_span_top_y_pixels) ||
            !valid_y(section.proposed_span_projection_visible,
                     section.proposed_span_bottom_y_pixels) ||
            (section.proposed_span_projection_visible &&
             section.proposed_span_top_y_pixels > section.proposed_span_bottom_y_pixels)) {
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

    return true;
}

static bool compose_geometry_audio_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->audio_bookmark_count; ++index) {
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

    return true;
}

static bool compose_geometry_hover_valid(const WormUiComposeView* view) {    if (view->hover_note_visible && (!view->hover_tooltip || view->hover_note_id == 0)) {
        return 0;
    }

    return true;
}

static bool compose_geometry_branch_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->branch_row_count; ++index) {
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

    return true;
}

static bool compose_geometry_cursor_valid(const WormUiComposeView* view) {
    return !view->cursor || compose_rect_valid(&view->cursor->bounds);
}

static bool compose_geometry_scene_elements_valid(const WormUiComposeView* view) {
    return compose_geometry_basic_elements_valid(view) &&
           compose_geometry_grid_elements_valid(view) &&
           compose_geometry_timing_elements_valid(view) &&
           compose_geometry_resource_elements_valid(view) &&
           compose_geometry_note_elements_valid(view) &&
           compose_geometry_waveform_elements_valid(view) &&
           compose_geometry_bmson_elements_valid(view) &&
           compose_geometry_bookmark_elements_valid(view) &&
           compose_geometry_beat_section_elements_valid(view) &&
           compose_geometry_audio_elements_valid(view) &&
           compose_geometry_hover_valid(view) &&
           compose_geometry_branch_elements_valid(view) &&
           compose_geometry_cursor_valid(view);
}

static bool compose_beat_fit_valid(const WormUiComposeView* view) {
    return !view->beat_fit_drag_active ||
           (view->beat_fit_chart_row != 0 && view->waveform_overview_sample_rate != 0 &&
            view->beat_fit_original_bpm_denominator != 0 &&
            view->beat_fit_proposed_bpm_denominator != 0 &&
            std::isfinite(view->beat_fit_target_y_pixels) &&
            std::isfinite(view->beat_fit_anchor_y_pixels) &&
            std::isfinite(view->beat_fit_beat_y_pixels) &&
            std::abs(view->beat_fit_target_y_pixels) <= 10000000.0 &&
            std::abs(view->beat_fit_anchor_y_pixels) <= 10000000.0 &&
            std::abs(view->beat_fit_beat_y_pixels) <= 10000000.0 &&
            view->beat_fit_target_sample_frame < view->waveform_overview_total_frames);
}

static bool compose_waveform_overview_valid(const WormUiComposeView* view) {
    return std::isfinite(view->waveform_overview_visible_begin_fraction) &&
           std::isfinite(view->waveform_overview_visible_end_fraction) &&
           view->waveform_overview_visible_begin_fraction >= 0.0 &&
           view->waveform_overview_visible_end_fraction <= 1.0 &&
           view->waveform_overview_visible_begin_fraction <=
               view->waveform_overview_visible_end_fraction &&
           (!view->waveform_beat_zero_visible ||
            (std::isfinite(view->waveform_beat_zero_fraction) &&
             view->waveform_beat_zero_fraction >= 0.0 &&
             view->waveform_beat_zero_fraction <= 1.0)) &&
           (!view->waveform_proposed_beat_zero_visible ||
            (view->waveform_offset_drag_active &&
             std::isfinite(view->waveform_proposed_beat_zero_fraction) &&
             view->waveform_proposed_beat_zero_fraction >= 0.0 &&
             view->waveform_proposed_beat_zero_fraction <= 1.0)) &&
           (!view->waveform_offset_drag_active ||
            view->waveform_proposed_audio_offset_denominator != 0) &&
           (view->waveform_overview_row_count == 0 ||
            (view->waveform_overview_total_frames != 0 &&
             view->waveform_overview_sample_rate != 0 &&
             view->waveform_audio_offset_denominator != 0));
}

static bool compose_scene_arrays_present(const WormUiComposeView* view) {
    return (view->band_count == 0 || view->bands) && (view->lane_count == 0 || view->lanes) &&
           (view->grid_line_count == 0 || view->grid_lines) &&
           (view->proposed_grid_line_count == 0 || view->proposed_grid_lines) &&
           (view->timing_preview_rect_count == 0 || view->timing_preview_rects) &&
           (view->timing_preview_marker_count == 0 || view->timing_preview_markers) &&
           (view->timing_marker_count == 0 || view->timing_markers) &&
           (view->difference_count == 0 || view->differences) &&
           (view->hold_body_count == 0 || view->hold_bodies) &&
           (view->note_endpoint_count == 0 || view->note_endpoints) &&
           (view->bgm_lane_count == 0 || view->bgm_lanes) &&
           (view->bgm_event_count == 0 || view->bgm_events) &&
           (view->waveform_row_count == 0 || view->waveform_rows) &&
           (view->channel_waveform_row_count == 0 ||
            (view->channel_waveform_rows && view->channel_waveform_source_lane_id != 0 &&
             view->bgm_lane_count != 0)) &&
           (view->waveform_overview_row_count == 0 || view->waveform_overview_rows) &&
           (view->semantic_layer_count == 0 || view->semantic_layers) &&
           (view->lane_key_binding_count == 0 || view->lane_key_bindings) &&
           (view->branch_choice_count == 0 || view->branch_choices) &&
           (view->branch_row_count == 0 || view->branch_rows);
}

int worm_ui_nuklear_compose_view_valid(const WormUiComposeView* view) {
    return view && view->band_count <= WORM_UI_COMPOSE_BAND_CAPACITY &&
           (view->selected_bmson_bgm_event_id == 0 || view->selected_object_count == 1) &&
           (view->selected_bmson_note_event_id == 0 || view->selected_note_count == 1) &&
           ((view->selected_bmson_bgm_event_id == 0) ==
            (view->selected_bmson_source_lane_id == 0)) &&
           view->staged_bmson_marker_count <= 256 &&
           (view->staged_bmson_marker_count == 0 ||
            (view->staged_bmson_marker_playable_lanes != nullptr &&
             view->staged_bmson_marker_restarts != nullptr)) &&
           view->bmson_marker_draft_count <= view->staged_bmson_marker_count &&
           (view->bmson_marker_draft_count == 0 || view->bmson_marker_drafts != nullptr) &&
           view->lane_count <= WORM_UI_COMPOSE_LANE_CAPACITY &&
           view->grid_line_count <= WORM_UI_COMPOSE_GRID_CAPACITY &&
           view->proposed_grid_line_count <= WORM_UI_COMPOSE_GRID_CAPACITY &&
           view->timing_preview_rect_count <= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY &&
           view->timing_preview_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           (view->proposed_grid_line_count == 0 || view->waveform_offset_drag_active ||
            view->beat_fit_drag_active) &&
           ((view->timing_preview_rect_count == 0 && view->timing_preview_marker_count == 0) ||
            view->waveform_offset_drag_active || view->beat_fit_drag_active) &&
           compose_beat_fit_valid(view) &&
           view->timing_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           view->difference_count <= WORM_UI_COMPOSE_DIFFERENCE_CAPACITY &&
           view->hold_body_count <= WORM_UI_COMPOSE_HOLD_CAPACITY &&
           view->note_endpoint_count <= WORM_UI_COMPOSE_NOTE_CAPACITY &&
           view->bgm_lane_count <= WORM_UI_COMPOSE_BGM_LANE_CAPACITY &&
           view->bgm_event_count <= WORM_UI_COMPOSE_BGM_EVENT_CAPACITY &&
           view->waveform_row_count <= WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY &&
           view->channel_waveform_row_count <= WORM_UI_COMPOSE_CHANNEL_WAVEFORM_ROW_CAPACITY &&
           view->waveform_overview_row_count <= WORM_UI_COMPOSE_WAVEFORM_OVERVIEW_CAPACITY &&
           compose_waveform_overview_valid(view) &&
           view->semantic_layer_count <= WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
           view->lane_key_binding_count <= WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY &&
           view->branch_row_count <= WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY &&
           view->branch_choice_count <= WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY &&
           view->branch_diagnostic_count <= WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY &&
           compose_scene_arrays_present(view) && compose_geometry_valid(view);
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

static bool release_compose_key_hold(const auto& key_event, WormUiWorkspaceAction* action) {
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
    if (state.space_range_hold_active && !key_event.down &&
        key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
        if (state.space_range_hold_moved)
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
        state.space_range_hold_active = false;
        state.space_range_hold_moved = false;
        return true;
    }
    return false;
}

static bool process_compose_navigation_key(const auto& key_event,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action) {
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
    update_compose_pointer_navigation(action, std::nullopt);
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE, navigation_action);
    state.navigation_key_active = !state.action_overflowed;
    state.navigation_keycode = key_event.scancode;
    state.navigation_action = navigation_action;
    if ((key == static_cast<uint32_t>(SDLK_UP) || key == static_cast<uint32_t>(SDLK_DOWN)) &&
        !state.action_overflowed && state.space_range_hold_active)
        state.space_range_hold_moved = true;
    return true;
}

static bool process_compose_escape_or_space(const auto& key_event,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action,
                                           bool pointer_paint_mode_active) {
    if (key_event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
        cancel_beat_fit_drag(action);
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
        return true;
    }
    if (key_event.keycode != static_cast<uint32_t>(SDLK_SPACE)) return false;
    constexpr auto any_space_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                        SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
    if (!pointer_paint_mode_active && !state.space_range_hold_active &&
        (key_event.modifiers & any_space_modifier) == 0) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION, 0.0, 0.0,
                             0, false, WORM_UI_COMPOSE_SELECTION_REPLACE);
        state.space_range_hold_active = !view->space_range_preview_visible &&
                                        !state.action_overflowed;
        state.space_range_hold_moved = false;
    }
    return true;
}

static void process_compose_lane_key(const auto& key_event, const WormUiComposeView* view,
                                    WormUiWorkspaceAction* action) {
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
    if (!keyboard_ready || !state.keyboard_focus || !key_event.down || key_event.repeat) return;
    if (process_compose_escape_or_space(key_event, view, action, pointer_paint_mode_active)) return;
    if (compose_shortcut_modifier(key_event.modifiers)) return;
    if (process_compose_navigation_key(key_event, view, action)) return;
    if (pointer_paint_mode_active) return;
    if ((key_event.modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_MODE |
                                SDL_KMOD_LEVEL5)) != 0)
        return;
    process_compose_lane_key(key_event, view, action);
}

static void process_compose_key_events(struct nk_context* context, const WormUiComposeView* view,
                                       WormUiWorkspaceAction* action, bool window_focused,
                                       bool ime_composition_active) {
    if (state.inline_timing_open) {
        for (size_t index = 0; index < state.key_event_count; ++index) {
            const auto& event = state.key_events[index];
            if (event.down && event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
                state.inline_timing_open = false;
                action->timing_stage_action = WORM_UI_TIMING_STAGE_CANCEL;
                break;
            }
        }
        state.key_event_count = 0;
        return;
    }
    const bool keyboard_ready =
        window_focused && state.keyboard_focus && view->scene_available && context;
    const bool pointer_paint_mode_active = compose_pointer_paint_mode_active(view, action);
    if (ime_composition_active) {
        cancel_compose_navigation(action);
        state.key_event_count = 0;
        return;
    }
    cancel_holds_for_pointer_paint(pointer_paint_mode_active, action);
    for (size_t event_index = 0; event_index < state.key_event_count && !state.action_overflowed;
         ++event_index) {
        const auto& key_event = state.key_events[event_index];
        process_compose_key_event(key_event, view, action, keyboard_ready,
                                  pointer_paint_mode_active);
    }
    state.key_event_count = 0;
}

static void cancel_pending_compose_panel_actions(WormUiWorkspaceAction* action) {
    cancel_beat_fit_drag(action);
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

struct ComposeOverviewInput {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect overview;
    bool available;
    bool pressed;
    bool window_focused;
    bool beat_zero_was_active;
};

struct ComposeOverviewContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_command_buffer* canvas;
    struct nk_rect bounds;
};

struct ComposePointerFocusContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    struct nk_rect overview;
    float editor_input_right;
    bool overview_available;
    bool window_focused;
};

struct ComposePointerFocusState {
    bool pointer_inside;
    bool pointer_clicked;
    bool overview_pressed;
};

static ComposePointerFocusState update_compose_pointer_focus(
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

struct ComposeViewportInputContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    float input_right;
    bool pointer_inside;
    bool window_focused;
};

struct ComposeViewportCursor {
    double x;
    double y;
    double note_field_left;
    double note_field_right;
    bool in_note_field;
};

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
        if (state.middle_click_pending && state.middle_click_x >= input.viewport.x &&
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

static void append_compose_wheel_inputs(const ComposeViewportInputContext& input) {
    const auto& view = *input.view;
    if (!state.action_overflowed && view.scene_available && input.window_focused) {
        for (size_t index = 0; index < state.wheel_event_count; ++index) {
            const auto& wheel = state.wheel_events[index];
            if (wheel.mouse_x < input.viewport.x || wheel.mouse_x >= input.input_right ||
                wheel.mouse_y < input.viewport.y || wheel.mouse_y > input.viewport.y + input.viewport.h) {
                continue;
            }
            const auto zoom_modifier = (wheel.modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            input.action->compose_viewport_inputs[input.action->compose_viewport_input_count++] = {
                .kind = wheel.coarse_steps ? WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                                           : WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
                .zoom_modifier = zoom_modifier,
                .delta_x = wheel.delta_x,
                .delta_y = wheel.delta_y,
                .cursor_x_pixels =
                    (wheel.mouse_x - input.viewport.x) * view.viewport_width_pixels / input.viewport.w,
                .cursor_y_pixels =
                    (wheel.mouse_y - input.viewport.y) * view.viewport_height_pixels / input.viewport.h,
            };
        }
    }
    state.wheel_event_count = 0;
}

static ComposeViewportCursor collect_compose_viewport_inputs(
    const ComposeViewportInputContext& input) {
    const auto cursor = update_compose_viewport_pointer(input);
    append_compose_wheel_inputs(input);
    return cursor;
}

struct ComposeBeatFitInput {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    double pointer_y;
    double pointer_started_x;
    double pointer_started_y;
    std::uint16_t pointer_modifiers;
    std::uint16_t blocked_modifiers;
    std::size_t shortcut_event_count;
    bool window_focused;
    bool pointer_started_in_view;
    bool left_down;
    bool left_released;
};

static bool compose_beat_fit_start_allowed(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    return view.scene_available && input.window_focused && input.pointer_started_in_view &&
           input.left_down && input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.blocked_modifiers) == 0 &&
           input.pointer_started_x >= 22.0 && input.pointer_started_x <= 42.0 &&
           view.waveform_row_count != 0 && view.waveform_overview_sample_rate != 0 &&
           !view.waveform_timing_locked && !view.waveform_offset_drag_active;
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
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, 0.0,
                                 std::clamp(input.pointer_y, 0.0,
                                            view.viewport_height_pixels));
            state.beat_fit_drag_active = false;
        }
    } else if (input.left_down && nk_input_is_mouse_moved(&input.context->input)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 0.0,
                             std::clamp(input.pointer_y, 0.0, view.viewport_height_pixels));
    }
}

static bool process_compose_beat_fit_pointer(const ComposeBeatFitInput& input) {
    if (state.beat_fit_drag_active) {
        update_active_beat_fit_drag(input);
        return true;
    }
    if (input.view->beat_fit_drag_active) {
        cancel_unmatched_view_beat_fit(input);
        return false;
    }
    return start_compose_beat_fit_drag(input);
}

static void request_audio_offset_nudge(const ComposeOverviewContext& overview,
                                       int64_t frames) {
    if (overview.action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        return;
    }
    overview.action->audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_NUDGE;
    overview.action->audio_offset_drag_chart_id = overview.view->chart_id;
    overview.action->audio_offset_drag_document_revision = overview.view->document_revision;
    overview.action->audio_offset_drag_sample_rate = overview.view->waveform_overview_sample_rate;
    overview.action->audio_offset_drag_delta_frames = frames;
}

static void render_compose_offset_controls(const ComposeOverviewContext& overview) {
    const auto& view = *overview.view;
    if (view.waveform_overview_row_count == 0) {
        return;
    }
    const auto numerator = view.waveform_offset_drag_active
                               ? view.waveform_proposed_audio_offset_numerator
                               : view.waveform_audio_offset_numerator;
    const auto denominator = view.waveform_offset_drag_active
                                 ? view.waveform_proposed_audio_offset_denominator
                                 : view.waveform_audio_offset_denominator;
    const auto offset_seconds = static_cast<double>(numerator) / static_cast<double>(denominator);
    const auto disabled = view.waveform_timing_locked || view.waveform_offset_drag_active;
    const auto ten_ms = static_cast<int64_t>(std::max(1U, view.waveform_overview_sample_rate / 100U));
    auto* context = overview.context;
    nk_layout_row_begin(context, NK_DYNAMIC, 22, 5);
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_push(context, 0.13f);
    if (nk_button_label(context, "-10ms")) request_audio_offset_nudge(overview, -ten_ms);
    nk_layout_row_push(context, 0.10f);
    if (nk_button_label(context, "-1f")) request_audio_offset_nudge(overview, -1);
    if (disabled) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.54f);
    nk_labelf(context, NK_TEXT_CENTERED, "Offset %+.6f s%s", offset_seconds,
              view.waveform_timing_locked        ? " · locked"
              : view.waveform_offset_drag_active ? " · preview"
                                                  : "");
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_push(context, 0.10f);
    if (nk_button_label(context, "+1f")) request_audio_offset_nudge(overview, 1);
    nk_layout_row_push(context, 0.13f);
    if (nk_button_label(context, "+10ms")) request_audio_offset_nudge(overview, ten_ms);
    if (disabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
}

static void render_compose_waveform_overview(const ComposeOverviewContext& overview) {
    const auto& view = *overview.view;
    const auto bounds = overview.bounds;
    if (view.waveform_overview_row_count == 0 || bounds.w <= 1.0f || bounds.h <= 1.0f) {
        return;
    }
    auto* canvas = overview.canvas;
    nk_fill_rect(canvas, bounds, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::overview));
    const auto center = bounds.x + bounds.w * 0.5f;
    const auto half_width = bounds.w * 0.43f;
    for (size_t index = 0; index < view.waveform_overview_row_count; ++index) {
        const auto& row = view.waveform_overview_rows[index];
        const auto y = bounds.y + static_cast<float>(row.audio_fraction) * bounds.h;
        nk_stroke_line(canvas, center - row.absolute_peak * half_width, y,
                       center + row.absolute_peak * half_width, y, 1.0f,
                       nk_rgba(114, 222, 239, 190));
    }
    const auto thumb_top = bounds.y +
        static_cast<float>(view.waveform_overview_visible_begin_fraction) * bounds.h;
    const auto thumb_bottom = bounds.y +
        static_cast<float>(view.waveform_overview_visible_end_fraction) * bounds.h;
    const auto thumb_height = std::max(6.0f, thumb_bottom - thumb_top);
    const auto thumb_y = std::min(thumb_top, bounds.y + bounds.h - thumb_height);
    const auto thumb = nk_rect(bounds.x + 1.0f, thumb_y, bounds.w - 2.0f, thumb_height);
    nk_fill_rect(canvas, thumb, 0.0f, nk_rgba(99, 173, 221, 55));
    nk_stroke_rect(canvas, thumb, 0.0f, 1.0f, nk_rgba(139, 210, 255, 180));
    if (view.waveform_beat_zero_visible) {
        const auto zero_y = std::min(bounds.y +
                                         static_cast<float>(view.waveform_beat_zero_fraction) * bounds.h,
                                     bounds.y + bounds.h - 1.0f);
        nk_stroke_line(canvas, bounds.x, zero_y, bounds.x + bounds.w, zero_y,
                       view.waveform_offset_drag_active ? 1.0f : 2.0f,
                       view.waveform_offset_drag_active ? nk_rgba(255, 207, 104, 110)
                                                        : nk_rgb(255, 207, 104));
    }
    if (view.waveform_proposed_beat_zero_visible) {
        const auto proposed_y = std::min(
            bounds.y + static_cast<float>(view.waveform_proposed_beat_zero_fraction) * bounds.h,
            bounds.y + bounds.h - 1.0f);
        nk_stroke_line(canvas, bounds.x, proposed_y, bounds.x + bounds.w, proposed_y, 2.0f,
                       nk_rgb(255, 237, 143));
    }
}

static std::int64_t waveform_offset_drag_delta(const ComposeOverviewInput& input) {
    if (state.waveform_beat_zero_drag_height <= 0.0f) {
        return 0;
    }
    const auto frames = (static_cast<long double>(input.context->input.mouse.pos.y) -
                         state.waveform_beat_zero_drag_anchor_y) /
                        state.waveform_beat_zero_drag_height *
                        state.waveform_beat_zero_drag_total_frames;
    const auto minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    const auto maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (frames <= minimum) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (frames >= maximum) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(std::round(frames));
}

static void start_waveform_beat_zero_drag(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    if (!input.pressed || !input.window_focused || !view.scene_available ||
        !view.waveform_beat_zero_visible || view.waveform_timing_locked ||
        view.waveform_offset_drag_active ||
        input.action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE ||
        std::abs(static_cast<double>(left_button.clicked_pos.y) -
                 (input.overview.y + view.waveform_beat_zero_fraction * input.overview.h)) > 7.0) {
        return;
    }
    state.waveform_overview_drag_active = false;
    state.waveform_beat_zero_drag_active = true;
    state.waveform_beat_zero_drag_anchor_y = left_button.clicked_pos.y;
    state.waveform_beat_zero_drag_height = input.overview.h;
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
        !view.waveform_timing_locked && view.chart_id == state.waveform_beat_zero_drag_chart_id &&
        view.document_revision == state.waveform_beat_zero_drag_revision &&
        view.selected_audio_asset_id == state.waveform_beat_zero_drag_asset_id &&
        view.waveform_overview_total_frames == state.waveform_beat_zero_drag_total_frames &&
        view.waveform_overview_sample_rate == state.waveform_beat_zero_drag_sample_rate &&
        input.overview.h == state.waveform_beat_zero_drag_height &&
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
    if (!input.beat_zero_was_active && !state.waveform_beat_zero_drag_active && input.pressed &&
        input.window_focused &&
        view.scene_available && !view.waveform_offset_drag_active &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        state.waveform_overview_drag_active = true;
    }
    if (!input.window_focused || !left_button.down) {
        state.waveform_overview_drag_active = false;
    }
    if (state.waveform_overview_drag_active && input.action->compose_input_count <
                                                   WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY &&
        (input.pressed || nk_input_is_mouse_moved(&input.context->input))) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK);
        input.action->compose_inputs[input.action->compose_input_count - 1].audio_fraction =
            std::clamp((static_cast<double>(input.context->input.mouse.pos.y) - input.overview.y) /
                           input.overview.h,
                       0.0, 1.0);
    }
}

static void process_compose_overview_input(const ComposeOverviewInput& input) {
    if (input.view->waveform_offset_drag_active && !state.waveform_beat_zero_drag_active &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        input.action->audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
    }
    update_waveform_beat_zero_drag(input);
    update_waveform_overview_seek(input);
}

struct ComposeDoubleClickContext {
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
    const worm_ui_nuklear_pointer_click_t* clicks;
    std::size_t click_count;
    struct nk_rect viewport;
    double editor_input_right;
    double note_field_left;
    double note_field_right;
};

static bool begin_timing_stage_from_rail_click(const ComposeDoubleClickContext& input,
                                               double click_x, double click_y) {
    const auto& view = *input.view;
    const auto& timing_view = *input.timing_view;
    auto& action = *input.action;
    if (timing_view.stage_active || click_x < compose_measure_rail_width ||
        click_x >= input.note_field_left ||
        action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE)
        return false;
    const auto visible_lane_count = static_cast<std::size_t>(
        std::popcount(view.timing_lane_visibility_mask));
    const auto lane_width = visible_lane_count == 0
                                ? 0.0
                                : (input.note_field_left - compose_measure_rail_width) /
                                      visible_lane_count;
    if (lane_width <= 0.0 || visible_lane_count == 0) return false;
    const auto visible_slot = std::min(
        static_cast<std::size_t>((click_x - compose_measure_rail_width) / lane_width),
        visible_lane_count - 1);
    std::size_t clicked_timing_lane_ordinal = 0;
    for (std::size_t candidate = 0, slot = 0; candidate < WORM_UI_TIMING_LANE_COUNT;
         ++candidate) {
        if ((view.timing_lane_visibility_mask & (1U << candidate)) == 0) continue;
        if (slot++ == visible_slot) {
            clicked_timing_lane_ordinal = candidate;
            break;
        }
    }
    const auto* clicked_marker = compose_timing_marker_at(view, click_x, click_y);
    const auto existing_object_id = clicked_marker ? clicked_marker->object_id : 0;
    action.timing_stage_action = WORM_UI_TIMING_STAGE_BEGIN;
    action.timing_chart_id = timing_view.chart_id;
    action.timing_document_revision = timing_view.document_revision;
    action.timing_lane = static_cast<WormUiTimingLaneKind>(clicked_timing_lane_ordinal);
    action.timing_object_id = existing_object_id;
    if (existing_object_id == 0) {
        action.timing_has_target_y = true;
        const auto viewport_fraction =
            std::clamp(click_y / view.viewport_height_pixels, 0.0, 1.0);
        action.timing_target_y_fraction = clicked_marker && clicked_marker->fixed_initial
            ? 0.0
            : view.timing_document_begin_fraction +
                  viewport_fraction * (view.timing_document_end_fraction -
                                       view.timing_document_begin_fraction);
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

static bool process_compose_double_clicks(const ComposeDoubleClickContext& input,
                                          bool compose_pointer_captured,
                                          bool window_focused) {
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

struct ComposePointerEventContext {
    nk_context* context;
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    double editor_input_right;
    double pointer_x;
    double pointer_y;
    double note_field_left;
    double note_field_right;
    double pointer_started_x;
    double pointer_started_y;
    double paint_anchor_x;
    double paint_anchor_y;
    double paint_pointer_x;
    double paint_pointer_y;
    double left_clicked_screen_x;
    double left_clicked_screen_y;
    uint16_t pointer_modifiers;
    uint16_t any_pointer_modifier;
    std::size_t shortcut_event_count;
    bool window_focused;
    bool pointer_inside;
    bool pointer_clicked;
    bool pointer_in_note_field;
    bool pointer_started_in_view;
    bool pointer_started_in_note_field;
    bool left_down;
    bool left_released;
    bool alt_down;
    bool compose_pointer_captured;
    bool compose_note_double_click_in_view;
};

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
        const auto viewport_fraction =
            std::clamp(input.pointer_y / view.viewport_height_pixels, 0.0, 1.0);
        action.timing_stage_action = WORM_UI_TIMING_STAGE_MOVE;
        action.timing_chart_id = timing.chart_id;
        action.timing_document_revision = timing.document_revision;
        action.timing_object_id = timing.stage_existing_object_id;
        action.timing_lane = timing.stage_lane;
        action.timing_has_target_y = true;
        action.timing_target_y_fraction =
            view.timing_document_begin_fraction +
            viewport_fraction * (view.timing_document_end_fraction -
                                 view.timing_document_begin_fraction);
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

static void update_timing_stage_drag(const ComposePointerEventContext& input) {
    if (state.timing_stage_drag_active)
        update_active_timing_stage_drag(input);
    else
        start_timing_stage_drag(input);
}

static auto resource_pointer_can_start(const ComposePointerEventContext& input,
                                       bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    return !paint_pointer_consumed &&
           state.resource_pointer_mode != worm_ui_nuklear_compose_resource_pointer_mode_t::off &&
           view.scene_available && input.pointer_started_in_view && input.window_focused &&
           input.shortcut_event_count == 0 &&
           (input.pointer_modifiers & input.any_pointer_modifier) == 0 &&
           !input.compose_note_double_click_in_view && !input.compose_pointer_captured &&
           !state.note_move_candidate_active && !state.area_selection_candidate_active &&
           !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active;
}

static auto begin_compose_resource_pointer(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) -> bool {
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

static auto process_bmson_razor_pointer(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool {
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

static auto process_bmson_marker_move_pointer(const ComposePointerEventContext& input,
                                              bool paint_pointer_consumed) -> bool {
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

static auto process_semantic_layer_paint_pointer(const ComposePointerEventContext& input,
                                                 bool& paint_pointer_consumed) -> bool {
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

static void process_note_paint_pointer(const ComposePointerEventContext& input,
                                       bool& paint_pointer_consumed) {
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
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             state.area_selection_anchor_x, state.area_selection_anchor_y);
        state.area_selection_candidate_active = false;
    } else if (input.left_down) {
        constexpr float drag_threshold_pixels = 4.0f;
        const auto dx = input.context->input.mouse.pos.x - state.area_selection_anchor_screen_x;
        const auto dy = input.context->input.mouse.pos.y - state.area_selection_anchor_screen_y;
        if (std::hypot(dx, dy) >= drag_threshold_pixels) {
            append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                                 state.area_selection_anchor_x, state.area_selection_anchor_y,
                                 0, false, state.area_selection_mode);
            state.area_selection_candidate_active = false;
            state.area_selection_active = !state.action_overflowed;
            if (state.area_selection_active)
                append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                                     input.pointer_x, input.pointer_y);
        }
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

static void process_compose_drag_gestures(const ComposePointerEventContext& input,
                                          bool paint_pointer_consumed) {
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
        input.window_focused && !input.compose_note_double_click_in_view) {
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
    if (!marker) return false;
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

static auto process_compose_timing_marker_pointer(const ComposePointerEventContext& input,
                                                  bool paint_pointer_consumed) -> bool {
    if (update_compose_timing_marker_drag(input)) return true;
    return begin_compose_timing_marker_drag(input, paint_pointer_consumed);
}

static auto handle_compose_note_field_pointer_start(const ComposePointerEventContext& input,
                                                    bool paint_pointer_consumed) -> bool {
    const auto& view = *input.view;
    if (state.action_overflowed || paint_pointer_consumed || !view.scene_available ||
        !input.pointer_started_in_note_field || !input.window_focused || input.alt_down ||
        state.note_move_active || state.note_move_candidate_active || state.area_selection_active ||
        state.area_selection_candidate_active || input.compose_note_double_click_in_view)
        return false;
    if (compose_point_hits_selected_note(&view, input.pointer_x, input.pointer_y) &&
        input.left_down) {
        state.note_move_candidate_active = true;
        state.note_move_anchor_x = input.pointer_x;
        state.note_move_anchor_y = input.pointer_y;
        state.note_move_anchor_screen_x = input.context->input.mouse.pos.x;
        state.note_move_anchor_screen_y = input.context->input.mouse.pos.y;
    } else if (compose_point_hits_note(&view, input.pointer_x, input.pointer_y)) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             input.pointer_x, input.pointer_y);
    } else if (input.left_down) {
        state.area_selection_candidate_active = true;
        state.area_selection_anchor_x = input.pointer_x;
        state.area_selection_anchor_y = input.pointer_y;
        state.area_selection_anchor_screen_x = input.context->input.mouse.pos.x;
        state.area_selection_anchor_screen_y = input.context->input.mouse.pos.y;
        state.area_selection_mode = compose_selection_mode(input.pointer_modifiers);
    } else {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                             input.pointer_x, input.pointer_y);
    }
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
                         input.pointer_x, input.pointer_y);
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

static void process_compose_pointer_actions(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) {
    if (handle_compose_measure_click(input, paint_pointer_consumed)) return;
    if (handle_compose_alt_pointer(input, paint_pointer_consumed)) return;
    if (handle_compose_note_field_pointer_start(input, paint_pointer_consumed)) return;
    if (handle_compose_note_field_click(input, paint_pointer_consumed)) return;
    handle_compose_bgm_click(input, paint_pointer_consumed);
}

struct ComposePanelRenderContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
    bool window_focused;
    bool ime_composition_active;
    const worm_ui_nuklear_pointer_click_t* double_clicks;
    std::size_t double_click_count;
    std::size_t shortcut_event_count;
};

static void render_compose_inline_timing_editor(const ComposePanelRenderContext& panel,
                                                struct nk_rect viewport) {
    if (!state.inline_timing_open) return;
    const auto& timing = *panel.timing_view;
    if (!timing.stage_active) return;
    if (timing.chart_id != state.inline_timing_chart_id ||
        timing.document_revision != state.inline_timing_revision ||
        timing.stage_existing_object_id != state.inline_timing_object_id ||
        timing.stage_lane != state.inline_timing_lane) {
        state.inline_timing_open = false;
        state.inline_timing_focus_pending = false;
        return;
    }
    if (state.inline_timing_focus_pending)
        worm_ui_nuklear_inline_timing_initial_text(
            timing, state.inline_timing_text.data(), state.inline_timing_text.size());

    constexpr float popup_width = 196.0f;
    constexpr float popup_height = 75.0f;
    const auto popup_x = std::clamp(state.inline_timing_anchor_screen_x,
                                    viewport.x, viewport.x + std::max(0.0f, viewport.w - popup_width));
    const auto popup_y = std::clamp(state.inline_timing_anchor_screen_y - 24.0f,
                                    viewport.y,
                                    viewport.y + std::max(0.0f, viewport.h - popup_height));
    const auto parent_clip = nk_window_get_content_region(panel.context);
    const auto popup_rect = nk_rect(popup_x - parent_clip.x, popup_y - parent_clip.y,
                                    popup_width, popup_height);
    if (!nk_popup_begin(panel.context, NK_POPUP_STATIC, "Compose timing value",
                        NK_WINDOW_NO_SCROLLBAR, popup_rect)) {
        state.inline_timing_open = false;
        state.inline_timing_focus_pending = false;
        panel.action->timing_stage_action = WORM_UI_TIMING_STAGE_CANCEL;
        return;
    }
    nk_layout_row_dynamic(panel.context, 25.0f, 1);
    const auto edit_flags = nk_edit_string_zero_terminated(
        panel.context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER |
                           NK_EDIT_AUTO_SELECT,
        state.inline_timing_text.data(), static_cast<int>(state.inline_timing_text.size()),
        nk_filter_default);
    if (state.inline_timing_focus_pending) {
        nk_edit_focus(panel.context, NK_EDIT_ALWAYS_INSERT_MODE);
        state.inline_timing_focus_pending = false;
    }
    if ((edit_flags & NK_EDIT_COMMITTED) != 0) {
        if (worm_ui_nuklear_inline_timing_confirm(timing, state.inline_timing_text.data(),
                                                   *panel.action)) {
            state.inline_timing_open = false;
            state.inline_timing_focus_pending = false;
            nk_popup_close(panel.context);
        } else {
            std::snprintf(state.inline_timing_error.data(), state.inline_timing_error.size(),
                          "Enter a valid timing value");
            nk_edit_focus(panel.context, NK_EDIT_ALWAYS_INSERT_MODE);
        }
    }
    nk_layout_row_dynamic(panel.context, 17.0f, 1);
    nk_label(panel.context, state.inline_timing_error[0] ? state.inline_timing_error.data()
                                                    : "Enter to apply · Esc to cancel",
             NK_TEXT_LEFT);
    nk_popup_end(panel.context);
}

static void render_compose_editor_canvas(const ComposePanelRenderContext& panel,
                                        bool inspector_drawn, float inspector_width) {
    auto* context = panel.context;
    const auto* view = panel.view;
    const auto* timing_view = panel.timing_view;
    auto* action = panel.action;
    const auto window_focused = panel.window_focused;
    const auto ime_composition_active = panel.ime_composition_active;
    const auto* double_clicks = panel.double_clicks;
    const auto double_click_count = panel.double_click_count;
    const auto shortcut_event_count = panel.shortcut_event_count;

    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, compose_editor_status_hint(view, timing_view), NK_TEXT_LEFT);

    const bool overview_available = view->waveform_overview_row_count != 0;
    const auto group_bounds = nk_window_get_content_region(context);
    render_compose_offset_controls({context, view, action, nullptr, {}});
    const auto surface_height =
        std::max(32.0f, group_bounds.h - (overview_available ? 52.0f : 24.0f));
    const auto overview_fraction =
        overview_available ? std::min(0.08f, 26.0f / std::max(group_bounds.w, 1.0f)) : 0.0f;
    nk_layout_space_begin(context, NK_DYNAMIC, surface_height, overview_available ? 2 : 1);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, 1.0f - overview_fraction, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, context);
    struct nk_rect overview_rect{};
    if (overview_available) {
        nk_layout_space_push(context,
                             nk_rect(1.0f - overview_fraction, 0.0f, overview_fraction, 1.0f));
        nk_widget(&overview_rect, context);
    }
    nk_layout_space_end(context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f)
        return;

    auto* canvas = nk_window_get_canvas(context);
    if (!canvas)
        return;

    render_compose_waveform_overview({context, view, action, canvas, overview_rect});

    const auto editor_input_right = inspector_drawn
        ? viewport.x + viewport.w - inspector_width - 8.0f
        : viewport.x + viewport.w;
    const auto pointer_focus = update_compose_pointer_focus(
        {context, view, action, viewport, overview_rect, editor_input_right, overview_available,
         window_focused});
    const auto pointer_inside = pointer_focus.pointer_inside;
    const auto pointer_clicked = pointer_focus.pointer_clicked;
    const auto overview_pressed = pointer_focus.overview_pressed;
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    process_compose_key_events(context, view, action, window_focused, ime_composition_active);
    process_compose_overview_input({context, view, action, overview_rect, overview_available,
                                    overview_pressed, window_focused,
                                    state.waveform_beat_zero_drag_active});

    const ComposeViewportInputContext viewport_input{
        context, view, action, viewport, editor_input_right, pointer_inside, window_focused};
    const auto pointer = collect_compose_viewport_inputs(viewport_input);
    const auto pointer_x = pointer.x;
    const auto pointer_y = pointer.y;
    const auto note_field_left = pointer.note_field_left;
    const auto note_field_right = pointer.note_field_right;
    const auto pointer_in_note_field = pointer.in_note_field;
    const bool alt_down = nk_input_is_key_down(&context->input, NK_KEY_ALT) != 0;
    const bool left_down = context->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    const auto pointer_modifiers = static_cast<uint16_t>(SDL_GetModState());
    constexpr uint16_t any_pointer_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                              SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
    const auto pointer_started_in_view = left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x < editor_input_right &&
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
    const ComposeBeatFitInput beat_fit_input{
        context, view, action, viewport, pointer_y, pointer_started_x, pointer_started_y,
        pointer_modifiers, any_pointer_modifier, shortcut_event_count, window_focused,
        pointer_started_in_view, left_down, left_released};
    const bool beat_fit_pointer_consumed = process_compose_beat_fit_pointer(beat_fit_input);
    ComposePointerEventContext pointer_event{
        context, view, timing_view, action, viewport, editor_input_right, pointer_x, pointer_y,
        note_field_left, note_field_right, pointer_started_x, pointer_started_y,
        paint_anchor_x, paint_anchor_y, paint_pointer_x, paint_pointer_y,
        left_button.clicked_pos.x, left_button.clicked_pos.y,
        pointer_modifiers, any_pointer_modifier, shortcut_event_count, window_focused,
        pointer_inside, pointer_clicked, pointer_in_note_field,
        pointer_started_in_view, pointer_started_in_note_field,
        left_down, left_released, alt_down, false, false};
    if (!state.inline_timing_open && !state.timing_marker_candidate_active)
        update_timing_stage_drag(pointer_event);
    const auto compose_pointer_captured =
        state.inline_timing_open || state.note_paint_pointer_active ||
        state.bmson_razor_pointer_active ||
        state.bmson_marker_move_active || state.semantic_layer_paint_pointer_active ||
        state.pointer_hold_active || state.lane_hold_active || state.note_move_active ||
        state.area_selection_active || state.timing_stage_drag_active ||
        state.timing_marker_candidate_active || state.timing_marker_drag_active ||
        state.waveform_beat_zero_drag_active || beat_fit_pointer_consumed;
    action->compose_has_pointer_hover = !state.action_overflowed && window_focused &&
                                        view->scene_available && pointer_in_note_field &&
                                        !compose_pointer_captured;
    if (action->compose_has_pointer_hover) {
        action->compose_pointer_hover_x_pixels = pointer_x;
        action->compose_pointer_hover_y_pixels = pointer_y;
    }
    const bool compose_note_double_click_in_view = process_compose_double_clicks(
        {view, timing_view, action, double_clicks, double_click_count, viewport,
         editor_input_right, note_field_left, note_field_right},
        compose_pointer_captured, window_focused);
    pointer_event.compose_pointer_captured = compose_pointer_captured;
    pointer_event.compose_note_double_click_in_view = compose_note_double_click_in_view;
    bool paint_pointer_consumed =
        state.inline_timing_open || state.note_paint_pointer_active ||
        beat_fit_pointer_consumed ||
        state.bmson_razor_pointer_active || state.bmson_marker_move_candidate_active ||
        state.bmson_marker_move_active || state.semantic_layer_paint_pointer_active ||
        compose_action_has_cancel(action);
    paint_pointer_consumed = process_compose_timing_marker_pointer(pointer_event,
                                                                    paint_pointer_consumed) ||
                             paint_pointer_consumed;
    paint_pointer_consumed = begin_compose_resource_pointer(pointer_event, paint_pointer_consumed) ||
                             paint_pointer_consumed;
    paint_pointer_consumed = process_bmson_razor_pointer(pointer_event, paint_pointer_consumed);
    paint_pointer_consumed = process_bmson_marker_move_pointer(pointer_event, paint_pointer_consumed);
    if (!process_semantic_layer_paint_pointer(pointer_event, paint_pointer_consumed))
        process_note_paint_pointer(pointer_event, paint_pointer_consumed);

    process_compose_pointer_actions(pointer_event, paint_pointer_consumed);
    process_compose_drag_gestures(pointer_event, paint_pointer_consumed);

    draw_compose_scene_canvas(context, view, canvas, viewport);
    render_compose_inline_timing_editor(panel, viewport);
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
    render_compose_toolbar({context, view, action});
    const ComposePanelRenderContext panel{context, view, timing_view, action, window_focused,
                                         ime_composition_active, double_clicks,
                                         double_click_count, shortcut_event_count};

    const auto compose_body_bounds = nk_window_get_content_region(context);
    const auto body_top = nk_widget_position(context).y;
    const auto compose_body_height = std::max(
        64.0f, compose_body_bounds.y + compose_body_bounds.h - body_top -
                   context->style.window.spacing.y);
    const auto inspector_drawn = state.inspector_visible && compose_body_bounds.w >= 540.0f;
    const auto inspector_width = inspector_drawn
                                     ? std::clamp(compose_body_bounds.w * 0.24f, 280.0f, 380.0f)
                                     : 0.0f;
    const auto editor_width = compose_body_bounds.w;
    nk_layout_space_begin(context, NK_STATIC, compose_body_height, inspector_drawn ? 2 : 1);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, editor_width, compose_body_height));
    if (nk_group_begin(context, "Compose editor", NK_WINDOW_NO_SCROLLBAR)) {
    render_compose_editor_canvas(panel, inspector_drawn, inspector_width);
    nk_group_end(context);
    }
    if (inspector_drawn) {
        nk_layout_space_push(context, nk_rect(editor_width - inspector_width - 8.0f, 8.0f,
                                              inspector_width, compose_body_height - 16.0f));
        if (nk_group_begin(context, "Compose inspector", NK_WINDOW_BORDER)) {

    render_compose_timing_inspector({context, view, timing_view, action});

    const ComposeInspectorContext inspector{context, view, timing_view, action};
    render_compose_note_label_editor(inspector);

    const auto visible_layer_count = view->semantic_layer_count;
    const bool semantic_target_active = semantic_layer_target_active(view);
    if (render_semantic_layer_palette_panel(inspector)) {
        render_semantic_layer_selected_panel(inspector, visible_layer_count);
        render_semantic_layer_membership_buttons(inspector, semantic_target_active);
        render_semantic_layer_target_buttons(inspector, semantic_target_active);
        nk_tree_pop(context);
    }

    worm_ui_nuklear_render_bms_branch_panel(context, view, action);
    nk_group_end(context);
        }
    }
    nk_layout_space_end(context);
}

bool worm_ui_nuklear_compose_lane_focused(const WormUiComposeView* view, bool compose_tab_active,
                                          bool window_focused) {
    return view && window_focused && compose_tab_active && state.keyboard_focus && view->visible &&
           view->scene_available && view->cursor && view->cursor->lane_id != 0;
}
