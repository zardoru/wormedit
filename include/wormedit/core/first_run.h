#pragma once

#include <wormedit/core/preferences.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

// First-run steps are asked once, in table order, and remembered by id in the preferences.
// Adding a step (for example the interface language) means appending a row here and a
// presentation for its id in the frontend; nothing else in the mechanism changes.
struct first_run_step_t {
    std::string_view id;
};
inline constexpr std::array first_run_steps{
    first_run_step_t{"updates"},
};

// The first step in table order that has not been completed, or empty. `unavailable` lists
// steps this build cannot present (for example the update question in the browser build).
[[nodiscard]] auto next_first_run_step(const first_run_preferences_t&,
                                       std::span<const std::string_view> unavailable = {}) -> std::string_view;
// Returns false for an id that is not in the table or was already completed.
auto complete_first_run_step(first_run_preferences_t&, std::string_view id) -> bool;

} // namespace wormedit
