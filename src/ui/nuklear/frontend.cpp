#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#if defined(__linux__)
#define GL_GLEXT_PROTOTYPES
#endif

#include "ui/frontend.h"

#include "wormedit/nuklear_config.h"

#include "nuklear_sdl_gl3_sdl3.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <string_view>
#include <vector>

#define WORMEDIT_MAX_VERTEX_MEMORY (512 * 1024)
#define WORMEDIT_MAX_ELEMENT_MEMORY (128 * 1024)

static SDL_Window* g_window;
static struct nk_context* g_context;
static char g_recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
static constexpr size_t metadata_visible_tag_count = 64;
static uint64_t g_metadata_buffer_revision = std::numeric_limits<uint64_t>::max();
static uint64_t g_metadata_buffer_chart_id{};
static bool g_metadata_buffer_has_chart{};
static bool g_metadata_song_buffers_valid{};
static bool g_metadata_chart_buffers_valid{};
static bool g_metadata_tag_buffers_valid{};
static std::array<char, WORM_UI_METADATA_TITLE_BYTES + 1> g_metadata_title{};
static std::array<char, WORM_UI_METADATA_TITLE_BYTES + 1> g_metadata_artist{};
static std::array<char, WORM_UI_METADATA_CHART_TEXT_BYTES + 1> g_metadata_chart_name{};
static std::array<char, WORM_UI_METADATA_CHART_TEXT_BYTES + 1> g_metadata_chart_author{};
static std::array<char, 64> g_metadata_chart_rating_numerator{};
static std::array<char, 64> g_metadata_chart_rating_denominator{};
static std::vector<std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1>> g_metadata_search_tags;
static std::vector<const char*> g_metadata_search_tag_pointers;
static std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> g_metadata_new_search_tag{};
static size_t g_metadata_selected_format_field = std::numeric_limits<size_t>::max();
static bool g_metadata_format_target_valid{};
static bool g_metadata_format_target_chart_scoped{};
static uint8_t g_metadata_format_target_family{};
static size_t g_metadata_format_target_index{};
static size_t g_metadata_format_target_field_count{};
static bool g_metadata_format_insert_mode{};
static bool g_metadata_format_buffers_valid{};
static std::vector<char> g_metadata_format_name;
static std::vector<char> g_metadata_format_value;

struct compose_key_event_t {
    uint32_t keycode{};
    uint32_t scancode{};
    uint16_t modifiers{};
    bool down{};
    bool repeat{};
};

static std::array<compose_key_event_t, 64> g_compose_key_events{};
static size_t g_compose_key_event_count{};
struct compose_wheel_event_t {
    double delta_x{};
    double delta_y{};
    double mouse_x{};
    double mouse_y{};
    uint16_t modifiers{};
    bool coarse_steps{};
};

static std::array<compose_wheel_event_t, WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY>
    g_compose_wheel_events{};
static size_t g_compose_wheel_event_count{};
static bool g_compose_middle_click_pending{};
static float g_compose_middle_click_x{};
static float g_compose_middle_click_y{};
struct timing_double_click_t {
    float x{};
    float y{};
};
static std::array<timing_double_click_t, 16> g_timing_double_clicks{};
static size_t g_timing_double_click_count{};
static bool g_timing_stage_buffers_active{};
static uint64_t g_timing_stage_buffers_chart{};
static uint64_t g_timing_stage_buffers_revision{};
static uint64_t g_timing_stage_buffers_object{};
static WormUiTimingLaneKind g_timing_stage_buffers_lane{WORM_UI_TIMING_LANE_BPM};
static double g_timing_stage_buffers_y{};
static std::array<char, 64> g_timing_primary_numerator{};
static std::array<char, 64> g_timing_primary_denominator{};
static std::array<char, 64> g_timing_secondary_numerator{};
static std::array<char, 64> g_timing_secondary_denominator{};
static std::array<char, 32> g_timing_meter_measure{'1'};
static std::array<char, 32> g_timing_meter_numerator{'4'};
static std::array<char, 32> g_timing_meter_denominator{'4'};
static bool g_timing_recipe_buffers_active{};
static uint64_t g_timing_recipe_buffers_chart{};
static uint64_t g_timing_recipe_buffers_revision{};
static uint64_t g_timing_recipe_buffers_id{};
static WormUiTimingRecipeForm g_timing_recipe_form{};
static bool g_timing_recipe_range_drag_active{};
enum class timing_recipe_range_drag_mode_t {
    new_range,
    begin_handle,
    end_handle,
    whole_span,
};
static timing_recipe_range_drag_mode_t g_timing_recipe_range_drag_mode{
    timing_recipe_range_drag_mode_t::new_range};
static uint64_t g_timing_recipe_drag_chart{};
static uint64_t g_timing_recipe_drag_revision{};
static double g_timing_recipe_drag_anchor_fraction{};
static double g_timing_recipe_drag_current_fraction{};
static double g_timing_recipe_drag_original_begin{};
static double g_timing_recipe_drag_original_end{};
static float g_timing_recipe_drag_anchor_screen_y{};
static bool g_timing_recipe_drag_moved{};
static bool g_timing_recipe_cancel_pending{};
static bool g_timing_marker_drag_active{};
static bool g_timing_marker_drag_moved{};
static bool g_timing_marker_drag_cancel_pending{};
static uint64_t g_timing_marker_drag_chart{};
static uint64_t g_timing_marker_drag_revision{};
static uint64_t g_timing_marker_drag_object{};
static WormUiTimingLaneKind g_timing_marker_drag_lane{WORM_UI_TIMING_LANE_BPM};
static float g_timing_marker_drag_anchor_screen_y{};
static bool g_window_focused{true};
static std::array<WormUiShortcutEvent, WORM_UI_SHORTCUT_EVENT_CAPACITY> g_shortcut_events{};
static size_t g_shortcut_event_count{};
static bool g_shortcut_events_overflowed{};
static bool g_ime_composition_active{};
static bool g_compose_cancel_pending{};
static bool g_compose_keyboard_focus{};
static bool g_compose_action_overflowed{};
static bool g_compose_pointer_hold_active{};
static bool g_compose_note_paint_pointer_active{};
static bool g_compose_semantic_layer_paint_pointer_active{};
static bool g_compose_lane_hold_active{};
static bool g_compose_space_range_hold_active{};
static bool g_compose_space_range_hold_moved{};
static bool g_compose_note_move_candidate_active{};
static bool g_compose_note_move_active{};
static double g_compose_note_move_anchor_x{};
static double g_compose_note_move_anchor_y{};
static float g_compose_note_move_anchor_screen_x{};
static float g_compose_note_move_anchor_screen_y{};
static bool g_compose_area_selection_candidate_active{};
static bool g_compose_area_selection_active{};
static double g_compose_area_selection_anchor_x{};
static double g_compose_area_selection_anchor_y{};

static void cancel_timing_recipe_range_drag();
static void cancel_timing_marker_drag();
static float g_compose_area_selection_anchor_screen_x{};
static float g_compose_area_selection_anchor_screen_y{};
static WormUiComposeSelectionMode g_compose_area_selection_mode{WORM_UI_COMPOSE_SELECTION_REPLACE};
static uint32_t g_compose_lane_hold_keycode{};
static uint64_t g_compose_lane_hold_lane_id{};
static bool g_compose_navigation_key_active{};
static uint32_t g_compose_navigation_keycode{};
static WormUiComposeNavigationAction g_compose_navigation_action{
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
static bool g_compose_pointer_navigation_active{};
static WormUiComposeNavigationAction g_compose_pointer_navigation_action{
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
static uint64_t g_last_compose_chart_id{};
static uint64_t g_last_compose_document_revision{};
static uint64_t g_compose_label_chart_id{std::numeric_limits<uint64_t>::max()};
static uint64_t g_compose_label_document_revision{std::numeric_limits<uint64_t>::max()};
static uint64_t g_compose_label_note_id{std::numeric_limits<uint64_t>::max()};
static std::array<char, WORM_UI_COMPOSE_NOTE_LABEL_BYTES + 1> g_compose_note_label{};
static std::array<char, WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY> g_compose_semantic_layer_name{};
static uint64_t g_compose_semantic_layer_name_id{};
static uint64_t g_compose_semantic_layer_name_revision{std::numeric_limits<uint64_t>::max()};

static void append_compose_input(
    WormUiWorkspaceAction* action, WormUiComposeInputKind kind, double x_pixels = 0.0,
    double y_pixels = 0.0, uint64_t lane_id = 0, bool successive = false,
    WormUiComposeSelectionMode selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
    WormUiComposeNavigationAction navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD) {
    if (!action) {
        return;
    }
    if (action->compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        action->compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY - 1] = {
            .kind = WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
            .lane_id = 0,
            .successive = false,
            .selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
            .navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
            .x_pixels = 0.0,
            .y_pixels = 0.0,
        };
        action->compose_input_count = WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY;
        g_compose_cancel_pending = false;
        g_compose_pointer_hold_active = false;
        g_compose_note_paint_pointer_active = false;
        g_compose_semantic_layer_paint_pointer_active = false;
        g_compose_lane_hold_active = false;
        g_compose_space_range_hold_active = false;
        g_compose_space_range_hold_moved = false;
        g_compose_note_move_candidate_active = false;
        g_compose_note_move_active = false;
        g_compose_area_selection_candidate_active = false;
        g_compose_area_selection_active = false;
        g_compose_navigation_key_active = false;
        g_compose_navigation_keycode = 0;
        g_compose_pointer_navigation_active = false;
        g_compose_keyboard_focus = false;
        g_compose_lane_hold_keycode = 0;
        g_compose_lane_hold_lane_id = 0;
        g_compose_action_overflowed = true;
        return;
    }
    action->compose_inputs[action->compose_input_count++] = {
        .kind = kind,
        .lane_id = lane_id,
        .successive = successive,
        .selection_mode = selection_mode,
        .navigation_action = navigation_action,
        .x_pixels = x_pixels,
        .y_pixels = y_pixels,
    };
}

static void update_compose_pointer_navigation(
    WormUiWorkspaceAction* action,
    std::optional<WormUiComposeNavigationAction> desired_action) {
    if (!action) {
        return;
    }
    if (g_compose_pointer_navigation_active &&
        (!desired_action || *desired_action != g_compose_pointer_navigation_action)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                             false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             g_compose_pointer_navigation_action);
        g_compose_pointer_navigation_active = false;
    }
    if (desired_action && !g_compose_pointer_navigation_active) {
        if (g_compose_navigation_key_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 g_compose_navigation_action);
            g_compose_navigation_key_active = false;
            g_compose_navigation_keycode = 0;
        }
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0, false,
                             WORM_UI_COMPOSE_SELECTION_REPLACE, *desired_action);
        g_compose_pointer_navigation_active = !g_compose_action_overflowed;
        g_compose_pointer_navigation_action = *desired_action;
    }
}

static void cancel_compose_navigation(WormUiWorkspaceAction* action) {
    if (action && (g_compose_navigation_key_active || g_compose_pointer_navigation_active)) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
    }
    g_compose_navigation_key_active = false;
    g_compose_navigation_keycode = 0;
    g_compose_pointer_navigation_active = false;
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
    if (!event || g_compose_cancel_pending) {
        return;
    }
    if (g_compose_key_event_count == g_compose_key_events.size()) {
        g_compose_key_event_count = 0;
        g_compose_cancel_pending = true;
        g_compose_pointer_hold_active = false;
        g_compose_note_paint_pointer_active = false;
        g_compose_semantic_layer_paint_pointer_active = false;
        g_compose_lane_hold_active = false;
        g_compose_space_range_hold_active = false;
        g_compose_space_range_hold_moved = false;
        g_compose_note_move_candidate_active = false;
        g_compose_note_move_active = false;
        g_compose_area_selection_candidate_active = false;
        g_compose_area_selection_active = false;
        g_compose_keyboard_focus = false;
        g_compose_lane_hold_keycode = 0;
        g_compose_lane_hold_lane_id = 0;
        return;
    }
    g_compose_key_events[g_compose_key_event_count++] = {
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
    if (g_compose_wheel_event_count == g_compose_wheel_events.size()) {
        g_compose_wheel_event_count = 0;
        g_compose_cancel_pending = true;
        g_compose_key_event_count = 0;
        g_compose_pointer_hold_active = false;
        g_compose_note_paint_pointer_active = false;
        g_compose_semantic_layer_paint_pointer_active = false;
        g_compose_lane_hold_active = false;
        g_compose_space_range_hold_active = false;
        g_compose_space_range_hold_moved = false;
        g_compose_note_move_candidate_active = false;
        g_compose_note_move_active = false;
        g_compose_area_selection_candidate_active = false;
        g_compose_area_selection_active = false;
        g_compose_keyboard_focus = false;
        g_compose_lane_hold_keycode = 0;
        g_compose_lane_hold_lane_id = 0;
        return;
    }
    const auto whole_steps = std::trunc(event->x) == event->x && std::trunc(event->y) == event->y &&
                             (event->x != 0.0f || event->y != 0.0f);
    // Convert SDL's positive-away wheel convention into content-offset deltas.
    // FLIPPED sources already report the opposite sign, so they need no negation.
    const auto direction_sign = event->direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0 : -1.0;
    g_compose_wheel_events[g_compose_wheel_event_count++] = {
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

static int copy_metadata_text(char* destination, size_t capacity, const char* source,
                              size_t source_bytes) {
    if (!destination || capacity == 0 || !source || source_bytes >= capacity ||
        strlen(source) != source_bytes) {
        if (destination && capacity != 0) {
            destination[0] = '\0';
        }
        return 0;
    }
    memcpy(destination, source, source_bytes);
    destination[source_bytes] = '\0';
    return 1;
}

static void initialize_metadata_buffers(const WormUiMetadataView* metadata) {
    if (!metadata) {
        return;
    }
    g_metadata_buffer_revision = metadata->document_revision;
    g_metadata_buffer_has_chart = metadata->has_chart;
    g_metadata_buffer_chart_id = metadata->has_chart ? metadata->chart_id : 0;
    g_metadata_song_buffers_valid =
        metadata->song_fields_editable &&
        copy_metadata_text(g_metadata_title.data(), g_metadata_title.size(), metadata->title,
                           metadata->title_bytes) &&
        copy_metadata_text(g_metadata_artist.data(), g_metadata_artist.size(), metadata->artist,
                           metadata->artist_bytes);
    g_metadata_chart_buffers_valid =
        metadata->has_chart && metadata->chart_fields_editable &&
        copy_metadata_text(g_metadata_chart_name.data(), g_metadata_chart_name.size(),
                           metadata->chart_name, metadata->chart_name_bytes) &&
        copy_metadata_text(g_metadata_chart_author.data(), g_metadata_chart_author.size(),
                           metadata->chart_author, metadata->chart_author_bytes);
    g_metadata_chart_rating_numerator.fill('\0');
    g_metadata_chart_rating_denominator.fill('\0');
    if (metadata->has_chart) {
        const auto numerator = metadata->has_chart_rating
                                   ? std::to_string(metadata->chart_rating_numerator)
                                   : std::string{};
        const auto denominator = metadata->has_chart_rating
                                     ? std::to_string(metadata->chart_rating_denominator)
                                     : std::string{"1"};
        (void)copy_metadata_text(g_metadata_chart_rating_numerator.data(),
                                 g_metadata_chart_rating_numerator.size(), numerator.c_str(),
                                 numerator.size());
        (void)copy_metadata_text(g_metadata_chart_rating_denominator.data(),
                                 g_metadata_chart_rating_denominator.size(), denominator.c_str(),
                                 denominator.size());
    }
    g_metadata_tag_buffers_valid =
        metadata->search_tags_editable &&
        metadata->search_tag_count <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
        (metadata->search_tag_count == 0 || (metadata->search_tags && metadata->search_tag_bytes));
    g_metadata_search_tags.clear();
    if (g_metadata_tag_buffers_valid) {
        g_metadata_search_tags.reserve(metadata->search_tag_count);
        for (size_t index = 0; index < metadata->search_tag_count; ++index) {
            std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> tag{};
            if (!copy_metadata_text(tag.data(), tag.size(), metadata->search_tags[index],
                                    metadata->search_tag_bytes[index])) {
                g_metadata_tag_buffers_valid = 0;
                g_metadata_search_tags.clear();
                break;
            }
            g_metadata_search_tags.push_back(tag);
        }
    }
    g_metadata_search_tag_pointers.clear();
    g_metadata_new_search_tag.fill('\0');
    g_metadata_selected_format_field = std::numeric_limits<size_t>::max();
    g_metadata_format_target_valid = 0;
    g_metadata_format_target_chart_scoped = 0;
    g_metadata_format_target_family = 0;
    g_metadata_format_target_index = 0;
    g_metadata_format_target_field_count = 0;
    g_metadata_format_insert_mode = 0;
    g_metadata_format_buffers_valid = 0;
    g_metadata_format_name.clear();
    g_metadata_format_value.clear();
}

static int metadata_buffer_key_changed(const WormUiMetadataView* metadata) {
    const uint64_t chart_id = metadata && metadata->has_chart ? metadata->chart_id : 0;
    const int has_chart = metadata && metadata->has_chart;
    return metadata &&
           (g_metadata_buffer_revision != metadata->document_revision ||
            g_metadata_buffer_chart_id != chart_id || g_metadata_buffer_has_chart != has_chart);
}

static int metadata_song_changed(const WormUiMetadataView* metadata) {
    if (!metadata || !g_metadata_song_buffers_valid || !g_metadata_tag_buffers_valid ||
        !metadata->title || !metadata->artist) {
        return 0;
    }
    if (metadata->search_tag_count != g_metadata_search_tags.size()) {
        return 1;
    }
    if (strcmp(g_metadata_title.data(), metadata->title) != 0 ||
        strcmp(g_metadata_artist.data(), metadata->artist) != 0) {
        return 1;
    }
    for (size_t index = 0; index < g_metadata_search_tags.size(); ++index) {
        if (!metadata->search_tags ||
            strcmp(g_metadata_search_tags[index].data(), metadata->search_tags[index]) != 0) {
            return 1;
        }
    }
    return 0;
}

struct metadata_rating_value_t {
    bool has_rating{};
    int64_t numerator{};
    uint64_t denominator{1};
};

static auto metadata_chart_rating_value(const WormUiMetadataView* metadata)
    -> std::optional<metadata_rating_value_t> {
    if (!metadata)
        return std::nullopt;
    const std::string_view numerator{g_metadata_chart_rating_numerator.data()};
    const std::string_view denominator{g_metadata_chart_rating_denominator.data()};
    if (numerator.empty() && !metadata->has_chart_rating && denominator == "1") {
        return metadata_rating_value_t{};
    }
    if (numerator.empty() || denominator.empty() || numerator.size() > 21 ||
        denominator.size() > 21) {
        return std::nullopt;
    }

    const auto numerator_digits =
        numerator.front() == '+' || numerator.front() == '-' ? numerator.substr(1) : numerator;
    if (numerator_digits.empty() || numerator_digits.size() > 20)
        return std::nullopt;
    auto numerator_text = numerator;
    if (numerator_text.front() == '+')
        numerator_text.remove_prefix(1);
    int64_t parsed_numerator{};
    const auto numerator_result = std::from_chars(
        numerator_text.data(), numerator_text.data() + numerator_text.size(), parsed_numerator, 10);
    if (numerator_result.ec != std::errc{} ||
        numerator_result.ptr != numerator_text.data() + numerator_text.size()) {
        return std::nullopt;
    }

    auto denominator_text = denominator;
    if (denominator_text.front() == '+')
        denominator_text.remove_prefix(1);
    if (denominator_text.empty() || denominator_text.size() > 20)
        return std::nullopt;
    uint64_t parsed_denominator{};
    const auto denominator_result =
        std::from_chars(denominator_text.data(), denominator_text.data() + denominator_text.size(),
                        parsed_denominator, 10);
    if (denominator_result.ec != std::errc{} ||
        denominator_result.ptr != denominator_text.data() + denominator_text.size() ||
        parsed_denominator == 0) {
        return std::nullopt;
    }
    return metadata_rating_value_t{true, parsed_numerator, parsed_denominator};
}

static int metadata_chart_changed(const WormUiMetadataView* metadata) {
    if (!metadata || !metadata->has_chart || !g_metadata_chart_buffers_valid ||
        !metadata->chart_name || !metadata->chart_author) {
        return 0;
    }
    const bool text_changed = strcmp(g_metadata_chart_name.data(), metadata->chart_name) != 0 ||
                              strcmp(g_metadata_chart_author.data(), metadata->chart_author) != 0;
    const auto rating = metadata_chart_rating_value(metadata);
    const bool rating_changed =
        rating &&
        (rating->has_rating != metadata->has_chart_rating ||
         (rating->has_rating && (rating->numerator != metadata->chart_rating_numerator ||
                                 rating->denominator != metadata->chart_rating_denominator)));
    return text_changed || rating_changed;
}

static void metadata_input_row(const char* label, char* buffer, size_t capacity, int enabled) {
    nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(g_context, 0.20f);
    nk_label(g_context, label, NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.80f);
    if (!enabled) {
        nk_widget_disable_begin(g_context);
    }
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, buffer, capacity,
                                         nk_filter_default);
    if (!enabled) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_end(g_context);
}

static void set_metadata_song_action(WormUiWorkspaceAction* action,
                                     const WormUiMetadataView* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !g_metadata_tag_buffers_valid ||
        g_metadata_search_tags.size() > WORM_UI_METADATA_SEARCH_TAG_CAPACITY) {
        return;
    }
    g_metadata_search_tag_pointers.clear();
    g_metadata_search_tag_pointers.reserve(g_metadata_search_tags.size());
    for (const auto& tag : g_metadata_search_tags) {
        g_metadata_search_tag_pointers.push_back(tag.data());
    }
    action->metadata_edit = WORM_UI_METADATA_EDIT_SONG;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_title = g_metadata_title.data();
    action->metadata_artist = g_metadata_artist.data();
    action->metadata_search_tags = g_metadata_search_tag_pointers.data();
    action->metadata_search_tag_count = g_metadata_search_tag_pointers.size();
}

