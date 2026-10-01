#pragma once

#include "ui/frontend.h"

[[nodiscard]] bool worm_ui_nuklear_setup_view_valid(const WormUiSetupView* view);

[[nodiscard]] bool
worm_ui_nuklear_setup_policy_values_valid(WormUiStepmaniaFileProfile file_profile,
                                          WormUiStepmaniaTimingSyntax timing_syntax,
                                          WormUiStepmaniaImplementation implementation);

#include <array>
#include <cstdint>
#include <cstddef>

struct project_chart_selection_t {
    std::uint64_t song{};
    std::uint64_t session_generation{};
    std::array<std::uint64_t, WORM_UI_PROJECT_CHART_CAPACITY> ids{};
    std::size_t count{};
    std::uint64_t pending_delete{};
    std::uint64_t pending_delete_revision{};
};
void worm_ui_nuklear_reconcile_project_selection(project_chart_selection_t& state,
    const WormUiSetupView& view);
bool worm_ui_nuklear_project_chart_selected(const project_chart_selection_t& state, std::uint64_t id);
void worm_ui_nuklear_toggle_project_chart_selection(project_chart_selection_t& state, std::uint64_t id);
