#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION

#include "wormedit/vendorimpl.h"

#include "nuklear_sdl_gl3_sdl3.h"

#define WORMEDIT_MAX_VERTEX_MEMORY (512 * 1024)
#define WORMEDIT_MAX_ELEMENT_MEMORY (128 * 1024)

static SDL_Window *g_window;
static struct nk_context *g_ctx;

struct nk_context *worm_vendor_nk_init(SDL_Window *window)
{
    struct nk_context *ctx = nk_sdl_init(window);
    struct nk_font_atlas *atlas = NULL;

    nk_sdl_font_stash_begin(&atlas);
    (void)atlas;
    nk_sdl_font_stash_end();

    SDL_StartTextInput(window);
    g_window = window;
    g_ctx = ctx;
    return ctx;
}

void worm_vendor_nk_shutdown(void)
{
    if (g_window) {
        SDL_StopTextInput(g_window);
    }

    nk_sdl_shutdown();
    g_window = NULL;
    g_ctx = NULL;
}

void worm_vendor_nk_begin_input(void)
{
    nk_input_begin(g_ctx);
}

bool worm_vendor_nk_handle_event(const SDL_Event *event)
{
    if (!event) {
        return false;
    }

    SDL_Event mutable_event = *event;
    return nk_sdl_handle_event(&mutable_event) != 0;
}

void worm_vendor_nk_end_input(void)
{
    nk_input_end(g_ctx);
    nk_sdl_handle_grab();
}

void worm_vendor_nk_render(int window_width, int window_height)
{
    (void)window_width;
    (void)window_height;

    nk_sdl_render(NK_ANTI_ALIASING_ON, WORMEDIT_MAX_VERTEX_MEMORY, WORMEDIT_MAX_ELEMENT_MEMORY);
}
