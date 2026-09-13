#include "sys/platform_shortcut_profile.h"

#include <wormedit/core/shortcut_profile_startup.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <utility>

namespace wormedit {

auto prepare_platform_shortcut_profile_path() -> platform_shortcut_profile_path_result_t {
    platform_shortcut_profile_path_result_t result;
    char* preference_path = SDL_GetPrefPath("otoworld", "wormedit");
    if (preference_path == nullptr) {
        result.error = "SDL could not resolve the per-user application directory";
        return result;
    }
    result.path = std::filesystem::path{preference_path} / "shortcuts.json";
    SDL_free(preference_path);
    return result;
}

auto load_platform_shortcut_profile(command_registry_t& registry)
    -> shortcut_profile_startup_result_t {
    const auto preference_path = prepare_platform_shortcut_profile_path();
    if (!preference_path) {
        auto message = preference_path.error.empty()
                           ? std::string{"SDL could not resolve the shortcut preference path"}
                           : preference_path.error;
        if (message.size() > maximum_shortcut_profile_startup_diagnostic_bytes) {
            message.resize(maximum_shortcut_profile_startup_diagnostic_bytes - 3);
            message += "...";
        }
        return {
            .status = shortcut_profile_startup_status_t::unavailable,
            .diagnostics = {{.severity = diagnostic_severity_t::warning,
                             .code = diagnostic_code_t::invalid_binding,
                             .message = std::move(message)}},
        };
    }
    return load_shortcut_profile_for_startup(preference_path.path, registry);
}

} // namespace wormedit
