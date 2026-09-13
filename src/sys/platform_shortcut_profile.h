#pragma once

#include <wormedit/core/shortcut_profile_startup.h>

#include <filesystem>
#include <string>

namespace wormedit {

struct platform_shortcut_profile_path_result_t {
    std::filesystem::path path;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !path.empty();
    }
};

// Resolves the per-user shortcut preference file beside the recovery folder.
// File contents and publication are handled by the headless core storage API.
[[nodiscard]] auto prepare_platform_shortcut_profile_path()
    -> platform_shortcut_profile_path_result_t;

// Resolves the platform preference path and applies the optional profile to
// the startup registry. A missing first-run profile is intentionally silent.
[[nodiscard]] auto load_platform_shortcut_profile(command_registry_t& registry)
    -> shortcut_profile_startup_result_t;

} // namespace wormedit
