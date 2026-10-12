#pragma once
#include "compose.h"

typedef struct WormUiTimingSpeedView {
    uint64_t object_id;
    uint64_t source_order;
    uint64_t storage_order;
    double marker_y_pixels;
    double marker_left_fraction;
    double marker_right_fraction;
    double affected_begin_y_pixels;
    double affected_end_y_pixels;
    double multiplier;
    double transition_duration;
    bool transition_is_seconds;
    bool transition_clipped;
} WormUiTimingSpeedView;

// Same numeric kind ordering as editor_grid_line_kind_t, kept scalar across
// the frontend boundary: active snap, beat, group, then measure.
typedef struct WormUiTimingGridLineView {
    double y_pixels;
    uint8_t kind;
    uint8_t emphasis;
} WormUiTimingGridLineView;

typedef enum WormUiTimingPauseUnit {
    WORM_UI_TIMING_PAUSE_SECONDS = 0,
    WORM_UI_TIMING_PAUSE_MILLISECONDS,
    WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS,
    WORM_UI_TIMING_PAUSE_BEATS,
} WormUiTimingPauseUnit;

// Renderer-neutral scalar projection of the timing pane. SPEEDS spans are
// evaluated by the timing core before crossing this private C boundary.
typedef struct WormUiTimingView {
    bool input_occluded;
    bool visible;
    bool scene_available;
    bool truncated;
    double viewport_width_pixels;
    double viewport_height_pixels;
    uint64_t chart_id;
    uint64_t document_revision;
    uint64_t total_rows;
    uint64_t active_snap_denominator;
    const uint64_t* supported_snap_denominators;
    size_t supported_snap_denominator_count;
    const char* status_text;
    size_t measure_count;
    size_t bpm_count;
    bool initial_bpm_editable;
    bool initial_bpm_present;
    uint64_t initial_bpm_event_id;
    double initial_bpm_value;
    size_t pause_count;
    size_t warp_count;
    size_t bookmark_count;
    size_t section_count;
    size_t anchor_count;
    size_t speed_total_count;
    const WormUiTimingSpeedView* speeds;
    size_t speed_count;
    const WormUiTimingMarkerView* markers;
    size_t marker_count;
    const WormUiTimingGridLineView* grid_lines;
    size_t grid_line_count;
    bool stage_active;
    bool stage_valid;
    uint64_t stage_existing_object_id;
    WormUiTimingLaneKind stage_lane;
    double stage_marker_y_pixels;
    double stage_value;
    double stage_secondary_value;
    bool stage_transition_is_seconds;
    bool stage_toggle_primary;
    WormUiTimingPauseUnit stage_pause_unit;
    char stage_primary_decimal_text[64];
    bool stage_primary_decimal_exact;
    char stage_primary_numerator_text[64];
    char stage_primary_denominator_text[64];
    char stage_secondary_numerator_text[64];
    char stage_secondary_denominator_text[64];
    const char* stage_status_text;
} WormUiTimingView;

typedef enum WormUiTimingStageAction {
    WORM_UI_TIMING_STAGE_NONE = 0,
    WORM_UI_TIMING_STAGE_BEGIN,
    WORM_UI_TIMING_STAGE_UPDATE,
    // MOVE carries a normalized target row for an already-open existing
    // marker. The application snaps it to the active exact timing grid.
    WORM_UI_TIMING_STAGE_MOVE,
    WORM_UI_TIMING_STAGE_CONFIRM,
    WORM_UI_TIMING_STAGE_CANCEL,
    WORM_UI_TIMING_STAGE_DELETE,
    // Begins a new event at the target row with its default value (a BPM equal
    // to the one in force, or an anchor) and confirms it in one step.
    WORM_UI_TIMING_STAGE_QUICK_INSERT,
} WormUiTimingStageAction;

typedef enum WormUiPlayerPreviewSkin {
    WORM_UI_PLAYER_PREVIEW_SKIN_NEUTRAL = 0,
    WORM_UI_PLAYER_PREVIEW_SKIN_MANIA,
    WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS,
} WormUiPlayerPreviewSkin;

typedef enum WormUiVisualFitPolicy {
    WORM_UI_VISUAL_FIT_STRETCH = 0,
    WORM_UI_VISUAL_FIT_CONTAIN,
    WORM_UI_VISUAL_FIT_COVER,
} WormUiVisualFitPolicy;

typedef enum WormUiPlayerPreviewVisualKind {
    WORM_UI_PLAYER_PREVIEW_VISUAL_BACKGROUND = 0,
    WORM_UI_PLAYER_PREVIEW_VISUAL_POOR,
    WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER,
    WORM_UI_PLAYER_PREVIEW_VISUAL_VIDEO,
    WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD,
    WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER2,
} WormUiPlayerPreviewVisualKind;

typedef enum WormUiPlayerPreviewVisualDiagnostic {
    WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_NONE = 0,
    WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_FRAME_LATE,
    WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_DECODE_FAILED,
} WormUiPlayerPreviewVisualDiagnostic;

typedef WormUiComposeRect WormUiPlayerPreviewRect;

