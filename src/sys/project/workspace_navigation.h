#pragma once
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/workspace.h>
#include <span>
#include <utility>

namespace wormedit::sys {
// Build the complete view-only jump before publishing either workspace or cursor state.
struct workspace_navigation_t {
    workspace_controller_t workspace;
    editor_interaction_state_t interaction;

    [[nodiscard]] static auto from(document_history_t& history,
        const workspace_controller_t& workspace,
        const std::optional<editor_interaction_state_t>& previous,
        chart_id_t chart_id, local_position_t position) -> std::optional<workspace_navigation_t> {
        const auto* chart = history.document.find_chart(chart_id);
        if (!chart) return std::nullopt;
        auto candidate = previous;
        if (!candidate || candidate->chart != chart_id) {
            const auto* indices = history.indices(chart_id);
            if (!indices || !indices->measures) return std::nullopt;
            const auto* layout = layout_at(*chart, *indices->measures, position);
            const auto lane = layout && !layout->lanes.empty() ? layout->lanes.front().id : lane_id_t{};
            candidate = editor_interaction_state_t::from(history, chart_id, lane, position,
                previous ? std::span<const std::uint64_t>{previous->supported_snap_denominators}
                         : std::span<const std::uint64_t>{default_snap_denominators},
                previous ? previous->active_snap_denominator : 16,
                previous ? previous->viewport_height_pixels : 1.0).state;
        }
        auto next = workspace;
        if (!candidate ||
            !apply_workspace_action(next, history.document, workspace_select_chart_action_t{chart_id})
                .is_accepted(diagnostic_acceptance_policy_t::require_empty) ||
            !apply_workspace_action(next, history.document,
                workspace_switch_tab_action_t{workspace_tab_t::compose})
                .is_accepted(diagnostic_acceptance_policy_t::require_empty) ||
            !dispatch_editor_action(history, *candidate,
                jump_edit_cursor_action_t{position, history.document.revision}).accepted)
            return std::nullopt;
        return workspace_navigation_t{std::move(next), std::move(*candidate)};
    }
    void apply(workspace_controller_t& destination,
               std::optional<editor_interaction_state_t>& cursor) && {
        destination = std::move(workspace);
        cursor = std::move(interaction);
    }
};
} // namespace wormedit::sys
