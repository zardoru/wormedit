#include "ui/nuklear/external_player_panel.h"
#include "ui/nuklear/options_window.h"
#include "ui/nuklear/tap_timing_panel.h"

#include "ui/frontend.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_input_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/content_margin.h"
#include "ui/nuklear/image_quad.h"
#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/metadata_state.h"
#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/timing_panel.h"
#include "ui/nuklear/timing_state.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/notification_toasts.h"
#include "ui/nuklear/notification_panel.h"

#include "ui/nuklear/sdl_backend.h"

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
#include <memory>
#include <mutex>
#include <new>
#include <stddef.h>
#include <string>
#include <utility>

using wormedit::ui::lucide::Glyph;

static bool g_frame_timing_visible;
static bool g_compose_area_shortcuts_active;
static SDL_Window* g_window;
static SDL_Renderer* g_renderer;
static struct nk_context* g_context;
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

static void render_notification_frame(const WormUiNotificationView& view,
    WormUiWorkspaceAction& action, int width, int height, bool project_modal);

static std::array<WormUiShortcutEvent, WORM_UI_SHORTCUT_EVENT_CAPACITY> g_shortcut_events{};
static size_t g_shortcut_event_count{};
static bool g_tap_timing_visible{};
static bool g_tap_key_captured{};
static bool g_tap_space_captured{};
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
};

static ProjectDialogState g_project_dialog;
static std::string g_project_dialog_error;

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
    return g_project_dialog.active;
}

static void SDLCALL project_file_dialog_callback(void* userdata,
                                                 const char* const* files, int) {
    std::unique_ptr<std::uint64_t> generation{static_cast<std::uint64_t*>(userdata)};
    if (!generation) return;
    std::string path;
    std::string error;
    if (!files) {
        const auto* sdl_error = SDL_GetError();
        error = sdl_error && sdl_error[0] ? sdl_error : "File picker failed.";
    } else if (files[0]) {
        std::size_t length{};
        while (length < WORM_UI_PROJECT_PATH_CAPACITY && files[0][length]) ++length;
        if (length == WORM_UI_PROJECT_PATH_CAPACITY)
            error = "Selected project path is too long.";
        else
            path.assign(files[0], length);
    }
    std::lock_guard lock{g_project_dialog.mutex};
    if (!g_project_dialog.active || g_project_dialog.generation != *generation) return;
    g_project_dialog.path = std::move(path);
    g_project_dialog.error = std::move(error);
    g_project_dialog.ready = true;
}

static void start_project_file_dialog(WormUiWorkspaceActionKind kind) {
    std::uint64_t generation{};
    {
        std::lock_guard lock{g_project_dialog.mutex};
        if (g_project_dialog.active) return;
        generation = ++g_project_dialog.generation;
        g_project_dialog.active = true;
        g_project_dialog.ready = false;
        g_project_dialog.kind = kind;
    }
    auto token = std::unique_ptr<std::uint64_t>{new (std::nothrow) std::uint64_t{generation}};
    if (!token) {
        reset_project_dialog();
        g_project_dialog_error = "File picker could not allocate callback state.";
        return;
    }
    g_project_dialog_error.clear();
    static constexpr SDL_DialogFileFilter open_filters[] = {
        {"Chart or project", "wormedit;bms;bme;bml;pms;bmson;sm;ssc;osu"},
    };
    static constexpr SDL_DialogFileFilter save_filters[] = {
        {"wormedit project", "wormedit"},
    };
    if (kind == WORM_UI_WORKSPACE_ACTION_OPEN_FILE)
        SDL_ShowOpenFileDialog(project_file_dialog_callback, token.release(), g_window,
                               open_filters, SDL_arraysize(open_filters), nullptr, false);
    else
        SDL_ShowSaveFileDialog(project_file_dialog_callback, token.release(), g_window,
                               save_filters, SDL_arraysize(save_filters), "Untitled.wormedit");
}

static void consume_project_file_dialog(WormUiWorkspaceAction& action) {
    std::lock_guard lock{g_project_dialog.mutex};
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
}

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

static bool nuklear_text_entry_active(void);
static bool project_modal_active(const WormUiWorkspaceView& view);
static bool notification_history_focused();

