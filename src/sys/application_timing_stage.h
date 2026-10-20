#pragma once
#include "sys/compose_view.h"
#include <wormedit/core/timing_lane_controller.h>

namespace wormedit::sys {
inline auto validate_timing_stage_binding(const wormedit::ui::workspace_action_t& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    const wormedit::timing_lane_pseudonote_outcome_t& stage) -> std::optional<std::string> {
    const auto* report = stage.report();
    if (!report || workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        report->existing_event.value_or(object_id_t{}) != action.timing_object_id)
        return "Timing event staging expired because its chart or revision changed.";
    const chart_stamp_t action_stamp{
        .document = {.project = history.document.id, .revision = action.timing_document_revision},
        .chart = action.timing_chart_id};
    if (!stage.captured_stamp().is_current(action_stamp.chart, action_stamp.document.revision) ||
        !action_stamp.is_current(history.document, *workspace.selected_chart))
        return stale_action_message("Timing event staging");
    return std::nullopt;
}

auto timing_position_from_fraction(const wormedit::chart_document_t& chart,
                                   const wormedit::chart_indices_t& indices, double fraction,
                                   std::uint64_t active_denominator)
    -> std::optional<wormedit::local_position_t>;
auto begin_timing_stage(const wormedit::ui::workspace_action_t& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace, bool timing_locked,
                        std::uint64_t active_snap_denominator)
    -> wormedit::timing_lane_pseudonote_outcome_t;
auto timing_stage_value(const wormedit::timing_lane_pseudonote_state_t& preview,
                        const wormedit::chart_document_t& chart,
                        const wormedit::chart_indices_t& indices,
                        const wormedit::ui::workspace_action_t& action, std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t>;
} // namespace wormedit::sys
