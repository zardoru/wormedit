#pragma once
#include "sys/compose_view.h"
#include <wormedit/core/timing_lane_controller.h>

namespace wormedit::sys {
auto timing_position_from_fraction(const wormedit::chart_document_t& chart, double fraction,
                                   std::uint64_t active_denominator)
    -> std::optional<wormedit::local_position_t>;
auto timing_lane_kind_from_ui(WormUiTimingLaneKind lane)
    -> std::optional<wormedit::timing_lane_kind_t>;
auto begin_timing_stage(const WormUiWorkspaceAction& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace, bool timing_locked,
                        std::uint64_t active_snap_denominator)
    -> wormedit::timing_lane_pseudonote_preview_t;
auto timing_stage_value(const wormedit::timing_lane_pseudonote_preview_t& preview,
                        const wormedit::chart_document_t& chart,
                        const WormUiWorkspaceAction& action, std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t>;
} // namespace wormedit::sys