static void queue_shortcut_key_event(const SDL_KeyboardEvent* event, bool tap_capture = false) {
    if (!event || !g_window || !g_window_focused || event->type != SDL_EVENT_KEY_DOWN ||
        !event->down || event->windowID == 0 || event->windowID != SDL_GetWindowID(g_window) ||
        g_shortcut_events_overflowed) {
        return;
    }

    if (!tap_capture && compose_area_consumes_shortcut({g_compose_area_shortcuts_active, g_window_focused,
        project_dialog_active() || g_new_project_wizard.open,
        nuklear_text_entry_active(), compose_area_capture_active(),
        compose_area_band_visible() && worm_ui_nuklear_compose_state().keyboard_focus,
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
    action->shortcut_event_count = g_new_project_wizard.open || !g_bms_encoding_path_seen.empty()
                                       ? 0 : g_shortcut_event_count;
    for (size_t index = 0; index < g_shortcut_event_count; ++index) {
        action->shortcut_events[index] = g_shortcut_events[index];
    }
    action->shortcut_overflowed = g_shortcut_events_overflowed;
    action->shortcut_window_focused = g_window_focused;
    action->shortcut_notification_history_focused = notification_history_focused();
    auto& compose_state = worm_ui_nuklear_compose_state();
    action->shortcut_text_entry_active = nuklear_text_entry_active() ||
                                         compose_state.inline_timing_input_consumed ||
                                         compose_state.inline_offset_input_consumed;
    compose_state.inline_timing_input_consumed = false;
    compose_state.inline_offset_input_consumed = false;
    action->shortcut_ime_composition_active = g_ime_composition_active;
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
    struct nk_font* editor_font = nullptr;
    if (atlas && SDL_GetBasePath()) {
        const auto font_path = std::filesystem::path{SDL_GetBasePath()} /
                               "assets/fonts/AdwaitaMono-Regular.ttf";
        editor_font = nk_font_atlas_add_from_file(atlas, font_path.string().c_str(), 15.0f,
                                                  nullptr);
    }
    nk_sdl_font_stash_end(context);
    const auto theme_directory = SDL_GetBasePath()
                                     ? std::filesystem::path{SDL_GetBasePath()} / "assets/themes"
                                     : std::filesystem::path{"assets/themes"};
    wormedit::ui::theme::load(theme_directory);
    wormedit::ui::chrome::apply_theme(context);
    if (editor_font) nk_style_set_font(context, &editor_font->handle);

    if (!wormedit::ui::lucide::initialize(renderer)) {
        nk_sdl_shutdown(context);
        return false;
    }

    if (!SDL_StartTextInput(window)) {
        wormedit::ui::lucide::shutdown();
        nk_sdl_shutdown(context);
        return false;
    }
    g_window = window;
    g_renderer = renderer;
    g_context = context;
    initialize_notification_context();
    g_window_focused = true;
    g_notification_state = {};
    g_notification_history_state = {};
    g_notification_history_close_pending = false;
    g_notification_view = {};
    g_notification_modal = false;
    g_notification_project_modal = false;
    g_notification_escape_captured = false;
    worm_ui_nuklear_reset_external_player_panel();
    g_compose_area_shortcuts_active = false;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_double_click_count = 0;
    g_last_window_width = 0;
    g_last_window_height = 0;
    reset_project_dialog();
    g_new_project_wizard = {};
    return true;
}

void worm_ui_frontend_shutdown(void) {
    shutdown_notification_context();
    worm_ui_nuklear_reset_external_player_panel();
    reset_project_dialog();
    g_new_project_wizard = {};
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
    wormedit::ui::lucide::shutdown();
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
    if (!g_tap_timing_visible || !g_window_focused || !g_window ||
        event.windowID != SDL_GetWindowID(g_window) || project_dialog_active() ||
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
    if (!g_context || !g_window ||
        (window_id != 0 && window_id != SDL_GetWindowID(g_window)) || !std::isfinite(x) ||
        !std::isfinite(y))
        return {};
    return worm_ui_nuklear_metadata_drop_target(x, y);
}

static bool notification_document_gesture_active() {
    const auto& compose = worm_ui_nuklear_compose_state();
    const auto& timing = worm_ui_nuklear_timing_state();
    return g_notification_view.edit_gesture_active || g_audio.offset_drag_active ||
        compose.pointer_hold_active || compose.note_paint_pointer_active ||
        compose.bmson_razor_pointer_active || compose.semantic_layer_paint_pointer_active ||
        compose.lane_hold_active || compose.space_range_hold_active ||
        compose.note_move_candidate_active || compose.note_move_active ||
        compose.timing_stage_drag_active || compose.timing_marker_candidate_active ||
        compose.timing_marker_drag_active || timing.recipe_range_drag_active ||
        timing.marker_drag_active || compose_area_capture_active();
}

static bool notification_editor_gesture_active() {
    return notification_document_gesture_active() ||
        std::ranges::any_of(g_context->input.mouse.buttons,
                            [](const auto& button) { return button.down; });
}

static bool notification_history_contains(struct nk_rect bounds, float x, float y) {
    return std::isfinite(x) && std::isfinite(y) && x >= bounds.x && y >= bounds.y &&
           x < bounds.x + bounds.w && y < bounds.y + bounds.h;
}

static struct nk_rect notification_history_bounds(bool overlay) {
    auto* context = overlay ? &g_notification_context : g_context;
    const auto* window = context ? nk_window_find(context, "Notifications") : nullptr;
    if (window) return window->bounds;
    return nk_rect(12, 40, std::max(1.0f, std::min(760.0f, float(g_last_window_width) - 24)),
                   std::max(1.0f, std::min(560.0f, float(g_last_window_height) - 60)));
}

static bool notification_editor_text_owned() {
    const auto& compose = worm_ui_nuklear_compose_state();
    return (nuklear_text_entry_active() && !nk_window_is_active(g_context, "Notifications")) ||
        g_ime_composition_active || compose.inline_timing_open || compose.inline_offset_open ||
        compose.go_to_measure_open;
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
        !g_notification_history_close_pending && !nk_window_is_closed(g_context, "Notifications") &&
        nk_window_is_active(g_context, "Notifications") && !notification_history_locked() &&
        (nuklear_text_entry_active() || notification_history_contains(notification_history_bounds(false),
            g_context->input.mouse.pos.x, g_context->input.mouse.pos.y));
}

static void close_notification_history_windows() {
    nk_window_close(g_context, "Notifications");
    if (g_notification_context_initialized)
        nk_window_close(&g_notification_context, "Notifications");
}

static bool notification_owns_pointer(float x, float y) {
    if (g_notification_project_modal) return false;
    return wormedit::ui::nuklear::notification_pointer_owned(
        g_notification_state, x, y, notification_editor_gesture_active());
}

static bool capture_notification_key(const SDL_KeyboardEvent& event) {
    if (event.key != SDLK_ESCAPE || !g_window ||
        event.windowID != SDL_GetWindowID(g_window)) return false;
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
        nuklear_text_entry_active() || compose.inline_timing_open || compose.inline_offset_open ||
            compose.go_to_measure_open,
        g_ime_composition_active,
        notification_editor_gesture_active() || compose.key_event_count != 0,
        g_notification_modal || g_new_project_wizard.open || project_dialog_active());
    if (!target) return false;
    g_notification_state.dismiss_pending = *target;
    g_notification_escape_captured = true;
    return true;
}

static bool capture_notification_button(const SDL_MouseButtonEvent& event) {
    if (!g_window_focused || !g_window ||
        (event.windowID != 0 && event.windowID != SDL_GetWindowID(g_window)) ||
        !std::isfinite(event.x) || !std::isfinite(event.y)) return false;
    const bool captured = wormedit::ui::nuklear::notification_pointer_edge(
        g_notification_state, event.x, event.y, event.button, event.down,
        !g_notification_project_modal && !notification_editor_gesture_active());
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
    const bool available = g_notification_view.history.visible && !g_notification_project_modal &&
        !g_notification_history_close_pending && g_window_focused &&
        (!locked || g_notification_context_initialized);
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

static bool capture_inline_offset_key(const SDL_KeyboardEvent& event) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!compose.inline_offset_open) return false;
    compose.inline_offset_input_consumed = true;
    if (event.key == SDLK_ESCAPE) {
        if (event.down) compose.inline_offset_cancel_pending = true;
        return true;
    }
    SDL_Event copy{};
    copy.type = event.type;
    copy.key = event;
    (void)nk_sdl_handle_event(g_context, &copy);
    return true;
}

static bool handle_frontend_keyboard_event(const SDL_KeyboardEvent& event) {
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
        const bool tab_horizontal = worm_ui_nuklear_compose_state().raw_tab_down &&
            (event.key == SDLK_LEFT || event.key == SDLK_RIGHT);
        if (!tab_horizontal) queue_shortcut_key_event(&event);
        return false;
}

static bool queue_compose_pinch_event(const SDL_Event& event) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    using phase = worm_ui_nuklear_pinch_phase_t;
    if (event.type != SDL_EVENT_PINCH_BEGIN && event.type != SDL_EVENT_PINCH_UPDATE &&
        event.type != SDL_EVENT_PINCH_END) return false;
    if (!g_window || !g_window_focused ||
        (event.pinch.windowID != 0 && event.pinch.windowID != SDL_GetWindowID(g_window))) {
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

static bool handle_workspace_frontend_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    if (queue_compose_pinch_event(*event)) return true;
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        g_window_focused = false;
        g_tap_key_captured = false;
        g_tap_space_captured = false;
        g_audio.offset_cancel_pending = true;
        worm_ui_nuklear_compose_state().inline_offset_cancel_pending = true;
        g_shortcut_event_count = 0;
        g_double_click_count = 0;
        worm_ui_nuklear_compose_handle_focus_lost();
        worm_ui_nuklear_timing_handle_focus_lost();
        g_ime_composition_active = false;
    } else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        g_window_focused = true;
    } else if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (handle_frontend_keyboard_event(event->key)) return true;
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

static void render_close_confirmation(const WormUiCloseConfirmationView& view,
    WormUiWorkspaceAction& action, int width, int height) {
    if (!view.visible) return;
    if (nk_begin(g_context, "Unsaved changes", nk_rect(width / 2.0f - 200, height / 2.0f - 95, 400, 190),
        NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(g_context, 45, 1);
        nk_label_wrap(g_context, view.application
            ? "Save your changes before quitting WormEdit?" : "Save your changes before closing this project?");
        nk_layout_row_dynamic(g_context, 30, 3);
        WormUiCloseDecision decision = WORM_UI_CLOSE_NONE;
        if (nk_button_label(g_context, "Cancel")) decision = WORM_UI_CLOSE_CANCEL;
        if (nk_button_label(g_context, "Discard changes")) decision = WORM_UI_CLOSE_DISCARD;
        if (nk_button_label(g_context, "Save")) {
            decision = WORM_UI_CLOSE_SAVE;
            if (view.has_native_path)
                action.kind = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT;
            else start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
        }
        if (decision != WORM_UI_CLOSE_NONE) {
            action.close_decision = decision;
            action.close_project_id = view.project_id;
            action.close_revision = view.revision;
        }
        nk_layout_row_dynamic(g_context, 40, 1);
        nk_label_wrap(g_context, view.status_text ? view.status_text : "");
    }
    nk_end(g_context);
}

static void render_workspace_error(WormUiWorkspaceAction* action, int width, int height,
                                   const char* diagnostic, bool input_blocked = false) {
    worm_ui_nuklear_compose_discard_frame_input();
    worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
    if (nk_begin(g_context, "wormedit-workspace", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | (input_blocked ? NK_WINDOW_NO_INPUT : 0))) {
        nk_layout_row_dynamic(g_context, 30, 1);
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
    worm_ui_nuklear_begin_image_quad_frame();
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    consume_project_file_dialog(action);
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
    g_notification_view = notifications ? *notifications : WormUiNotificationView{};
    const bool modal = close && close->visible;
    g_notification_modal = modal;
    g_notification_project_modal = modal;
    render_workspace_error(&action, width, height, diagnostic, modal);
    if (close) render_close_confirmation(*close, action, width, height);
    render_notification_frame(g_notification_view, action, width, height, modal);
    return action;
}

static void render_loaded_project_menu_files(const WormUiWorkspaceView* view,
                                             WormUiWorkspaceAction* action, bool dialog_busy) {
    if (dialog_busy) nk_widget_disable_begin(g_context);
    if (nk_menu_item_label(g_context, "Save project", NK_TEXT_LEFT) &&
        !dialog_busy && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        if (view->project_has_native_path)
            action->kind = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT;
        else
            start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    }
    if (nk_menu_item_label(g_context, "Save project as...", NK_TEXT_LEFT) && !dialog_busy &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT);
    if (nk_menu_item_label(g_context, "Close project", NK_TEXT_LEFT) && !dialog_busy &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action->kind = WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT;
    if (dialog_busy) nk_widget_disable_end(g_context);
}

static void render_project_menu_files(const WormUiWorkspaceView* view,
                                      WormUiWorkspaceAction* action) {
    nk_layout_row_dynamic(g_context, 25, 1);
    const auto dialog_busy = project_dialog_active();
    if (dialog_busy) nk_widget_disable_begin(g_context);
    if (nk_menu_item_label(g_context, "New project", NK_TEXT_LEFT) &&
        !dialog_busy && action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        request_manual_new_project();
    if (dialog_busy) nk_widget_disable_end(g_context);
    if (dialog_busy) nk_widget_disable_begin(g_context);
    if (nk_menu_item_label(g_context, "Open chart or project...", NK_TEXT_LEFT) && !dialog_busy &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_OPEN_FILE);
    if (dialog_busy) nk_widget_disable_end(g_context);
    if (view->project_loaded) render_loaded_project_menu_files(view, action, dialog_busy);
    if (nk_menu_item_label(g_context, "Options...", NK_TEXT_LEFT))
        action->options.kind = WORM_UI_OPTIONS_OPEN;
}

static void render_project_menu_navigation(const WormUiWorkspaceView* view,
                                           WormUiWorkspaceAction* action) {
    if (!view->project_loaded) return;
    for (int tab = 0; tab < WORM_UI_WORKSPACE_TAB_COUNT; ++tab) {
        if (tab == WORM_UI_WORKSPACE_TAB_TIMING || tab == WORM_UI_WORKSPACE_TAB_COMPOSE)
            continue;
        const auto& item = view->tabs[tab];
        if (!item.label) continue;
        if (!item.enabled) nk_widget_disable_begin(g_context);
        const bool chosen = nk_menu_item_label(g_context, item.label, NK_TEXT_LEFT) != 0;
        if (!item.enabled) nk_widget_disable_end(g_context);
        if (chosen && item.enabled && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action->kind = WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
            action->tab = static_cast<WormUiWorkspaceTab>(tab);
        }
    }
}

static void render_external_player_menu(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, "External player preferences...", NK_TEXT_LEFT))
        action.external_player.kind = WORM_UI_EXTERNAL_PLAYER_OPEN;
    if (!view.project_loaded) return;
    if (nk_menu_item_label(g_context, "External play from start", NK_TEXT_LEFT))
        action.external_player.kind = WORM_UI_EXTERNAL_PLAYER_PLAY_START;
    if (nk_menu_item_label(g_context, "External play from here", NK_TEXT_LEFT))
        action.external_player.kind = WORM_UI_EXTERNAL_PLAYER_PLAY_HERE;
    if (nk_menu_item_label(g_context, "External stop", NK_TEXT_LEFT))
        action.external_player.kind = WORM_UI_EXTERNAL_PLAYER_STOP;
}

static void render_project_menu(const WormUiWorkspaceView* view,
                                WormUiWorkspaceAction* action) {
    if (!nk_menu_begin_label(g_context, "File", NK_TEXT_LEFT, nk_vec2(250, 540))) return;
    render_project_menu_files(view, action);
    render_project_menu_navigation(view, action);
    render_external_player_menu(*view, *action);
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, "Quit", NK_TEXT_LEFT))
        action->kind = WORM_UI_WORKSPACE_ACTION_QUIT;
    nk_menu_end(g_context);
}

static void render_clipboard_menu(const WormUiWorkspaceView* view,
                                  WormUiWorkspaceAction* action) {
    if (!nk_menu_begin_label(g_context, "Edit", NK_TEXT_LEFT, nk_vec2(190, 120))) return;
    const bool text_owner = nuklear_text_entry_active() || g_ime_composition_active;
    const auto item = [&](const char* label, bool enabled, WormUiClipboardAction kind) {
        nk_layout_row_dynamic(g_context, 25, 1);
        if (!enabled) nk_widget_disable_begin(g_context);
        const bool chosen = nk_menu_item_label(g_context, label, NK_TEXT_LEFT) != 0;
        if (!enabled) nk_widget_disable_end(g_context);
        if (chosen && enabled) action->clipboard = kind;
    };
    item("Copy", !text_owner && view->compose.clipboard_copy_available, WORM_UI_CLIPBOARD_COPY);
    item("Cut", !text_owner && view->compose.clipboard_copy_available, WORM_UI_CLIPBOARD_CUT);
    item("Paste", !text_owner && view->compose.clipboard_paste_available, WORM_UI_CLIPBOARD_PASTE);
    nk_menu_end(g_context);
}

static void render_view_menu(const WormUiWorkspaceView* view,
                             WormUiWorkspaceAction* action) {
    if (!nk_menu_begin_label(g_context, "View", NK_TEXT_LEFT, nk_vec2(230, 160))) return;
    nk_layout_row_dynamic(g_context, 25, 1);
    if (view->project_loaded && nk_menu_item_label(g_context, view->player_preview.visible ? "Hide preview"
                                                                 : "Show preview", NK_TEXT_LEFT) &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action->kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW;
    if (view->compose.visible) {
        nk_layout_row_dynamic(g_context, 25, 1);
        if (nk_menu_item_label(g_context, "Reset editor view", NK_TEXT_LEFT) &&
            action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
            action->compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW;
    }
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, g_frame_timing_visible ? "Hide frame timing" : "Frame timing", NK_TEXT_LEFT))
        g_frame_timing_visible = !g_frame_timing_visible;
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, view->tap_timing.visible ? "Hide tap timing" : "Tap timing", NK_TEXT_LEFT))
        action->tap_timing.toggle = true;
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, "Notifications", NK_TEXT_LEFT) && view->notifications.history_available)
        action->notifications = {WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY, 0};
    nk_menu_end(g_context);
}

