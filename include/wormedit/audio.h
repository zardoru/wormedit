#ifndef WORMEDIT_AUDIO_H
#define WORMEDIT_AUDIO_H

#include <stdbool.h>

typedef struct WormAudioSystem {
    bool initialized;
} WormAudioSystem;

typedef struct WormAudioImportRequest {
    const char *path;
} WormAudioImportRequest;

bool worm_audio_init(WormAudioSystem *audio);
void worm_audio_shutdown(WormAudioSystem *audio);

bool worm_audio_import_probe(const WormAudioImportRequest *request);

#endif
