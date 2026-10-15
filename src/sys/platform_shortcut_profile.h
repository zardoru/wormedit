#pragma once

#include <wormedit/core/shortcut_profile_startup.h>

#include <filesystem>
#include <string>

namespace wormedit {

using platform_shortcut_profile_path_result_t = result_t<std::filesystem::path, std::string>;

// Resolves the per-user shortcut preference file beside the recovery folder.
// File contents and publication are handled by the headless core storage API.
[[nodiscard]] auto prepare_platform_shortcut_profile_path()
    -> platform_shortcut_profile_path_result_t;

// Resolves the platform preference path and applies the optional profile to
// the startup registry. A missing first-run profile is intentionally silent.
[[nodiscard]] auto load_platform_shortcut_profile(command_registry_t& registry)
    -> shortcut_profile_startup_outcome_t;

} // namespace wormedit
