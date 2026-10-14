#pragma once

#include <wormedit/core/command_registry.h>
#include <wormedit/core/result.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit {

enum class shortcut_profile_load_status_t {
    loaded,
    missing,
    invalid,
    resource_limit,
    io_error,
};

// A load failure may carry parser diagnostics as well as its storage status.
using shortcut_profile_load_outcome_t =
    result_t<shortcut_profile_t, diagnosed_t<error_t<shortcut_profile_load_status_t>>>;

// Loads bounded regular files, distinguishing missing, malformed, and I/O failures.
// This API does not mutate the command registry.
[[nodiscard]] auto open_shortcut_profile(const std::filesystem::path& path)
    -> shortcut_profile_load_outcome_t;

enum class shortcut_profile_save_status_t {
    published,
    invalid_profile,
    io_error,
};

using shortcut_profile_save_outcome_t = result_t<void, error_t<shortcut_profile_save_status_t>>;

// Serializes and atomically replaces the profile. Before publication, failures
// leave the previous destination untouched.
[[nodiscard]] auto create_shortcut_profile_file(const std::filesystem::path& path,
                                                const shortcut_profile_t& profile)
    -> shortcut_profile_save_outcome_t;

} // namespace wormedit
