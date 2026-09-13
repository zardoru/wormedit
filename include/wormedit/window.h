#ifndef WORMEDIT_WINDOW_H
#define WORMEDIT_WINDOW_H

#include <stdbool.h>

#include <SDL3/SDL_video.h>

typedef struct WormWindowDesc {
    const char *title;
    int width;
    int height;
} WormWindowDesc;

typedef struct WormWindow {
    SDL_Window *handle;
    SDL_GLContext gl;
    int width;
    int height;
} WormWindow;

bool worm_window_create(WormWindow *window, const WormWindowDesc *desc);
void worm_window_destroy(WormWindow *window);
void worm_window_swap(WormWindow *window);
void worm_window_get_size(WormWindow *window, int *width, int *height);

#endif
