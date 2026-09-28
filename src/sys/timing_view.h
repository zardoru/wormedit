#pragma once

#include "sys/workspace_projection.h"

#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/sv_recipe.h>

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

struct timing_speed_cache_entry_t {
    std::uint64_t object_id{};
    std::uint64_t source_order{};
    std::uint64_t storage_order{};
    long double marker_row{};
    long double affected_begin_row{};
    long double affected_end_row{};
    double multiplier{};
    double transition_duration{};
    bool transition_is_seconds{};
    bool transition_clipped{};
};

struct timing_scene_cache_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    bool truncated{};
    std::uint64_t total_rows{};
    std::vector<timing_speed_cache_entry_t> speeds;
    std::string status;
};

void populate_timing_view(flattened_workspace_t& flattened,
                          const wormedit::document_history_t& history,
                          const wormedit::workspace_controller_t& workspace,
                          timing_scene_cache_t& cache, int window_width, int window_height,
                          const timing_grid_state_t& timing_grid,
                          const wormedit::timing_lane_pseudonote_preview_t* stage,
                          const wormedit::sv_recipe_draft_t* recipe_draft,
                          const wormedit::sv_recipe_draft_preview_t* recipe_preview,
                          bool recipe_selection_armed);

} // namespace wormedit::sys
