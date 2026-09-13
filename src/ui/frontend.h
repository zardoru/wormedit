#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORM_UI_WORKSPACE_TAB_COUNT 7
#define WORM_UI_WORKSPACE_SUMMARY_CAPACITY 16
#define WORM_UI_RECOVERY_ROW_CAPACITY 4
#define WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY 4
#define WORM_UI_RECOVERY_DESTINATION_CAPACITY 512
#define WORM_UI_METADATA_SEARCH_TAG_CAPACITY 4096
#define WORM_UI_METADATA_SEARCH_TAG_BYTES 4096
#define WORM_UI_METADATA_TITLE_BYTES 16384
#define WORM_UI_METADATA_CHART_TEXT_BYTES 16384
#define WORM_UI_METADATA_FORMAT_NAME_BYTES 1024
#define WORM_UI_METADATA_FORMAT_VALUE_BYTES 1048576
#define WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY 16384
#define WORM_UI_METADATA_FORMAT_FIELD_CAPACITY 128
#define WORM_UI_METADATA_FORMAT_TARGET_CAPACITY 14
#define WORM_UI_COMPOSE_BAND_CAPACITY 4096
#define WORM_UI_COMPOSE_LANE_CAPACITY 32768
#define WORM_UI_COMPOSE_GRID_CAPACITY 16384
#define WORM_UI_COMPOSE_DIFFERENCE_CAPACITY 32768
#define WORM_UI_COMPOSE_HOLD_CAPACITY 32768
#define WORM_UI_COMPOSE_NOTE_CAPACITY 32768
#define WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY 32768
#define WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY 128
#define WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY 16
#define WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY 16
#define WORM_UI_SHORTCUT_EVENT_CAPACITY 64
#define WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY 96
#define WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY 4
#define WORM_UI_TIMING_SPEED_CAPACITY 4096
#define WORM_UI_TIMING_MARKER_CAPACITY 16384
#define WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE 24.0
#define WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE 4096.0

typedef enum WormUiRecoveryAction {
    WORM_UI_RECOVERY_ACTION_INSPECT = 1 << 0,
    WORM_UI_RECOVERY_ACTION_RECOVER = 1 << 1,
    WORM_UI_RECOVERY_ACTION_SAVE_AS = 1 << 2,
    WORM_UI_RECOVERY_ACTION_DISCARD = 1 << 3,
    WORM_UI_RECOVERY_ACTION_QUARANTINE = 1 << 4,
} WormUiRecoveryAction;

typedef struct WormUiRecoveryCandidateView {
    const char* path_label;
    const char* summary;
    const char* disabled_reason;
    size_t action_mask;
    uint64_t scan_generation;
    size_t row_index;
    size_t candidate_index;
} WormUiRecoveryCandidateView;

typedef enum WormUiWorkspaceActionKind {
    WORM_UI_WORKSPACE_ACTION_NONE = 0,
    WORM_UI_WORKSPACE_ACTION_SWITCH_TAB,
    WORM_UI_WORKSPACE_ACTION_QUIT,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE,
} WormUiWorkspaceActionKind;

typedef enum WormUiWorkspaceTab {
    WORM_UI_WORKSPACE_TAB_SETUP = 0,
    WORM_UI_WORKSPACE_TAB_METADATA,
    WORM_UI_WORKSPACE_TAB_TIMING,
    WORM_UI_WORKSPACE_TAB_COMPOSE,
    WORM_UI_WORKSPACE_TAB_STORYBOARD,
    WORM_UI_WORKSPACE_TAB_AUDIO_SLICES,
    WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE,
} WormUiWorkspaceTab;

typedef struct WormUiWorkspaceTabItem {
    const char* label;
    bool enabled;
} WormUiWorkspaceTabItem;

typedef struct WormUiMetadataFormatFieldView {
    bool chart_scoped;
    uint8_t family;
    bool first_group_for_family;
    size_t group_index;
    size_t field_index;
    size_t group_field_count;
    const char* name;
    const char* value;
    size_t name_bytes;
    size_t value_bytes;
    bool has_source_order;
    size_t source_order;
} WormUiMetadataFormatFieldView;

typedef struct WormUiMetadataFormatTargetView {
    bool chart_scoped;
    uint8_t family;
    bool group_exists;
    size_t group_index;
    size_t field_count;
} WormUiMetadataFormatTargetView;

