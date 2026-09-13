#include "wormedit/window.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <stdio.h>

bool worm_window_create(WormWindow *window, const WormWindowDesc *desc)
{
    if (!window || !desc || !desc->title || desc->width <= 0 || desc->height <= 0) {
        return false;
    }

    *window = WormWindow{};

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

    window->handle = SDL_CreateWindow(
        desc->title,
        desc->width,
        desc->height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window->handle) {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    window->gl = SDL_GL_CreateContext(window->handle);
    if (!window->gl) {
        fprintf(stderr, "SDL GL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window->handle);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        *window = WormWindow{};
        return false;
    }

    SDL_GL_MakeCurrent(window->handle, window->gl);
    SDL_GL_SetSwapInterval(1);

    window->width = desc->width;
    window->height = desc->height;
    return true;
}

void worm_window_destroy(WormWindow *window)
{
    if (!window) {
        return;
    }

    if (window->gl) {
        SDL_GL_DestroyContext(window->gl);
    }

    if (window->handle) {
        SDL_DestroyWindow(window->handle);
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    *window = WormWindow{};
}

void worm_window_swap(WormWindow *window)
{
    if (window && window->handle) {
        SDL_GL_SwapWindow(window->handle);
    }
}

void worm_window_get_size(WormWindow *window, int *width, int *height)
{
    if (!window || !window->handle) {
        if (width) {
            *width = 0;
        }
        if (height) {
            *height = 0;
        }
        return;
    }

    SDL_GetWindowSize(window->handle, &window->width, &window->height);
    if (width) {
        *width = window->width;
    }
    if (height) {
        *height = window->height;
    }
}