static void set_metadata_chart_action(WormUiWorkspaceAction* action,
                                      const WormUiMetadataView* metadata, int unset_rating) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !metadata->has_chart || !g_metadata_chart_buffers_valid) {
        return;
    }
    const auto rating = unset_rating ? std::optional<metadata_rating_value_t>{}
                                     : metadata_chart_rating_value(metadata);
    if (!unset_rating && !rating)
        return;
    action->metadata_edit = WORM_UI_METADATA_EDIT_CHART;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = metadata->chart_id;
    action->metadata_chart_name = g_metadata_chart_name.data();
    action->metadata_chart_author = g_metadata_chart_author.data();
    action->metadata_has_chart_rating = !unset_rating && rating->has_rating;
    if (action->metadata_has_chart_rating) {
        action->metadata_chart_rating_numerator_text = g_metadata_chart_rating_numerator.data();
        action->metadata_chart_rating_denominator_text = g_metadata_chart_rating_denominator.data();
    }
}

static const char* metadata_format_family_name(uint8_t family) {
    static const char* names[] = {"Native", "BMS",  "StepMania", "osu!mania",
                                  "BMSON",  "MIDI", "Quaver"};
    return family < sizeof(names) / sizeof(names[0]) ? names[family] : "Unknown";
}

static int initialize_format_buffers(const WormUiMetadataFormatFieldView* field, int insert_mode) {
    g_metadata_format_name.assign(WORM_UI_METADATA_FORMAT_NAME_BYTES + 1, '\0');
    g_metadata_format_value.assign(WORM_UI_METADATA_FORMAT_VALUE_BYTES + 1, '\0');
    g_metadata_format_insert_mode = insert_mode != 0;
    if (insert_mode) {
        g_metadata_format_buffers_valid = field && field->first_group_for_family;
        return g_metadata_format_buffers_valid;
    }
    if (!field || !field->first_group_for_family || !field->name || !field->value ||
        field->group_field_count > WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY) {
        g_metadata_format_buffers_valid = 0;
        return 0;
    }
    g_metadata_format_buffers_valid =
        copy_metadata_text(g_metadata_format_name.data(), g_metadata_format_name.size(),
                           field->name, field->name_bytes) &&
        copy_metadata_text(g_metadata_format_value.data(), g_metadata_format_value.size(),
                           field->value, field->value_bytes);
    return g_metadata_format_buffers_valid;
}

static void initialize_format_target_buffers(const WormUiMetadataView* metadata,
                                             const WormUiMetadataFormatTargetView* target) {
    if (!metadata || !target || target->family > 6 ||
        (target->chart_scoped && !metadata->has_chart) ||
        target->field_count > WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY) {
        g_metadata_format_target_valid = 0;
        g_metadata_format_buffers_valid = 0;
        return;
    }
    g_metadata_selected_format_field = std::numeric_limits<size_t>::max();
    g_metadata_format_target_valid = 1;
    g_metadata_format_target_chart_scoped = target->chart_scoped;
    g_metadata_format_target_family = target->family;
    g_metadata_format_target_field_count = target->field_count;
    g_metadata_format_target_index = target->field_count;
    g_metadata_format_insert_mode = 1;
    g_metadata_format_name.assign(WORM_UI_METADATA_FORMAT_NAME_BYTES + 1, '\0');
    g_metadata_format_value.assign(WORM_UI_METADATA_FORMAT_VALUE_BYTES + 1, '\0');
    g_metadata_format_buffers_valid = 1;
}

static void set_metadata_format_action(WormUiWorkspaceAction* action,
                                       const WormUiMetadataView* metadata,
                                       WormUiMetadataEditKind kind) {
    const int needs_text = kind != WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD;
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !g_metadata_format_target_valid || (needs_text && !g_metadata_format_buffers_valid) ||
        g_metadata_format_target_family > 6 ||
        (g_metadata_format_target_chart_scoped && !metadata->has_chart)) {
        return;
    }
    action->metadata_edit = kind;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = g_metadata_format_target_chart_scoped ? metadata->chart_id : 0;
    action->metadata_format_chart_scoped = g_metadata_format_target_chart_scoped;
    action->metadata_format_family = g_metadata_format_target_family;
    action->metadata_format_index = g_metadata_format_target_index;
    action->metadata_format_name = needs_text ? g_metadata_format_name.data() : nullptr;
    action->metadata_format_value = needs_text ? g_metadata_format_value.data() : nullptr;
}

static void render_metadata_editor(const WormUiMetadataView* metadata,
                                   WormUiWorkspaceAction* action) {
    if (!metadata || !metadata->visible) {
        return;
    }
    if (metadata_buffer_key_changed(metadata)) {
        initialize_metadata_buffers(metadata);
    }

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context, "Song metadata", NK_TEXT_LEFT);
    metadata_input_row("Title", g_metadata_title.data(), g_metadata_title.size(),
                       metadata->song_fields_editable && g_metadata_song_buffers_valid);
    metadata_input_row("Artist", g_metadata_artist.data(), g_metadata_artist.size(),
                       metadata->song_fields_editable && g_metadata_song_buffers_valid);

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_labelf(g_context, NK_TEXT_LEFT, "Ordered search tags (%zu; duplicates are preserved)",
              metadata->search_tag_count);
    const int tags_editable = metadata->song_fields_editable && g_metadata_song_buffers_valid &&
                              g_metadata_tag_buffers_valid;
    if (!g_metadata_tag_buffers_valid || !metadata->song_fields_editable) {
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_label(g_context, "Tags are read-only because the song metadata exceeds an edit cap.",
                 NK_TEXT_LEFT);
    } else {
        const size_t visible_tags =
            std::min(g_metadata_search_tags.size(), metadata_visible_tag_count);
        for (size_t index = 0; index < visible_tags; ++index) {
            char remove_label[32];
            (void)snprintf(remove_label, sizeof(remove_label), "Remove %zu", index + 1);
            nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 3);
            nk_layout_row_push(g_context, 0.10f);
            nk_labelf(g_context, NK_TEXT_LEFT, "%zu", index + 1);
            nk_layout_row_push(g_context, 0.75f);
            if (!tags_editable) {
                nk_widget_disable_begin(g_context);
            }
            (void)nk_edit_string_zero_terminated(
                g_context, NK_EDIT_FIELD, g_metadata_search_tags[index].data(),
                g_metadata_search_tags[index].size(), nk_filter_default);
            if (!tags_editable) {
                nk_widget_disable_end(g_context);
            }
            nk_layout_row_push(g_context, 0.15f);
            if (!tags_editable) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, remove_label)) {
                g_metadata_search_tags.erase(g_metadata_search_tags.begin() +
                                             static_cast<std::ptrdiff_t>(index));
                if (!tags_editable) {
                    nk_widget_disable_end(g_context);
                }
                nk_layout_row_end(g_context);
                break;
            }
            if (!tags_editable) {
                nk_widget_disable_end(g_context);
            }
            nk_layout_row_end(g_context);
        }
        if (g_metadata_search_tags.size() > metadata_visible_tag_count) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_labelf(g_context, NK_TEXT_LEFT, "Showing first %zu tags; the rest stay unchanged.",
                      metadata_visible_tag_count);
        }
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(g_context, 0.82f);
        if (!tags_editable) {
            nk_widget_disable_begin(g_context);
        }
        (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD,
                                             g_metadata_new_search_tag.data(),
                                             g_metadata_new_search_tag.size(), nk_filter_default);
        if (!tags_editable) {
            nk_widget_disable_end(g_context);
        }
        nk_layout_row_push(g_context, 0.18f);
        const int can_add_tag =
            tags_editable && g_metadata_search_tags.size() < WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
            g_metadata_new_search_tag[0] != '\0';
        if (!can_add_tag) {
            nk_widget_disable_begin(g_context);
        }
        const int add_tag = nk_button_label(g_context, "Add tag");
        if (!can_add_tag) {
            nk_widget_disable_end(g_context);
        }
        if (add_tag && can_add_tag) {
            g_metadata_search_tags.push_back(g_metadata_new_search_tag);
            g_metadata_new_search_tag.fill('\0');
        }
        nk_layout_row_end(g_context);
    }

    const int song_changed = metadata_song_changed(metadata);
    nk_layout_row_dynamic(g_context, 26, 1);
    if (!song_changed || !metadata->song_fields_editable) {
        nk_widget_disable_begin(g_context);
    }
    if (nk_button_label(g_context, "Apply song metadata") && song_changed &&
        metadata->song_fields_editable) {
        set_metadata_song_action(action, metadata);
    }
    if (!song_changed || !metadata->song_fields_editable) {
        nk_widget_disable_end(g_context);
    }

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context,
             metadata->has_chart ? "Selected difficulty metadata"
                                 : "Select a difficulty to edit chart metadata",
             NK_TEXT_LEFT);
    if (metadata->has_chart) {
        metadata_input_row("Difficulty", g_metadata_chart_name.data(), g_metadata_chart_name.size(),
                           metadata->chart_fields_editable && g_metadata_chart_buffers_valid);
        metadata_input_row("Mapper", g_metadata_chart_author.data(), g_metadata_chart_author.size(),
                           metadata->chart_fields_editable && g_metadata_chart_buffers_valid);
        nk_layout_row_dynamic(g_context, 24, 1);
        nk_label(g_context, "Exact signed rational rating (stored without reduction)",
                 NK_TEXT_LEFT);
        const int rating_fields_editable =
            metadata->chart_fields_editable && g_metadata_chart_buffers_valid;
        metadata_input_row("Numerator", g_metadata_chart_rating_numerator.data(),
                           g_metadata_chart_rating_numerator.size(), rating_fields_editable);
        metadata_input_row("Denominator", g_metadata_chart_rating_denominator.data(),
                           g_metadata_chart_rating_denominator.size(), rating_fields_editable);
        const auto parsed_rating = metadata_chart_rating_value(metadata);
        const bool rating_input_started =
            g_metadata_chart_rating_numerator[0] != '\0' ||
            strcmp(g_metadata_chart_rating_denominator.data(), "1") != 0;
        if (!parsed_rating && (metadata->has_chart_rating || rating_input_started)) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context,
                     "Use a signed 64-bit numerator and positive 64-bit denominator; values are "
                     "not truncated or reduced.",
                     NK_TEXT_LEFT);
        }
        const int chart_changed = metadata_chart_changed(metadata);
        const int can_apply_chart = chart_changed && parsed_rating.has_value() &&
                                    metadata->chart_fields_editable &&
                                    g_metadata_chart_buffers_valid && action &&
                                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        const int can_unset_rating = metadata->has_chart_rating &&
                                     metadata->chart_fields_editable &&
                                     g_metadata_chart_buffers_valid && action &&
                                     action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(g_context, 0.5f);
        if (!can_apply_chart) {
            nk_widget_disable_begin(g_context);
        }
        const int apply_chart = nk_button_label(g_context, "Apply difficulty metadata");
        if (!can_apply_chart) {
            nk_widget_disable_end(g_context);
        }
        if (apply_chart && can_apply_chart) {
            set_metadata_chart_action(action, metadata, 0);
        }
        nk_layout_row_push(g_context, 0.5f);
        if (!can_unset_rating) {
            nk_widget_disable_begin(g_context);
        }
        const int unset_rating = nk_button_label(g_context, "Unset rating");
        if (!can_unset_rating) {
            nk_widget_disable_end(g_context);
        }
        if (unset_rating && can_unset_rating) {
            set_metadata_chart_action(action, metadata, 1);
        }
        nk_layout_row_end(g_context);
    }

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_labelf(g_context, NK_TEXT_LEFT, "Format-specific fields (%zu; duplicates stay ordered)",
              metadata->format_field_total_count);
    if (metadata->format_field_count != 0 && !metadata->format_fields) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Format field list is unavailable.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < metadata->format_field_count; ++index) {
        const WormUiMetadataFormatFieldView* field = &metadata->format_fields[index];
        char edit_label[32];
        (void)snprintf(edit_label, sizeof(edit_label), "Edit %zu", index + 1);
        nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(g_context, 0.78f);
        nk_labelf(g_context, NK_TEXT_LEFT, "%s · %s · %s #%zu%s",
                  field->chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(field->family), field->name ? field->name : "(empty)",
                  field->field_index + 1, field->has_source_order ? " (source)" : "");
        nk_layout_row_push(g_context, 0.22f);
        const int field_editable = field->first_group_for_family && field->family <= 6 &&
                                   (!field->chart_scoped || metadata->has_chart);
        if (!field_editable) {
            nk_widget_disable_begin(g_context);
        }
        if (nk_button_label(g_context, edit_label) && field_editable) {
            g_metadata_selected_format_field = index;
            g_metadata_format_target_valid = 1;
            g_metadata_format_target_chart_scoped = field->chart_scoped;
            g_metadata_format_target_family = field->family;
            g_metadata_format_target_index = field->field_index;
            g_metadata_format_target_field_count = field->group_field_count;
            initialize_format_buffers(field, 0);
        }
        if (!field_editable) {
            nk_widget_disable_end(g_context);
        }
        nk_layout_row_end(g_context);
    }
    if (metadata->format_fields_truncated) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Additional format fields are hidden by the frontend row cap.",
                 NK_TEXT_LEFT);
    }
    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context, "Add a field to a format group", NK_TEXT_LEFT);
    if (metadata->format_target_count != 0 && !metadata->format_targets) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Format group targets are unavailable.", NK_TEXT_LEFT);
    } else {
        for (size_t index = 0; index < metadata->format_target_count; ++index) {
            const WormUiMetadataFormatTargetView* target = &metadata->format_targets[index];
            char add_label[32];
            (void)snprintf(add_label, sizeof(add_label), "Add field %zu", index + 1);
            nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
            nk_layout_row_push(g_context, 0.76f);
            nk_labelf(g_context, NK_TEXT_LEFT, "%s · %s · %s (%zu fields)",
                      target->chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(target->family),
                      target->group_exists ? "existing group" : "new group", target->field_count);
            nk_layout_row_push(g_context, 0.24f);
            const int target_editable =
                target->family <= 6 && (!target->chart_scoped || metadata->has_chart) &&
                target->field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
            if (!target_editable) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, add_label) && target_editable) {
                initialize_format_target_buffers(metadata, target);
            }
            if (!target_editable) {
                nk_widget_disable_end(g_context);
            }
            nk_layout_row_end(g_context);
        }
    }
    if (g_metadata_format_target_valid) {
        const WormUiMetadataFormatFieldView* field =
            g_metadata_selected_format_field < metadata->format_field_count
                ? &metadata->format_fields[g_metadata_selected_format_field]
                : nullptr;
        nk_layout_row_dynamic(g_context, 24, 1);
        if (field && !g_metadata_format_insert_mode) {
            nk_labelf(g_context, NK_TEXT_LEFT, "%s · %s field #%zu",
                      field->chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(field->family), field->field_index + 1);
        } else {
            nk_labelf(g_context, NK_TEXT_LEFT, "New %s · %s field",
                      g_metadata_format_target_chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(g_metadata_format_target_family));
        }
        if (!g_metadata_format_buffers_valid) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context,
                     "Field is read-only because it exceeds the text cap or group scope.",
                     NK_TEXT_LEFT);
            if (field && field->first_group_for_family) {
                nk_layout_row_dynamic(g_context, 26, 1);
                if (nk_button_label(g_context, "Delete field to repair metadata") &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
                }
            }
        } else {
            metadata_input_row("Name", g_metadata_format_name.data(), g_metadata_format_name.size(),
                               1);
            metadata_input_row("Value", g_metadata_format_value.data(),
                               g_metadata_format_value.size(), 1);
            if (g_metadata_format_insert_mode) {
                nk_layout_row_dynamic(g_context, 26, 1);
                const int insert_enabled = g_metadata_format_target_field_count <
                                           WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
                if (!insert_enabled) {
                    nk_widget_disable_begin(g_context);
                }
                if (nk_button_label(g_context, "Insert field at end of this format group") &&
                    insert_enabled && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD);
                }
                if (!insert_enabled) {
                    nk_widget_disable_end(g_context);
                }
            } else {
                nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 3);
                nk_layout_row_push(g_context, 0.34f);
                if (nk_button_label(g_context, "Replace field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD);
                }
                nk_layout_row_push(g_context, 0.33f);
                if (nk_button_label(g_context, "Delete field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
                }
                nk_layout_row_push(g_context, 0.33f);
                if (nk_button_label(g_context, "Prepare new field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    g_metadata_format_target_index = field->group_field_count;
                    initialize_format_buffers(field, 1);
                }
                nk_layout_row_end(g_context);
            }
        }
    }
}