typedef struct WormUiMetadataView {
    bool visible;
    bool song_fields_editable;
    bool chart_fields_editable;
    bool search_tags_editable;
    uint64_t document_revision;
    bool has_chart;
    uint64_t chart_id;
    const char* title;
    const char* artist;
    size_t title_bytes;
    size_t artist_bytes;
    const char* chart_name;
    const char* chart_author;
    size_t chart_name_bytes;
    size_t chart_author_bytes;
    bool has_chart_rating;
    int64_t chart_rating_numerator;
    uint64_t chart_rating_denominator;
    const char* const* search_tags;
    const size_t* search_tag_bytes;
    size_t search_tag_count;
    bool format_fields_truncated;
    const WormUiMetadataFormatFieldView* format_fields;
    size_t format_field_total_count;
    size_t format_field_count;
    const WormUiMetadataFormatTargetView* format_targets;
    size_t format_target_count;
} WormUiMetadataView;

typedef struct WormUiComposeRect {
    double left;
    double top;
    double right;
    double bottom;
} WormUiComposeRect;

typedef struct WormUiComposeBandView {
    WormUiComposeRect bounds;
    bool split_stage;
} WormUiComposeBandView;

typedef struct WormUiComposeLaneView {
    WormUiComposeRect bounds;
    uint64_t lane_id;
    uint32_t display_order;
    uint8_t role;
    uint8_t stage;
    uint8_t glyph;
} WormUiComposeLaneView;

typedef struct WormUiComposeGridLineView {
    double y_pixels;
    uint8_t kind;
    uint8_t emphasis;
} WormUiComposeGridLineView;

typedef struct WormUiComposeDifferenceView {
    WormUiComposeRect bounds;
} WormUiComposeDifferenceView;

typedef struct WormUiComposeHoldView {
    WormUiComposeRect bounds;
    uint64_t note_id;
    uint64_t lane_id;
    bool selected;
} WormUiComposeHoldView;

typedef struct WormUiComposeNoteView {
    WormUiComposeRect bounds;
    uint64_t note_id;
    uint64_t lane_id;
    uint8_t kind;
    bool tail;
    bool selected;
} WormUiComposeNoteView;

typedef struct WormUiComposeCursorView {
    WormUiComposeRect bounds;
    uint64_t lane_id;
} WormUiComposeCursorView;

typedef enum WormUiComposeSelectionMode {
    WORM_UI_COMPOSE_SELECTION_REPLACE = 0,
    WORM_UI_COMPOSE_SELECTION_ADD,
    WORM_UI_COMPOSE_SELECTION_TOGGLE,
} WormUiComposeSelectionMode;

typedef struct WormUiComposeLaneKeyBinding {
    uint32_t scancode;
    uint64_t lane_id;
} WormUiComposeLaneKeyBinding;

// Shortcut keys cross the frontend boundary as USB HID Keyboard/Keypad
// usages. Modifier bits match shortcut_modifier_t's actual modifiers.
typedef struct WormUiShortcutEvent {
    uint16_t hid_usage;
    uint8_t modifiers;
    bool repeat;
} WormUiShortcutEvent;

typedef enum WormUiShortcutModifier {
    WORM_UI_SHORTCUT_MODIFIER_SHIFT = 1 << 0,
    WORM_UI_SHORTCUT_MODIFIER_CONTROL = 1 << 1,
    WORM_UI_SHORTCUT_MODIFIER_ALT = 1 << 2,
    WORM_UI_SHORTCUT_MODIFIER_META = 1 << 3,
} WormUiShortcutModifier;

typedef struct WormUiComposeBranchRowView {
    const char* display_text;
    size_t depth;
    bool active;
    bool selected;
    bool display_text_truncated;
    bool has_source_order;
    size_t source_order;
} WormUiComposeBranchRowView;

