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

[[nodiscard]] auto open_shortcut_profile(const std::filesystem::path& path)
    -> shortcut_profile_load_outcome_t;

struct shortcut_profile_load_result_t {
    [[nodiscard]] static auto from(shortcut_profile_load_outcome_t&& outcome)
        -> shortcut_profile_load_result_t;

    shortcut_profile_load_status_t status{shortcut_profile_load_status_t::io_error};
    std::optional<shortcut_profile_t> profile;
    diagnostic_list_t diagnostics;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == shortcut_profile_load_status_t::loaded && profile.has_value();
    }
};

enum class shortcut_profile_save_status_t {
    published,
    invalid_profile,
    io_error,
};

using shortcut_profile_save_outcome_t = result_t<void, error_t<shortcut_profile_save_status_t>>;

[[nodiscard]] auto create_shortcut_profile_file(const std::filesystem::path& path,
                                                const shortcut_profile_t& profile)
    -> shortcut_profile_save_outcome_t;

struct shortcut_profile_save_result_t {
    [[nodiscard]] static auto from(shortcut_profile_save_outcome_t&& outcome)
        -> shortcut_profile_save_result_t;

    shortcut_profile_save_status_t status{shortcut_profile_save_status_t::io_error};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == shortcut_profile_save_status_t::published;
    }
};

// Loads only bounded regular files and reports missing, malformed, and I/O
// failures separately. This API does not mutate a command registry; callers
// can apply a successfully loaded profile with apply_shortcut_profile().
[[nodiscard]] auto load_shortcut_profile(const std::filesystem::path& path)
    -> shortcut_profile_load_result_t;

// Serializes and atomically replaces the preference file using a temporary
// file in the destination directory. Failures before publication leave the
// previous destination untouched.
[[nodiscard]] auto save_shortcut_profile_atomic(const std::filesystem::path& path,
                                                const shortcut_profile_t& profile)
    -> shortcut_profile_save_result_t;

} // namespace wormedit
