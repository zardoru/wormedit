#pragma once

#include "sys/compose_view.h"

#include <wormedit/core/chart_compare.h>

#include <algorithm>
#include <string>
#include <span>
#include <vector>

namespace wormedit::sys {

struct compare_scene_cache_t {
    wormedit::workspace_bms_branch_outline_cache_t branch_outline;
    compose_scene_cache_t first_scene;
    compose_scene_cache_t second_scene;
    std::optional<wormedit::editor_interaction_state_t> first_state;
    std::optional<wormedit::editor_interaction_state_t> second_state;
    wormedit::chart_id_t first_chart;
    wormedit::chart_id_t second_chart;
    wormedit::song_id_t song;
    std::uint64_t revision{};
    double shared_scroll_measures{};
    double pixels_per_measure{wormedit::default_pixels_per_standard_measure};
    std::vector<std::uint64_t> chart_ids;
    std::vector<std::string> chart_names;
    std::vector<const char*> chart_name_pointers;
    std::uint64_t diagnostics_revision{};
    std::uint64_t diagnostics_projection_revision{};
    wormedit::song_id_t diagnostics_song;
    wormedit::chart_id_t diagnostics_chart;
    wormedit::diagnostic_list_t cached_diagnostics;
    std::vector<std::string> diagnostic_labels;
    std::vector<const char*> diagnostic_label_pointers;
    std::vector<std::uint64_t> diagnostic_rows;
    std::vector<std::uint64_t> diagnostic_measures;
    std::vector<std::uint8_t> diagnostic_has_rows;
};

void cache_compare_diagnostics(compare_scene_cache_t& cache,
                               const wormedit::song_document_t& document);
[[nodiscard]] auto build_workspace_frame_model(
    compare_scene_cache_t& cache, const wormedit::workspace_controller_t& workspace,
    const wormedit::song_document_t& document) -> wormedit::workspace_view_result_t;

[[nodiscard]] inline auto choose_compare_chart_pair(
    std::span<const wormedit::chart_id_t> available,
    std::optional<wormedit::chart_id_t> active,
    std::span<const wormedit::chart_id_t> comparison)
    -> std::optional<std::pair<wormedit::chart_id_t, wormedit::chart_id_t>> {
    if (available.size() < 2) return std::nullopt;
    auto first = active && std::ranges::find(available, *active) != available.end()
                     ? *active
                     : (!comparison.empty() ? comparison.front() : available.front());
    if (std::ranges::find(available, first) == available.end()) first = available.front();
    auto second = std::ranges::find_if(comparison, [first](auto chart) {
        return chart != first;
    });
    if (second != comparison.end() && std::ranges::find(available, *second) != available.end())
        return std::pair{first, *second};
    const auto other = std::ranges::find_if(available, [first](auto chart) {
        return chart != first;
    });
    if (other == available.end()) return std::nullopt;
    return std::pair{first, *other};
}

[[nodiscard]] auto compare_local_difference_positions(
    const wormedit::chart_compare_result_t& comparison, wormedit::chart_id_t chart)
    -> std::vector<wormedit::local_position_t>;

void populate_compare_view(flattened_workspace_t& result,
                           flattened_workspace_t& first_pane,
                           flattened_workspace_t& second_pane,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compare_scene_cache_t& cache,
                           application_command_state_t& application_state,
                           int window_width, int window_height);

} // namespace wormedit::sys
