#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <stdbool.h>
#include <stddef.h>

#define WORM_UI_WORKSPACE_TAB_COUNT 7
#define WORM_UI_WORKSPACE_SUMMARY_CAPACITY 16

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
} WormUiWorkspaceView;

typedef enum WormUiWorkspaceActionKind {
    WORM_UI_WORKSPACE_ACTION_NONE = 0,
    WORM_UI_WORKSPACE_ACTION_SWITCH_TAB,
    WORM_UI_WORKSPACE_ACTION_QUIT,
} WormUiWorkspaceActionKind;

typedef struct WormUiWorkspaceAction {
    WormUiWorkspaceActionKind kind;
    WormUiWorkspaceTab tab;
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
