#pragma once

#include <filesystem>
#include <optional>
#include <wormedit/core/notifications.h>
#include <string>

namespace wormedit {

struct platform_recovery_directory_result_t {
    std::filesystem::path path;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !path.empty();
    }
};

// Resolves and creates Wormedit's per-user recovery directory through SDL's
// platform filesystem policy. This remains an application boundary; the
// headless recovery scanner only receives caller-owned paths.
[[nodiscard]] auto prepare_platform_recovery_directory(
    std::optional<notification_sink_t> notifications = {}) -> platform_recovery_directory_result_t;

} // namespace wormedit
