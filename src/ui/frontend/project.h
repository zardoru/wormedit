#pragma once
#include "common.h"
#include "catalog.h"

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
    WORM_UI_WORKSPACE_ACTION_NEW_PROJECT,
    WORM_UI_WORKSPACE_ACTION_OPEN_FILE,
    WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING,
    WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT,
    WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT,
    WORM_UI_WORKSPACE_ACTION_SWITCH_TAB,
    WORM_UI_WORKSPACE_ACTION_QUIT,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_TOGGLE_METRONOME,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH,
    WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET,
    WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE,
    WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE,
    WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET,
    WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW,
    WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN,
    WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER,
    WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE,
    WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT,
    WORM_UI_WORKSPACE_ACTION_COMPARE_SELECT_CHART,
    WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL,
    WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP,
    /* The native picker returned a chart file for the chart import dialog (project_path). */
    WORM_UI_WORKSPACE_ACTION_IMPORT_CHART_FILE,
} WormUiWorkspaceActionKind;

typedef enum WormUiChartImportActionKind {
    WORM_UI_CHART_IMPORT_NONE = 0,
    WORM_UI_CHART_IMPORT_BEGIN,    /* Open the dialog. */
    WORM_UI_CHART_IMPORT_LOAD,     /* Read the chart file named by project_path. */
    WORM_UI_CHART_IMPORT_TOGGLE,   /* Toggle one difficulty (chart_id). */
    WORM_UI_CHART_IMPORT_CONFIRM,  /* Import the ticked difficulties (document_revision stamp). */
    WORM_UI_CHART_IMPORT_CANCEL,
} WormUiChartImportActionKind;

typedef struct WormUiChartImportAction {
    WormUiChartImportActionKind kind;
    uint64_t chart_id;
    uint64_t document_revision;
} WormUiChartImportAction;

typedef struct WormUiChartImportRowView {
    uint64_t chart_id;
    const char* label;   /* "Hard · 7 measures · 412 notes" */
    bool selected;
} WormUiChartImportRowView;

#define WORM_UI_CHART_IMPORT_NOTE_CAPACITY 24

typedef struct WormUiChartImportView {
    bool visible;
    const char* path;     /* Text the path field starts from. */
    const char* error;    /* Empty when the file read fine. */
    const WormUiChartImportRowView* rows;
    size_t row_count;
    const char* summary;  /* "1 reused, 2 added, 1 missing" or empty before a file is loaded. */
    /* Missing files and timing differences, one line each, capped. */
    const char* notes[WORM_UI_CHART_IMPORT_NOTE_CAPACITY];
    size_t note_count;
    bool can_import;
    uint64_t document_revision;
} WormUiChartImportView;

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
    WormUiWorkspaceTab id;
} WormUiWorkspaceTabItem;

typedef enum WormUiStepmaniaFileProfile {
    WORM_UI_STEPMANIA_FILE_SM = 0,
    WORM_UI_STEPMANIA_FILE_SSC,
} WormUiStepmaniaFileProfile;

typedef enum WormUiStepmaniaTimingSyntax {
    WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS = 0,
    WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_BPM,
    WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP,
} WormUiStepmaniaTimingSyntax;

typedef enum WormUiStepmaniaImplementation {
    WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY = 0,
    WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1,
    WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1,
} WormUiStepmaniaImplementation;

#define WORM_UI_PROJECT_CHART_CAPACITY 256

typedef enum WormUiExistingFilesChoice {
    WORM_UI_EXISTING_FILES_ASK = 0,
    WORM_UI_EXISTING_FILES_REPLACE,
    WORM_UI_EXISTING_FILES_KEEP,
    WORM_UI_EXISTING_FILES_SAVED = 255,
} WormUiExistingFilesChoice;

typedef struct WormUiProjectChartRow {
    uint64_t chart_id;
    const char* name;
    const char* author;
    const char* rating;
    const char* mode;
    uint32_t key_count;
    const char* output_groups;
    uint64_t note_count;
    const char* length;
    const char* diagnostic;
} WormUiProjectChartRow;

typedef enum WormUiProjectChartActionKind {
    WORM_UI_PROJECT_CHART_NONE = 0,
    WORM_UI_PROJECT_CHART_NEW,
    WORM_UI_PROJECT_CHART_DUPLICATE,
    WORM_UI_PROJECT_CHART_DELETE,
    WORM_UI_PROJECT_CHART_UP,
    WORM_UI_PROJECT_CHART_DOWN,
    WORM_UI_PROJECT_CHART_OPEN,
    WORM_UI_PROJECT_CHART_BATCH_METADATA,
    WORM_UI_PROJECT_CHART_REORDER,
} WormUiProjectChartActionKind;

typedef struct WormUiProjectChartAction {
    WormUiProjectChartActionKind kind;
    uint64_t document_revision;
    uint64_t session_generation;
    uint64_t chart_id;
    size_t selected_count;
    uint64_t selected_chart_ids[WORM_UI_PROJECT_CHART_CAPACITY];
    bool edit_author;
    const char* author;
    bool edit_rating;
    bool unset_rating;
    const char* rating_numerator;
    const char* rating_denominator;
    uint64_t target_chart_id; // Reorder relative to this stable row ID.
    bool insert_after;
} WormUiProjectChartAction;

typedef struct WormUiProjectOutputGroupView {
    size_t document_index;
    const char* name;
    uint8_t family;
    const char* path;
    const uint64_t* chart_ids;
    size_t chart_count;
    const char* preview_status;
    WormUiExistingFilesChoice existing_files;
    bool has_stepmania_policy;
    WormUiStepmaniaFileProfile stepmania_file_profile;
    WormUiStepmaniaTimingSyntax stepmania_timing_syntax;
    WormUiStepmaniaImplementation stepmania_implementation;
} WormUiProjectOutputGroupView;

typedef enum WormUiProjectOutputGroupActionKind {
    WORM_UI_PROJECT_OUTPUT_GROUP_NONE = 0,
    WORM_UI_PROJECT_OUTPUT_GROUP_CREATE,
    WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE,
    WORM_UI_PROJECT_OUTPUT_GROUP_DELETE,
    WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW,
} WormUiProjectOutputGroupActionKind;

typedef struct WormUiProjectOutputGroupAction {
    WormUiProjectOutputGroupActionKind kind;
    uint64_t project_id;
    uint64_t document_revision;
    uint64_t session_generation;
    size_t group_index;
    const char* name;
    const char* path;
    uint8_t family;
    WormUiExistingFilesChoice existing_files;
    bool has_stepmania_policy;
    WormUiStepmaniaFileProfile stepmania_file_profile;
    WormUiStepmaniaTimingSyntax stepmania_timing_syntax;
    WormUiStepmaniaImplementation stepmania_implementation;
    size_t chart_count;
    uint64_t chart_ids[WORM_UI_PROJECT_CHART_CAPACITY];
} WormUiProjectOutputGroupAction;

typedef struct WormUiSetupView {
    WormUiKindCatalogView output_families;
    bool visible;
    uint64_t song_id;
    uint64_t document_revision;
    uint64_t session_generation;
    const WormUiProjectChartRow* charts;
    size_t chart_row_count;
    bool charts_truncated;
    const WormUiProjectOutputGroupView* output_groups;
    size_t output_group_count;
    bool output_groups_truncated;
    uint64_t selected_chart_id;
    size_t chart_count;
    size_t audio_asset_count;
    size_t visual_asset_count;
} WormUiSetupView;
