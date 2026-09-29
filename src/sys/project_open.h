#pragma once

#include <wormedit/core/document.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct project_open_result_t {
    std::optional<song_document_t> document;
    std::filesystem::path source_path;
    std::filesystem::path asset_base;
    bool native_project{};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return document.has_value(); }
};

// Reads and parses a selected project or chart without changing the active
// workspace. Only an opened .wormedit is an existing saved native project;
// imported charts are new unsaved sessions rooted at their source directory.
[[nodiscard]] auto open_project_file(const std::filesystem::path& selected_path)
    -> project_open_result_t;

} // namespace wormedit::sys
