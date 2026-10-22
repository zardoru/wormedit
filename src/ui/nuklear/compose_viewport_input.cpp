#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/theme.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "ui/nuklear/project/bms_branch_panel.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/compose_recipe_menu.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_pointer_chords.h"
#include "ui/nuklear/compose_measure_resize_input.h"
#include "ui/nuklear/timing/timing_stage_editor.h"

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
#include "ui/nuklear/compose_viewport_input.h"
#include "ui/nuklear/compose_note_menu.h"

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
        state.input_queue.keyboard_focus = true;
    } else if (nk_input_any_mouse_click_in_rect(
                   &context.input, nk_rect(0.0f, 0.0f, 1000000.0f, 1000000.0f)) &&
               !pointer_inside && !nk_input_is_mouse_hovering_rect(&context.input, focus.overview)) {
        state.input_queue.keyboard_focus = false;
        if (!state.keyboard_navigation.pointer_navigation_active) {
            append_compose_input(focus.action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
        }
        if (state.range_selection.space_range_hold_active) {
            append_compose_cancel_once(focus.action);
            state.range_selection.space_range_hold_active = false;
            state.range_selection.space_range_hold_moved = false;
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
    if (!state.input_queue.action_overflowed && input.window_focused && view.scene_available) {
        input.action->compose_viewport_pointer_valid = true;
        input.action->compose_viewport_pointer_x_pixels = x;
        input.action->compose_viewport_pointer_y_pixels = y;
        if (!compose_area_capture_active() && state.input_queue.middle_click_pending && state.input_queue.middle_click_x >= input.viewport.x &&
            state.input_queue.middle_click_x < input.input_right && state.input_queue.middle_click_y >= input.viewport.y &&
            state.input_queue.middle_click_y <= input.viewport.y + input.viewport.h) {
            input.action->compose_viewport_middle_click = true;
            input.action->compose_viewport_middle_click_x_pixels =
                (static_cast<double>(state.input_queue.middle_click_x) - input.viewport.x) *
                view.viewport_width_pixels / input.viewport.w;
            input.action->compose_viewport_middle_click_y_pixels =
                (static_cast<double>(state.input_queue.middle_click_y) - input.viewport.y) *
                view.viewport_height_pixels / input.viewport.h;
        }
    }
    state.input_queue.middle_click_pending = false;
    return {x, y, note_left, note_right,
            input.pointer_inside && x >= note_left && x < note_right};
}

static bool append_beat_fit_snap_wheel(const ComposeViewportInputContext& input,
                                       const worm_ui_nuklear_compose_wheel_event_t& wheel) {
    if (!state.waveform_drag.beat_fit_drag_active || (wheel.modifiers & SDL_KMOD_CTRL) == 0) return false;
    if ((wheel.modifiers & SDL_KMOD_ALT) != 0) return true;
    // Compose wheel deltas are positive toward the song end: wheel down picks a finer step.
    const auto step = wheel.delta_y > 0.0 ? 1 : wheel.delta_y < 0.0 ? -1 : 0;
    const auto next = static_cast<int>(state.waveform_drag.beat_fit_snap_step_index) + step;
    const auto updated = static_cast<std::uint8_t>(
        std::clamp(next, 0, static_cast<int>(beat_fit_snap_steps.size() - 1)));
    if (updated != state.waveform_drag.beat_fit_snap_step_index) {
        state.waveform_drag.beat_fit_snap_step_index = updated;
        const auto y = (wheel.mouse_y - input.viewport.y) * input.view->viewport_height_pixels /
                       input.viewport.h;
        append_beat_fit_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, y, true);
    }
    return true;
}

static bool append_alt_wheel_step(const ComposeViewportInputContext& input,
                                   const worm_ui_nuklear_compose_wheel_event_t& wheel) {
    const auto alt = (wheel.modifiers & SDL_KMOD_ALT) != 0 && !compose_wheel_zooms(wheel.modifiers);
    const auto step = worm_ui_nuklear_alt_wheel_step(state, wheel.delta_y, alt, SDL_GetTicks());
    if (!alt) return false;
    if (step == 0) return true;
    const auto chart_delta = wormedit::sys::compose_wheel_scroll_delta(step, input.view->downscroll);
    append_compose_input(input.action, chart_delta > 0
        ? WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD : WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD);
    return true;
}

static void append_compose_wheel_inputs(const ComposeViewportInputContext& input) {
    const auto& view = *input.view;
    if (!state.input_queue.action_overflowed && view.scene_available && !view.input_occluded &&
        input.window_focused) {
        for (size_t index = 0; index < state.input_queue.wheel_event_count; ++index) {
            const auto& wheel = state.input_queue.wheel_events[index];
            if (wheel.mouse_x < input.viewport.x || wheel.mouse_x >= input.input_right ||
                wheel.mouse_y < input.viewport.y || wheel.mouse_y > input.viewport.y + input.viewport.h) {
                continue;
            }
            if (append_beat_fit_snap_wheel(input, wheel)) continue;
            if (append_alt_wheel_step(input, wheel)) continue;
            const auto zoom_modifier = compose_wheel_zooms(wheel.modifiers);
            if (input.action->compose_viewport_inputs.size() >= wormedit::ui::compose_viewport_input_admission_limit)
                break;
            const auto visual_y = (wheel.mouse_y - input.viewport.y) * view.viewport_height_pixels /
                                  input.viewport.h;
            input.action->compose_viewport_inputs.emplace_back() = {
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
    state.input_queue.wheel_event_count = 0;
}

static void append_compose_pinch_inputs(const ComposeViewportInputContext& input,
                                        bool competing_capture) {
    const auto& view = *input.view;
    if (competing_capture || !input.context || !view.scene_available || view.input_occluded ||
        !input.window_focused || compose_text_owned(input.context) || state.inline_timing.inline_timing_open ||
        state.dialogs.go_to_measure_open || state.pinch.pinch.project_id != view.project_id.value ||
        state.pinch.pinch.chart_id != view.chart_id.value || !(input.viewport.w > 0) || !(input.viewport.h > 0)) {
        state.pinch.pinch = {};
        return;
    }
    for (std::size_t index = 0; index < state.pinch.pinch.count; ++index) {
        const auto& update = state.pinch.pinch.pending[index];
        if (update.mouse_x < input.viewport.x || update.mouse_x >= input.input_right ||
            update.mouse_y < input.viewport.y || update.mouse_y > input.viewport.y + input.viewport.h) {
            state.pinch.pinch = {};
            return;
        }
        if (input.action->compose_viewport_inputs.size() >= wormedit::ui::compose_viewport_input_admission_limit) {
            state.pinch.pinch = {};
            return;
        }
        const auto visual_y = (update.mouse_y - input.viewport.y) * view.viewport_height_pixels /
                              input.viewport.h;
        input.action->compose_viewport_inputs.emplace_back() = {
            .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM,
            .cursor_x_pixels = (update.mouse_x - input.viewport.x) * view.viewport_width_pixels /
                               input.viewport.w,
            .cursor_y_pixels = worm_ui_nuklear_compose_zoom_anchor(visual_y,
                                   view.viewport_height_pixels, view.downscroll),
            .zoom_factor = update.factor,
        };
    }
    state.pinch.pinch.count = 0;
}

ComposeViewportCursor collect_compose_viewport_inputs(
    const ComposeViewportInputContext& input) {
    const bool competing_capture = state.range_selection.space_range_hold_active || input.view->space_range_preview_visible || state.pointer_capture.pointer_hold_active || state.range_selection.lane_hold_active ||
        state.pointer_capture.note_paint_pointer_active || state.note_move.note_move_active || state.note_move.note_move_candidate_active ||
        state.area_selection.area_selection_active || state.area_selection.area_selection_candidate_active ||
        state.bmson_input.bmson_razor_pointer_active || state.bmson_input.bmson_marker_move_active ||
        state.bmson_input.bmson_marker_move_candidate_active || state.pointer_capture.semantic_layer_paint_pointer_active ||
        state.inline_timing.inline_timing_open || state.timing_drag.timing_stage_drag_active || state.timing_drag.kiai_drag_active ||
        state.timing_drag.recipe_drag_active || compose_recipe_menu_open() ||
        state.timing_marker.timing_marker_candidate_active || state.timing_marker.timing_marker_drag_active ||
        state.waveform_drag.beat_fit_drag_active || state.waveform_drag.offset_fit_drag_active ||
        state.waveform_drag.waveform_beat_zero_drag_active || state.waveform_drag.waveform_overview_drag_active;
    if (!competing_capture && !compose_area_capture_active())
        process_compose_note_menu(input.context, *input.view, *input.action, input.viewport,
                                  input.input_right, input.window_focused);
    if (compose_note_menu_consumed()) return update_compose_viewport_pointer(input);
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
           view.waveform_rows.size() != 0 && view.waveform_overview_sample_rate != 0 &&
           !view.waveform_timing_locked && !view.waveform_offset_drag_active &&
           !state.waveform_drag.beat_fit_drag_active && !state.waveform_drag.waveform_beat_zero_drag_active;
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
           view.waveform_rows.size() != 0 && view.waveform_overview_sample_rate != 0 &&
           !view.waveform_timing_locked && !view.audio_offset_locked && !view.waveform_offset_drag_active &&
           !state.waveform_drag.offset_fit_drag_active && !state.waveform_drag.waveform_beat_zero_drag_active;
}

static std::uint64_t nearest_beat_fit_row(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    std::uint64_t nearest_row = 0;
    auto nearest_distance = 6.0 * view.viewport_height_pixels / input.viewport.h;
    for (std::size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto& line = view.grid_lines[index];
        if (!worm_ui_nuklear_beat_fit_line(line)) {
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

static bool start_compose_fit_drag(const ComposeBeatFitInput& input, bool offset) {
    if (!(offset ? compose_offset_fit_start_allowed(input) : compose_beat_fit_start_allowed(input)))
        return false;
    const auto nearest_row = nearest_beat_fit_row(input);
    if (nearest_row == 0) return false;
    const auto& view = *input.view;
    const auto& press = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos;
    if (!state.timing_drag.fit_capture.begin(view, press.x, press.y,
                                 view.waveform_timing_locked || (offset && view.audio_offset_locked), true))
        return false;
    state.waveform_drag.beat_fit_drag_active = !offset;
    state.waveform_drag.offset_fit_drag_active = offset;
    state.waveform_drag.beat_fit_ctrl_held = state.waveform_drag.beat_fit_onset_snap_held = false;
    state.waveform_drag.beat_fit_chart_row = nearest_row;
    append_compose_input(input.action, offset ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN
                                             : WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN,
                         0.0, input.pointer_started_y, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, nearest_row);
    return true;
}

static void cancel_unmatched_view_beat_fit(const ComposeBeatFitInput& input) {
    const auto cancel_pending = std::ranges::any_of(
        input.action->compose_inputs.begin(), input.action->compose_inputs.end(),
        [](const auto& event) { return event.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL; });
    if (!cancel_pending) {
        append_compose_input(input.action, WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
    }
}

static void emit_fit_motion(const ComposeBeatFitInput& input, bool offset, bool release) {
    const auto y = std::clamp(input.pointer_y, 0.0, input.view->viewport_height_pixels);
    state.waveform_drag.beat_fit_ctrl_held = !offset && (input.pointer_modifiers & SDL_KMOD_CTRL) != 0;
    state.waveform_drag.beat_fit_onset_snap_held = state.waveform_drag.beat_fit_ctrl_held &&
                                    (input.pointer_modifiers & SDL_KMOD_ALT) != 0;
    if (offset)
        append_compose_input(input.action, release ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE
                                                  : WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 0.0, y);
    else
        append_beat_fit_input(input.action, release ? WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE
                                                   : WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE,
                             y, state.waveform_drag.beat_fit_ctrl_held, state.waveform_drag.beat_fit_onset_snap_held);
}

static void update_active_fit_drag(const ComposeBeatFitInput& input, bool offset) {
    const auto& view = *input.view;
    const auto runtime_active = offset ? view.offset_fit_drag_active : view.beat_fit_drag_active;
    const auto matches = input.window_focused && runtime_active && !state.input_queue.action_overflowed &&
        state.timing_drag.fit_capture.matches(view, view.waveform_timing_locked || (offset && view.audio_offset_locked));
    const auto& pointer = input.context->input.mouse.pos;
    // Fits use the release position rather than a movement latch; returning to
    // the anchor cancels even after an earlier excursion.
    const auto continuation = state.timing_drag.fit_capture.advance(
        {.down = input.left_down, .released = input.left_released, .x = pointer.x, .y = pointer.y},
        matches, compose_drag_release_policy_t::vertical_at_release);
    if (continuation.phase == compose_drag_phase_t::cancel) {
        if (offset) cancel_offset_fit_drag(input.action);
        else cancel_beat_fit_drag(input.action);
        return;
    }
    const auto ctrl = !offset && (input.pointer_modifiers & SDL_KMOD_CTRL) != 0;
    const auto onset = ctrl && (input.pointer_modifiers & SDL_KMOD_ALT) != 0;
    if (continuation.phase == compose_drag_phase_t::commit || nk_input_is_mouse_moved(&input.context->input) ||
        ctrl != state.waveform_drag.beat_fit_ctrl_held || onset != state.waveform_drag.beat_fit_onset_snap_held)
        emit_fit_motion(input, offset, continuation.phase == compose_drag_phase_t::commit);
    if (continuation.phase == compose_drag_phase_t::commit) {
        state.waveform_drag.beat_fit_drag_active = state.waveform_drag.offset_fit_drag_active = false;
        state.timing_drag.fit_capture.cancel();
    }
}

bool process_compose_beat_fit_pointer(const ComposeBeatFitInput& input) {
    if (input.view->paste_active || compose_area_pointer_consumed()) return true;
    if (state.waveform_drag.offset_fit_drag_active || state.waveform_drag.beat_fit_drag_active) {
        update_active_fit_drag(input, state.waveform_drag.offset_fit_drag_active);
        return true;
    }
    if (input.view->beat_fit_drag_active) {
        cancel_unmatched_view_beat_fit(input);
        return false;
    }
    if (process_compose_measure_resize_pointer(input)) return true;
    return start_compose_fit_drag(input, (input.pointer_modifiers & SDL_KMOD_SHIFT) != 0);
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
        input.context->input.mouse.pos.x, state.timing_drag.beat_zero_capture.anchor_x,
        state.waveform_drag.waveform_beat_zero_drag_width,
        overview_window_frames(state.waveform_drag.waveform_beat_zero_drag_total_frames,
                               state.waveform_drag.waveform_beat_zero_drag_span));
}

static void start_waveform_beat_zero_drag(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto zero_x = overview_window_x(input, view.waveform_beat_zero_fraction);
    if (state.input_queue.action_overflowed || !input.available || !input.pressed ||
        !input.window_focused || !view.scene_available ||
        !view.waveform_beat_zero_visible || view.waveform_timing_locked || view.audio_offset_locked ||
        view.waveform_offset_drag_active ||
        input.action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE ||
        !zero_x || std::abs(static_cast<double>(left_button.clicked_pos.x) - *zero_x) > 7.0) {
        return;
    }
    if (!state.timing_drag.beat_zero_capture.begin(view, left_button.clicked_pos.x, left_button.clicked_pos.y,
                                       view.waveform_timing_locked || view.audio_offset_locked, true)) return;
    state.waveform_drag.waveform_overview_drag_active = false;
    state.waveform_drag.waveform_beat_zero_drag_active = true;
    state.waveform_drag.waveform_beat_zero_drag_width = input.overview.w;
    state.waveform_drag.waveform_beat_zero_drag_left = input.overview.x;
    state.waveform_drag.waveform_beat_zero_drag_span = input.window_end - input.window_begin;
    state.waveform_drag.waveform_beat_zero_drag_total_frames = view.waveform_overview_total_frames;
    state.waveform_drag.waveform_beat_zero_drag_delta_frames = 0;
    set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_BEGIN, 0);
}

static void update_waveform_beat_zero_drag(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto released = nk_input_is_mouse_released(&input.context->input, NK_BUTTON_LEFT);
    if (!state.waveform_drag.waveform_beat_zero_drag_active) {
        start_waveform_beat_zero_drag(input);
        return;
    }
    const bool matches =
        input.available && input.window_focused &&
        state.timing_drag.beat_zero_capture.matches(view, view.waveform_timing_locked || view.audio_offset_locked) &&
        view.waveform_overview_total_frames == state.waveform_drag.waveform_beat_zero_drag_total_frames &&
        view.waveform_overview_sample_rate == state.timing_drag.beat_zero_capture.sample_rate &&
        input.overview.w == state.waveform_drag.waveform_beat_zero_drag_width &&
        input.overview.x == state.waveform_drag.waveform_beat_zero_drag_left &&
        (view.waveform_offset_drag_active || input.pressed);
    const auto& pointer = input.context->input.mouse.pos;
    const auto continuation = state.timing_drag.beat_zero_capture.advance(
        {.down = left_button.down != 0, .released = released != 0, .x = pointer.x, .y = pointer.y},
        matches, compose_drag_release_policy_t::horizontal_at_release);
    if (continuation.phase == compose_drag_phase_t::cancel) {
        // A valid click cancels with the current release delta. Invalid source or
        // a lost button cancels with the last published delta instead.
        const auto delta = matches && released ? waveform_offset_drag_delta(input)
                                               : state.waveform_drag.waveform_beat_zero_drag_delta_frames;
        set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL, delta);
        reset_waveform_offset_drag();
    } else if (continuation.phase == compose_drag_phase_t::commit) {
        set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_COMMIT,
                                        waveform_offset_drag_delta(input));
        reset_waveform_offset_drag();
    } else if (nk_input_is_mouse_moved(&input.context->input)) {
        const auto delta = waveform_offset_drag_delta(input);
        state.waveform_drag.waveform_beat_zero_drag_delta_frames = delta;
        set_waveform_offset_drag_action(input.action, WORM_UI_AUDIO_OFFSET_DRAG_UPDATE, delta);
    }
}

static void update_waveform_overview_seek(const ComposeOverviewInput& input) {
    const auto& view = *input.view;
    const auto& left_button = input.context->input.mouse.buttons[NK_BUTTON_LEFT];
    if (input.available && !state.input_queue.action_overflowed && !input.beat_zero_was_active &&
        !state.waveform_drag.waveform_beat_zero_drag_active && input.pressed &&
        input.window_focused &&
        view.scene_available && !view.waveform_offset_drag_active &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        state.waveform_drag.waveform_overview_drag_active = true;
        state.waveform_drag.waveform_overview_capture = {
            view.chart_id.value, view.document_revision, view.selected_audio_asset_id.value,
            view.waveform_overview_total_frames, view.waveform_overview_sample_rate,
            input.overview.x, input.overview.w, input.window_begin, input.window_end,
            input.window_capture, view.project_id.value};
    }
    if (state.waveform_drag.waveform_overview_drag_active &&
        state.waveform_drag.waveform_overview_capture.window_capture != input.window_capture)
        return;
    const bool valid = worm_ui_nuklear_overview_capture_matches(
            state.waveform_drag.waveform_overview_capture, view, input.overview.x, input.overview.w,
            input.available, input.window_focused) &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE &&
        !view.waveform_offset_drag_active;
    const auto event = worm_ui_nuklear_overview_gesture(state.waveform_drag.waveform_overview_gesture,
        input.context->input.mouse.pos.x, input.pressed && state.waveform_drag.waveform_overview_drag_active,
        left_button.down, valid);
    state.waveform_drag.waveform_overview_drag_active = state.waveform_drag.waveform_overview_gesture.active;
    if ((event == worm_ui_nuklear_overview_event_t::scroll &&
         !nk_input_is_mouse_moved(&input.context->input)) ||
        event == worm_ui_nuklear_overview_event_t::none ||
        input.action->compose_inputs.size() >= wormedit::ui::compose_input_admission_limit) return;
    append_compose_input(input.action, event == worm_ui_nuklear_overview_event_t::click
        ? WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_CLICK : WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK);
    auto& emitted = input.action->compose_inputs[input.action->compose_inputs.size() - 1];
    const auto captured_window = worm_ui_nuklear_overview_window_for_capture(
        state.waveform_drag.waveform_overview_capture, state.waveform_drag.waveform_overview_capture.window_capture &&
            (state.waveform_drag.waveform_overview_drag_active ||
             event != worm_ui_nuklear_overview_event_t::none),
        input.window_begin, input.window_end);
    emitted.audio_fraction = captured_window.begin +
        worm_ui_nuklear_overview_fraction(input.context->input.mouse.pos.x,
            input.overview.x, input.overview.w).value_or(0.0) *
        (captured_window.end - captured_window.begin);
    const auto modifiers = state.input_queue.pointer_modifiers;
    emitted.overview_shift = (modifiers & SDL_KMOD_SHIFT) != 0;
#ifdef __APPLE__
    emitted.overview_primary = (modifiers & SDL_KMOD_GUI) != 0;
#else
    emitted.overview_primary = (modifiers & SDL_KMOD_CTRL) != 0;
#endif
}

void process_compose_overview_input(const ComposeOverviewInput& input) {
    if (input.available && input.window_focused && input.pressed &&
        input.view->scene_available && !state.input_queue.action_overflowed)
        state.input_queue.keyboard_focus = true;
    // An orphaned offset preview is cancelled unless the rail offset fit owns it.
    if (input.view->waveform_offset_drag_active && !state.waveform_drag.waveform_beat_zero_drag_active &&
        !state.waveform_drag.offset_fit_drag_active &&
        input.action->audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        input.action->audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
    }
    update_waveform_beat_zero_drag(input);
    update_waveform_overview_seek(input);
}

// Document fraction under a screen-space y. The document fractions run in source order, so a
// downscroll field (source row 0 at the bottom) reads the viewport from the other end.
