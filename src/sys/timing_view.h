#pragma once

#include "sys/workspace_view.h"

#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/measure.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wormedit::sys {

struct timing_grid_state_t {
    wormedit::chart_id_t chart;
    std::vector<std::uint64_t> supported_denominators;
    std::uint64_t active_denominator{};
    bool active_was_fallback{};
};

struct timing_scene_cache_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    bool truncated{};
    std::uint64_t total_rows{};
    bool compose_rows_attempted{};
    std::uint64_t compose_total_rows{};
    bool measure_attempted{};
    std::optional<wormedit::measure_index_t> measures;
};

void populate_timing_view_for_compose(
    flattened_workspace_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, timing_scene_cache_t& cache,
    int window_width, int window_height,
    const timing_grid_state_t& timing_grid,
    const wormedit::timing_lane_pseudonote_preview_t* stage);

} // namespace wormedit::sys
