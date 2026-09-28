#define NK_IMPLEMENTATION
#define NK_SDL3_RENDERER_IMPLEMENTATION

#include "ui/frontend.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/image_quad.h"
#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/metadata_state.h"
#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/timing_panel.h"
#include "ui/nuklear/timing_state.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL.h>
#include <demo/sdl3_renderer/nuklear_sdl3_renderer.h>

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stddef.h>

static SDL_Window* g_window;
static SDL_Renderer* g_renderer;
static struct nk_context* g_context;
static char g_recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
static auto& g_audio = worm_ui_nuklear_audio_state();

static std::array<worm_ui_nuklear_pointer_click_t, 16> g_double_clicks{};
static size_t g_double_click_count{};
static bool g_window_focused{true};
static std::array<WormUiShortcutEvent, WORM_UI_SHORTCUT_EVENT_CAPACITY> g_shortcut_events{};
static size_t g_shortcut_event_count{};
static bool g_shortcut_events_overflowed{};
static bool g_ime_composition_active{};
static int g_last_window_width{};
static int g_last_window_height{};

static bool frontend_window_size(int* width, int* height) {
    int current_width = 0;
    int current_height = 0;
    if (g_window && SDL_GetWindowSize(g_window, &current_width, &current_height) &&
        current_width > 0 && current_height > 0) {
        g_last_window_width = current_width;
        g_last_window_height = current_height;
    }
    if (g_last_window_width <= 0 || g_last_window_height <= 0) {
        return false;
    }
    *width = g_last_window_width;
    *height = g_last_window_height;
    return true;
}

void worm_ui_nuklear_append_image_quad(void* canvas, short, short, unsigned short, unsigned short,
                                       nk_handle callback_data) {
    auto* list = static_cast<struct nk_draw_list*>(canvas);
    const auto* quad = static_cast<const WormUiNuklearImageQuad*>(callback_data.ptr);
    if (!list || !quad) {
        return;
    }
    nk_draw_list_push_image(list, quad->image.handle);
    const auto base = static_cast<nk_draw_index>(list->vertex_count);
    auto* vertices = nk_draw_list_alloc_vertices(list, 4);
    auto* indices = nk_draw_list_alloc_elements(list, 6);
    if (!vertices || !indices) {
        return;
    }
    indices[0] = static_cast<nk_draw_index>(base + 0);
    indices[1] = static_cast<nk_draw_index>(base + 1);
    indices[2] = static_cast<nk_draw_index>(base + 2);
    indices[3] = static_cast<nk_draw_index>(base + 0);
    indices[4] = static_cast<nk_draw_index>(base + 2);
    indices[5] = static_cast<nk_draw_index>(base + 3);
    struct nk_colorf color{};
    nk_color_fv(&color.r, quad->color);
    for (std::size_t index = 0; index < 4; ++index) {
        vertices =
            nk_draw_vertex(vertices, &list->config, quad->points[index], quad->uvs[index], color);
    }
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
    const auto whole_steps = std::trunc(event->x) == event->x && std::trunc(event->y) == event->y &&
                             (event->x != 0.0f || event->y != 0.0f);
    // Convert SDL's positive-away wheel convention into content-offset deltas.
    // FLIPPED sources already report the opposite sign, so they need no negation.
    const auto direction_sign = event->direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0 : -1.0;
    worm_ui_nuklear_queue_compose_wheel(static_cast<double>(event->x) * direction_sign,
                                        static_cast<double>(event->y) * direction_sign,
                                        event->mouse_x, event->mouse_y,
                                        static_cast<std::uint16_t>(SDL_GetModState()), whole_steps);
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

bool worm_ui_frontend_init(SDL_Window* window, SDL_Renderer* renderer) {
    if (!window || !renderer || g_context) {
        return false;
    }
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_reset_timing_state();
    worm_ui_nuklear_reset_audio_state();
    worm_ui_nuklear_reset_compose_panel();

    struct nk_context* context = nk_sdl_init(window, renderer, nk_sdl_allocator());
    if (!context) {
        return false;
    }
    struct nk_font_atlas* atlas = nk_sdl_font_stash_begin(context);
    (void)atlas;
    nk_sdl_font_stash_end(context);

    if (!SDL_StartTextInput(window)) {
        nk_sdl_shutdown(context);
        return false;
    }
    g_window = window;
    g_renderer = renderer;
    g_context = context;
    g_window_focused = true;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_double_click_count = 0;
    g_last_window_width = 0;
    g_last_window_height = 0;
    return true;
}

void worm_ui_frontend_shutdown(void) {
    if (!g_context) {
        worm_ui_nuklear_reset_metadata_state();
        worm_ui_nuklear_reset_timing_state();
        worm_ui_nuklear_reset_audio_state();
        worm_ui_nuklear_reset_compose_panel();
        return;
    }
    if (g_window) {
        SDL_StopTextInput(g_window);
    }
    worm_ui_nuklear_shutdown_player_preview();
    nk_sdl_shutdown(g_context);
    g_window = NULL;
    g_renderer = NULL;
    g_context = NULL;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_double_click_count = 0;
    g_last_window_width = 0;
    g_last_window_height = 0;
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_reset_timing_state();
    worm_ui_nuklear_reset_audio_state();
    worm_ui_nuklear_reset_compose_panel();
}

void worm_ui_frontend_begin_input(void) {
    if (g_context) {
        nk_input_begin(g_context);
        g_shortcut_event_count = 0;
        g_shortcut_events_overflowed = false;
        g_double_click_count = 0;
    }
}

WormUiMetadataDropTarget worm_ui_frontend_metadata_drop_target(uint32_t window_id, float x,
                                                                float y) {
    if (!g_context || !g_window ||
        (window_id != 0 && window_id != SDL_GetWindowID(g_window)) || !std::isfinite(x) ||
        !std::isfinite(y))
        return {};
    return worm_ui_nuklear_metadata_drop_target(x, y);
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        g_window_focused = false;
        g_audio.offset_cancel_pending = true;
        g_shortcut_event_count = 0;
        g_double_click_count = 0;
        worm_ui_nuklear_compose_handle_focus_lost();
        worm_ui_nuklear_timing_handle_focus_lost();
        g_ime_composition_active = false;
    } else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        g_window_focused = true;
    } else if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        const bool down = event->type == SDL_EVENT_KEY_DOWN;
        if (down && !event->key.repeat && event->key.key == SDLK_ESCAPE) {
            g_audio.offset_escape_pending = true;
        }
        worm_ui_nuklear_queue_compose_key(static_cast<std::uint32_t>(event->key.key),
                                          static_cast<std::uint32_t>(event->key.scancode),
                                          static_cast<std::uint16_t>(event->key.mod), down,
                                          event->key.repeat);
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
        worm_ui_nuklear_queue_compose_middle_click(event->button.x, event->button.y);
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
    return nk_sdl_handle_event(g_context, &mutable_event) != 0;
}

