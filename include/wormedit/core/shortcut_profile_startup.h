#pragma once

#include <wormedit/core/shortcut_profile_storage.h>

namespace wormedit {

enum class shortcut_profile_startup_status_t {
    applied,
    missing,
    unavailable,
    rejected,
};

inline constexpr std::size_t maximum_shortcut_profile_startup_diagnostics = 8;
inline constexpr std::size_t maximum_shortcut_profile_startup_diagnostic_bytes = 192;

struct shortcut_profile_startup_result_t {
    shortcut_profile_startup_status_t status{shortcut_profile_startup_status_t::unavailable};
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == shortcut_profile_startup_status_t::applied;
    }
};

// Loads and applies a user preference profile at startup. A missing file is a
// normal first-run condition and returns silently; rejected preferences leave
// the registry unchanged. Diagnostics are capped for direct UI presentation.
[[nodiscard]] auto load_shortcut_profile_for_startup(const std::filesystem::path& path,
                                                     command_registry_t& registry)
    -> shortcut_profile_startup_result_t;

} // namespace wormedit
