#include "wormedit/audio.h"
#include "wormedit/vendorimpl.h"
#include "wormedit/window.h"

#include "wormedit/config.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <stdio.h>

int main()
{
    if (!SDL_Init(0)) {
        fprintf(stderr, "SDL init failed\n");
        return 1;
    }

    WormAudioSystem audio = {};
    if (!worm_audio_init(&audio)) {
        fprintf(stderr, "Audio subsystem init failed\n");
        SDL_Quit();
        return 1;
    }

    WormWindow window = {};
    const WormWindowDesc window_desc = {"wormedit", 1280, 720};

    if (!worm_window_create(&window, &window_desc)) {
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    struct nk_context *nk = worm_vendor_nk_init(window.handle);
    if (!nk) {
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;

        worm_vendor_nk_begin_input();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }

            worm_vendor_nk_handle_event(&event);
        }
        worm_vendor_nk_end_input();

        if (nk_begin(nk, "wormedit", nk_rect(20, 20, 360, 190),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                    NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(nk, 24, 1);
            nk_label(nk, "SDL3 + Nuklear scaffold", NK_TEXT_LEFT);
            nk_label(nk, "Audio import subsystem placeholder ready", NK_TEXT_LEFT);
            nk_label(nk, "Git hash: " WORMEDIT_GIT_HASH, NK_TEXT_LEFT);

            nk_layout_row_dynamic(nk, 30, 2);
            if (nk_button_label(nk, "Probe audio")) {
                const WormAudioImportRequest request = {"placeholder.wav"};
                printf("audio probe: %s\n", worm_audio_import_probe(&request) ? "ok" : "failed");
            }
            if (nk_button_label(nk, "Quit")) {
                running = false;
            }
        }
        nk_end(nk);

        int width = 0;
        int height = 0;
        worm_window_get_size(&window, &width, &height);
        worm_vendor_nk_render(width, height);
        worm_window_swap(&window);

        SDL_Delay(1);
    }

    worm_vendor_nk_shutdown();
    worm_window_destroy(&window);
    worm_audio_shutdown(&audio);
    SDL_Quit();
    return 0;
}