void worm_ui_frontend_end_input(void) {
    if (g_context) {
        nk_input_end(g_context);
    }
}

static void render_workspace_error(WormUiWorkspaceAction* action, int width, int height,
                                   const char* diagnostic) {
    worm_ui_nuklear_compose_discard_frame_input();
    worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
    if (nk_begin(g_context, "wormedit-workspace", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_layout_row_dynamic(g_context, 30, 1);
        nk_label(g_context, "The editor could not display this workspace frame.", NK_TEXT_LEFT);
        nk_layout_row_dynamic(g_context, 72, 1);
        nk_label_wrap(g_context, diagnostic && diagnostic[0]
                                     ? diagnostic
                                     : "No diagnostic was provided for the workspace error.");
        nk_layout_row_dynamic(g_context, 30, 2);
        if (nk_button_label(g_context, "Setup")) {
            action->kind = WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
            action->tab = WORM_UI_WORKSPACE_TAB_SETUP;
        }
        if (nk_button_label(g_context, "Quit")) {
            action->kind = WORM_UI_WORKSPACE_ACTION_QUIT;
        }
    }
    nk_end(g_context);
    expose_shortcut_input(action);
}

WormUiWorkspaceAction worm_ui_frontend_workspace_error(const char* diagnostic) {
    worm_ui_nuklear_begin_image_quad_frame();
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_NONE;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    if (!g_context || !g_window) {
        return action;
    }
    int width = 0;
    int height = 0;
    if (!frontend_window_size(&width, &height)) {
        return action;
    }
    render_workspace_error(&action, width, height, diagnostic);
    return action;
}

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
    worm_ui_nuklear_begin_image_quad_frame();
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_NONE;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    const bool timing_tab_active = view && view->active_tab == WORM_UI_WORKSPACE_TAB_TIMING;
    worm_ui_nuklear_reconcile_timing_gestures(view ? &view->timing : nullptr, timing_tab_active,
                                              g_window_focused);
    if (!g_context || !g_window || !view) {
        worm_ui_nuklear_compose_discard_frame_input();
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        expose_shortcut_input(&action);
        return action;
    }

    const char* validation_error = nullptr;
    if (!view->project_title || !view->artist || !view->selected_difficulty ||
        !view->status_text || !view->git_hash || !view->tab_heading ||
        view->summary_line_count > WORM_UI_WORKSPACE_SUMMARY_CAPACITY ||
        view->recovery_candidate_count > WORM_UI_RECOVERY_ROW_CAPACITY ||
        view->recovery_diagnostic_count > WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY ||
        view->active_tab < WORM_UI_WORKSPACE_TAB_SETUP ||
        view->active_tab > WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE) {
        validation_error = "Workspace view data is invalid.";
    } else if (!worm_ui_nuklear_setup_view_valid(&view->setup)) {
        validation_error = "Setup view data is invalid.";
    } else if (view->metadata.visible &&
               (view->metadata.format_field_count > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY ||
                view->metadata.format_target_count > WORM_UI_METADATA_FORMAT_TARGET_CAPACITY ||
                view->metadata.audio_waveform_column_count >
                    WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY ||
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
                  !view->metadata.audio_waveform_has_selected_asset)))) {
        validation_error = "Metadata view data is invalid.";
    } else if ((view->timing.visible || view->compose.visible) &&
               !worm_ui_nuklear_timing_view_valid(&view->timing)) {
        validation_error = "Timing view data is invalid.";
    } else if (view->audio_slices.visible &&
               !worm_ui_nuklear_audio_slices_view_valid(&view->audio_slices)) {
        validation_error = "Audio slices view data is invalid.";
    } else if (!worm_ui_nuklear_player_preview_view_valid(&view->player_preview)) {
        validation_error = "Player preview view data is invalid.";
    } else if (view->compose.visible &&
               !worm_ui_nuklear_compose_view_valid(&view->compose)) {
        validation_error = "Compose view data is invalid.";
    }

    int width = 0;
    int height = 0;
    if (!frontend_window_size(&width, &height)) {
        worm_ui_nuklear_compose_discard_frame_input();
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        expose_shortcut_input(&action);
        return action;
    }

    if (validation_error) {
        render_workspace_error(&action, width, height, validation_error);
        return action;
    }

    worm_ui_nuklear_emit_timing_cancellations(&view->timing, timing_tab_active, &action);

    const bool compose_tab_active = view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    worm_ui_nuklear_prepare_compose_panel(&view->compose, compose_tab_active, g_window_focused,
                                          &action);

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
            if (view->setup.visible) {
                worm_ui_nuklear_render_setup_panel(g_context, &view->setup, &action);
            } else if (view->metadata.visible) {
                worm_ui_nuklear_render_metadata_panel(g_context, &view->metadata, &action,
                                                      g_window_focused);
            } else if (view->timing.visible) {
                worm_ui_nuklear_render_timing_panel(g_context, &view->timing, &action,
                                                    g_window_focused, g_double_clicks.data(),
                                                    g_double_click_count);
            } else if (view->compose.visible) {
                worm_ui_nuklear_render_compose_panel(
                    g_context, &view->compose, &view->timing, &action, g_window_focused,
                    g_ime_composition_active, g_double_clicks.data(), g_double_click_count,
                    g_shortcut_event_count);
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
    worm_ui_nuklear_render_player_preview(g_context, g_renderer, &view->player_preview, &action,
                                          width, height);
    const bool compose_lane_focused =
        worm_ui_nuklear_compose_lane_focused(&view->compose, compose_tab_active, g_window_focused);
    expose_shortcut_input(&action, compose_lane_focused,
                          compose_lane_focused ? view->compose.cursor->lane_id : 0);
    g_audio.offset_escape_pending = false;
    return action;
}

