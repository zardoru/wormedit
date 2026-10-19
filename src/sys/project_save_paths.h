#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/command.h>
#include <wormedit/formats/native_project_storage.h>

#include <filesystem>
#include <optional>
#include <map>
#include <stop_token>

namespace wormedit::sys {

[[nodiscard]] auto nonportable_project_asset_count(const song_document_t& document) -> std::size_t;

// Scan once per revision; warn on a new project or a transition to external assets.
class project_portability_notice_t {
  public:
    [[nodiscard]] auto update(const song_document_t& document, std::uint64_t session_generation) -> bool;
  private:
    std::optional<std::uint64_t> revision_;
    std::uint64_t session_generation_{};
    std::size_t external_count_{};
};

// Resolve relative asset paths against their current base, then express them
// relative to the destination directory when saving a native project there.
// Absolute and empty paths are preserved.
void rebase_project_paths(song_document_t& document,
                          const std::filesystem::path& old_base,
                          const std::filesystem::path& new_base);

struct project_save_request_t {
    song_document_t document;
    std::filesystem::path source_base;
    std::filesystem::path destination;
    native_project_storage_options_t storage_options;
};

// Partial copy failures retain the saved external references and name each failed file.
struct project_save_report_t {
    std::filesystem::path destination;
    native_project_storage_outcome_t storage;
    std::map<std::filesystem::path, std::filesystem::path> relinks;
    std::vector<std::filesystem::path> copied_files;
    std::vector<error_t<failure_code_t>> copy_errors;

    [[nodiscard]] auto status_message() const -> std::string;
    void publish_paths(document_history_t& history, const std::filesystem::path& old_base) const;
};

// Worker path: write the native snapshot, copy external assets atomically, then persist links.
[[nodiscard]] auto create_project_save(const project_save_request_t& request,
                                      std::stop_token stop = {}) -> project_save_report_t;

} // namespace wormedit::sys