// Renderer-neutral field primitives projected into bounded scalar storage.
// Coordinates are relative to the scene viewport; no chart/core types cross
// the private frontend boundary.
typedef struct WormUiComposeView {
    bool visible;
    bool scene_available;
    bool truncated;
    uint64_t chart_id;
    uint64_t document_revision;
    double viewport_width_pixels;
    double viewport_height_pixels;
    double pixels_per_standard_measure;
    const char* status_text;
    const WormUiComposeBandView* bands;
    size_t band_count;
    const WormUiComposeLaneView* lanes;
    size_t lane_count;
    const WormUiComposeGridLineView* grid_lines;
    size_t grid_line_count;
    const WormUiComposeDifferenceView* differences;
    size_t difference_count;
    const WormUiComposeHoldView* hold_bodies;
    size_t hold_body_count;
    const WormUiComposeNoteView* note_endpoints;
    size_t note_endpoint_count;
    const WormUiComposeCursorView* cursor;
    bool area_selection_preview_visible;
    WormUiComposeRect area_selection_preview_bounds;
    WormUiComposeSelectionMode area_selection_preview_mode;
    bool space_range_preview_visible;
    WormUiComposeRect space_range_preview_bounds;
    WormUiComposeSelectionMode space_range_preview_mode;
    const WormUiComposeLaneKeyBinding* lane_key_bindings;
    size_t lane_key_binding_count;
    bool branch_outline_available;
    bool branch_rows_truncated;
    bool branch_display_text_truncated;
    bool branch_diagnostics_truncated;
    const WormUiComposeBranchRowView* branch_rows;
    size_t branch_row_count;
    const char* branch_diagnostics[WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY];
    size_t branch_diagnostic_count;
} WormUiComposeView;

typedef struct WormUiTimingSpeedView {
    uint64_t object_id;
    double marker_y_pixels;
    double affected_begin_y_pixels;
    double affected_end_y_pixels;
    double multiplier;
    double transition_duration;
    bool transition_is_seconds;
    bool transition_clipped;
} WormUiTimingSpeedView;

typedef enum WormUiTimingLaneKind {
    WORM_UI_TIMING_LANE_BPM = 0,
    WORM_UI_TIMING_LANE_SCROLL,
    WORM_UI_TIMING_LANE_SPEED,
    WORM_UI_TIMING_LANE_STOP,
    WORM_UI_TIMING_LANE_DELAY,
    WORM_UI_TIMING_LANE_WARP,
} WormUiTimingLaneKind;

typedef struct WormUiTimingMarkerView {
    uint64_t object_id;
    double marker_y_pixels;
    WormUiTimingLaneKind lane;
} WormUiTimingMarkerView;

// Renderer-neutral scalar projection of the timing pane. SPEEDS spans are
// evaluated by the timing core before crossing this private C boundary.
typedef struct WormUiTimingView {
    bool visible;
    bool scene_available;
    bool truncated;
    double viewport_width_pixels;
    double viewport_height_pixels;
    uint64_t chart_id;
    uint64_t document_revision;
    uint64_t total_rows;
    const char* status_text;
    size_t measure_count;
    size_t bpm_count;
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
    bool stage_active;
    bool stage_valid;
    WormUiTimingLaneKind stage_lane;
    double stage_marker_y_pixels;
    double stage_value;
    double stage_secondary_value;
    bool stage_transition_is_seconds;
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
    WORM_UI_TIMING_STAGE_CONFIRM,
    WORM_UI_TIMING_STAGE_CANCEL,
} WormUiTimingStageAction;

// Private scalar view consumed by the selected frontend adapter. The owning
// application keeps every pointed-to string alive for the duration of compose.
typedef struct WormUiWorkspaceView {
    const char* project_title;
    const char* artist;
    const char* selected_difficulty;
    const char* status_text;
    const char* git_hash;
    WormUiWorkspaceTab active_tab;
    WormUiWorkspaceTabItem tabs[WORM_UI_WORKSPACE_TAB_COUNT];
    const char* tab_heading;
    const char* summary_lines[WORM_UI_WORKSPACE_SUMMARY_CAPACITY];
    size_t summary_line_count;
    bool history_available;
    bool dirty;
    size_t undo_depth;
    size_t redo_depth;
    bool recovery_panel_visible;
    const char* recovery_heading;
    const char* recovery_notice;
    bool recovery_rows_truncated;
    size_t recovery_candidate_count;
    WormUiRecoveryCandidateView recovery_candidates[WORM_UI_RECOVERY_ROW_CAPACITY];
    size_t recovery_diagnostic_count;
    const char* recovery_diagnostics[WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY];
    WormUiMetadataView metadata;
    WormUiTimingView timing;
    WormUiComposeView compose;
} WormUiWorkspaceView;

typedef enum WormUiComposeActionKind {
    WORM_UI_COMPOSE_ACTION_NONE = 0,
    WORM_UI_COMPOSE_ACTION_ZOOM_IN,
    WORM_UI_COMPOSE_ACTION_ZOOM_OUT,
    WORM_UI_COMPOSE_ACTION_SCROLL_UP,
    WORM_UI_COMPOSE_ACTION_SCROLL_DOWN,
    WORM_UI_COMPOSE_ACTION_RESET_VIEW,
} WormUiComposeActionKind;

