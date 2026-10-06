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
#include "ui/nuklear/compose_measure_resize_input.h"
#include "ui/nuklear/compose_area_menu.h"

void queue_compose_key_input(std::uint32_t keycode, std::uint32_t scancode,
                                       std::uint16_t modifiers, bool down, bool repeat) {
    if (state.cancel_pending) {
        return;
    }
    if (!down && (modifiers & SDL_KMOD_ALT) == 0)
        state.alt_wheel_last_event_ms.reset();
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
        state.space_range_latched = false;
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
        state.space_range_latched = false;
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
        state.bga_event_move_candidate_active = false;
        state.bga_event_move_active = false;
        state.bga_event_move_object_id = 0;
        state.bookmark_move_candidate_active = false;
        state.bookmark_move_active = false;
        state.bookmark_move_id = 0;
        state.bga_stack_open = false;
        cancel_beat_fit_drag(action);
        cancel_compose_measure_resize();
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
            state.space_range_latched = false;
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
        if (!state.action_overflowed && action->compose_input_count > 0)
            action->compose_inputs[action->compose_input_count - 1].resource_id =
                view->selected_note_count != 0
                    ? 0 : worm_ui_nuklear_armed_dock_sound(state, *view);
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
    if (state.space_range_hold_active) {
        append_compose_cancel_once(action);
        state.space_range_hold_active = false;
        state.space_range_latched = false;
        state.space_range_hold_moved = false;
    }
    if (state.note_move_candidate_active || state.note_move_active) {
        if (state.note_move_active) append_compose_cancel_once(action);
        state.note_move_candidate_active = false;
        state.note_move_active = false;
    }
}

static bool process_compose_latched_range(const auto& event, const WormUiComposeView& view,
                                          WormUiWorkspaceAction* action,
                                          bool pointer_paint_mode_active) {
    if (event.keycode != static_cast<std::uint32_t>(SDLK_SPACE) ||
        (event.modifiers & SDL_KMOD_CTRL) == 0 ||
        (event.modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI |
                            SDL_KMOD_MODE | SDL_KMOD_LEVEL5)) != 0) return false;
    if (view.paste_active || pointer_paint_mode_active) return true;
    append_compose_input(action, WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION);
    const auto starting = !state.space_range_hold_active;
    state.space_range_hold_active = starting && !state.action_overflowed;
    state.space_range_latched = state.space_range_hold_active;
    state.space_range_hold_moved = false;
    state.shift_range_keys = 0;
    return true;
}

static void process_compose_key_event(const auto& key_event, const WormUiComposeView* view,
                                      WormUiWorkspaceAction* action, bool keyboard_ready,
                                      bool pointer_paint_mode_active) {
    if (release_compose_key_hold(key_event, action)) return;
    if (key_event.down && state.space_range_hold_active && !state.space_range_latched)
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
    if (process_compose_latched_range(key_event, *view, action, pointer_paint_mode_active)) return;
    if (compose_shortcut_modifier(key_event.modifiers)) return;
    if (process_compose_tab_key(key_event, *view, action, pointer_paint_mode_active)) return;
    if (process_compose_horizontal_tab_key(key_event, *view, action, pointer_paint_mode_active)) return;
    if (process_compose_navigation_key(key_event, view, action, pointer_paint_mode_active)) return;
    if (view->paste_active || view->semantic_layer_paint_mode_active ||
        state.note_paint_pointer_active || state.semantic_layer_paint_pointer_active) return;
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

bool compose_text_owned(const nk_context* context) {
    if (state.bgm_column_count_edit.input_owned) return true;
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
    state.space_range_latched = false;
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
