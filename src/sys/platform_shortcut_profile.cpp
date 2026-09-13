#include "sys/platform_shortcut_profile.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

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

} // namespace wormedit