bool worm_ui_frontend_init(SDL_Window* window) {
    if (!window || g_context) {
        return false;
    }

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
    g_compose_key_event_count = 0;
    g_compose_cancel_pending = false;
    g_compose_keyboard_focus = false;
    g_compose_pointer_hold_active = false;
    g_compose_note_paint_pointer_active = false;
    g_compose_semantic_layer_paint_pointer_active = false;
    g_compose_semantic_layer_name_id = 0;
    g_compose_semantic_layer_name_revision = std::numeric_limits<uint64_t>::max();
    g_compose_lane_hold_active = false;
    g_compose_space_range_hold_active = false;
    g_compose_space_range_hold_moved = false;
    g_compose_note_move_candidate_active = false;
    g_compose_note_move_active = false;
    g_compose_area_selection_candidate_active = false;
    g_compose_area_selection_active = false;
    g_compose_wheel_event_count = 0;
    g_compose_middle_click_pending = false;
    g_timing_double_click_count = 0;
    g_timing_stage_buffers_active = false;
    g_timing_recipe_buffers_active = false;
    g_timing_recipe_range_drag_active = false;
    g_timing_recipe_drag_moved = false;
    g_timing_recipe_cancel_pending = false;
    g_timing_marker_drag_active = false;
    g_timing_marker_drag_moved = false;
    g_timing_marker_drag_cancel_pending = false;
    g_last_compose_chart_id = 0;
    g_last_compose_document_revision = 0;
    return true;
}

void worm_ui_frontend_shutdown(void) {
    if (!g_context) {
        return;
    }
    if (g_window) {
        SDL_StopTextInput(g_window);
    }
    nk_sdl_shutdown();
    g_window = NULL;
    g_context = NULL;
    g_shortcut_event_count = 0;
    g_shortcut_events_overflowed = false;
    g_ime_composition_active = false;
    g_compose_key_event_count = 0;
    g_compose_wheel_event_count = 0;
    g_compose_middle_click_pending = false;
    g_timing_double_click_count = 0;
    g_timing_stage_buffers_active = false;
    g_timing_recipe_buffers_active = false;
    g_timing_recipe_range_drag_active = false;
    g_timing_recipe_drag_moved = false;
    g_timing_recipe_cancel_pending = false;
    g_timing_marker_drag_active = false;
    g_timing_marker_drag_moved = false;
    g_timing_marker_drag_cancel_pending = false;
    g_compose_pointer_hold_active = false;
    g_compose_note_paint_pointer_active = false;
    g_compose_semantic_layer_paint_pointer_active = false;
    g_compose_lane_hold_active = false;
    g_compose_area_selection_candidate_active = false;
    g_compose_area_selection_active = false;
    g_compose_navigation_key_active = false;
    g_compose_navigation_keycode = 0;
    g_compose_pointer_navigation_active = false;
    g_compose_keyboard_focus = false;
    g_last_compose_chart_id = 0;
    g_last_compose_document_revision = 0;
}

void worm_ui_frontend_begin_input(void) {
    if (g_context) {
        nk_input_begin(g_context);
        g_shortcut_event_count = 0;
        g_shortcut_events_overflowed = false;
        g_timing_double_click_count = 0;
    }
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        g_window_focused = false;
        g_compose_cancel_pending = true;
        g_compose_key_event_count = 0;
        g_shortcut_event_count = 0;
        g_compose_wheel_event_count = 0;
        g_compose_middle_click_pending = false;
        g_timing_double_click_count = 0;
        g_timing_recipe_cancel_pending = true;
        cancel_timing_recipe_range_drag();
        if (g_timing_marker_drag_active) {
            g_timing_marker_drag_cancel_pending = true;
        }
        cancel_timing_marker_drag();
        g_ime_composition_active = false;
        g_compose_keyboard_focus = false;
        g_compose_pointer_hold_active = false;
        // Keep paint capture until compose can deliver the pending cancel.
        g_compose_lane_hold_active = false;
        g_compose_space_range_hold_active = false;
        g_compose_space_range_hold_moved = false;
        g_compose_note_move_candidate_active = false;
        g_compose_note_move_active = false;
        g_compose_area_selection_candidate_active = false;
        g_compose_area_selection_active = false;
        g_compose_navigation_key_active = false;
        g_compose_navigation_keycode = 0;
        g_compose_pointer_navigation_active = false;
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
        g_compose_middle_click_pending = true;
        g_compose_middle_click_x = event->button.x;
        g_compose_middle_click_y = event->button.y;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.down &&
               event->button.button == SDL_BUTTON_LEFT && event->button.clicks >= 2 &&
               g_window_focused && g_window &&
               (event->button.windowID == 0 ||
                event->button.windowID == SDL_GetWindowID(g_window)) &&
               std::isfinite(event->button.x) && std::isfinite(event->button.y) &&
               g_timing_double_click_count < g_timing_double_clicks.size()) {
        g_timing_double_clicks[g_timing_double_click_count++] = {event->button.x, event->button.y};
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
        view->branch_row_count > WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY ||
        view->branch_diagnostic_count > WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY ||
        view->semantic_layer_count > WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY ||
        (view->branch_row_count != 0 && !view->branch_rows))
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
    for (size_t index = 0; index < view->semantic_layer_count; ++index) {
        if (!view->semantic_layers || view->semantic_layers[index].layer_id == 0 ||
            !view->semantic_layers[index].name) {
            return 0;
        }
    }
    if (view->hover_note_visible &&
        (!view->hover_tooltip || view->hover_note_id == 0)) {
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
        g_window_focused && g_compose_keyboard_focus && view->scene_available && g_context;
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
        g_compose_key_event_count = 0;
        return;
    }
    if (pointer_paint_mode_active && g_compose_lane_hold_active) {
        append_compose_cancel_once(action);
        g_compose_lane_hold_active = false;
        g_compose_lane_hold_keycode = 0;
        g_compose_lane_hold_lane_id = 0;
    }
    if (pointer_paint_mode_active && g_compose_space_range_hold_active) {
        append_compose_cancel_once(action);
        g_compose_space_range_hold_active = false;
        g_compose_space_range_hold_moved = false;
    }
    if (pointer_paint_mode_active &&
        (g_compose_note_move_candidate_active || g_compose_note_move_active)) {
        if (g_compose_note_move_active) {
            append_compose_cancel_once(action);
        }
        g_compose_note_move_candidate_active = false;
        g_compose_note_move_active = false;
    }
    for (size_t event_index = 0;
         event_index < g_compose_key_event_count && !g_compose_action_overflowed; ++event_index) {
        const auto& key_event = g_compose_key_events[event_index];
        WormUiComposeNavigationAction navigation_action{};
        if (!key_event.down && g_compose_navigation_key_active &&
            key_event.scancode == g_compose_navigation_keycode) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 g_compose_navigation_action);
            g_compose_navigation_key_active = false;
            g_compose_navigation_keycode = 0;
            continue;
        }
        if (g_compose_lane_hold_active && !key_event.down &&
            key_event.scancode == g_compose_lane_hold_keycode) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 0.0, 0.0,
                                 g_compose_lane_hold_lane_id);
            g_compose_lane_hold_active = false;
            g_compose_lane_hold_keycode = 0;
            g_compose_lane_hold_lane_id = 0;
            continue;
        }
        if (g_compose_space_range_hold_active && !key_event.down &&
            key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
            if (g_compose_space_range_hold_moved) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION);
            }
            g_compose_space_range_hold_active = false;
            g_compose_space_range_hold_moved = false;
            continue;
        }
        if (!keyboard_ready || !g_compose_keyboard_focus || !key_event.down || key_event.repeat) {
            continue;
        }
        const bool shortcut = compose_shortcut_modifier(key_event.modifiers);
        if (key_event.keycode == static_cast<uint32_t>(SDLK_ESCAPE)) {
            if (g_compose_space_range_hold_active) {
                append_compose_cancel_once(action);
                g_compose_space_range_hold_active = false;
                g_compose_space_range_hold_moved = false;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_SPACE)) {
            constexpr auto any_space_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                                SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
            if (!pointer_paint_mode_active && !g_compose_space_range_hold_active &&
                (key_event.modifiers & any_space_modifier) == 0) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION, 0.0, 0.0,
                                     0, false, WORM_UI_COMPOSE_SELECTION_REPLACE);
                g_compose_space_range_hold_active =
                    !view->space_range_preview_visible && !g_compose_action_overflowed;
                g_compose_space_range_hold_moved = false;
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
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE, navigation_action);
            g_compose_navigation_key_active = !g_compose_action_overflowed;
            g_compose_navigation_keycode = key_event.scancode;
            g_compose_navigation_action = navigation_action;
            if (!g_compose_action_overflowed && g_compose_space_range_hold_active) {
                g_compose_space_range_hold_moved = true;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_DOWN)) {
            if (!compose_navigation_action_for_key(key_event.keycode, key_event.modifiers,
                                                   &navigation_action)) {
                continue;
            }
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE, navigation_action);
            g_compose_navigation_key_active = !g_compose_action_overflowed;
            g_compose_navigation_keycode = key_event.scancode;
            g_compose_navigation_action = navigation_action;
            if (!g_compose_action_overflowed && g_compose_space_range_hold_active) {
                g_compose_space_range_hold_moved = true;
            }
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_PAGEUP)) {
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD);
            g_compose_navigation_key_active = !g_compose_action_overflowed;
            g_compose_navigation_keycode = key_event.scancode;
            g_compose_navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD;
            continue;
        }
        if (key_event.keycode == static_cast<uint32_t>(SDLK_PAGEDOWN)) {
            update_compose_pointer_navigation(action, std::nullopt);
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS, 0.0, 0.0, 0,
                                 false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                                 WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD);
            g_compose_navigation_key_active = !g_compose_action_overflowed;
            g_compose_navigation_keycode = key_event.scancode;
            g_compose_navigation_action = WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD;
            continue;
        }
        if (pointer_paint_mode_active) {
            continue;
        }
        if (g_compose_lane_hold_active ||
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
                g_compose_lane_hold_active = true;
                g_compose_lane_hold_keycode = key_event.scancode;
                g_compose_lane_hold_lane_id = binding.lane_id;
            }
            break;
        }
    }
    g_compose_key_event_count = 0;
}

