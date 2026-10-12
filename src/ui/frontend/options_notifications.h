#pragma once
#include "timing_preview.h"

typedef enum WormUiOptionsCategory {
    WORM_UI_OPTIONS_GENERAL = 0, WORM_UI_OPTIONS_EDITOR, WORM_UI_OPTIONS_AUDIO,
    WORM_UI_OPTIONS_DISPLAY, WORM_UI_OPTIONS_INPUT, WORM_UI_OPTIONS_PLAYERS,
    WORM_UI_OPTIONS_CATEGORY_COUNT
} WormUiOptionsCategory;
typedef enum WormUiOptionsField {
    WORM_UI_OPTIONS_FIELD_AUTOSAVE, WORM_UI_OPTIONS_FIELD_RECENT_FILES,
    WORM_UI_OPTIONS_FIELD_SNAP, WORM_UI_OPTIONS_FIELD_RATE,
    WORM_UI_OPTIONS_FIELD_THEME, WORM_UI_OPTIONS_FIELD_SCALE,
    WORM_UI_OPTIONS_FIELD_LAYOUT, WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH,
    WORM_UI_OPTIONS_FIELD_PLAYERS, WORM_UI_OPTIONS_FIELD_PAGE,
    WORM_UI_OPTIONS_FIELD_CONFIRM_CLOSE, WORM_UI_OPTIONS_FIELD_MINIMAP_CLICK,
    WORM_UI_OPTIONS_FIELD_KEEP_PITCH, WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY, WORM_UI_OPTIONS_FIELD_TIMING_OVERLAY,
    WORM_UI_OPTIONS_FIELD_PLAYER_NAME, WORM_UI_OPTIONS_FIELD_PLAYER_PATH,
    WORM_UI_OPTIONS_FIELD_PLAYER_START, WORM_UI_OPTIONS_FIELD_PLAYER_HERE,
    WORM_UI_OPTIONS_FIELD_PLAYER_STOP, WORM_UI_OPTIONS_FIELD_PLAYER_SELECTED,
    WORM_UI_OPTIONS_FIELD_PLAYER_ADD, WORM_UI_OPTIONS_FIELD_PLAYER_REMOVE,
    WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION, WORM_UI_OPTIONS_FIELD_ANCHOR,
    WORM_UI_OPTIONS_FIELD_NOTE_PALETTE, WORM_UI_OPTIONS_FIELD_LANE_LEGEND,
    WORM_UI_OPTIONS_FIELD_AREA_ALPHA, WORM_UI_OPTIONS_FIELD_AUDITION_ON_CLICK,
    WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY, WORM_UI_OPTIONS_FIELD_FIELD_SCHEME,
    WORM_UI_OPTIONS_FIELD_SNAP_COLORED_GRID, WORM_UI_OPTIONS_FIELD_SNAP_GRID_OPACITY,
    WORM_UI_OPTIONS_FIELD_INVERT_SCROLL_WHEEL, WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY,
    WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION, WORM_UI_OPTIONS_FIELD_P2_SCRATCH_POSITION,
    WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME
} WormUiOptionsField;
typedef struct WormUiOptionsDiagnostic {
    WormUiOptionsField field;
    const char* message;
} WormUiOptionsDiagnostic;
// Input & shortcuts rows: one per registry command, at most the registry's command cap.
#define WORM_UI_SHORTCUT_ROW_CAPACITY 512
typedef struct WormUiShortcutRowView {
    const char* label;
    const char* chord;
    bool conflict;
} WormUiShortcutRowView;
typedef struct WormUiOptionsView {
    bool visible;
    uint64_t revision;
    WormUiOptionsCategory category;
    bool changed;
    bool can_apply;
    uint32_t autosave_minutes, recent_files_count, default_snap, ui_scale_percent;
    uint32_t scroll_directions[6];
    uint32_t playhead_anchor_percent, area_band_alpha;
    uint32_t move_collision_policy;
    bool note_kind_palette_visible, lane_key_legend_visible;
    bool audition_on_click;
    bool invert_scroll_wheel;
    bool confirm_unsaved_close, keep_pitch, frame_timing_overlay;
    bool snap_colored_grid;
    uint32_t snap_grid_opacity_percent;
    uint32_t field_waveform_opacity_percent;
    bool minimap_click_moves_paused_playhead;
    uint32_t keysound_budget_mib;
    uint64_t rate_numerator, rate_denominator;
    uint32_t p1_scratch_position, p2_scratch_position;
    const char* field_scheme; // Default skin for layouts without an override.
    const char* current_field_layout_key;
    const char* current_field_layout_name;
    const char* current_layout_field_scheme; // Empty inherits the default.
    const WormUiComposeLaneView* current_field_layout_lanes;
    size_t current_field_layout_lane_count;
    const char* theme;
    const char* dock_layout;
    const char* shortcut_profile_path;
    const struct WormUiExternalPlayerDefinitionView* players;
    size_t player_count, selected_player;
    const WormUiOptionsDiagnostic* diagnostics;
    size_t diagnostic_count;
    const char* status;
    // Input & shortcuts page only; empty when that page is not the active category.
    const WormUiShortcutRowView* shortcut_rows;
    size_t shortcut_row_count;
    bool shortcut_capturing;
    size_t shortcut_capture_row;
} WormUiOptionsView;
typedef enum WormUiOptionsActionKind {
    WORM_UI_OPTIONS_NONE = 0, WORM_UI_OPTIONS_OPEN, WORM_UI_OPTIONS_CLOSE,
    WORM_UI_OPTIONS_SELECT_CATEGORY, WORM_UI_OPTIONS_APPLY,
    WORM_UI_OPTIONS_REVERT, WORM_UI_OPTIONS_DEFAULTS, WORM_UI_OPTIONS_EDIT_FIELD,
    WORM_UI_OPTIONS_SHORTCUT_CAPTURE, WORM_UI_OPTIONS_SHORTCUT_CANCEL
} WormUiOptionsActionKind;
// EDIT_FIELD payload is consumed immediately; strings are bounded and owned.
#define WORM_UI_OPTIONS_TEXT_CAPACITY 4097
typedef struct WormUiOptionsAction {
    WormUiOptionsActionKind kind;
    uint64_t revision;
    WormUiOptionsCategory category;
    WormUiOptionsField field;
    uint64_t integer, denominator;
    bool boolean;
    uint32_t player_index;
    uint32_t family_index;
    // SHORTCUT_CAPTURE: index into the view's shortcut rows.
    uint32_t shortcut_row;
    char text[WORM_UI_OPTIONS_TEXT_CAPACITY];
} WormUiOptionsAction;

