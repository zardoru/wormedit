#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION

#include "ui/frontend.h"

#include "wormedit/nuklear_config.h"

#include "nuklear_sdl_gl3_sdl3.h"

#include <stddef.h>

#define WORMEDIT_MAX_VERTEX_MEMORY (512 * 1024)
#define WORMEDIT_MAX_ELEMENT_MEMORY (128 * 1024)

static SDL_Window* g_window;
static struct nk_context* g_context;

bool worm_ui_frontend_init(SDL_Window* window) {
    if (!window || g_context) {
        return false;
    }

    struct nk_context* context = nk_sdl_init(window);
    if (!context) {
        return false;
    }
    struct nk_font_atlas* atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);
    (void)atlas;
    nk_sdl_font_stash_end();

    if (!SDL_StartTextInput(window)) {
        nk_sdl_shutdown();
        return false;
    }
    g_window = window;
    g_context = context;
    return true;
}

void worm_ui_frontend_shutdown(void) {
    if (!g_context) {
        return;
    }
    if (g_window) {
        SDL_StopTextInput(g_window);
    }
    nk_sdl_shutdown();
    g_window = NULL;
    g_context = NULL;
}

void worm_ui_frontend_begin_input(void) {
    if (g_context) {
        nk_input_begin(g_context);
    }
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    SDL_Event mutable_event = *event;
    return nk_sdl_handle_event(&mutable_event) != 0;
}

void worm_ui_frontend_end_input(void) {
    if (g_context) {
        nk_input_end(g_context);
        nk_sdl_handle_grab();
    }
}

WormUiScaffoldAction worm_ui_frontend_compose(const WormUiScaffoldView* view) {
    if (!g_context || !view || !view->status_text || !view->git_hash) {
        return WORM_UI_SCAFFOLD_ACTION_NONE;
    }

    WormUiScaffoldAction action = WORM_UI_SCAFFOLD_ACTION_NONE;
    if (nk_begin(g_context, "wormedit", nk_rect(20, 20, 360, 190),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE |
                     NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(g_context, 24, 1);
        nk_label(g_context, "SDL3 + Nuklear scaffold", NK_TEXT_LEFT);
        nk_label(g_context, view->status_text, NK_TEXT_LEFT);
        nk_labelf(g_context, NK_TEXT_LEFT, "Git hash: %s", view->git_hash);

        nk_layout_row_dynamic(g_context, 30, 2);
        if (nk_button_label(g_context, "Probe audio")) {
            action = WORM_UI_SCAFFOLD_ACTION_PROBE_AUDIO;
        }
        if (nk_button_label(g_context, "Quit")) {
            action = WORM_UI_SCAFFOLD_ACTION_QUIT;
        }
    }
    nk_end(g_context);
    return action;
}

void worm_ui_frontend_render(int window_width, int window_height) {
    (void)window_width;
    (void)window_height;
    if (g_context) {
        nk_sdl_render(NK_ANTI_ALIASING_ON, WORMEDIT_MAX_VERTEX_MEMORY, WORMEDIT_MAX_ELEMENT_MEMORY);
    }
}