typedef enum WormUiComposeInputKind {
    WORM_UI_COMPOSE_INPUT_NONE = 0,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD,
    WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
    WORM_UI_COMPOSE_INPUT_DELETE_NOTES,
    WORM_UI_COMPOSE_INPUT_UNDO,
    WORM_UI_COMPOSE_INPUT_REDO,
    WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
    WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
    WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION,
    WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
} WormUiComposeInputKind;

typedef struct WormUiComposeInputAction {
    WormUiComposeInputKind kind;
    uint64_t lane_id;
    bool successive;
    WormUiComposeSelectionMode selection_mode;
    double x_pixels;
    double y_pixels;
} WormUiComposeInputAction;

typedef enum WormUiComposeViewportInputKind {
    WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL = 0,
    WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
} WormUiComposeViewportInputKind;

typedef struct WormUiComposeViewportInput {
    WormUiComposeViewportInputKind kind;
    bool zoom_modifier;
    double delta_x;
    double delta_y;
    double cursor_x_pixels;
    double cursor_y_pixels;
} WormUiComposeViewportInput;

typedef enum WormUiMetadataEditKind {
    WORM_UI_METADATA_EDIT_NONE = 0,
    WORM_UI_METADATA_EDIT_SONG,
    WORM_UI_METADATA_EDIT_CHART,
    WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD,
} WormUiMetadataEditKind;

typedef struct WormUiWorkspaceAction {
    WormUiWorkspaceActionKind kind;
    WormUiWorkspaceTab tab;
    uint64_t recovery_scan_generation;
    size_t recovery_row_index;
    size_t recovery_candidate_index;
    char recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
    // Metadata pointers refer to frontend-owned staging buffers and are valid
    // only until the next compose call; the app dispatches them immediately.
    WormUiMetadataEditKind metadata_edit;
    uint64_t metadata_document_revision;
    uint64_t metadata_chart_id;
    bool metadata_has_chart_rating;
    const char* metadata_chart_rating_numerator_text;
    const char* metadata_chart_rating_denominator_text;
    const char* metadata_title;
    const char* metadata_artist;
    const char* metadata_chart_name;
    const char* metadata_chart_author;
    const char* const* metadata_search_tags;
    size_t metadata_search_tag_count;
    bool metadata_format_chart_scoped;
    uint8_t metadata_format_family;
    size_t metadata_format_index;
    const char* metadata_format_name;
    const char* metadata_format_value;
    WormUiComposeActionKind compose_action;
    uint64_t compose_input_chart_id;
    uint64_t compose_input_document_revision;
    double compose_input_viewport_width_pixels;
    WormUiComposeInputAction compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY];
    size_t compose_input_count;
    WormUiComposeViewportInput compose_viewport_inputs[WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY];
    size_t compose_viewport_input_count;
    WormUiTimingStageAction timing_stage_action;
    uint64_t timing_chart_id;
    uint64_t timing_document_revision;
    WormUiTimingLaneKind timing_lane;
    double timing_target_y_fraction;
    double timing_value;
    double timing_secondary_value;
    bool timing_transition_is_seconds;
    char timing_primary_numerator_text[64];
    char timing_primary_denominator_text[64];
    char timing_secondary_numerator_text[64];
    char timing_secondary_denominator_text[64];
    WormUiShortcutEvent shortcut_events[WORM_UI_SHORTCUT_EVENT_CAPACITY];
    size_t shortcut_event_count;
    bool shortcut_overflowed;
    bool shortcut_window_focused;
    bool shortcut_text_entry_active;
    bool shortcut_ime_composition_active;
    bool shortcut_compose_lane_focused;
    uint64_t shortcut_focused_lane_id;
} WormUiWorkspaceAction;

// Private application/frontend boundary. Implementations own all widget
// toolkit state and translate semantic views into typed actions.
bool worm_ui_frontend_init(SDL_Window* window);
void worm_ui_frontend_shutdown(void);
void worm_ui_frontend_begin_input(void);
bool worm_ui_frontend_handle_event(const SDL_Event* event);
void worm_ui_frontend_end_input(void);
WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view);
void worm_ui_frontend_render(int window_width, int window_height);
