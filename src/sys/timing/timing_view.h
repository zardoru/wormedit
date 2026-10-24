#pragma once

#include "sys/project/workspace_view.h"
#include "sys/compose/compose_source_key.h"

#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/keyed_cache.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::sys {

// The timing stage keeps a standard grid; a snap the chart cannot represent is one measure.
auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t;

struct timing_grid_state_t {
    wormedit::chart_id_t chart;
    std::vector<std::uint64_t> supported_denominators;
    std::uint64_t active_denominator{};
    bool active_was_fallback{};
};

// Capacity totals follow the same document and authoring Chart identity as the field.
// Full measure geometry is read only for an active stage.
using timing_scene_cache_t = wormedit::keyed_cache_t<compose_scene_source_key_t, std::uint64_t>;

void populate_timing_view_for_compose(
    frontend_workspace_view_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, timing_scene_cache_t& cache,
    int window_width, int window_height,
    const timing_grid_state_t& timing_grid,
    const wormedit::timing_lane_pseudonote_outcome_t* stage);

} // namespace wormedit::sys
