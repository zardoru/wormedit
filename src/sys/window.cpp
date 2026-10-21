#include "wormedit/window.h"
#include "ui/frontend/actions.h"
#include "sys/frame_timing.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <stdio.h>

namespace {

#if !defined(__APPLE__)
SDL_HitTestResult SDLCALL window_hit_test(SDL_Window* window, const SDL_Point* point, void*) {
    int width{};
    int height{};
    if (!point || !SDL_GetWindowSize(window, &width, &height)) return SDL_HITTEST_NORMAL;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) == 0) {
        constexpr int edge = 6;
        const bool left = point->x < edge;
        const bool right = point->x >= width - edge;
        const bool top = point->y < edge;
        const bool bottom = point->y >= height - edge;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    }
    // Drag only the bare title bar: the menu buttons on the left and the window
    // controls on the right must receive their own clicks.
    if (worm_ui_frontend_is_title_bar_drag_position(
            static_cast<float>(point->x), static_cast<float>(point->y), width))
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}
#endif

} // namespace

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
    SDL_SetWindowMinimumSize(window->handle, 800, 560);

#if !defined(__APPLE__)
    if (SDL_SetWindowHitTest(window->handle, window_hit_test, nullptr) &&
        !SDL_SetWindowBordered(window->handle, false)) {
        SDL_SetWindowHitTest(window->handle, nullptr, nullptr);
    }
#endif

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
    // All application UVs stay within [0, 1], so AUTO would always resolve to
    // CLAMP, but only after scanning every submitted vertex's UV per draw call.
    // The mode is renderer state and survives render-target switches.
    if (!SDL_SetRenderTextureAddressMode(window->renderer, SDL_TEXTURE_ADDRESS_CLAMP,
                                         SDL_TEXTURE_ADDRESS_CLAMP)) {
        fprintf(stderr, "SDL texture address mode setup failed: %s\n", SDL_GetError());
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
        const wormedit::sys::frame_phase_scope_t present_phase{wormedit::sys::frame_phase_t::render_present};
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