void worm_ui_frontend_resource_placed(uint64_t placed_resource_id, uint64_t next_resource_id) {
    worm_ui_nuklear_advance_resource_picker(placed_resource_id, next_resource_id);
}

void worm_ui_frontend_render(int window_width, int window_height) {
    if (window_width <= 0 || window_height <= 0) {
        (void)frontend_window_size(&window_width, &window_height);
    }
    if (g_context && g_renderer && window_width > 0 && window_height > 0) {
        // Nuklear and SDL input use window-coordinate points, while a high-DPI
        // renderer targets the larger pixel backbuffer. Keep the renderer's
        // logical space synchronized after every resize so geometry, clipping,
        // hit testing, and the cleared background cover the same full window.
        if (!SDL_SetRenderLogicalPresentation(g_renderer, window_width, window_height,
                                              SDL_LOGICAL_PRESENTATION_STRETCH)) {
            // A renderer can reject a new logical presentation after a resize.
            // Keep presenting the current Nuklear frame instead of leaving the
            // previous backbuffer (or a blank one) on screen indefinitely.
            (void)SDL_SetRenderLogicalPresentation(g_renderer, 0, 0,
                                                   SDL_LOGICAL_PRESENTATION_DISABLED);
        }
        SDL_SetRenderDrawColor(g_renderer, 18, 20, 26, 255);
        SDL_RenderClear(g_renderer);
        nk_sdl_render(g_context, NK_ANTI_ALIASING_ON);
    }
}
