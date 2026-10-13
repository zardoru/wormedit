#include "ui/field/rect.h"
#include "ui/nuklear/modal_input.h"
#include "ui/nuklear/text_fit.h"
#include "wormedit/config.h"
#include "ui/nuklear/catalog_confirmation.h"
#include "ui/workspace_menu.h"
#include "ui/nuklear/menu_bar.h"
#if defined(WORMEDIT_HAS_MACOS_MENU)
#include "sys/macos_menu.h"
#endif
#include "ui/nuklear/ui_gallery.h"
#include "ui/nuklear/sound_import_dialog.h"
#include "ui/nuklear/external_player_panel.h"
#include "ui/nuklear/options_window.h"
#include "ui/nuklear/tap_timing_panel.h"
#include "ui/nuklear/timing_settings_panel.h"

#include "ui/frontend.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_layers_panel.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_snap_grid.h"
#include "ui/nuklear/sound_import_runtime.h"
#include "ui/nuklear/compose_bga_panel.h"
#include "ui/nuklear/compose_input_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
#include "ui/nuklear/chart_import_dialog.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/compose_recipe_menu.h"
#include "ui/nuklear/content_margin.h"
#include "ui/nuklear/image_quad.h"
#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/metadata_state.h"
#include "ui/nuklear/command_menu.h"
#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/timing_stage_editor.h"
#include "ui/nuklear/timing_state.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/notification_toasts.h"
#include "ui/nuklear/notification_panel.h"
#include "ui/nuklear/compare_panel.h"

#include "ui/nuklear/sdl_backend.h"
#include "ui/nuklear/font_atlas.h"

#include <wormedit/formats/bms.h>

#include <SDL3/SDL.h>
class RenderSubphaseScope {
  public:
    explicit RenderSubphaseScope(WormUiFrameRenderPhase phase)
        : previous_(worm_ui_frame_render_phase_begin(phase)) {}
    ~RenderSubphaseScope() { worm_ui_frame_render_phase_end(previous_); }
    RenderSubphaseScope(const RenderSubphaseScope&) = delete;
    auto operator=(const RenderSubphaseScope&) -> RenderSubphaseScope& = delete;
  private:
    uint32_t previous_;
};

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stddef.h>
#include <string>
#include <utility>

using wormedit::ui::lucide::Glyph;
static bool g_menu_popup_open{};
enum class HelpDialog { none, about, shortcuts };
static HelpDialog g_help_dialog{};
static wormedit::ui::chrome::Dialog g_about_dialog;
static bool g_menu_popup_release_pending{};

static wormedit::ui::nuklear::editor_font_t g_editor_font;

static bool g_frame_timing_visible;
static bool g_compose_area_shortcuts_active;
static bool g_context_invocation_allowed;
static std::uint64_t g_context_project{}, g_context_chart{}, g_context_revision{};
static bool g_project_export_available{};
static WormUiWorkspaceTab g_context_tab{WORM_UI_WORKSPACE_TAB_METADATA};
static bool g_save_project_requested;
static uint64_t g_save_project_request_id;
// Layout-independent Compose hotkeys read from typed text: [ ] { } change the
// playback rate and + - zoom, wherever the layout puts those characters.
static std::array<char, 8> g_compose_text_hotkeys{};
static std::size_t g_compose_text_hotkey_count{};
static SDL_Window* g_window;
static SDL_Renderer* g_renderer;
static struct nk_context* g_context;
static bool g_headless;
static SDL_Cursor* g_stem_resize_cursor;
static bool g_stem_resize_cursor_attempted;
static bool g_stem_resize_cursor_applied;

static void apply_stem_resize_cursor(bool requested) {
    if (g_headless || !g_window || !(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return;
    if (requested && !g_stem_resize_cursor_attempted) {
        g_stem_resize_cursor_attempted = true;
        g_stem_resize_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    }
    const auto apply = requested && g_stem_resize_cursor;
    if (apply == g_stem_resize_cursor_applied) return;
    auto* cursor = apply ? g_stem_resize_cursor : SDL_GetDefaultCursor();
    if (cursor && SDL_SetCursor(cursor)) g_stem_resize_cursor_applied = apply;
}
static void shutdown_stem_resize_cursor() {
    apply_stem_resize_cursor(false);
    if (g_stem_resize_cursor && (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
        SDL_DestroyCursor(g_stem_resize_cursor);
    g_stem_resize_cursor = nullptr;
    g_stem_resize_cursor_applied = g_stem_resize_cursor_attempted = false;
}

static void clear_inactive_stem_resize_cursor(const WormUiWorkspaceView& view, bool focused) {
    if (!g_compose_area_shortcuts_active || !focused || view.compose.input_occluded) {
        apply_stem_resize_cursor(false);
    }
}
static int g_headless_width;
static int g_headless_height;
static char g_recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
static auto& g_audio = worm_ui_nuklear_audio_state();

static std::array<worm_ui_nuklear_pointer_click_t, 16> g_double_clicks{};
static size_t g_double_click_count{};
static bool g_window_focused{true};
static wormedit::ui::nuklear::notification_toast_state_t g_notification_state;
static wormedit::ui::nuklear::notification_panel_state_t g_notification_history_state;
static bool g_notification_history_close_pending;
static WormUiNotificationView g_notification_view{};
static nk_context g_notification_context{};
static bool g_notification_context_initialized{};
static bool g_notification_modal{};
static bool g_notification_project_modal{};
static bool g_notification_escape_captured{};
// Esc deselects before it dismisses a toast: set each frame while Compose has something to clear.
static bool g_compose_escape_has_selection{};

static void render_notification_frame(const WormUiNotificationView& view,
    WormUiWorkspaceAction& action, int width, int height, bool project_modal);

static std::array<WormUiShortcutEvent, WORM_UI_SHORTCUT_EVENT_CAPACITY> g_shortcut_events{};
static size_t g_shortcut_event_count{};
static bool g_tap_timing_visible{};
static bool g_tap_key_captured{};
static bool g_shortcut_events_overflowed{};
static bool g_ime_composition_active{};
static int g_last_window_width{};
static int g_last_window_height{};

struct ProjectDialogState {
    std::mutex mutex;
    std::uint64_t generation{};
    WormUiWorkspaceActionKind kind{WORM_UI_WORKSPACE_ACTION_NONE};
    bool active{};
    bool ready{};
    std::string path;
    std::string error;
    WormUiNotificationSeverity severity{WORM_UI_NOTIFICATION_ERROR};
    WormUiNotificationProducerSource source{WORM_UI_NOTIFICATION_PRODUCER_SYSTEM};
};

static ProjectDialogState g_project_dialog;
static std::string g_project_dialog_error;
static void* g_notification_sink_context{};
static WormUiNotificationPostCallback g_notification_post{};

void worm_ui_frontend_set_notification_sink(void* context, WormUiNotificationPostCallback callback) {
    g_notification_sink_context = context;
    g_notification_post = callback;
}

void worm_ui_frontend_notify(WormUiNotificationSeverity severity,
    WormUiNotificationProducerSource source, const char* message) {
    if (g_notification_post && message && message[0])
        g_notification_post(g_notification_sink_context, severity, source, message);
}

struct NewProjectWizardState {
    bool open{};
    bool audio{};
    std::uint8_t layout_index{3};
    std::string audio_path;
    std::string last_pending_audio_path;
};

static NewProjectWizardState g_new_project_wizard;
static std::string g_bms_encoding_path_seen;
static int g_bms_encoding_choice{};

static void request_manual_new_project() {
    g_new_project_wizard.open = true;
    g_new_project_wizard.audio = false;
    g_new_project_wizard.layout_index = 3;
    g_new_project_wizard.audio_path.clear();
}

static void sync_pending_audio_project(const WormUiWorkspaceView& view) {
    const std::string pending = view.pending_new_project_audio_path
                                    ? view.pending_new_project_audio_path : "";
    if (pending.empty()) {
        g_new_project_wizard.last_pending_audio_path.clear();
        if (g_new_project_wizard.audio) g_new_project_wizard.open = false;
        return;
    }
    if (pending == g_new_project_wizard.last_pending_audio_path) return;
    g_new_project_wizard.last_pending_audio_path = pending;
    g_new_project_wizard.audio_path = pending;
    g_new_project_wizard.audio = true;
    g_new_project_wizard.layout_index = 0;
    g_new_project_wizard.open = true;
}

static void reset_project_dialog() {
    std::lock_guard lock{g_project_dialog.mutex};
    ++g_project_dialog.generation;
    g_project_dialog.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    g_project_dialog.active = false;
    g_project_dialog.ready = false;
    g_project_dialog.path.clear();
    g_project_dialog.error.clear();
    g_project_dialog_error.clear();
}

static bool project_dialog_active() {
    std::lock_guard lock{g_project_dialog.mutex};
    return g_project_dialog.active || worm_ui_nuklear_setup_popup_owns_input();
}

static bool nuklear_text_entry_active(void);
static bool compose_popup_owns_input(bool include_menu = true);

bool worm_ui_frontend_request_new_project(void) {
    if (project_dialog_active() || g_new_project_wizard.open || g_notification_modal ||
        !g_bms_encoding_path_seen.empty())
        return false;
    request_manual_new_project();
    return true;
}

bool worm_ui_frontend_request_save_project(uint64_t project_id) {
    if ((!g_window && !g_headless) || project_dialog_active() || g_new_project_wizard.open ||
        g_notification_project_modal || nuklear_text_entry_active() || compose_popup_owns_input(false))
        return false;
    g_save_project_requested = true;
    g_save_project_request_id = project_id;
    return true;
}

static void SDLCALL project_file_dialog_callback(void* userdata,
                                                 const char* const* files, int) {
    std::unique_ptr<std::uint64_t> generation{static_cast<std::uint64_t*>(userdata)};
    if (!generation) return;
    std::string path;
    std::string error;
    auto severity = WORM_UI_NOTIFICATION_ERROR;
    auto source = WORM_UI_NOTIFICATION_PRODUCER_SYSTEM;
    if (!files) {
        const auto* sdl_error = SDL_GetError();
        error = sdl_error && sdl_error[0] ? sdl_error : "File picker failed.";
    } else if (files[0]) {
        std::size_t length{};
        while (length < WORM_UI_PROJECT_PATH_CAPACITY && files[0][length]) ++length;
        if (length == WORM_UI_PROJECT_PATH_CAPACITY) {
            error = "Selected project path is too long.";
            severity = WORM_UI_NOTIFICATION_WARNING;
            source = WORM_UI_NOTIFICATION_PRODUCER_INPUT;
        } else
            path.assign(files[0], length);
    }
    std::lock_guard lock{g_project_dialog.mutex};
    if (!g_project_dialog.active || g_project_dialog.generation != *generation) return;
    g_project_dialog.path = std::move(path);
    g_project_dialog.error = std::move(error);
    g_project_dialog.severity = severity;
    g_project_dialog.source = source;
    g_project_dialog.ready = true;
}

static uint64_t begin_project_file_dialog(WormUiWorkspaceActionKind kind) {
    std::lock_guard lock{g_project_dialog.mutex};
    if (g_project_dialog.active) return 0;
    const auto generation = ++g_project_dialog.generation;
    g_project_dialog.active = true;
    g_project_dialog.ready = false;
    g_project_dialog.kind = kind;
    return generation;
}

static void start_project_file_dialog(WormUiWorkspaceActionKind kind) {
    const auto generation = begin_project_file_dialog(kind);
    if (!generation) return;
    auto token = std::unique_ptr<std::uint64_t>{new (std::nothrow) std::uint64_t{generation}};
    if (!token) {
        reset_project_dialog();
        g_project_dialog_error = "File picker could not allocate callback state.";
        worm_ui_frontend_notify(WORM_UI_NOTIFICATION_ERROR, WORM_UI_NOTIFICATION_PRODUCER_SYSTEM,
                               g_project_dialog_error.c_str());
        return;
    }
    g_project_dialog_error.clear();
    static constexpr SDL_DialogFileFilter open_filters[] = {
        {"Chart or project", "wormedit;bms;bme;bml;pms;bmson;sm;ssc;osu"},
    };
    static constexpr SDL_DialogFileFilter save_filters[] = {
        {"wormedit project", "wormedit"},
    };
    if (kind == WORM_UI_WORKSPACE_ACTION_OPEN_FILE || kind == WORM_UI_WORKSPACE_ACTION_IMPORT_CHART_FILE)
        SDL_ShowOpenFileDialog(project_file_dialog_callback, token.release(), g_window,
                               open_filters, SDL_arraysize(open_filters), nullptr, false);
    else
        SDL_ShowSaveFileDialog(project_file_dialog_callback, token.release(), g_window,
                               save_filters, SDL_arraysize(save_filters), "Untitled.wormedit");
}

static void consume_project_file_dialog(WormUiWorkspaceAction& action) {
    std::unique_lock lock{g_project_dialog.mutex};
    if (!g_project_dialog.active || !g_project_dialog.ready) return;
    g_project_dialog.active = false;
    g_project_dialog.ready = false;
    g_project_dialog_error = std::move(g_project_dialog.error);
    if (g_project_dialog_error.empty() && !g_project_dialog.path.empty() &&
        action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        action.kind = g_project_dialog.kind;
        std::memcpy(action.project_path, g_project_dialog.path.data(),
                    g_project_dialog.path.size());
        action.project_path[g_project_dialog.path.size()] = '\0';
    }
    g_project_dialog.path.clear();
    g_project_dialog.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    const auto severity = g_project_dialog.severity;
    const auto source = g_project_dialog.source;
    lock.unlock();
    worm_ui_frontend_notify(severity, source, g_project_dialog_error.c_str());
}

uint64_t worm_ui_frontend_begin_project_dialog_for_testing(WormUiWorkspaceActionKind kind) {
    return begin_project_file_dialog(kind);
}

void worm_ui_frontend_project_dialog_callback_for_testing(uint64_t generation, const char* const* files) {
    project_file_dialog_callback(new std::uint64_t{generation}, files, 0);
}

WormUiWorkspaceAction worm_ui_frontend_consume_project_dialog_for_testing(void) {
    WormUiWorkspaceAction action{};
    consume_project_file_dialog(action);
    return action;
}

static bool frontend_event_window_matches(uint32_t window_id, bool allow_zero = false) {
    if (g_headless) return window_id == 0;
    return g_window && (window_id == SDL_GetWindowID(g_window) ||
                        (allow_zero && window_id == 0));
}

static bool frontend_window_size(int* width, int* height) {
    if (g_headless) {
        if (width) *width = g_headless_width;
        if (height) *height = g_headless_height;
        return g_headless_width > 0 && g_headless_height > 0;
    }
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

static bool nuklear_text_entry_active(void);
static bool project_modal_active(const WormUiWorkspaceView& view);
static bool external_project_modal_active(const WormUiWorkspaceView& view);
static bool notification_history_focused();

// Ctrl+A with the pointer over the Sounds or BGA dock list selects the rows shown there instead
// of the chart's notes. An empty list selects nothing and still consumes the key.
static bool select_all_in_hovered_dock_list(const SDL_KeyboardEvent& event) {
    constexpr auto others = SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI;
    if (!g_context || event.scancode != SDL_SCANCODE_A || (event.mod & SDL_KMOD_CTRL) == 0 ||
        (event.mod & others) != 0 || nuklear_text_entry_active() ||
        !g_compose_area_shortcuts_active) return false;
    auto& state = worm_ui_nuklear_compose_state();
    const auto x = g_context->input.mouse.pos.x;
    const auto y = g_context->input.mouse.pos.y;
    if (worm_ui_nuklear_sound_list_contains(x, y)) {
        worm_ui_keysound_select_all(state.keysound_catalog.keysound_selection, state.keysound_catalog.keysound_shown);
        return true;
    }
    const auto in_bga = state.bga_catalog.bga_list_valid && wormedit::ui::is_point_in_rect({state.bga_catalog.bga_list_x, state.bga_catalog.bga_list_y,
            state.bga_catalog.bga_list_width, state.bga_catalog.bga_list_height}, x, y);
    if (!in_bga) return false;
    worm_ui_keysound_select_all(state.bga_catalog.bga_selection, state.bga_catalog.bga_shown);
    return true;
}

static void queue_shortcut_key_event(const SDL_KeyboardEvent* event, bool tap_capture = false) {
    if (!event || (!g_window && !g_headless) || !g_window_focused ||
        event->type != SDL_EVENT_KEY_DOWN || !event->down ||
        !frontend_event_window_matches(event->windowID) || g_shortcut_events_overflowed) {
        return;
    }

    if (worm_ui_nuklear_setup_popup_owns_input()) return;
    if (!tap_capture && select_all_in_hovered_dock_list(*event)) return;

    if (!tap_capture && compose_area_consumes_shortcut({g_compose_area_shortcuts_active, g_window_focused,
        project_dialog_active() || g_new_project_wizard.open,
        nuklear_text_entry_active(), compose_area_capture_active(),
        compose_area_band_visible() && worm_ui_nuklear_compose_state().input_queue.keyboard_focus,
        event->key == SDLK_ESCAPE})) return;

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
        .timestamp_ns = event->timestamp,
        .tap_timing_space = tap_capture && event->scancode == SDL_SCANCODE_SPACE,
    };
}

static bool nuklear_text_entry_active(void) {
    if (worm_ui_nuklear_compose_state().inline_offset.bgm_column_count_edit.input_owned) {
        return true;
    }
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
    action->shortcut_event_count = (g_help_dialog != HelpDialog::none) || g_new_project_wizard.open || !g_bms_encoding_path_seen.empty() || worm_ui_nuklear_setup_popup_owns_input()
                                       ? 0 : g_shortcut_event_count;
    for (size_t index = 0; index < g_shortcut_event_count; ++index) {
        action->shortcut_events[index] = g_shortcut_events[index];
    }
    action->shortcut_overflowed = g_shortcut_events_overflowed;
    action->shortcut_window_focused = g_window_focused;
    action->shortcut_notification_history_focused = notification_history_focused();
    auto& compose_state = worm_ui_nuklear_compose_state();
    action->shortcut_text_entry_active = command_menu_owns_input() || nuklear_text_entry_active() ||
                                         compose_state.inline_timing.inline_timing_input_consumed ||
                                         compose_state.inline_offset.inline_offset_input_consumed ||
                                         compose_state.layer_editor.layers_menu_input_consumed ||
                                         compose_note_menu_consumed() || compose_quantize_owns_input() ||
                                         wormedit::ui::chrome::context_menu_owns_input(compose_state.snap_grid.snap_grid_menu);
    compose_state.inline_timing.inline_timing_input_consumed = false;
    compose_state.inline_offset.inline_offset_input_consumed = false;
    compose_state.layer_editor.layers_menu_input_consumed = false;
    action->shortcut_ime_composition_active = g_ime_composition_active;
    action->shortcut_compose_lane_focused = compose_lane_focused && g_window_focused;
    action->shortcut_focused_lane_id = action->shortcut_compose_lane_focused ? focused_lane_id : 0;

    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
}

static bool notification_editor_text_owned();

static void queue_compose_text_hotkeys(const char* text) {
    if (!text || !g_compose_area_shortcuts_active || !g_window_focused ||
        notification_editor_text_owned())
        return;
    for (const char* character = text; *character != '\0'; ++character) {
        if (std::strchr("+-[]{}", *character) == nullptr ||
            g_compose_text_hotkey_count == g_compose_text_hotkeys.size())
            continue;
        g_compose_text_hotkeys[g_compose_text_hotkey_count++] = *character;
    }
}

// Turns the first queued hotkey into a Compose action; the rest are dropped.
static void consume_compose_text_hotkeys(WormUiWorkspaceAction& action) {
    const auto count = std::exchange(g_compose_text_hotkey_count, std::size_t{});
    if (count == 0 || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        worm_ui_nuklear_compose_state().inline_offset.bgm_column_count_edit.input_owned ||
        worm_ui_nuklear_compose_state().layer_editor.layers_menu_input_consumed || compose_note_menu_consumed() || compose_quantize_owns_input() ||
        wormedit::ui::chrome::context_menu_owns_input(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu) || nuklear_text_entry_active())
        return;
    switch (g_compose_text_hotkeys[0]) {
    case '+': action.compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_IN; break;
    case '-': action.compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_OUT; break;
    case '[':
    case '{': action.compose_action = WORM_UI_COMPOSE_ACTION_RATE_SLOWER; break;
    case ']':
    case '}': action.compose_action = WORM_UI_COMPOSE_ACTION_RATE_FASTER; break;
    default: break;
    }
}

static void queue_compose_wheel_event(const SDL_MouseWheelEvent* event) {
    if (!event || !g_window_focused || (!g_window && !g_headless) ||
        !frontend_event_window_matches(event->windowID, true) ||
        !std::isfinite(event->x) || !std::isfinite(event->y) || !std::isfinite(event->mouse_x) ||
        !std::isfinite(event->mouse_y)) {
        return;
    }
    const auto wheel = worm_ui_nuklear_normalize_compose_wheel(event->x, event->y,
        event->mouse_x, event->mouse_y, static_cast<std::uint16_t>(SDL_GetModState()),
        event->direction == SDL_MOUSEWHEEL_FLIPPED);
    if (!wheel) return;
    worm_ui_nuklear_queue_compose_wheel(wheel->delta_x, wheel->delta_y,
        wheel->mouse_x, wheel->mouse_y, wheel->modifiers, wheel->coarse_steps);
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

static void initialize_notification_context() {
    const auto allocator = nk_sdl_allocator();
    g_notification_context_initialized = nk_init(&g_notification_context, &allocator, g_context->style.font);
    if (g_notification_context_initialized) g_notification_context.userdata = g_context->userdata;
}

static void shutdown_notification_context() {
    if (!g_notification_context_initialized) return;
    nk_free(&g_notification_context);
    g_notification_context_initialized = false;
}

static float headless_font_width(nk_handle, float height, const char*, int length) {
    return static_cast<float>(length) * height * 0.5f;
}

static void reset_frontend_session() {
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_reset_timing_state();
    worm_ui_nuklear_reset_audio_state();
    worm_ui_nuklear_reset_compose_panel();
}

void worm_ui_frontend_begin_project_session() {
    worm_ui_nuklear_reset_sound_import();
}

static auto initialize_frontend(SDL_Window* window, SDL_Renderer* renderer,
                                int width, int height, bool headless) -> bool {
    if (g_context || (headless ? width <= 0 || height <= 0 : !window || !renderer))
        return false;
    reset_frontend_session();
    reset_compose_area_menu();
    g_menu_popup_open = false;
    g_menu_popup_release_pending = false;
    g_help_dialog = HelpDialog::none;
    g_about_dialog = {};

    static struct nk_user_font test_font{
        .userdata = nk_handle_ptr(nullptr),
        .height = 15.0f,
        .width = headless_font_width,
    };
    struct nk_context* context = headless
        ? nk_sdl_init_headless(nk_sdl_allocator(), &test_font)
        : nk_sdl_init(window, renderer, nk_sdl_allocator());
    if (!context) return false;
    if (headless) {
        static nk_user_font display_font = test_font;
        display_font.height = 28;
        wormedit::ui::theme::display_font = &display_font;
    }
    if (!headless) {
        const auto font_directory = SDL_GetBasePath()
            ? std::filesystem::path{SDL_GetBasePath()} / "assets/fonts"
            : std::filesystem::path{"assets/fonts"};
        g_editor_font.initialize(context, font_directory);
        wormedit::ui::theme::display_font = g_editor_font.display_handle();
        const auto theme_directory = SDL_GetBasePath()
                                         ? std::filesystem::path{SDL_GetBasePath()} / "assets/themes"
                                         : std::filesystem::path{"assets/themes"};
        wormedit::ui::theme::load(theme_directory);
        wormedit::ui::chrome::apply_theme(context);
        nk_style_set_font(context, g_editor_font.handle());
        if (!wormedit::ui::lucide::initialize(renderer)) {
            nk_sdl_shutdown(context);
            return false;
        }
        if (!SDL_StartTextInput(window)) {
            wormedit::ui::lucide::shutdown();
            nk_sdl_shutdown(context);
            return false;
        }
    } else {
        wormedit::ui::chrome::apply_theme(context);
        nk_style_set_font(context, &test_font);
    }
    g_window = window;
    g_renderer = renderer;
    worm_ui_nuklear_bga_set_renderer(renderer);
    g_context = context;
    g_headless = headless;
    g_headless_width = headless ? width : 0;
    g_headless_height = headless ? height : 0;
#if defined(WORMEDIT_HAS_MACOS_MENU)
    if (!headless) (void)wormedit::sys::initialize_macos_menu();
#endif
    initialize_notification_context();
    g_window_focused = true;
    g_notification_state = {};
    g_notification_history_state = {};
    g_notification_history_close_pending = false;
    g_notification_view = {};
    g_notification_modal = false;
    g_notification_project_modal = false;
    g_compose_escape_has_selection = false;
    g_save_project_requested = false;
    g_save_project_request_id = 0;
    worm_ui_nuklear_reset_sound_import();
    g_notification_escape_captured = false;
    worm_ui_nuklear_reset_external_player_panel();
    g_compose_area_shortcuts_active = false;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_double_click_count = 0;
    g_last_window_width = headless ? width : 0;
    g_last_window_height = headless ? height : 0;
    reset_project_dialog();
    g_new_project_wizard = {};
    return true;
}

bool worm_ui_frontend_init(SDL_Window* window, SDL_Renderer* renderer) {
    return initialize_frontend(window, renderer, 0, 0, false);
}

bool worm_ui_frontend_init_headless(int width, int height) {
    return initialize_frontend(nullptr, nullptr, width, height, true);
}

void worm_ui_frontend_shutdown(void) {
    reset_command_menu();
    wormedit::ui::chrome::context_menu_cancel_keyboard();
    g_context_invocation_allowed = false;
    shutdown_stem_resize_cursor();
#if defined(WORMEDIT_HAS_MACOS_MENU)
    wormedit::sys::shutdown_macos_menu();
#endif
    worm_ui_frontend_set_notification_sink(nullptr, nullptr);
    worm_ui_nuklear_reset_sound_import();
    g_save_project_requested = false;
    g_save_project_request_id = 0;
    reset_compose_note_menu();
    shutdown_notification_context();
    worm_ui_nuklear_reset_external_player_panel();
    reset_project_dialog();
    g_new_project_wizard = {};
    if (!g_context) {
        g_headless = false;
        g_headless_width = 0;
        g_headless_height = 0;
        reset_frontend_session();
        return;
    }
    if (g_window && !g_headless) {
        SDL_StopTextInput(g_window);
    }
    worm_ui_nuklear_bga_set_renderer(nullptr);
    worm_ui_nuklear_shutdown_player_preview();
    wormedit::ui::lucide::shutdown();
    nk_sdl_shutdown(g_context);
    wormedit::ui::theme::display_font = nullptr;
    g_editor_font.reset();
    g_window = NULL;
    g_renderer = NULL;
    g_context = NULL;
    g_headless = false;
    g_headless_width = 0;
    g_headless_height = 0;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_double_click_count = 0;
    g_last_window_width = 0;
    g_last_window_height = 0;
    reset_frontend_session();
}

void worm_ui_frontend_begin_input(void) {
    worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested = false;
    if (g_context) {
        nk_sdl_begin_frame(g_context);
        nk_input_begin(g_context);
        g_notification_history_state.pointer_owned_this_frame = false;
        g_shortcut_event_count = 0;
        g_shortcut_events_overflowed = false;
        g_double_click_count = 0;
    }
}

static bool capture_tap_timing_key(const SDL_KeyboardEvent& event) {
    if (event.scancode != SDL_SCANCODE_T) return false;
    bool& captured = g_tap_key_captured;
    if (event.type == SDL_EVENT_KEY_UP) {
        const bool consumed = captured;
        captured = false;
        return consumed;
    }
    if (event.repeat) return captured;
    if (!g_tap_timing_visible || !g_window_focused || (!g_window && !g_headless) ||
        !frontend_event_window_matches(event.windowID) || project_dialog_active() ||
        g_new_project_wizard.open || g_ime_composition_active ||
        nuklear_text_entry_active()) return false;
    const auto modifiers = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                           SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
    if ((event.mod & modifiers) != 0) return false;
    captured = true;
    if (!event.repeat) queue_shortcut_key_event(&event, true);
    return true;
}

WormUiMetadataDropTarget worm_ui_frontend_metadata_drop_target(uint32_t window_id, float x,
                                                                float y) {
    if (!g_context ||
        (window_id != 0 && (!g_window || window_id != SDL_GetWindowID(g_window))) || !std::isfinite(x) ||
        !std::isfinite(y))
        return {};
    return worm_ui_nuklear_metadata_drop_target(x, y);
}

void worm_ui_frontend_audio_waveform_viewport_request(WormUiAudioWaveformViewportRequest* request) {
    if (!request) return;
    worm_ui_nuklear_audio_waveform_viewport_request(*request);
}

void worm_ui_frontend_bga_dock_request(WormUiBgaDockRequest* request) {
    if (!request) return;
    const auto& state = worm_ui_nuklear_compose_state();
    *request = {};
    request->visible = state.dock_layout.bga_panel_visible && state.bga_catalog.bga_list_valid;
    std::copy(state.bga_catalog.bga_search.begin(), state.bga_catalog.bga_search.end(), request->search);
    request->first_row = state.bga_catalog.bga_first_row;
    request->row_limit = state.bga_catalog.bga_row_limit;
    request->sort_by_id = state.bga_catalog.bga_sort_by_id;
    request->use_filter = state.bga_catalog.bga_use_filter;
}

void worm_ui_frontend_bookmark_chooser_request(WormUiBookmarkChooserRequest* request) {
    if (!request) return;
    const auto& chooser = worm_ui_nuklear_compose_state().bookmark_ui.bookmark_chooser;
    *request = {chooser.first_row, chooser.row_limit, chooser.open, {}};
    std::copy(chooser.query.begin(), chooser.query.end(), request->query);
}

double worm_ui_frontend_compose_bgm_lane_width(uint64_t project, uint64_t chart, uint64_t lane) {
    return worm_ui_nuklear_stem_lane_width(worm_ui_nuklear_compose_state(), project, chart, lane);
}
void worm_ui_frontend_keysound_dock_request(WormUiKeysoundDockRequest* request) {
    if (!request) return;
    const auto& state = worm_ui_nuklear_compose_state();
    *request = {};
    request->sound_picker_open = state.sound_picker_ui.sound_picker.open;
    std::copy(state.sound_picker_ui.sound_picker.query.begin(), state.sound_picker_ui.sound_picker.query.end(), request->sound_picker_query);
    request->sort_by_id = state.keysound_filters.keysound_sort_by_id;
    request->slots_mode = state.keysound_filters.keysound_slots_mode;
    request->selected_slot = state.keysound_catalog.keysound_selected_slot;
    std::copy(state.keysound_catalog.keysound_slots_search.begin(), state.keysound_catalog.keysound_slots_search.end(), request->search);
    request->first_row = state.keysound_catalog.keysound_slots_first_row;
    request->row_limit = state.keysound_catalog.keysound_slots_row_limit;
    std::copy(state.keysound_filters.keysound_filters.search.begin(), state.keysound_filters.keysound_filters.search.end(),
              request->automatic_search);
    request->use_filter = state.keysound_filters.keysound_filters.use_filter;
    request->group_filter_active = state.keysound_filters.keysound_filters.group_filter_active;
    request->group_filter_ids = state.keysound_filters.keysound_filters.group_ids.data();
    request->group_filter_count = state.keysound_filters.keysound_filters.group_ids.size();
    request->include_ungrouped = state.keysound_filters.keysound_filters.include_ungrouped;
    request->layer_filter_kind = state.keysound_filters.keysound_filters.layer_filter_kind;
    request->layer_filter_id = state.keysound_filters.keysound_filters.layer_filter_id;
    request->collapsed_group_ids = state.keysound_filters.keysound_filters.collapsed_group_ids.data();
    request->collapsed_group_count = state.keysound_filters.keysound_filters.collapsed_group_ids.size();
    request->ungrouped_collapsed = state.keysound_filters.keysound_filters.ungrouped_collapsed;
    request->selected_resource_ids = state.keysound_catalog.keysound_selection.selected.data();
    request->selected_resource_count = state.keysound_catalog.keysound_selection.selected.size();
    request->include_selected_metadata = state.keysound_catalog.keysound_group_review.open_pending;
    request->automatic_first_row = state.keysound_filters.keysound_auto_first_row;
    request->automatic_row_limit = state.keysound_filters.keysound_auto_row_limit;
    request->primary_resource_id = state.keysound_catalog.keysound_selection.primary;
    request->filter_layer_first_row = state.keysound_filters.keysound_filter_layer_first;
    request->filter_layer_row_limit = 64;
}

bool worm_ui_frontend_stage_sound_slot_batch(uint64_t project, uint64_t chart, uint64_t revision,
    uint16_t first_slot, const WormUiSoundSlotImportItem* items, size_t count, const char* reason) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!state.keysound_filters.keysound_slots_mode) return false;
    if (wormedit::ui::nuklear::slot_review_active(state) || state.keysound_slots.keysound_pending_file) {
        state.keysound_slots.keysound_slot_feedback = "Finish the current WAV slot review before choosing another file.";
        return false;
    }
    worm_ui_sound_slot_batch_stage(state.keysound_slots.keysound_slot_batch, project, chart, revision,
                                   first_slot, items, count, reason);
    return true;
}

void worm_ui_frontend_sound_slot_batch_result(bool applied, const char* message) {
    worm_ui_sound_slot_batch_result(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch,
                                    applied, message);
}

bool worm_ui_frontend_stage_sound_slot_file_at(uint64_t project, uint64_t chart,
    uint64_t revision, uint16_t slot, uint64_t occupied_resource, const char* path) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!state.keysound_filters.keysound_slots_mode) return false;
    if (wormedit::ui::nuklear::slot_review_active(state) || state.keysound_slots.keysound_pending_file) {
        state.keysound_slots.keysound_slot_feedback = "Finish the current WAV slot review before choosing another file.";
        return false;
    }
    if (!path || slot == 0 || slot > 1295 || state.keysound_catalog.keysound_slots_project != project ||
        state.keysound_catalog.keysound_slots_chart != chart || state.keysound_catalog.keysound_slots_revision != revision) {
        state.keysound_slots.keysound_slot_feedback = "The selected WAV slot is no longer available.";
        return true;
    }
    state.keysound_slots.keysound_pending_path = path;
    state.keysound_slots.keysound_pending_project = project;
    state.keysound_slots.keysound_pending_chart = chart;
    state.keysound_slots.keysound_pending_revision = revision;
    state.keysound_slots.keysound_pending_slot = slot;
    state.keysound_slots.keysound_pending_occupied_resource = occupied_resource;
    state.keysound_slots.keysound_pending_file = true;
    state.keysound_slots.keysound_slot_feedback.clear();
    return true;
}

bool worm_ui_frontend_stage_sound_slot_file(uint64_t project, uint64_t chart, uint64_t revision,
                                            const char* path, size_t file_count) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!state.keysound_filters.keysound_slots_mode) return false;
    if (!path || file_count != 1) {
        state.keysound_slots.keysound_slot_feedback = "Choose exactly one file for a BMS WAV slot.";
        return true;
    }
    if (state.keysound_catalog.keysound_slots_project != project ||
        state.keysound_catalog.keysound_slots_chart != chart || state.keysound_catalog.keysound_slots_revision != revision ||
        state.keysound_catalog.keysound_selected_slot == 0 ||
        state.keysound_catalog.keysound_selected_slot > 1295 || !state.keysound_catalog.keysound_selected_slot_resource_known) {
        state.keysound_slots.keysound_slot_feedback = "The selected WAV slot is no longer available.";
        return true;
    }
    return worm_ui_frontend_stage_sound_slot_file_at(project, chart, revision,
        state.keysound_catalog.keysound_selected_slot, state.keysound_catalog.keysound_selected_slot_resource, path);
}

