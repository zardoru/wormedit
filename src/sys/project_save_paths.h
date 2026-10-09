#pragma once

#include <wormedit/core/document.h>

#include <filesystem>
#include <optional>

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

} // namespace wormedit::sys
