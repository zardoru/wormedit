#include <wormedit/core/first_run.h>

#include <algorithm>

namespace wormedit {

auto next_first_run_step(const first_run_preferences_t& preferences, std::span<const std::string_view> unavailable)
    -> std::string_view {
    for (const auto& step : first_run_steps)
        if (std::ranges::find(preferences.completed, step.id) == preferences.completed.end() &&
            std::ranges::find(unavailable, step.id) == unavailable.end())
            return step.id;
    return {};
}

auto complete_first_run_step(first_run_preferences_t& preferences, std::string_view id) -> bool {
    const auto known = std::ranges::any_of(first_run_steps, [id](const auto& step) { return step.id == id; });
    if (!known || std::ranges::find(preferences.completed, id) != preferences.completed.end()) return false;
    preferences.completed.emplace_back(id);
    return true;
}

} // namespace wormedit