bool worm_ui_frontend_bga_list_contains(uint32_t window_id, float x, float y) {
    if (!g_context || (window_id && (!g_window || SDL_GetWindowID(g_window) != window_id))) return false;
    const auto& state = worm_ui_nuklear_compose_state();
    return state.bga_catalog.bga_list_valid && wormedit::ui::is_point_in_rect({state.bga_catalog.bga_list_x, state.bga_catalog.bga_list_y,
            state.bga_catalog.bga_list_width, state.bga_catalog.bga_list_height}, x, y);
}

bool worm_ui_frontend_sound_list_contains(uint32_t window_id, float x, float y) {
    if (!g_context ||
        (window_id != 0 && (!g_window || window_id != SDL_GetWindowID(g_window))) || !std::isfinite(x) ||
        !std::isfinite(y))
        return false;
    const auto& audio = worm_ui_nuklear_audio_state();
    if (g_context_tab == WORM_UI_WORKSPACE_TAB_AUDIO_SLICES) {
        return !g_notification_modal && !project_dialog_active() &&
            !worm_ui_nuklear_sound_import_active() && !g_menu_popup_open &&
            !g_menu_popup_release_pending && audio.import_drop_valid &&
            wormedit::ui::is_point_in_rect({audio.import_drop_x, audio.import_drop_y,
                audio.import_drop_width, audio.import_drop_height}, x, y);
    }
    return worm_ui_nuklear_sound_list_contains(x, y) ||
        (audio.sound_list_valid && wormedit::ui::is_point_in_rect({audio.sound_list_x, audio.sound_list_y,
             audio.sound_list_width, audio.sound_list_height}, x, y));
}

static bool notification_document_gesture_active() {
    const auto& compose = worm_ui_nuklear_compose_state();
    return g_notification_view.edit_gesture_active || g_audio.offset_drag_active ||
        compose.pointer_capture.pointer_hold_active || compose.pointer_capture.note_paint_pointer_active ||
        compose.bmson_input.bmson_razor_pointer_active || compose.pointer_capture.semantic_layer_paint_pointer_active ||
        compose.range_selection.lane_hold_active || compose.range_selection.space_range_hold_active ||
        compose.note_move.note_move_candidate_active || compose.note_move.note_move_active ||
        compose.timing_drag.timing_stage_drag_active || compose.timing_marker.timing_marker_candidate_active ||
        compose.timing_marker.timing_marker_drag_active || compose_area_capture_active();
}

static bool notification_editor_gesture_active() {
    return notification_document_gesture_active() ||
        std::ranges::any_of(g_context->input.mouse.buttons,
                            [](const auto& button) { return button.down; });
}

static bool notification_history_contains(struct nk_rect bounds, float x, float y) {
    return std::isfinite(x) && std::isfinite(y) &&
           wormedit::ui::is_point_in_rect({bounds.x, bounds.y, bounds.w, bounds.h}, x, y);
}

static bool notification_history_window_visible(bool overlay) {
    auto* context = overlay ? &g_notification_context : g_context;
    return context && nk_window_find(context, "Notifications") &&
           !nk_window_is_closed(context, "Notifications") &&
           !nk_window_is_hidden(context, "Notifications");
}

static struct nk_rect notification_history_bounds(bool overlay) {
    auto* context = overlay ? &g_notification_context : g_context;
    const auto* window = context ? nk_window_find(context, "Notifications") : nullptr;
    if (!window || nk_window_is_closed(context, "Notifications") ||
        nk_window_is_hidden(context, "Notifications"))
        return nk_rect(0, 0, 0, 0);
    return window->bounds;
}

static bool notification_editor_text_owned() {
    const auto& compose = worm_ui_nuklear_compose_state();
    return (nuklear_text_entry_active() && !nk_window_is_active(g_context, "Notifications")) ||
        g_ime_composition_active || compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open ||
        compose.dialogs.go_to_measure_open || compose.bookmark_ui.bookmark_rename.active ||
        (worm_ui_nuklear_bookmark_chooser_owns_input(compose.bookmark_ui.bookmark_chooser) || compose_sound_picker_owns_input(compose.sound_picker_ui.sound_picker));
}

static bool notification_external_pointer_active() {
    const auto bounds = notification_history_bounds(false);
    return std::ranges::any_of(g_context->input.mouse.buttons, [&](const auto& button) {
        return button.down && !notification_history_contains(bounds, button.clicked_pos.x,
                                                               button.clicked_pos.y);
    });
}

