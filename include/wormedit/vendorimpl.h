#ifndef WORMEDIT_VENDORIMPL_H
#define WORMEDIT_VENDORIMPL_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "wormedit/nuklear_config.h"

struct nk_context *worm_vendor_nk_init(SDL_Window *window);
void worm_vendor_nk_shutdown(void);

void worm_vendor_nk_begin_input(void);
bool worm_vendor_nk_handle_event(const SDL_Event *event);
void worm_vendor_nk_end_input(void);

void worm_vendor_nk_render(int window_width, int window_height);

#endif
