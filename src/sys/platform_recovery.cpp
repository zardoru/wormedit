#include "sys/platform_recovery.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <system_error>

namespace wormedit {
namespace {

void post_recovery_directory_error(const std::optional<notification_sink_t>& notifications,
                                  const std::string& message) {
    if (!notifications || message.empty()) return;
    notifications->post(notification_severity_t::error, notification_source_t::recovery, message);
}

} // namespace

auto prepare_platform_recovery_directory(std::optional<notification_sink_t> notifications)
    -> platform_recovery_directory_result_t {
    platform_recovery_directory_result_t result;
    char* preference_path = SDL_GetPrefPath("otoworld", "wormedit");
    if (preference_path == nullptr) {
        result.error = "SDL could not resolve the per-user application directory";
        post_recovery_directory_error(notifications, result.error);
        return result;
    }
    result.path = std::filesystem::path{preference_path} / "recovery";
    SDL_free(preference_path);

    std::error_code error;
    std::filesystem::create_directories(result.path, error);
    if (error) {
        result.error = "could not create the platform recovery directory: " + error.message();
        result.path.clear();
        post_recovery_directory_error(notifications, result.error);
    }
    return result;
}

} // namespace wormedit