static bool notification_history_locked() {
    return notification_editor_text_owned() || notification_document_gesture_active() ||
           notification_external_pointer_active();
}

static bool notification_history_focused() {
    return g_context && g_notification_view.history.visible && !g_notification_project_modal &&
        !g_notification_history_close_pending && notification_history_window_visible(false) &&
        nk_window_is_active(g_context, "Notifications") && !notification_history_locked() &&
        (nuklear_text_entry_active() || notification_history_contains(notification_history_bounds(false),
            g_context->input.mouse.pos.x, g_context->input.mouse.pos.y));
}

static void close_notification_history_windows() {
    nk_window_close(g_context, "Notifications");
    if (g_notification_context_initialized)
        nk_window_close(&g_notification_context, "Notifications");
}

static bool compose_popup_owns_input(bool include_menu) {
    const auto& compose = worm_ui_nuklear_compose_state();
    return (include_menu && (g_menu_popup_open || g_menu_popup_release_pending)) || compose_quantize_owns_input() ||
           wormedit::ui::chrome::context_menu_owns_input(compose.snap_grid.snap_grid_menu) ||
           compose_area_menu_open() || compose_area_menu_consumed() ||
           compose_recipe_menu_open() || compose_recipe_menu_consumed() ||
           compose.dialogs.export_dialog_open || compose.dialogs.compose_dialog_release_pending ||
           compose.layer_editor.layer_creation_context.has_value() ||
           wormedit::ui::chrome::context_menu_owns_input(compose.layer_editor.layers_menu) || compose.keysound_slots.keysound_slot_confirmation_open ||
           compose.keysound_slots.keysound_slot_batch.open || compose.keysound_catalog.keysound_group_review.open ||
           wormedit::ui::chrome::context_menu_owns_input(compose.keysound_catalog.keysound_group_menu) ||
           compose.keysound_filters.is_popup_open() || compose.keysound_filters.keysound_filter_release_pending ||
           compose.bga_interaction.bga_stack_open || compose.bga_interaction.bga_stack_release_pending ||
           compose.bga_catalog.bga_renumber_confirmation_open || compose.bga_catalog.bga_renumber_release_pending ||
           compose_note_menu_open() || compose_note_menu_consumed() ||
           (worm_ui_nuklear_bookmark_chooser_owns_input(compose.bookmark_ui.bookmark_chooser) || compose_sound_picker_owns_input(compose.sound_picker_ui.sound_picker));
}

int worm_ui_frontend_compose_left_dock_pixels(void) {
    return static_cast<int>(std::lround(worm_ui_nuklear_compose_state().dock_layout.branch_dock_width_drawn));
}

bool worm_ui_frontend_open_bookmark_chooser(uint64_t project_id, uint64_t chart_id, uint64_t revision) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!g_context || !g_window_focused || nuklear_text_entry_active() || g_ime_composition_active ||
        compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open || compose.dialogs.go_to_measure_open ||
        compose.bookmark_ui.bookmark_rename.active || compose.bookmark_ui.bookmark_rename.release_pending ||
        compose_popup_owns_input()) return false;
    worm_ui_nuklear_bookmark_chooser_open(compose.bookmark_ui.bookmark_chooser, project_id, chart_id, revision);
    return true;
}

bool worm_ui_frontend_open_sound_picker(uint64_t project_id, uint64_t chart_id, uint64_t revision) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!g_context || !g_window_focused || !g_compose_area_shortcuts_active ||
        g_notification_modal || project_dialog_active() || nuklear_text_entry_active() ||
        g_ime_composition_active || compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open ||
        compose.dialogs.go_to_measure_open || compose.bookmark_ui.bookmark_rename.active ||
        compose.bookmark_ui.bookmark_rename.release_pending || compose_popup_owns_input()) return false;
    open_compose_sound_picker(compose.sound_picker_ui.sound_picker, project_id, chart_id, revision);
    return true;
}

bool worm_ui_frontend_open_project_export(uint64_t project_id, uint64_t chart_id, uint64_t revision) {
    if (!g_context || !g_window_focused || !g_project_export_available || g_notification_modal || project_dialog_active() ||
        g_ime_composition_active || compose_popup_owns_input(false) ||
        !project_id || !chart_id || project_id != g_context_project || chart_id != g_context_chart ||
        revision != g_context_revision) return false;
    auto& compose = worm_ui_nuklear_compose_state();
    compose.dialogs.export_dialog_open = true;
    compose.dialogs.export_dialog_project_id = project_id;
    compose.dialogs.export_dialog_chart_id = chart_id;
    compose.dialogs.export_dialog_revision = revision;
    compose.dialogs.export_dialog = {};
    return true;
}

static bool compose_dock_input_available() {
    const auto& compose = worm_ui_nuklear_compose_state();
    return g_context && g_window_focused && g_compose_area_shortcuts_active &&
        !g_notification_modal && !project_dialog_active() && !nuklear_text_entry_active() &&
        !g_ime_composition_active && !compose.inline_timing.inline_timing_open && !compose.inline_offset.inline_offset_open &&
        !compose.dialogs.go_to_measure_open && !compose.bookmark_ui.bookmark_rename.active &&
        !compose.bookmark_ui.bookmark_rename.release_pending && !compose_popup_owns_input();
}

bool worm_ui_frontend_compose_dock_command(WormUiComposeDockCommand command,
    uint64_t project_id, uint64_t chart_id, uint64_t revision) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!compose_dock_input_available() || compose.layer_editor.layer_creation_request ||
        compose.document_context.last_project_id != project_id || compose.document_context.last_chart_id != chart_id ||
        compose.document_context.last_document_revision != revision) return false;
    switch (command) {
    case WORM_UI_COMPOSE_DOCK_OPEN_LAYERS:
        compose.dock_layout.layers_panel_visible = !compose.dock_layout.layers_panel_visible;
        if (compose.dock_layout.layers_panel_visible && compose.dock_layout.compact_dock_layout)
            compose.dock_layout.sounds_panel_visible = false;
        return true;
    case WORM_UI_COMPOSE_DOCK_CREATE_LAYER:
        compose.dock_layout.layers_panel_visible = true;
        // Compact Compose has room for one dock; reveal the requested Layers dock.
        if (compose.dock_layout.compact_dock_layout) compose.dock_layout.sounds_panel_visible = false;
        if (command == WORM_UI_COMPOSE_DOCK_CREATE_LAYER)
            compose.layer_editor.layer_creation_request = {project_id, chart_id, revision};
        return true;
    case WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS:
        compose.dock_layout.sounds_panel_visible = !compose.dock_layout.sounds_panel_visible;
        if (compose.dock_layout.sounds_panel_visible && compose.dock_layout.compact_dock_layout)
            compose.dock_layout.layers_panel_visible = false;
        compose.dock_layout.sounds_dock_bmson_tab = false;
        return true;
    }
    return false;
}

static void consume_layer_creation_request(const WormUiWorkspaceView& view,
                                            WormUiWorkspaceAction& action) {
    auto& compose = worm_ui_nuklear_compose_state();
    const auto request = std::exchange(compose.layer_editor.layer_creation_request, std::nullopt);
    if (!request || !compose_dock_input_available() || !view.project_loaded || view.options.visible ||
        view.compose.timing_focus || view.active_tab != WORM_UI_WORKSPACE_TAB_COMPOSE ||
        !view.compose.visible || !view.compose.scene_available || external_project_modal_active(view) ||
        view.compose.input_occluded || request->project != view.compose.project_id ||
        request->chart != view.compose.chart_id || request->revision != view.compose.document_revision ||
        action.kind != WORM_UI_WORKSPACE_ACTION_NONE || action.compose_input_count != 0 ||
        (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE &&
         action.compose_action != WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW)) return;
    worm_ui_nuklear_open_layer_creation(view.compose, compose);
}

static bool notification_owns_pointer(float x, float y) {
    if (g_notification_project_modal || compose_popup_owns_input()) return false;
    return wormedit::ui::nuklear::notification_pointer_owned(
        g_notification_state, x, y, notification_editor_gesture_active());
}

static bool capture_notification_key(const SDL_KeyboardEvent& event) {
    if (event.key != SDLK_ESCAPE || !frontend_event_window_matches(event.windowID))
        return false;
    if (g_notification_escape_captured) {
        if (event.type == SDL_EVENT_KEY_UP) g_notification_escape_captured = false;
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.repeat) return false;
    if (g_window_focused && notification_history_focused()) {
        g_notification_history_close_pending = true;
        close_notification_history_windows();
        g_notification_escape_captured = true;
        return true;
    }
    if (g_notification_state.count == 0) return false;
    const auto& compose = worm_ui_nuklear_compose_state();
    const auto target = wormedit::ui::nuklear::notification_escape_target(
        g_notification_view, g_window_focused,
        nuklear_text_entry_active() || compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open ||
            compose.dialogs.go_to_measure_open || compose.bookmark_ui.bookmark_rename.active || compose.bookmark_ui.bookmark_rename.release_pending || compose_popup_owns_input() ||
            compose.layer_editor.semantic_layer_name_editor_visible || compose.layer_editor.semantic_layer_color_editor_visible,
        g_ime_composition_active,
        notification_editor_gesture_active() || compose.input_queue.key_event_count != 0 ||
            g_compose_escape_has_selection,
        g_notification_modal || g_new_project_wizard.open || project_dialog_active());
    if (!target) return false;
    g_notification_state.dismiss_pending = *target;
    g_notification_escape_captured = true;
    return true;
}

static bool capture_notification_button(const SDL_MouseButtonEvent& event) {
    if (!g_window_focused || (!g_window && !g_headless) ||
        !frontend_event_window_matches(event.windowID, true) ||
        !std::isfinite(event.x) || !std::isfinite(event.y)) return false;
    const bool captured = wormedit::ui::nuklear::notification_pointer_edge(
        g_notification_state, event.x, event.y, event.button, event.down,
        !g_notification_project_modal && !notification_editor_gesture_active() &&
            !compose_popup_owns_input());
    if (captured) nk_input_motion(g_context, static_cast<int>(event.x), static_cast<int>(event.y));
    return captured;
}

static bool capture_notification_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        wormedit::ui::nuklear::notification_focus_lost(g_notification_state);
        g_notification_history_state.pointer_routes.fill(
            wormedit::ui::nuklear::notification_history_pointer_route_t::none);
        g_notification_escape_captured = false;
        return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        return capture_notification_key(event.key);
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return capture_notification_button(event.button);
    if (!g_window_focused) return false;
    if (event.type == SDL_EVENT_MOUSE_MOTION && std::isfinite(event.motion.x) &&
        std::isfinite(event.motion.y) && notification_owns_pointer(event.motion.x, event.motion.y)) {
        nk_input_motion(g_context, static_cast<int>(event.motion.x), static_cast<int>(event.motion.y));
        return true;
    }
    return event.type == SDL_EVENT_MOUSE_WHEEL &&
        notification_owns_pointer(event.wheel.mouse_x, event.wheel.mouse_y);
}

static bool feed_history_event(const SDL_Event& event, bool interactive) {
    if (interactive) {
        SDL_Event copy = event;
        (void)nk_sdl_handle_event(g_context, &copy);
    }
    return true;
}

static bool history_pointer_capture_active() {
    using route_t = wormedit::ui::nuklear::notification_history_pointer_route_t;
    return std::ranges::any_of(g_notification_history_state.pointer_routes,
                               [](route_t route) { return route != route_t::none; });
}

static bool capture_history_button(const SDL_Event& event, bool available, bool interactive) {
    using route_t = wormedit::ui::nuklear::notification_history_pointer_route_t;
    const auto route = wormedit::ui::nuklear::notification_history_pointer_edge(
        g_notification_history_state, notification_history_bounds(!interactive),
        event.button.x, event.button.y, event.button.button, event.button.down,
        available && !notification_document_gesture_active() && !notification_external_pointer_active(),
        interactive);
    if (route == route_t::none) return false;
    return feed_history_event(event, route == route_t::interactive);
}

static bool capture_notification_history_event(const SDL_Event& event) {
    const bool locked = notification_history_locked();
    const bool overlay = locked;
    const bool available = g_notification_view.history.visible && !g_notification_project_modal &&
        !g_notification_history_close_pending && g_window_focused &&
        (!locked || g_notification_context_initialized) &&
        notification_history_window_visible(overlay);
    const bool interactive = available && !locked;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return capture_history_button(event, available, interactive);
    if (!available) return false;
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        if (!notification_history_focused()) return false;
        queue_shortcut_key_event(&event.key);
        return feed_history_event(event, true);
    }
    if (event.type == SDL_EVENT_TEXT_INPUT && notification_history_focused())
        return feed_history_event(event, true);
    const auto bounds = notification_history_bounds(!interactive);
    if (event.type == SDL_EVENT_MOUSE_MOTION && (history_pointer_capture_active() ||
        notification_history_contains(bounds, event.motion.x, event.motion.y))) {
        if (notification_document_gesture_active() || notification_external_pointer_active()) return false;
        g_notification_history_state.pointer_owned_this_frame = true;
        return feed_history_event(event, interactive);
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL &&
        notification_history_contains(bounds, event.wheel.mouse_x, event.wheel.mouse_y) &&
        !notification_document_gesture_active() && !notification_external_pointer_active()) {
        g_notification_history_state.pointer_owned_this_frame = true;
        return feed_history_event(event, interactive);
    }
    return false;
}

static bool capture_sound_slot_key(const SDL_KeyboardEvent& event) {
    if (!worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_confirmation_open) return false;
    wormedit::ui::chrome::forward_modal_key(g_context, event);
    return true;
}

static bool capture_inline_offset_key(const SDL_KeyboardEvent& event) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!compose.inline_offset.inline_offset_open) return false;
    compose.inline_offset.inline_offset_input_consumed = true;
    if (event.key == SDLK_ESCAPE) {
        if (event.down) compose.inline_offset.inline_offset_cancel_pending = true;
        return true;
    }
    wormedit::ui::chrome::forward_modal_key(g_context, event);
    return true;
}

void worm_ui_frontend_bga_move_preview(uint64_t* chart_id, uint64_t* document_revision,
                                       uint64_t* event_id, double* y_pixels) {
    const auto& state = worm_ui_nuklear_compose_state();
    const bool active = g_window_focused && state.bga_interaction.bga_event_move_active;
    if (chart_id) *chart_id = active ? state.timing_drag.bga_capture.chart : 0;
    if (document_revision) *document_revision = active ? state.timing_drag.bga_capture.revision : 0;
    if (event_id) *event_id = active ? state.bga_interaction.bga_event_move_object_id : 0;
    if (y_pixels) *y_pixels = active ? state.bga_interaction.bga_event_move_current_y_pixels : 0.0;
}

static bool capture_bga_stack_key(const SDL_KeyboardEvent& event) {
    if (!worm_ui_nuklear_compose_state().bga_interaction.bga_stack_open) return false;
    wormedit::ui::chrome::forward_modal_key(g_context, event);
    return true;
}

static bool capture_keysound_group_review_key(const SDL_KeyboardEvent& event) {
    auto& review = worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review;
    auto& compose = worm_ui_nuklear_compose_state();
    if (compose.keysound_catalog.keysound_group_menu.open) {
        wormedit::ui::chrome::forward_modal_key(g_context, event);
        return true;
    }
    if (!review.open) return false;
    if (event.down && !event.repeat && event.key == SDLK_ESCAPE) {
        review.open = false;
        compose.keysound_catalog.keysound_group_menu.release_pending = true;
    } else {
        wormedit::ui::chrome::forward_modal_key(g_context, event);
    }
    return true;
}


static bool capture_bookmark_editor_key(const SDL_KeyboardEvent& event) {
    auto& compose = worm_ui_nuklear_compose_state();
    const bool chooser = worm_ui_nuklear_bookmark_chooser_owns_input(compose.bookmark_ui.bookmark_chooser);
    const bool rename = compose.bookmark_ui.bookmark_rename.active || compose.bookmark_ui.bookmark_rename.release_pending;
    if (!chooser && !rename) return false;
    wormedit::ui::chrome::forward_modal_key(g_context, event);
    if (event.down && !event.repeat && event.key == SDLK_ESCAPE) {
        if (chooser) worm_ui_nuklear_bookmark_chooser_cancel(g_context, compose.bookmark_ui.bookmark_chooser);
        else cancel_compose_bookmark_rename(compose.bookmark_ui.bookmark_rename);
    }
    return true;
}

static bool capture_sound_picker_key(const SDL_KeyboardEvent& event) {
    auto& picker = worm_ui_nuklear_compose_state().sound_picker_ui.sound_picker;
    if (!compose_sound_picker_owns_input(picker)) return false;
    // Enter commits IME composition before it can confirm a sound.
    if (g_ime_composition_active && event.down &&
        (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER))
        return true;
    wormedit::ui::chrome::forward_modal_key(g_context, event);
    if (event.down && !event.repeat && event.key == SDLK_ESCAPE)
        cancel_compose_sound_picker(picker);
    return true;
}

static bool capture_snap_grid_key(const SDL_KeyboardEvent& event) {
    if (!wormedit::ui::chrome::context_menu_owns_input(
            worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu)) return false;
    wormedit::ui::chrome::forward_modal_key(g_context, event);
    return true;
}

static bool capture_compose_modal_key(const SDL_KeyboardEvent& event) {
    const auto& compose = worm_ui_nuklear_compose_state();
    if (compose.layer_editor.layer_creation_context || compose.dialogs.export_dialog_open) {
        if (g_ime_composition_active && event.down &&
            (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER)) return true;
        wormedit::ui::chrome::forward_modal_key(g_context, event);
        return true;
    }
    if (capture_snap_grid_key(event) || capture_sound_picker_key(event) || capture_bookmark_editor_key(event)) return true;
    const bool escape_menu = g_menu_popup_open || g_menu_popup_release_pending ||
        compose.keysound_filters.is_popup_open() || compose.keysound_filters.keysound_filter_release_pending;
    if (escape_menu && event.key == SDLK_ESCAPE) {
        wormedit::ui::chrome::forward_modal_key(g_context, event);
        return true;
    }
    return capture_bga_stack_key(event) || capture_keysound_group_review_key(event);
}

static auto context_key(SDL_Keycode key) -> wormedit::ui::chrome::ContextMenuKey {
    using Key = wormedit::ui::chrome::ContextMenuKey;
    switch (key) {
    case SDLK_UP: return Key::up;
    case SDLK_DOWN: return Key::down;
    case SDLK_HOME: return Key::home;
    case SDLK_END: return Key::end;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return Key::activate;
    case SDLK_ESCAPE: return Key::escape;
    case SDLK_LEFT: return Key::left;
    case SDLK_RIGHT: return Key::right;
    default: return Key::none;
    }
}

static bool command_menu_dialog_blocked() {
    const auto& compose = worm_ui_nuklear_compose_state();
    return g_new_project_wizard.open || g_help_dialog != HelpDialog::none ||
        compose_popup_owns_input() || compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open ||
        compose.dialogs.go_to_measure_open || compose.bookmark_ui.bookmark_rename.active || compose.bookmark_ui.bookmark_rename.release_pending ||
        notification_editor_gesture_active();
}

static bool context_invocation_blocked() {
    const auto& compose = worm_ui_nuklear_compose_state();
    return !g_context_invocation_allowed || !g_window_focused || nuklear_text_entry_active() || g_ime_composition_active ||
        g_notification_modal || g_new_project_wizard.open || project_dialog_active() ||
        g_help_dialog != HelpDialog::none || compose_popup_owns_input() ||
        compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open || compose.dialogs.go_to_measure_open ||
        notification_editor_gesture_active();
}

static bool capture_context_menu_key(const SDL_KeyboardEvent& event) {
    namespace chrome = wormedit::ui::chrome;
    if (!frontend_event_window_matches(event.windowID) || !g_window_focused) return false;
    const auto down = event.type == SDL_EVENT_KEY_DOWN;
    if (chrome::context_menu_capture_key(context_key(event.key), event.scancode, down, event.repeat))
        return true;
    const auto modifiers = event.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    const bool invocation = (event.key == SDLK_APPLICATION && modifiers == 0) ||
                            (event.key == SDLK_F10 && (modifiers & SDL_KMOD_SHIFT) != 0 &&
                             (modifiers & ~SDL_KMOD_SHIFT) == 0);
    if (!invocation) return false;
    if (!down || event.repeat || context_invocation_blocked()) return true;
    auto target = chrome::KeyboardContextTarget::charts;
    if (g_compose_area_shortcuts_active) {
        target = chrome::context_menu_focused_target();
        if (worm_ui_nuklear_compose_state().input_queue.keyboard_focus ||
            (target != chrome::KeyboardContextTarget::sounds && target != chrome::KeyboardContextTarget::layers))
            target = chrome::KeyboardContextTarget::compose;
    }
    chrome::context_menu_request_open(target);
    (void)chrome::context_menu_capture_key(chrome::ContextMenuKey::none, event.scancode, true, false);
    return true;
}

