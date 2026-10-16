#pragma once

#include <wormedit/core/operation_state.h>
#include <wormedit/core/shortcut_profile_storage.h>

#include <cstdint>
#include <type_traits>

namespace wormedit {

enum class shortcut_profile_startup_phase_t : std::uint8_t { applied, missing };
static_assert(sizeof(operation_state_t<shortcut_profile_startup_phase_t>) <= 2 * sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<operation_state_t<shortcut_profile_startup_phase_t>>);

inline constexpr std::size_t maximum_shortcut_profile_startup_diagnostics = 8;
inline constexpr std::size_t maximum_shortcut_profile_startup_diagnostic_bytes = 192;

using shortcut_profile_startup_outcome_t = diagnosed_t<operation_state_t<shortcut_profile_startup_phase_t>>;

// Loads and applies a user preference profile at startup. A missing file is a
// normal first-run condition and returns silently; rejected preferences leave
// the registry unchanged. Diagnostics are capped for direct UI presentation.
[[nodiscard]] auto apply_startup_shortcut_profile(const std::filesystem::path& path,
                                                  command_registry_t& registry)
    -> shortcut_profile_startup_outcome_t;
} // namespace wormedit