static void close_unavailable_inline_offset(const WormUiWorkspaceView& view) {
    auto& compose = worm_ui_nuklear_compose_state();
    if (!compose.inline_offset_open) return;
    const bool available = g_window_focused && view.project_loaded &&
        view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view.compose.visible &&
        view.compose.audio_offset_editable && !view.options.visible && !project_modal_active(view) &&
        compose.inline_offset_chart_id == view.compose.chart_id &&
        compose.inline_offset_revision == view.compose.document_revision;
    if (!available || compose.inline_offset_cancel_pending) {
        worm_ui_nuklear_cancel_inline_offset(compose);
        nk_edit_unfocus(g_context);
    }
}

static void render_title_bar(const WormUiWorkspaceView* view,
                             WormUiWorkspaceAction* action) {
    close_unavailable_inline_offset(*view);
    const bool custom_controls = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_BORDERLESS) != 0;
    nk_menubar_begin(g_context);
    nk_layout_row_template_begin(g_context, 32);
    nk_layout_row_template_push_static(g_context, 76);
    nk_layout_row_template_push_static(g_context, 56);
    nk_layout_row_template_push_static(g_context, 56);
    nk_layout_row_template_push_dynamic(g_context);
    if (custom_controls) wormedit::ui::chrome::IconButton::layout_columns(g_context, 3);
    nk_layout_row_template_end(g_context);
    render_project_menu(view, action);
    render_clipboard_menu(view, action);
    render_view_menu(view, action);
    nk_labelf(g_context, NK_TEXT_LEFT, "  %s%s", view->project_title,
              view->dirty ? " *" : "");
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
}

