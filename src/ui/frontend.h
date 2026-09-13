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
} WormUiWorkspaceView;

typedef struct WormUiWorkspaceAction {
    WormUiWorkspaceActionKind kind;
    WormUiWorkspaceTab tab;
    uint64_t recovery_scan_generation;
    size_t recovery_row_index;
    size_t recovery_candidate_index;
    char recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
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
