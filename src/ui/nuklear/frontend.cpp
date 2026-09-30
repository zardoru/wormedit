#include "ui/nuklear/external_player_panel.h"
#define NK_IMPLEMENTATION
#define NK_SDL3_RENDERER_IMPLEMENTATION

#include "ui/frontend.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/image_quad.h"
#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/metadata_state.h"
#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/timing_panel.h"
#include "ui/nuklear/timing_state.h"
#include "ui/nuklear/theme.h"

#include "wormedit/nuklear_config.h"

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

// Keep counters at the adapter boundary: converted buffers are shared by every
// draw command, so their vertices and indices are counted once per frame.
struct GpuSubmissionCounters {
    WormUiFrameGpuWork work{};
    SDL_Rect current_clip{};
    SDL_Rect previous_clip{};
    SDL_Texture* previous_texture{};
    bool current_clip_enabled{};
    bool previous_clip_enabled{};
    bool submitted{};
};
static GpuSubmissionCounters g_gpu_submission;

static nk_flags convert_nuklear_commands(nk_context* context, nk_buffer* commands,
    nk_buffer* vertices, nk_buffer* indices, const nk_convert_config* config) {
    const RenderSubphaseScope convert_phase{WORM_UI_FRAME_RENDER_CONVERT};
    const auto result = nk_convert(context, commands, vertices, indices, config);
    g_gpu_submission.work.vertices = config->vertex_size == 0 ? 0 : vertices->allocated / config->vertex_size;
    g_gpu_submission.work.indices = indices->allocated / sizeof(nk_draw_index);
    return result;
}

static bool set_nuklear_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    const auto result = SDL_SetRenderClipRect(renderer, rect);
    if (result) {
        g_gpu_submission.current_clip_enabled = rect != nullptr;
        g_gpu_submission.current_clip = rect ? *rect : SDL_Rect{};
    }
    return result;
}

static bool same_nuklear_clip() {
    const auto& current = g_gpu_submission.current_clip;
    const auto& previous = g_gpu_submission.previous_clip;
    return g_gpu_submission.current_clip_enabled == g_gpu_submission.previous_clip_enabled &&
        current.x == previous.x && current.y == previous.y && current.w == previous.w && current.h == previous.h;
}

// The bundled SDL adapter hardcodes 16-bit indices. Keep its conversion/render
// implementation, but submit the configured Nuklear index width to SDL.
static_assert(sizeof(nk_draw_index) == 4);
static bool render_nuklear_geometry_raw(SDL_Renderer* renderer, SDL_Texture* texture,
    const float* xy, int xy_stride, const SDL_FColor* colors, int color_stride,
    const float* uv, int uv_stride, int vertex_count, const void* indices,
    int index_count, int) {
    ++g_gpu_submission.work.draw_calls;
    if (g_gpu_submission.submitted) {
        g_gpu_submission.work.clip_rect_changes += !same_nuklear_clip();
        g_gpu_submission.work.texture_switches += texture != g_gpu_submission.previous_texture;
    }
    g_gpu_submission.previous_clip = g_gpu_submission.current_clip;
    g_gpu_submission.previous_clip_enabled = g_gpu_submission.current_clip_enabled;
    g_gpu_submission.previous_texture = texture;
    g_gpu_submission.submitted = true;
    return SDL_RenderGeometryRaw(renderer, texture, xy, xy_stride, colors, color_stride,
        uv, uv_stride, vertex_count, indices, index_count, sizeof(nk_draw_index));
}
#define nk_convert convert_nuklear_commands
#define SDL_SetRenderClipRect set_nuklear_clip_rect
#define SDL_RenderGeometryRaw render_nuklear_geometry_raw
#include <demo/sdl3_renderer/nuklear_sdl3_renderer.h>
#undef SDL_RenderGeometryRaw
#undef SDL_SetRenderClipRect
#undef nk_convert

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
static std::array<WormUiShortcutEvent, WORM_UI_SHORTCUT_EVENT_CAPACITY> g_shortcut_events{};
static size_t g_shortcut_event_count{};
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

static bool nuklear_text_entry_active(void);