static void render_workspace_tabs(const WormUiWorkspaceView* view,
                                  WormUiWorkspaceAction* action) {
    nk_layout_row_dynamic(g_context, 32, WORM_UI_WORKSPACE_TAB_COUNT);
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
    return view.visible && view.scene_available && view.has_bgm_events &&
           worm_ui_nuklear_compose_view_valid(&view);
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
    if (available && view.compose.waveform_overview_row_count == 0) {
        nk_widget_disable_begin(g_context);
        nk_label(g_context, "Overview unavailable", NK_TEXT_CENTERED);
        nk_widget_disable_end(g_context);
        if (nk_widget_is_hovered(g_context))
            nk_tooltip(g_context, "Audio is unavailable for this chart overview.");
        return;
    }
    struct nk_rect bounds{};
    const auto widget = available ? nk_widget(&bounds, g_context) : NK_WIDGET_INVALID;
    const bool interactive = widget == NK_WIDGET_VALID || widget == NK_WIDGET_ROM;
    const bool readonly = (g_context->current->layout->flags & NK_WINDOW_ROM) != 0;
    const bool focused = g_window_focused && interactive && !readonly &&
        !g_new_project_wizard.open && !project_dialog_active() &&
        (!view.pending_bms_encoding_path || !view.pending_bms_encoding_path[0]);
    worm_ui_nuklear_render_compose_minimap(g_context, &view.compose, action, bounds, focused);
    if (available && g_window_focused && !g_new_project_wizard.open && !project_dialog_active())
        render_minimap_preferences(view, action, bounds);
}