static bool capture_audio_list_key(const SDL_KeyboardEvent& event) {
    if (!frontend_event_window_matches(event.windowID) || !g_window_focused ||
        g_context_tab != WORM_UI_WORKSPACE_TAB_AUDIO_SLICES ||
        g_notification_modal || project_dialog_active() || command_menu_dialog_blocked() ||
        nuklear_text_entry_active() || g_ime_composition_active ||
        (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) return false;
    const auto direction = event.key == SDLK_UP ? -1 : event.key == SDLK_DOWN ? 1 : 0;
    const auto audition = event.key == SDLK_SPACE;
    if (!direction && !audition) return false;
    if (!event.down) return g_audio.sound_list_focused;
    return worm_ui_nuklear_audio_list_key_command(direction, audition && !event.repeat);
}

static bool handle_frontend_keyboard_event(const SDL_KeyboardEvent& event) {
        if (frontend_event_window_matches(event.windowID) &&
            capture_command_menu_key(g_context, event, g_ime_composition_active)) return true;
        if (capture_context_menu_key(event)) return true;
        if ((g_help_dialog != HelpDialog::none)) {
            wormedit::ui::chrome::forward_modal_key(g_context, event);
            return true;
        }
        if (capture_compose_modal_key(event)) return true;
        auto& slot_batch = worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch;
        if (slot_batch.open) {
            if (event.down && !event.repeat && event.key == SDLK_ESCAPE)
                worm_ui_sound_slot_batch_cancel(slot_batch);
            else {
                wormedit::ui::chrome::forward_modal_key(g_context, event);
            }
            return true;
        }
        if (worm_ui_nuklear_sound_import_active()) {
            if (event.down && !event.repeat && event.key == SDLK_ESCAPE)
                worm_ui_nuklear_reset_sound_import();
            else {
                wormedit::ui::chrome::forward_modal_key(g_context, event);
            }
            return true;
        }
        if (capture_audio_list_key(event)) return true;
        if (capture_sound_slot_key(event)) return true;
        if (capture_inline_offset_key(event)) return true;
        if (capture_tap_timing_key(event)) return true;
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.repeat && event.key == SDLK_ESCAPE) {
            g_audio.offset_escape_pending = true;
        }
        worm_ui_nuklear_queue_compose_key(static_cast<std::uint32_t>(event.key),
                                          static_cast<std::uint32_t>(event.scancode),
                                          static_cast<std::uint16_t>(event.mod), down,
                                          event.repeat);
        const bool tab_horizontal = worm_ui_nuklear_compose_state().keyboard_navigation.raw_tab_down &&
            (event.key == SDLK_LEFT || event.key == SDLK_RIGHT);
        if (!tab_horizontal) queue_shortcut_key_event(&event);
        return false;
}

static bool queue_compose_pinch_event(const SDL_Event& event) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    using phase = worm_ui_nuklear_pinch_phase_t;
    if (event.type != SDL_EVENT_PINCH_BEGIN && event.type != SDL_EVENT_PINCH_UPDATE &&
        event.type != SDL_EVENT_PINCH_END) return false;
    if ((!g_window && !g_headless) || !g_window_focused ||
        !frontend_event_window_matches(event.pinch.windowID, true)) {
        queue_compose_pinch_input(phase::cancel, 0.0, 0.0, 0.0);
        return true;
    }
    float x{}, y{};
    SDL_GetMouseState(&x, &y);
    const auto input_phase = event.type == SDL_EVENT_PINCH_BEGIN ? phase::begin :
        event.type == SDL_EVENT_PINCH_END ? phase::end : phase::update;
    queue_compose_pinch_input(input_phase, event.pinch.scale, x, y);
    return true;
#else
    (void)event;
    return false;
#endif
}

static void track_compose_draw_pointer(const SDL_Event& event) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!g_window_focused || (!g_window && !g_headless)) return;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.button != SDL_BUTTON_RIGHT ||
            !frontend_event_window_matches(event.button.windowID, true) ||
            !std::isfinite(event.button.x) || !std::isfinite(event.button.y)) return;
        state.pointer_capture.draw_right_native_down = event.button.down;
        state.pointer_capture.draw_right_current_screen_x = event.button.x;
        state.pointer_capture.draw_right_current_screen_y = event.button.y;
        if (event.button.down) {
            state.pointer_capture.draw_right_press_pending = true;
            state.pointer_capture.draw_right_release_pending = false;
            state.pointer_capture.draw_right_dragged = false;
            state.pointer_capture.draw_right_screen_x = event.button.x;
            state.pointer_capture.draw_right_screen_y = event.button.y;
        } else {
            state.pointer_capture.draw_right_release_pending = true;
        }
    } else if (event.type == SDL_EVENT_MOUSE_MOTION && state.pointer_capture.draw_right_native_down &&
        frontend_event_window_matches(event.motion.windowID, true)) {
        state.pointer_capture.draw_right_current_screen_x = event.motion.x;
        state.pointer_capture.draw_right_current_screen_y = event.motion.y;
        state.pointer_capture.draw_right_dragged = state.pointer_capture.draw_right_dragged ||
            std::hypot(event.motion.x - state.pointer_capture.draw_right_screen_x,
                       event.motion.y - state.pointer_capture.draw_right_screen_y) >= 3.0;
    }
}

static bool handle_frontend_text_input(const SDL_Event& event) {
    if (capture_command_menu_text(g_context, event)) {
        g_ime_composition_active = false;
        return true;
    }
    g_ime_composition_active = false;
    if (wormedit::ui::chrome::context_menu_keyboard_owned()) return true;
    queue_compose_text_hotkeys(event.text.text);
    auto copy = event;
    return nk_sdl_handle_event(g_context, &copy) != 0;
}

static bool handle_workspace_frontend_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    track_compose_draw_pointer(*event);
    if (queue_compose_pinch_event(*event)) return true;
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        g_window_focused = false;
        cancel_command_menu();
        wormedit::ui::chrome::context_menu_cancel_keyboard();
        g_tap_key_captured = false;
        g_audio.offset_cancel_pending = true;
        worm_ui_nuklear_compose_state().inline_offset.inline_offset_cancel_pending = true;
        g_shortcut_event_count = 0;
        g_double_click_count = 0;
        worm_ui_nuklear_compose_handle_focus_lost();
        worm_ui_nuklear_cancel_setup_drag();
        worm_ui_nuklear_cancel_options_drag();
        worm_ui_nuklear_cancel_metadata_tag_drag();
        apply_stem_resize_cursor(false);
        g_ime_composition_active = false;
    } else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        g_window_focused = true;
    } else if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (handle_frontend_keyboard_event(event->key)) return true;
    } else if (event->type == SDL_EVENT_TEXT_EDITING &&
               frontend_event_window_matches(event->edit.windowID)) {
        g_ime_composition_active = event->edit.text && event->edit.text[0] != '\0';
    } else if (event->type == SDL_EVENT_TEXT_EDITING_CANDIDATES &&
               frontend_event_window_matches(event->edit_candidates.windowID)) {
        if (event->edit_candidates.num_candidates > 0) {
            g_ime_composition_active = true;
        }
    } else if (event->type == SDL_EVENT_TEXT_INPUT &&
               frontend_event_window_matches(event->text.windowID)) {
        return handle_frontend_text_input(*event);
    } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        queue_compose_wheel_event(&event->wheel);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.down &&
               event->button.button == SDL_BUTTON_MIDDLE && g_window_focused &&
               (g_window || g_headless) &&
               frontend_event_window_matches(event->button.windowID, true) &&
               std::isfinite(event->button.x) && std::isfinite(event->button.y)) {
        worm_ui_nuklear_queue_compose_middle_click(event->button.x, event->button.y);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.down &&
               event->button.button == SDL_BUTTON_LEFT && event->button.clicks >= 2 &&
               g_window_focused && (g_window || g_headless) &&
               frontend_event_window_matches(event->button.windowID, true) &&
               std::isfinite(event->button.x) && std::isfinite(event->button.y) &&
               g_double_click_count < g_double_clicks.size()) {
        g_double_clicks[g_double_click_count++] = {event->button.x, event->button.y};
    }
    SDL_Event mutable_event = *event;
    return nk_sdl_handle_event(g_context, &mutable_event) != 0;
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) return false;
    if (capture_notification_event(*event)) return true;
    if (capture_notification_history_event(*event)) return true;
    return handle_workspace_frontend_event(event);
}

void worm_ui_frontend_end_input(void) {
    if (g_context) {
        nk_input_end(g_context);
    }
}

static wormedit::ui::chrome::Dialog g_close_dialog;

// Maps a dialog result to the close decision; Save also starts the save, through the file
// dialog when the project has no native path yet.
static void apply_close_result(const WormUiCloseConfirmationView& view, WormUiWorkspaceAction& action,
                               wormedit::ui::chrome::DialogResult result) {
    using wormedit::ui::chrome::DialogResult;
    WormUiCloseDecision decision = WORM_UI_CLOSE_NONE;
    if (result == DialogResult::cancel || result == DialogResult::secondary) decision = WORM_UI_CLOSE_CANCEL;
    else if (result == DialogResult::destructive) decision = WORM_UI_CLOSE_DISCARD;
    else if (result == DialogResult::primary) {
        decision = WORM_UI_CLOSE_SAVE;
        if (view.has_native_path)
            action.kind = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT;
        else start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    }
    if (decision == WORM_UI_CLOSE_NONE) return;
    action.close_decision = decision;
    action.close_project_id = view.project_id;
    action.close_revision = view.revision;
}

static void render_close_confirmation(const WormUiCloseConfirmationView& view,
    WormUiWorkspaceAction& action, int width, int height) {
    if (!view.visible) return;
    namespace chrome = wormedit::ui::chrome;
    auto& dialog = g_close_dialog;
    if (chrome::dialog_begin(dialog, g_context, "Unsaved changes", "Unsaved changes",
                             {.modal = true, .width = 440.0f,
                              .area = nk_vec2(static_cast<float>(width), static_cast<float>(height))})) {
        chrome::dialog_text(dialog, g_context, view.application
            ? "Save your changes before quitting WormEdit?"
            : "Save your changes before closing this project?", chrome::color(chrome::UiColor::text));
        if (view.status_text && view.status_text[0])
            chrome::dialog_text(dialog, g_context, view.status_text, chrome::color(chrome::UiColor::conflict));
        chrome::dialog_footer_begin(dialog, g_context, 3, true, 120.0f);
        chrome::dialog_button(dialog, g_context, "Discard changes", chrome::DialogButtonRole::destructive);
        chrome::dialog_button(dialog, g_context, "Cancel", chrome::DialogButtonRole::secondary);
        chrome::dialog_button(dialog, g_context, "Save", chrome::DialogButtonRole::primary);
    }
    apply_close_result(view, action, chrome::dialog_end(dialog, g_context));
}

static void render_workspace_error(WormUiWorkspaceAction* action, int width, int height,
                                   const char* diagnostic, bool input_blocked = false) {
    worm_ui_nuklear_compose_discard_frame_input();
    if (nk_begin(g_context, "wormedit-workspace", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | (input_blocked ? NK_WINDOW_NO_INPUT : 0))) {
        nk_layout_row_dynamic(g_context, 30, 1);
        worm_ui_nuklear_retire_setup_popups(g_context);
        wormedit::ui::chrome::context_menu_retire(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu, g_context);
        nk_label(g_context, "The editor could not display this workspace frame.", NK_TEXT_LEFT);
        nk_layout_row_dynamic(g_context, 72, 1);
        nk_label_wrap(g_context, diagnostic && diagnostic[0]
                                     ? diagnostic
                                     : "No diagnostic was provided for the workspace error.");
        nk_layout_row_dynamic(g_context, 30, 2);
        if (nk_button_label(g_context, "Project")) {
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

WormUiWorkspaceAction worm_ui_frontend_workspace_error(
    const char* diagnostic, const WormUiNotificationView* notifications,
    const WormUiCloseConfirmationView* close) {
    worm_ui_nuklear_compose_state().layer_editor.layer_creation_request.reset();
    worm_ui_nuklear_begin_image_quad_frame();
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    consume_project_file_dialog(action);
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_NONE;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    if (!g_context || (!g_window && !g_headless)) {
        return action;
    }
    int width = 0;
    int height = 0;
    if (!frontend_window_size(&width, &height)) {
        return action;
    }
    g_notification_view = notifications ? *notifications : WormUiNotificationView{};
    const bool modal = close && close->visible;
    g_notification_modal = modal;
    g_notification_project_modal = modal;
    render_workspace_error(&action, width, height, diagnostic, modal);
    if (close) render_close_confirmation(*close, action, width, height);
    render_notification_frame(g_notification_view, action, width, height, modal);
    return action;
}

using MenuAction = wormedit::ui::workspace_menu_action_t;
static wormedit::ui::workspace_menu_t g_workspace_menu;

static void refresh_workspace_menu_model(const WormUiWorkspaceView& view) {
    g_workspace_menu = wormedit::ui::build_workspace_menu(view,
        project_modal_active(view) || view.options.visible || project_dialog_active(),
        nuklear_text_entry_active() || g_ime_composition_active, g_frame_timing_visible);
    const auto& compose = worm_ui_nuklear_compose_state();
    for (auto& item : g_workspace_menu) {
        if (item.action == MenuAction::sounds) item.checked = compose.dock_layout.sounds_panel_visible;
        if (item.action == MenuAction::layers) item.checked = compose.dock_layout.layers_panel_visible;
    }
}

static void apply_workspace_menu_action(MenuAction chosen,
                                         WormUiWorkspaceAction& action) {
    const auto found = std::ranges::find(g_workspace_menu, chosen, &wormedit::ui::workspace_menu_item_t::action);
    if (found == g_workspace_menu.end() || !found->enabled) return;
    if (found->recent_path) {
        action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        std::snprintf(action.project_path, sizeof(action.project_path), "%s", found->recent_path);
        return;
    }
    if (found->command_id) {
        std::snprintf(action.menu_command_id, sizeof(action.menu_command_id), "%s", found->command_id);
        return;
    }
    switch (chosen) {
    case MenuAction::open: start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_OPEN_FILE); break;
    case MenuAction::save_as: start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT); break;
    case MenuAction::close: action.kind = WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT; break;
    case MenuAction::quit: action.kind = WORM_UI_WORKSPACE_ACTION_QUIT; break;
    case MenuAction::reset_view: action.compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW; break;
    case MenuAction::frame_timing: g_frame_timing_visible = !g_frame_timing_visible; break;
    case MenuAction::timing_settings: action.timing_settings.toggle = true; break;
    case MenuAction::preview: action.kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW; break;
    case MenuAction::external_preferences: action.external_player.kind = WORM_UI_EXTERNAL_PLAYER_OPEN; break;
    case MenuAction::about:
        g_help_dialog = HelpDialog::about;
        g_about_dialog = {};
        break;
    case MenuAction::shortcuts:
        g_help_dialog = HelpDialog::shortcuts;
        g_about_dialog = {};
        break;
    default: break; // Registry-backed entries returned above.
    }
}

static bool native_menu_active() {
#if defined(WORMEDIT_HAS_MACOS_MENU)
    return !g_headless && wormedit::sys::macos_menu_active();
#else
    return false;
#endif
}

int worm_ui_frontend_title_bar_height(void) { return native_menu_active() ? 0 : 36; }

static void service_native_menu(const WormUiWorkspaceView& view, WormUiWorkspaceAction&
#if defined(WORMEDIT_HAS_MACOS_MENU)
                                action
#endif
) {
    refresh_workspace_menu_model(view);
#if defined(WORMEDIT_HAS_MACOS_MENU)
    if (!native_menu_active()) return;
    if (nuklear_text_entry_active() || g_ime_composition_active)
        for (auto& item : g_workspace_menu) if (item.command_id) item.enabled = false;
    char title[512]{};
    std::snprintf(title, sizeof(title), "%s%s", view.project_title, view.dirty ? " *" : "");
    if (std::strcmp(SDL_GetWindowTitle(g_window), title) != 0) SDL_SetWindowTitle(g_window, title);
    wormedit::sys::refresh_macos_menu(g_workspace_menu);
    const auto chosen = wormedit::sys::take_macos_menu_action();
    const auto found = std::ranges::find(g_workspace_menu, chosen, &wormedit::ui::workspace_menu_item_t::action);
    if (found == g_workspace_menu.end() || !found->enabled || action.kind != WORM_UI_WORKSPACE_ACTION_NONE) return;
    if (found->command_id) std::snprintf(action.menu_command_id, sizeof(action.menu_command_id), "%s", found->command_id);
    else apply_workspace_menu_action(chosen, action);

#endif
}

static void close_unavailable_inline_offset(const WormUiWorkspaceView& view) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!compose.inline_offset.inline_offset_open) return;
    const bool available = g_window_focused && view.project_loaded &&
        view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view.compose.visible &&
        view.compose.audio_offset_editable && !view.options.visible && !project_modal_active(view) &&
        (compose.inline_offset.inline_offset_in_inspector ? compose.dock_layout.inspector_visible && !view.compose.timing_focus
                                            : view.compose.timing_focus) &&
        compose.inline_offset.inline_offset_chart_id == view.compose.chart_id &&
        compose.inline_offset.inline_offset_revision == view.compose.document_revision;
    if (!available || compose.inline_offset.inline_offset_cancel_pending) {
        worm_ui_nuklear_cancel_inline_offset(compose);
        nk_edit_unfocus(g_context);
    }
}

// The screen title is centered on the window, not on the room left by the menus.
static void render_title_label(const WormUiWorkspaceView& view) {
    struct nk_rect bounds{};
    if (nk_widget(&bounds, g_context) == NK_WIDGET_INVALID) return;
    const auto* font = g_context->style.font;
    if (!font) return;
    char title[512]{};
    std::snprintf(title, sizeof(title), "%s%s", view.project_title, view.dirty ? " *" : "");
    const auto length = static_cast<int>(std::strlen(title));
    const auto width = font->width(font->userdata, font->height, title, length);
    const auto window = nk_window_get_bounds(g_context);
    const float centered = window.x + (window.w - width) * 0.5f;
    const float x = std::clamp(centered, bounds.x, std::max(bounds.x, bounds.x + bounds.w - width));
    auto* canvas = nk_window_get_canvas(g_context);
    const auto previous_clip = canvas->clip;
    nk_push_scissor(canvas, bounds);
    nk_draw_text(canvas, nk_rect(x, bounds.y + (bounds.h - font->height) * 0.5f, width, font->height),
                 title, length, font, nk_rgba(0, 0, 0, 0),
                 wormedit::ui::chrome::color(wormedit::ui::theme::UiColor::text));
    nk_push_scissor(canvas, previous_clip);
}

// Menus render before the workspace and auxiliary windows. Closing a Nuklear
// popup unlocks its parent immediately, so retain its pointer ownership through
// every later widget in this frame, then restore input for the next input cycle.
struct MenuPointerCapture;
static MenuPointerCapture* g_menu_pointer_capture{};

struct MenuPointerCapture {
    nk_context* context{};
    struct nk_mouse original{};
    bool captured{};

    explicit MenuPointerCapture(nk_context* current) : context(current) {
        g_menu_pointer_capture = this;
    }
    void capture() {
        if (!context || captured) return;
        original = context->input.mouse;
        captured = true;
        for (auto& button : context->input.mouse.buttons) button = {};
        context->input.mouse.delta = {};
        context->input.mouse.scroll_delta = {};
    }
    ~MenuPointerCapture() {
        if (captured) {
            std::copy(std::begin(original.buttons), std::end(original.buttons),
                      std::begin(context->input.mouse.buttons));
            context->input.mouse.delta = original.delta;
            context->input.mouse.scroll_delta = original.scroll_delta;
        }
        g_menu_pointer_capture = nullptr;
    }
};

