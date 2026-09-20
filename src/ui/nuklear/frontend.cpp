#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#if defined(__linux__)
#define GL_GLEXT_PROTOTYPES
#endif

#include "ui/frontend.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/metadata_state.h"
#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/timing_panel.h"
#include "ui/nuklear/timing_state.h"

#include "wormedit/nuklear_config.h"

#include "nuklear_sdl_gl3_sdl3.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#define WORMEDIT_MAX_VERTEX_MEMORY (512 * 1024)
#define WORMEDIT_MAX_ELEMENT_MEMORY (128 * 1024)

static SDL_Window* g_window;
static struct nk_context* g_context;
static char g_recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
static auto& g_audio = worm_ui_nuklear_audio_state();
static auto& g_compose = worm_ui_nuklear_compose_state();

static std::array<worm_ui_nuklear_timing_double_click_t, 16> g_double_clicks{};
static size_t g_double_click_count{};
static bool g_window_focused{true};
static std::array<WormUiShortcutEvent, WORM_UI_SHORTCUT_EVENT_CAPACITY> g_shortcut_events{};
static size_t g_shortcut_event_count{};
static bool g_shortcut_events_overflowed{};
static bool g_ime_composition_active{};

static void append_compose_input(
    WormUiWorkspaceAction* action, WormUiComposeInputKind kind, double x_pixels = 0.0,
    double y_pixels = 0.0, uint64_t lane_id = 0, bool successive = false,
    WormUiComposeSelectionMode selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
    WormUiComposeNavigationAction navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
    uint64_t object_id = 0) {
    if (!action) {
        return;
    }
    if (action->compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        action->compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY - 1] = {
            .kind = WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
            .lane_id = 0,
            .object_id = 0,
            .successive = false,
            .selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
            .navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
            .x_pixels = 0.0,
            .y_pixels = 0.0,
        };
        action->compose_input_count = WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY;
        g_compose.cancel_pending = false;
        g_compose.pointer_hold_active = false;
        g_compose.note_paint_pointer_active = false;
        g_compose.semantic_layer_paint_pointer_active = false;
        g_compose.lane_hold_active = false;
        g_compose.space_range_hold_active = false;
        g_compose.space_range_hold_moved = false;
        g_compose.note_move_candidate_active = false;
        g_compose.note_move_active = false;
        g_compose.area_selection_candidate_active = false;
        g_compose.area_selection_active = false;
        g_compose.navigation_key_active = false;
        g_compose.navigation_keycode = 0;
        g_compose.pointer_navigation_active = false;
        g_compose.keyboard_focus = false;
        g_compose.lane_hold_keycode = 0;
        g_compose.lane_hold_lane_id = 0;
        g_compose.action_overflowed = true;
        return;
    }
    action->compose_inputs[action->compose_input_count++] = {
        .kind = kind,
        .lane_id = lane_id,
        .object_id = object_id,
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
    if (g_compose.pointer_navigation_active &&
        (!desired_action || *desired_action != g_compose.pointer_navigation_action)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE,
                             g_compose.pointer_navigation_action);
        g_compose.pointer_navigation_active = false;
    }
    if (desired_action && !g_compose.pointer_navigation_active) {
        if (g_compose.navigation_key_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 g_compose.navigation_action);
            g_compose.navigation_key_active = false;
            g_compose.navigation_keycode = 0;
        }
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE, *desired_action);
        g_compose.pointer_navigation_active = !g_compose.action_overflowed;
        g_compose.pointer_navigation_action = *desired_action;
    }
}

static void cancel_compose_navigation(WormUiWorkspaceAction* action) {
    if (action && (g_compose.navigation_key_active || g_compose.pointer_navigation_active)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
    }
    g_compose.navigation_key_active = false;
    g_compose.navigation_keycode = 0;
    g_compose.pointer_navigation_active = false;
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

static void queue_compose_key_event(const SDL_KeyboardEvent* event, bool down) {
    if (event && down && !event->repeat && event->key == SDLK_ESCAPE)
        g_audio.offset_escape_pending = true;
    if (!event || g_compose.cancel_pending) {
        return;
    }
    if (g_compose.key_event_count == g_compose.key_events.size()) {
        g_compose.key_event_count = 0;
        g_compose.cancel_pending = true;
        g_compose.pointer_hold_active = false;
        g_compose.note_paint_pointer_active = false;
        g_compose.semantic_layer_paint_pointer_active = false;
        g_compose.lane_hold_active = false;
        g_compose.space_range_hold_active = false;
        g_compose.space_range_hold_moved = false;
        g_compose.note_move_candidate_active = false;
        g_compose.note_move_active = false;
        g_compose.area_selection_candidate_active = false;
        g_compose.area_selection_active = false;
        g_compose.keyboard_focus = false;
        g_compose.lane_hold_keycode = 0;
        g_compose.lane_hold_lane_id = 0;
        return;
    }
    g_compose.key_events[g_compose.key_event_count++] = {
        .keycode = static_cast<uint32_t>(event->key),
        .scancode = static_cast<uint32_t>(event->scancode),
        .modifiers = static_cast<uint16_t>(event->mod),
        .down = down,
        .repeat = event->repeat,
    };
}

static void queue_shortcut_key_event(const SDL_KeyboardEvent* event) {
    if (!event || !g_window || !g_window_focused || event->type != SDL_EVENT_KEY_DOWN ||
        !event->down || event->windowID == 0 || event->windowID != SDL_GetWindowID(g_window) ||
        g_shortcut_events_overflowed) {
        return;
    }

    // SDL documents the usage-page-0x07 scancodes as the USB Keyboard/Keypad
    // usages used by command_registry. Exclude unknown, modifier usages, and
    // SDL's non-keyboard scancode pages instead of interpreting keycode text.
    const auto hid_usage = static_cast<uint16_t>(event->scancode);
    if (hid_usage < 0x04 || hid_usage > 0xdf) {
        return;
    }

    // These modifiers do not have a corresponding command-registry bit. Drop
    // AltGr/Level5 input rather than accidentally turning it into an unmodified
    // shortcut on layouts where SDL reports it separately from right Alt.
    if ((event->mod & (SDL_KMOD_MODE | SDL_KMOD_LEVEL5)) != 0) {
        return;
    }

    if (g_shortcut_event_count == g_shortcut_events.size()) {
        g_shortcut_event_count = 0;
        g_shortcut_events_overflowed = true;
        return;
    }

    uint8_t modifiers{};
    if ((event->mod & SDL_KMOD_SHIFT) != 0) {
        modifiers |= WORM_UI_SHORTCUT_MODIFIER_SHIFT;
    }
    if ((event->mod & SDL_KMOD_CTRL) != 0) {
        modifiers |= WORM_UI_SHORTCUT_MODIFIER_CONTROL;
    }
    if ((event->mod & SDL_KMOD_ALT) != 0) {
        modifiers |= WORM_UI_SHORTCUT_MODIFIER_ALT;
    }
    if ((event->mod & SDL_KMOD_GUI) != 0) {
        modifiers |= WORM_UI_SHORTCUT_MODIFIER_META;
    }

    g_shortcut_events[g_shortcut_event_count++] = {
        .hid_usage = hid_usage,
        .modifiers = modifiers,
        .repeat = event->repeat,
    };
}

static bool nuklear_text_entry_active(void) {
    if (!g_context || !g_context->active) {
        return false;
    }
    const auto* active_window = g_context->active;
    if (active_window->popup.win) {
        return active_window->popup.win->edit.active != 0;
    }
    return active_window->edit.active != 0;
}

static void expose_shortcut_input(WormUiWorkspaceAction* action, bool compose_lane_focused = false,
                                  uint64_t focused_lane_id = 0) {
    if (!action) {
        return;
    }
    action->shortcut_event_count = g_shortcut_event_count;
    for (size_t index = 0; index < g_shortcut_event_count; ++index) {
        action->shortcut_events[index] = g_shortcut_events[index];
    }
    action->shortcut_overflowed = g_shortcut_events_overflowed;
    action->shortcut_window_focused = g_window_focused;
    action->shortcut_text_entry_active = nuklear_text_entry_active();
    action->shortcut_ime_composition_active =
        action->shortcut_text_entry_active && g_ime_composition_active;
    action->shortcut_compose_lane_focused = compose_lane_focused && g_window_focused;
    action->shortcut_focused_lane_id = action->shortcut_compose_lane_focused ? focused_lane_id : 0;

    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
}

static void queue_compose_wheel_event(const SDL_MouseWheelEvent* event) {
    if (!event || !g_window_focused || !g_window ||
        (event->windowID != 0 && event->windowID != SDL_GetWindowID(g_window)) ||
        !std::isfinite(event->x) || !std::isfinite(event->y) || !std::isfinite(event->mouse_x) ||
        !std::isfinite(event->mouse_y)) {
        return;
    }
    if (g_compose.wheel_event_count == g_compose.wheel_events.size()) {
        g_compose.wheel_event_count = 0;
        g_compose.cancel_pending = true;
        g_compose.key_event_count = 0;
        g_compose.pointer_hold_active = false;
        g_compose.note_paint_pointer_active = false;
        g_compose.semantic_layer_paint_pointer_active = false;
        g_compose.lane_hold_active = false;
        g_compose.space_range_hold_active = false;
        g_compose.space_range_hold_moved = false;
        g_compose.note_move_candidate_active = false;
        g_compose.note_move_active = false;
        g_compose.area_selection_candidate_active = false;
        g_compose.area_selection_active = false;
        g_compose.keyboard_focus = false;
        g_compose.lane_hold_keycode = 0;
        g_compose.lane_hold_lane_id = 0;
        return;
    }
    const auto whole_steps = std::trunc(event->x) == event->x && std::trunc(event->y) == event->y &&
                             (event->x != 0.0f || event->y != 0.0f);
    // Convert SDL's positive-away wheel convention into content-offset deltas.
    // FLIPPED sources already report the opposite sign, so they need no negation.
    const auto direction_sign = event->direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0 : -1.0;
    g_compose.wheel_events[g_compose.wheel_event_count++] = {
        .delta_x = static_cast<double>(event->x) * direction_sign,
        .delta_y = static_cast<double>(event->y) * direction_sign,
        .mouse_x = event->mouse_x,
        .mouse_y = event->mouse_y,
        .modifiers = static_cast<uint16_t>(SDL_GetModState()),
        .coarse_steps = whole_steps,
    };
}

static int recovery_action_offered(const WormUiRecoveryCandidateView* candidate, size_t action) {
    return candidate && (candidate->action_mask & action) != 0;
}

static void set_recovery_action(WormUiWorkspaceAction* action,
                                const WormUiRecoveryCandidateView* candidate,
                                WormUiWorkspaceActionKind kind) {
    if (!action || !candidate) {
        return;
    }
    action->kind = kind;
    action->recovery_scan_generation = candidate->scan_generation;
    action->recovery_row_index = candidate->row_index;
    action->recovery_candidate_index = candidate->candidate_index;
    if (kind == WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS) {
        memcpy(action->recovery_save_as_destination, g_recovery_save_as_destination,
               sizeof(action->recovery_save_as_destination));
    }
}

bool worm_ui_frontend_init(SDL_Window* window) {
    if (!window || g_context) {
        return false;
    }
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_reset_timing_state();
    worm_ui_nuklear_reset_audio_state();
    worm_ui_nuklear_reset_compose_state();

    struct nk_context* context = nk_sdl_init(window);
    if (!context) {
        return false;
    }
    struct nk_font_atlas* atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);
    (void)atlas;
    nk_sdl_font_stash_end();

    if (!SDL_StartTextInput(window)) {
        nk_sdl_shutdown();
        return false;
    }
    g_window = window;
    g_context = context;
    g_window_focused = true;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    worm_ui_nuklear_reset_compose_markers();
    g_double_click_count = 0;
    return true;
}

