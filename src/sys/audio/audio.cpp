#include "wormedit/audio.h"

#include <SDL3/SDL_init.h>

bool worm_audio_init(WormAudioSystem *audio)
{
    if (!audio) {
        return false;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }

    audio->initialized = true;
    return true;
}

void worm_audio_shutdown(WormAudioSystem *audio)
{
    if (!audio || !audio->initialized) {
        return;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio->initialized = false;
}

bool worm_audio_import_probe(const WormAudioImportRequest *request)
{
    return request && request->path && request->path[0] != '\0';
}