static void queue_shortcut_key_event(const SDL_KeyboardEvent* event) {
    if (!event || !g_window || !g_window_focused || event->type != SDL_EVENT_KEY_DOWN ||
        !event->down || event->windowID == 0 || event->windowID != SDL_GetWindowID(g_window) ||
        g_shortcut_events_overflowed) {
        return;
    }

    if (compose_area_consumes_shortcut({g_compose_area_shortcuts_active, g_window_focused,
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
    g_window_focused = true;
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
    if (!nk_menu_begin_label(g_context, "Project", NK_TEXT_LEFT, nk_vec2(250, 540))) return;
    render_project_menu_files(view, action);
    render_project_menu_navigation(view, action);
    render_external_player_menu(*view, *action);
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, "Quit", NK_TEXT_LEFT))
        action->kind = WORM_UI_WORKSPACE_ACTION_QUIT;
    nk_menu_end(g_context);
}

static void render_view_menu(const WormUiWorkspaceView* view,
                             WormUiWorkspaceAction* action) {
    if (!nk_menu_begin_label(g_context, "View", NK_TEXT_LEFT, nk_vec2(230, 130))) return;
    nk_layout_row_dynamic(g_context, 25, 1);
    if (nk_menu_item_label(g_context, view->player_preview.visible ? "Hide preview"
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
    nk_menu_end(g_context);
}

static void render_title_bar(const WormUiWorkspaceView* view,
                             WormUiWorkspaceAction* action) {
    const bool custom_controls = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_BORDERLESS) != 0;
    nk_menubar_begin(g_context);
    nk_layout_row_template_begin(g_context, 32);
    nk_layout_row_template_push_dynamic(g_context);
    nk_layout_row_template_push_static(g_context, 76);
    nk_layout_row_template_push_static(g_context, 56);
    if (custom_controls) wormedit::ui::chrome::IconButton::layout_columns(g_context, 3);
    nk_layout_row_template_end(g_context);
    nk_labelf(g_context, NK_TEXT_LEFT, "  %s%s", view->project_title,
              view->dirty ? " *" : "");
    render_project_menu(view, action);
    if (view->project_loaded) render_view_menu(view, action);
    else nk_label(g_context, "", NK_TEXT_LEFT);
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
    return view.visible && view.scene_available && view.waveform_overview_row_count != 0 &&
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
    std::string status = !g_project_dialog_error.empty()
                             ? g_project_dialog_error : view.status_text;
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
    const int index = view.transport_rate_index < 9 ? 8 - view.transport_rate_index : 0;
    if (!view.transport_rate_available) nk_widget_disable_begin(g_context);
    const int selected = nk_combo(g_context, labels, 9, index, 24, nk_vec2(100, 250));
    if (!view.transport_rate_available) nk_widget_disable_end(g_context);
    if (view.transport_rate_available && selected != index && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE;
        action->transport_rate_index = static_cast<uint8_t>(8 - selected);
    }
    if (!view.transport_keep_pitch_available) nk_widget_disable_begin(g_context);
    if (wormedit::ui::chrome::mode_button(g_context, "Keep pitch", "Preserve pitch at reduced speed", view.transport_keep_pitch) &&
        view.transport_keep_pitch_available && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH;
        action->transport_keep_pitch = !view.transport_keep_pitch;
    }
    if (!view.transport_keep_pitch_available) nk_widget_disable_end(g_context);
}

static void render_status_bar(const WormUiWorkspaceView* view,
                              WormUiWorkspaceAction* action) {
    const bool minimap_available = status_minimap_available(view->compose);
    nk_layout_row_template_begin(g_context, 30);
    wormedit::ui::chrome::IconButton::layout_columns(g_context, 3);
    nk_layout_row_template_push_static(g_context, 80);
    nk_layout_row_template_push_static(g_context, 100);
    nk_layout_row_template_push_dynamic(g_context);
    if (minimap_available) nk_layout_row_template_push_variable(g_context, 120);
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
    render_transport_rate(*view, action);
    const auto status = workspace_status_line(*view);
    nk_label(g_context, status.c_str(), NK_TEXT_LEFT);
    render_status_minimap(*view, action, minimap_available);
    const auto sample_rate = view->transport_sample_rate == 0 ? 1U : view->transport_sample_rate;
    const auto seconds = view->transport_frame / sample_rate;
    nk_labelf(g_context, NK_TEXT_RIGHT, "%llu:%02llu",
              static_cast<unsigned long long>(seconds / 60),
              static_cast<unsigned long long>(seconds % 60));
    nk_labelf(g_context, NK_TEXT_RIGHT, "%s · %s", view->artist,
              view->selected_difficulty);
}

static auto workspace_content_flags(bool compose_visible) -> nk_flags {
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

static void render_workspace_content(const WormUiWorkspaceView& view,
                                     WormUiWorkspaceAction& action) {
    prepare_workspace_content_scroll(g_context, view.compose.visible);
    if (!nk_group_begin(g_context, "workspace-content",
                        workspace_content_flags(view.compose.visible)))
        return;
    if (view.setup.visible) {
        worm_ui_nuklear_render_setup_panel(g_context, &view.setup, &action);
    } else if (view.metadata.visible) {
        worm_ui_nuklear_render_metadata_panel(g_context, &view.metadata, &action,
                                              g_window_focused);
    } else if (view.timing.visible) {
        worm_ui_nuklear_render_timing_panel(g_context, &view.timing, &action,
                                            g_window_focused, g_double_clicks.data(),
                                            g_double_click_count);
    } else if (view.compose.visible) {
        worm_ui_nuklear_render_compose_panel(
            g_context, &view.compose, &view.timing, &action, g_window_focused,
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
    if (!worm_ui_nuklear_setup_view_valid(&view.setup)) return "Setup view data is invalid.";
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

static void render_transport_seek_control(const WormUiWorkspaceView& view,
                                         WormUiWorkspaceAction& action) {
    if (!view.transport_available || view.transport_end_frame == 0) return;
    constexpr nk_size seek_steps = 10'000;
    const auto bounded_frame = std::min(view.transport_frame, view.transport_end_frame);
    nk_size seek_position = static_cast<nk_size>(
        static_cast<long double>(bounded_frame) /
        static_cast<long double>(view.transport_end_frame) * seek_steps);
    nk_layout_row_dynamic(g_context, 16, 1);
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
    nk_layout_row_dynamic(g_context, 38, 1);
    nk_label_wrap(g_context, view.status_text);
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

static void render_new_project_wizard(WormUiWorkspaceAction& action, int width, int height) {
    if (!g_new_project_wizard.open) return;
    constexpr std::array<const char*, 8> layout_labels{
        "4K", "5K", "6K", "7K", "8K", "9K", "10K", "14K"};
    const float popup_width = std::min(500.0f, std::max(280.0f, width - 24.0f));
    const float popup_height = g_new_project_wizard.audio ? 266.0f : 204.0f;
    const auto region = nk_window_get_content_region(g_context);
    const auto bounds = nk_rect((width - popup_width) * 0.5f - region.x,
                                (height - popup_height) * 0.5f - region.y,
                                popup_width, popup_height);
    if (!nk_popup_begin(g_context, NK_POPUP_STATIC, "Create project",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        cancel_new_project_wizard(action);
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
    const auto selected = nk_combo(g_context, layout_labels.data(),
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
        nk_popup_close(g_context);
    }
    if (audio_path_too_long) nk_widget_disable_begin(g_context);
    if (nk_button_label(g_context, "Create project") && !audio_path_too_long) {
        confirm_new_project_wizard(action);
        nk_popup_close(g_context);
    }
    if (audio_path_too_long) nk_widget_disable_end(g_context);
    nk_popup_end(g_context);
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
    const auto region = nk_window_get_content_region(g_context);
    const auto bounds = nk_rect((width - popup_width) * 0.5f - region.x,
                                (height - popup_height) * 0.5f - region.y,
                                popup_width, popup_height);
    if (!nk_popup_begin(g_context, NK_POPUP_STATIC, "BMS text encoding",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        cancel_bms_encoding_dialog(action);
        return;
    }
    nk_layout_row_dynamic(g_context, 30, 1);
    nk_label(g_context, "Choose the BMS text encoding", NK_TEXT_LEFT);
    nk_layout_row_dynamic(g_context, 46, 1);
    nk_label_wrap(g_context, path.c_str());
    nk_layout_row_dynamic(g_context, 42, 1);
    nk_label_wrap(g_context, "This chart contains non-ASCII text without a UTF-8 BOM.");
    nk_layout_row_dynamic(g_context, 30, 1);
    g_bms_encoding_choice = nk_combo(g_context, labels.data(), count,
                                      g_bms_encoding_choice, 25, nk_vec2(250.0f, 180.0f));
    nk_layout_row_dynamic(g_context, 34, 2);
    if (nk_button_label(g_context, "Cancel")) {
        cancel_bms_encoding_dialog(action);
        nk_popup_close(g_context);
    }
    if (nk_button_label(g_context, "Open chart") &&
        action.kind == WORM_UI_WORKSPACE_ACTION_NONE &&
        path.size() < sizeof(action.project_path)) {
        action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        action.bms_encoding_index = indexes[static_cast<std::size_t>(g_bms_encoding_choice)];
        std::memcpy(action.project_path, path.c_str(), path.size() + 1);
        nk_popup_close(g_context);
    }
    nk_popup_end(g_context);
}

static void render_empty_project_window(const WorkspaceRenderContext& frame) {
    if (nk_begin(g_context, "wormedit-workspace",
                 nk_rect(0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        render_title_bar(&frame.view, &frame.action);
        const float recovery_height = render_recovery_panel(frame.view, frame.action);
        const float available_height = std::max(100.0f,
                                                static_cast<float>(frame.height) - 64.0f -
                                                    recovery_height);
        render_empty_project_content(frame.view, frame.action, available_height);
        if (!frame.view.pending_bms_encoding_path ||
            !frame.view.pending_bms_encoding_path[0])
            render_new_project_wizard(frame.action, frame.width, frame.height);
        render_bms_encoding_dialog(frame.view, frame.action, frame.width, frame.height);
    }
    nk_end(g_context);
}

static void render_gpu_work_summary(const WormUiFrameTimingView& timer) {
    const uint64_t latest[WORM_UI_FRAME_GPU_WORK_COUNT]{timer.gpu_work_latest.vertices,
        timer.gpu_work_latest.indices, timer.gpu_work_latest.draw_calls,
        timer.gpu_work_latest.clip_rect_changes, timer.gpu_work_latest.texture_switches};
    for (std::size_t metric = 0; metric < WORM_UI_FRAME_GPU_WORK_COUNT; ++metric) {
        const auto summary = timer.gpu_work[metric];
        nk_layout_row_dynamic(g_context, 22, 1);
        if (!timer.gpu_work_available || summary.count == 0)
            nk_labelf(g_context, NK_TEXT_LEFT, "%s: N/A", timer.gpu_work_names[metric]);
        else
            nk_labelf(g_context, NK_TEXT_LEFT, "%s: last %llu, avg %.0f, p99 %.0f",
                timer.gpu_work_names[metric], static_cast<unsigned long long>(latest[metric]),
                summary.average, summary.p99);
    }
}

static void render_frame_timing_overlay(int width, int height) {
    if (!g_frame_timing_visible) return;
    const auto timer = worm_ui_frame_timing_view();
    const float panel_width = std::min(480.0f, static_cast<float>(width));
    const float panel_height = std::min(720.0f, static_cast<float>(height) - 42.0f);
    if (nk_begin(g_context, "Frame timing",
                 nk_rect(static_cast<float>(width) - panel_width, 42, panel_width, panel_height),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE)) {
        const auto frame = timer.frame;
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_labelf(g_context, NK_TEXT_LEFT, "Last %zu frames: avg %.3f ms, p99 %.3f ms",
                  frame.count, frame.average_ms, frame.p99_ms);
        nk_layout_row_dynamic(g_context, 90, 1);
        const auto maximum_ms = timer.count == 0 ? 1.0 : std::max(1.0,
            *std::max_element(timer.frame_ms, timer.frame_ms + timer.count));
        if (nk_chart_begin(g_context, NK_CHART_LINES, static_cast<int>(timer.count), 0.0f,
                           static_cast<float>(maximum_ms))) {
            for (std::size_t i = 0; i < timer.count; ++i)
                nk_chart_push(g_context, static_cast<float>(timer.frame_ms[i]));
            nk_chart_end(g_context);
        }
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
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        render_title_bar(&view, &action);
        render_transport_seek_control(view, action);
        render_workspace_tabs(&view, &action);
        const float recovery_height = render_recovery_panel(view, action);
        float content_height = frame.height > 300
                                  ? static_cast<float>(frame.height) - 132.0f - recovery_height
                                  : 80.0f;
        if (content_height < 80.0f) content_height = 80.0f;
        nk_layout_row_dynamic(g_context, content_height, 1);
        render_workspace_content(view, action);
        render_status_bar(&view, &action);
        if (!view.pending_bms_encoding_path || !view.pending_bms_encoding_path[0])
            render_new_project_wizard(action, frame.width, frame.height);
        render_bms_encoding_dialog(view, action, frame.width, frame.height);
    }
    nk_end(g_context);
}

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
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
        render_workspace_error(&action, width, height, validation_error);
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
        worm_ui_nuklear_render_external_player_panel(g_context, &view->external_player, &action.external_player);
        expose_shortcut_input(&action);
        g_audio.offset_escape_pending = false;
        return action;
    }

    worm_ui_nuklear_emit_timing_cancellations(&view->timing, timing_tab_active, &action);

    const bool compose_tab_active = view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    g_compose_area_shortcuts_active = compose_tab_active && view->compose.visible &&
        (!view->pending_bms_encoding_path || !view->pending_bms_encoding_path[0]);
    worm_ui_nuklear_prepare_compose_panel(&view->compose, compose_tab_active, g_window_focused,
                                          &action);

    render_workspace_main_window(WorkspaceRenderContext{*view, action, width, height});
    render_frame_timing_overlay(width, height);
    worm_ui_nuklear_render_external_player_panel(g_context, &view->external_player, &action.external_player);
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
        g_gpu_submission = {};
        nk_sdl_render(g_context, NK_ANTI_ALIASING_ON);
        worm_ui_frame_timing_record_gpu_work(g_gpu_submission.work);
    }
}