static void render_menu_titles(WormUiWorkspaceAction& action) {
    for (const auto* title : wormedit::ui::workspace_menu_titles) {
        const auto chosen = wormedit::ui::render_workspace_menu(g_context, g_workspace_menu,
            title, g_window_focused, g_menu_popup_open, g_menu_popup_release_pending);
        if (chosen != MenuAction::none && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
            apply_workspace_menu_action(chosen, action);
    }
}

static void render_title_bar(const WormUiWorkspaceView* view,
                             WormUiWorkspaceAction* action) {
    const bool menu_was_open = g_menu_popup_open;
    (void)wormedit::ui::chrome::popup_update_release_pending(g_context, g_menu_popup_release_pending);
    g_menu_popup_open = false;
    close_unavailable_inline_offset(*view);
    const bool custom_controls = g_window &&
        (SDL_GetWindowFlags(g_window) & SDL_WINDOW_BORDERLESS) != 0;
    if (native_menu_active()) return;
    nk_menubar_begin(g_context);
    nk_layout_row_template_begin(g_context, 32);
    nk_layout_row_template_push_static(g_context, wormedit::ui::workspace_menu_icon_width);
    for (const auto width : wormedit::ui::workspace_menu_title_widths)
        nk_layout_row_template_push_static(g_context, width);
    nk_layout_row_template_push_dynamic(g_context);
    if (custom_controls) wormedit::ui::chrome::IconButton::layout_columns(g_context, 3);
    nk_layout_row_template_end(g_context);
    struct nk_rect app_icon{};
    if (nk_widget(&app_icon, g_context) != NK_WIDGET_INVALID)
        wormedit::ui::lucide::draw(g_context, app_icon, Glyph::audio_lines,
            wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted));
    render_menu_titles(*action);
    render_title_label(*view);
    if (custom_controls) {
        if (wormedit::ui::chrome::IconButton{Glyph::minus, "Minimize window"}.render(g_context))
            SDL_MinimizeWindow(g_window);
        const bool maximized = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) != 0;
        if (wormedit::ui::chrome::IconButton{maximized ? Glyph::minimize_2 : Glyph::maximize,
                                             maximized ? "Restore window" : "Maximize window"}
                .render(g_context)) {
            if (maximized) SDL_RestoreWindow(g_window);
            else SDL_MaximizeWindow(g_window);
        }
        if (wormedit::ui::chrome::IconButton{Glyph::x, "Close window"}.render(g_context))
            action->kind = WORM_UI_WORKSPACE_ACTION_QUIT;
    }
    nk_menubar_end(g_context);
    if (menu_was_open && !g_menu_popup_open) g_menu_popup_release_pending = true;
    if (g_menu_pointer_capture && (g_menu_popup_open || g_menu_popup_release_pending))
        g_menu_pointer_capture->capture();
}

static void render_workspace_tabs(const WormUiWorkspaceView* view,
                                  WormUiWorkspaceAction* action) {
    int visible_tabs = 0;
    for (const auto& item : view->tabs) visible_tabs += item.label != nullptr;
    nk_layout_row_dynamic(g_context, 32, visible_tabs);
    for (int tab = 0; tab < WORM_UI_WORKSPACE_TAB_COUNT; ++tab) {
        const auto& item = view->tabs[tab];
        if (!item.label) continue;
        if (!item.enabled) nk_widget_disable_begin(g_context);
        const bool selected = tab == static_cast<int>(view->active_tab);
        const bool clicked = wormedit::ui::chrome::mode_button(
            g_context, item.label, nullptr, selected);
        if (!item.enabled) nk_widget_disable_end(g_context);
        if (clicked && item.enabled && !selected &&
            action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action->kind = WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
            action->tab = static_cast<WormUiWorkspaceTab>(tab);
        }
    }
}

static bool status_minimap_available(const WormUiComposeView& view) {
    // Any song waveform enables the bar, whether or not the chart has BGM events.
    const bool has_audio = view.has_bgm_events || view.bgm_bar_row_count != 0 ||
                           view.waveform_overview_row_count != 0;
    return view.visible && view.scene_available && has_audio &&
           worm_ui_nuklear_compose_overview_valid(&view);
}

static void render_minimap_preferences(const WormUiWorkspaceView& view,
                                      WormUiWorkspaceAction* action, struct nk_rect bounds) {
    static bool open = false;
    static struct nk_rect popup_bounds{};
    if (nk_input_mouse_clicked(&g_context->input, NK_BUTTON_RIGHT, bounds)) {
        const auto content = nk_window_get_content_region(g_context);
        popup_bounds = nk_rect(
            std::clamp(bounds.x - content.x, 0.0f, std::max(0.0f, content.w - 340.0f)),
            std::max(0.0f, bounds.y - content.y - 70.0f), 340, 60);
        open = true;
    }
    if (!open) return;
    if (!nk_popup_begin(g_context, NK_POPUP_STATIC, "Minimap preferences",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, popup_bounds)) {
        open = false;
        return;
    }
    nk_layout_row_dynamic(g_context, 30, 1);
    const char* label = view.minimap_click_moves_paused_playhead
        ? "[x] Plain click moves paused playhead" : "[ ] Plain click moves paused playhead";
    if (nk_button_label(g_context, label) &&
        action->compose_input_count < WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        auto& input = action->compose_inputs[action->compose_input_count++];
        input = {};
        input.kind = WORM_UI_COMPOSE_INPUT_SET_MINIMAP_PAUSED_BEHAVIOR;
        input.overview_primary = !view.minimap_click_moves_paused_playhead;
        open = false;
        nk_popup_close(g_context);
    }
    nk_popup_end(g_context);
}

static void render_status_minimap(const WormUiWorkspaceView& view,
                                  WormUiWorkspaceAction* action, bool available) {
    if (available && view.compose.bgm_bar_row_count == 0) {
        const auto bounds = nk_widget_bounds(g_context);
        nk_widget_disable_begin(g_context);
        nk_label(g_context, "Overview unavailable", NK_TEXT_CENTERED);
        nk_widget_disable_end(g_context);
        if (wormedit::ui::nuklear::tooltip::hovered(g_context, bounds))
            wormedit::ui::nuklear::tooltip::text(
                g_context, "Audio is unavailable for this chart overview.");
        return;
    }
    struct nk_rect bounds{};
    const auto widget = available ? nk_widget(&bounds, g_context) : NK_WIDGET_INVALID;
    const bool interactive = widget == NK_WIDGET_VALID || widget == NK_WIDGET_ROM;
    const bool readonly = (g_context->current->layout->flags & NK_WINDOW_ROM) != 0;
    const bool focused = g_window_focused && interactive && !readonly &&
        !g_new_project_wizard.open && !project_dialog_active() &&
        (!view.pending_bms_encoding_path || !view.pending_bms_encoding_path[0]);
    worm_ui_nuklear_render_compose_bgm_bar(g_context, &view.compose, action, bounds, focused);
    if (available && g_window_focused && !g_new_project_wizard.open && !project_dialog_active())
        render_minimap_preferences(view, action, bounds);
}

static void render_elided_status_label(std::string_view text, nk_flags alignment,
                                       struct nk_color color) {
    const auto width = nk_widget_bounds(g_context).w;
    const auto* font = g_context->style.font;
    const auto label = wormedit::ui::chrome::fit_text_with_ellipsis(font, text, width);
    nk_label_colored(g_context, label.c_str(), alignment, color);
}

static auto workspace_status_line(const WormUiWorkspaceView& view) -> std::string {
    const auto* line = view.notifications.current_text ? view.notifications.current_text : view.status_text;
    std::string status = line;
    return status;
}

static void render_transport_rate(const WormUiWorkspaceView& view, WormUiWorkspaceAction* action) {
    static const char* labels[]{"1×", "0.9×", "0.8×", "0.75×", "0.5×", "0.33×", "0.25×", "0.2×", "0.1×"};
    const int index = view.transport_rate_index < 9 ? view.transport_rate_index : 8;
    const int label_index = 8 - index;
    int target = index;
    nk_spacing(g_context, 1);
    if (!view.transport_rate_available) nk_widget_disable_begin(g_context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left, "Slower playback rate"}.render(g_context) &&
        index > 0) target = index - 1;
    nk_label(g_context, labels[label_index], NK_TEXT_CENTERED);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_right, "Faster playback rate"}.render(g_context) &&
        index < 8) target = index + 1;
    if (!view.transport_rate_available) nk_widget_disable_end(g_context);
    nk_spacing(g_context, 1);
    if (view.transport_rate_available && target != index && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE;
        action->transport_rate_index = static_cast<uint8_t>(target);
    }
    if (!view.transport_keep_pitch_available) nk_widget_disable_begin(g_context);
    const auto* keep_pitch_hint = view.transport_keep_pitch_available
        ? "Preserve pitch at reduced speed" : "Keep pitch is unavailable without audio output";
    if (wormedit::ui::chrome::IconButton{Glyph::music_2, keep_pitch_hint,
                                         view.transport_keep_pitch}.render(g_context) &&
        view.transport_keep_pitch_available && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH;
        action->transport_keep_pitch = !view.transport_keep_pitch;
    }
    if (!view.transport_keep_pitch_available) nk_widget_disable_end(g_context);
}

static void render_metronome_toggle(const WormUiWorkspaceView& view,
                                    WormUiWorkspaceAction& action) {
    if (!view.transport_metronome_available) nk_widget_disable_begin(g_context);
    const auto hint = view.transport_metronome_enabled ? "Disable metronome" : "Enable metronome";
    if (wormedit::ui::chrome::IconButton{Glyph::metronome, hint,
                                        view.transport_metronome_enabled}.render(g_context) &&
        view.transport_metronome_available && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_TOGGLE_METRONOME;
    if (!view.transport_metronome_available) nk_widget_disable_end(g_context);
}

static bool notification_badge_visible(const WormUiNotificationView& view) {
    return view.unread_warnings != 0 || view.unread_errors != 0;
}

static nk_color notification_status_color(const WormUiNotificationView& view) {
    using wormedit::ui::theme::UiColor;
    if (view.current_severity == WORM_UI_NOTIFICATION_ERROR)
        return wormedit::ui::theme::ui(UiColor::accent);
    if (view.current_severity == WORM_UI_NOTIFICATION_WARNING)
        return wormedit::ui::theme::ui(UiColor::text_bright);
    return wormedit::ui::theme::ui(UiColor::text);
}

static void render_notification_badge(const WormUiNotificationView& view,
                                     WormUiWorkspaceAction& action) {
    char label[64]{};
    std::snprintf(label, sizeof(label), "%zu warnings · %zu errors", view.unread_warnings,
                  view.unread_errors);
    if (!view.history_available) {
        nk_label(g_context, label, NK_TEXT_LEFT);
        return;
    }
    const auto bounds = nk_widget_bounds(g_context);
    const auto hovered_visible_badge = nk_widget_is_hovered(g_context) != 0;
    const auto clicked = nk_button_label(g_context, label) != 0;
    const auto click_matches_visible_badge =
        nk_input_is_mouse_hovering_rect(&g_context->input, bounds) != 0 &&
        hovered_visible_badge;
    if (clicked && click_matches_visible_badge)
        action.notifications = {WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY, 0};
}

static void render_notification_landing_status(const WormUiWorkspaceView& view,
                                               WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(g_context, 38, 1);
    const auto* text = view.notifications.current_text ? view.notifications.current_text : view.status_text;
    nk_label_wrap(g_context, text);
    if (!notification_badge_visible(view.notifications)) return;
    nk_layout_row_dynamic(g_context, 28, 1);
    render_notification_badge(view.notifications, action);
}

static bool snap_badge_available(const WormUiWorkspaceView& view) {
    const auto own = wormedit::ui::chrome::context_menu_owns_input(
        worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu);
    return g_window_focused && view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE &&
        view.compose.scene_available && !view.compose.input_occluded && !view.options.visible &&
        !project_modal_active(view) && !g_notification_modal && !project_dialog_active() &&
        !nuklear_text_entry_active() && !g_ime_composition_active &&
        !notification_document_gesture_active() && !view.compose.paste_active &&
        (!compose_popup_owns_input() || own);
}

static void render_status_transport_buttons(const WormUiWorkspaceView* view,
                                            WormUiWorkspaceAction* action) {
    if (!view->transport_available) nk_widget_disable_begin(g_context);
    const char* play_hint = view->transport_playing ? "Pause playback"
                            : view->transport_paused ? "Resume playback"
                            : view->compose.visible ? "Play from cursor" : "Play chart";
    if (wormedit::ui::chrome::IconButton{view->transport_playing ? Glyph::pause : Glyph::play,
                                         play_hint,
                                         view->transport_playing}.render(g_context) &&
        view->transport_available)
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE;
    if (!view->transport_available) nk_widget_disable_end(g_context);
    const bool can_stop = view->transport_playing || view->transport_paused;
    if (!can_stop) nk_widget_disable_begin(g_context);
    if (wormedit::ui::chrome::IconButton{Glyph::square, "Stop playback"}
            .render(g_context) &&
        can_stop)
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP;
    if (!can_stop) nk_widget_disable_end(g_context);
    if (wormedit::ui::chrome::IconButton{Glyph::eye,
                                         view->player_preview.visible ? "Hide player preview"
                                                                      : "Show player preview",
                                         view->player_preview.visible}.render(g_context) &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action->kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW;
    render_metronome_toggle(*view, *action);
}

static void render_status_bar(const WormUiWorkspaceView* view,
                              WormUiWorkspaceAction* action) {
    const bool snap_label_visible = view->compose.visible && view->compose.area_snap_denominator != 0;
    // Zoom readout, moved here from the Compose toolbar; hidden when the window is too narrow.
    const bool zoom_label_visible = view->compose.visible && snap_label_visible &&
        std::isfinite(view->compose.pixels_per_standard_measure) &&
        nk_window_get_bounds(g_context).w >= 900.0f;
    nk_layout_row_template_begin(g_context, 30);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 4);
    // Rate stepper: chevrons hug the value, with a gutter on both sides.
    nk_layout_row_template_push_static(g_context, 10);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 1);
    nk_layout_row_template_push_static(g_context, 40);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 1);
    nk_layout_row_template_push_static(g_context, 10);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 1);  // Keep pitch.
    if (snap_label_visible) nk_layout_row_template_push_static(g_context, 64);
    if (zoom_label_visible) nk_layout_row_template_push_static(g_context, 116);
    nk_layout_row_template_push_dynamic(g_context);
    nk_layout_row_template_push_static(g_context,
        std::clamp((nk_window_get_bounds(g_context).w - 700.0f) * 0.5f, 40.0f, 180.0f));
    nk_layout_row_template_push_static(g_context, 80);
    nk_layout_row_template_push_static(g_context,
        std::clamp(nk_window_get_bounds(g_context).w * 0.2f, 80.0f, 240.0f));
    nk_layout_row_template_end(g_context);
    render_status_transport_buttons(view, action);
    render_transport_rate(*view, action);
    if (snap_label_visible)
        render_compose_snap_badge(g_context, view->compose, *action, snap_badge_available(*view));
    if (zoom_label_visible)
        nk_labelf(g_context, NK_TEXT_CENTERED, "%.0f px/measure",
                  view->compose.pixels_per_standard_measure);
    const auto status = workspace_status_line(*view);
    render_elided_status_label(status, NK_TEXT_LEFT, notification_status_color(view->notifications));
    if (notification_badge_visible(view->notifications))
        render_notification_badge(view->notifications, *action);
    else nk_spacing(g_context, 1);
    const auto sample_rate = view->transport_sample_rate == 0 ? 1U : view->transport_sample_rate;
    const auto seconds = view->transport_frame / sample_rate;
    nk_labelf(g_context, NK_TEXT_RIGHT, "%llu:%02llu",
              static_cast<unsigned long long>(seconds / 60),
              static_cast<unsigned long long>(seconds % 60));
    const auto details = std::string{view->artist} + " · " + view->selected_difficulty;
    render_elided_status_label(details, NK_TEXT_RIGHT, g_context->style.text.color);
}

static auto workspace_content_flags(bool compose_visible, bool form_screen) -> nk_flags {
    if (form_screen) return 0;
    return compose_visible ? NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR : NK_WINDOW_BORDER;
}

static void prepare_workspace_content_scroll(nk_context* context, bool compose_visible) {
    if (compose_visible) nk_group_set_scroll(context, "workspace-content", 0, 0);
}

struct WorkspaceRenderContext {
    const WormUiWorkspaceView& view;
    WormUiWorkspaceAction& action;
    int width;
    int height;
};

static auto recovery_panel_height(const WormUiWorkspaceView& view) -> float {
    float height = 40.0f +
                   (view.recovery_notice && view.recovery_notice[0] ? 22.0f : 0.0f) +
                   static_cast<float>(view.recovery_candidate_count) * 48.0f +
                   static_cast<float>(view.recovery_diagnostic_count) * 18.0f;
    for (size_t row = 0; row < view.recovery_candidate_count; ++row) {
        const auto* candidate = &view.recovery_candidates[row];
        if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
            height += 25.0f;
            if (g_recovery_save_as_destination[0] == '\0') height += 18.0f;
        }
        if (candidate->disabled_reason && candidate->disabled_reason[0]) height += 20.0f;
    }
    return height;
}

static void render_recovery_buttons(const WormUiRecoveryCandidateView* candidate,
                                   WormUiWorkspaceAction& action) {
    const struct {
        size_t bit;
        const char* label;
        WormUiWorkspaceActionKind kind;
    } buttons[] = {
        {WORM_UI_RECOVERY_ACTION_INSPECT, "Inspect", WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT},
        {WORM_UI_RECOVERY_ACTION_RECOVER, "Recover", WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER},
        {WORM_UI_RECOVERY_ACTION_SAVE_AS, "Save as", WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS},
        {WORM_UI_RECOVERY_ACTION_DISCARD, "Discard", WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD},
        {WORM_UI_RECOVERY_ACTION_QUARANTINE, "Quarantine",
         WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE},
    };
    nk_layout_row_dynamic(g_context, 25, 5);
    for (size_t button = 0; button < sizeof(buttons) / sizeof(buttons[0]); ++button) {
        const int offered = recovery_action_offered(candidate, buttons[button].bit);
        if (!offered) nk_widget_disable_begin(g_context);
        const int clicked = nk_button_label(g_context, buttons[button].label);
        if (!offered) nk_widget_disable_end(g_context);
        const int save_as_ready = buttons[button].kind != WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS ||
                                  g_recovery_save_as_destination[0] != '\0';
        if (offered && save_as_ready && clicked &&
            action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
            set_recovery_action(&action, candidate, buttons[button].kind);
    }
}

static void render_recovery_candidate(const WormUiRecoveryCandidateView* candidate,
                                      WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(g_context, 20, 1);
    nk_labelf(g_context, NK_TEXT_LEFT, "%s  %s",
              candidate->path_label ? candidate->path_label : "(unknown recovery path)",
              candidate->summary ? candidate->summary : "");
    render_recovery_buttons(candidate, action);
    if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(g_context, 0.18f);
        nk_label(g_context, "New project path", NK_TEXT_LEFT);
        nk_layout_row_push(g_context, 0.82f);
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
            g_context, NK_EDIT_FIELD, g_recovery_save_as_destination,
            sizeof(g_recovery_save_as_destination), nk_filter_default);
        nk_layout_row_end(g_context);
        if (g_recovery_save_as_destination[0] == '\0') {
            nk_layout_row_dynamic(g_context, 18, 1);
            nk_label(g_context, "Save as is disabled until a new destination is entered.",
                     NK_TEXT_LEFT);
        }
    }
    if (candidate->disabled_reason && candidate->disabled_reason[0]) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, candidate->disabled_reason, NK_TEXT_LEFT);
    }
}

