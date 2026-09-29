#pragma once

#include <wormedit/core/document.h>

#include <filesystem>

namespace wormedit::sys {

// Resolve relative asset paths against their current base, then express them
// relative to the destination directory when saving a native project there.
// Absolute and empty paths are preserved.
void rebase_project_paths(song_document_t& document,
                          const std::filesystem::path& old_base,
                          const std::filesystem::path& new_base);

} // namespace wormedit::sys