#define WORM_UI_NOTIFICATION_TOAST_CAPACITY 3

typedef enum WormUiNotificationSeverity {
    WORM_UI_NOTIFICATION_HINT = 0,
    WORM_UI_NOTIFICATION_INFO,
    WORM_UI_NOTIFICATION_SUCCESS,
    WORM_UI_NOTIFICATION_WARNING,
    WORM_UI_NOTIFICATION_ERROR
} WormUiNotificationSeverity;

typedef enum WormUiNotificationProducerSource {
    WORM_UI_NOTIFICATION_PRODUCER_INPUT,
    WORM_UI_NOTIFICATION_PRODUCER_SYSTEM
} WormUiNotificationProducerSource;

typedef void (*WormUiNotificationPostCallback)(void* context, WormUiNotificationSeverity severity,
    WormUiNotificationProducerSource source, const char* message);

typedef struct WormUiNotificationToastView {
    uint64_t id;
    WormUiNotificationSeverity severity;
    const char* message;
    uint32_t repeat_count;
} WormUiNotificationToastView;

#define WORM_UI_NOTIFICATION_HISTORY_CAPACITY 256

typedef enum WormUiNotificationTargetAvailability {
    WORM_UI_NOTIFICATION_NO_TARGET = 0,
    WORM_UI_NOTIFICATION_TARGET_POSITION,
    WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL,
    WORM_UI_NOTIFICATION_TARGET_MISSING
} WormUiNotificationTargetAvailability;

typedef struct WormUiNotificationHistoryRow {
    uint64_t id;
    WormUiNotificationSeverity severity;
    uint32_t source;
    const char* message;
    const char* detail;
    uint32_t repeat_count;
    uint64_t age_seconds;
    bool read;
    WormUiNotificationTargetAvailability target;
} WormUiNotificationHistoryRow;

typedef struct WormUiNotificationHistoryView {
    bool visible;
    size_t row_count;
    const WormUiNotificationHistoryRow* rows;
} WormUiNotificationHistoryView;

typedef struct WormUiNotificationView {
    bool edit_gesture_active;
    bool history_available;
    WormUiNotificationHistoryView history;
    const char* current_text;
    WormUiNotificationSeverity current_severity;
    size_t unread_warnings;
    size_t unread_errors;
    size_t toast_count;
    WormUiNotificationToastView toasts[WORM_UI_NOTIFICATION_TOAST_CAPACITY];
} WormUiNotificationView;

typedef enum WormUiNotificationActionKind {
    WORM_UI_NOTIFICATION_ACTION_NONE = 0,
    WORM_UI_NOTIFICATION_ACTION_DISMISS,
    WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY,
    WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY,
    WORM_UI_NOTIFICATION_ACTION_COPY_DETAILS,
    WORM_UI_NOTIFICATION_ACTION_GO_TO,
    WORM_UI_NOTIFICATION_ACTION_DISMISS_ALL
} WormUiNotificationActionKind;

typedef struct WormUiNotificationAction {
    WormUiNotificationActionKind kind;
    uint64_t id;
} WormUiNotificationAction;