static auto render_recovery_panel(const WormUiWorkspaceView& view,
                                  WormUiWorkspaceAction& action) -> float {
    if (!view.recovery_panel_visible || !view.recovery_has_actionable_candidate) return 0.0f;
    const auto height = recovery_panel_height(view);
    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context, view.recovery_heading ? view.recovery_heading : "Recovery", NK_TEXT_LEFT);
    if (view.recovery_notice && view.recovery_notice[0]) {
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_label(g_context, view.recovery_notice, NK_TEXT_LEFT);
    }
    for (size_t row = 0; row < view.recovery_candidate_count; ++row)
        render_recovery_candidate(&view.recovery_candidates[row], action);
    if (view.recovery_rows_truncated) {
        nk_layout_row_dynamic(g_context, 18, 1);
        nk_label(g_context, "Additional recovery candidates are not shown.", NK_TEXT_LEFT);
    }
    for (size_t diagnostic = 0; diagnostic < view.recovery_diagnostic_count; ++diagnostic) {
        if (view.recovery_diagnostics[diagnostic]) {
            nk_layout_row_dynamic(g_context, 18, 1);
            nk_label(g_context, view.recovery_diagnostics[diagnostic], NK_TEXT_LEFT);
        }
    }
    return height;
}

static auto render_recovery_above_content(const WormUiWorkspaceView& view,
    WormUiWorkspaceAction& action) -> float {
    if (view.setup.visible) return 0.0f;
    return render_recovery_panel(view, action);
}

static bool history_owns_content_input(const WormUiWorkspaceView& view) {
    if (!g_context || g_notification_project_modal || notification_editor_text_owned() ||
        notification_document_gesture_active() || notification_external_pointer_active()) return false;
    if (g_notification_history_state.pointer_owned_this_frame || history_pointer_capture_active()) return true;
    return view.notifications.history.visible && notification_history_contains(
        notification_history_bounds(false), g_context->input.mouse.pos.x, g_context->input.mouse.pos.y);
}

static bool modeless_window_owns_pointer(const char* name, struct nk_rect fallback) {
    if (!g_context) return false;
    const auto* window = nk_window_find(g_context, name);
    const auto bounds = window ? window->bounds : fallback;
    if (nk_input_is_mouse_hovering_rect(&g_context->input, bounds)) return true;
    for (const auto& button : g_context->input.mouse.buttons) {
        if ((button.down || button.clicked) &&
            wormedit::ui::is_point_in_rect({bounds.x, bounds.y, bounds.w, bounds.h},
                button.clicked_pos.x, button.clicked_pos.y, wormedit::ui::rect_edges_t::inclusive))
            return true;
    }
    return false;
}

static bool modeless_timing_owns_pointer(const WormUiWorkspaceView& view) {
    return (view.timing_settings_visible && modeless_window_owns_pointer(
                "Timing Settings", nk_rect(340, 120, 420, 430))) ||
           (view.tap_timing.visible && modeless_window_owns_pointer(
                "Tap timing", nk_rect(300, 110, 470, 530)));
}

static bool options_owns_content_input(const WormUiWorkspaceView& view) {
    if (view.close_confirmation.visible) return true;
    if (g_menu_popup_open || g_menu_popup_release_pending) return true;
    if (history_owns_content_input(view)) return true;
    if (modeless_timing_owns_pointer(view)) return true;
    const auto& compose = worm_ui_nuklear_compose_state();
    const bool text_active = nuklear_text_entry_active() || g_ime_composition_active ||
                             compose.inline_timing.inline_timing_open || compose.dialogs.go_to_measure_open ||
                             compose_popup_owns_input();
    if (g_context && !g_notification_project_modal &&
        wormedit::ui::nuklear::notification_content_occluded(g_notification_state,
            g_context->input.mouse.pos.x, g_context->input.mouse.pos.y,
            notification_editor_gesture_active(), text_active)) return true;
    if (!view.options.visible || !g_context) return false;
    const auto* window = nk_window_find(g_context, "Options");
    const auto bounds = window ? window->bounds : nk_rect(180, 110, 800, 450);
    if (nk_input_is_mouse_hovering_rect(&g_context->input, bounds)) return true;
    for (const auto& button : g_context->input.mouse.buttons) {
        if (button.down && wormedit::ui::is_point_in_rect({bounds.x, bounds.y, bounds.w, bounds.h},
                button.clicked_pos.x, button.clicked_pos.y, wormedit::ui::rect_edges_t::inclusive)) return true;
    }
    return nk_window_is_active(g_context, "Options") && nuklear_text_entry_active();
}

static void retire_sound_picker(const WormUiWorkspaceView& view, WormUiWorkspaceAction& action) {
    if (view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view.compose.visible &&
        !view.options.visible && !project_modal_active(view)) return;
    auto& state = worm_ui_nuklear_compose_state();
    cancel_compose_sound_picker(state.sound_picker_ui.sound_picker);
    render_compose_sound_picker(g_context, view.compose, action, state, false);
}

static void retire_bookmark_editors(const WormUiWorkspaceView& view, WormUiWorkspaceAction& action) {
    if (view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view.compose.visible) return;
    auto& state = worm_ui_nuklear_compose_state();
    worm_ui_nuklear_bookmark_chooser_cancel(g_context, state.bookmark_ui.bookmark_chooser);
    cancel_compose_bookmark_rename(state.bookmark_ui.bookmark_rename);
    (void)worm_ui_nuklear_render_compose_bookmark_chooser(g_context, view.compose.bookmark_chooser,
        &action, false, &state.bookmark_ui.bookmark_chooser);
    render_compose_bookmark_rename(g_context, view.compose, action, state.bookmark_ui.bookmark_rename,
        nk_rect(0, 0, 0, 0), false);
}

static bool audio_content_interactive(const WormUiWorkspaceView& view) {
    return g_window_focused && !view.options.visible && !options_owns_content_input(view) &&
        !project_modal_active(view) && !project_dialog_active() && !g_notification_modal &&
        !g_menu_popup_open;
}

static void render_workspace_content(const WormUiWorkspaceView& view,
                                     WormUiWorkspaceAction& action) {
    const bool metadata_interactive = g_window_focused && view.metadata.visible &&
        !options_owns_content_input(view) && !project_modal_active(view) &&
        !project_dialog_active() && !g_notification_modal && !g_menu_popup_open;
    if (!metadata_interactive) worm_ui_nuklear_cancel_metadata_tag_drag();
    prepare_workspace_content_scroll(g_context, view.compose.visible);
    // Project and Metadata share the form side margins.
    const bool padded = view.metadata.visible || view.setup.visible;
    if (padded) {
        const auto padding = g_context->style.window.group_padding;
        const auto form_margin = wormedit::ui::layout::form_side_margin(
            static_cast<float>(g_last_window_width), static_cast<float>(g_last_window_height));
        // The multi-column chart table needs more width than Metadata's form fields.
        const auto margin = view.setup.visible ? std::min(form_margin, 48.0f) : form_margin;
        nk_style_push_vec2(g_context, &g_context->style.window.group_padding,
                           nk_vec2(margin, padding.y));
    }
    const bool open = nk_group_begin(g_context, "workspace-content",
                                     workspace_content_flags(view.compose.visible, view.metadata.visible)) != 0;
    if (padded) nk_style_pop_vec2(g_context);
    if (!open) return;
    retire_sound_picker(view, action);
    retire_bookmark_editors(view, action);
    if (view.setup.visible) {
        worm_ui_nuklear_render_setup_panel(g_context, &view.setup, &action);
        (void)render_recovery_panel(view, action);
    } else if (view.metadata.visible) {
        worm_ui_nuklear_render_metadata_panel(g_context, &view.metadata, &action,
                                              metadata_interactive);
    } else if (view.compose.visible) {
        auto compose = view.compose;
        compose.input_occluded = options_owns_content_input(view);
        worm_ui_nuklear_render_compose_panel(
            g_context, &compose, &view.timing, &action, g_window_focused,
            g_ime_composition_active, g_double_clicks.data(), g_double_click_count,
            g_shortcut_event_count);
    } else if (view.audio_slices.visible) {
        worm_ui_nuklear_render_audio_slices(g_context, &view.audio_slices, &action, true,
                                            audio_content_interactive(view));
    } else if (view.compare.visible) {
        worm_ui_nuklear_render_compare_panel(g_context, &view.compare, &action);
    } else {
        nk_layout_row_dynamic(g_context, 24, 1);
        if (view.summary_line_count == 0) nk_label(g_context, "No items in this view.", NK_TEXT_LEFT);
        for (size_t line = 0; line < view.summary_line_count; ++line) {
            if (view.summary_lines[line]) nk_label(g_context, view.summary_lines[line], NK_TEXT_LEFT);
        }
    }
    nk_group_end(g_context);
}

static auto workspace_base_view_valid(const WormUiWorkspaceView& view) -> bool {
    return view.project_title && view.artist && view.selected_difficulty && view.status_text &&
           view.git_hash && view.tab_heading &&
           view.summary_line_count <= WORM_UI_WORKSPACE_SUMMARY_CAPACITY &&
           view.recovery_candidate_count <= WORM_UI_RECOVERY_ROW_CAPACITY &&
           view.recovery_diagnostic_count <= WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY &&
           view.active_tab >= WORM_UI_WORKSPACE_TAB_SETUP &&
           view.active_tab <= WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE;
}

static auto metadata_workspace_view_valid(const WormUiMetadataView& metadata) -> bool {
    return !metadata.visible ||
           (metadata.format_field_count <= WORM_UI_METADATA_FORMAT_FIELD_CAPACITY &&
            metadata.format_target_count <= WORM_UI_METADATA_FORMAT_TARGET_CAPACITY &&
            metadata.audio_waveform_column_count <= WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY &&
            (metadata.audio_waveform_column_count == 0 || metadata.audio_waveform_columns) &&
            !(metadata.audio_waveform_analysis_pending && metadata.audio_waveform_available) &&
            (metadata.audio_waveform_available || metadata.audio_waveform_column_count == 0) &&
            (!metadata.audio_waveform_available ||
             (metadata.audio_waveform_column_count != 0 &&
              metadata.audio_waveform_total_frames != 0 &&
              metadata.audio_waveform_sample_rate != 0 &&
              metadata.audio_waveform_has_selected_asset)));
}

static auto workspace_validation_error(const WormUiWorkspaceView& view) -> const char* {
    if (!view.project_loaded)
        return view.project_title && view.status_text &&
                       view.recovery_candidate_count <= WORM_UI_RECOVERY_ROW_CAPACITY &&
                       view.recovery_diagnostic_count <= WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY
                   ? nullptr : "Empty workspace view data is invalid.";
    if (!workspace_base_view_valid(view)) return "Workspace view data is invalid.";
    if (!worm_ui_nuklear_setup_view_valid(&view.setup)) return "Project view data is invalid.";
    if (!metadata_workspace_view_valid(view.metadata)) return "Metadata view data is invalid.";
    if (view.compose.visible &&
        !worm_ui_nuklear_timing_view_valid(&view.timing))
        return "Timing view data is invalid.";
    if (view.audio_slices.visible &&
        !worm_ui_nuklear_audio_slices_view_valid(&view.audio_slices))
        return "Audio slices view data is invalid.";
    if (!worm_ui_nuklear_player_preview_view_valid(&view.player_preview))
        return "Player preview view data is invalid.";
    if (view.compose.visible && !worm_ui_nuklear_compose_view_valid(&view.compose))
        return "Compose view data is invalid.";
    return nullptr;
}

// Two rows under the mode tabs: the transport seek bar, always, and below it the song-length
// BGM bar when the chart has BGM audio. Stopped or paused, the seek bar shows the Compose cursor
// (or the paused point) as the playhead. Dragging while stopped navigates Compose;
// dragging during playback or pause seeks the transport.
static constexpr float preview_row_height = 24.0f;
static constexpr float bgm_bar_row_height = 48.0f;

static void render_seek_grip(struct nk_rect bounds, nk_size position, nk_size steps) {
    using wormedit::ui::theme::UiColor;
    auto* canvas = nk_window_get_canvas(g_context);
    const float width = 14.0f;
    const float x = bounds.x + static_cast<float>(position) / static_cast<float>(steps) *
        std::max(0.0f, bounds.w - width);
    const auto handle = nk_rect(x, bounds.y, width, bounds.h);
    nk_fill_rect(canvas, handle, 3.0f, wormedit::ui::chrome::color(UiColor::text_bright));
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 2; ++column) {
            const auto dot = nk_rect(x + 4.0f + column * 4.0f,
                bounds.y + bounds.h * 0.5f - 5.0f + row * 4.0f, 2.0f, 2.0f);
            nk_fill_circle(canvas, dot, wormedit::ui::chrome::color(UiColor::background));
        }
    }
}

static void report_transport_seek_hover(const WormUiWorkspaceView& view,
                                        WormUiWorkspaceAction& action, struct nk_rect bounds) {
    if (view.compose.selected_audio_asset_id == 0 || view.transport_sample_rate == 0 ||
        view.transport_end_frame == 0 || !nk_input_is_mouse_hovering_rect(&g_context->input, bounds))
        return;
    const auto inset_x = g_context->style.progress.padding.x + g_context->style.progress.border;
    const auto inset_y = g_context->style.progress.padding.y + g_context->style.progress.border;
    const auto cursor = nk_rect(bounds.x + inset_x, bounds.y + inset_y,
                                bounds.w - 2.0f * inset_x, bounds.h - 2.0f * inset_y);
    const auto ratio = cursor.w > 0.0f
        ? std::max(0.0f, static_cast<float>(g_context->input.mouse.pos.x - cursor.x)) / cursor.w
        : 0.0f;
    constexpr nk_size seek_steps = 10'000;
    const auto pointer_step = static_cast<nk_size>(std::clamp(
        static_cast<float>(seek_steps) * ratio, 0.0f, static_cast<float>(seek_steps)));
    const auto source_frame =
        (view.transport_end_frame / seek_steps) * pointer_step +
        ((view.transport_end_frame % seek_steps) * pointer_step) / seek_steps;
    if (source_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return;
    action.hover_source_frame = static_cast<std::int64_t>(source_frame);
    action.hover_source_frame_valid = true;
    action.hover_source_total_frames = view.transport_end_frame;
    action.hover_source_sample_rate = view.transport_sample_rate;
    const auto& compose = view.compose;
    if (compose.hover_timing_valid && compose.hover_timing_source_frame == action.hover_source_frame &&
        compose.hover_timing_total_frames == view.transport_end_frame &&
        compose.hover_timing_sample_rate == view.transport_sample_rate &&
        wormedit::ui::nuklear::tooltip::hovered(g_context, bounds))
        wormedit::ui::chrome::timing_tooltip(g_context, compose.hover_timing_label);
}

static void render_transport_seek_bar(const WormUiWorkspaceView& view,
                                      WormUiWorkspaceAction& action) {
    constexpr nk_size seek_steps = 10'000;
    nk_layout_row_dynamic(g_context, preview_row_height, 1);
    if (view.transport_end_frame == 0) {
        nk_size unavailable_position = 0;
        nk_widget_disable_begin(g_context);
        nk_progress(g_context, &unavailable_position, seek_steps, NK_FIXED);
        nk_widget_disable_end(g_context);
        return;
    }
    const auto bounded_frame = std::min(view.transport_frame, view.transport_end_frame);
    nk_size seek_position = static_cast<nk_size>(
        static_cast<long double>(bounded_frame) /
        static_cast<long double>(view.transport_end_frame) * seek_steps);
    const bool draggable = view.transport_available &&
        (view.compose.scene_available || view.transport_playing || view.transport_paused);
    const auto bounds = nk_widget_bounds(g_context);
    if (nk_progress(g_context, &seek_position, seek_steps, draggable ? NK_MODIFIABLE : NK_FIXED) &&
        draggable) {
        action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK;
        action.transport_seek_frame =
            (view.transport_end_frame / seek_steps) * seek_position +
            ((view.transport_end_frame % seek_steps) * seek_position) / seek_steps;
    }
    report_transport_seek_hover(view, action, bounds);
    render_seek_grip(bounds, seek_position, seek_steps);
}

// Flat stand-in while the song's waveform is still being prepared.
static void render_bgm_bar_placeholder() {
    struct nk_rect bounds{};
    if (nk_widget(&bounds, g_context) == NK_WIDGET_INVALID) return;
    auto* canvas = nk_window_get_canvas(g_context);
    nk_fill_rect(canvas, bounds, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::overview));
    const auto* font = g_context->style.font;
    const char* text = "Preparing song waveform…";
    if (!font) return;
    const auto length = static_cast<int>(std::strlen(text));
    const auto width = font->width(font->userdata, font->height, text, length);
    nk_draw_text(canvas, nk_rect(bounds.x + (bounds.w - width) * 0.5f,
                                 bounds.y + (bounds.h - font->height) * 0.5f, width, font->height),
                 text, length, font, nk_rgba(0, 0, 0, 0),
                 wormedit::ui::chrome::color(wormedit::ui::theme::UiColor::text_muted));
}

static void render_transport_seek_control(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action) {
    render_transport_seek_bar(view, action);
    // Without song audio, on other tabs, or when hidden from the toolbar the row does not exist.
    if (!view.compose.visible || !view.compose.bgm_bar_reserved) return;
    nk_layout_row_dynamic(g_context, bgm_bar_row_height, 1);
    if (status_minimap_available(view.compose)) render_status_minimap(view, &action, true);
    else render_bgm_bar_placeholder();
}

static void render_recent_project_list(const WormUiWorkspaceView& view,
                                       WormUiWorkspaceAction& action, float height) {
    const auto count = std::min(view.recent_file_count, std::size(view.recent_files));
    if (!count) return;
    nk_layout_row_dynamic(g_context, 30, 1);
    nk_label(g_context, "Recent projects and charts", NK_TEXT_LEFT);
    const auto area = nk_window_get_content_region(g_context);
    const float remaining = area.y + area.h - nk_widget_position(g_context).y - 12.0f;
    nk_layout_row_dynamic(g_context, std::max(48.0f, std::min(height, remaining)), 1);
    if (!nk_group_begin(g_context, "recent-projects-list", NK_WINDOW_BORDER)) return;
    const bool blocked = project_dialog_active() || project_modal_active(view);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& entry = view.recent_files[index];
        nk_layout_row_dynamic(g_context, 34, 1);
        const auto bounds = nk_widget_bounds(g_context);
        if (blocked) nk_widget_disable_begin(g_context);
        const bool chosen = nk_button_label(g_context, entry.label);
        if (blocked) nk_widget_disable_end(g_context);
        if (chosen && !blocked && action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
            std::snprintf(action.project_path, sizeof(action.project_path), "%s", entry.path);
        }
        if (wormedit::ui::nuklear::tooltip::hovered(g_context, bounds))
            wormedit::ui::nuklear::tooltip::text(g_context, entry.path);
    }
    nk_group_end(g_context);
}

static void render_startup_buttons(WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(g_context, 42, 1);
    const bool dialog_busy = project_dialog_active();
    if (dialog_busy) nk_widget_disable_begin(g_context);
    if (nk_button_label(g_context, "New project") &&
        !dialog_busy && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        request_manual_new_project();
    if (nk_button_label(g_context, dialog_busy ? "Choosing file..." : "Open chart or project...") &&
        !dialog_busy && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_OPEN_FILE);
    if (dialog_busy) nk_widget_disable_end(g_context);
}

static void render_empty_project_content(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action, float height) {
    nk_layout_row_dynamic(g_context, height, 1);
    if (!nk_group_begin(g_context, "empty-project-content", NK_WINDOW_NO_SCROLLBAR)) return;
    const auto area = nk_window_get_content_region(g_context);
    const float width = std::min(560.0f, std::max(1.0f, area.w - 32.0f));
    const float margin = std::max(0.0f, (area.w - width) * 0.5f);
    nk_layout_row_begin(g_context, NK_STATIC, std::max(1.0f, height - 24.0f), 3);
    nk_layout_row_push(g_context, margin);
    nk_spacing(g_context, 1);
    nk_layout_row_push(g_context, width);
    if (nk_group_begin(g_context, "startup-column", 0)) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_spacing(g_context, 1);
        nk_layout_row_dynamic(g_context, 48, 1);
        nk_label(g_context, "No project loaded", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(g_context, 28, 1);
        nk_label(g_context, "Create a project or open an existing chart to begin.", NK_TEXT_CENTERED);
        render_startup_buttons(action);
        if (!g_project_dialog_error.empty()) {
            nk_layout_row_dynamic(g_context, 38, 1);
            nk_label_wrap(g_context, g_project_dialog_error.c_str());
        }
        render_notification_landing_status(view, action);
        render_recent_project_list(view, action, std::min(360.0f, height - 280.0f));
        nk_group_end(g_context);
    }
    nk_layout_row_push(g_context, margin);
    nk_spacing(g_context, 1);
    nk_layout_row_end(g_context);
    nk_group_end(g_context);
}

static void cancel_new_project_wizard(WormUiWorkspaceAction& action) {
    if (g_new_project_wizard.audio && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action.kind = WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT;
    g_new_project_wizard.open = false;
}

static void confirm_new_project_wizard(WormUiWorkspaceAction& action) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_NONE) return;
    const auto& audio_path = g_new_project_wizard.audio_path;
    if (g_new_project_wizard.audio && audio_path.size() >= sizeof(action.project_path)) return;
    action.kind = WORM_UI_WORKSPACE_ACTION_NEW_PROJECT;
    action.new_project_layout_index = g_new_project_wizard.layout_index;
    if (g_new_project_wizard.audio)
        std::memcpy(action.project_path, audio_path.c_str(), audio_path.size() + 1);
    g_new_project_wizard.open = false;
}

