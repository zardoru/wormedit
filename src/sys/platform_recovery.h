#pragma once

#include <filesystem>
#include <optional>
#include <wormedit/core/document.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/result.h>
#include <string>

namespace wormedit {

using platform_recovery_directory_result_t = result_t<std::filesystem::path, std::string>;

// Resolves and creates Wormedit's per-user recovery directory through SDL's
// platform filesystem policy. This remains an application boundary; the
// headless recovery scanner only receives caller-owned paths.
[[nodiscard]] auto prepare_platform_recovery_directory(
    notification_sink_t notifications = notification_sink_t::muted())
    -> platform_recovery_directory_result_t;

// The crash marker lives in the recovery directory; the scanner ignores it as a candidate.
[[nodiscard]] auto crash_marker_path(const std::filesystem::path& recovery_directory)
    -> std::filesystem::path;
// Atomically records this process ID and the project it opened. Returns an empty string on
// success, otherwise a message for a status warning; a marker failure never stops startup.
[[nodiscard]] auto write_crash_marker(const std::filesystem::path& marker, song_id_t project_id)
    -> std::string;
// Removes the marker on clean shutdown. A missing marker is not an error.
void remove_crash_marker(const std::filesystem::path& marker) noexcept;

} // namespace wormedit
