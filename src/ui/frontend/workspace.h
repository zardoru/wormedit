#pragma once
#include "options_notifications.h"

typedef enum WormUiCloseDecision {
    WORM_UI_CLOSE_NONE = 0, WORM_UI_CLOSE_CANCEL, WORM_UI_CLOSE_DISCARD, WORM_UI_CLOSE_SAVE
} WormUiCloseDecision;
typedef struct WormUiCloseConfirmationView {
    bool visible, application, has_native_path;
    uint64_t project_id, revision;
    const char* status_text;
} WormUiCloseConfirmationView;
typedef struct WormUiCompareView {
    bool visible;
    bool has_two_difficulties;
    uint64_t first_chart_id;
    uint64_t second_chart_id;
    const char* first_difficulty;
    const char* second_difficulty;
    const WormUiComposeView* first_pane;
    const WormUiComposeView* second_pane;
    const uint64_t* chart_ids;
    const char* const* chart_names;
    size_t chart_count;
    double scroll_pixels;
    size_t diagnostic_count;
    const char* const* diagnostic_labels;
    const uint64_t* diagnostic_rows;
    const uint64_t* diagnostic_measures;
    const uint8_t* diagnostic_has_rows;
    uint64_t diagnostic_chart_id;
} WormUiCompareView;

enum { WORM_UI_MENU_COMMAND_CAPACITY = 64 };
typedef struct WormUiMenuCommandView {
    char id[96];
    char label[128];
    char shortcut[96];
    bool enabled;
    bool checked;
} WormUiMenuCommandView;

typedef struct WormUiRecentFileView {
    const char* label;
    const char* path;
} WormUiRecentFileView;

enum { WORM_UI_COMMAND_MENU_CAPACITY = 512 };
typedef struct WormUiCommandMenuRow {
    const char* id;
    const char* label;
    bool enabled;
} WormUiCommandMenuRow;

typedef struct WormUiWorkspaceView {
    size_t recent_file_count;
    WormUiRecentFileView recent_files[100];
    size_t menu_command_count;
    WormUiMenuCommandView menu_commands[WORM_UI_MENU_COMMAND_CAPACITY];
    WormUiNotificationView notifications;
    WormUiOptionsView options;
    WormUiCloseConfirmationView close_confirmation;
    WormUiChartImportView chart_import;
    WormUiTapTimingView tap_timing;
    bool timing_settings_visible;
    uint8_t metronome_volume;
    uint8_t metronome_count_in;
    bool project_loaded;
    bool project_has_native_path;
    uint64_t project_id;
    uint64_t selected_chart_id;
    uint64_t document_revision;
    // A dropped MP3/OGG awaiting confirmation as a new project's audio.
    const char* pending_new_project_audio_path;
    // A BMS-family source whose text encoding must be selected before opening.
    const char* pending_bms_encoding_path;
    const char* project_title;
    const char* artist;
    const char* selected_difficulty;
    const char* status_text;
    const char* git_hash;
    WormUiWorkspaceTab active_tab;
    WormUiWorkspaceTabItem tabs[WORM_UI_WORKSPACE_TAB_COUNT];
    const char* tab_heading;
    bool transport_available;
    bool transport_playing;
    bool transport_paused;
    bool transport_metronome_enabled;
    bool transport_metronome_available;
    uint8_t transport_rate_index;
    bool minimap_click_moves_paused_playhead;
    bool transport_rate_available;
    bool transport_keep_pitch;
    bool transport_keep_pitch_available;
    uint64_t transport_frame;
    uint64_t transport_end_frame;
    uint32_t transport_sample_rate;
    const char* summary_lines[WORM_UI_WORKSPACE_SUMMARY_CAPACITY];
    size_t summary_line_count;
    bool history_available;
    bool dirty;
    size_t undo_depth;
    size_t redo_depth;
    bool recovery_panel_visible;
    // Computed from all controller rows before the UI row limit is applied.
    bool recovery_has_actionable_candidate;
    const char* recovery_heading;
    const char* recovery_notice;
    bool recovery_rows_truncated;
    size_t recovery_candidate_count;
    WormUiRecoveryCandidateView recovery_candidates[WORM_UI_RECOVERY_ROW_CAPACITY];
    size_t recovery_diagnostic_count;
    const char* recovery_diagnostics[WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY];
    WormUiSetupView setup;
    WormUiMetadataView metadata;
    WormUiTimingView timing;
    WormUiComposeView compose;
    WormUiAudioSlicesView audio_slices;
    WormUiCompareView compare;
    WormUiPlayerPreviewView player_preview;
    struct WormUiExternalPlayerView external_player;
    size_t command_menu_count;
    WormUiCommandMenuRow command_menu_rows[WORM_UI_COMMAND_MENU_CAPACITY];
} WormUiWorkspaceView;