static bool external_project_modal_active(const WormUiWorkspaceView& view) {
    return (g_help_dialog != HelpDialog::none) || view.close_confirmation.visible || view.chart_import.visible ||
           g_new_project_wizard.open ||
           worm_ui_nuklear_sound_import_active() ||
           worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open ||
           worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review.open ||
           (view.pending_bms_encoding_path && view.pending_bms_encoding_path[0]);
}

static bool project_modal_active(const WormUiWorkspaceView& view) {
    const auto& state = worm_ui_nuklear_compose_state();
    return external_project_modal_active(view) || state.dialogs.export_dialog_open ||
        state.layer_editor.layer_creation_context.has_value();
}

static void render_about_body(const WormUiWorkspaceView& view) {
    namespace chrome = wormedit::ui::chrome;
    const auto text = chrome::color(chrome::UiColor::text);
    const auto muted = chrome::color(chrome::UiColor::text_muted);
    chrome::dialog_text(g_about_dialog, g_context, "WormEdit " WORMEDIT_VERSION, text);
    char build[160]{};
    std::snprintf(build, sizeof(build), "Build %s", view.git_hash ? view.git_hash : "unknown");
    chrome::dialog_text(g_about_dialog, g_context, build, muted);
    chrome::dialog_text(g_about_dialog, g_context, "Created and directed by zardoru.", text);
    chrome::dialog_text(g_about_dialog, g_context,
        "Developed with heavy AI assistance from Claude and Codex. Most code, tests and documentation "
        "are written by AI agents, guided and reviewed by the author.", muted);
    chrome::dialog_text(g_about_dialog, g_context,
        "Public domain (The Unlicense). Dependencies retain their own licenses.", muted);
    nk_layout_row_dynamic(g_context, chrome::dialog_button_height, 1);
    if (nk_button_label(g_context, "github.com/zardoru/wormedit"))
        (void)SDL_OpenURL("https://github.com/zardoru/wormedit");
}

static void render_shortcut_sheet(const WormUiWorkspaceView& view) {
    namespace chrome = wormedit::ui::chrome;
    constexpr std::array rows{
        std::pair{"edit.tool_select", "Select tool"},
        std::pair{"edit.tool_draw", "Draw tool"},
        std::pair{"edit.tool_layer", "Layer brush"},
        std::pair{"view.open_layers", "Show Layers"},
        std::pair{"semantic_layer.create", "Create layer"},
        std::pair{"view.toggle_sounds", "Toggle Sounds"},
        std::pair{"navigation.go_to_sound", "Go to sound"},
        std::pair{"timing.toggle_lock", "Timing lock"},
        std::pair{"measures.split_at_cursor", "Split measure at cursor"},
        std::pair{"transport.loop_selection", "Loop selected range"}};
    const auto count = std::min(view.menu_command_count, std::size(view.menu_commands));
    const auto* end = view.menu_commands + count;
    for (const auto& [id, label] : rows) {
        const auto* command = std::find_if(view.menu_commands, end, [id](const auto& row) {
            return std::strcmp(row.id, id) == 0;
        });
        const char* shortcut = command == end ? "Unavailable" :
            (command->shortcut[0] ? command->shortcut : "Unbound");
        const float columns[]{0.58f, 0.42f};
        nk_layout_row(g_context, NK_DYNAMIC, wormedit::ui::metrics::menu_item_height, 2, columns);
        nk_label(g_context, label, NK_TEXT_LEFT);
        nk_label_colored(g_context, shortcut, NK_TEXT_RIGHT, chrome::color(chrome::UiColor::text_muted));
    }
    chrome::dialog_text(g_about_dialog, g_context, "Current bindings; change them in Shortcuts.",
        chrome::color(chrome::UiColor::text_muted));
    chrome::dialog_text(g_about_dialog, g_context,
        "Tab: next measure. Tab + Up/Down: measure navigation. Page Up/Down: one measure, "
        "at least 2 beats; Tab doubles it. Shift extends the range.", chrome::color(chrome::UiColor::text));
    chrome::dialog_text(g_about_dialog, g_context,
        "Lane keys: move selected notes/BGM. Shift moves one and deselects. "
        "Draw: press/release for a tap; move the cursor while held for a hold.", chrome::color(chrome::UiColor::text));
}

static void render_about(const WormUiWorkspaceView& view) {
    if (g_help_dialog == HelpDialog::none) return;
    namespace chrome = wormedit::ui::chrome;
    const bool shortcuts = g_help_dialog == HelpDialog::shortcuts;
    const auto* title = shortcuts ? "Shortcut cheat sheet" : "About WormEdit";
    if (chrome::dialog_begin(g_about_dialog, g_context, title, title, {.width = 560.0f})) {
        if (shortcuts) render_shortcut_sheet(view);
        else render_about_body(view);
        chrome::dialog_footer_begin(g_about_dialog, g_context, 1);
        chrome::dialog_button(g_about_dialog, g_context, "Close", chrome::DialogButtonRole::primary);
    }
    const auto result = chrome::dialog_end(g_about_dialog, g_context);
    if (result == chrome::DialogResult::primary || result == chrome::DialogResult::cancel)
        g_help_dialog = HelpDialog::none;
}

static nk_flags workspace_window_flags(const WormUiWorkspaceView& view) {
    const auto flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND;
    return flags | ((project_modal_active(view) || command_menu_owns_input()) ? NK_WINDOW_NO_INPUT : 0);
}

static wormedit::ui::chrome::Dialog g_project_modal_dialog;

static void render_new_project_wizard(WormUiWorkspaceAction& action, int width, int height) {
    if (!g_new_project_wizard.open) return;
    namespace chrome = wormedit::ui::chrome;
    constexpr std::array<const char*, 8> layout_labels{
        "4K", "5K", "6K", "7K", "8K", "9K", "10K", "14K"};
    auto& dialog = g_project_modal_dialog;
    const bool audio_path_too_long = g_new_project_wizard.audio &&
                                     g_new_project_wizard.audio_path.size() >=
                                         sizeof(action.project_path);
    if (chrome::dialog_begin(dialog, g_context, "Create project",
            g_new_project_wizard.audio ? "Import audio as a new project?" : "Create a new project",
            {.modal = true, .width = 500.0f,
             .area = nk_vec2(static_cast<float>(width), static_cast<float>(height))})) {
        if (g_new_project_wizard.audio)
            chrome::dialog_text(dialog, g_context, g_new_project_wizard.audio_path.c_str(),
                                chrome::color(chrome::UiColor::text_muted));
        nk_layout_row_dynamic(g_context, 15, 1);
        nk_label_colored(g_context, "Chart layout", NK_TEXT_LEFT, chrome::color(chrome::UiColor::text_muted));
        nk_layout_row_dynamic(g_context, wormedit::ui::metrics::control_default, 1);
        const auto selected = chrome::safe_combo(g_context, layout_labels.data(),
                                       static_cast<int>(layout_labels.size()),
                                       g_new_project_wizard.layout_index, 25,
                                       nk_vec2(180.0f, 225.0f));
        if (selected >= 0 && selected < static_cast<int>(layout_labels.size()))
            g_new_project_wizard.layout_index = static_cast<std::uint8_t>(selected);
        if (audio_path_too_long)
            chrome::dialog_text(dialog, g_context, "Audio path is too long to import.",
                                chrome::color(chrome::UiColor::conflict));
        chrome::dialog_footer_begin(dialog, g_context, 2, false, 120.0f);
        if (chrome::dialog_button(dialog, g_context, "Cancel", chrome::DialogButtonRole::secondary))
            cancel_new_project_wizard(action);
        if (chrome::dialog_button(dialog, g_context, "Create project", chrome::DialogButtonRole::primary,
                                  !audio_path_too_long))
            confirm_new_project_wizard(action);
    }
    // Escape cancels like the Cancel button (the dialog may already have closed itself).
    if (chrome::dialog_end(dialog, g_context) == chrome::DialogResult::cancel && g_new_project_wizard.open)
        cancel_new_project_wizard(action);
}

static void cancel_bms_encoding_dialog(WormUiWorkspaceAction& action) {
    if (action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action.kind = WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING;
}

static void render_bms_encoding_dialog(const WormUiWorkspaceView& view,
                                       WormUiWorkspaceAction& action, int width, int height) {
    const std::string path = view.pending_bms_encoding_path ? view.pending_bms_encoding_path : "";
    if (path.empty()) {
        g_bms_encoding_path_seen.clear();
        return;
    }
    g_new_project_wizard.open = false;
    if (path != g_bms_encoding_path_seen) {
        g_bms_encoding_path_seen = path;
        g_bms_encoding_choice = 0;
    }
    constexpr std::array<const char*, 5> all_labels{
        "UTF-8", "CP932 (Japanese)", "EUC-KR (Korean)", "CP949 (Korean)",
        "Windows-1252 (Western)"};
    constexpr std::array<wormedit::text_encoding_t, 5> all_encodings{
        wormedit::text_encoding_t::utf8, wormedit::text_encoding_t::cp932,
        wormedit::text_encoding_t::euc_kr, wormedit::text_encoding_t::cp949,
        wormedit::text_encoding_t::windows_1252};
    std::array<const char*, 5> labels{};
    std::array<std::uint8_t, 5> indexes{};
    int count = 0;
    for (std::size_t i = 0; i < all_encodings.size(); ++i) {
        if (!wormedit::is_bms_text_encoding_supported(all_encodings[i])) continue;
        labels[static_cast<std::size_t>(count)] = all_labels[i];
        indexes[static_cast<std::size_t>(count)] = static_cast<std::uint8_t>(i + 1);
        ++count;
    }
    if (g_bms_encoding_choice >= count) g_bms_encoding_choice = 0;
    namespace chrome = wormedit::ui::chrome;
    auto& dialog = g_project_modal_dialog;
    if (chrome::dialog_begin(dialog, g_context, "BMS text encoding", "BMS text encoding",
            {.modal = true, .width = 520.0f,
             .area = nk_vec2(static_cast<float>(width), static_cast<float>(height))})) {
        chrome::dialog_text(dialog, g_context, path.c_str(), chrome::color(chrome::UiColor::text_muted));
        chrome::dialog_text(dialog, g_context, "This chart contains non-ASCII text without a UTF-8 BOM.",
                            chrome::color(chrome::UiColor::text));
        nk_layout_row_dynamic(g_context, 15, 1);
        nk_label_colored(g_context, "Encoding", NK_TEXT_LEFT, chrome::color(chrome::UiColor::text_muted));
        nk_layout_row_dynamic(g_context, wormedit::ui::metrics::control_default, 1);
        g_bms_encoding_choice = chrome::safe_combo(g_context, labels.data(), count,
                                          g_bms_encoding_choice, 25, nk_vec2(250.0f, 180.0f));
        chrome::dialog_footer_begin(dialog, g_context, 2, false, 120.0f);
        if (chrome::dialog_button(dialog, g_context, "Cancel", chrome::DialogButtonRole::secondary))
            cancel_bms_encoding_dialog(action);
        if (chrome::dialog_button(dialog, g_context, "Open chart", chrome::DialogButtonRole::primary) &&
            action.kind == WORM_UI_WORKSPACE_ACTION_NONE &&
            path.size() < sizeof(action.project_path)) {
            action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
            action.bms_encoding_index = indexes[static_cast<std::size_t>(g_bms_encoding_choice)];
            std::memcpy(action.project_path, path.c_str(), path.size() + 1);
        }
    }
    if (chrome::dialog_end(dialog, g_context) == chrome::DialogResult::cancel)
        cancel_bms_encoding_dialog(action);
}

static void render_project_modals(const WorkspaceRenderContext& frame) {
    if (!frame.view.pending_bms_encoding_path || !frame.view.pending_bms_encoding_path[0])
        render_new_project_wizard(frame.action, frame.width, frame.height);
    render_bms_encoding_dialog(frame.view, frame.action, frame.width, frame.height);
}

static void render_empty_project_window(const WorkspaceRenderContext& frame) {
    if (nk_begin(g_context, "wormedit-workspace",
                 nk_rect(0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)),
                 workspace_window_flags(frame.view))) {
        worm_ui_nuklear_retire_setup_popups(g_context);
        wormedit::ui::chrome::context_menu_retire(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu, g_context);
        worm_ui_nuklear_dismiss_layer_popup(g_context);
        dismiss_compose_note_popup(g_context);
        render_title_bar(&frame.view, &frame.action);
        const float recovery_height = render_recovery_panel(frame.view, frame.action);
        const float available_height = std::max(100.0f,
                                                static_cast<float>(frame.height) - 28.0f - worm_ui_frontend_title_bar_height() -
                                                    recovery_height);
        render_empty_project_content(frame.view, frame.action, available_height);

    }
    nk_end(g_context);
    render_project_modals(frame);
}

struct GpuWorkSection {
    const char* heading;
    uint32_t available;
    WormUiFrameGpuWork latest;
    const WormUiFrameGpuWorkStats* stats;
};

static void render_gpu_work_section(const WormUiFrameTimingView& timer,
                                    const GpuWorkSection& section) {
    nk_layout_row_dynamic(g_context, 22, 1);
    nk_label(g_context, section.heading, NK_TEXT_LEFT);
    const uint64_t latest[WORM_UI_FRAME_GPU_WORK_COUNT]{section.latest.vertices,
        section.latest.indices, section.latest.draw_calls, section.latest.clip_rect_changes,
        section.latest.texture_switches};
    for (std::size_t metric = 0; metric < WORM_UI_FRAME_GPU_WORK_COUNT; ++metric) {
        const auto summary = section.stats[metric];
        nk_layout_row_dynamic(g_context, 22, 1);
        if (!section.available || summary.count == 0)
            nk_labelf(g_context, NK_TEXT_LEFT, "  %s: N/A", timer.gpu_work_names[metric]);
        else
            nk_labelf(g_context, NK_TEXT_LEFT, "  %s: last %llu, avg %.0f, p99 %.0f",
                timer.gpu_work_names[metric], static_cast<unsigned long long>(latest[metric]),
                summary.average, summary.p99);
    }
}

// The panel draws in its own pass, so its GPU work is reported on its own and
// never folded into the application's numbers.
static void render_gpu_work_summary(const WormUiFrameTimingView& timer) {
    render_gpu_work_section(timer, {"GPU work - application (excludes this panel):",
        timer.gpu_work_available, timer.gpu_work_latest, timer.gpu_work});
    render_gpu_work_section(timer, {"GPU work - this panel only:",
        timer.diagnostic_gpu_work_available, timer.diagnostic_gpu_work_latest,
        timer.diagnostic_gpu_work});
}

// nk_chart_begin keeps the themed background and plot bounds; one polyline
// replaces NK_CHART_LINES' per-point line segments and filled point markers.
static void render_frame_time_graph(const WormUiFrameTimingView& timer) {
    nk_layout_row_dynamic(g_context, 90, 1);
    const auto count = std::min<std::size_t>(timer.count, WORM_UI_FRAME_TIMING_CAPACITY);
    const auto maximum_ms = static_cast<float>(count == 0 ? 1.0 : std::max(1.0,
        *std::max_element(timer.frame_ms, timer.frame_ms + count)));
    if (!nk_chart_begin(g_context, NK_CHART_LINES, static_cast<int>(count), 0.0f, maximum_ms))
        return;
    const auto& chart = g_context->current->layout->chart;
    const float step = count == 0 ? 0.0f : chart.w / static_cast<float>(count);
    std::array<float, 2 * WORM_UI_FRAME_TIMING_CAPACITY> points{};
    for (std::size_t i = 0; i < count; ++i) {
        const float ratio = static_cast<float>(timer.frame_ms[i]) / maximum_ms;
        points[2 * i] = chart.x + step * static_cast<float>(i);
        points[2 * i + 1] = chart.y + chart.h - ratio * chart.h;
    }
    if (count >= 2)
        nk_stroke_polyline(nk_window_get_canvas(g_context), points.data(), static_cast<int>(count),
                           1.0f, chart.slots[0].color);
    nk_chart_end(g_context);
}

static void render_frame_timing_overlay(int width, int height) {
    if (!g_frame_timing_visible) return;
    const auto timer = worm_ui_frame_timing_view();
    const float panel_width = std::min(480.0f, static_cast<float>(width));
    const float panel_height = std::min(720.0f, static_cast<float>(height) - 42.0f);
    // Drawn in a diagnostic pass so its GPU work is reported apart from the application's.
    const wormedit::ui::nuklear::diagnostic_window_scope_t diagnostic{g_context};
    if (nk_begin(g_context, "Frame timing",
                 nk_rect(static_cast<float>(width) - panel_width, 42, panel_width, panel_height),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE)) {
        const auto frame = timer.frame;
        nk_layout_row_dynamic(g_context, 22, 1);
        if (frame.cpu_count == 0)
            nk_labelf(g_context, NK_TEXT_LEFT, "Last %zu frames: avg %.3f ms, p99 %.3f ms",
                      frame.count, frame.average_ms, frame.p99_ms);
        else nk_labelf(g_context, NK_TEXT_LEFT, "Last %zu frames: avg %.3f ms, p99 %.3f ms (cpu p99 %.3f)",
                       frame.count, frame.average_ms, frame.p99_ms, frame.cpu_p99_ms);
        render_frame_time_graph(timer);
        nk_layout_row_dynamic(g_context, 22, 1);
        if (timer.heap_allocations_available)
            nk_labelf(g_context, NK_TEXT_LEFT,
                "Projection new: %llu / %llu bytes (avg %.1f, p99 %.0f)",
                static_cast<unsigned long long>(timer.projection_heap_allocations_latest),
                static_cast<unsigned long long>(timer.projection_heap_bytes_latest),
                timer.projection_heap_allocations.average,
                timer.projection_heap_allocations.p99);
        else nk_label(g_context, "Projection new: unavailable (allocation hook inactive)", NK_TEXT_LEFT);
        render_gpu_work_summary(timer);
        for (std::size_t i = 0; i < WORM_UI_FRAME_TIMING_PHASE_COUNT; ++i) {
            const auto summary = timer.phases[i];
            nk_layout_row_dynamic(g_context, 22, 1);
            if (summary.count == 0) nk_labelf(g_context, NK_TEXT_LEFT, "%s: N/A", timer.phase_names[i]);
            else if (summary.cpu_count == 0)
                nk_labelf(g_context, NK_TEXT_LEFT, "%s: avg %.3f ms, p99 %.3f ms",
                          timer.phase_names[i], summary.average_ms, summary.p99_ms);
            else nk_labelf(g_context, NK_TEXT_LEFT, "%s: avg %.3f ms, p99 %.3f ms (cpu p99 %.3f)",
                           timer.phase_names[i], summary.average_ms, summary.p99_ms, summary.cpu_p99_ms);
        }
    }
    nk_end(g_context);
}

