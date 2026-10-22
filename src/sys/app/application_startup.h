#pragma once

#include "sys/app/platform_recovery.h"

#include <wormedit/core/startup_session.h>
#include <wormedit/audio.h>
#include <wormedit/window.h>

#include <string>

namespace wormedit::sys {

struct application_startup_arguments_t {
    native_project_startup_request_t request;
    std::filesystem::path requested_primary_path;
};

// Returns false after printing the existing usage diagnostic when arguments
// exceed the supported number of startup paths.
[[nodiscard]] auto parse_application_startup_arguments(int argc, char** argv,
                                                       application_startup_arguments_t& result)
    -> bool;

[[nodiscard]] auto select_initial_application_status(
    bool loaded_primary, const std::filesystem::path& requested_primary_path,
    const result_t<std::filesystem::path, std::string>& platform_recovery) -> std::string;

struct application_runtime_t {
    WormAudioSystem audio{};
    WormWindow window{};
    bool sdl_initialized{};
    bool audio_initialized{};
    bool window_initialized{};
    bool frontend_initialized{};
};

// Returns 2 for invalid arguments, 1 for SDL initialization failure, or 0.
[[nodiscard]] auto initialize_application_startup(
    int argc, char** argv, application_startup_arguments_t& arguments,
    application_runtime_t& runtime) -> int;

[[nodiscard]] auto initialize_application_runtime(application_runtime_t& runtime) -> bool;
[[nodiscard]] auto initialize_sdl_runtime(application_runtime_t& runtime) -> bool;
void shutdown_application_runtime(application_runtime_t& runtime);

} // namespace wormedit::sys
