#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <stdbool.h>

typedef struct WormUiScaffoldView {
    const char* status_text;
    const char* git_hash;
} WormUiScaffoldView;

typedef enum WormUiScaffoldAction {
    WORM_UI_SCAFFOLD_ACTION_NONE = 0,
    WORM_UI_SCAFFOLD_ACTION_PROBE_AUDIO,
    WORM_UI_SCAFFOLD_ACTION_QUIT,
} WormUiScaffoldAction;

// Private application/frontend boundary. Implementations own all widget
// toolkit state and translate semantic views into typed actions.
bool worm_ui_frontend_init(SDL_Window* window);
void worm_ui_frontend_shutdown(void);
void worm_ui_frontend_begin_input(void);
bool worm_ui_frontend_handle_event(const SDL_Event* event);
void worm_ui_frontend_end_input(void);
WormUiScaffoldAction worm_ui_frontend_compose(const WormUiScaffoldView* view);
void worm_ui_frontend_render(int window_width, int window_height);
