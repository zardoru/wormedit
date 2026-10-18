#pragma once

#include <wormedit/core/command_registry.h>
#include <wormedit/core/result.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit {

// A load failure may carry parser diagnostics as well as its storage status.
// Loads bounded regular files, distinguishing missing, malformed, and I/O failures.
// This API does not mutate the command registry.
[[nodiscard]] auto open_shortcut_profile(const std::filesystem::path& path)
    -> result_t<shortcut_profile_t, diagnosed_t<error_t<failure_code_t>>>;

// Serializes and atomically replaces the profile. Before publication, failures
// leave the previous destination untouched.
[[nodiscard]] auto create_shortcut_profile_file(const std::filesystem::path& path,
                                                const shortcut_profile_t& profile)
    -> result_t<void, error_t<failure_code_t>>;

} // namespace wormedit
