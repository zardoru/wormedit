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
#include <span>

struct project_chart_selection_t {
    std::uint64_t song{};
    std::uint64_t session_generation{};
    std::array<std::uint64_t, WORM_UI_PROJECT_CHART_CAPACITY> ids{};
    std::size_t count{};
    std::uint64_t pending_delete{};
    std::uint64_t pending_delete_revision{};
    // The row a Shift-click extends from; reset when it leaves the table.
    std::uint64_t anchor{};
};

// Header checkbox state for the displayed rows.
enum class project_selection_summary_t : std::uint8_t { none, partial, all };

void worm_ui_nuklear_reconcile_project_selection(project_chart_selection_t& state,
    const WormUiSetupView& view);
bool worm_ui_nuklear_project_chart_selected(const project_chart_selection_t& state, std::uint64_t id);
void worm_ui_nuklear_toggle_project_chart_selection(project_chart_selection_t& state, std::uint64_t id);
// `shown` lists the table rows in display order. A plain click selects only `id`; Ctrl toggles
// it; Shift replaces the selection with the rows between the anchor and `id`, inclusive. A Shift
// click with no anchor on screen acts as a plain click. Every click moves the anchor to `id`
// except a Shift-click, which keeps the anchor so the range can be extended.
void worm_ui_nuklear_click_project_chart(project_chart_selection_t& state,
    std::span<const std::uint64_t> shown, std::uint64_t id, bool ctrl, bool shift);
// The header checkbox: selects every shown row, or clears the selection when all are selected.
void worm_ui_nuklear_toggle_all_project_charts(project_chart_selection_t& state,
    std::span<const std::uint64_t> shown);
[[nodiscard]] project_selection_summary_t worm_ui_nuklear_project_selection_summary(
    const project_chart_selection_t& state, std::span<const std::uint64_t> shown);