static auto workspace_status_line(const WormUiWorkspaceView& view) -> std::string {
    const auto* line = view.notifications.current_text ? view.notifications.current_text : view.status_text;
    std::string status = !g_project_dialog_error.empty() ? g_project_dialog_error : line;
    if (!view.recovery_has_actionable_candidate && view.recovery_notice &&
        view.recovery_notice[0]) {
        if (!status.empty()) status += " · ";
        status += "Recovery: ";
        status += view.recovery_notice;
    }
    return status;
}

static void render_transport_rate(const WormUiWorkspaceView& view, WormUiWorkspaceAction* action) {
    static const char* labels[]{"1×", "0.9×", "0.8×", "0.75×", "0.5×", "0.33×", "0.25×", "0.2×", "0.1×"};
    const int index = view.transport_rate_index < 9 ? view.transport_rate_index : 8;
    const int label_index = 8 - index;
    int target = index;
    if (!view.transport_rate_available) nk_widget_disable_begin(g_context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left, "Slower playback rate"}.render(g_context) &&
        index > 0) target = index - 1;
    nk_label(g_context, labels[label_index], NK_TEXT_CENTERED);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_right, "Faster playback rate"}.render(g_context) &&
        index < 8) target = index + 1;
    if (!view.transport_rate_available) nk_widget_disable_end(g_context);
    if (view.transport_rate_available && target != index && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE;
        action->transport_rate_index = static_cast<uint8_t>(target);
    }
    if (!view.transport_keep_pitch_available) nk_widget_disable_begin(g_context);
    if (wormedit::ui::chrome::mode_button(g_context, "Keep pitch", "Preserve pitch at reduced speed", view.transport_keep_pitch) &&
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
    if (wormedit::ui::chrome::IconButton{Glyph::audio_lines, hint,
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
    if (nk_button_label(g_context, label))
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

static void render_status_bar(const WormUiWorkspaceView* view,
                              WormUiWorkspaceAction* action) {
    const bool snap_label_visible = view->compose.visible && view->compose.area_snap_denominator != 0;
    nk_layout_row_template_begin(g_context, 30);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 4);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 1);
    nk_layout_row_template_push_static(g_context, 54);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 1);
    nk_layout_row_template_push_static(g_context, 100);
    if (snap_label_visible) nk_layout_row_template_push_static(g_context, 64);
    nk_layout_row_template_push_dynamic(g_context);
    if (notification_badge_visible(view->notifications))
        nk_layout_row_template_push_static(g_context, 180);
    nk_layout_row_template_push_static(g_context, 80);
    nk_layout_row_template_push_dynamic(g_context);
    nk_layout_row_template_end(g_context);
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
    render_transport_rate(*view, action);
    if (snap_label_visible)
        nk_labelf(g_context, NK_TEXT_CENTERED, "Snap 1/%llu",
                  static_cast<unsigned long long>(view->compose.area_snap_denominator));
    const auto status = workspace_status_line(*view);
    nk_label_colored(g_context, status.c_str(), NK_TEXT_LEFT,
                      notification_status_color(view->notifications));
    if (notification_badge_visible(view->notifications))
        render_notification_badge(view->notifications, *action);
    const auto sample_rate = view->transport_sample_rate == 0 ? 1U : view->transport_sample_rate;
    const auto seconds = view->transport_frame / sample_rate;
    nk_labelf(g_context, NK_TEXT_RIGHT, "%llu:%02llu",
              static_cast<unsigned long long>(seconds / 60),
              static_cast<unsigned long long>(seconds % 60));
    nk_labelf(g_context, NK_TEXT_RIGHT, "%s · %s", view->artist,
              view->selected_difficulty);
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
        (void)nk_edit_string_zero_terminated(
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

static bool options_owns_content_input(const WormUiWorkspaceView& view) {
    if (view.close_confirmation.visible) return true;
    if (history_owns_content_input(view)) return true;
    const auto& compose = worm_ui_nuklear_compose_state();
    const bool text_active = nuklear_text_entry_active() || g_ime_composition_active ||
                             compose.inline_timing_open || compose.go_to_measure_open;
    if (g_context && !g_notification_project_modal &&
        wormedit::ui::nuklear::notification_content_occluded(g_notification_state,
            g_context->input.mouse.pos.x, g_context->input.mouse.pos.y,
            notification_editor_gesture_active(), text_active)) return true;
    if (!view.options.visible || !g_context) return false;
    const auto* window = nk_window_find(g_context, "Options");
    const auto bounds = window ? window->bounds : nk_rect(180, 110, 800, 450);
    if (nk_input_is_mouse_hovering_rect(&g_context->input, bounds)) return true;
    for (const auto& button : g_context->input.mouse.buttons) {
        if (button.down && button.clicked_pos.x >= bounds.x && button.clicked_pos.x <= bounds.x + bounds.w &&
            button.clicked_pos.y >= bounds.y && button.clicked_pos.y <= bounds.y + bounds.h) return true;
    }
    return nk_window_is_active(g_context, "Options") && nuklear_text_entry_active();
}

static void render_workspace_content(const WormUiWorkspaceView& view,
                                     WormUiWorkspaceAction& action) {
    prepare_workspace_content_scroll(g_context, view.compose.visible);
    // Form screens stretch rows across the window; a centered column keeps label and control close.
    const bool padded = view.metadata.visible || view.setup.visible;
    if (padded) {
        const auto padding = g_context->style.window.group_padding;
        nk_style_push_vec2(g_context, &g_context->style.window.group_padding,
                           nk_vec2(wormedit::ui::layout::form_side_margin(
                                       static_cast<float>(g_last_window_width),
                                       static_cast<float>(g_last_window_height)),
                                   padding.y));
    }
    const bool open = nk_group_begin(g_context, "workspace-content",
                                     workspace_content_flags(view.compose.visible, padded)) != 0;
    if (padded) nk_style_pop_vec2(g_context);
    if (!open) return;
    if (view.setup.visible) {
        worm_ui_nuklear_render_setup_panel(g_context, &view.setup, &action);
        (void)render_recovery_panel(view, action);
    } else if (view.metadata.visible) {
        worm_ui_nuklear_render_metadata_panel(g_context, &view.metadata, &action,
                                              g_window_focused);
    } else if (view.timing.visible) {
        auto timing = view.timing;
        timing.input_occluded = options_owns_content_input(view);
        worm_ui_nuklear_render_timing_panel(g_context, &timing, &action,
                                            g_window_focused, g_double_clicks.data(),
                                            g_double_click_count);
    } else if (view.compose.visible) {
        auto compose = view.compose;
        compose.input_occluded = options_owns_content_input(view);
        worm_ui_nuklear_render_compose_panel(
            g_context, &compose, &view.timing, &action, g_window_focused,
            g_ime_composition_active, g_double_clicks.data(), g_double_click_count,
            g_shortcut_event_count);
    } else if (view.audio_slices.visible) {
        worm_ui_nuklear_render_audio_slices(g_context, &view.audio_slices, &action, true,
                                            g_window_focused);
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
    if ((view.timing.visible || view.compose.visible) &&
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

// One fixed-height row under the mode tabs, so the layout never shifts between tabs: the
// chart minimap when the chart has BGM audio, otherwise the transport seek bar.
static constexpr float preview_row_height = 24.0f;

static void render_transport_seek_control(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(g_context, preview_row_height, 1);
    if (status_minimap_available(view.compose)) {
        render_status_minimap(view, &action, true);
        return;
    }
    constexpr nk_size seek_steps = 10'000;
    if (!view.transport_available || view.transport_end_frame == 0) {
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
    if (nk_progress(g_context, &seek_position, seek_steps, NK_MODIFIABLE)) {
        action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK;
        action.transport_seek_frame =
            (view.transport_end_frame / seek_steps) * seek_position +
            ((view.transport_end_frame % seek_steps) * seek_position) / seek_steps;
    }
}

static void render_empty_project_content(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action, float height) {
    nk_layout_row_dynamic(g_context, height, 1);
    if (!nk_group_begin(g_context, "empty-project-content", NK_WINDOW_BORDER)) return;
    nk_layout_row_dynamic(g_context, 48, 1);
    nk_label(g_context, "No project loaded", NK_TEXT_CENTERED);
    nk_layout_row_dynamic(g_context, 28, 1);
    nk_label(g_context, "Create a project or open an existing chart to begin.", NK_TEXT_CENTERED);
    nk_layout_row_dynamic(g_context, 42, 1);
    const bool dialog_busy = project_dialog_active();
    if (dialog_busy) nk_widget_disable_begin(g_context);
    if (nk_button_label(g_context, "New project") &&
        !dialog_busy && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        request_manual_new_project();
    if (dialog_busy) nk_widget_disable_end(g_context);
    if (dialog_busy) nk_widget_disable_begin(g_context);
    if (nk_button_label(g_context, dialog_busy ? "Choosing file..." : "Open chart or project...") &&
        !dialog_busy && action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
        start_project_file_dialog(WORM_UI_WORKSPACE_ACTION_OPEN_FILE);
    if (dialog_busy) nk_widget_disable_end(g_context);
    if (!g_project_dialog_error.empty()) {
        nk_layout_row_dynamic(g_context, 38, 1);
        nk_label_wrap(g_context, g_project_dialog_error.c_str());
    }
    render_notification_landing_status(view, action);
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

static bool project_modal_active(const WormUiWorkspaceView& view) {
    return view.close_confirmation.visible || g_new_project_wizard.open ||
           (view.pending_bms_encoding_path && view.pending_bms_encoding_path[0]);
}

static nk_flags workspace_window_flags(const WormUiWorkspaceView& view) {
    const auto flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND;
    return flags | (project_modal_active(view) ? NK_WINDOW_NO_INPUT : 0);
}

static bool begin_project_modal(const char* title, struct nk_rect bounds) {
    nk_window_show(g_context, title, NK_SHOWN);
    nk_window_set_focus(g_context, title);
    return nk_begin(g_context, title, bounds,
                    NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR);
}

static void render_new_project_wizard(WormUiWorkspaceAction& action, int width, int height) {
    if (!g_new_project_wizard.open) return;
    constexpr std::array<const char*, 8> layout_labels{
        "4K", "5K", "6K", "7K", "8K", "9K", "10K", "14K"};
    const float popup_width = std::min(500.0f, std::max(280.0f, width - 24.0f));
    const float popup_height = g_new_project_wizard.audio ? 266.0f : 204.0f;
    const auto bounds = nk_rect((width - popup_width) * 0.5f,
                                (height - popup_height) * 0.5f,
                                popup_width, popup_height);
    if (!begin_project_modal("Create project", bounds)) {
        nk_end(g_context);
        return;
    }
    nk_layout_row_dynamic(g_context, 28, 1);
    nk_label(g_context, g_new_project_wizard.audio ? "Import audio as a new project?"
                                                  : "Create a new project", NK_TEXT_LEFT);
    if (g_new_project_wizard.audio) {
        nk_layout_row_dynamic(g_context, 52, 1);
        nk_label_wrap(g_context, g_new_project_wizard.audio_path.c_str());
    }
    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context, "Choose the initial chart layout", NK_TEXT_LEFT);
    nk_layout_row_dynamic(g_context, 30, 1);
    const auto selected = wormedit::ui::chrome::safe_combo(g_context, layout_labels.data(),
                                   static_cast<int>(layout_labels.size()),
                                   g_new_project_wizard.layout_index, 25,
                                   nk_vec2(180.0f, 225.0f));
    if (selected >= 0 && selected < static_cast<int>(layout_labels.size()))
        g_new_project_wizard.layout_index = static_cast<std::uint8_t>(selected);
    const bool audio_path_too_long = g_new_project_wizard.audio &&
                                     g_new_project_wizard.audio_path.size() >=
                                         sizeof(action.project_path);
    if (audio_path_too_long) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Audio path is too long to import.", NK_TEXT_LEFT);
    }
    nk_layout_row_dynamic(g_context, 34, 2);
    if (nk_button_label(g_context, "Cancel")) {
        cancel_new_project_wizard(action);
    }
    if (audio_path_too_long) nk_widget_disable_begin(g_context);
    if (nk_button_label(g_context, "Create project") && !audio_path_too_long) {
        confirm_new_project_wizard(action);
    }
    if (audio_path_too_long) nk_widget_disable_end(g_context);
    nk_end(g_context);
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
    const float popup_width = std::min(520.0f, std::max(280.0f, width - 24.0f));
    const float popup_height = 252.0f;
    const auto bounds = nk_rect((width - popup_width) * 0.5f,
                                (height - popup_height) * 0.5f,
                                popup_width, popup_height);
    if (!begin_project_modal("BMS text encoding", bounds)) {
        nk_end(g_context);
        return;
    }
    nk_layout_row_dynamic(g_context, 30, 1);
    nk_label(g_context, "Choose the BMS text encoding", NK_TEXT_LEFT);
    nk_layout_row_dynamic(g_context, 46, 1);
    nk_label_wrap(g_context, path.c_str());
    nk_layout_row_dynamic(g_context, 42, 1);
    nk_label_wrap(g_context, "This chart contains non-ASCII text without a UTF-8 BOM.");
    nk_layout_row_dynamic(g_context, 30, 1);
    g_bms_encoding_choice = wormedit::ui::chrome::safe_combo(g_context, labels.data(), count,
                                      g_bms_encoding_choice, 25, nk_vec2(250.0f, 180.0f));
    nk_layout_row_dynamic(g_context, 34, 2);
    if (nk_button_label(g_context, "Cancel")) {
        cancel_bms_encoding_dialog(action);
    }
    if (nk_button_label(g_context, "Open chart") &&
        action.kind == WORM_UI_WORKSPACE_ACTION_NONE &&
        path.size() < sizeof(action.project_path)) {
        action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        action.bms_encoding_index = indexes[static_cast<std::size_t>(g_bms_encoding_choice)];
        std::memcpy(action.project_path, path.c_str(), path.size() + 1);
    }
    nk_end(g_context);
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
        render_title_bar(&frame.view, &frame.action);
        const float recovery_height = render_recovery_panel(frame.view, frame.action);
        const float available_height = std::max(100.0f,
                                                static_cast<float>(frame.height) - 64.0f -
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
        nk_labelf(g_context, NK_TEXT_LEFT, "Last %zu frames: avg %.3f ms, p99 %.3f ms",
                  frame.count, frame.average_ms, frame.p99_ms);
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
            else nk_labelf(g_context, NK_TEXT_LEFT, "%s: avg %.3f ms, p99 %.3f ms",
                           timer.phase_names[i], summary.average_ms, summary.p99_ms);
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
        render_title_bar(&view, &action);
        render_workspace_tabs(&view, &action);
        render_transport_seek_control(view, action);
        const float recovery_height = render_recovery_above_content(view, action);
        float content_height = frame.height > 300
                                  ? static_cast<float>(frame.height) - 140.0f - recovery_height
                                  : 80.0f;
        if (content_height < 80.0f) content_height = 80.0f;
        nk_layout_row_dynamic(g_context, content_height, 1);
        render_workspace_content(view, action);
        render_status_bar(&view, &action);

    }
    nk_end(g_context);
    render_project_modals(frame);
}

static void render_auxiliary_windows(const WorkspaceRenderContext& frame) {
    render_close_confirmation(frame.view.close_confirmation, frame.action, frame.width, frame.height);
    if (!project_modal_active(frame.view))
        worm_ui_nuklear_render_options_window(g_context, &frame.view.options, &frame.action.options);
    if (project_modal_active(frame.view)) return;
    if (!frame.view.project_loaded) {
        worm_ui_nuklear_render_external_player_panel(g_context, &frame.view.external_player,
                                                     &frame.action.external_player);
        worm_ui_nuklear_render_tap_timing_panel(g_context, frame.view.tap_timing, frame.action.tap_timing);
        return;
    }
    render_frame_timing_overlay(frame.width, frame.height);
    worm_ui_nuklear_render_tap_timing_panel(g_context, frame.view.tap_timing, frame.action.tap_timing);
    worm_ui_nuklear_render_external_player_panel(g_context, &frame.view.external_player,
                                                 &frame.action.external_player);
    worm_ui_nuklear_render_player_preview(g_context, g_renderer, &frame.view.player_preview,
                                          &frame.action, frame.width, frame.height);
}

static void remember_notification_frame(const WormUiWorkspaceView* view) {
    g_notification_view = view ? view->notifications : WormUiNotificationView{};
    g_notification_project_modal = view && project_modal_active(*view);
    g_notification_modal = g_notification_project_modal || (view && view->options.visible);
}

static void render_notification_history(const WormUiNotificationView& view,
    WormUiWorkspaceAction& action, int width, int height, bool project_modal) {
    if (g_notification_history_close_pending) {
        action.notifications = {WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY, 0};
        g_notification_history_close_pending = false;
        return;
    }
    if (project_modal || !view.history.visible) return;
    const bool locked = notification_history_locked();
    if (locked && !g_notification_context_initialized) return;
    auto* context = locked ? &g_notification_context : g_context;
    if (locked) context->style = g_context->style;
    wormedit::ui::nuklear::render_notification_panel(context, view.history,
        g_notification_history_state, action.notifications, width, height, !locked);
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

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
    remember_notification_frame(view);
    worm_ui_nuklear_begin_image_quad_frame();
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_NONE;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    consume_project_file_dialog(action);
    const bool timing_tab_active = view && view->project_loaded &&
                                   view->active_tab == WORM_UI_WORKSPACE_TAB_TIMING;
    worm_ui_nuklear_reconcile_timing_gestures(view ? &view->timing : nullptr, timing_tab_active,
                                              g_window_focused);
    if (!g_context || !g_window || !view) {
        worm_ui_nuklear_compose_discard_frame_input();
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        expose_shortcut_input(&action);
        return action;
    }
    g_tap_timing_visible = view->tap_timing.visible && !project_modal_active(*view);
    sync_pending_audio_project(*view);

    const auto* validation_error = workspace_validation_error(*view);

    int width = 0;
    int height = 0;
    if (!frontend_window_size(&width, &height)) {
        worm_ui_nuklear_compose_discard_frame_input();
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        expose_shortcut_input(&action);
        return action;
    }

    if (validation_error) {
        render_workspace_error(&action, width, height, validation_error, view->close_confirmation.visible);
        render_close_confirmation(view->close_confirmation, action, width, height);
        render_notification_frame(view->notifications, action, width, height, project_modal_active(*view));
        return action;
    }

    const std::string window_title = std::string{view->project_title} +
                                     (view->dirty ? " *" : "") + " — wormedit";
    if (window_title != SDL_GetWindowTitle(g_window))
        SDL_SetWindowTitle(g_window, window_title.c_str());

    if (!view->project_loaded) {
        worm_ui_nuklear_compose_discard_frame_input();
        worm_ui_nuklear_reconcile_timing_gestures(nullptr, false, false);
        render_workspace_main_window(WorkspaceRenderContext{*view, action, width, height});
        render_auxiliary_windows(WorkspaceRenderContext{*view, action, width, height});
        render_notification_frame(view->notifications, action, width, height, project_modal_active(*view));
        expose_shortcut_input(&action);
        g_audio.offset_escape_pending = false;
        return action;
    }

    worm_ui_nuklear_emit_timing_cancellations(&view->timing, timing_tab_active, &action);

    const bool compose_tab_active = view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    g_compose_area_shortcuts_active = compose_tab_active && view->compose.visible &&
        !project_modal_active(*view);
    worm_ui_nuklear_prepare_compose_panel(&view->compose,
                                          compose_tab_active && !project_modal_active(*view), g_window_focused,
                                          &action);

    render_workspace_main_window(WorkspaceRenderContext{*view, action, width, height});
    render_auxiliary_windows(WorkspaceRenderContext{*view, action, width, height});
    render_notification_frame(view->notifications, action, width, height, project_modal_active(*view));
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
        wormedit::ui::nuklear::render_sdl_backend(g_context, NK_ANTI_ALIASING_ON,
            g_notification_context_initialized ? &g_notification_context : nullptr);
    }
}