static void render_workspace_main_window(const WorkspaceRenderContext& frame) {
    const auto& view = frame.view;
    auto& action = frame.action;
    if (!view.project_loaded) {
        render_empty_project_window(frame);
        return;
    }
    if (nk_begin(g_context, "wormedit-workspace",
                 nk_rect(0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)),
                 workspace_window_flags(frame.view))) {
        worm_ui_nuklear_retire_setup_popups(g_context);
        wormedit::ui::chrome::context_menu_retire(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu, g_context);
        worm_ui_nuklear_dismiss_layer_popup(g_context);
        dismiss_compose_note_popup(g_context);
        render_title_bar(&view, &action);
        render_workspace_tabs(&view, &action);
        render_transport_seek_control(view, action);
        const float recovery_height = render_recovery_above_content(view, action);
        float content_height = frame.height > 300
                                  ? static_cast<float>(frame.height) - 104.0f - worm_ui_frontend_title_bar_height() -
                                        (view.compose.visible && view.compose.bgm_bar_reserved
                                             ? WORM_UI_BGM_BAR_CHROME_HEIGHT : 0.0f) -
                                        recovery_height
                                  : 80.0f;
        if (content_height < 80.0f) content_height = 80.0f;
        nk_layout_row_dynamic(g_context, content_height, 1);
        render_workspace_content(view, action);
        render_status_bar(&view, &action);

    }
    nk_end(g_context);
    render_project_modals(frame);
}

static void render_tap_timing_tool(const WorkspaceRenderContext& frame) {
    auto tap = frame.view.tap_timing;
    const auto& compose = worm_ui_nuklear_compose_state();
    tap.input_blocked = frame.view.options.visible || g_notification_modal ||
        command_menu_owns_input() || g_new_project_wizard.open || g_help_dialog != HelpDialog::none ||
        compose_popup_owns_input() || compose.inline_timing.inline_timing_open || compose.inline_offset.inline_offset_open ||
        compose.dialogs.go_to_measure_open || compose.bookmark_ui.bookmark_rename.active || compose.bookmark_ui.bookmark_rename.release_pending ||
        notification_document_gesture_active();
    for (std::size_t index = 0; index < g_shortcut_event_count; ++index) {
        const auto& key = g_shortcut_events[index];
        if (key.hid_usage == SDL_SCANCODE_ESCAPE && key.modifiers == 0) tap.close_key_pressed = true;
    }
    worm_ui_nuklear_render_tap_timing_panel(g_context, tap, frame.action.tap_timing);
    if (!frame.action.tap_timing.close) return;
    std::size_t count = 0;
    for (std::size_t index = 0; index < g_shortcut_event_count; ++index) {
        const auto& key = g_shortcut_events[index];
        if (key.hid_usage == SDL_SCANCODE_ESCAPE && key.modifiers == 0) continue;
        g_shortcut_events[count++] = key;
    }
    g_shortcut_event_count = count;
}

static void render_auxiliary_windows(const WorkspaceRenderContext& frame) {
    worm_ui_nuklear_render_setup_dialogs(g_context, frame.view.setup, frame.action);
    render_close_confirmation(frame.view.close_confirmation, frame.action, frame.width, frame.height);
    if (frame.view.project_loaded &&
        worm_ui_nuklear_render_chart_import_dialog(g_context, frame.view.chart_import, frame.action,
                                                   frame.width, frame.height))
        start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_IMPORT_CHART_FILE);
    if (!project_modal_active(frame.view))
        worm_ui_nuklear_render_options_window(g_context, &frame.view.options, &frame.action.options);
    if (project_modal_active(frame.view)) {
        worm_ui_nuklear_cancel_options_drag();
        return;
    }
    if (!frame.view.project_loaded) {
        worm_ui_nuklear_render_external_player_panel(g_context, &frame.view.external_player,
                                                     &frame.action.external_player);
        render_tap_timing_tool(frame);
        return;
    }
    render_frame_timing_overlay(frame.width, frame.height);
    render_tap_timing_tool(frame);
    worm_ui_nuklear_render_timing_settings_panel(g_context, frame.view, frame.action);
    worm_ui_nuklear_render_external_player_panel(g_context, &frame.view.external_player,
                                                 &frame.action.external_player);
    worm_ui_nuklear_render_player_preview(g_context, g_renderer, &frame.view.player_preview,
                                          &frame.action, frame.width, frame.height);
}

static void remember_context_menu_scope(const WormUiWorkspaceView* view) {
    namespace chrome = wormedit::ui::chrome;
    const auto project = view ? view->project_id : 0;
    const auto chart = view ? view->compose.chart_id : 0;
    const auto tab = view ? view->active_tab : WORM_UI_WORKSPACE_TAB_METADATA;
    const bool changed = project != g_context_project || chart != g_context_chart || tab != g_context_tab;
    const bool content = view && ((tab == WORM_UI_WORKSPACE_TAB_SETUP && view->setup.visible) ||
        (tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view->compose.visible && !view->compose.input_occluded));
    g_context_invocation_allowed = content && view->project_loaded && g_window_focused &&
        !view->options.visible && !project_modal_active(*view) &&
        !g_menu_popup_open && !g_menu_popup_release_pending;
    if (changed || !g_context_invocation_allowed) chrome::context_menu_close_active_keyboard();
    if (changed || !view || !view->audio_slices.visible) {
        g_audio.sound_list_focused = false;
        g_audio.sound_list_valid = false;
        g_audio.import_drop_valid = false;
        g_audio.list_move_pending = 0;
        g_audio.list_audition_pending = false;
        g_audio.waveform_pan_active = false;
    }
    g_context_project = project;
    g_context_chart = chart;
    g_context_revision = view ? view->document_revision : 0;
    g_project_export_available = view && view->project_loaded && !view->options.visible &&
        !project_modal_active(*view);
    g_context_tab = tab;
}

static void remember_notification_frame(const WormUiWorkspaceView* view) {
    g_notification_view = view ? view->notifications : WormUiNotificationView{};
    g_notification_project_modal = view && project_modal_active(*view);
    g_notification_modal = g_notification_project_modal || (view && view->options.visible);
    g_compose_escape_has_selection = view && view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE &&
        (view->compose.selected_note_count != 0 || view->compose.selected_object_count != 0 ||
         view->compose.selected_bgm_event_count != 0 || view->compose.area_range_available);
}

static void render_notification_history(const WormUiNotificationView& view,
    WormUiWorkspaceAction& action, int width, int height, bool project_modal) {
    if (g_notification_history_close_pending) {
        action.notifications = {WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY, 0};
        g_notification_history_close_pending = false;
        close_notification_history_windows();
        return;
    }
    if (project_modal || !view.history.visible) return;
    const bool locked = notification_history_locked();
    if (locked && !g_notification_context_initialized) return;
    auto* context = locked ? &g_notification_context : g_context;
    if (locked) context->style = g_context->style;
    wormedit::ui::nuklear::render_notification_panel(context, view.history,
        g_notification_history_state, action.notifications, width, height, !locked);
    if (action.notifications.kind == WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY)
        close_notification_history_windows();
}

static void render_notification_frame(const WormUiNotificationView& view,
    WormUiWorkspaceAction& action, int width, int height, bool project_modal) {
    render_notification_history(view, action, width, height, project_modal);
    const bool valid_count = view.toast_count <= WORM_UI_NOTIFICATION_TOAST_CAPACITY;
    const auto visible = project_modal || !valid_count ? WormUiNotificationView{} : view;
    wormedit::ui::nuklear::render_notification_overlay(g_context,
        g_notification_context_initialized ? &g_notification_context : nullptr,
        visible, g_notification_state, width, height);
    const auto pending = wormedit::ui::nuklear::notification_take_action(g_notification_state);
    if (pending.kind != WORM_UI_NOTIFICATION_ACTION_NONE &&
        action.notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE) action.notifications = pending;
}

static void retire_unavailable_layer_actions(const WormUiWorkspaceView& view) {
    if (!g_window_focused || !view.project_loaded || view.options.visible || external_project_modal_active(view))
        worm_ui_nuklear_compose_state().dialogs.export_dialog_open = false;
    if (view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && g_window_focused &&
        view.project_loaded && view.compose.visible && !view.compose.input_occluded &&
        !view.options.visible && !external_project_modal_active(view)) return;
    auto& state = worm_ui_nuklear_compose_state();
    state.layer_editor.layer_creation_request.reset();
    state.layer_editor.layer_creation_context.reset();
    wormedit::ui::chrome::context_menu_abandon(state.snap_grid.snap_grid_menu);
    wormedit::ui::chrome::context_menu_abandon(state.layer_editor.layers_menu);
    abandon_compose_quantize_popup();
    state.bga_interaction.bga_stack_open = false;
    state.layer_editor.layers_menu_input_consumed = false;
    state.layer_editor.semantic_layer_name_editor_visible = false;
    state.layer_editor.semantic_layer_name_focus_pending = false;
    state.layer_editor.semantic_layer_color_editor_visible = false;
}

static void consume_pending_save_request(const WormUiWorkspaceView* view, WormUiWorkspaceAction& action) {
    if (g_save_project_requested) {
        const bool same_project = view && view->project_id == g_save_project_request_id;
        const bool eligible = same_project && view->project_loaded && action.kind == WORM_UI_WORKSPACE_ACTION_NONE &&
                              !project_modal_active(*view) && !project_dialog_active() &&
                              !g_new_project_wizard.open;
        g_save_project_requested = false;
        g_save_project_request_id = 0;
        if (eligible) {
            if (view->project_has_native_path)
                action.kind = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT;
            else
                start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
        }
    }
}

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
    struct RequestFrame {
        ~RequestFrame() { wormedit::ui::chrome::context_menu_finish_frame(); }
    } request_frame;
    MenuPointerCapture menu_pointer_capture{g_context};
    wormedit::ui::nuklear::tooltip::set_viewport(nk_rect(0.0F, 0.0F, 0.0F, 0.0F));
    reset_compose_canvas_viewport_for_testing();
    worm_ui_nuklear_sync_setup_popups(g_context, view ? &view->setup : nullptr);
    remember_notification_frame(view);
    remember_context_menu_scope(view);
    worm_ui_nuklear_begin_image_quad_frame();
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_NONE;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    consume_project_file_dialog(action);
    consume_pending_save_request(view, action);
    if (!g_context || (!g_window && !g_headless) || !view) {
        worm_ui_nuklear_compose_state().layer_editor.layer_creation_request.reset();
        worm_ui_nuklear_compose_discard_frame_input();
        expose_shortcut_input(&action);
        return action;
    }
    (void)wormedit::ui::chrome::popup_update_release_pending(
        g_context, worm_ui_nuklear_compose_state().dialogs.compose_dialog_release_pending);
    service_native_menu(*view, action);
    retire_unavailable_layer_actions(*view);
    g_tap_timing_visible = view->tap_timing.visible && !project_modal_active(*view);
    sync_pending_audio_project(*view);

    const auto* validation_error = workspace_validation_error(*view);
    prepare_command_menu(view, !validation_error && g_window_focused &&
        !view->options.visible && !project_modal_active(*view) && !project_dialog_active() &&
        !g_notification_modal && !command_menu_dialog_blocked() &&
        !wormedit::ui::chrome::context_menu_keyboard_owned());
    // Existing command owners see the closing frame before release guards retire.
    worm_ui_nuklear_update_compose_release_latches(worm_ui_nuklear_compose_state(), g_context);

    int width = 0;
    int height = 0;
    if (!frontend_window_size(&width, &height)) {
        worm_ui_nuklear_compose_discard_frame_input();
        expose_shortcut_input(&action);
        return action;
    }
    wormedit::ui::nuklear::tooltip::set_viewport(
        nk_rect(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)));

    if (wormedit::ui::gallery::requested()) {
        static wormedit::ui::gallery::state_t gallery_state;
        wormedit::ui::gallery::render(g_context, gallery_state, width, height);
        return action;
    }

    if (validation_error) {
        worm_ui_nuklear_compose_state().layer_editor.layer_creation_request.reset();
        wormedit::ui::chrome::context_menu_abandon(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu);
        render_workspace_error(&action, width, height, validation_error, view->close_confirmation.visible);
        render_close_confirmation(view->close_confirmation, action, width, height);
        worm_ui_nuklear_render_keysound_group_review(g_context, view->compose, action,
            worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review,
            worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_menu.release_pending, width, height);
        render_about(*view);
        render_notification_frame(view->notifications, action, width, height, project_modal_active(*view));
        return action;
    }

    const std::string window_title = std::string{view->project_title} +
                                     (view->dirty ? " *" : "") + " — wormedit";
    if (g_window && window_title != SDL_GetWindowTitle(g_window))
        SDL_SetWindowTitle(g_window, window_title.c_str());

    if (!view->project_loaded) {
        worm_ui_nuklear_compose_discard_frame_input();
        worm_ui_nuklear_audio_state().sound_list_valid = false;
    worm_ui_nuklear_compose_state().sounds_dock.sound_list_valid = false;
    render_workspace_main_window(WorkspaceRenderContext{*view, action, width, height});
        render_auxiliary_windows(WorkspaceRenderContext{*view, action, width, height});
        render_command_menu(g_context, *view, action, width, height);
        render_about(*view);
        render_notification_frame(view->notifications, action, width, height, project_modal_active(*view));
        expose_shortcut_input(&action);
        g_audio.offset_escape_pending = false;
        return action;
    }

    const bool compose_tab_active = view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    g_compose_area_shortcuts_active = compose_tab_active && view->compose.visible &&
        !project_modal_active(*view);
    worm_ui_nuklear_prepare_compose_panel(&view->compose,
                                          compose_tab_active && !project_modal_active(*view), g_window_focused,
                                          &action);
    clear_inactive_stem_resize_cursor(*view, g_window_focused);

    worm_ui_nuklear_audio_state().sound_list_valid = false;
    worm_ui_nuklear_compose_state().sounds_dock.sound_list_valid = false;
    render_workspace_main_window(WorkspaceRenderContext{*view, action, width, height});
    render_auxiliary_windows(WorkspaceRenderContext{*view, action, width, height});
    render_command_menu(g_context, *view, action, width, height);
    if (action.sound_import_pick_requested && !g_headless)
        worm_ui_nuklear_sound_import_pick(g_window, *view, action.sound_import_kind);
    consume_layer_creation_request(*view, action);
    worm_ui_nuklear_render_compose_dialogs(g_context, view->compose, action,
        worm_ui_nuklear_compose_state());
    worm_ui_nuklear_consume_sound_picker(*view, action);
    worm_ui_nuklear_render_pending_sound_import(g_context, *view, action, width, height);
    worm_ui_nuklear_render_sound_slot_batch(g_context,
        worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch, view->compose,
        action, width, height);
    worm_ui_nuklear_render_keysound_group_review(g_context, view->compose, action,
        worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review,
            worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_menu.release_pending, width, height);
    render_about(*view);
    render_notification_frame(view->notifications, action, width, height, project_modal_active(*view));
    const bool compose_lane_focused =
        !worm_ui_nuklear_compose_state().inline_offset.bgm_column_count_edit.input_owned &&
        worm_ui_nuklear_compose_lane_focused(&view->compose, compose_tab_active, g_window_focused);
    if (compose_tab_active) consume_compose_text_hotkeys(action);
    else g_compose_text_hotkey_count = 0;
    expose_shortcut_input(&action, compose_lane_focused,
                          compose_lane_focused ? view->compose.cursor->lane_id : 0);
    g_audio.offset_escape_pending = false;
    return action;
}

void worm_ui_frontend_resource_placed(uint64_t placed_resource_id, uint64_t next_resource_id) {
    worm_ui_nuklear_advance_resource_picker(placed_resource_id, next_resource_id);
}

size_t worm_ui_frontend_copy_drawn_text_for_testing(char* output, size_t capacity) {
    size_t length = 0;
    if (!g_headless || !g_context) return 0;
    const struct nk_command* command{};
    nk_foreach(command, g_context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const struct nk_command_text*>(command);
        const auto count = static_cast<size_t>(text->length);
        if (output && length < capacity) {
            const auto copied = std::min(count, capacity - length);
            std::memcpy(output + length, text->string, copied);
        }
        length += count;
        if (output && length < capacity) output[length] = '\n';
        ++length;
    }
    return length;
}

size_t worm_ui_frontend_copy_drawn_text_bounds_for_testing(WormUiTestingDrawnText* output,
                                                            size_t capacity) {
    size_t count = 0;
    if (!g_headless || !g_context) return count;
    const struct nk_command* command{};
    nk_foreach(command, g_context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const struct nk_command_text*>(command);
        if (output && count < capacity) {
            auto& target = output[count];
            target.x = text->x;
            target.y = text->y;
            target.width = text->w;
            target.height = text->h;
            const auto length = std::min(static_cast<size_t>(text->length), sizeof(target.text) - 1);
            std::memcpy(target.text, text->string, length);
            target.text[length] = '\0';
        }
        ++count;
    }
    return count;
}

bool worm_ui_frontend_compose_viewport_for_testing(WormUiTestingRect* output) {
    if (!g_headless || !output) return false;
    const auto viewport = compose_canvas_viewport_for_testing();
    if (!viewport) return false;
    *output = *viewport;
    return true;
}

bool worm_ui_frontend_window_bounds_for_testing(const char* name, WormUiTestingRect* output) {
    if (!g_headless || !g_context || !name || !output) return false;
    const auto* window = nk_window_find(g_context, name);
    if (!window) return false;
    *output = {window->bounds.x, window->bounds.y, window->bounds.w, window->bounds.h};
    return true;
}

bool worm_ui_frontend_hide_notification_history_for_testing(void) {
    if (!g_headless || !g_context || !nk_window_find(g_context, "Notifications")) return false;
    nk_window_show(g_context, "Notifications", NK_HIDDEN);
    if (g_notification_context_initialized &&
        nk_window_find(&g_notification_context, "Notifications"))
        nk_window_show(&g_notification_context, "Notifications", NK_HIDDEN);
    return true;
}

void worm_ui_frontend_render(int window_width, int window_height) {
    apply_stem_resize_cursor(worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested);
    if (window_width <= 0 || window_height <= 0) {
        (void)frontend_window_size(&window_width, &window_height);
    }
    if (g_headless && g_context) {
        nk_clear(g_context);
        if (g_notification_context_initialized) nk_clear(&g_notification_context);
        return;
    }
    if (g_context && g_renderer && window_width > 0 && window_height > 0) {
        const RenderSubphaseScope submit_phase{WORM_UI_FRAME_RENDER_SUBMIT};
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
        g_editor_font.refresh(g_context,
            g_notification_context_initialized ? &g_notification_context : nullptr);
        wormedit::ui::nuklear::render_sdl_backend(g_context, NK_ANTI_ALIASING_ON,
            g_notification_context_initialized ? &g_notification_context : nullptr);
    }
}

void worm_ui_frontend_set_snap_colored_grid(bool enabled) {
    wormedit::ui::theme::set_snap_colored_grid(enabled);
}

void worm_ui_frontend_set_snap_grid_opacity(uint32_t percent) {
    wormedit::ui::theme::set_snap_grid_opacity(percent);
}

void worm_ui_frontend_set_field_waveform_opacity(uint32_t percent) {
    wormedit::ui::theme::set_field_waveform_opacity(percent);
}

static auto& configured_field_lanes() {
    static std::vector<wormedit::ui::theme::field_lane_t> lanes;
    return lanes;
}

bool worm_ui_frontend_configure_field_scheme(const char* requested, const char* fallback,
    const WormUiComposeLaneView* lanes, size_t count) {
    auto& configured = configured_field_lanes();
    configured.clear();
    if (lanes) {
        configured.reserve(count);
        for (const auto& lane : std::span(lanes, count))
            configured.push_back({lane.display_order, lane.stage, lane.key_color, lane.role, lane.lane_id});
    }
    return wormedit::ui::theme::select_field_scheme_for_layout(requested ? requested : "",
        fallback ? fallback : "", configured);
}

int worm_ui_frontend_field_scratch_side(uint8_t stage) {
    const auto side = wormedit::ui::theme::field_scratch_side(stage, configured_field_lanes());
    return side ? static_cast<int>(*side) : -1;
}

bool worm_ui_frontend_select_field_scheme(const char* id) {
    return wormedit::ui::theme::select_field_scheme(id ? id : "");
}

bool worm_ui_frontend_open_command_menu(void) {
    if (!g_context || !g_window_focused || nuklear_text_entry_active() ||
        g_ime_composition_active || g_notification_modal || project_dialog_active() ||
        command_menu_dialog_blocked() ||
        wormedit::ui::chrome::context_menu_keyboard_owned()) return false;
    return open_command_menu();
}