static int timing_view_valid(const WormUiTimingView* view) {
    if (!view || view->speed_count > WORM_UI_TIMING_SPEED_CAPACITY ||
        (view->speed_count != 0 && !view->speeds) ||
        view->marker_count > WORM_UI_TIMING_MARKER_CAPACITY ||
        (view->marker_count != 0 && !view->markers) ||
        !std::isfinite(view->viewport_width_pixels) || view->viewport_width_pixels <= 0.0 ||
        !std::isfinite(view->viewport_height_pixels) || view->viewport_height_pixels <= 0.0) {
        return 0;
    }
    if (view->scene_available &&
        (view->chart_id == 0 || view->document_revision == 0 || view->total_rows == 0)) {
        return 0;
    }
    for (size_t index = 0; index < view->speed_count; ++index) {
        const auto& speed = view->speeds[index];
        if (!std::isfinite(speed.marker_y_pixels) || !std::isfinite(speed.marker_left_fraction) ||
            !std::isfinite(speed.marker_right_fraction) || speed.marker_left_fraction < 0.0 ||
            speed.marker_right_fraction <= speed.marker_left_fraction ||
            speed.marker_right_fraction > 1.0 || !std::isfinite(speed.affected_begin_y_pixels) ||
            !std::isfinite(speed.affected_end_y_pixels) || !std::isfinite(speed.multiplier) ||
            !std::isfinite(speed.transition_duration) || speed.marker_y_pixels < 0.0 ||
            speed.marker_y_pixels > view->viewport_height_pixels ||
            speed.affected_begin_y_pixels < 0.0 ||
            speed.affected_end_y_pixels < speed.affected_begin_y_pixels ||
            speed.affected_end_y_pixels > view->viewport_height_pixels || speed.multiplier < 0.0 ||
            speed.transition_duration < 0.0) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->marker_count; ++index) {
        const auto& marker = view->markers[index];
        if (!std::isfinite(marker.marker_y_pixels) || marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            !std::isfinite(marker.marker_left_fraction) ||
            !std::isfinite(marker.marker_right_fraction) || marker.marker_left_fraction < 0.0 ||
            marker.marker_right_fraction <= marker.marker_left_fraction ||
            marker.marker_right_fraction > 1.0 || marker.lane < WORM_UI_TIMING_LANE_BPM ||
            marker.lane > WORM_UI_TIMING_LANE_ANCHOR) {
            return 0;
        }
    }
    if (view->grid_line_count > WORM_UI_TIMING_GRID_CAPACITY ||
        (view->grid_line_count != 0 && !view->grid_lines)) {
        return 0;
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        const auto& line = view->grid_lines[index];
        if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
            line.y_pixels > view->viewport_height_pixels || line.kind > 3 || line.emphasis > 3) {
            return 0;
        }
    }
    const auto& recipe = view->sv_recipe;
    if (recipe.candidate_point_count > WORM_UI_TIMING_RECIPE_POINT_CAPACITY ||
        (recipe.candidate_point_count != 0 && !recipe.candidate_points) ||
        recipe.composed_point_count > WORM_UI_TIMING_RECIPE_POINT_CAPACITY ||
        (recipe.composed_point_count != 0 && !recipe.composed_points)) {
        return 0;
    }
    if (recipe.selection_armed &&
        (recipe.chart_id == 0 || recipe.document_revision != view->document_revision ||
         recipe.chart_id != view->chart_id)) {
        return 0;
    }
    if (recipe.active &&
        (recipe.chart_id == 0 || recipe.document_revision != view->document_revision ||
         recipe.chart_id != view->chart_id || !std::isfinite(recipe.range_begin_fraction) ||
         !std::isfinite(recipe.range_end_fraction) || recipe.range_begin_fraction < 0.0 ||
         recipe.range_end_fraction <= recipe.range_begin_fraction ||
         recipe.range_end_fraction > 1.0 || recipe.selection_armed)) {
        return 0;
    }
    const auto form_valid = [](const WormUiTimingRecipeForm& form) {
        const auto terminated = [](const char* value, size_t size) {
            return std::memchr(value, '\0', size) != nullptr;
        };
        const WormUiTimingRecipeRationalText* rationals[] = {
            &form.value_from, &form.value_to,    &form.shape,
            &form.bump_start, &form.bump_length, &form.bump_x,
            &form.bump_y,     &form.tolerance,   &form.transition_duration,
        };
        for (const auto* rational : rationals) {
            if (!terminated(rational->numerator, sizeof(rational->numerator)) ||
                !terminated(rational->denominator, sizeof(rational->denominator))) {
                return false;
            }
        }
        return terminated(form.fixed_point_count, sizeof(form.fixed_point_count)) &&
               terminated(form.fixed_step_rows, sizeof(form.fixed_step_rows)) &&
               terminated(form.adaptive_max_points, sizeof(form.adaptive_max_points)) &&
               form.output >= WORM_UI_TIMING_RECIPE_OUTPUT_SV &&
               form.output <= WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS &&
               form.curve >= WORM_UI_TIMING_RECIPE_CURVE_CONSTANT &&
               form.curve <= WORM_UI_TIMING_RECIPE_CURVE_BUMP &&
               form.sampling_mode >= WORM_UI_TIMING_RECIPE_SAMPLING_FIXED &&
               form.sampling_mode <= WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE &&
               form.fixed_extent >= WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT &&
               form.fixed_extent <= WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS;
    };
    if (!form_valid(recipe.form)) {
        return 0;
    }
    const auto points_valid = [](const WormUiTimingRecipePoint* points, size_t count) {
        double previous_row_fraction = 0.0;
        for (size_t index = 0; index < count; ++index) {
            const auto& point = points[index];
            if (!std::isfinite(point.row_fraction) || !std::isfinite(point.value_fraction) ||
                point.row_fraction < 0.0 || point.row_fraction > 1.0 ||
                point.value_fraction < 0.0 || point.value_fraction > 1.0 ||
                (index != 0 && point.row_fraction < previous_row_fraction)) {
                return false;
            }
            previous_row_fraction = point.row_fraction;
        }
        return true;
    };
    if (!points_valid(recipe.candidate_points, recipe.candidate_point_count) ||
        !points_valid(recipe.composed_points, recipe.composed_point_count)) {
        return 0;
    }
    if (view->stage_active &&
        (!std::isfinite(view->stage_marker_y_pixels) || view->stage_marker_y_pixels < 0.0 ||
         view->stage_marker_y_pixels > view->viewport_height_pixels ||
         view->stage_lane < WORM_UI_TIMING_LANE_BPM ||
         view->stage_lane > WORM_UI_TIMING_LANE_ANCHOR ||
         view->stage_pause_unit < WORM_UI_TIMING_PAUSE_SECONDS ||
         view->stage_pause_unit > WORM_UI_TIMING_PAUSE_BEATS || !std::isfinite(view->stage_value) ||
         !std::isfinite(view->stage_secondary_value))) {
        return 0;
    }
    return 1;
}

static void set_timing_action(WormUiWorkspaceAction* action, const WormUiTimingView* view,
                              WormUiTimingStageAction kind, double primary = 0.0,
                              double secondary = 0.0, bool transition_is_seconds = false) {
    if (!action || !view) {
        return;
    }
    action->timing_stage_action = kind;
    action->timing_chart_id = view->chart_id;
    action->timing_document_revision = view->document_revision;
    action->timing_object_id = view->stage_existing_object_id;
    action->timing_value = primary;
    action->timing_secondary_value = secondary;
    action->timing_transition_is_seconds = transition_is_seconds;
    action->timing_toggle_primary = view->stage_toggle_primary;
    action->timing_pause_unit = view->stage_pause_unit;
}

static void copy_timing_text(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) {
        return;
    }
    const auto length = source ? std::min(strlen(source), capacity - 1) : 0;
    if (length != 0) {
        std::memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

static void copy_timing_form_to_action(WormUiWorkspaceAction* action) {
    if (!action) {
        return;
    }
    copy_timing_text(action->timing_primary_numerator_text,
                     sizeof(action->timing_primary_numerator_text),
                     g_timing_primary_numerator.data());
    copy_timing_text(action->timing_primary_denominator_text,
                     sizeof(action->timing_primary_denominator_text),
                     g_timing_primary_denominator.data());
    copy_timing_text(action->timing_secondary_numerator_text,
                     sizeof(action->timing_secondary_numerator_text),
                     g_timing_secondary_numerator.data());
    copy_timing_text(action->timing_secondary_denominator_text,
                     sizeof(action->timing_secondary_denominator_text),
                     g_timing_secondary_denominator.data());
}

static void set_timing_recipe_action(WormUiWorkspaceAction* action,
                                     const WormUiTimingRecipeDraftView* view,
                                     WormUiTimingRecipeActionKind kind, double range_begin = 0.0,
                                     double range_end = 0.0) {
    if (!action || !view) {
        return;
    }
    auto& recipe_action = action->timing_recipe_action;
    recipe_action.kind = kind;
    recipe_action.chart_id = view->chart_id;
    recipe_action.document_revision = view->document_revision;
    recipe_action.range_begin_fraction = range_begin;
    recipe_action.range_end_fraction = range_end;
    recipe_action.form = g_timing_recipe_buffers_active ? g_timing_recipe_form : view->form;
}

static void cancel_timing_recipe_range_drag() {
    g_timing_recipe_range_drag_active = false;
    g_timing_recipe_range_drag_mode = timing_recipe_range_drag_mode_t::new_range;
    g_timing_recipe_drag_moved = false;
}

static void cancel_timing_marker_drag() {
    g_timing_marker_drag_active = false;
    g_timing_marker_drag_moved = false;
    g_timing_marker_drag_chart = 0;
    g_timing_marker_drag_revision = 0;
    g_timing_marker_drag_object = 0;
}

static auto timing_marker_drag_context_matches(const WormUiWorkspaceView* view) -> bool {
    if (!g_timing_marker_drag_active) {
        return true;
    }
    return view && view->active_tab == WORM_UI_WORKSPACE_TAB_TIMING && g_window_focused &&
           view->timing.visible && view->timing.scene_available &&
           view->timing.chart_id == g_timing_marker_drag_chart &&
           view->timing.document_revision == g_timing_marker_drag_revision;
}

static auto timing_recipe_range_drag_context_matches(const WormUiWorkspaceView* view) -> bool {
    if (!g_timing_recipe_range_drag_active) {
        return true;
    }
    if (!view || view->active_tab != WORM_UI_WORKSPACE_TAB_TIMING || !g_window_focused ||
        !view->timing.visible || !view->timing.scene_available || view->timing.stage_active ||
        view->timing.chart_id != g_timing_recipe_drag_chart ||
        view->timing.document_revision != g_timing_recipe_drag_revision) {
        return false;
    }
    return g_timing_recipe_range_drag_mode == timing_recipe_range_drag_mode_t::new_range
               ? view->timing.sv_recipe.selection_armed
               : view->timing.sv_recipe.active;
}

static void ensure_timing_recipe_buffers(const WormUiTimingRecipeDraftView* view) {
    if (!view || !view->active) {
        g_timing_recipe_buffers_active = false;
        return;
    }
    if (g_timing_recipe_buffers_active && g_timing_recipe_buffers_chart == view->chart_id &&
        g_timing_recipe_buffers_revision == view->document_revision &&
        g_timing_recipe_buffers_id == view->recipe_id) {
        return;
    }
    g_timing_recipe_form = view->form;
    g_timing_recipe_buffers_active = true;
    g_timing_recipe_buffers_chart = view->chart_id;
    g_timing_recipe_buffers_revision = view->document_revision;
    g_timing_recipe_buffers_id = view->recipe_id;
}

static auto timing_recipe_output_label(WormUiTimingRecipeOutput output) -> const char* {
    switch (output) {
    case WORM_UI_TIMING_RECIPE_OUTPUT_SV:
        return "SV";
    case WORM_UI_TIMING_RECIPE_OUTPUT_SCROLLS:
        return "SCROLLS";
    case WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS:
        return "SPEEDS";
    }
    return "SV";
}

static auto timing_recipe_curve_label(WormUiTimingRecipeCurve curve) -> const char* {
    switch (curve) {
    case WORM_UI_TIMING_RECIPE_CURVE_CONSTANT:
        return "Constant";
    case WORM_UI_TIMING_RECIPE_CURVE_LINEAR:
        return "Linear";
    case WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL:
        return "Exponential";
    case WORM_UI_TIMING_RECIPE_CURVE_BUMP:
        return "Bump";
    }
    return "Linear";
}

static auto timing_recipe_form_rational(const char* label, WormUiTimingRecipeRationalText* rational)
    -> bool {
    if (!rational) {
        return false;
    }
    bool changed = false;
    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 4);
    nk_layout_row_push(g_context, 0.22f);
    nk_label(g_context, label, NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.34f);
    changed |= nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, rational->numerator,
                                              sizeof(rational->numerator), nk_filter_float) != 0;
    nk_layout_row_push(g_context, 0.08f);
    nk_label(g_context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(g_context, 0.34f);
    changed |=
        nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, rational->denominator,
                                       sizeof(rational->denominator), nk_filter_decimal) != 0;
    nk_layout_row_end(g_context);
    return changed;
}

static void render_timing_recipe_form(const WormUiTimingView* view, WormUiWorkspaceAction* action) {
    const auto& draft = view->sv_recipe;
    ensure_timing_recipe_buffers(&draft);
    if (!draft.active) {
        return;
    }
    bool changed = false;
    auto& form = g_timing_recipe_form;

    nk_layout_row_dynamic(g_context, 22, 1);
    nk_labelf(g_context, NK_TEXT_LEFT, "SV recipe draft · range %.4f to %.4f",
              draft.range_begin_fraction, draft.range_end_fraction);
    nk_layout_row_dynamic(g_context, 18, 1);
    nk_label(g_context, "Amber: raw candidate · Mint: composed baseline × recipe", NK_TEXT_LEFT);
    nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(g_context, 0.18f);
    nk_label(g_context, "Output", NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.32f);
    if (nk_button_label(g_context, timing_recipe_output_label(form.output))) {
        form.output =
            static_cast<WormUiTimingRecipeOutput>((static_cast<int>(form.output) + 1) % 3);
        changed = true;
    }
    nk_layout_row_push(g_context, 0.18f);
    nk_label(g_context, "Curve", NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.32f);
    if (nk_button_label(g_context, timing_recipe_curve_label(form.curve))) {
        form.curve = static_cast<WormUiTimingRecipeCurve>((static_cast<int>(form.curve) + 1) % 4);
        changed = true;
    }
    nk_layout_row_end(g_context);

    if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_CONSTANT) {
        changed |= timing_recipe_form_rational("Value", &form.value_from);
    } else if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_LINEAR) {
        changed |= timing_recipe_form_rational("From", &form.value_from);
        changed |= timing_recipe_form_rational("To", &form.value_to);
    } else if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL) {
        changed |= timing_recipe_form_rational("From", &form.value_from);
        changed |= timing_recipe_form_rational("To", &form.value_to);
        changed |= timing_recipe_form_rational("Shape", &form.shape);
    } else {
        changed |= timing_recipe_form_rational("Bump start", &form.bump_start);
        changed |= timing_recipe_form_rational("Bump length", &form.bump_length);
        changed |= timing_recipe_form_rational("Bump X", &form.bump_x);
        changed |= timing_recipe_form_rational("Bump Y", &form.bump_y);
    }
    if (form.output == WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS) {
        changed |= timing_recipe_form_rational("Transition", &form.transition_duration);
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_bool transition_is_seconds = form.transition_is_seconds ? 1 : 0;
        if (nk_checkbox_label(g_context, "SPEEDS transition uses seconds",
                              &transition_is_seconds)) {
            form.transition_is_seconds = transition_is_seconds != 0;
            changed = true;
        }
    }

    nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(g_context, 0.18f);
    nk_label(g_context, "Sampling", NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.32f);
    if (nk_button_label(g_context, form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
                                       ? "Fixed"
                                       : "Adaptive")) {
        form.sampling_mode = form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
                                 ? WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE
                                 : WORM_UI_TIMING_RECIPE_SAMPLING_FIXED;
        changed = true;
    }
    nk_layout_row_push(g_context, 0.18f);
    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        nk_label(g_context, "Fixed extent", NK_TEXT_LEFT);
    } else {
        nk_label(g_context, "Adaptive", NK_TEXT_LEFT);
    }
    nk_layout_row_push(g_context, 0.32f);
    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        if (nk_button_label(g_context, form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT
                                           ? "Point count"
                                           : "Row step")) {
            form.fixed_extent = form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT
                                    ? WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS
                                    : WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
            changed = true;
        }
    } else {
        nk_label(g_context, "error tolerance", NK_TEXT_LEFT);
    }
    nk_layout_row_end(g_context);

    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(g_context, 0.22f);
        nk_label(g_context,
                 form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT ? "Points"
                                                                              : "Step rows",
                 NK_TEXT_LEFT);
        nk_layout_row_push(g_context, 0.78f);
        if (form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT) {
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, form.fixed_point_count,
                           sizeof(form.fixed_point_count), nk_filter_decimal) != 0;
        } else {
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, form.fixed_step_rows,
                           sizeof(form.fixed_step_rows), nk_filter_decimal) != 0;
        }
        nk_layout_row_end(g_context);
    } else {
        changed |= timing_recipe_form_rational("Tolerance", &form.tolerance);
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(g_context, 0.22f);
        nk_label(g_context, "Max points", NK_TEXT_LEFT);
        nk_layout_row_push(g_context, 0.78f);
        changed |= nk_edit_string_zero_terminated(
                       g_context, NK_EDIT_FIELD, form.adaptive_max_points,
                       sizeof(form.adaptive_max_points), nk_filter_decimal) != 0;
        nk_layout_row_end(g_context);
    }

    if (draft.status_text && draft.status_text[0]) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, draft.status_text, NK_TEXT_LEFT);
    }
    nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(g_context, 0.50f);
    if (!draft.valid) {
        nk_widget_disable_begin(g_context);
    }
    const auto apply_clicked = nk_button_label(g_context, "Apply SV recipe");
    if (!draft.valid) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_push(g_context, 0.50f);
    const auto cancel_clicked = nk_button_label(g_context, "Cancel recipe");
    nk_layout_row_end(g_context);
    if (action && action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        if (cancel_clicked) {
            set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
        } else if (apply_clicked && draft.valid) {
            set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_APPLY);
        } else if (changed) {
            set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_UPDATE);
        }
    }
}

