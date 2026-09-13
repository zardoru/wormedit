#pragma once

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

} // namespace wormedit
