#pragma once
#include "ui/nuklear/app/sdl_backend.h"
#include <SDL3/SDL_events.h>

namespace wormedit::ui::chrome {
inline void forward_modal_key(nk_context* context, const SDL_KeyboardEvent& event) {
    SDL_Event copy{};
    copy.type = event.type;
    copy.key = event;
    (void)nk_sdl_handle_event(context, &copy);
}
} // namespace wormedit::ui::chrome