static void render_timing_lane(const WormUiTimingView* view, WormUiWorkspaceAction* action) {
    if (!view->stage_active) {
        g_timing_stage_buffers_active = false;
    } else if (!g_timing_stage_buffers_active || g_timing_stage_buffers_chart != view->chart_id ||
               g_timing_stage_buffers_revision != view->document_revision ||
               g_timing_stage_buffers_object != view->stage_existing_object_id ||
               g_timing_stage_buffers_lane != view->stage_lane ||
               g_timing_stage_buffers_y != view->stage_marker_y_pixels) {
        copy_timing_text(g_timing_primary_numerator.data(), g_timing_primary_numerator.size(),
                         view->stage_primary_numerator_text);
        copy_timing_text(g_timing_primary_denominator.data(), g_timing_primary_denominator.size(),
                         view->stage_primary_denominator_text);
        copy_timing_text(g_timing_secondary_numerator.data(), g_timing_secondary_numerator.size(),
                         view->stage_secondary_numerator_text);
        copy_timing_text(g_timing_secondary_denominator.data(),
                         g_timing_secondary_denominator.size(),
                         view->stage_secondary_denominator_text);
        g_timing_stage_buffers_active = true;
        g_timing_stage_buffers_chart = view->chart_id;
        g_timing_stage_buffers_revision = view->document_revision;
        g_timing_stage_buffers_object = view->stage_existing_object_id;
        g_timing_stage_buffers_lane = view->stage_lane;
        g_timing_stage_buffers_y = view->stage_marker_y_pixels;
    }
    const bool can_change_grid = !view->stage_active && !view->sv_recipe.active &&
                                 !view->sv_recipe.selection_armed && view->scene_available &&
                                 view->supported_snap_denominator_count > 0;
    nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 3);
    nk_layout_row_push(g_context, 0.42f);
    nk_labelf(g_context, NK_TEXT_LEFT, "Active grid: 1/%llu%s",
              static_cast<unsigned long long>(view->active_snap_denominator),
              view->active_snap_denominator == 0 ? " (unavailable)" : "");
    nk_layout_row_push(g_context, 0.29f);
    if (!can_change_grid) {
        nk_widget_disable_begin(g_context);
    }
    const auto previous_grid_clicked = nk_button_label(g_context, "Previous grid");
    if (!can_change_grid) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_push(g_context, 0.29f);
    if (!can_change_grid) {
        nk_widget_disable_begin(g_context);
    }
    const auto next_grid_clicked = nk_button_label(g_context, "Next grid");
    if (!can_change_grid) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_end(g_context);
    if (action && action->timing_grid_action == WORM_UI_TIMING_GRID_ACTION_NONE &&
        (previous_grid_clicked || next_grid_clicked)) {
        size_t current = 0;
        while (current < view->supported_snap_denominator_count &&
               view->supported_snap_denominators[current] != view->active_snap_denominator) {
            ++current;
        }
        if (current < view->supported_snap_denominator_count) {
            const auto count = view->supported_snap_denominator_count;
            const auto next = previous_grid_clicked ? (current + count - 1) % count
                                                    : (current + 1) % count;
            action->timing_grid_action = WORM_UI_TIMING_GRID_ACTION_SELECT;
            action->timing_grid_chart_id = view->chart_id;
            action->timing_grid_document_revision = view->document_revision;
            action->timing_grid_denominator = view->supported_snap_denominators[next];
        }
    }
    const bool can_apply_meter = !view->stage_active && !view->sv_recipe.active &&
                                 !view->sv_recipe.selection_armed && view->scene_available &&
                                 view->measure_count != 0;
    nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 5);
    nk_layout_row_push(g_context, 0.23f);
    nk_label(g_context, "Meter at measure", NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.18f);
    if (!can_apply_meter) nk_widget_disable_begin(g_context);
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, g_timing_meter_measure.data(),
                                         g_timing_meter_measure.size(), nk_filter_decimal);
    if (!can_apply_meter) nk_widget_disable_end(g_context);
    nk_layout_row_push(g_context, 0.16f);
    if (!can_apply_meter) nk_widget_disable_begin(g_context);
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD,
                                         g_timing_meter_numerator.data(),
                                         g_timing_meter_numerator.size(), nk_filter_decimal);
    if (!can_apply_meter) nk_widget_disable_end(g_context);
    nk_layout_row_push(g_context, 0.16f);
    if (!can_apply_meter) nk_widget_disable_begin(g_context);
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD,
                                         g_timing_meter_denominator.data(),
                                         g_timing_meter_denominator.size(), nk_filter_decimal);
    if (!can_apply_meter) nk_widget_disable_end(g_context);
    nk_layout_row_push(g_context, 0.27f);
    if (!can_apply_meter) nk_widget_disable_begin(g_context);
    const auto apply_meter_clicked = nk_button_label(g_context, "Apply meter");
    if (!can_apply_meter) nk_widget_disable_end(g_context);
    nk_layout_row_end(g_context);
    if (action && action->timing_meter_action == WORM_UI_TIMING_METER_ACTION_NONE &&
        apply_meter_clicked) {
        action->timing_meter_action = WORM_UI_TIMING_METER_ACTION_APPLY;
        action->timing_meter_chart_id = view->chart_id;
        action->timing_meter_document_revision = view->document_revision;
        copy_timing_text(action->timing_meter_measure_text,
                         sizeof(action->timing_meter_measure_text), g_timing_meter_measure.data());
        copy_timing_text(action->timing_meter_numerator_text,
                         sizeof(action->timing_meter_numerator_text),
                         g_timing_meter_numerator.data());
        copy_timing_text(action->timing_meter_denominator_text,
                         sizeof(action->timing_meter_denominator_text),
                         g_timing_meter_denominator.data());
    }
    nk_layout_row_dynamic(g_context, 24, 1);
    nk_labelf(g_context, NK_TEXT_LEFT,
              "%zu measures · %zu BPM · %zu stop/delay · %zu warp · %zu SPEEDS",
              view->measure_count, view->bpm_count, view->pause_count, view->warp_count,
              view->speed_total_count);
    nk_layout_row_dynamic(g_context, 18, 1);
    nk_labelf(g_context, NK_TEXT_LEFT,
              "%zu bookmarks/sections · %zu anchors · Hover a SPEEDS marker for its exact span; "
              "Double-click a marker to edit its exact value, or an empty row to insert on the "
              "current active timing grid. Coincident markers split left-to-right by source order.",
              view->bookmark_count + view->section_count, view->anchor_count);

    nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(g_context, 0.38f);
    if (view->sv_recipe.selection_armed) {
        if (nk_button_label(g_context, "Cancel SV recipe selection") && action &&
            action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
            set_timing_recipe_action(action, &view->sv_recipe, WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
        }
    } else if (!view->sv_recipe.active) {
        const bool can_arm_recipe =
            !view->stage_active && view->scene_available && view->chart_id != 0;
        if (!can_arm_recipe) {
            nk_widget_disable_begin(g_context);
        }
        const auto arm_clicked = nk_button_label(g_context, "New SV recipe");
        if (!can_arm_recipe) {
            nk_widget_disable_end(g_context);
        }
        if (arm_clicked && can_arm_recipe && action &&
            action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
            set_timing_recipe_action(action, &view->sv_recipe,
                                     WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION);
        }
    } else {
        nk_label(g_context,
                 view->stage_active ? "Finish or cancel marker edit before resnapping this range"
                                    : "Drag a boundary or the interval body to resnap it",
                 NK_TEXT_LEFT);
    }
    nk_layout_row_push(g_context, 0.62f);
    if (view->sv_recipe.selection_armed) {
        nk_label(g_context, "Drag vertically in Scroll / SV to choose an exact range.",
                 NK_TEXT_LEFT);
    } else if (view->sv_recipe.active) {
        nk_label(g_context, "Candidate and composed curves are preview only until Apply.",
                 NK_TEXT_LEFT);
    } else {
        nk_label(g_context, "Parametric curves stay editable in the project.", NK_TEXT_LEFT);
    }
    nk_layout_row_end(g_context);

    const auto group_bounds = nk_window_get_content_region(g_context);
    const auto reserved_panel_height =
        120.0f + (view->stage_active ? 212.0f : 0.0f) + (view->sv_recipe.active ? 300.0f : 0.0f);
    const auto surface_height = std::max(32.0f, group_bounds.h - reserved_panel_height);
    nk_layout_space_begin(g_context, NK_DYNAMIC, surface_height, 1);
    nk_layout_space_push(g_context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, g_context);
    nk_layout_space_end(g_context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f) {
        cancel_timing_recipe_range_drag();
        return;
    }

    auto* canvas = nk_window_get_canvas(g_context);
    if (!canvas) {
        cancel_timing_recipe_range_drag();
        return;
    }
    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    nk_fill_rect(canvas, viewport, 0.0f, nk_rgb(22, 25, 33));
    if (!view->scene_available) {
        cancel_timing_recipe_range_drag();
        const auto* status = view->status_text ? view->status_text : "Timing lane is unavailable.";
        nk_draw_text(canvas, viewport, status, static_cast<int>(strlen(status)),
                     g_context->style.font, nk_rgb(22, 25, 33), nk_rgb(190, 196, 207));
        nk_push_scissor(canvas, parent_clip);
        return;
    }

    const auto screen_y = [&](double y_pixels) {
        return viewport.y + static_cast<float>(y_pixels) * viewport.h /
                                static_cast<float>(view->viewport_height_pixels);
    };
    constexpr std::array<WormUiTimingLaneKind, 8> lane_kinds{
        WORM_UI_TIMING_LANE_BPM,  WORM_UI_TIMING_LANE_SCROLL, WORM_UI_TIMING_LANE_SPEED,
        WORM_UI_TIMING_LANE_STOP, WORM_UI_TIMING_LANE_DELAY,  WORM_UI_TIMING_LANE_WARP,
        WORM_UI_TIMING_LANE_KIAI, WORM_UI_TIMING_LANE_ANCHOR};
    constexpr std::array<const char*, 8> lane_labels{"BPM",  "Scroll / SV", "SPEEDS", "Stop",
                                                     "Delay", "Warp", "Kiai",   "Anchor"};
    const std::array<struct nk_color, 8> lane_colors{
        nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218),
        nk_rgb(224, 132, 119), nk_rgb(207, 153, 104), nk_rgb(176, 142, 218),
        nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};
    const auto lane_left = viewport.x + viewport.w * 0.025f;
    const auto lane_width = viewport.w * 0.95f / static_cast<float>(lane_kinds.size());
    std::array<struct nk_rect, 8> lanes{};
    for (size_t index = 0; index < lanes.size(); ++index) {
        lanes[index] = nk_rect(lane_left + lane_width * static_cast<float>(index) + 2.0f,
                               viewport.y, std::max(1.0f, lane_width - 4.0f), viewport.h);
        nk_fill_rect(canvas, lanes[index], 0.0f, nk_rgb(31, 37, 49));
        nk_stroke_line(canvas, lanes[index].x, lanes[index].y, lanes[index].x,
                       lanes[index].y + lanes[index].h, 1.0f, nk_rgb(66, 76, 94));
        nk_stroke_line(canvas, lanes[index].x + lanes[index].w, lanes[index].y,
                       lanes[index].x + lanes[index].w, lanes[index].y + lanes[index].h, 1.0f,
                       nk_rgb(66, 76, 94));
        const auto title = nk_rect(lanes[index].x + 3.0f, viewport.y + 3.0f,
                                   std::max(0.0f, lanes[index].w - 6.0f), 18.0f);
        nk_draw_text(canvas, title, lane_labels[index],
                     static_cast<int>(strlen(lane_labels[index])), g_context->style.font,
                     nk_rgba(22, 25, 33, 190), lane_colors[index]);
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        const auto& line = view->grid_lines[index];
        const auto y = screen_y(line.y_pixels);
        auto color = nk_rgb(42, 49, 63);
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
    }
    const auto recipe_lane_index =
        view->sv_recipe.active && view->sv_recipe.form.output == WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS
            ? std::size_t{2}
            : std::size_t{1};
    const auto& recipe_lane = lanes[recipe_lane_index];

    const bool pointer_inside = nk_input_is_mouse_hovering_rect(&g_context->input, viewport);
    const auto pointer_y = g_context->input.mouse.pos.y;
    size_t hovered = view->speed_count;
    auto nearest_distance = std::numeric_limits<float>::max();
    const auto& speed_lane = lanes[2];
    const auto marker_horizontal_bounds = [](const struct nk_rect& lane, double left_fraction,
                                             double right_fraction) {
        const auto inner_left = lane.x + 4.0f;
        const auto inner_width = std::max(0.0f, lane.w - 8.0f);
        const auto left = inner_left + inner_width * static_cast<float>(left_fraction);
        const auto right = inner_left + inner_width * static_cast<float>(right_fraction);
        return std::pair{left, right};
    };
    const auto normalized_y_fraction = [&](double y) {
        return std::clamp((y - static_cast<double>(viewport.y)) / static_cast<double>(viewport.h),
                          0.0, 1.0);
    };
    const auto current_recipe_drag_range = [&]() -> std::pair<double, double> {
        const auto current = g_timing_recipe_drag_current_fraction;
        switch (g_timing_recipe_range_drag_mode) {
        case timing_recipe_range_drag_mode_t::new_range:
            return std::pair{std::min(g_timing_recipe_drag_anchor_fraction, current),
                             std::max(g_timing_recipe_drag_anchor_fraction, current)};
        case timing_recipe_range_drag_mode_t::begin_handle:
            return std::pair{std::min(current, g_timing_recipe_drag_original_end),
                             std::max(current, g_timing_recipe_drag_original_end)};
        case timing_recipe_range_drag_mode_t::end_handle:
            return std::pair{std::min(g_timing_recipe_drag_original_begin, current),
                             std::max(g_timing_recipe_drag_original_begin, current)};
        case timing_recipe_range_drag_mode_t::whole_span: {
            const auto minimum_delta = -g_timing_recipe_drag_original_begin;
            const auto maximum_delta = 1.0 - g_timing_recipe_drag_original_end;
            const auto delta = std::clamp(current - g_timing_recipe_drag_anchor_fraction,
                                          minimum_delta, maximum_delta);
            return std::pair{g_timing_recipe_drag_original_begin + delta,
                             g_timing_recipe_drag_original_end + delta};
        }
        }
        return std::pair{0.0, 0.0};
    };
    const auto& left_button = g_context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto pointer_started_in_view = left_button.down && left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x <= viewport.x + viewport.w &&
                                         left_button.clicked_pos.y >= viewport.y &&
                                         left_button.clicked_pos.y <= viewport.y + viewport.h;
    const auto pointer_started_in_recipe_lane =
        pointer_started_in_view && left_button.clicked_pos.x >= recipe_lane.x &&
        left_button.clicked_pos.x <= recipe_lane.x + recipe_lane.w;
    const auto marker_at_pointer = [&](float x, float y)
        -> std::optional<std::pair<WormUiTimingLaneKind, uint64_t>> {
        for (size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
            if (x < lanes[lane_index].x || x > lanes[lane_index].x + lanes[lane_index].w) {
                continue;
            }
            uint64_t object_id = 0;
            auto nearest = std::numeric_limits<float>::max();
            const auto consider = [&](uint64_t candidate, double left_fraction,
                                      double right_fraction, double marker_y) {
                const auto [left, right] =
                    marker_horizontal_bounds(lanes[lane_index], left_fraction, right_fraction);
                if (x < left || x > right) {
                    return;
                }
                const auto distance = std::abs(y - screen_y(marker_y));
                if (distance <= 7.0f && distance <= nearest) {
                    object_id = candidate;
                    nearest = distance;
                }
            };
            for (size_t marker = 0; marker < view->marker_count; ++marker) {
                const auto& candidate = view->markers[marker];
                if (candidate.lane == lane_kinds[lane_index]) {
                    consider(candidate.object_id, candidate.marker_left_fraction,
                             candidate.marker_right_fraction, candidate.marker_y_pixels);
                }
            }
            if (lane_kinds[lane_index] == WORM_UI_TIMING_LANE_SPEED) {
                for (size_t marker = 0; marker < view->speed_count; ++marker) {
                    const auto& candidate = view->speeds[marker];
                    consider(candidate.object_id, candidate.marker_left_fraction,
                             candidate.marker_right_fraction, candidate.marker_y_pixels);
                }
            }
            if (object_id != 0) {
                return std::pair{lane_kinds[lane_index], object_id};
            }
        }
        return std::nullopt;
    };
    if (!g_timing_marker_drag_active && !view->stage_active && !view->sv_recipe.active &&
        !view->sv_recipe.selection_armed && g_window_focused && pointer_started_in_view) {
        if (const auto marker = marker_at_pointer(left_button.clicked_pos.x,
                                                   left_button.clicked_pos.y)) {
            g_timing_marker_drag_active = true;
            g_timing_marker_drag_moved = false;
            g_timing_marker_drag_chart = view->chart_id;
            g_timing_marker_drag_revision = view->document_revision;
            g_timing_marker_drag_lane = marker->first;
            g_timing_marker_drag_object = marker->second;
            g_timing_marker_drag_anchor_screen_y = left_button.clicked_pos.y;
        }
    }
    if (!g_timing_recipe_range_drag_active && !view->stage_active && action &&
        action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE &&
        !g_timing_marker_drag_active && g_window_focused && pointer_started_in_recipe_lane) {
        const bool armed_selection = view->sv_recipe.selection_armed && !view->sv_recipe.active;
        const auto clicked_fraction = normalized_y_fraction(left_button.clicked_pos.y);
        const auto selected_begin = view->sv_recipe.range_begin_fraction;
        const auto selected_end = view->sv_recipe.range_end_fraction;
        const bool hit_selected_interval = view->sv_recipe.active &&
                                           clicked_fraction >= selected_begin - 0.01 &&
                                           clicked_fraction <= selected_end + 0.01;
        if (armed_selection || hit_selected_interval) {
            g_timing_recipe_range_drag_active = true;
            g_timing_recipe_drag_original_begin = selected_begin;
            g_timing_recipe_drag_original_end = selected_end;
            if (armed_selection) {
                g_timing_recipe_range_drag_mode = timing_recipe_range_drag_mode_t::new_range;
            } else {
                const auto handle_tolerance =
                    std::max(0.005, 6.0 / static_cast<double>(viewport.h));
                if (std::abs(clicked_fraction - selected_begin) <= handle_tolerance) {
                    g_timing_recipe_range_drag_mode = timing_recipe_range_drag_mode_t::begin_handle;
                } else if (std::abs(clicked_fraction - selected_end) <= handle_tolerance) {
                    g_timing_recipe_range_drag_mode = timing_recipe_range_drag_mode_t::end_handle;
                } else {
                    g_timing_recipe_range_drag_mode = timing_recipe_range_drag_mode_t::whole_span;
                }
            }
            g_timing_recipe_drag_chart = view->chart_id;
            g_timing_recipe_drag_revision = view->document_revision;
            g_timing_recipe_drag_anchor_fraction = clicked_fraction;
            g_timing_recipe_drag_current_fraction = clicked_fraction;
            g_timing_recipe_drag_anchor_screen_y = left_button.clicked_pos.y;
            g_timing_recipe_drag_moved = false;
        }
    }
    const bool left_down = left_button.down != 0;
    const bool left_released = nk_input_is_mouse_released(&g_context->input, NK_BUTTON_LEFT);
    if (g_timing_marker_drag_active) {
        const bool matches_drag = g_timing_marker_drag_chart == view->chart_id &&
                                  g_timing_marker_drag_revision == view->document_revision;
        if (!g_window_focused || !view->scene_available || !matches_drag ||
            (!left_down && !left_released)) {
            cancel_timing_marker_drag();
        } else {
            const auto distance =
                std::abs(g_context->input.mouse.pos.y - g_timing_marker_drag_anchor_screen_y);
            g_timing_marker_drag_moved = g_timing_marker_drag_moved || distance >= 4.0f;
            if (g_timing_marker_drag_moved && action &&
                action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE &&
                action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
                const auto target = std::min(
                    normalized_y_fraction(g_context->input.mouse.pos.y),
                    std::nextafter(1.0, 0.0));
                set_timing_action(action, view,
                                  view->stage_active ? WORM_UI_TIMING_STAGE_MOVE
                                                     : WORM_UI_TIMING_STAGE_BEGIN);
                action->timing_chart_id = g_timing_marker_drag_chart;
                action->timing_document_revision = g_timing_marker_drag_revision;
                action->timing_lane = g_timing_marker_drag_lane;
                action->timing_object_id = g_timing_marker_drag_object;
                action->timing_has_target_y = true;
                action->timing_target_y_fraction = target;
            }
            if (left_released) {
                cancel_timing_marker_drag();
            }
        }
    }
    if (g_timing_recipe_range_drag_active) {
        const bool view_matches_drag =
            g_timing_recipe_drag_chart == view->chart_id &&
            g_timing_recipe_drag_revision == view->document_revision &&
            (g_timing_recipe_range_drag_mode == timing_recipe_range_drag_mode_t::new_range
                 ? view->sv_recipe.selection_armed
                 : view->sv_recipe.active);
        if (!g_window_focused || !view->scene_available || view->stage_active ||
            !view_matches_drag || (!left_down && !left_released)) {
            cancel_timing_recipe_range_drag();
        } else {
            g_timing_recipe_drag_current_fraction =
                normalized_y_fraction(g_context->input.mouse.pos.y);
            const auto distance =
                std::abs(g_context->input.mouse.pos.y - g_timing_recipe_drag_anchor_screen_y);
            g_timing_recipe_drag_moved = g_timing_recipe_drag_moved || distance >= 4.0f;
            if (left_released) {
                if (g_timing_recipe_drag_moved && action &&
                    action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
                    const auto [begin, end] = current_recipe_drag_range();
                    const auto kind = g_timing_recipe_range_drag_mode ==
                                              timing_recipe_range_drag_mode_t::new_range
                                          ? WORM_UI_TIMING_RECIPE_ACTION_BEGIN
                                          : WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE;
                    set_timing_recipe_action(action, &view->sv_recipe, kind, begin, end);
                }
                cancel_timing_recipe_range_drag();
            }
        }
    }
    if (pointer_inside && g_context->input.mouse.pos.x >= speed_lane.x &&
        g_context->input.mouse.pos.x <= speed_lane.x + speed_lane.w) {
        for (size_t index = 0; index < view->speed_count; ++index) {
            const auto [marker_left, marker_right] =
                marker_horizontal_bounds(speed_lane, view->speeds[index].marker_left_fraction,
                                         view->speeds[index].marker_right_fraction);
            if (g_context->input.mouse.pos.x < marker_left ||
                g_context->input.mouse.pos.x > marker_right) {
                continue;
            }
            const auto distance =
                std::abs(pointer_y - screen_y(view->speeds[index].marker_y_pixels));
            if (distance <= 7.0f && distance <= nearest_distance) {
                hovered = index;
                nearest_distance = distance;
            }
        }
    }

    if (hovered != view->speed_count) {
        const auto& speed = view->speeds[hovered];
        auto top = screen_y(speed.affected_begin_y_pixels);
        auto bottom = screen_y(speed.affected_end_y_pixels);
        if (bottom - top < 3.0f) {
            bottom = std::min(viewport.y + viewport.h, top + 3.0f);
        }
        nk_fill_rect(canvas, nk_rect(speed_lane.x, top, speed_lane.w, std::max(0.0f, bottom - top)),
                     0.0f, nk_rgba(136, 203, 238, 54));
        nk_stroke_line(canvas, speed_lane.x, top, speed_lane.x + speed_lane.w, top, 1.0f,
                       nk_rgb(137, 207, 239));
        nk_stroke_line(canvas, speed_lane.x, bottom, speed_lane.x + speed_lane.w, bottom, 1.0f,
                       nk_rgb(91, 145, 177));
    }

    for (size_t index = 0; index < view->marker_count; ++index) {
        const auto& marker = view->markers[index];
        for (size_t lane_index = 0; lane_index < lane_kinds.size(); ++lane_index) {
            if (marker.lane != lane_kinds[lane_index]) {
                continue;
            }
            const auto y = screen_y(marker.marker_y_pixels);
            const auto [marker_left, marker_right] = marker_horizontal_bounds(
                lanes[lane_index], marker.marker_left_fraction, marker.marker_right_fraction);
            nk_fill_rect(
                canvas,
                nk_rect(marker_left, y - 2.0f, std::max(0.0f, marker_right - marker_left), 4.0f),
                0.0f, lane_colors[lane_index]);
            break;
        }
    }

    double selected_begin = view->sv_recipe.range_begin_fraction;
    double selected_end = view->sv_recipe.range_end_fraction;
    if (g_timing_recipe_range_drag_active) {
        std::tie(selected_begin, selected_end) = current_recipe_drag_range();
    }
    if ((view->sv_recipe.active || g_timing_recipe_range_drag_active) &&
        selected_end > selected_begin) {
        const auto top = viewport.y + static_cast<float>(selected_begin) * viewport.h;
        const auto bottom = viewport.y + static_cast<float>(selected_end) * viewport.h;
        const struct nk_rect selected =
            nk_rect(recipe_lane.x + 1.0f, top, recipe_lane.w - 2.0f, std::max(1.0f, bottom - top));
        nk_fill_rect(canvas, selected, 0.0f, nk_rgba(180, 160, 230, 32));
        nk_stroke_line(canvas, selected.x, selected.y, selected.x + selected.w, selected.y, 1.0f,
                       nk_rgb(196, 173, 242));
        nk_stroke_line(canvas, selected.x, selected.y + selected.h, selected.x + selected.w,
                       selected.y + selected.h, 1.0f, nk_rgb(196, 173, 242));
    }
    if (view->sv_recipe.active) {
        const auto draw_recipe_curve = [&](const WormUiTimingRecipePoint* points, size_t count,
                                           struct nk_color color, float thickness) {
            if (!points || count == 0) {
                return;
            }
            const auto point_x = [&](const WormUiTimingRecipePoint& point) {
                const auto [inner_left, inner_right] = marker_horizontal_bounds(
                    recipe_lane, point.value_fraction, point.value_fraction);
                return (inner_left + inner_right) * 0.5f;
            };
            const auto point_y = [&](const WormUiTimingRecipePoint& point) {
                return viewport.y + static_cast<float>(point.row_fraction) * viewport.h;
            };
            for (size_t index = 0; index < count; ++index) {
                const auto x = point_x(points[index]);
                const auto y = point_y(points[index]);
                if (index != 0) {
                    nk_stroke_line(canvas, point_x(points[index - 1]), point_y(points[index - 1]),
                                   x, y, thickness, color);
                } else {
                    nk_fill_rect(canvas, nk_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f), 0.0f, color);
                }
                if (index + 1 == count) {
                    nk_fill_rect(canvas, nk_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f), 0.0f, color);
                }
            }
        };
        draw_recipe_curve(view->sv_recipe.candidate_points, view->sv_recipe.candidate_point_count,
                          nk_rgb(246, 183, 106), 2.0f);
        draw_recipe_curve(view->sv_recipe.composed_points, view->sv_recipe.composed_point_count,
                          nk_rgb(139, 229, 181), 1.5f);
    }
    for (size_t index = 0; index < view->speed_count; ++index) {
        const auto y = screen_y(view->speeds[index].marker_y_pixels);
        const auto color = index == hovered ? nk_rgb(191, 232, 248) : nk_rgb(113, 184, 218);
        const auto [marker_left, marker_right] =
            marker_horizontal_bounds(speed_lane, view->speeds[index].marker_left_fraction,
                                     view->speeds[index].marker_right_fraction);
        nk_fill_rect(
            canvas,
            nk_rect(marker_left, y - 2.0f, std::max(0.0f, marker_right - marker_left), 4.0f), 0.0f,
            color);
    }
    if (view->stage_active) {
        const auto stage_y = screen_y(view->stage_marker_y_pixels);
        for (size_t index = 0; index < lane_kinds.size(); ++index) {
            if (lane_kinds[index] == view->stage_lane) {
                nk_stroke_line(canvas, lanes[index].x + 2.0f, stage_y,
                               lanes[index].x + lanes[index].w - 2.0f, stage_y, 3.0f,
                               view->stage_valid ? nk_rgb(249, 238, 154) : nk_rgb(237, 116, 104));
                break;
            }
        }
    }
    if (hovered != view->speed_count) {
        const auto& speed = view->speeds[hovered];
        char label[192]{};
        const auto count =
            snprintf(label, sizeof(label), "SPEEDS #%llu  %.4gx over %.4g %s%s",
                     static_cast<unsigned long long>(speed.object_id), speed.multiplier,
                     speed.transition_duration, speed.transition_is_seconds ? "seconds" : "beats",
                     speed.transition_clipped ? " (clipped by next event or chart end)" : "");
        if (count > 0) {
            const auto text_bounds = nk_rect(viewport.x + 8.0f, viewport.y + 24.0f,
                                             std::max(0.0f, viewport.w - 16.0f), 20.0f);
            nk_draw_text(canvas, text_bounds, label,
                         static_cast<int>(std::min<size_t>(strlen(label), sizeof(label) - 1)),
                         g_context->style.font, nk_rgba(22, 25, 33, 220), nk_rgb(207, 232, 244));
        }
    }
    if (view->truncated) {
        const char* warning = "Additional timing markers are hidden by the frontend cap.";
        const auto text_bounds = nk_rect(viewport.x + 8.0f, viewport.y + viewport.h - 24.0f,
                                         std::max(0.0f, viewport.w - 16.0f), 18.0f);
        nk_draw_text(canvas, text_bounds, warning, static_cast<int>(strlen(warning)),
                     g_context->style.font, nk_rgba(22, 25, 33, 220), nk_rgb(224, 184, 103));
    }
    nk_push_scissor(canvas, parent_clip);

    if (!view->stage_active && action && action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE &&
        action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        for (size_t click = 0; click < g_timing_double_click_count; ++click) {
            const auto& pointer = g_timing_double_clicks[click];
            if (pointer.x < viewport.x || pointer.x >= viewport.x + viewport.w ||
                pointer.y < viewport.y || pointer.y >= viewport.y + viewport.h) {
                continue;
            }
            for (size_t index = 0; index < lanes.size(); ++index) {
                if (pointer.x < lanes[index].x || pointer.x >= lanes[index].x + lanes[index].w) {
                    continue;
                }
                uint64_t existing_object_id = 0;
                auto existing_distance = std::numeric_limits<float>::max();
                for (size_t marker = 0; marker < view->marker_count; ++marker) {
                    if (view->markers[marker].lane != lane_kinds[index]) {
                        continue;
                    }
                    const auto [marker_left, marker_right] = marker_horizontal_bounds(
                        lanes[index], view->markers[marker].marker_left_fraction,
                        view->markers[marker].marker_right_fraction);
                    if (pointer.x < marker_left || pointer.x > marker_right) {
                        continue;
                    }
                    const auto distance =
                        std::abs(pointer.y - screen_y(view->markers[marker].marker_y_pixels));
                    if (distance <= 7.0f && distance <= existing_distance) {
                        existing_object_id = view->markers[marker].object_id;
                        existing_distance = distance;
                    }
                }
                if (lane_kinds[index] == WORM_UI_TIMING_LANE_SPEED) {
                    for (size_t marker = 0; marker < view->speed_count; ++marker) {
                        const auto [marker_left, marker_right] = marker_horizontal_bounds(
                            lanes[index], view->speeds[marker].marker_left_fraction,
                            view->speeds[marker].marker_right_fraction);
                        if (pointer.x < marker_left || pointer.x > marker_right) {
                            continue;
                        }
                        const auto distance =
                            std::abs(pointer.y - screen_y(view->speeds[marker].marker_y_pixels));
                        if (distance <= 7.0f && distance <= existing_distance) {
                            existing_object_id = view->speeds[marker].object_id;
                            existing_distance = distance;
                        }
                    }
                }
                set_timing_action(action, view, WORM_UI_TIMING_STAGE_BEGIN);
                action->timing_lane = lane_kinds[index];
                action->timing_object_id = existing_object_id;
                if (existing_object_id == 0) {
                    action->timing_has_target_y = true;
                    action->timing_target_y_fraction = static_cast<double>(pointer.y - viewport.y) /
                                                       static_cast<double>(viewport.h);
                }
                break;
            }
            if (action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
                break;
            }
        }
    }

    if (view->stage_active) {
        nk_bool seconds = view->stage_transition_is_seconds ? 1 : 0;
        nk_bool toggle_primary = view->stage_toggle_primary ? 1 : 0;
        bool changed = false;
        const char* primary_label = "Value";
        const char* pause_unit_label = "seconds";
        switch (view->stage_pause_unit) {
        case WORM_UI_TIMING_PAUSE_SECONDS:
            pause_unit_label = "seconds";
            break;
        case WORM_UI_TIMING_PAUSE_MILLISECONDS:
            pause_unit_label = "milliseconds";
            break;
        case WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS:
            pause_unit_label = "BMS STOP ticks";
            break;
        case WORM_UI_TIMING_PAUSE_BEATS:
            pause_unit_label = "beats";
            break;
        }
        if (view->stage_lane == WORM_UI_TIMING_LANE_BPM) {
            primary_label = "BPM";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_SCROLL) {
            primary_label = "Scroll multiplier";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_SPEED) {
            primary_label = "SPEEDS multiplier";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_STOP ||
                   view->stage_lane == WORM_UI_TIMING_LANE_DELAY) {
            primary_label = "Duration";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_ANCHOR) {
            primary_label = "Source sample frame";
        }
        nk_layout_row_dynamic(g_context, 22, 1);
        if (view->stage_existing_object_id != 0) {
            nk_labelf(g_context, NK_TEXT_LEFT,
                      "Editing timing event #%llu — drag its row to move the staged preview.",
                      static_cast<unsigned long long>(view->stage_existing_object_id));
        } else {
            nk_label(g_context, "Staged timing event — nothing is written until Confirm.",
                     NK_TEXT_LEFT);
        }
        if (view->stage_lane == WORM_UI_TIMING_LANE_KIAI) {
            nk_layout_row_dynamic(g_context, 24, 1);
            changed |= nk_checkbox_label(g_context, "Kiai enabled", &toggle_primary) != 0;
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_ANCHOR) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context, "Exact source sample frame", NK_TEXT_LEFT);
            nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 2);
            nk_layout_row_push(g_context, 0.38f);
            nk_label(g_context, primary_label, NK_TEXT_LEFT);
            nk_layout_row_push(g_context, 0.62f);
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, g_timing_primary_numerator.data(),
                           g_timing_primary_numerator.size(), nk_filter_decimal) != 0;
            nk_layout_row_end(g_context);
            nk_layout_row_dynamic(g_context, 24, 1);
            changed |= nk_checkbox_label(g_context, "Lock anchor", &toggle_primary) != 0;
        } else {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context, "Exact rational value: numerator / denominator", NK_TEXT_LEFT);
            nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 4);
            nk_layout_row_push(g_context, 0.24f);
            nk_label(g_context, primary_label, NK_TEXT_LEFT);
            nk_layout_row_push(g_context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, g_timing_primary_numerator.data(),
                           g_timing_primary_numerator.size(), nk_filter_float) != 0;
            nk_layout_row_push(g_context, 0.05f);
            nk_label(g_context, "/", NK_TEXT_CENTERED);
            nk_layout_row_push(g_context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, g_timing_primary_denominator.data(),
                           g_timing_primary_denominator.size(), nk_filter_decimal) != 0;
            nk_layout_row_end(g_context);
        }
        if (view->stage_lane == WORM_UI_TIMING_LANE_SPEED) {
            nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 4);
            nk_layout_row_push(g_context, 0.24f);
            nk_label(g_context, "Transition", NK_TEXT_LEFT);
            nk_layout_row_push(g_context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, g_timing_secondary_numerator.data(),
                           g_timing_secondary_numerator.size(), nk_filter_decimal) != 0;
            nk_layout_row_push(g_context, 0.05f);
            nk_label(g_context, "/", NK_TEXT_CENTERED);
            nk_layout_row_push(g_context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, g_timing_secondary_denominator.data(),
                           g_timing_secondary_denominator.size(), nk_filter_decimal) != 0;
            nk_layout_row_end(g_context);
            nk_layout_row_dynamic(g_context, 22, 1);
            changed |= nk_checkbox_label(g_context, "Seconds", &seconds) != 0;
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_WARP) {
            nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 3);
            nk_layout_row_push(g_context, 0.38f);
            nk_label(g_context,
                     view->stage_existing_object_id != 0 ? "Length in exact chart rows"
                                                         : "Length in current grid steps",
                     NK_TEXT_LEFT);
            nk_layout_row_push(g_context, 0.34f);
            changed |= nk_edit_string_zero_terminated(
                           g_context, NK_EDIT_FIELD, g_timing_secondary_numerator.data(),
                           g_timing_secondary_numerator.size(), nk_filter_decimal) != 0;
            nk_layout_row_push(g_context, 0.28f);
            nk_label(g_context,
                     view->stage_existing_object_id != 0 ? "rows (denominator 1)"
                                                         : "steps (denominator 1)",
                     NK_TEXT_LEFT);
            nk_layout_row_end(g_context);
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_STOP ||
                   view->stage_lane == WORM_UI_TIMING_LANE_DELAY) {
            nk_layout_row_dynamic(g_context, 18, 1);
            nk_labelf(g_context, NK_TEXT_LEFT, "Pause duration uses its authored unit: %s.",
                      pause_unit_label);
        }
        if (view->stage_status_text && view->stage_status_text[0]) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context, view->stage_status_text, NK_TEXT_LEFT);
        }
        nk_layout_row_begin(g_context, NK_DYNAMIC, 28,
                            view->stage_existing_object_id != 0 ? 3 : 2);
        nk_layout_row_push(g_context, view->stage_existing_object_id != 0 ? 0.40f : 0.50f);
        const auto confirm_clicked = nk_button_label(g_context, view->stage_existing_object_id != 0
                                                                    ? "Apply timing edit"
                                                                    : "Confirm timing event");
        nk_layout_row_push(g_context, view->stage_existing_object_id != 0 ? 0.30f : 0.50f);
        const auto cancel_clicked = nk_button_label(g_context, "Cancel");
        bool delete_clicked = false;
        if (view->stage_existing_object_id != 0) {
            nk_layout_row_push(g_context, 0.30f);
            delete_clicked = nk_button_label(g_context, "Delete timing event");
        }
        nk_layout_row_end(g_context);
        if (action && action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE && changed) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_UPDATE, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
        }
        if (action && action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
            action->timing_transition_is_seconds = seconds != 0;
            action->timing_toggle_primary = toggle_primary != 0;
            copy_timing_form_to_action(action);
        }
        if (action && delete_clicked) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_DELETE, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
            copy_timing_form_to_action(action);
        } else if (action && cancel_clicked) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_CANCEL, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
            copy_timing_form_to_action(action);
        } else if (action && confirm_clicked) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_CONFIRM, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
            copy_timing_form_to_action(action);
        }
        if (action && action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
            action->timing_toggle_primary = toggle_primary != 0;
        }
    }
    render_timing_recipe_form(view, action);
}

