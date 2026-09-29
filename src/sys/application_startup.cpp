#include "sys/application_startup.h"

#include "ui/frontend.h"

#include <SDL3/SDL_init.h>

#include <cstdio>

namespace wormedit::sys {

auto parse_application_startup_arguments(int argc, char** argv,
                                         application_startup_arguments_t& result) -> bool {
    constexpr int maximum_startup_paths = 30;
    if (argc > maximum_startup_paths + 1) {
        std::fprintf(stderr, "usage: wormedit [project.wormedit [recovery-artifact ...]]\n");
        return false;
    }
    if (argc > 1) {
        result.request.primary_path = std::filesystem::path{argv[1]};
    }
    for (int index = 2; index < argc; ++index) {
        result.request.recovery_entries.emplace_back(argv[index]);
    }
    result.requested_primary_path = result.request.primary_path;
    return true;
}

auto select_initial_application_status(
    bool loaded_primary, const std::filesystem::path& requested_primary_path,
    const platform_recovery_directory_result_t& platform_recovery) -> std::string {
    std::string status;
    if (loaded_primary) {
        status = "Opened native project: " + requested_primary_path.generic_string();
    } else if (!requested_primary_path.empty()) {
        status = "Primary project was not loaded; review the completed recovery scan.";
    } else if (platform_recovery) {
        status = "No project loaded; recovery scan complete.";
    } else {
        status = "No project loaded; recovery discovery unavailable.";
    }
    if (!platform_recovery && !platform_recovery.error.empty()) {
        status += " " + platform_recovery.error;
    }
    return status;
}

auto initialize_sdl_runtime(application_runtime_t& runtime) -> bool {
    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL init failed\n");
        return false;
    }
    runtime.sdl_initialized = true;
    return true;
}

auto initialize_application_startup(int argc, char** argv,
                                    application_startup_arguments_t& arguments,
                                    application_runtime_t& runtime) -> int {
    if (!parse_application_startup_arguments(argc, argv, arguments))
        return 2;
    if (!initialize_sdl_runtime(runtime))
        return 1;
    return 0;
}

auto initialize_application_runtime(application_runtime_t& runtime) -> bool {
    if (!worm_audio_init(&runtime.audio)) {
        std::fprintf(stderr, "Audio subsystem init failed\n");
        SDL_Quit();
        runtime.sdl_initialized = false;
        return false;
    }
    runtime.audio_initialized = true;
    const WormWindowDesc window_desc{"wormedit", 1280, 720};
    if (!worm_window_create(&runtime.window, &window_desc)) {
        worm_audio_shutdown(&runtime.audio);
        runtime.audio_initialized = false;
        SDL_Quit();
        runtime.sdl_initialized = false;
        return false;
    }
    runtime.window_initialized = true;
    if (!worm_ui_frontend_init(runtime.window.handle, runtime.window.renderer)) {
        worm_window_destroy(&runtime.window);
        runtime.window_initialized = false;
        worm_audio_shutdown(&runtime.audio);
        runtime.audio_initialized = false;
        SDL_Quit();
        runtime.sdl_initialized = false;
        return false;
    }
    runtime.frontend_initialized = true;
    return true;
}

void shutdown_application_runtime(application_runtime_t& runtime) {
    if (runtime.frontend_initialized) {
        worm_ui_frontend_shutdown();
        runtime.frontend_initialized = false;
    }
    if (runtime.window_initialized) {
        worm_window_destroy(&runtime.window);
        runtime.window_initialized = false;
    }
    if (runtime.audio_initialized) {
        worm_audio_shutdown(&runtime.audio);
        runtime.audio_initialized = false;
    }
    if (runtime.sdl_initialized) {
        SDL_Quit();
        runtime.sdl_initialized = false;
    }
}

} // namespace wormedit::sys