typedef struct WormUiPlayerPreviewLaneView {
    uint64_t lane_id;
    uint8_t role;
    uint8_t glyph;
    uint32_t display_order;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewLaneView;

typedef struct WormUiPlayerPreviewReceptorView {
    uint64_t lane_id;
    uint8_t glyph;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewReceptorView;

typedef struct WormUiPlayerPreviewNoteView {
    uint64_t note_id;
    uint64_t lane_id;
    uint8_t kind;
    uint8_t glyph;
    bool tail;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewNoteView;

typedef struct WormUiPlayerPreviewHoldView {
    uint64_t note_id;
    uint64_t lane_id;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewHoldView;

typedef struct WormUiPlayerPreviewVisualLayerView {
    uint64_t object_id;
    uint64_t resource_id;
    uint64_t event_sample_frame;
    WormUiPlayerPreviewVisualKind kind;
    size_t draw_order;
    size_t source_order;
    bool video_clock_ready;
    uint64_t video_elapsed_sample_frames;
    uint64_t video_time_numerator;
    uint32_t video_time_denominator;
    bool static_image;
    bool video_frame;
    bool image_ready;
    bool image_pending;
    WormUiPlayerPreviewVisualDiagnostic diagnostic;
    const char* diagnostic_text;
    uint64_t image_content_id;
    uint32_t image_width;
    uint32_t image_height;
    const uint8_t* rgba_pixels;
    size_t rgba_byte_count;
} WormUiPlayerPreviewVisualLayerView;

typedef struct WormUiPlayerPreviewStoryboardSpriteView {
    uint64_t stable_id;
    uint64_t resource_id;
    size_t source_order;
    uint8_t layer_order;
    double x;
    double y;
    double origin_x;
    double origin_y;
    double scale_x;
    double scale_y;
    double rotation_radians;
    double opacity;
    double red;
    double green;
    double blue;
    bool image_ready;
    bool image_pending;
    uint64_t image_content_id;
    uint32_t image_width;
    uint32_t image_height;
    const uint8_t* rgba_pixels;
    size_t rgba_byte_count;
} WormUiPlayerPreviewStoryboardSpriteView;

typedef struct WormUiPlayerPreviewView {
    bool visible;
    bool scene_available;
    bool truncated;
    bool pre_zero;
    bool split_stage;
    WormUiPlayerPreviewSkin skin;
    WormUiVisualFitPolicy visual_fit;
    uint64_t project_id;
    uint64_t chart_id;
    uint64_t document_revision;
    uint64_t sample_frame;
    double viewport_width_pixels;
    double viewport_height_pixels;
    const char* status_text;
    const WormUiPlayerPreviewLaneView* lanes;
    size_t lane_count;
    const WormUiPlayerPreviewReceptorView* receptors;
    size_t receptor_count;
    const WormUiPlayerPreviewHoldView* hold_bodies;
    size_t hold_body_count;
    const WormUiPlayerPreviewNoteView* note_endpoints;
    size_t note_endpoint_count;
    const WormUiPlayerPreviewVisualLayerView* visual_layers;
    size_t visual_layer_count;
    const WormUiPlayerPreviewStoryboardSpriteView* storyboard_sprites;
    size_t storyboard_sprite_count;
} WormUiPlayerPreviewView;

// Private scalar view consumed by the selected frontend adapter. The owning
// application keeps every pointed-to string alive for the duration of compose.
// Plain view/action data; the application owns persistence and launching.
struct WormUiExternalPlayerDefinitionView {
    const char* name;
    const char* path;
    const char* start_arguments;
    const char* here_arguments;
    const char* stop_arguments;
};
struct WormUiExternalPlayerView {
    int visible;
    uint64_t revision;
    const struct WormUiExternalPlayerDefinitionView* players;
    size_t player_count;
    size_t selected;
};
enum WormUiExternalPlayerActionKind {
    WORM_UI_EXTERNAL_PLAYER_NONE,
    WORM_UI_EXTERNAL_PLAYER_SELECT,
    WORM_UI_EXTERNAL_PLAYER_SAVE,
    WORM_UI_EXTERNAL_PLAYER_ADD,
    WORM_UI_EXTERNAL_PLAYER_REMOVE,
    WORM_UI_EXTERNAL_PLAYER_CLOSE,
    WORM_UI_EXTERNAL_PLAYER_OPEN,
    WORM_UI_EXTERNAL_PLAYER_PLAY_START,
    WORM_UI_EXTERNAL_PLAYER_PLAY_HERE,
    WORM_UI_EXTERNAL_PLAYER_STOP
};
struct WormUiExternalPlayerAction {
    enum WormUiExternalPlayerActionKind kind;
    size_t selected;
    char name[4097];
    char path[4097];
    char start_arguments[4097];
    char here_arguments[4097];
    char stop_arguments[4097];
};

typedef struct WormUiTapTimingView {
    bool visible;
    bool playing;
    bool source_available;
    bool estimate_available;
    bool stable;
    bool downbeat_flash;
    uint16_t beats_per_measure;
    uint16_t active_segment;
    uint64_t tap_count;
    uint64_t tap_capacity;
    double bpm;
    double ms_per_beat;
    double offset_ms;
    double deviation_ms;
    double confidence;
    uint8_t bpm_choice;
    bool apply_available;
    uint64_t apply_revision;
    double proposed_bpm;
    double proposed_offset_ms;
    const char* status;
    bool tap_flash;
    uint16_t tap_segment;
    bool timing_locked;
    bool music_available;
    bool input_blocked;
    bool close_key_pressed;
} WormUiTapTimingView;

typedef struct WormUiTapTimingAction {
    bool toggle;
    bool close;
    bool reset;
    bool choose_bpm;
    uint8_t bpm_choice;
    bool apply;
    uint64_t apply_revision;
} WormUiTapTimingAction;

typedef struct WormUiTimingSettingsAction {
    bool toggle;
    bool close;
    bool change_enable;
    bool enabled;
    bool change_volume;
    uint8_t volume;
    bool change_count_in;
    uint8_t count_in;
} WormUiTimingSettingsAction;