static void render_compose_field(const WormUiComposeView* view, WormUiWorkspaceAction* action) {
    if (g_compose_label_chart_id != view->chart_id ||
        g_compose_label_document_revision != view->document_revision ||
        g_compose_label_note_id != view->selected_note_label_note_id) {
        g_compose_note_label.fill('\0');
        if (view->selected_note_label && view->selected_note_label_note_id != 0) {
            const auto length =
                std::min(strlen(view->selected_note_label), g_compose_note_label.size() - 1);
            memcpy(g_compose_note_label.data(), view->selected_note_label, length);
            g_compose_note_label[length] = '\0';
        }
        g_compose_label_chart_id = view->chart_id;
        g_compose_label_document_revision = view->document_revision;
        g_compose_label_note_id = view->selected_note_label_note_id;
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
    if (g_compose_pointer_navigation_active && pointer_down) {
        pointer_navigation = g_compose_pointer_navigation_action;
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
        if (!g_compose_pointer_navigation_active && pointer_started &&
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

    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 4);
    nk_layout_row_push(g_context, 0.13f);
    nk_labelf(g_context, NK_TEXT_LEFT, "Label (%zu)", view->selected_note_count);
    nk_layout_row_push(g_context, 0.53f);
    if (!view->scene_available || view->selected_note_count == 0) {
        nk_widget_disable_begin(g_context);
    }
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, g_compose_note_label.data(),
                                         g_compose_note_label.size(), nk_filter_default);
    if (view->focus_note_label_editor) {
        nk_edit_focus(g_context, NK_EDIT_GOTO_END_ON_ACTIVATE);
        if (action) {
            action->compose_note_label_focus_consumed = true;
        }
    }
    nk_layout_row_push(g_context, 0.17f);
    if (nk_button_label(g_context, "Apply") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_note_label, g_compose_note_label.data(),
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
    nk_layout_row_dynamic(g_context, 22, 1);
    if (nk_button_label(g_context, "Add semantic layer") && action &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
    }
    if (visible_layer_count == 0) {
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_label(g_context, "Semantic layers: no visible palette entries", NK_TEXT_LEFT);
    } else {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, static_cast<int>(visible_layer_count + 1));
        nk_layout_row_push(g_context, 1.0f / static_cast<float>(visible_layer_count + 1));
        nk_label(g_context, view->semantic_layers_truncated ? "Layers+" : "Layers",
                 NK_TEXT_LEFT);
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

        const auto selected_layer = std::ranges::find_if(
            view->semantic_layers, view->semantic_layers + visible_layer_count,
            [](const auto& layer) { return layer.selected; });
        if (selected_layer != view->semantic_layers + visible_layer_count) {
            if (g_compose_semantic_layer_name_id != selected_layer->layer_id ||
                g_compose_semantic_layer_name_revision != view->document_revision) {
                g_compose_semantic_layer_name.fill('\0');
                const auto length = std::min(strlen(selected_layer->name),
                                             g_compose_semantic_layer_name.size() - 1);
                memcpy(g_compose_semantic_layer_name.data(), selected_layer->name, length);
                g_compose_semantic_layer_name_id = selected_layer->layer_id;
                g_compose_semantic_layer_name_revision = view->document_revision;
            }
            nk_layout_row_begin(g_context, NK_DYNAMIC, 24, 7);
            nk_layout_row_push(g_context, 0.35f);
            (void)nk_edit_string_zero_terminated(
                g_context, NK_EDIT_FIELD, g_compose_semantic_layer_name.data(),
                g_compose_semantic_layer_name.size(), nk_filter_default);
            nk_layout_row_push(g_context, 0.15f);
            if (nk_button_label(g_context, "Rename") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                memcpy(action->compose_semantic_layer_name, g_compose_semantic_layer_name.data(),
                       sizeof(action->compose_semantic_layer_name));
                action->compose_action = WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER;
            }
            nk_layout_row_push(g_context, 0.12f);
            if (nk_button_label(g_context, "Color") && action &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_CYCLE_SEMANTIC_LAYER_COLOR;
            }
            nk_layout_row_push(g_context, 0.12f);
            if (nk_button_label(g_context, "Delete") && action &&
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
            if (nk_button_label(g_context,
                                view->semantic_layer_target_visible ? "Hide" : "Show") &&
                action && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY;
            }
            nk_layout_row_end(g_context);
        }

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
        const auto layer_paint_enabled = view->scene_available && semantic_target_active &&
                                         !view->semantic_layer_target_locked;
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
        const auto solo_clicked = nk_button_label(
            g_context, view->semantic_layer_solo_active ? "Solo: On" : "Solo: Off");
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
        const auto lock_clicked = nk_button_label(
            g_context, view->semantic_layer_target_locked ? "Unlock" : "Lock");
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
             "hold Space + ↑/↓ then release selects; tap Space twice selects cursor range · lane keys assign/hold · "
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
    const auto surface_height =
        std::max(32.0f, group_bounds.h - 54.0f - 36.0f - semantic_toolbar_height -
                            branch_panel_height);
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
        g_compose_keyboard_focus = true;
    } else if (nk_input_any_mouse_click_in_rect(&g_context->input,
                                                nk_rect(0.0f, 0.0f, 1000000.0f, 1000000.0f)) &&
               !pointer_inside) {
        g_compose_keyboard_focus = false;
        if (!g_compose_pointer_navigation_active) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL);
        }
        if (g_compose_space_range_hold_active) {
            append_compose_cancel_once(action);
            g_compose_space_range_hold_active = false;
            g_compose_space_range_hold_moved = false;
        }
    }
    process_compose_key_events(view, action);

    const auto pointer_x = (static_cast<double>(g_context->input.mouse.pos.x) - viewport.x) *
                           view->viewport_width_pixels / viewport.w;
    const auto pointer_y = (static_cast<double>(g_context->input.mouse.pos.y) - viewport.y) *
                           view->viewport_height_pixels / viewport.h;
    if (!g_compose_action_overflowed && g_window_focused && view->scene_available) {
        action->compose_viewport_pointer_valid = true;
        action->compose_viewport_pointer_x_pixels = pointer_x;
        action->compose_viewport_pointer_y_pixels = pointer_y;
        if (g_compose_middle_click_pending &&
            g_compose_middle_click_x >= viewport.x &&
            g_compose_middle_click_x <= viewport.x + viewport.w &&
            g_compose_middle_click_y >= viewport.y &&
            g_compose_middle_click_y <= viewport.y + viewport.h) {
            action->compose_viewport_middle_click = true;
            action->compose_viewport_middle_click_x_pixels =
                (static_cast<double>(g_compose_middle_click_x) - viewport.x) *
                view->viewport_width_pixels / viewport.w;
            action->compose_viewport_middle_click_y_pixels =
                (static_cast<double>(g_compose_middle_click_y) - viewport.y) *
                view->viewport_height_pixels / viewport.h;
        }
    }
    g_compose_middle_click_pending = false;
    if (!g_compose_action_overflowed && view->scene_available && g_window_focused) {
        for (size_t index = 0; index < g_compose_wheel_event_count; ++index) {
            const auto& wheel = g_compose_wheel_events[index];
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
    g_compose_wheel_event_count = 0;
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
    const auto clamp_paint_x = [&](double value) {
        return std::clamp(value, 0.0, view->viewport_width_pixels);
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
        g_compose_note_paint_pointer_active || g_compose_semantic_layer_paint_pointer_active ||
        g_compose_pointer_hold_active ||
        g_compose_lane_hold_active || g_compose_note_move_active ||
        g_compose_area_selection_active;
    action->compose_has_pointer_hover =
        !g_compose_action_overflowed && g_window_focused && view->scene_available &&
        pointer_inside && !compose_pointer_captured;
    if (action->compose_has_pointer_hover) {
        action->compose_pointer_hover_x_pixels = pointer_x;
        action->compose_pointer_hover_y_pixels = pointer_y;
    }
    bool compose_note_double_click_in_view = false;
    if (!compose_pointer_captured && g_window_focused && view->scene_available) {
        for (size_t click = 0; click < g_timing_double_click_count; ++click) {
            const auto& pointer = g_timing_double_clicks[click];
            if (pointer.x < viewport.x || pointer.x >= viewport.x + viewport.w ||
                pointer.y < viewport.y || pointer.y >= viewport.y + viewport.h) {
                continue;
            }
            append_compose_input(
                action, WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK,
                (static_cast<double>(pointer.x) - viewport.x) * view->viewport_width_pixels /
                    viewport.w,
                (static_cast<double>(pointer.y) - viewport.y) * view->viewport_height_pixels /
                    viewport.h);
            compose_note_double_click_in_view = true;
        }
    }
    bool paint_pointer_consumed = g_compose_note_paint_pointer_active ||
                                  g_compose_semantic_layer_paint_pointer_active ||
                                  compose_action_has_cancel(action);
    if (!g_compose_action_overflowed && g_compose_semantic_layer_paint_pointer_active) {
        if (!g_window_focused || !view->scene_available ||
            !view->semantic_layer_paint_mode_active || g_shortcut_event_count != 0 ||
            (pointer_modifiers & any_pointer_modifier) != 0 || (!left_down && !left_released)) {
            append_compose_cancel_once(action);
            g_compose_semantic_layer_paint_pointer_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                 paint_pointer_x, paint_pointer_y);
            g_compose_semantic_layer_paint_pointer_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
                                 paint_pointer_x, paint_pointer_y);
        }
    } else if (!g_compose_action_overflowed && view->scene_available &&
               view->semantic_layer_paint_mode_active && pointer_started_in_view &&
               g_window_focused && g_shortcut_event_count == 0 &&
               (pointer_modifiers & any_pointer_modifier) == 0 &&
               !g_compose_pointer_hold_active && !g_compose_lane_hold_active &&
               !g_compose_area_selection_active && !g_compose_area_selection_candidate_active) {
        paint_pointer_consumed = true;
        if (compose_point_hits_note(view, paint_anchor_x, paint_anchor_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
                                 paint_anchor_x, paint_anchor_y);
            g_compose_semantic_layer_paint_pointer_active =
                !g_compose_action_overflowed && left_down;
            if (!g_compose_action_overflowed && !left_down) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
                                     paint_pointer_x, paint_pointer_y);
            }
        }
    } else if (!g_compose_action_overflowed && g_compose_note_paint_pointer_active) {
        if (!g_window_focused || !view->scene_available || !view->note_paint_mode_active ||
            g_shortcut_event_count != 0 || (pointer_modifiers & any_pointer_modifier) != 0 ||
            (!left_down && !left_released)) {
            append_compose_cancel_once(action);
            g_compose_note_paint_pointer_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, paint_pointer_x,
                                 paint_pointer_y);
            g_compose_note_paint_pointer_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE, paint_pointer_x,
                                 paint_pointer_y);
        }
    } else if (!g_compose_action_overflowed && view->scene_available &&
               view->note_paint_mode_active && pointer_started_in_view && g_window_focused &&
               g_shortcut_event_count == 0 && (pointer_modifiers & any_pointer_modifier) == 0 &&
               !g_compose_pointer_hold_active && !g_compose_lane_hold_active &&
               !g_compose_area_selection_active && !g_compose_area_selection_candidate_active) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN, paint_anchor_x,
                             paint_anchor_y);
        paint_pointer_consumed = true;
        g_compose_note_paint_pointer_active = !g_compose_action_overflowed && left_down;
        if (!g_compose_action_overflowed && !left_down) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, paint_pointer_x,
                                 paint_pointer_y);
        }
    }

    if (!g_compose_action_overflowed && !paint_pointer_consumed && view->scene_available &&
        pointer_pressed_in_view && alt_down && !g_compose_pointer_hold_active && g_window_focused &&
        !compose_note_double_click_in_view) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN, pointer_x, pointer_y);
        g_compose_pointer_hold_active = !g_compose_action_overflowed;
    } else if (!g_compose_action_overflowed && !paint_pointer_consumed &&
               g_compose_pointer_hold_active) {
        if (!g_window_focused || !alt_down) {
            append_compose_cancel_once(action);
            g_compose_pointer_hold_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE, pointer_x,
                                 pointer_y);
            g_compose_pointer_hold_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE, pointer_x,
                                 pointer_y);
        }
    } else if (!g_compose_action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_pressed_in_view && g_window_focused && !alt_down &&
               !g_compose_note_move_active && !g_compose_note_move_candidate_active &&
               !g_compose_area_selection_active && !g_compose_area_selection_candidate_active &&
               !compose_note_double_click_in_view) {
        if (compose_point_hits_selected_note(view, pointer_x, pointer_y) && left_down) {
            g_compose_note_move_candidate_active = true;
            g_compose_note_move_anchor_x = pointer_x;
            g_compose_note_move_anchor_y = pointer_y;
            g_compose_note_move_anchor_screen_x = g_context->input.mouse.pos.x;
            g_compose_note_move_anchor_screen_y = g_context->input.mouse.pos.y;
        } else if (compose_point_hits_note(view, pointer_x, pointer_y)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
        } else if (left_down) {
            g_compose_area_selection_candidate_active = true;
            g_compose_area_selection_anchor_x = pointer_x;
            g_compose_area_selection_anchor_y = pointer_y;
            g_compose_area_selection_anchor_screen_x = g_context->input.mouse.pos.x;
            g_compose_area_selection_anchor_screen_y = g_context->input.mouse.pos.y;
            g_compose_area_selection_mode =
                compose_selection_mode(static_cast<uint16_t>(SDL_GetModState()));
        } else {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
        }
    } else if (!g_compose_action_overflowed && !paint_pointer_consumed && view->scene_available &&
               pointer_clicked && g_window_focused && !g_compose_pointer_hold_active &&
               !g_compose_note_move_active && !g_compose_note_move_candidate_active &&
               !g_compose_area_selection_active && !g_compose_area_selection_candidate_active &&
               !compose_note_double_click_in_view) {
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK, pointer_x, pointer_y);
    }

    if (!g_compose_action_overflowed && !paint_pointer_consumed &&
        g_compose_note_move_candidate_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            g_compose_note_move_candidate_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 g_compose_note_move_anchor_x, g_compose_note_move_anchor_y);
            g_compose_note_move_candidate_active = false;
        } else if (left_down) {
            constexpr float drag_threshold_pixels = 4.0f;
            const auto dx = g_context->input.mouse.pos.x - g_compose_note_move_anchor_screen_x;
            const auto dy = g_context->input.mouse.pos.y - g_compose_note_move_anchor_screen_y;
            if (std::hypot(dx, dy) >= drag_threshold_pixels) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
                                     g_compose_note_move_anchor_x, g_compose_note_move_anchor_y);
                g_compose_note_move_candidate_active = false;
                g_compose_note_move_active = !g_compose_action_overflowed;
                if (g_compose_note_move_active) {
                    append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE,
                                         pointer_x, pointer_y);
                }
            }
        }
    } else if (!g_compose_action_overflowed && !paint_pointer_consumed &&
               g_compose_note_move_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            g_compose_note_move_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, pointer_x,
                                 pointer_y);
            g_compose_note_move_active = false;
        } else if (left_down && nk_input_is_mouse_moved(&g_context->input)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, pointer_x,
                                 pointer_y);
        }
    } else if (!g_compose_action_overflowed && !paint_pointer_consumed &&
               g_compose_area_selection_candidate_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            if (g_window_focused) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            }
            g_compose_area_selection_candidate_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
                                 g_compose_area_selection_anchor_x,
                                 g_compose_area_selection_anchor_y);
            g_compose_area_selection_candidate_active = false;
        } else if (left_down) {
            constexpr float drag_threshold_pixels = 4.0f;
            const auto dx = g_context->input.mouse.pos.x - g_compose_area_selection_anchor_screen_x;
            const auto dy = g_context->input.mouse.pos.y - g_compose_area_selection_anchor_screen_y;
            if (std::hypot(dx, dy) >= drag_threshold_pixels) {
                append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
                                     g_compose_area_selection_anchor_x,
                                     g_compose_area_selection_anchor_y, 0, false,
                                     g_compose_area_selection_mode);
                g_compose_area_selection_candidate_active = false;
                g_compose_area_selection_active = !g_compose_action_overflowed;
                if (g_compose_area_selection_active) {
                    append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
                                         pointer_x, pointer_y);
                }
            }
        }
    } else if (!g_compose_action_overflowed && !paint_pointer_consumed &&
               g_compose_area_selection_active) {
        if (!g_window_focused || (!left_down && !left_released)) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            g_compose_area_selection_active = false;
        } else if (left_released) {
            append_compose_input(action, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE, pointer_x,
                                 pointer_y);
            g_compose_area_selection_active = false;
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
                const auto color = hold.proposal_origin
                                       ? nk_rgb(51, 82, 100)
                                       : (hold.semantic_color_visible
                                              ? nk_rgba(hold.semantic_red, hold.semantic_green,
                                                        hold.semantic_blue, hold.semantic_alpha)
                                              : (hold.selected ? nk_rgb(62, 170, 204)
                                                               : nk_rgb(48, 111, 151)));
                nk_fill_rect(canvas, rect, 0.0f, color);
                if (hold.selected) {
                    nk_stroke_rect(canvas, rect, 0.0f, 1.6f, nk_rgb(220, 247, 255));
                }
                if (view->hover_note_visible && hold.note_id == view->hover_note_id) {
                    nk_stroke_rect(canvas, rect, 0.0f, 2.0f, nk_rgb(255, 235, 148));
                }
            }
        }
        for (size_t index = 0; index < view->note_endpoint_count; ++index) {
            const auto& note = view->note_endpoints[index];
            auto color = note.proposal_origin
                             ? nk_rgb(73, 101, 120)
                             : (note.semantic_color_visible
                                    ? nk_rgba(note.semantic_red, note.semantic_green,
                                              note.semantic_blue, note.semantic_alpha)
                                    : (note.selected ? nk_rgb(111, 221, 236)
                                                     : nk_rgb(133, 182, 222)));
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
        if (view->cursor) {
            const auto rect = compose_screen_rect(&view->cursor->bounds, view, viewport);
            if (rect.w > 0.0f && rect.h > 0.0f) {
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
                                     anchor_bright ? nk_rgb(255, 224, 122)
                                                   : nk_rgb(185, 142, 72));
            } else {
                draw_space_range_row(view->space_range_anchor_y_pixels,
                                     anchor_bright ? nk_rgb(255, 224, 122)
                                                   : nk_rgb(185, 142, 72));
                draw_space_range_row(view->space_range_tail_y_pixels,
                                     anchor_bright ? nk_rgb(97, 201, 255)
                                                   : nk_rgb(63, 125, 176));
            }
        }
        if (view->area_selection_preview_visible) {
            draw_selection_preview(view->area_selection_preview_bounds,
                                   view->area_selection_preview_mode);
        }
        if (view->viewport_autoscroll_active &&
            std::isfinite(view->viewport_autoscroll_anchor_x_pixels) &&
            std::isfinite(view->viewport_autoscroll_anchor_y_pixels)) {
            const auto anchor_x = viewport.x +
                                  static_cast<float>(std::clamp(
                                      view->viewport_autoscroll_anchor_x_pixels, 0.0,
                                      view->viewport_width_pixels) *
                                      viewport.w / view->viewport_width_pixels);
            const auto anchor_y = viewport.y +
                                  static_cast<float>(std::clamp(
                                      view->viewport_autoscroll_anchor_y_pixels, 0.0,
                                      view->viewport_height_pixels) *
                                      viewport.h / view->viewport_height_pixels);
            nk_stroke_line(canvas, anchor_x - 9.0f, anchor_y, anchor_x + 9.0f, anchor_y, 2.0f,
                           nk_rgb(255, 211, 103));
            nk_stroke_line(canvas, anchor_x, anchor_y - 9.0f, anchor_x, anchor_y + 9.0f, 2.0f,
                           nk_rgb(255, 211, 103));
            const auto indicator = nk_rect(viewport.x + 8.0f, viewport.y + viewport.h - 26.0f,
                                           std::min(250.0f, std::max(150.0f, viewport.w - 16.0f)),
                                           18.0f);
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
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    if (!timing_recipe_range_drag_context_matches(view)) {
        cancel_timing_recipe_range_drag();
    }
    if (!timing_marker_drag_context_matches(view)) {
        cancel_timing_marker_drag();
    }
    if (!g_context || !g_window || !view || !view->project_title || !view->artist ||
        !view->selected_difficulty || !view->status_text || !view->git_hash || !view->tab_heading ||
        view->summary_line_count > WORM_UI_WORKSPACE_SUMMARY_CAPACITY ||
        view->recovery_candidate_count > WORM_UI_RECOVERY_ROW_CAPACITY ||
        view->recovery_diagnostic_count > WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY ||
        (view->metadata.visible &&
         (view->metadata.format_field_count > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY ||
          view->metadata.format_target_count > WORM_UI_METADATA_FORMAT_TARGET_CAPACITY)) ||
        (view->timing.visible && !timing_view_valid(&view->timing)) ||
        (view->compose.visible &&
         (view->compose.band_count > WORM_UI_COMPOSE_BAND_CAPACITY ||
          view->compose.lane_count > WORM_UI_COMPOSE_LANE_CAPACITY ||
          view->compose.grid_line_count > WORM_UI_COMPOSE_GRID_CAPACITY ||
          view->compose.difference_count > WORM_UI_COMPOSE_DIFFERENCE_CAPACITY ||
          view->compose.hold_body_count > WORM_UI_COMPOSE_HOLD_CAPACITY ||
          view->compose.note_endpoint_count > WORM_UI_COMPOSE_NOTE_CAPACITY ||
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
          (view->compose.semantic_layer_count != 0 && !view->compose.semantic_layers) ||
          (view->compose.lane_key_binding_count != 0 && !view->compose.lane_key_bindings) ||
          (view->compose.branch_row_count != 0 && !view->compose.branch_rows) ||
          !compose_geometry_valid(&view->compose))) ||
        view->active_tab < WORM_UI_WORKSPACE_TAB_SETUP ||
        view->active_tab > WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE) {
        g_compose_wheel_event_count = 0;
        g_compose_middle_click_pending = false;
        cancel_timing_recipe_range_drag();
        expose_shortcut_input(&action);
        return action;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(g_window, &width, &height) || width <= 0 || height <= 0) {
        g_compose_wheel_event_count = 0;
        g_compose_middle_click_pending = false;
        cancel_timing_recipe_range_drag();
        expose_shortcut_input(&action);
        return action;
    }

    if (g_timing_recipe_cancel_pending) {
        if (view->active_tab == WORM_UI_WORKSPACE_TAB_TIMING && view->timing.visible &&
            (view->timing.sv_recipe.active || view->timing.sv_recipe.selection_armed)) {
            set_timing_recipe_action(&action, &view->timing.sv_recipe,
                                     WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
        }
        g_timing_recipe_cancel_pending = false;
    }
    if (g_timing_marker_drag_cancel_pending) {
        if (view->active_tab == WORM_UI_WORKSPACE_TAB_TIMING && view->timing.visible &&
            view->timing.stage_active && view->timing.stage_existing_object_id != 0) {
            set_timing_action(&action, &view->timing, WORM_UI_TIMING_STAGE_CANCEL);
        }
        g_timing_marker_drag_cancel_pending = false;
    }

    if (view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE && view->compose.visible) {
        g_compose_action_overflowed = false;
        action.compose_input_chart_id = view->compose.chart_id;
        action.compose_input_document_revision = view->compose.document_revision;
        action.compose_input_viewport_width_pixels = view->compose.viewport_width_pixels;
        if (!view->compose.scene_available) {
            cancel_compose_navigation(&action);
        }
        if (g_compose_cancel_pending) {
            append_compose_input(&action, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
            g_compose_cancel_pending = false;
            g_compose_note_paint_pointer_active = false;
            g_compose_semantic_layer_paint_pointer_active = false;
            g_compose_space_range_hold_active = false;
            g_compose_space_range_hold_moved = false;
            g_compose_note_move_candidate_active = false;
            g_compose_note_move_active = false;
        }
        const auto chart_changed = g_last_compose_chart_id != view->compose.chart_id;
        const auto revision_changed =
            g_last_compose_document_revision != view->compose.document_revision;
        if (chart_changed || revision_changed) {
            if (g_compose_note_paint_pointer_active ||
                g_compose_semantic_layer_paint_pointer_active ||
                g_compose_space_range_hold_active || g_compose_note_move_active) {
                append_compose_cancel_once(&action);
            }
            g_compose_pointer_hold_active = false;
            g_compose_note_paint_pointer_active = false;
            g_compose_semantic_layer_paint_pointer_active = false;
            g_compose_lane_hold_active = false;
            g_compose_space_range_hold_active = false;
            g_compose_space_range_hold_moved = false;
            g_compose_note_move_candidate_active = false;
            g_compose_note_move_active = false;
            g_compose_area_selection_candidate_active = false;
            g_compose_area_selection_active = false;
            g_compose_lane_hold_keycode = 0;
            g_compose_lane_hold_lane_id = 0;
            cancel_compose_navigation(&action);
        }
        if (chart_changed) {
            // Events received before the first frame for another chart still
            // target the previously visible geometry. Events after an ordinary
            // document edit already belong to the current frame/revision.
            g_compose_key_event_count = 0;
            g_compose_wheel_event_count = 0;
            g_compose_middle_click_pending = false;
            g_compose_keyboard_focus = false;
        }
        if (chart_changed || revision_changed) {
            g_last_compose_chart_id = view->compose.chart_id;
            g_last_compose_document_revision = view->compose.document_revision;
        }
        if (g_compose_note_paint_pointer_active &&
            (!g_window_focused || !view->compose.scene_available ||
             !view->compose.note_paint_mode_active)) {
            append_compose_cancel_once(&action);
            g_compose_note_paint_pointer_active = false;
        }
        if (g_compose_semantic_layer_paint_pointer_active &&
            (!g_window_focused || !view->compose.scene_available ||
             !view->compose.semantic_layer_paint_mode_active)) {
            append_compose_cancel_once(&action);
            g_compose_semantic_layer_paint_pointer_active = false;
        }
    } else {
        g_compose_cancel_pending = false;
        g_compose_key_event_count = 0;
        g_compose_wheel_event_count = 0;
        g_compose_middle_click_pending = false;
        g_compose_keyboard_focus = false;
        if (g_compose_pointer_hold_active || g_compose_lane_hold_active ||
            g_compose_note_paint_pointer_active || g_compose_semantic_layer_paint_pointer_active ||
            g_compose_space_range_hold_active ||
            g_compose_area_selection_active) {
            append_compose_cancel_once(&action);
        }
        g_compose_pointer_hold_active = false;
        g_compose_note_paint_pointer_active = false;
        g_compose_semantic_layer_paint_pointer_active = false;
        g_compose_lane_hold_active = false;
        g_compose_space_range_hold_active = false;
        g_compose_space_range_hold_moved = false;
        g_compose_area_selection_candidate_active = false;
        g_compose_area_selection_active = false;
        cancel_compose_navigation(&action);
        g_last_compose_chart_id = 0;
        g_last_compose_document_revision = 0;
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
                render_metadata_editor(&view->metadata, &action);
            } else if (view->timing.visible) {
                render_timing_lane(&view->timing, &action);
            } else if (view->compose.visible) {
                render_compose_field(&view->compose, &action);
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
    const bool compose_lane_focused = g_window_focused && g_compose_keyboard_focus &&
                                      view->active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE &&
                                      view->compose.visible && view->compose.scene_available &&
                                      view->compose.cursor && view->compose.cursor->lane_id != 0;
    expose_shortcut_input(&action, compose_lane_focused,
                          compose_lane_focused ? view->compose.cursor->lane_id : 0);
    return action;
}

void worm_ui_frontend_render(int window_width, int window_height) {
    (void)window_width;
    (void)window_height;
    if (g_context) {
        nk_sdl_render(NK_ANTI_ALIASING_ON, WORMEDIT_MAX_VERTEX_MEMORY, WORMEDIT_MAX_ELEMENT_MEMORY);
    }
}
