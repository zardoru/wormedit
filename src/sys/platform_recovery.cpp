#include "sys/platform_recovery.h"

#include <wormedit/formats/native_project_storage.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <system_error>
#include <utility>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace wormedit {
namespace {

void post_recovery_directory_error(const notification_sink_t& notifications,
                                   const std::string& message) {
    if (notifications.is_muted() || message.empty())
        return;
    notifications.post(notification_severity_t::error, notification_source_t::recovery, message);
}

auto current_process_id() -> long long {
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

} // namespace

auto crash_marker_path(const std::filesystem::path& recovery_directory) -> std::filesystem::path {
    return recovery_directory / "session.wormedit-crash-marker";
}

auto write_crash_marker(const std::filesystem::path& marker, song_id_t project_id) -> std::string {
    const auto text = "pid=" + std::to_string(current_process_id()) + "\nproject_id=" +
                      std::to_string(project_id.value) + "\n";
    const auto result = create_native_bytes_file(marker, text);
    if (result.value) return {};
    const auto detail = result.diagnostics.empty() ? std::string{"unknown failure"}
                                                   : result.diagnostics.front().message;
    return "could not write the crash marker: " + detail;
}

void remove_crash_marker(const std::filesystem::path& marker) noexcept {
    std::error_code error;
    std::filesystem::remove(marker, error);
}

auto prepare_platform_recovery_directory(notification_sink_t notifications)
    -> platform_recovery_directory_result_t {
    char* preference_path = SDL_GetPrefPath("otoworld", "wormedit");
    if (preference_path == nullptr) {
        std::string message = "SDL could not resolve the per-user application directory";
        post_recovery_directory_error(notifications, message);
        return std::unexpected{std::move(message)};
    }
    auto path = std::filesystem::path{preference_path} / "recovery";
    SDL_free(preference_path);

    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        auto message = "could not create the platform recovery directory: " + error.message();
        post_recovery_directory_error(notifications, message);
        return std::unexpected{std::move(message)};
    }
    return path;
}

} // namespace wormedit
