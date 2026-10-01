#pragma once

#include <wormedit/core/document.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

// Match the extensions accepted by open_project_file, case-insensitively.
[[nodiscard]] auto is_supported_project_file_extension(
    const std::filesystem::path& path) -> bool;

struct project_open_result_t {
    std::optional<song_document_t> document;
    std::filesystem::path source_path;
    std::filesystem::path asset_base;
    bool native_project{};
    // A BMS-family source contains non-ASCII bytes without a UTF-8 BOM and
    // needs the caller to choose an encoding before retrying this same path.
    bool needs_encoding_choice{};
    std::string error;
    // Nonfatal asset-resolution conflicts requiring an explicit choice.
    std::vector<std::string> warnings;

    [[nodiscard]] explicit operator bool() const noexcept { return document.has_value(); }
};

// Applies load-time normalization and graph validation before a parsed document
// can replace a session. Repairs change storage only; rejection clears the
// proposed document and leaves its caller's active session untouched.
void validate_opened_document(project_open_result_t& result);

// Reads and parses a selected project or chart without changing the active
// workspace. Only an opened .wormedit is an existing saved native project;
// imported charts are new unsaved sessions rooted at their source directory.
[[nodiscard]] auto open_project_file(
    const std::filesystem::path& selected_path,
    std::optional<text_encoding_t> encoding_override = {})
    -> project_open_result_t;

} // namespace wormedit::sys
