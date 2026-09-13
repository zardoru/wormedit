#pragma once

#include <wormedit/formats/native_project_storage.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace wormedit {

struct native_project_startup_request_t {
    std::filesystem::path primary_path;
    std::vector<std::filesystem::path> recovery_entries;
    std::vector<std::filesystem::path> recovery_directories;
    native_project_recovery_scan_options_t scan_options{};
};

struct native_project_startup_result_t {
    song_document_t document;
    bool recovery_scan_configured{};
    bool loaded_primary{};
    std::optional<native_project_document_version_t> primary_version;
    native_project_recovery_scan_result_t recovery_scan;
};

// Scans the caller-selected project and recovery artifacts once. A valid
// primary becomes the saved startup document; otherwise the caller's fallback
// remains loaded while the completed scan is retained for explicit recovery.
[[nodiscard]] auto prepare_native_project_startup(song_document_t fallback,
                                                  native_project_startup_request_t request = {})
    -> native_project_startup_result_t;

} // namespace wormedit
