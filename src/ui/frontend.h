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
} WormUiWorkspaceView;

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
    int64_t metadata_chart_rating_numerator;
    uint64_t metadata_chart_rating_denominator;
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
