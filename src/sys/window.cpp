#include "wormedit/window.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
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

    window->handle = SDL_CreateWindow(
        desc->title,
        desc->width,
        desc->height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window->handle) {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    // Request SDL's renderer backed by the SDL_GPU API. Using an explicit
    // driver name prevents a silent fallback to another rendering API.
    window->renderer = SDL_CreateRenderer(window->handle, "gpu");
    if (!window->renderer) {
        fprintf(stderr, "SDL GPU renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window->handle);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        *window = WormWindow{};
        return false;
    }
    if (!SDL_GetGPURendererDevice(window->renderer)) {
        fprintf(stderr, "SDL renderer did not expose a GPU device: %s\n", SDL_GetError());
        SDL_DestroyRenderer(window->renderer);
        SDL_DestroyWindow(window->handle);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        *window = WormWindow{};
        return false;
    }
    if (!SDL_SetRenderVSync(window->renderer, 1)) {
        fprintf(stderr, "SDL GPU renderer VSync setup failed: %s\n", SDL_GetError());
    }

    window->width = desc->width;
    window->height = desc->height;
    return true;
}

void worm_window_destroy(WormWindow *window)
{
    if (!window) {
        return;
    }

    if (window->renderer) {
        SDL_DestroyRenderer(window->renderer);
    }

    if (window->handle) {
        SDL_DestroyWindow(window->handle);
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    *window = WormWindow{};
}

void worm_window_swap(WormWindow *window)
{
    if (window && window->renderer) {
        SDL_RenderPresent(window->renderer);
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