void worm_ui_frontend_shutdown(void) {
    if (!g_context) {
        worm_ui_nuklear_reset_metadata_state();
        worm_ui_nuklear_reset_timing_state();
        worm_ui_nuklear_reset_audio_state();
        worm_ui_nuklear_reset_compose_state();
        return;
    }
    if (g_window) {
        SDL_StopTextInput(g_window);
    }
    worm_ui_nuklear_shutdown_player_preview();
    nk_sdl_shutdown();
    g_window = NULL;
    g_context = NULL;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_double_click_count = 0;
    worm_ui_nuklear_reset_compose_markers();
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_reset_timing_state();
    worm_ui_nuklear_reset_audio_state();
    worm_ui_nuklear_reset_compose_state();
}

void worm_ui_frontend_begin_input(void) {
    if (g_context) {
        nk_input_begin(g_context);
        g_shortcut_event_count = 0;
        g_shortcut_events_overflowed = false;
        g_double_click_count = 0;
    }
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        g_window_focused = false;
        g_audio.offset_cancel_pending = true;
        g_compose.cancel_pending = true;
        g_compose.key_event_count = 0;
        g_shortcut_event_count = 0;
        g_compose.wheel_event_count = 0;
        g_compose.middle_click_pending = false;
        g_double_click_count = 0;
        worm_ui_nuklear_timing_handle_focus_lost();
        g_ime_composition_active = false;
        g_compose.keyboard_focus = false;
        g_compose.pointer_hold_active = false;
        // Keep paint capture until compose can deliver the pending cancel.
        g_compose.lane_hold_active = false;
        g_compose.space_range_hold_active = false;
        g_compose.space_range_hold_moved = false;
        g_compose.note_move_candidate_active = false;
        g_compose.note_move_active = false;
        g_compose.area_selection_candidate_active = false;
        g_compose.area_selection_active = false;
        g_compose.navigation_key_active = false;
        g_compose.navigation_keycode = 0;
        g_compose.pointer_navigation_active = false;
    } else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        g_window_focused = true;
    } else if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        queue_compose_key_event(&event->key, event->type == SDL_EVENT_KEY_DOWN);
        queue_shortcut_key_event(&event->key);
    } else if (event->type == SDL_EVENT_TEXT_EDITING && g_window && event->edit.windowID != 0 &&
               event->edit.windowID == SDL_GetWindowID(g_window)) {
        g_ime_composition_active = event->edit.text && event->edit.text[0] != '\0';
    } else if (event->type == SDL_EVENT_TEXT_EDITING_CANDIDATES && g_window &&
               event->edit_candidates.windowID != 0 &&
               event->edit_candidates.windowID == SDL_GetWindowID(g_window)) {
        if (event->edit_candidates.num_candidates > 0) {
            g_ime_composition_active = true;
        }
    } else if (event->type == SDL_EVENT_TEXT_INPUT && g_window && event->text.windowID != 0 &&
               event->text.windowID == SDL_GetWindowID(g_window)) {
        g_ime_composition_active = false;
    } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        queue_compose_wheel_event(&event->wheel);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.down &&
               event->button.button == SDL_BUTTON_MIDDLE && g_window_focused && g_window &&
               (event->button.windowID == 0 ||
                event->button.windowID == SDL_GetWindowID(g_window)) &&
               std::isfinite(event->button.x) && std::isfinite(event->button.y)) {
        g_compose.middle_click_pending = true;
        g_compose.middle_click_x = event->button.x;
        g_compose.middle_click_y = event->button.y;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.down &&
               event->button.button == SDL_BUTTON_LEFT && event->button.clicks >= 2 &&
               g_window_focused && g_window &&
               (event->button.windowID == 0 ||
                event->button.windowID == SDL_GetWindowID(g_window)) &&
               std::isfinite(event->button.x) && std::isfinite(event->button.y) &&
               g_double_click_count < g_double_clicks.size()) {
        g_double_clicks[g_double_click_count++] = {event->button.x, event->button.y};
    }
    SDL_Event mutable_event = *event;
    return nk_sdl_handle_event(&mutable_event) != 0;
}

void worm_ui_frontend_end_input(void) {
    if (g_context) {
        nk_input_end(g_context);
        nk_sdl_handle_grab();
    }
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
        view->playable_field_right_pixels < 0.0 ||
        view->playable_field_right_pixels > view->viewport_width_pixels ||
        (view->playhead_visible &&
         (!std::isfinite(view->playhead_y_pixels) || view->playhead_y_pixels < 0.0 ||
          view->playhead_y_pixels > view->viewport_height_pixels)) ||
        view->branch_row_count > WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY ||
        view->branch_diagnostic_count > WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY ||
        view->semantic_layer_count > WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY ||
        view->beat_bookmark_count > WORM_UI_COMPOSE_BOOKMARK_CAPACITY ||
        view->beat_section_count > WORM_UI_COMPOSE_SECTION_CAPACITY ||
        view->audio_bookmark_count > WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY ||
        view->audio_section_count > WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY ||
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
        (view->beat_bookmark_count != 0 && !view->beat_bookmarks) ||
        (view->beat_section_count != 0 && !view->beat_sections) ||
        (view->audio_bookmark_count != 0 && !view->audio_bookmarks) ||
        (view->audio_section_count != 0 && !view->audio_sections))
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
    for (size_t index = 0; index < view->difference_count; ++index) {
        if (!compose_rect_valid(&view->differences[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        if (!compose_rect_valid(&view->hold_bodies[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        if (!compose_rect_valid(&view->note_endpoints[index].bounds))
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
    return !view->cursor || compose_rect_valid(&view->cursor->bounds);
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

static void process_compose_key_events(const WormUiComposeView* view,
                                       WormUiWorkspaceAction* action) {
    const bool keyboard_ready =
        g_window_focused && g_compose.keyboard_focus && view->scene_available && g_context;
    const bool note_paint_mode_active =
        view->note_paint_mode_active ||
        action->compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    const bool semantic_layer_paint_mode_active =
        view->semantic_layer_paint_mode_active ||
        action->compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
    const bool pointer_paint_mode_active =
        note_paint_mode_active || semantic_layer_paint_mode_active;
    if (g_ime_composition_active) {
        cancel_compose_navigation(action);
        g_compose.key_event_count = 0;
        return;
    }
    if (pointer_paint_mode_active && g_compose.lane_hold_active) {
        append_compose_cancel_once(action);
        g_compose.lane_hold_active = false;
        g_compose.lane_hold_keycode = 0;
        g_compose.lane_hold_lane_id = 0;
    }
    if (pointer_paint_mode_active && g_compose.space_range_hold_active) {
        append_compose_cancel_once(action);
        g_compose.space_range_hold_active = false;
        g_compose.space_range_hold_moved = false;
    }
    if (pointer_paint_mode_active &&
        (g_compose.note_move_candidate_active || g_compose.note_move_active)) {
        if (g_compose.note_move_active) {
            append_compose_cancel_once(action);
        }
        g_compose.note_move_candidate_active = false;
        g_compose.note_move_active = false;
    }
    for (size_t event_index = 0;
         event_index < g_compose.key_event_count && !g_compose.action_overflowed; ++event_index) {
        const auto& key_event = g_compose.key_events[event_index];
        WormUiComposeNavigationAction navigation_action{};
        if (!key_event.down && g_compose.navigation_key_active &&
            key_event.scancode == g_compose.navigation_keycode) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 g_compose.navigation_action);
            g_compose.navigation_key_active = false;
            g_compose.navigation_keycode = 0;
            continue;
        }
        if (g_compose.lane_hold_active && !key_event.down &&
            key_event.scancode == g_compose.lane_hold_keycode) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 0.0, 0.0,
                                 g_compose.lane_hold_lane_id);
            g_compose.lane_hold_active = false;
            g_compose.lane_hold_keycode = 0;
            g_compose.lane_hold_lane_id = 0;
            continue;
        }
        if (g_compose.space_range_hold_active && !key_event.down &&
            key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
            if (g_compose.space_range_hold_moved) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
            }
            g_compose.space_range_hold_active = false;
            g_compose.space_range_hold_moved = false;
            continue;
        }
        if (!keyboard_ready || !g_compose.keyboard_focus || !key_event.down || key_event.repeat) {
            continue;
        }
        const bool shortcut = compose_shortcut_modifier(key_event.modifiers);
        if (key_event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
            if (g_compose.space_range_hold_active) {
                append_compose_cancel_once(action);
                g_compose.space_range_hold_active = false;
                g_compose.space_range_hold_moved = false;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
            constexpr auto any_space_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                                SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
            if (!pointer_paint_mode_active && !g_compose.space_range_hold_active &&
                (key_event.modifiers & any_space_modifier) == 0) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION, 0.0, 0.0,
                                     0, false, WORM_UI_COMPOSE_SELECTION_REPLACE);
                g_compose.space_range_hold_active =
                    !view->space_range_preview_visible && !g_compose.action_overflowed;
                g_compose.space_range_hold_moved = false;
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
            g_compose.navigation_key_active = !g_compose.action_overflowed;
            g_compose.navigation_keycode = key_event.scancode;
            g_compose.navigation_action = navigation_action;
            if (!g_compose.action_overflowed && g_compose.space_range_hold_active) {
                g_compose.space_range_hold_moved = true;
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
            g_compose.navigation_key_active = !g_compose.action_overflowed;
            g_compose.navigation_keycode = key_event.scancode;
            g_compose.navigation_action = navigation_action;
            if (!g_compose.action_overflowed && g_compose.space_range_hold_active) {
                g_compose.space_range_hold_moved = true;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_PAGEUP)) {
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD);
            g_compose.navigation_key_active = !g_compose.action_overflowed;
            g_compose.navigation_keycode = key_event.scancode;
            g_compose.navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD;
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_PAGEDOWN)) {
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                                 WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD);
            g_compose.navigation_key_active = !g_compose.action_overflowed;
            g_compose.navigation_keycode = key_event.scancode;
            g_compose.navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
            continue;
        }
        if (pointer_paint_mode_active) {
            continue;
        }
        if (g_compose.lane_hold_active ||
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
                g_compose.lane_hold_active = true;
                g_compose.lane_hold_keycode = key_event.scancode;
                g_compose.lane_hold_lane_id = binding.lane_id;
            }
            break;
        }
    }
    g_compose.key_event_count = 0;
}

