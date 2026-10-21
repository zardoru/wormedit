#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef union SDL_Event SDL_Event;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Window SDL_Window;

#define WORM_UI_WORKSPACE_TAB_COUNT 7
#define WORM_UI_COMPOSE_LANE_PROFILE_NAME_BYTES 48
#define WORM_UI_COMPOSE_LAYOUT_PROFILE_NAME_BYTES 64
#define WORM_UI_COMPOSE_LAYOUT_PROFILE_ID_BYTES 64
#define WORM_UI_COMPOSE_MEASURE_REMOVAL_MESSAGE_BYTES 192
#define WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD 2
// Window chrome taken by the BGM bar row under the transport seek bar (48 px row + 4 px spacing).
#define WORM_UI_BGM_BAR_CHROME_HEIGHT 52.0f
#define WORM_UI_COMPOSE_NOTE_LABEL_BYTES 4096
#define WORM_UI_COMPOSE_QUANTIZE_PRESET_NAME_BYTES 64
#define WORM_UI_COMPOSE_IMPORT_PATH_BYTES 4096
#define WORM_UI_COMPOSE_MARKER_LABEL_BYTES 4096
#define WORM_UI_COMPOSE_MARKER_TIME_BYTES 32
#define WORM_UI_TIMING_RECIPE_RATIONAL_TEXT_BYTES 64
#define WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES 32
#define WORM_UI_PLAYER_PREVIEW_IMAGE_MAX_DIMENSION 16384
#define WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE 24.0
#define WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE 4096.0

namespace wormedit::ui {
// Visible shortcut/menu windows and input admission are work limits, independent of storage.
inline constexpr size_t semantic_layer_shortcut_limit = 9;
inline constexpr size_t semantic_layer_window_limit = 256;
inline constexpr size_t note_layer_window_limit = 18;
inline constexpr size_t semantic_layer_name_input_bytes = 128;
inline constexpr size_t sound_drop_resource_limit = 1295;

// Admission bounds protect one input frame independently of payload storage.
inline constexpr size_t compose_input_admission_limit = 16;
inline constexpr size_t compose_viewport_input_admission_limit = 16;
inline constexpr size_t shortcut_event_admission_limit = 64;
} // namespace wormedit::ui
