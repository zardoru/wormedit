#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>
#include <wormedit/formats/native_project_storage.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

class project_chart_runtime_t;

struct pending_import_t {
    std::string audio_project_path;
    std::string chart_project_path;
    std::string bms_encoding_path;
};

struct project_identity_t {
    bool session_loaded{};
    bool native_project_loaded{};
    bool fresh_blank_project{};
    pending_import_t pending_import;
    std::filesystem::path active_project_path;
    std::filesystem::path asset_base;
    std::optional<native_project_document_version_t> scanned_primary_version;
    std::filesystem::path external_player_source_path;
};

// Generation changes before any project state, so captured work is stale first.
// The dropped-chart request retains its existing consume-on-dispatch lifetime.
void begin_project_session(project_identity_t& current, project_chart_runtime_t& charts,
                           project_identity_t replacement);

// Match the extensions accepted by open_project_file, case-insensitively.
[[nodiscard]] auto is_supported_project_file_extension(
    const std::filesystem::path& path) -> bool;

struct project_open_result_t {
    [[nodiscard]] auto is_success() const noexcept -> bool { return document.has_value(); }

    std::optional<song_document_t> document;
    std::filesystem::path source_path;
    std::filesystem::path asset_base;
    bool native_project{};
    // A BMS-family source contains non-ASCII bytes without a UTF-8 BOM and
    // needs the caller to choose an encoding before retrying this same path.
    bool needs_encoding_choice{};
    std::string error;
    // Nonfatal osu import quantization and asset-resolution diagnostics.
    std::vector<std::string> warnings;
};

// Refreshes exact audio file availability for documents loaded through startup,
// recovery or session replacement. Decoder support does not affect missing state;
// aliases share the per-asset result, without changing revision/history.
void refresh_project_audio_availability(song_document_t& document,
                                        const std::filesystem::path& asset_base);

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
