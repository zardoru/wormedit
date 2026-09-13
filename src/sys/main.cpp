#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "ui/frontend.h"
#include "wormedit/config.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <stdio.h>

int main() {
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

    if (!worm_ui_frontend_init(window.handle)) {
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;

        worm_ui_frontend_begin_input();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }

            worm_ui_frontend_handle_event(&event);
        }
        worm_ui_frontend_end_input();

        const WormUiScaffoldView view = {
            "Audio import subsystem placeholder ready",
            WORMEDIT_GIT_HASH,
        };
        switch (worm_ui_frontend_compose(&view)) {
        case WORM_UI_SCAFFOLD_ACTION_PROBE_AUDIO: {
            const WormAudioImportRequest request = {"placeholder.wav"};
            printf("audio probe: %s\n", worm_audio_import_probe(&request) ? "ok" : "failed");
            break;
        }
        case WORM_UI_SCAFFOLD_ACTION_QUIT:
            running = false;
            break;
        case WORM_UI_SCAFFOLD_ACTION_NONE:
            break;
        }

        int width = 0;
        int height = 0;
        worm_window_get_size(&window, &width, &height);
        worm_ui_frontend_render(width, height);
        worm_window_swap(&window);

        SDL_Delay(1);
    }

    worm_ui_frontend_shutdown();
    worm_window_destroy(&window);
    worm_audio_shutdown(&audio);
    SDL_Quit();
    return 0;
}
