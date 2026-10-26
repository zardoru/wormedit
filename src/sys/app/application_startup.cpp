#include "sys/app/application_startup.h"
#include "sys/app/display_health.h"
#include "sys/project/project_file_types.h"
#include "core/text_util.h"

#include "ui/frontend.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>

#include <cstdio>

namespace wormedit::sys {

auto parse_application_startup_arguments(int argc, char** argv,
                                         application_startup_arguments_t& result) -> bool {
    constexpr int maximum_startup_paths = 30;
    if (argc > maximum_startup_paths + 1) {
        std::fprintf(stderr, "usage: wormedit [chart-or-project [recovery-artifact ...]]\n");
        return false;
    }
    if (argc > 1) {
        // argv is UTF-8 everywhere: SDL converts the Windows command line (see main.cpp).
        const auto path = text_util::path_from_utf8(argv[1]);
        if (is_supported_project_file_extension(path) && text_util::lowercase_extension(path) != ".wormedit")
            result.chart_import_path = path;
        else result.request.primary_path = path;
    }
    for (int index = 2; index < argc; ++index) {
        result.request.recovery_entries.push_back(text_util::path_from_utf8(argv[index]));
    }
    result.requested_primary_path = result.request.primary_path;
    return true;
}

auto select_initial_application_status(
    bool loaded_primary, const std::filesystem::path& requested_primary_path,
    const result_t<std::filesystem::path, std::string>& platform_recovery) -> std::string {
    std::string status;
    if (loaded_primary) {
        status = "Opened native project: " + requested_primary_path.generic_string();
    } else if (!requested_primary_path.empty()) {
        status = "Primary project was not loaded; review the completed recovery scan.";
    } else {
        status = "Create a project or open a chart to begin.";
    }
    if (!platform_recovery && !platform_recovery.error().empty()) {
        status += " " + platform_recovery.error();
    }
    return status;
}

auto initialize_sdl_runtime(application_runtime_t& runtime) -> bool {
    const auto env = [](const char* name) -> std::string_view {
        const auto* value = SDL_getenv(name);
        return value ? std::string_view{value} : std::string_view{};
    };
    if (const auto drivers = preferred_video_drivers(env("WAYLAND_DISPLAY"), env("SDL_VIDEODRIVER")))
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, drivers->data());
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