static void render_compose_field(const WormUiComposeView* view, WormUiWorkspaceAction* action) {
    if (g_compose.label_chart_id != view->chart_id ||
        g_compose.label_document_revision != view->document_revision ||
        g_compose.label_note_id != view->selected_note_label_note_id) {
        g_compose.note_label.fill('\0');
        if (view->selected_note_label && view->selected_note_label_note_id != 0) {
            const auto length =
                std::min(strlen(view->selected_note_label), g_compose.note_label.size() - 1);
            memcpy(g_compose.note_label.data(), view->selected_note_label, length);
            g_compose.note_label[length] = '\0';
        }
        g_compose.label_chart_id = view->chart_id;
        g_compose.label_document_revision = view->document_revision;
        g_compose.label_note_id = view->selected_note_label_note_id;
    }
    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 10);
    nk_layout_row_push(g_context, 0.06f);
    if (nk_button_label(g_context, "Zoom -") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_OUT;
    nk_layout_row_push(g_context, 0.06f);
    if (nk_button_label(g_context, "Zoom +") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_IN;
    std::optional<WormUiComposeNavigationAction> pointer_navigation;
    const auto& navigation_left_button = g_context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto pointer_down =
        g_window_focused && view->scene_available && navigation_left_button.down != 0;
    const auto pointer_started = pointer_down && navigation_left_button.clicked != 0;
    if (g_compose.pointer_navigation_active && pointer_down) {
        pointer_navigation = g_compose.pointer_navigation_action;
    }
    const auto navigation_button = [&](const char* label, WormUiComposeNavigationAction action) {
        const auto bounds = nk_widget_bounds(g_context);
        if (!view->scene_available) {
            nk_widget_disable_begin(g_context);
        }
        (void)nk_button_label(g_context, label);
        if (!view->scene_available) {
            nk_widget_disable_end(g_context);
        }
        if (!g_compose.pointer_navigation_active && pointer_started &&
            navigation_left_button.clicked_pos.x >= bounds.x &&
            navigation_left_button.clicked_pos.x <= bounds.x + bounds.w &&
            navigation_left_button.clicked_pos.y >= bounds.y &&
            navigation_left_button.clicked_pos.y <= bounds.y + bounds.h) {
            pointer_navigation = action;
        }
    };
    nk_layout_row_push(g_context, 0.07f);
    navigation_button("Step up", WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD);
    nk_layout_row_push(g_context, 0.07f);
    navigation_button("Step down", WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD);
    nk_layout_row_push(g_context, 0.08f);
    navigation_button("Page up", WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD);
    nk_layout_row_push(g_context, 0.08f);
    navigation_button("Page down", WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD);
    update_compose_pointer_navigation(action, pointer_navigation);
    nk_layout_row_push(g_context, 0.07f);
    if (nk_button_label(g_context, "Reset") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW;
    nk_layout_row_push(g_context, 0.15f);
    nk_labelf(g_context, NK_TEXT_LEFT, "Zoom %.0f px/measure", view->pixels_per_standard_measure);
    nk_layout_row_push(g_context, 0.15f);
    if (!view->scene_available) {
        nk_widget_disable_begin(g_context);
    }
    const auto paint_label = view->note_paint_mode_active ? "Paint: Armed" : "Paint: Off";
    if (nk_button_label(g_context, paint_label) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    }
    if (!view->scene_available) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_push(g_context, 0.31f);
    if (view->truncated) {
        nk_label(g_context, "Visible geometry capped", NK_TEXT_RIGHT);
    } else {
        nk_labelf(g_context, NK_TEXT_RIGHT, "%zu note endpoints · %zu lanes · %zu grid lines",
                  view->note_endpoint_count, view->lane_count, view->grid_line_count);
    }
    nk_layout_row_end(g_context);

    nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 3);
    if (!view->scene_available || !view->beat_marker_navigation_available) {
        nk_widget_disable_begin(g_context);
    }
    nk_layout_row_push(g_context, 0.22f);
    const auto previous_marker_clicked = nk_button_label(g_context, "Previous marker");
    nk_layout_row_push(g_context, 0.22f);
    const auto next_marker_clicked = nk_button_label(g_context, "Next marker");
    if (!view->scene_available || !view->beat_marker_navigation_available) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_push(g_context, 0.56f);
    nk_label(g_context,
             view->beat_marker_navigation_available
                 ? "Navigate beat-anchored bookmarks and section boundaries"
                 : "No beat-anchored bookmarks or sections",
             NK_TEXT_LEFT);
    nk_layout_row_end(g_context);
    if (action && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        if (previous_marker_clicked) {
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER;
        } else if (next_marker_clicked) {
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER;
        }
    }

    worm_ui_nuklear_render_compose_markers(g_context, view, action);

    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 4);
    nk_layout_row_push(g_context, 0.13f);
    nk_labelf(g_context, NK_TEXT_LEFT, "Label (%zu)", view->selected_note_count);
    nk_layout_row_push(g_context, 0.53f);
    if (!view->scene_available || view->selected_note_count == 0) {
        nk_widget_disable_begin(g_context);
    }
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, g_compose.note_label.data(),
                                         g_compose.note_label.size(), nk_filter_default);
    if (view->focus_note_label_editor) {
        nk_edit_focus(g_context, NK_EDIT_GOTO_END_ON_ACTIVATE);
        if (action) {
            action->compose_note_label_focus_consumed = true;
        }
    }
    nk_layout_row_push(g_context, 0.17f);
    if (nk_button_label(g_context, "Apply") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_note_label, g_compose.note_label.data(),
               sizeof(action->compose_note_label));
        action->compose_action = WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL;
    }
    nk_layout_row_push(g_context, 0.17f);
    if (nk_button_label(g_context, "Clear") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL;
    }
    if (!view->scene_available || view->selected_note_count == 0) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_end(g_context);

    const auto visible_layer_count = view->semantic_layer_count;
    bool semantic_target_active = false;
    for (size_t index = 0; index < visible_layer_count; ++index) {
        semantic_target_active = semantic_target_active || view->semantic_layers[index].selected;
    }
    nk_layout_row_begin(g_context, NK_DYNAMIC, 22, 4);
    nk_layout_row_push(g_context, 0.40f);
    if (nk_button_label(g_context, "Add semantic layer") && action &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
    }
    nk_layout_row_push(g_context, 0.15f);
    if (view->semantic_layer_page == 0) {
        nk_widget_disable_begin(g_context);
    }
    const auto previous_layer_page_clicked = nk_button_label(g_context, "Previous");
    if (view->semantic_layer_page == 0) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_push(g_context, 0.30f);
    if (view->semantic_layer_page_count == 0) {
        nk_label(g_context, "Palette empty", NK_TEXT_CENTERED);
    } else {
        nk_labelf(g_context, NK_TEXT_CENTERED, "Page %zu/%zu · %zu layers",
                  view->semantic_layer_page + 1, view->semantic_layer_page_count,
                  view->semantic_layer_total_count);
    }
    nk_layout_row_push(g_context, 0.15f);
    const auto last_layer_page = view->semantic_layer_page_count == 0 ||
                                 view->semantic_layer_page + 1 >= view->semantic_layer_page_count;
    if (last_layer_page) {
        nk_widget_disable_begin(g_context);
    }
    const auto next_layer_page_clicked = nk_button_label(g_context, "Next");
    if (last_layer_page) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_end(g_context);
    if (action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (previous_layer_page_clicked) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE;
        } else if (next_layer_page_clicked) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE;
        }
    }
    if (visible_layer_count == 0) {
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_label(g_context, "Semantic layers: no visible palette entries", NK_TEXT_LEFT);
    } else {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, static_cast<int>(visible_layer_count + 1));
        nk_layout_row_push(g_context, 1.0f / static_cast<float>(visible_layer_count + 1));
        nk_label(g_context, view->semantic_layers_truncated ? "Layers+" : "Layers", NK_TEXT_LEFT);
        for (size_t index = 0; index < visible_layer_count; ++index) {
            const auto& layer = view->semantic_layers[index];
            std::array<char, 96> label{};
            const auto name_length = std::min<size_t>(strlen(layer.name), 24);
            snprintf(label.data(), label.size(), "%zu:%.*s #%llu%s%s", index + 1,
                     static_cast<int>(name_length), layer.name,
                     static_cast<unsigned long long>(layer.layer_id),
                     layer.visible ? "" : " [hidden]", layer.selected ? " *" : "");
            nk_layout_row_push(g_context, 1.0f / static_cast<float>(visible_layer_count + 1));
            const auto palette_color = nk_rgba(layer.red, layer.green, layer.blue, layer.alpha);
            nk_style_push_style_item(g_context, &g_context->style.button.normal,
                                     nk_style_item_color(palette_color));
            nk_style_push_style_item(g_context, &g_context->style.button.hover,
                                     nk_style_item_color(palette_color));
            nk_style_push_style_item(g_context, &g_context->style.button.active,
                                     nk_style_item_color(palette_color));
            if (!view->scene_available) {
                nk_widget_disable_begin(g_context);
            }
            const auto clicked = nk_button_label(g_context, label.data());
            if (!view->scene_available) {
                nk_widget_disable_end(g_context);
            }
            nk_style_pop_style_item(g_context);
            nk_style_pop_style_item(g_context);
            nk_style_pop_style_item(g_context);
            if (clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER;
                action->compose_semantic_layer_id = layer.layer_id;
            }
        }
        nk_layout_row_end(g_context);

        const auto selected_layer =
            std::ranges::find_if(view->semantic_layers, view->semantic_layers + visible_layer_count,
                                 [](const auto& layer) { return layer.selected; });
        if (selected_layer != view->semantic_layers + visible_layer_count) {
            if (g_compose.semantic_layer_name_id != selected_layer->layer_id ||
                g_compose.semantic_layer_name_revision != view->document_revision) {
                g_compose.semantic_layer_name.fill('\0');
                const auto length = std::min(strlen(selected_layer->name),
                                             g_compose.semantic_layer_name.size() - 1);
                memcpy(g_compose.semantic_layer_name.data(), selected_layer->name, length);
                g_compose.semantic_layer_name_id = selected_layer->layer_id;
                g_compose.semantic_layer_name_revision = view->document_revision;
            }
            if (g_compose.semantic_layer_color_id != selected_layer->layer_id) {
                g_compose.semantic_layer_color_id = selected_layer->layer_id;
                g_compose.semantic_layer_color_editor_visible = false;
            }
            if (!g_compose.semantic_layer_color_editor_visible) {
                const auto color =
                    nk_color_cf(nk_rgba(selected_layer->red, selected_layer->green,
                                        selected_layer->blue, selected_layer->alpha));
                g_compose.semantic_layer_color = {color.r, color.g, color.b, color.a};
            }
            nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 7);
            nk_layout_row_push(g_context, 0.35f);
            (void)nk_edit_string_zero_terminated(
                g_context, NK_EDIT_FIELD, g_compose.semantic_layer_name.data(),
                g_compose.semantic_layer_name.size(), nk_filter_default);
            nk_layout_row_push(g_context, 0.15f);
            if (nk_button_label(g_context, "Rename") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                memcpy(action->compose_semantic_layer_name, g_compose.semantic_layer_name.data(),
                       sizeof(action->compose_semantic_layer_name));
                action->compose_action = WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER;
            }
            nk_layout_row_push(g_context, 0.12f);
            if (nk_button_label(g_context, "Color...")) {
                g_compose.semantic_layer_color_editor_visible =
                    !g_compose.semantic_layer_color_editor_visible;
            }
            nk_layout_row_push(g_context, 0.12f);
            if (selected_layer->locked) {
                nk_widget_disable_begin(g_context);
            }
            const auto delete_layer_clicked = nk_button_label(g_context, "Delete");
            if (selected_layer->locked) {
                nk_widget_disable_end(g_context);
            }
            if (delete_layer_clicked && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER;
            }
            nk_layout_row_push(g_context, 0.08f);
            if (nk_button_label(g_context, "Up") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
            }
            nk_layout_row_push(g_context, 0.08f);
            if (nk_button_label(g_context, "Down") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN;
            }
            nk_layout_row_push(g_context, 0.10f);
            if (nk_button_label(g_context, view->semantic_layer_target_visible ? "Hide" : "Show") &&
                action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY;
            }
            nk_layout_row_end(g_context);
            if (g_compose.semantic_layer_color_editor_visible) {
                nk_layout_row_dynamic(g_context, 110, 1);
                auto color =
                    nk_colorf{g_compose.semantic_layer_color.r, g_compose.semantic_layer_color.g,
                              g_compose.semantic_layer_color.b, g_compose.semantic_layer_color.a};
                color = nk_color_picker(g_context, color, NK_RGBA);
                g_compose.semantic_layer_color = {color.r, color.g, color.b, color.a};
                const auto byte_component = [](float component) {
                    return static_cast<uint8_t>(
                        std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
                };
                const auto red = byte_component(g_compose.semantic_layer_color.r);
                const auto green = byte_component(g_compose.semantic_layer_color.g);
                const auto blue = byte_component(g_compose.semantic_layer_color.b);
                const auto alpha = byte_component(g_compose.semantic_layer_color.a);
                nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 3);
                nk_layout_row_push(g_context, 0.46f);
                nk_labelf(g_context, NK_TEXT_LEFT, "RGBA %u %u %u %u", static_cast<unsigned>(red),
                          static_cast<unsigned>(green), static_cast<unsigned>(blue),
                          static_cast<unsigned>(alpha));
                nk_layout_row_push(g_context, 0.27f);
                const auto apply_color_clicked = nk_button_label(g_context, "Apply color");
                nk_layout_row_push(g_context, 0.27f);
                const auto cancel_color_clicked = nk_button_label(g_context, "Cancel color");
                nk_layout_row_end(g_context);
                if (apply_color_clicked && action &&
                    action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_semantic_layer_red = red;
                    action->compose_semantic_layer_green = green;
                    action->compose_semantic_layer_blue = blue;
                    action->compose_semantic_layer_alpha = alpha;
                    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
                    g_compose.semantic_layer_color_editor_visible = false;
                } else if (cancel_color_clicked) {
                    g_compose.semantic_layer_color_editor_visible = false;
                }
            }
            if (selected_layer->assigned_note_count != 0) {
                nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 2);
                nk_layout_row_push(g_context, 0.65f);
                nk_labelf(g_context, NK_TEXT_LEFT, "Assigned to %zu note%s",
                          selected_layer->assigned_note_count,
                          selected_layer->assigned_note_count == 1 ? "" : "s");
                nk_layout_row_push(g_context, 0.35f);
                if (selected_layer->locked) {
                    nk_widget_disable_begin(g_context);
                }
                const auto delete_and_unlink_clicked =
                    nk_button_label(g_context, "Delete + unlink");
                if (selected_layer->locked) {
                    nk_widget_disable_end(g_context);
                }
                if (delete_and_unlink_clicked && action &&
                    action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS;
                }
                nk_layout_row_end(g_context);
            }
            if (selected_layer->visible) {
                nk_layout_row_begin(g_context, NK_DYNAMIC, 22, 2);
                nk_layout_row_push(g_context, 0.5f);
                if (nk_button_label(g_context, view->semantic_layer_target_required_filter
                                                   ? "Require: On"
                                                   : "Require filter") &&
                    action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
                }
                nk_layout_row_push(g_context, 0.5f);
                if (nk_button_label(g_context, view->semantic_layer_target_excluded_filter
                                                   ? "Exclude: On"
                                                   : "Exclude filter") &&
                    action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                    action->compose_action =
                        WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER;
                }
                nk_layout_row_end(g_context);
            }
        }

        nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 9);
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Layer (%zu)", view->selected_note_count);
        const auto membership_enabled = view->scene_available && semantic_target_active &&
                                        !view->semantic_layer_target_locked &&
                                        view->selected_note_count != 0;
        const auto membership_button = [&](const char* label, WormUiComposeActionKind kind) {
            if (!membership_enabled) {
                nk_widget_disable_begin(g_context);
            }
            const auto clicked = nk_button_label(g_context, label);
            if (!membership_enabled) {
                nk_widget_disable_end(g_context);
            }
            if (clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = kind;
            }
        };
        nk_layout_row_push(g_context, 0.10f);
        if (!view->scene_available || !semantic_target_active) {
            nk_widget_disable_begin(g_context);
        }
        const auto select_clicked = nk_button_label(g_context, "Select");
        if (!view->scene_available || !semantic_target_active) {
            nk_widget_disable_end(g_context);
        }
        if (select_clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES;
        }
        nk_layout_row_push(g_context, 0.10f);
        membership_button("Add", WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER);
        nk_layout_row_push(g_context, 0.10f);
        membership_button("Toggle", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER);
        nk_layout_row_push(g_context, 0.10f);
        membership_button("Remove", WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER);
        nk_layout_row_push(g_context, 0.15f);
        const auto layer_paint_enabled =
            view->scene_available && semantic_target_active && !view->semantic_layer_target_locked;
        if (!layer_paint_enabled) {
            nk_widget_disable_begin(g_context);
        }
        const auto layer_paint_clicked = nk_button_label(
            g_context, view->semantic_layer_paint_mode_active ? "Paint: Armed" : "Paint: Off");
        if (!layer_paint_enabled) {
            nk_widget_disable_end(g_context);
        }
        if (layer_paint_clicked && action &&
            action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
        }
        nk_layout_row_push(g_context, 0.12f);
        if (!semantic_target_active) {
            nk_widget_disable_begin(g_context);
        }
        const auto solo_clicked =
            nk_button_label(g_context, view->semantic_layer_solo_active ? "Solo: On" : "Solo: Off");
        if (!semantic_target_active) {
            nk_widget_disable_end(g_context);
        }
        if (solo_clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO;
        }
        nk_layout_row_push(g_context, 0.10f);
        if (!semantic_target_active) {
            nk_widget_disable_begin(g_context);
        }
        const auto lock_clicked =
            nk_button_label(g_context, view->semantic_layer_target_locked ? "Unlock" : "Lock");
        if (!semantic_target_active) {
            nk_widget_disable_end(g_context);
        }
        if (lock_clicked && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK;
        }
        nk_layout_row_push(g_context, 0.13f);
        if (!semantic_target_active) {
            nk_widget_disable_begin(g_context);
        }
        const auto clear_target = nk_button_label(g_context, "Clear target");
        if (!semantic_target_active) {
            nk_widget_disable_end(g_context);
        }
        if (clear_target && action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER;
        }
        nk_layout_row_end(g_context);
    }

    nk_layout_row_dynamic(g_context, 18, 1);
    nk_label(g_context,
             view->semantic_layer_paint_mode_active
                 ? "Layer paint armed — unmodified click/drag toggles the selected layer on notes."
             : view->semantic_layer_solo_active
                 ? "Layer solo active — only notes carrying the selected layer are shown."
             : view->note_paint_mode_active
                 ? "Paint armed — unmodified click/drag adds notes; dragging over painted "
                   "notes erases."
                 : "Paint off — arm Paint to draw notes with an unmodified click or drag.",
             NK_TEXT_LEFT);
    nk_layout_row_dynamic(g_context, 18, 1);
    nk_label(g_context,
             "↑/↓ move (hold to repeat) · Page Up/Down page measures · Click toggles taps · "
             "drag selected notes moves · empty drag selects (Shift add, Ctrl/Cmd toggle) · "
             "hold Space + ↑/↓ then release selects; tap Space twice selects cursor range · lane "
             "keys assign/hold · "
             "Alt/Option-drag · Delete · "
             "Ctrl/Cmd+Z/Y",
             NK_TEXT_LEFT);

    const auto group_bounds = nk_window_get_content_region(g_context);
    const auto branch_rows_shown = std::min<size_t>(view->branch_row_count, 4);
    const auto branch_diagnostics_shown = std::min<size_t>(view->branch_diagnostic_count, 2);
    const bool branch_status_shown =
        view->branch_rows_truncated || view->branch_display_text_truncated ||
        view->branch_diagnostics_truncated || view->branch_row_count > branch_rows_shown ||
        view->branch_diagnostic_count > branch_diagnostics_shown;
    const bool branch_outline_visible = view->branch_outline_available ||
                                        view->branch_row_count != 0 ||
                                        view->branch_diagnostic_count != 0 || branch_status_shown;
    const auto branch_panel_height =
        branch_outline_visible
            ? 23.0f + static_cast<float>(branch_rows_shown + branch_diagnostics_shown +
                                         (branch_status_shown ? 1 : 0)) *
                          18.0f
            : 0.0f;
    const auto semantic_toolbar_height = visible_layer_count == 0 ? 22.0f : 49.0f;
    const auto surface_height = std::max(32.0f, group_bounds.h - 54.0f - 36.0f -
                                                    semantic_toolbar_height - branch_panel_height);
    nk_layout_space_begin(g_context, NK_DYNAMIC, surface_height, 1);
    nk_layout_space_push(g_context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, g_context);
    nk_layout_space_end(g_context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f)
        return;

    auto* canvas = nk_window_get_canvas(g_context);
    if (!canvas)
        return;

    const bool pointer_inside = nk_input_is_mouse_hovering_rect(&g_context->input, viewport);
    const bool pointer_clicked =
        nk_input_is_mouse_click_in_rect(&g_context->input, NK_BUTTON_LEFT, viewport);
    const auto& left_button = g_context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool pointer_pressed_in_view = left_button.down && left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x <= viewport.x + viewport.w &&
                                         left_button.clicked_pos.y >= viewport.y &&
                                         left_button.clicked_pos.y <= viewport.y + viewport.h;
    if (pointer_clicked || pointer_pressed_in_view) {
        g_compose.keyboard_focus = true;
    } else if (nk_input_any_mouse_click_in_rect(&g_context->input,
                                                nk_rect(0.0f, 0.0f, 1000000.0f, 1000000.0f)) &&
               !pointer_inside) {
        g_compose.keyboard_focus = false;
        if (!g_compose.pointer_navigation_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
        }
        if (g_compose.space_range_hold_active) {
            append_compose_cancel_once(action);
            g_compose.space_range_hold_active = false;
            g_compose.space_range_hold_moved = false;
        }
    }
    process_compose_key_events(view, action);

    const auto pointer_x = (static_cast<double>(g_context->input.mouse.pos.x) - viewport.x) *
                           view->viewport_width_pixels / viewport.w;
    const auto pointer_y = (static_cast<double>(g_context->input.mouse.pos.y) - viewport.y) *
                           view->viewport_height_pixels / viewport.h;
    const auto note_field_right = view->playable_field_right_pixels;
    const auto pointer_in_note_field = pointer_inside && pointer_x < note_field_right;
    if (!g_compose.action_overflowed && g_window_focused && view->scene_available) {
        action->compose_viewport_pointer_valid = true;
        action->compose_viewport_pointer_x_pixels = pointer_x;
        action->compose_viewport_pointer_y_pixels = pointer_y;
        if (g_compose.middle_click_pending && g_compose.middle_click_x >= viewport.x &&
            g_compose.middle_click_x <= viewport.x + viewport.w &&
            g_compose.middle_click_y >= viewport.y &&
            g_compose.middle_click_y <= viewport.y + viewport.h) {
            action->compose_viewport_middle_click = true;
            action->compose_viewport_middle_click_x_pixels =
                (static_cast<double>(g_compose.middle_click_x) - viewport.x) *
                view->viewport_width_pixels / viewport.w;
            action->compose_viewport_middle_click_y_pixels =
                (static_cast<double>(g_compose.middle_click_y) - viewport.y) *
                view->viewport_height_pixels / viewport.h;
        }
    }
    g_compose.middle_click_pending = false;
    if (!g_compose.action_overflowed && view->scene_available && g_window_focused) {
        for (size_t index = 0; index < g_compose.wheel_event_count; ++index) {
            const auto& wheel = g_compose.wheel_events[index];
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
    g_compose.wheel_event_count = 0;
    const bool alt_down = nk_input_is_key_down(&g_context->input, NK_KEY_ALT) != 0;
    const bool left_down = g_context->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    const bool left_released = nk_input_is_mouse_released(&g_context->input, NK_BUTTON_LEFT);
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
    const auto pointer_started_in_note_field =
        pointer_started_in_view && pointer_started_x < note_field_right;
    const auto clamp_paint_x = [&](double value) {
        return std::clamp(value, 0.0, note_field_right);
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
    const auto compose_pointer_captured =
        g_compose.note_paint_pointer_active || g_compose.semantic_layer_paint_pointer_active ||
        g_compose.pointer_hold_active || g_compose.lane_hold_active || g_compose.note_move_active ||
        g_compose.area_selection_active;
    action->compose_has_pointer_hover = !g_compose.action_overflowed && g_window_focused &&
                                        view->scene_available && pointer_in_note_field &&
                                        !compose_pointer_captured;
    if (action->compose_has_pointer_hover) {
        action->compose_pointer_hover_x_pixels = pointer_x;
        action->compose_pointer_hover_y_pixels = pointer_y;
    }
    bool compose_note_double_click_in_view = false;
    if (!compose_pointer_captured && g_window_focused && view->scene_available) {
        for (size_t click = 0; click < g_double_click_count; ++click) {
            const auto& pointer = g_double_clicks[click];
            if (pointer.x < viewport.x || pointer.x >= viewport.x + viewport.w ||
                pointer.y < viewport.y || pointer.y >= viewport.y + viewport.h) {
                continue;
            }
            const auto click_x = (static_cast<double>(pointer.x) - viewport.x) *
                                 view->viewport_width_pixels / viewport.w;
            if (click_x >= note_field_right)
                continue;
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK, click_x,
                                 (static_cast<double>(pointer.y) - viewport.y) *
                                     view->viewport_height_pixels / viewport.h);
            compose_note_double_click_in_view = true;
        }
    }
    bool paint_pointer_consumed = g_compose.note_paint_pointer_active ||
                                  g_compose.semantic_layer_paint_pointer_active ||
                                  compose_action_has_cancel(action);
    if (!g_compose.action_overflowed && g_compose.semantic_layer_paint_pointer_active) {
        if (!g_window_focused || !view->scene_available ||
            !view->semantic_layer_paint_mode_active || g_shortcut_event_count != 0 ||
            (pointer_modifiers & any_pointer_modifier) != 0 || (!left_down && !left_released)) {
            append_compose_cancel_once(action);
            g_compose.semantic_layer_paint_pointer_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                 paint_pointer_x, paint_pointer_y);
            g_compose.semantic_layer_paint_pointer_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
                                 paint_pointer_x, paint_pointer_y);
        }
    } else if (!g_compose.action_overflowed && view->scene_available &&
               view->semantic_layer_paint_mode_active && pointer_started_in_note_field &&
               g_window_focused && g_shortcut_event_count == 0 &&
               (pointer_modifiers & any_pointer_modifier) == 0 && !g_compose.pointer_hold_active &&
               !g_compose.lane_hold_active && !g_compose.area_selection_active &&
               !g_compose.area_selection_candidate_active) {
        paint_pointer_consumed = true;
        if (compose_point_hits_note(view, paint_anchor_x, paint_anchor_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
                                 paint_anchor_x, paint_anchor_y);
            g_compose.semantic_layer_paint_pointer_active =
                !g_compose.action_overflowed && left_down;
            if (!g_compose.action_overflowed && !left_down) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                     paint_pointer_x, paint_pointer_y);
            }
        }
    } else if (!g_compose.action_overflowed && g_compose.note_paint_pointer_active) {
        if (!g_window_focused || !view->scene_available || !view->note_paint_mode_active ||
            g_shortcut_event_count != 0 || (pointer_modifiers & any_pointer_modifier) != 0 ||
            (!left_down && !left_released)) {
            append_compose_cancel_once(action);
            g_compose.note_paint_pointer_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, paint_pointer_x,
                                 paint_pointer_y);
            g_compose.note_paint_pointer_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE, paint_pointer_x,
                                 paint_pointer_y);
        }
    } else if (!g_compose.action_overflowed && view->scene_available &&
               view->note_paint_mode_active && pointer_started_in_note_field && g_window_focused &&
               g_shortcut_event_count == 0 && (pointer_modifiers & any_pointer_modifier) == 0 &&
               !g_compose.pointer_hold_active && !g_compose.lane_hold_active &&
               !g_compose.area_selection_active && !g_compose.area_selection_candidate_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, paint_anchor_x,
                             paint_anchor_y);
        paint_pointer_consumed = true;
        g_compose.note_paint_pointer_active = !g_compose.action_overflowed && left_down;
        if (!g_compose.action_overflowed && !left_down) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, paint_pointer_x,
                                 paint_pointer_y);
        }
    }

    if (!g_compose.action_overflowed && !paint_pointer_consumed && view->scene_available &&
        pointer_started_in_note_field && alt_down && !g_compose.pointer_hold_active &&
        g_window_focused && !compose_note_double_click_in_view) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN, pointer_x, pointer_y);
        g_compose.pointer_hold_active = !g_compose.action_overflowed;
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed &&
               g_compose.pointer_hold_active) {
        if (!g_window_focused || !alt_down) {
            append_compose_cancel_once(action);
            g_compose.pointer_hold_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE, pointer_x,
                                 pointer_y);
            g_compose.pointer_hold_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE, pointer_x,
                                 pointer_y);
        }
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_started_in_note_field && g_window_focused && !alt_down &&
               !g_compose.note_move_active && !g_compose.note_move_candidate_active &&
               !g_compose.area_selection_active && !g_compose.area_selection_candidate_active &&
               !compose_note_double_click_in_view) {
        if (compose_point_hits_selected_note(view, pointer_x, pointer_y) && left_down) {
            g_compose.note_move_candidate_active = true;
            g_compose.note_move_anchor_x = pointer_x;
            g_compose.note_move_anchor_y = pointer_y;
            g_compose.note_move_anchor_screen_x = g_context->input.mouse.pos.x;
            g_compose.note_move_anchor_screen_y = g_context->input.mouse.pos.y;
        } else if (compose_point_hits_note(view, pointer_x, pointer_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
        } else if (left_down) {
            g_compose.area_selection_candidate_active = true;
            g_compose.area_selection_anchor_x = pointer_x;
            g_compose.area_selection_anchor_y = pointer_y;
            g_compose.area_selection_anchor_screen_x = g_context->input.mouse.pos.x;
            g_compose.area_selection_anchor_screen_y = g_context->input.mouse.pos.y;
            g_compose.area_selection_mode =
                compose_selection_mode(static_cast<uint16_t>(SDL_GetModState()));
        } else {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
        }
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_clicked && pointer_in_note_field && g_window_focused &&
               !g_compose.pointer_hold_active && !g_compose.note_move_active &&
               !g_compose.note_move_candidate_active && !g_compose.area_selection_active &&
               !g_compose.area_selection_candidate_active && !compose_note_double_click_in_view) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_clicked && pointer_inside && pointer_x >= note_field_right &&
               g_window_focused && !g_compose.pointer_hold_active && !g_compose.note_move_active &&
               !g_compose.note_move_candidate_active && !g_compose.area_selection_active &&
               !g_compose.area_selection_candidate_active) {
        const auto event_id = compose_bgm_event_at(view, pointer_x, pointer_y);
        if (event_id != 0) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK, pointer_x,
                                 pointer_y, 0, false, compose_selection_mode(pointer_modifiers),
                                 WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, event_id);
        }
    }

    if (!g_compose.action_overflowed && !paint_pointer_consumed &&
        g_compose.note_move_candidate_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            g_compose.note_move_candidate_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 g_compose.note_move_anchor_x, g_compose.note_move_anchor_y);
            g_compose.note_move_candidate_active = false;
        } else if (left_down) {
            constexpr float drag_threshold_pixels = 4.0f;
            const auto dx = g_context->input.mouse.pos.x - g_compose.note_move_anchor_screen_x;
            const auto dy = g_context->input.mouse.pos.y - g_compose.note_move_anchor_screen_y;
            if (std::hypot(dx, dy) >= drag_threshold_pixels) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
                                     g_compose.note_move_anchor_x, g_compose.note_move_anchor_y);
                g_compose.note_move_candidate_active = false;
                g_compose.note_move_active = !g_compose.action_overflowed;
                if (g_compose.note_move_active) {
                    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, pointer_x,
                                         pointer_y);
                }
            }
        }
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed &&
               g_compose.note_move_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            g_compose.note_move_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, pointer_x,
                                 pointer_y);
            g_compose.note_move_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, pointer_x,
                                 pointer_y);
        }
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed &&
               g_compose.area_selection_candidate_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            if (g_window_focused) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            }
            g_compose.area_selection_candidate_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 g_compose.area_selection_anchor_x,
                                 g_compose.area_selection_anchor_y);
            g_compose.area_selection_candidate_active = false;
        } else if (left_down) {
            constexpr float drag_threshold_pixels = 4.0f;
            const auto dx = g_context->input.mouse.pos.x - g_compose.area_selection_anchor_screen_x;
            const auto dy = g_context->input.mouse.pos.y - g_compose.area_selection_anchor_screen_y;
            if (std::hypot(dx, dy) >= drag_threshold_pixels) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                                     g_compose.area_selection_anchor_x,
                                     g_compose.area_selection_anchor_y, 0, false,
                                     g_compose.area_selection_mode);
                g_compose.area_selection_candidate_active = false;
                g_compose.area_selection_active = !g_compose.action_overflowed;
                if (g_compose.area_selection_active) {
                    append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                                         pointer_x, pointer_y);
                }
            }
        }
    } else if (!g_compose.action_overflowed && !paint_pointer_consumed &&
               g_compose.area_selection_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            g_compose.area_selection_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE, pointer_x,
                                 pointer_y);
            g_compose.area_selection_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
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
                     g_context->style.font, nk_rgb(22, 25, 33), nk_rgb(190, 196, 207));
        nk_push_scissor(canvas, parent_clip);
    } else {
        const auto rail_width = static_cast<float>(42.0 * viewport.w / view->viewport_width_pixels);
        const auto rail = nk_rect(viewport.x, viewport.y, rail_width, viewport.h);
        nk_fill_rect(canvas, rail, 0.0f, nk_rgb(18, 21, 29));
        nk_stroke_line(canvas, rail.x + rail.w, rail.y, rail.x + rail.w, rail.y + rail.h, 1.0f,
                       nk_rgb(80, 91, 112));
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
                                 g_context->style.font, nk_rgb(37, 43, 55), nk_rgb(122, 133, 153));
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
                                 g_context->style.font, nk_rgb(18, 21, 29), nk_rgb(174, 185, 203));
                }
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
                nk_fill_rect(canvas, rect, 1.5f, color);
                nk_stroke_rect(canvas, rect, 1.5f, border_width, border);
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
                         static_cast<int>(strlen(indicator_text)), g_context->style.font,
                         nk_rgb(12, 16, 24), nk_rgb(255, 231, 157));
        }
        if (view->hover_note_visible && view->hover_tooltip && view->hover_tooltip[0] != '\0') {
            const auto tooltip_width = std::min(420.0f, std::max(120.0f, viewport.w - 16.0f));
            const auto tooltip_bounds =
                nk_rect(viewport.x + 8.0f, viewport.y + 8.0f, tooltip_width, 24.0f);
            nk_fill_rect(canvas, tooltip_bounds, 3.0f, nk_rgba(12, 16, 24, 235));
            nk_stroke_rect(canvas, tooltip_bounds, 3.0f, 1.0f, nk_rgb(255, 235, 148));
            nk_draw_text(canvas, tooltip_bounds, view->hover_tooltip,
                         static_cast<int>(strlen(view->hover_tooltip)), g_context->style.font,
                         nk_rgb(12, 16, 24), nk_rgb(255, 244, 192));
        }
    }
    nk_push_scissor(canvas, parent_clip);

    if (!branch_outline_visible) {
        return;
    }
    nk_layout_row_dynamic(g_context, 20, 1);
    nk_label(g_context, "BMS branch outline · read-only", NK_TEXT_LEFT);
    for (size_t index = 0; index < branch_rows_shown; ++index) {
        const auto& row = view->branch_rows[index];
        std::string label;
        label.reserve(std::min<size_t>(row.depth, 32) * 2 + 96 + strlen(row.display_text));
        const auto indentation = std::min<size_t>(row.depth, 32);
        label.append(indentation, ' ');
        label += row.active ? "▶ " : "· ";
        if (row.selected) {
            label += "selected · ";
        } else if (!row.active) {
            label += "inactive · ";
        }
        if (row.has_source_order) {
            label += "#" + std::to_string(row.source_order) + " ";
        }
        label += row.display_text;
        if (row.display_text_truncated) {
            label += " …";
        }
        const auto color = row.selected ? nk_rgb(252, 221, 128)
                           : row.active ? nk_rgb(145, 222, 172)
                                        : nk_rgb(147, 154, 170);
        nk_style_push_color(g_context, &g_context->style.text.color, color);
        nk_layout_row_dynamic(g_context, 18, 1);
        nk_label(g_context, label.c_str(), NK_TEXT_LEFT);
        nk_style_pop_color(g_context);
    }
    if (branch_status_shown) {
        nk_layout_row_dynamic(g_context, 18, 1);
        nk_label(
            g_context,
            "Additional branch rows, text, or diagnostics were truncated by the workspace cap.",
            NK_TEXT_LEFT);
    }
    for (size_t index = 0; index < branch_diagnostics_shown; ++index) {
        if (!view->branch_diagnostics[index]) {
            continue;
        }
        nk_layout_row_dynamic(g_context, 18, 1);
        nk_labelf(g_context, NK_TEXT_LEFT, "Branch diagnostic: %s",
                  view->branch_diagnostics[index]);
    }
}

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_NONE;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    const bool timing_tab_active = view && view->active_tab == WORM_UI_WORKSPACE_TAB_TIMING;
    worm_ui_nuklear_reconcile_timing_gestures(view ? &view->timing : nullptr, timing_tab_active,
                                              g_window_focused);
    if (!g_context || !g_window || !view || !view->project_title || !view->artist ||
        !view->selected_difficulty || !view->status_text || !view->git_hash || !view->tab_heading ||
        view->summary_line_count > WORM_UI_WORKSPACE_SUMMARY_CAPACITY ||
        view->recovery_candidate_count > WORM_UI_RECOVERY_ROW_CAPACITY ||
        view->recovery_diagnostic_count > WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY ||
        (view->metadata.visible &&
         (view->metadata.format_field_count > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY ||
          view->metadata.format_target_count > WORM_UI_METADATA_FORMAT_TARGET_CAPACITY ||
          view->metadata.audio_waveform_column_count > WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY ||
          (view->metadata.audio_waveform_column_count != 0 &&
           !view->metadata.audio_waveform_columns) ||
          (view->metadata.audio_waveform_analysis_pending &&
           view->metadata.audio_waveform_available) ||
          (!view->metadata.audio_waveform_available &&
           view->metadata.audio_waveform_column_count != 0) ||
          (view->metadata.audio_waveform_available &&
           (view->metadata.audio_waveform_column_count == 0 ||
            view->metadata.audio_waveform_total_frames == 0 ||
            view->metadata.audio_waveform_sample_rate == 0 ||
            !view->metadata.audio_waveform_has_selected_asset)))) ||
        (view->timing.visible && !worm_ui_nuklear_timing_view_valid(&view->timing)) ||
        (view->audio_slices.visible &&
         !worm_ui_nuklear_audio_slices_view_valid(&view->audio_slices)) ||
        !worm_ui_nuklear_player_preview_view_valid(&view->player_preview) ||
        (view->compose.visible &&
         (view->compose.band_count > WORM_UI_COMPOSE_BAND_CAPACITY ||
          view->compose.lane_count > WORM_UI_COMPOSE_LANE_CAPACITY ||
          view->compose.grid_line_count > WORM_UI_COMPOSE_GRID_CAPACITY ||
          view->compose.difference_count > WORM_UI_COMPOSE_DIFFERENCE_CAPACITY ||
          view->compose.hold_body_count > WORM_UI_COMPOSE_HOLD_CAPACITY ||
          view->compose.note_endpoint_count > WORM_UI_COMPOSE_NOTE_CAPACITY ||
          view->compose.bgm_lane_count > WORM_UI_COMPOSE_BGM_LANE_CAPACITY ||
          view->compose.bgm_event_count > WORM_UI_COMPOSE_BGM_EVENT_CAPACITY ||
          view->compose.semantic_layer_count > WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY ||
          view->compose.lane_key_binding_count > WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY ||
          view->compose.branch_row_count > WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY ||
          view->compose.branch_diagnostic_count > WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY ||
          !std::isfinite(view->compose.viewport_width_pixels) ||
          view->compose.viewport_width_pixels <= 0.0 ||
          !std::isfinite(view->compose.viewport_height_pixels) ||
          view->compose.viewport_height_pixels <= 0.0 ||
          !std::isfinite(view->compose.pixels_per_standard_measure) ||
          view->compose.pixels_per_standard_measure < WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE ||
          view->compose.pixels_per_standard_measure > WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE ||
          (view->compose.band_count != 0 && !view->compose.bands) ||
          (view->compose.lane_count != 0 && !view->compose.lanes) ||
          (view->compose.grid_line_count != 0 && !view->compose.grid_lines) ||
          (view->compose.difference_count != 0 && !view->compose.differences) ||
          (view->compose.hold_body_count != 0 && !view->compose.hold_bodies) ||
          (view->compose.note_endpoint_count != 0 && !view->compose.note_endpoints) ||
          (view->compose.bgm_lane_count != 0 && !view->compose.bgm_lanes) ||
          (view->compose.bgm_event_count != 0 && !view->compose.bgm_events) ||
          (view->compose.semantic_layer_count != 0 && !view->compose.semantic_layers) ||
          (view->compose.lane_key_binding_count != 0 && !view->compose.lane_key_bindings) ||
          (view->compose.branch_row_count != 0 && !view->compose.branch_rows) ||
          !compose_geometry_valid(&view->compose))) ||
        view->active_tab < WORM_UI_WORKSPACE_TAB_SETUP ||
        view->active_tab > WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE) {
        g_compose.wheel_event_count = 0;
        g_compose.middle_click_pending = false;
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        expose_shortcut_input(&action);
        return action;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(g_window, &width, &height) || width <= 0 || height <= 0) {
        g_compose.wheel_event_count = 0;
        g_compose.middle_click_pending = false;
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        expose_shortcut_input(&action);
        return action;
    }

    worm_ui_nuklear_emit_timing_cancellations(&view->timing, timing_tab_active, &action);

    if (view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view->compose.visible) {
        g_compose.action_overflowed = false;
        action.compose_input_chart_id = view->compose.chart_id;
        action.compose_input_document_revision = view->compose.document_revision;
        action.compose_input_viewport_width_pixels = view->compose.viewport_width_pixels;
        if (!view->compose.scene_available) {
            cancel_compose_navigation(&action);
        }
        if (g_compose.cancel_pending) {
            append_compose_input(&action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            g_compose.cancel_pending = false;
            g_compose.note_paint_pointer_active = false;
            g_compose.semantic_layer_paint_pointer_active = false;
            g_compose.space_range_hold_active = false;
            g_compose.space_range_hold_moved = false;
            g_compose.note_move_candidate_active = false;
            g_compose.note_move_active = false;
        }
        const auto chart_changed = g_compose.last_chart_id != view->compose.chart_id;
        const auto revision_changed =
            g_compose.last_document_revision != view->compose.document_revision;
        if (chart_changed || revision_changed) {
            if (g_compose.note_paint_pointer_active ||
                g_compose.semantic_layer_paint_pointer_active ||
                g_compose.space_range_hold_active || g_compose.note_move_active) {
                append_compose_cancel_once(&action);
            }
            g_compose.pointer_hold_active = false;
            g_compose.note_paint_pointer_active = false;
            g_compose.semantic_layer_paint_pointer_active = false;
            g_compose.lane_hold_active = false;
            g_compose.space_range_hold_active = false;
            g_compose.space_range_hold_moved = false;
            g_compose.note_move_candidate_active = false;
            g_compose.note_move_active = false;
            g_compose.area_selection_candidate_active = false;
            g_compose.area_selection_active = false;
            g_compose.semantic_layer_color_editor_visible = false;
            g_compose.lane_hold_keycode = 0;
            g_compose.lane_hold_lane_id = 0;
            cancel_compose_navigation(&action);
        }
        if (chart_changed) {
            // Events received before the first frame for another chart still
            // target the previously visible geometry. Events after an ordinary
            // document edit already belong to the current frame/revision.
            g_compose.key_event_count = 0;
            g_compose.wheel_event_count = 0;
            g_compose.middle_click_pending = false;
            g_compose.keyboard_focus = false;
        }
        if (chart_changed || revision_changed) {
            g_compose.last_chart_id = view->compose.chart_id;
            g_compose.last_document_revision = view->compose.document_revision;
        }
        if (g_compose.note_paint_pointer_active &&
            (!g_window_focused || !view->compose.scene_available ||
             !view->compose.note_paint_mode_active)) {
            append_compose_cancel_once(&action);
            g_compose.note_paint_pointer_active = false;
        }
        if (g_compose.semantic_layer_paint_pointer_active &&
            (!g_window_focused || !view->compose.scene_available ||
             !view->compose.semantic_layer_paint_mode_active)) {
            append_compose_cancel_once(&action);
            g_compose.semantic_layer_paint_pointer_active = false;
        }
    } else {
        g_compose.cancel_pending = false;
        g_compose.key_event_count = 0;
        g_compose.wheel_event_count = 0;
        g_compose.middle_click_pending = false;
        g_compose.keyboard_focus = false;
        if (g_compose.pointer_hold_active || g_compose.lane_hold_active ||
            g_compose.note_paint_pointer_active || g_compose.semantic_layer_paint_pointer_active ||
            g_compose.space_range_hold_active || g_compose.area_selection_active) {
            append_compose_cancel_once(&action);
        }
        g_compose.pointer_hold_active = false;
        g_compose.note_paint_pointer_active = false;
        g_compose.semantic_layer_paint_pointer_active = false;
        g_compose.lane_hold_active = false;
        g_compose.space_range_hold_active = false;
        g_compose.space_range_hold_moved = false;
        g_compose.area_selection_candidate_active = false;
        g_compose.area_selection_active = false;
        g_compose.semantic_layer_color_editor_visible = false;
        cancel_compose_navigation(&action);
        g_compose.last_chart_id = 0;
        g_compose.last_document_revision = 0;
    }

    if (nk_begin(g_context, "wormedit-workspace", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 30, 4);
        nk_layout_row_push(g_context, 0.24f);
        nk_labelf(g_context, NK_TEXT_LEFT, "%s%s", view->project_title, view->dirty ? " *" : "");
        nk_layout_row_push(g_context, 0.18f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Artist: %s", view->artist);
        nk_layout_row_push(g_context, 0.22f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Difficulty: %s", view->selected_difficulty);
        nk_layout_row_push(g_context, 0.36f);
        nk_label(g_context, view->status_text, NK_TEXT_RIGHT);
        nk_layout_row_end(g_context);

        nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 5);
        nk_layout_row_push(g_context, 0.10f);
        if (!view->transport_available) {
            nk_widget_disable_begin(g_context);
        }
        const auto* play_label = view->transport_playing  ? "Pause"
                                 : view->transport_paused ? "Resume"
                                                          : "Play";
        if (nk_button_label(g_context, play_label) && view->transport_available) {
            action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE;
        }
        if (!view->transport_available) {
            nk_widget_disable_end(g_context);
        }
        nk_layout_row_push(g_context, 0.10f);
        const bool can_stop = view->transport_playing || view->transport_paused;
        if (!can_stop) {
            nk_widget_disable_begin(g_context);
        }
        if (nk_button_label(g_context, "Stop") && can_stop) {
            action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP;
        }
        if (!can_stop) {
            nk_widget_disable_end(g_context);
        }
        nk_layout_row_push(g_context, 0.22f);
        const auto sample_rate =
            view->transport_sample_rate == 0 ? 1U : view->transport_sample_rate;
        const auto total_seconds = view->transport_frame / sample_rate;
        const auto minutes = total_seconds / 60;
        const auto seconds = total_seconds % 60;
        nk_labelf(g_context, NK_TEXT_LEFT, "%s · %llu:%02llu",
                  view->transport_playing  ? "Playing"
                  : view->transport_paused ? "Paused"
                                           : "Stopped",
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds));
        nk_layout_row_push(g_context, 0.12f);
        if (nk_button_label(g_context,
                            view->player_preview.visible ? "Preview: On" : "Preview: Off") &&
            action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action.kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW;
        }
        nk_layout_row_push(g_context, 0.46f);
        nk_label(g_context, "Ctrl/Cmd+Space · Ctrl/Cmd+Shift+Space", NK_TEXT_RIGHT);
        nk_layout_row_end(g_context);

        if (view->transport_available && view->transport_end_frame != 0) {
            constexpr nk_size seek_steps = 10'000;
            const auto bounded_frame = std::min(view->transport_frame, view->transport_end_frame);
            nk_size seek_position = static_cast<nk_size>(
                static_cast<long double>(bounded_frame) /
                static_cast<long double>(view->transport_end_frame) * seek_steps);
            nk_layout_row_dynamic(g_context, 16, 1);
            if (nk_progress(g_context, &seek_position, seek_steps, NK_MODIFIABLE)) {
                action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK;
                action.transport_seek_frame =
                    (view->transport_end_frame / seek_steps) * seek_position +
                    ((view->transport_end_frame % seek_steps) * seek_position) / seek_steps;
            }
        }

        nk_layout_row_dynamic(g_context, 34, WORM_UI_WORKSPACE_TAB_COUNT);
        for (int tab = 0; tab < WORM_UI_WORKSPACE_TAB_COUNT; ++tab) {
            const WormUiWorkspaceTabItem* item = &view->tabs[tab];
            if (!item->label) {
                continue;
            }
            if (!item->enabled) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, item->label) && item->enabled &&
                tab != (int)view->active_tab) {
                action.kind = WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
                action.tab = (WormUiWorkspaceTab)tab;
            }
            if (!item->enabled) {
                nk_widget_disable_end(g_context);
            }
        }

        nk_layout_row_dynamic(g_context, 28, 1);
        nk_label(g_context, view->tab_heading, NK_TEXT_LEFT);

        float recovery_height = 0.0f;
        if (view->recovery_panel_visible) {
            recovery_height = 40.0f +
                              (view->recovery_notice && view->recovery_notice[0] ? 22.0f : 0.0f) +
                              (float)view->recovery_candidate_count * 48.0f +
                              (float)view->recovery_diagnostic_count * 18.0f;
            for (size_t row = 0; row < view->recovery_candidate_count; ++row) {
                const WormUiRecoveryCandidateView* candidate = &view->recovery_candidates[row];
                if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
                    recovery_height += 25.0f;
                    if (g_recovery_save_as_destination[0] == '\0') {
                        recovery_height += 18.0f;
                    }
                }
                if (candidate->disabled_reason && candidate->disabled_reason[0]) {
                    recovery_height += 20.0f;
                }
            }
            nk_layout_row_dynamic(g_context, 24, 1);
            nk_label(g_context, view->recovery_heading ? view->recovery_heading : "Recovery",
                     NK_TEXT_LEFT);
            if (view->recovery_notice && view->recovery_notice[0]) {
                nk_layout_row_dynamic(g_context, 22, 1);
                nk_label(g_context, view->recovery_notice, NK_TEXT_LEFT);
            }
            for (size_t row = 0; row < view->recovery_candidate_count; ++row) {
                const WormUiRecoveryCandidateView* candidate = &view->recovery_candidates[row];
                nk_layout_row_dynamic(g_context, 20, 1);
                nk_labelf(g_context, NK_TEXT_LEFT, "%s  %s",
                          candidate->path_label ? candidate->path_label : "(unknown recovery path)",
                          candidate->summary ? candidate->summary : "");

                nk_layout_row_dynamic(g_context, 25, 5);
                const struct {
                    size_t bit;
                    const char* label;
                    WormUiWorkspaceActionKind kind;
                } buttons[] = {
                    {WORM_UI_RECOVERY_ACTION_INSPECT, "Inspect",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT},
                    {WORM_UI_RECOVERY_ACTION_RECOVER, "Recover",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER},
                    {WORM_UI_RECOVERY_ACTION_SAVE_AS, "Save as",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS},
                    {WORM_UI_RECOVERY_ACTION_DISCARD, "Discard",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD},
                    {WORM_UI_RECOVERY_ACTION_QUARANTINE, "Quarantine",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE},
                };
                for (size_t button = 0; button < sizeof(buttons) / sizeof(buttons[0]); ++button) {
                    const int offered = recovery_action_offered(candidate, buttons[button].bit);
                    if (!offered) {
                        nk_widget_disable_begin(g_context);
                    }
                    const int clicked = nk_button_label(g_context, buttons[button].label);
                    if (!offered) {
                        nk_widget_disable_end(g_context);
                    }
                    int save_as_ready = 1;
                    if (buttons[button].kind == WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS) {
                        save_as_ready = g_recovery_save_as_destination[0] != '\0';
                    }
                    if (offered && save_as_ready && clicked &&
                        action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                        set_recovery_action(&action, candidate, buttons[button].kind);
                    }
                }
                if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
                    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
                    nk_layout_row_push(g_context, 0.18f);
                    nk_label(g_context, "New project path", NK_TEXT_LEFT);
                    nk_layout_row_push(g_context, 0.82f);
                    (void)nk_edit_string_zero_terminated(
                        g_context, NK_EDIT_FIELD, g_recovery_save_as_destination,
                        sizeof(g_recovery_save_as_destination), nk_filter_default);
                    nk_layout_row_end(g_context);
                    if (g_recovery_save_as_destination[0] == '\0') {
                        nk_layout_row_dynamic(g_context, 18, 1);
                        nk_label(g_context,
                                 "Save as is disabled until a new destination is entered.",
                                 NK_TEXT_LEFT);
                    }
                }
                if (candidate->disabled_reason && candidate->disabled_reason[0]) {
                    nk_layout_row_dynamic(g_context, 20, 1);
                    nk_label(g_context, candidate->disabled_reason, NK_TEXT_LEFT);
                }
            }
            if (view->recovery_rows_truncated) {
                nk_layout_row_dynamic(g_context, 18, 1);
                nk_label(g_context, "Additional recovery candidates are not shown.", NK_TEXT_LEFT);
            }
            for (size_t diagnostic = 0; diagnostic < view->recovery_diagnostic_count;
                 ++diagnostic) {
                if (view->recovery_diagnostics[diagnostic]) {
                    nk_layout_row_dynamic(g_context, 18, 1);
                    nk_label(g_context, view->recovery_diagnostics[diagnostic], NK_TEXT_LEFT);
                }
            }
        }

        float content_height = height > 300 ? (float)height - 165.0f - recovery_height : 80.0f;
        if (content_height < 80.0f) {
            content_height = 80.0f;
        }
        nk_layout_row_dynamic(g_context, content_height, 1);
        if (nk_group_begin(g_context, "workspace-content", NK_WINDOW_BORDER)) {
            if (view->metadata.visible) {
                worm_ui_nuklear_render_metadata_panel(g_context, &view->metadata, &action,
                                                      g_window_focused);
            } else if (view->timing.visible) {
                worm_ui_nuklear_render_timing_panel(g_context, &view->timing, &action,
                                                    g_window_focused, g_double_clicks.data(),
                                                    g_double_click_count);
            } else if (view->compose.visible) {
                render_compose_field(&view->compose, &action);
            } else if (view->audio_slices.visible) {
                worm_ui_nuklear_render_audio_slices(g_context, &view->audio_slices, &action, true,
                                                    g_window_focused);
            } else {
                nk_layout_row_dynamic(g_context, 24, 1);
                if (view->summary_line_count == 0) {
                    nk_label(g_context, "No items in this view.", NK_TEXT_LEFT);
                }
                for (size_t line = 0; line < view->summary_line_count; ++line) {
                    if (view->summary_lines[line]) {
                        nk_label(g_context, view->summary_lines[line], NK_TEXT_LEFT);
                    }
                }
            }
            nk_group_end(g_context);
        }

        nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 5);
        nk_layout_row_push(g_context, 0.16f);
        nk_labelf(g_context, NK_TEXT_LEFT, "History: %s",
                  view->history_available ? "available" : "unavailable");
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Undo: %zu", view->undo_depth);
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Redo: %zu", view->redo_depth);
        nk_layout_row_push(g_context, 0.52f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Revision build: %s", view->git_hash);
        nk_layout_row_push(g_context, 0.12f);
        if (nk_button_label(g_context, "Quit")) {
            action.kind = WORM_UI_WORKSPACE_ACTION_QUIT;
        }
        nk_layout_row_end(g_context);
    }
    nk_end(g_context);
    worm_ui_nuklear_render_player_preview(g_context, &view->player_preview, &action, width, height);
    const bool compose_lane_focused = g_window_focused && g_compose.keyboard_focus &&
                                      view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE &&
                                      view->compose.visible && view->compose.scene_available &&
                                      view->compose.cursor && view->compose.cursor->lane_id != 0;
    expose_shortcut_input(&action, compose_lane_focused,
                          compose_lane_focused ? view->compose.cursor->lane_id : 0);
    g_audio.offset_escape_pending = false;
    return action;
}

void worm_ui_frontend_render(int window_width, int window_height) {
    (void)window_width;
    (void)window_height;
    if (g_context) {
        nk_sdl_render(NK_ANTI_ALIASING_ON, WORMEDIT_MAX_VERTEX_MEMORY, WORMEDIT_MAX_ELEMENT_MEMORY);
    }
}
